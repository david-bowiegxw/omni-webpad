#include "server.h"
#include "app.h"
#include "platform.h"
#include "sha1.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static server_conn_t *sockets[SERVER_MAX_WS];
static server_conn_t *controller[PAD_MAX_PLAYERS];   // WebSocket that sent the player's latest input
static bool pushed_link;

// With several players each phone holds a seat (= player). A seat follows
// the page's token, so a reload or a Wi-Fi hiccup gets the same player back.
typedef struct {
    uint32_t token;
    server_conn_t *ws;      // NULL: polling client, or held for a vanished one
    uint32_t seen;
    bool used;
} seat_t;
static seat_t seats[PAD_MAX_PLAYERS];
static char scratch[2560];              // single-threaded: one response is built at a time

// ---------------------------------------------------------------- helpers

static const char *reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default: return "Internal Server Error";
    }
}

static bool respond(server_conn_t *c, int status, const char *type, const void *body, unsigned len,
                    bool gzip, bool is_static) {
    char head[224];
    // Only the polling endpoint is hit repeatedly; everything else frees its
    // connection slot straight away.
    bool keep = status == 200 && c->keep_alive;
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%s"
                     "Cache-Control: no-store\r\nConnection: %s\r\n\r\n",
                     status, reason(status), type, len, gzip ? "Content-Encoding: gzip\r\n" : "",
                     keep ? "keep-alive" : "close");
    if (!net_send(c->io, head, (unsigned)n, false)) return false;
    if (len && !net_send(c->io, body, len, is_static)) return false;
    if (!keep) { c->closing = true; net_close(c->io); }
    return true;
}

static bool respond_json(server_conn_t *c, int status, const char *body) {
    return respond(c, status, "application/json", body, (unsigned)strlen(body), false, false);
}

static bool respond_error(server_conn_t *c, int status, const char *message) {
    char body[96];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message);
    return respond_json(c, status, body);
}

// Value of a header (case-insensitive name) inside a NUL terminated request head.
static bool header_value(const char *head, const char *name, char *out, unsigned cap) {
    size_t name_len = strlen(name);
    for (const char *line = strstr(head, "\r\n"); line; line = strstr(line, "\r\n")) {
        line += 2;
        size_t i = 0;
        while (i < name_len && tolower((unsigned char)line[i]) == name[i]) i++;
        if (i != name_len || line[i] != ':') continue;
        const char *value = line + i + 1;
        while (*value == ' ') value++;
        unsigned n = 0;
        while (value[n] && value[n] != '\r' && n + 1 < cap) { out[n] = value[n]; n++; }
        out[n] = 0;
        return true;
    }
    return false;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// URL-decoded query parameter. Returns false when absent or too long.
static bool query_value(const char *query, const char *key, char *out, unsigned cap) {
    size_t key_len = strlen(key);
    for (const char *p = query; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, key_len) != 0 || p[key_len] != '=') continue;
        unsigned n = 0;
        for (p += key_len + 1; *p && *p != '&'; p++) {
            char ch = *p;
            if (ch == '+') ch = ' ';
            else if (ch == '%' && hex(p[1]) >= 0 && hex(p[2]) >= 0) {
                ch = (char)(hex(p[1]) << 4 | hex(p[2]));
                p += 2;
            }
            if (n + 1 >= cap) return false;
            out[n++] = ch;
        }
        out[n] = 0;
        return true;
    }
    return false;
}

static bool query_number(const char *query, const char *key, unsigned long max, unsigned long *out) {
    char text[16], *end;
    if (!query_value(query, key, text, sizeof(text)) || !isdigit((unsigned char)text[0])) return false;
    unsigned long value = strtoul(text, &end, 10);
    if (*end || value > max) return false;
    *out = value;
    return true;
}

static uint32_t query_token(const char *query) {
    char text[12], *end;
    if (!query_value(query, "id", text, sizeof(text)) || !isxdigit((unsigned char)text[0])) return 0;
    unsigned long value = strtoul(text, &end, 16);
    return *end ? 0 : (uint32_t)value;
}

// ---------------------------------------------------------------- players

