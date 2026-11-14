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
    uint8_t players;          // wanted pads, 1..PAD_MAX_PLAYERS (sits in what was padding)
} app_settings_t;

extern app_settings_t app_settings;
extern const pad_mode_t *app_mode;     // the multi variant when several players are on
extern uint8_t app_players;            // pads served now, 1..PAD_MAX_PLAYERS
extern uint8_t app_attached;           // bit n: a client holds player n
extern uint32_t app_reports_sent;

void app_init(void);                           // load settings, pick the personality
bool app_settings_valid(const app_settings_t *s);
bool app_settings_save(const app_settings_t *s);
void app_settings_defaults(app_settings_t *s);
void app_request_reboot(uint32_t delay_ms);
void app_poll(void);                           // call from the main loop

// Inputs for a player outside 0..app_players-1 are ignored.
void app_input(uint8_t player, const pad_state_t *state);   // from the network
void app_release(uint8_t player);              // its client went away
void app_release_all(void);
void app_attach(uint8_t player, bool attached);
// For the link: the state to put in the player's next report. Each queued
// state is returned at least once so short taps are never skipped.
void app_next_state(uint8_t player, pad_state_t *out);
const pad_state_t *app_current_state(uint8_t player);

void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
unsigned app_log_json(char *out, unsigned cap);
