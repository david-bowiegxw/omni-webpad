#include "app.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

app_settings_t app_settings;
const pad_mode_t *app_mode;
uint32_t app_reports_sent;

#define QUEUE_LEN 16
static pad_state_t queue[QUEUE_LEN], current;
static unsigned queue_head, queue_count;
static uint32_t last_input, reboot_at;
static bool have_input, reboot_pending;

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
           printable(s->ssid, 1, APP_SSID_MAX) && printable(s->pass, 8, APP_PASS_MAX);
}

void app_settings_defaults(app_settings_t *s) {
    memset(s, 0, sizeof(*s));
    const pad_mode_t *mode = pad_mode_select(NULL, plat_link_mask());
    snprintf(s->mode, sizeof(s->mode), "%s", mode ? mode->id : "");
    snprintf(s->ssid, sizeof(s->ssid), "OmniWebPad-%02X%02X", pad_identity[4], pad_identity[5]);
    snprintf(s->pass, sizeof(s->pass), "omniwebpad");
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
    if (plat_settings_read((uint8_t *)&blob, sizeof(blob)) && blob.magic == SETTINGS_MAGIC &&
        blob.checksum == checksum((const uint8_t *)&blob.settings, sizeof(blob.settings)) &&
        app_settings_valid(&blob.settings))
        app_settings = blob.settings;
    // A stored personality the board cannot do (other firmware build) falls back.
    app_mode = pad_mode_select(app_settings.mode, plat_link_mask());
    if (app_mode) snprintf(app_settings.mode, sizeof(app_settings.mode), "%s", app_mode->id);
    if (app_mode && app_mode->reset) app_mode->reset();
    pad_neutral(&current);
    app_log("boot %s mode=%s", plat_board_name(), app_mode ? app_mode->id : "none");
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

void app_input(const pad_state_t *state) {
    pad_state_t next = *state;
    pad_normalize(&next);
    last_input = plat_millis();
    have_input = true;
    unsigned tail = (queue_head + queue_count + QUEUE_LEN - 1) % QUEUE_LEN;
    const pad_state_t *previous = queue_count ? &queue[tail] : &current;
    if (queue_count && next.buttons == previous->buttons) {
        queue[tail] = next;                    // only analog values moved: keep the newest
        return;
    }
    if (queue_count == QUEUE_LEN) {            // link stalled: drop the oldest edge
        queue_head = (queue_head + 1) % QUEUE_LEN;
        queue_count--;
    }
    queue[(queue_head + queue_count++) % QUEUE_LEN] = next;
}

void app_release(void) {
    queue_count = 0;
    have_input = false;
    pad_neutral(&current);
}

void app_next_state(pad_state_t *out) {
    if (have_input && plat_millis() - last_input > APP_LEASE_MS) app_release();
    if (queue_count) {
        current = queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_LEN;
        queue_count--;
    }
    *out = current;
}

const pad_state_t *app_current_state(void) { return &current; }

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
