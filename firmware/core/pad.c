#include "pad.h"
#include <string.h>

pad_feedback_t pad_feedback;
uint8_t pad_identity[6] = {0x02, 0x57, 0x45, 0x42, 0x50, 0x44};

void pad_neutral(pad_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->lx = state->ly = state->rx = state->ry = 128;
}

void pad_normalize(pad_state_t *state) {
    state->buttons &= PAD_MASK_ALL;
    // A touch screen has digital triggers, a scripted client may send analog
    // ones. Whichever arrives, both representations end up consistent.
    if ((state->buttons & PAD_L2) && !state->lt) state->lt = 255;
    if ((state->buttons & PAD_R2) && !state->rt) state->rt = 255;
    if (state->lt) state->buttons |= PAD_L2;
    if (state->rt) state->buttons |= PAD_R2;
}

static void changed(void) { pad_feedback.sequence++; }

void pad_set_rumble(uint8_t strong, uint8_t weak) {
    if (strong == pad_feedback.rumble_strong && weak == pad_feedback.rumble_weak) return;
    pad_feedback.rumble_strong = strong;
    pad_feedback.rumble_weak = weak;
    changed();
}

void pad_set_led(uint8_t led) {
    if (led == pad_feedback.led) return;
    pad_feedback.led = led;
    changed();
}

void pad_set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (r == pad_feedback.r && g == pad_feedback.g && b == pad_feedback.b) return;
    pad_feedback.r = r;
    pad_feedback.g = g;
    pad_feedback.b = b;
    changed();
}

void pad_feedback_reset(void) {
    uint32_t sequence = pad_feedback.sequence + 1;
    memset(&pad_feedback, 0, sizeof(pad_feedback));
    pad_feedback.sequence = sequence;
}

uint8_t pad_hat(uint32_t buttons) {
    // Opposite directions cancel, as on a physical D-pad.
    int x = ((buttons & PAD_RIGHT) ? 1 : 0) - ((buttons & PAD_LEFT) ? 1 : 0);
    int y = ((buttons & PAD_DOWN) ? 1 : 0) - ((buttons & PAD_UP) ? 1 : 0);
    static const uint8_t table[3][3] = {{7, 0, 1}, {6, 8, 2}, {5, 4, 3}};
    return table[y + 1][x + 1];
}

int16_t pad_axis_s16(uint8_t value, bool invert) {
    int32_t v = value == 128 ? 0 : ((int32_t)value * 65535 + 127) / 255 - 32768;
    if (invert) v = v == -32768 ? 32767 : -v;
    return (int16_t)v;
}