// Player for a client: everybody shares pad 0 with a single player. With
// several, a known token gets its seat back, anyone else the lowest free one;
// -1 when every seat is taken.
static int8_t seat_claim(server_conn_t *ws, uint32_t token) {
    if (app_players <= 1) return 0;
    uint32_t now = plat_millis();
    for (int8_t i = 0; i < (int8_t)app_players; i++) {
        seat_t *s = &seats[i];
        if (!s->used || !token || s->token != token) continue;
        if (s->ws && s->ws != ws) s->ws->player = -2;   // same phone again: the newer connection wins
        s->ws = ws;
        s->seen = now;
        return i;
    }
    for (int8_t i = 0; i < (int8_t)app_players; i++) {
        seat_t *s = &seats[i];
        if (s->ws || (s->used && now - s->seen <= SERVER_SEAT_HOLD_MS)) continue;
        *s = (seat_t){token, ws, now, true};
        app_release((uint8_t)i);
        app_attach((uint8_t)i, true);
        return i;
    }
    return -1;
}

static void seat_leave(server_conn_t *ws) {
    for (uint8_t i = 0; i < PAD_MAX_PLAYERS; i++) {
        if (seats[i].ws != ws) continue;
        seats[i].ws = NULL;
        seats[i].seen = plat_millis();
        app_release(i);
    }
}

static void seats_expire(void) {
    uint32_t now = plat_millis();
    for (uint8_t i = 0; i < PAD_MAX_PLAYERS; i++) {
        seat_t *s = &seats[i];
        if (!s->used || s->ws || now - s->seen <= SERVER_SEAT_HOLD_MS) continue;
        s->used = false;
        app_attach(i, false);
    }
}

static const pad_feedback_t *feedback_for(int8_t player) {
    static const pad_feedback_t none;
    return player >= 0 && player < PAD_MAX_PLAYERS ? &pad_feedback[player] : &none;
}

// ---------------------------------------------------------------- JSON

static const char *link_name(pad_link_t link) {
    return link == PAD_LINK_BLE_HID ? "ble" : "usb";
}

static unsigned info_json(char *out, unsigned cap) {
    unsigned n = (unsigned)snprintf(
        out, cap, "{\"name\":\"OmniWebPad\",\"version\":\"" WEBPAD_VERSION "\",\"board\":\"%s\","
        "\"mode\":\"%s\",\"link\":%s,\"ssid\":\"%s\",\"players\":%u,\"players_setting\":%u,\"modes\":[",
        plat_board_name(), app_mode ? app_mode->id : "", plat_link_connected() ? "true" : "false",
        app_settings.ssid, app_players, app_settings.players);
    for (unsigned i = 0; i < pad_mode_count && n < cap; i++) {
        const pad_mode_t *m = pad_modes[i];
        n += (unsigned)snprintf(
            out + n, cap - n, "%s{\"id\":\"%s\",\"name\":\"%s\",\"family\":\"%s\",\"hosts\":\"%s\","
            "\"link\":\"%s\",\"available\":%s,\"max_players\":%u}",
            i ? "," : "", m->id, m->name, m->family, m->hosts, link_name(m->link),
            (plat_link_mask() & (1u << m->link)) ? "true" : "false", pad_mode_max_players(m));
    }
    if (n < cap) n += (unsigned)snprintf(out + n, cap - n, "]}");
    return n < cap ? n : cap - 1;
}

static unsigned status_json(char *out, unsigned cap) {
    unsigned n = (unsigned)snprintf(
        out, cap, "{\"version\":\"" WEBPAD_VERSION "\",\"board\":\"%s\",\"mode\":\"%s\",\"link\":%s,"
        "\"uptime_ms\":%lu,\"reports\":%lu,\"players\":%u,\"pads\":[",
        plat_board_name(), app_mode ? app_mode->id : "", plat_link_connected() ? "true" : "false",
        (unsigned long)plat_millis(), (unsigned long)app_reports_sent, app_players);
    for (uint8_t i = 0; i < app_players && n < cap; i++) {
        const pad_state_t *s = app_current_state(i);
        const pad_feedback_t *f = &pad_feedback[i];
        n += (unsigned)snprintf(
            out + n, cap - n, "%s{\"player\":%u,\"attached\":%s,\"buttons\":%lu,\"lx\":%u,\"ly\":%u,\"rx\":%u,"
            "\"ry\":%u,\"lt\":%u,\"rt\":%u,\"rumble\":[%u,%u],\"led\":%u,\"color\":[%u,%u,%u]}",
            i ? "," : "", i + 1, (app_players == 1 || (app_attached & (1u << i))) ? "true" : "false",
            (unsigned long)s->buttons, s->lx, s->ly, s->rx, s->ry, s->lt, s->rt,
            f->rumble_strong, f->rumble_weak, f->led, f->r, f->g, f->b);
    }
    if (n < cap) n += (unsigned)snprintf(out + n, cap - n, "],\"log\":");
    if (n >= cap) return cap - 1;
    n += app_log_json(out + n, cap - n - 2);
    out[n++] = '}';
    out[n] = 0;
    return n;
}

