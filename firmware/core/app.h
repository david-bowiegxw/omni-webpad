// Application state: persistent settings, the active personality and the
// queue between the network (input arrives) and the link (reports leave).
#pragma once
#include "mode.h"

#define APP_LEASE_MS 500      // inputs older than this release every control
#define APP_SSID_MAX 32
#define APP_PASS_MAX 63

typedef struct {
    char mode[16];
    char ssid[APP_SSID_MAX + 1];
    char pass[APP_PASS_MAX + 1];
} app_settings_t;

extern app_settings_t app_settings;
extern const pad_mode_t *app_mode;
extern uint32_t app_reports_sent;

void app_init(void);                           // load settings, pick the personality
bool app_settings_valid(const app_settings_t *s);
bool app_settings_save(const app_settings_t *s);
void app_settings_defaults(app_settings_t *s);
void app_request_reboot(uint32_t delay_ms);
void app_poll(void);                           // call from the main loop

void app_input(const pad_state_t *state);      // from the network
void app_release(void);                        // controlling client went away
// For the link: the state to put in the next report. Each queued state is
// returned at least once so short taps are never skipped.
void app_next_state(pad_state_t *out);
const pad_state_t *app_current_state(void);

void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
unsigned app_log_json(char *out, unsigned cap);
