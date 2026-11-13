// Transport-agnostic HTTP/1.1 + WebSocket server. A platform accepts TCP
// connections and feeds received bytes in; replies leave through net_send().
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SERVER_BUFFER 1024
#define SERVER_MAX_WS 4

typedef struct server_conn {
    void *io;
    bool websocket;
    bool closing;
    bool keep_alive;
    uint16_t used;
    uint8_t buffer[SERVER_BUFFER + 1];
} server_conn_t;

// The gzip-compressed controller page, generated at build time.
extern const unsigned char page_gz[];
extern const unsigned page_gz_len;

void server_open(server_conn_t *conn, void *io);
// Returns false when the connection should be dropped immediately.
bool server_feed(server_conn_t *conn, const uint8_t *data, unsigned len);
void server_closed(server_conn_t *conn);
void server_poll(void);   // pushes status changes to WebSocket clients

// Wire format, see docs/PROTOCOL.md.
enum { WS_MSG_STATE = 0x01, WS_MSG_PING = 0x02, WS_MSG_STATUS = 0x82 };
#define WS_STATE_LEN 12
#define WS_STATE_MOTION_LEN 24