// ---------------------------------------------------------------- HTTP routes

static bool route_state(server_conn_t *c, const char *query) {
    pad_state_t state;
    unsigned long v;
    pad_neutral(&state);
    if (!query_number(query, "m", PAD_MASK_ALL, &v)) return respond_error(c, 400, "invalid input");
    state.buttons = (uint32_t)v;
    static const char *const names[] = {"lx", "ly", "rx", "ry", "lt", "rt"};
    uint8_t *const fields[] = {&state.lx, &state.ly, &state.rx, &state.ry, &state.lt, &state.rt};
    for (unsigned i = 0; i < 6; i++) {
        char probe[8];
        if (!query_value(query, names[i], probe, sizeof(probe))) continue;
        if (!query_number(query, names[i], 255, &v)) return respond_error(c, 400, "invalid input");
        *fields[i] = (uint8_t)v;
    }
    // Without an id (scripts) polling drives player 1 without taking a seat.
    uint32_t token = query_token(query);
    int8_t player = token ? seat_claim(NULL, token) : 0;
    if (player >= 0) {
        app_input((uint8_t)player, &state);
        controller[player] = NULL;
    }
    c->keep_alive = true;
    const pad_feedback_t *f = feedback_for(player);
    char body[112];
    snprintf(body, sizeof(body), "{\"usb\":%s,\"l\":%u,\"r\":%u,\"q\":%lu,\"p\":%d,\"n\":%u}",
             plat_link_connected() ? "true" : "false", f->rumble_strong, f->rumble_weak,
             (unsigned long)f->sequence, player, app_players);
    return respond_json(c, 200, body);
}

static bool route_config(server_conn_t *c, const char *query) {
    app_settings_t next = app_settings;
    char value[APP_PASS_MAX + 2];
    if (query_value(query, "reset", value, sizeof(value)) && strcmp(value, "1") == 0) {
        app_settings_defaults(&next);
    } else {
        // An over-long value fails query_value and must not be silently ignored.
        if (strstr(query, "mode=")) {
            if (!query_value(query, "mode", value, sizeof(next.mode))) return respond_error(c, 400, "unknown mode");
            const pad_mode_t *mode = pad_mode_find(value);
            if (!mode) return respond_error(c, 400, "unknown mode");
            if (!(plat_link_mask() & (1u << mode->link))) return respond_error(c, 400, "mode not supported by this board");
            strcpy(next.mode, value);          // query_value() bounded it
        }
        if (strstr(query, "ssid=")) {
            if (!query_value(query, "ssid", value, sizeof(next.ssid))) return respond_error(c, 400, "invalid ssid");
            strcpy(next.ssid, value);
        }
        if (strstr(query, "players=")) {
            unsigned long players;
            if (!query_number(query, "players", PAD_MAX_PLAYERS, &players) || !players)
                return respond_error(c, 400, "players must be 1-4");
            next.players = (uint8_t)players;
        }
        if (strstr(query, "pass=")) {
            if (!query_value(query, "pass", value, sizeof(next.pass))) return respond_error(c, 400, "invalid password");
            strcpy(next.pass, value);
        }
    }
    if (!app_settings_valid(&next))
        return respond_error(c, 400, "ssid needs 1-32 and password 8-63 plain ASCII characters");
    bool changed = memcmp(&next, &app_settings, sizeof(next)) != 0;
    if (changed) {
        if (!app_settings_save(&next)) return respond_error(c, 500, "could not store settings");
        app_log("config mode=%s players=%u", next.mode, next.players);
        app_release_all();
        app_request_reboot(700);
    }
    return respond_json(c, 200, changed ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":true,\"reboot\":false}");
}

static void status_frame(const server_conn_t *c, uint8_t token, uint8_t *out) {
    const pad_feedback_t *f = feedback_for(c->player);
    out[0] = WS_MSG_STATUS;
    out[1] = token;
    out[2] = plat_link_connected() ? 1 : 0;
    out[3] = f->rumble_strong;
    out[4] = f->rumble_weak;
    out[5] = f->led;
    out[6] = f->r; out[7] = f->g; out[8] = f->b;
    out[9] = c->player >= 0 ? (uint8_t)c->player : 0xff;
    out[10] = app_players;
}

