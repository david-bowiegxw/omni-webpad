// Canonical controller state shared by the web UI protocol and every
// controller personality. All fields use the positional ("where is the
// button on the pad") convention, so a personality only has to translate.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WEBPAD_VERSION "0.5.0"
#define PAD_MAX_PLAYERS 4         // pads one board can present at once

enum {
    PAD_SELECT = 1u << 0,   // Select / Share / Create / Back / View / Minus
    PAD_L3     = 1u << 1,
    PAD_R3     = 1u << 2,
    PAD_START  = 1u << 3,   // Start / Options / Menu / Plus
    PAD_UP     = 1u << 4,
    PAD_RIGHT  = 1u << 5,
    PAD_DOWN   = 1u << 6,
    PAD_LEFT   = 1u << 7,
    PAD_L2     = 1u << 8,   // L2 / LT / ZL
    PAD_R2     = 1u << 9,   // R2 / RT / ZR
    PAD_L1     = 1u << 10,  // L1 / LB / L / White
    PAD_R1     = 1u << 11,  // R1 / RB / R / Black
    PAD_NORTH  = 1u << 12,  // Triangle / Xbox Y / Switch X
    PAD_EAST   = 1u << 13,  // Circle   / Xbox B / Switch A
    PAD_SOUTH  = 1u << 14,  // Cross    / Xbox A / Switch B
    PAD_WEST   = 1u << 15,  // Square   / Xbox X / Switch Y
    PAD_HOME   = 1u << 16,  // PS / Guide / Home
    PAD_MISC   = 1u << 17,  // Touchpad click / Capture
    PAD_MASK_ALL = (1u << 18) - 1,
};

typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry;   // 0..255, 128 = centre, +y is down
    uint8_t lt, rt;           // analog triggers, 0..255
    bool has_motion;          // false: personalities report their rest pose
    int16_t accel[3];         // milli-g; x right, y away from player, z out of the face
    int16_t gyro[3];          // 0.1 deg/s around the same axes
} pad_state_t;

// Host -> controller feedback, published to the web UI. One per player.
typedef struct {
    uint8_t rumble_strong, rumble_weak;
    uint8_t led;              // player LED bit mask where the host sets one
    uint8_t r, g, b;          // light bar colour where the host sets one
    uint32_t sequence;        // increments whenever anything above changes
} pad_feedback_t;

extern pad_feedback_t pad_feedback[PAD_MAX_PLAYERS];
extern uint8_t pad_identity[6];   // stable per-board address used by personalities

void pad_neutral(pad_state_t *state);
// Clamp buttons and make digital triggers and analog triggers agree.
void pad_normalize(pad_state_t *state);
// Out-of-range players are ignored.
void pad_set_rumble(uint8_t player, uint8_t strong, uint8_t weak);
void pad_set_led(uint8_t player, uint8_t led);
void pad_set_color(uint8_t player, uint8_t r, uint8_t g, uint8_t b);
void pad_feedback_reset(void);   // every player
// A host asked which controllers are present (wireless receivers). The link
// re-announces the players whose bits are set and clears them.
extern volatile uint8_t pad_presence_requests;
static inline void pad_presence_request(uint8_t player) { if (player < 8) pad_presence_requests |= (uint8_t)(1u << player); }

// Helpers shared by personalities.
uint8_t pad_hat(uint32_t buttons);                 // 0 = up, clockwise, 8 = centred
int16_t pad_axis_s16(uint8_t value, bool invert);  // 0..255 -> -32768..32767
static inline void pad_put_le16(uint8_t *out, uint16_t v) { out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); }
static inline void pad_put_be16(uint8_t *out, uint16_t v) { out[0] = (uint8_t)(v >> 8); out[1] = (uint8_t)v; }
static inline int32_t pad_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
