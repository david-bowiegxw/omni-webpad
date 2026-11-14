#include "app.h"
#include "platform.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

app_settings_t app_settings;
const pad_mode_t *app_mode;
uint8_t app_players = 1, app_attached;
uint32_t app_reports_sent;

#define QUEUE_LEN 16
typedef struct {
    pad_state_t queue[QUEUE_LEN], current;
    unsigned head, count;
    uint32_t last_input;
    bool have_input;
} player_t;
static player_t players[PAD_MAX_PLAYERS];
static uint32_t reboot_at;
static bool reboot_pending;

#define SETTINGS_MAGIC 0x3150574fu   // "OWP1"
typedef struct {
    uint32_t magic;
    app_settings_t settings;
    uint32_t checksum;
} settings_blob_t;

static uint32_t checksum(const uint8_t *data, unsigned len) {
    uint32_t hash = 2166136261u;   // FNV-1a
    while (len--) hash = (hash ^ *data++) * 16777619u;
    return hash;
}

static bool printable(const char *s, unsigned min, unsigned max) {
    unsigned n = 0;
    for (; s[n]; n++)
        if (s[n] < 0x20 || s[n] > 0x7e || s[n] == '"' || s[n] == '\\') return false;
    return n >= min && n <= max;
}

bool app_settings_valid(const app_settings_t *s) {
    return memchr(s->mode, 0, sizeof(s->mode)) && memchr(s->ssid, 0, sizeof(s->ssid)) &&
           memchr(s->pass, 0, sizeof(s->pass)) && pad_mode_find(s->mode) &&
           s->players >= 1 && s->players <= PAD_MAX_PLAYERS &&
           printable(s->ssid, 1, APP_SSID_MAX) && printable(s->pass, 8, APP_PASS_MAX);
}

void app_settings_defaults(app_settings_t *s) {
    memset(s, 0, sizeof(*s));
    const pad_mode_t *mode = pad_mode_select(NULL, plat_link_mask());
    snprintf(s->mode, sizeof(s->mode), "%s", mode ? mode->id : "");
    snprintf(s->ssid, sizeof(s->ssid), "OmniWebPad-%02X%02X", pad_identity[4], pad_identity[5]);
    snprintf(s->pass, sizeof(s->pass), "omniwebpad");
    s->players = 1;
}

bool app_settings_save(const app_settings_t *s) {
    if (!app_settings_valid(s)) return false;
    settings_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SETTINGS_MAGIC;
    blob.settings = *s;
    blob.checksum = checksum((const uint8_t *)&blob.settings, sizeof(blob.settings));
    return plat_settings_write((const uint8_t *)&blob, sizeof(blob));
}

void app_init(void) {
    settings_blob_t blob;
    app_settings_defaults(&app_settings);
    if (plat_settings_read((uint8_t *)&blob, sizeof(blob)) && blob.magic == SETTINGS_MAGIC) {
        // 0.4 blobs end before `players` (a zero padding byte then).
        const unsigned legacy = offsetof(app_settings_t, players);
        if (blob.checksum != checksum((const uint8_t *)&blob.settings, sizeof(blob.settings)) &&
            blob.settings.players == 0 && blob.checksum == checksum((const uint8_t *)&blob.settings, legacy))
            blob.settings.players = 1;
        else if (blob.checksum != checksum((const uint8_t *)&blob.settings, sizeof(blob.settings)))
            blob.settings.players = 0;   // invalid
        if (app_settings_valid(&blob.settings)) app_settings = blob.settings;
    }
    // A stored personality the board cannot do (other firmware build) falls back.
    const pad_mode_t *base = pad_mode_select(app_settings.mode, plat_link_mask());
    if (base) snprintf(app_settings.mode, sizeof(app_settings.mode), "%s", base->id);
    app_players = 1;
    app_mode = base;
    if (base && app_settings.players > 1 && pad_mode_max_players(base) > 1) {
        app_mode = base->multi;
        app_players = app_settings.players < app_mode->players ? app_settings.players : app_mode->players;
    }
    if (app_mode && app_mode->reset) app_mode->reset();
    app_attached = 0;
    app_release_all();
    app_log("boot %s mode=%s players=%u", plat_board_name(), app_mode ? app_mode->id : "none", app_players);
}