static bool ws_send(server_conn_t *c, uint8_t opcode, const uint8_t *payload, unsigned len) {
    uint8_t frame[2 + 125];
    if (len > 125) return false;
    frame[0] = (uint8_t)(0x80 | opcode);
    frame[1] = (uint8_t)len;
    memcpy(frame + 2, payload, len);
    return net_send(c->io, frame, len + 2, false);
}

static bool send_status(server_conn_t *c, uint8_t token) {
    uint8_t status[WS_STATUS_LEN];
    status_frame(c, token, status);
    c->pushed_player = status[9];
    c->pushed_sequence = feedback_for(c->player)->sequence;
    return ws_send(c, 0x2, status, sizeof(status));
}

static bool route_websocket(server_conn_t *c, const char *head, const char *query) {
    char key[96], accept[32];
    uint8_t digest[20];
    unsigned slot = 0;
    if (!header_value(head, "sec-websocket-key", key, sizeof(key) - 40)) return respond_error(c, 400, "websocket key missing");
    while (slot < SERVER_MAX_WS && sockets[slot]) slot++;
    if (slot == SERVER_MAX_WS) return respond_error(c, 400, "too many clients");
    strcat(key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    sha1((const uint8_t *)key, (unsigned)strlen(key), digest);
    base64_encode(digest, sizeof(digest), accept);
    char head_out[160];
    int n = snprintf(head_out, sizeof(head_out),
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (!net_send(c->io, head_out, (unsigned)n, false)) return false;
    c->websocket = true;
    sockets[slot] = c;
    c->token = query_token(query);
    c->player = seat_claim(c, c->token);
    return send_status(c, 0);
}

static bool route(server_conn_t *c, char *head) {
    char method[8], target[256];
    if (sscanf(head, "%7s %255s", method, target) != 2) return respond_error(c, 400, "malformed request");
    char *query = strchr(target, '?');
    if (query) *query++ = 0;
    else query = target + strlen(target);
    bool get = strcmp(method, "GET") == 0, post = strcmp(method, "POST") == 0;

    if (get && strcmp(target, "/ws") == 0) return route_websocket(c, head, query);
    if (get && (strcmp(target, "/") == 0 || strcmp(target, "/index.html") == 0))
        return respond(c, 200, "text/html; charset=utf-8", page_gz, page_gz_len, true, true);
    if (get && strcmp(target, "/api/info") == 0)
        return respond(c, 200, "application/json", scratch, info_json(scratch, sizeof(scratch)), false, false);
    if (get && strcmp(target, "/api/status") == 0)
        return respond(c, 200, "application/json", scratch, status_json(scratch, sizeof(scratch)), false, false);
    if (get && strcmp(target, "/state") == 0) return route_state(c, query);
    if (strcmp(target, "/api/config") == 0)
        return post ? route_config(c, query) : respond_error(c, 405, "use POST");
    if (!get && !post) return respond_error(c, 405, "method not allowed");
    return respond_error(c, 404, "not found");
}

// Handles one buffered HTTP request if complete. *progress tells the caller
// whether bytes were consumed.
static bool process_http(server_conn_t *c, bool *progress) {
    c->buffer[c->used] = 0;
    char *end = strstr((char *)c->buffer, "\r\n\r\n");
    if (!end) return c->used < SERVER_BUFFER;          // head larger than the buffer: drop
    unsigned head_len = (unsigned)(end + 4 - (char *)c->buffer);
    char number[12];
    unsigned long body_len = 0;
    *end = 0;
    if (header_value((char *)c->buffer, "content-length", number, sizeof(number))) body_len = strtoul(number, NULL, 10);
    if (body_len > SERVER_BUFFER - head_len) { respond_error(c, 413, "request too large"); return true; }
    if (c->used < head_len + body_len) { *end = '\r'; return true; }
    c->keep_alive = false;
    if (!route(c, (char *)c->buffer)) return false;
    unsigned consumed = head_len + (unsigned)body_len;
    memmove(c->buffer, c->buffer + consumed, c->used - consumed);
    c->used -= (uint16_t)consumed;
    *progress = true;
    return true;
}

// ---------------------------------------------------------------- WebSocket

static int16_t get_s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static bool ws_message(server_conn_t *c, uint8_t opcode, const uint8_t *data, unsigned len) {
    if (opcode == 0x8) {                               // close: echo and finish
        ws_send(c, 0x8, data, len < 2 ? len : 2);
        c->closing = true;
        net_close(c->io);
        return true;
    }
    if (opcode == 0x9) return ws_send(c, 0xa, data, len);
    if (opcode != 0x2 || !len) return true;            // text and pong frames are ignored
    if (data[0] == WS_MSG_PING && len >= 2) return send_status(c, data[1]);
    if (data[0] == WS_MSG_STATE && (len == WS_STATE_LEN || len == WS_STATE_MOTION_LEN)) {
        pad_state_t state;
        pad_neutral(&state);
        state.buttons = (uint32_t)data[2] | (uint32_t)data[3] << 8 | (uint32_t)data[4] << 16 | (uint32_t)data[5] << 24;
        state.lx = data[6]; state.ly = data[7]; state.rx = data[8]; state.ry = data[9];
        state.lt = data[10]; state.rt = data[11];
        if (len == WS_STATE_MOTION_LEN) {
            state.has_motion = true;
            for (unsigned i = 0; i < 3; i++) {
                state.accel[i] = get_s16(data + 12 + 2 * i);
                state.gyro[i] = get_s16(data + 18 + 2 * i);
            }
        }
        if (c->player >= 0) {
            controller[c->player] = c;
            app_input((uint8_t)c->player, &state);
        }
    }
    return true;
}

static bool process_ws(server_conn_t *c, bool *progress) {
    while (c->used >= 2 && !c->closing) {
        uint8_t *b = c->buffer;
        unsigned len = b[1] & 0x7f, head = 2;
        if (!(b[0] & 0x80) || !(b[1] & 0x80) || len == 127) return false;   // fragmented, unmasked or huge
        if (len == 126) {
            if (c->used < 4) break;
            len = (unsigned)b[2] << 8 | b[3];
            head = 4;
        }
        if (head + 4 + len > SERVER_BUFFER) return false;
        if (c->used < head + 4 + len) break;
        uint8_t *payload = b + head + 4;
        for (unsigned i = 0; i < len; i++) payload[i] ^= b[head + (i & 3)];
        if (!ws_message(c, b[0] & 0x0f, payload, len)) return false;
        unsigned consumed = head + 4 + len;
        memmove(b, b + consumed, c->used - consumed);
        c->used -= (uint16_t)consumed;
        *progress = true;
    }
    return true;
}

// ---------------------------------------------------------------- lifecycle

void server_open(server_conn_t *conn, void *io) {
    memset(conn, 0, offsetof(server_conn_t, buffer));
    conn->io = io;
    conn->player = -1;
}

bool server_feed(server_conn_t *conn, const uint8_t *data, unsigned len) {
    while (!conn->closing) {
        unsigned room = SERVER_BUFFER - conn->used, take = len < room ? len : room;
        memcpy(conn->buffer + conn->used, data, take);
        conn->used += (uint16_t)take;
        data += take;
        len -= take;
        bool progress = true;
        while (progress && conn->used && !conn->closing) {
            progress = false;
            if (!(conn->websocket ? process_ws(conn, &progress) : process_http(conn, &progress))) return false;
        }
        if (!len) break;
        if (conn->used == SERVER_BUFFER) return false;   // no room and nothing parseable
    }
    return true;
}

void server_closed(server_conn_t *conn) {
    for (unsigned i = 0; i < SERVER_MAX_WS; i++)
        if (sockets[i] == conn) sockets[i] = NULL;
    seat_leave(conn);
    for (uint8_t i = 0; i < PAD_MAX_PLAYERS; i++) {
        if (controller[i] != conn) continue;
        controller[i] = NULL;
        app_release(i);
    }
}

void server_poll(void) {
    seats_expire();
    bool link = plat_link_connected(), link_changed = link != pushed_link;
    pushed_link = link;
    for (unsigned i = 0; i < SERVER_MAX_WS; i++) {
        server_conn_t *c = sockets[i];
        if (!c || c->closing) continue;
        if (c->player == -1) c->player = seat_claim(c, c->token);   // a seat came free
        uint8_t player = c->player >= 0 ? (uint8_t)c->player : 0xff;
        if (link_changed || player != c->pushed_player || feedback_for(c->player)->sequence != c->pushed_sequence)
            send_status(c, 0);
    }
}

void server_reset(void) {
    memset(seats, 0, sizeof(seats));
    memset(controller, 0, sizeof(controller));
}
