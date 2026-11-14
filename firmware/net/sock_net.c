#include "sock_net.h"
#include "platform.h"
#include "server.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define MAX_CLIENTS 8
#define IDLE_TIMEOUT_MS 10000
#define UNUSED_TIMEOUT_MS 3000    // connected but never sent a byte (browser preconnect)
#define EVICT_GRACE_MS 300        // a connection this fresh is never evicted
#define LISTEN_BACKLOG 16
#define SEND_TIMEOUT_MS 2000

typedef struct {
    int fd;                       // -1 = free
    bool close_requested, failed, used;
    uint32_t last_activity;
    server_conn_t conn;
} client_t;

static client_t clients[MAX_CLIENTS];
static int listener = -1;
static bool initialised;

static void drop(client_t *c) {
    if (c->fd < 0) return;
    close(c->fd);
    c->fd = -1;
    server_closed(&c->conn);
}

bool net_send(void *io, const void *data, unsigned len, bool is_static) {
    client_t *c = io;
    const uint8_t *p = data;
    uint32_t start = plat_millis();
    (void)is_static;
    while (len && c->fd >= 0 && !c->failed) {
        int n = (int)send(c->fd, p, len, 0);
        if (n > 0) { p += n; len -= (unsigned)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && plat_millis() - start < SEND_TIMEOUT_MS) {
            fd_set writable;
            struct timeval wait = {0, 20000};
            FD_ZERO(&writable);
            FD_SET(c->fd, &writable);
            select(c->fd + 1, NULL, &writable, NULL, &wait);
            continue;
        }
        c->failed = true;         // dropped by the poll loop, not re-entrantly
    }
    return !len;
}

void net_close(void *io) { ((client_t *)io)->close_requested = true; }

bool sock_net_start(uint16_t port, bool loopback_only) {
    struct sockaddr_in addr;
    int yes = 1;
    if (!initialised) {
        for (unsigned i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;
        initialised = true;
    }
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listener, LISTEN_BACKLOG) < 0) {
        close(listener);
        listener = -1;
        return false;
    }
    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

void sock_net_stop(void) {
    for (unsigned i = 0; i < MAX_CLIENTS; i++) drop(&clients[i]);
    if (listener >= 0) close(listener);
    listener = -1;
}

// A free slot, or failing that the quietest plain HTTP connection that has
// been idle past the grace period (browsers park spare sockets). NULL when
// every slot is busy: the new connection then waits in the listen backlog.
static client_t *find_slot(uint32_t now) {
    client_t *stale = NULL;
    for (unsigned i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &clients[i];
        if (c->fd < 0) return c;
        if (c->conn.websocket || now - c->last_activity < EVICT_GRACE_MS) continue;
        if (!stale || (!c->used && stale->used) ||
            (c->used == stale->used && c->last_activity < stale->last_activity)) stale = c;
    }
    return stale;
}

static void accept_client(client_t *c) {
    int fd = accept(listener, NULL, NULL), yes = 1;
    if (fd < 0) return;
    drop(c);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    c->fd = fd;
    c->close_requested = c->failed = c->used = false;
    c->last_activity = plat_millis();
    server_open(&c->conn, c);
}

bool sock_net_poll(unsigned timeout_ms, int extra_fd) {
    fd_set readable;
    struct timeval wait = {(long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000};
    client_t *slot = find_slot(plat_millis());
    bool listening = listener >= 0 && slot;
    int top = listening && listener > extra_fd ? listener : extra_fd;
    FD_ZERO(&readable);
    if (!slot && timeout_ms > EVICT_GRACE_MS / 4) {   // re-check soon: a slot may become evictable
        wait.tv_sec = 0;
        wait.tv_usec = EVICT_GRACE_MS / 4 * 1000;
    }
    if (listening) FD_SET(listener, &readable);
    if (extra_fd >= 0) FD_SET(extra_fd, &readable);
    for (unsigned i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) continue;
        FD_SET(clients[i].fd, &readable);
        if (clients[i].fd > top) top = clients[i].fd;
    }
    if (select(top + 1, &readable, NULL, NULL, &wait) < 0) FD_ZERO(&readable);

    if (listening && FD_ISSET(listener, &readable)) {
        accept_client(slot);
        while ((slot = find_slot(plat_millis())) != NULL) {      // drain the backlog while slots last
            int before = slot->fd;
            accept_client(slot);
            if (slot->fd < 0 || slot->fd == before) break;
        }
    }
    uint32_t now = plat_millis();   // after accepting: no slot may look older than it is
    for (unsigned i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &clients[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, &readable)) {
            uint8_t chunk[512];
            int n = (int)recv(c->fd, chunk, sizeof(chunk), 0);
            if (n > 0) {
                c->last_activity = now;
                c->used = true;
                if (!server_feed(&c->conn, chunk, (unsigned)n)) c->failed = true;
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                c->failed = true;
            }
        }
        uint32_t idle = now - c->last_activity;
        if (c->failed || c->close_requested || idle > IDLE_TIMEOUT_MS || (!c->used && idle > UNUSED_TIMEOUT_MS)) drop(c);
    }
    return extra_fd >= 0 && FD_ISSET(extra_fd, &readable);
}
