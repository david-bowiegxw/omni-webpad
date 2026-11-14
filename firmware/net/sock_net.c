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
#define SEND_TIMEOUT_MS 2000

typedef struct {
    int fd;                       // -1 = free
    bool close_requested, failed;
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
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listener, 4) < 0) {
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

static void accept_client(void) {
    int fd = accept(listener, NULL, NULL), yes = 1;
    if (fd < 0) return;
    client_t *c = NULL, *stale = NULL;
    for (unsigned i = 0; i < MAX_CLIENTS && !c; i++) {
        if (clients[i].fd < 0) c = &clients[i];
        else if (!clients[i].conn.websocket && (!stale || clients[i].last_activity < stale->last_activity)) stale = &clients[i];
    }
    if (!c && stale) {            // browsers park idle connections; a new one matters more
        drop(stale);
        c = stale;
    }
    if (!c) { close(fd); return; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    c->fd = fd;
    c->close_requested = c->failed = false;
    c->last_activity = plat_millis();
    server_open(&c->conn, c);
}

bool sock_net_poll(unsigned timeout_ms, int extra_fd) {
    fd_set readable;
    struct timeval wait = {(long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000};
    int top = listener > extra_fd ? listener : extra_fd;
    FD_ZERO(&readable);
    if (listener >= 0) FD_SET(listener, &readable);
    if (extra_fd >= 0) FD_SET(extra_fd, &readable);
    for (unsigned i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) continue;
        FD_SET(clients[i].fd, &readable);
        if (clients[i].fd > top) top = clients[i].fd;
    }
    if (select(top + 1, &readable, NULL, NULL, &wait) < 0) FD_ZERO(&readable);

    uint32_t now = plat_millis();
    if (listener >= 0 && FD_ISSET(listener, &readable)) accept_client();
    for (unsigned i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &clients[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, &readable)) {
            uint8_t chunk[512];
            int n = (int)recv(c->fd, chunk, sizeof(chunk), 0);
            if (n > 0) {
                c->last_activity = now;
                if (!server_feed(&c->conn, chunk, (unsigned)n)) c->failed = true;
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                c->failed = true;
            }
        }
        if (c->failed || c->close_requested || now - c->last_activity > IDLE_TIMEOUT_MS) drop(c);
    }
    return extra_fd >= 0 && FD_ISSET(extra_fd, &readable);
}