void app_request_reboot(uint32_t delay_ms) {
    reboot_at = plat_millis() + delay_ms;
    reboot_pending = true;
}

void app_poll(void) {
    if (reboot_pending && (int32_t)(plat_millis() - reboot_at) >= 0) {
        reboot_pending = false;
        plat_reboot();
    }
}

static player_t *player_at(uint8_t player) { return player < app_players ? &players[player] : NULL; }

void app_input(uint8_t player, const pad_state_t *state) {
    player_t *p = player_at(player);
    if (!p) return;
    pad_state_t next = *state;
    pad_normalize(&next);
    p->last_input = plat_millis();
    p->have_input = true;
    unsigned tail = (p->head + p->count + QUEUE_LEN - 1) % QUEUE_LEN;
    const pad_state_t *previous = p->count ? &p->queue[tail] : &p->current;
    if (p->count && next.buttons == previous->buttons) {
        p->queue[tail] = next;                 // only analog values moved: keep the newest
        return;
    }
    if (p->count == QUEUE_LEN) {               // link stalled: drop the oldest edge
        p->head = (p->head + 1) % QUEUE_LEN;
        p->count--;
    }
    p->queue[(p->head + p->count++) % QUEUE_LEN] = next;
}

void app_release(uint8_t player) {
    if (player >= PAD_MAX_PLAYERS) return;
    player_t *p = &players[player];
    p->count = 0;
    p->have_input = false;
    pad_neutral(&p->current);
}

void app_release_all(void) {
    for (uint8_t i = 0; i < PAD_MAX_PLAYERS; i++) app_release(i);
}

void app_attach(uint8_t player, bool attached) {
    if (player >= app_players) return;
    uint8_t bit = (uint8_t)(1u << player);
    if (attached == !!(app_attached & bit)) return;
    app_attached = attached ? (uint8_t)(app_attached | bit) : (uint8_t)(app_attached & ~bit);
    if (!attached) app_release(player);
    app_log("player %u %s", player + 1, attached ? "joined" : "left");
}

void app_next_state(uint8_t player, pad_state_t *out) {
    player_t *p = player_at(player);
    if (!p) { pad_neutral(out); return; }
    if (p->have_input && plat_millis() - p->last_input > APP_LEASE_MS) app_release(player);
    if (p->count) {
        p->current = p->queue[p->head];
        p->head = (p->head + 1) % QUEUE_LEN;
        p->count--;
    }
    *out = p->current;
}

const pad_state_t *app_current_state(uint8_t player) {
    return &players[player < PAD_MAX_PLAYERS ? player : 0].current;
}

#define LOG_LINES 12
#define LOG_WIDTH 56
static char log_lines[LOG_LINES][LOG_WIDTH];
static unsigned log_index;

void app_log(const char *fmt, ...) {
    char *line = log_lines[log_index++ % LOG_LINES];
    int n = snprintf(line, LOG_WIDTH, "%lu ", (unsigned long)plat_millis());
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + n, (size_t)(LOG_WIDTH - n), fmt, args);
    va_end(args);
    for (char *p = line; *p; p++)
        if (*p == '"' || *p == '\\' || *p < 0x20) *p = '?';
}

unsigned app_log_json(char *out, unsigned cap) {
    unsigned n = 0, total = log_index < LOG_LINES ? log_index : LOG_LINES;
    if (cap < 3) return 0;
    out[n++] = '[';
    for (unsigned i = 0; i < total; i++) {
        const char *line = log_lines[(log_index - total + i) % LOG_LINES];
        if (n + strlen(line) + 5 > cap) break;
        n += (unsigned)snprintf(out + n, cap - n, "%s\"%s\"", i ? "," : "", line);
    }
    out[n++] = ']';
    return n;
}
