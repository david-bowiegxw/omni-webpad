// HORI Pokken Tournament DX Pro Pad: a licensed wired pad that a Nintendo
// Switch accepts without any handshake ("Pro Controller Wired Communication"
// must be enabled on the console). Also a plain HID gamepad on PCs.
#include "../mode.h"
#include <string.h>

#define SWITCH_LEN 8

static const uint8_t report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,             // Generic Desktop, Game Pad, Application
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x10,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x81, 0x02, //   16 buttons
    0x05, 0x01, 0x25, 0x07, 0x46, 0x3b, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14,
    0x09, 0x39, 0x81, 0x42,                         //   Hat switch
    0x65, 0x00, 0x95, 0x01, 0x81, 0x01,             //   4 bits padding
    0x26, 0xff, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x75, 0x08, 0x95, 0x04, 0x81, 0x02,             //   X, Y, Z, Rz
    0x06, 0x00, 0xff, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,   // vendor byte
    0x0a, 0x21, 0x26, 0x95, 0x08, 0x91, 0x02,       //   8 byte vendor output
    0xc0,
};

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x0f0d, 0x0092, 0x0100, 1, 2, 0)};
static const uint8_t config_desc[] = {USB_HID_CONFIG_DESC(0x80, sizeof(report_desc), 5)};

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    (void)now_ms;
    uint32_t b = in->buttons;
    uint16_t bits = (uint16_t)(
        ((b & PAD_WEST) ? 1u << 0 : 0) | ((b & PAD_SOUTH) ? 1u << 1 : 0) |   // Y, B
        ((b & PAD_EAST) ? 1u << 2 : 0) | ((b & PAD_NORTH) ? 1u << 3 : 0) |   // A, X
        ((b & PAD_L1) ? 1u << 4 : 0) | ((b & PAD_R1) ? 1u << 5 : 0) |
        ((b & PAD_L2) ? 1u << 6 : 0) | ((b & PAD_R2) ? 1u << 7 : 0) |        // ZL, ZR
        ((b & PAD_SELECT) ? 1u << 8 : 0) | ((b & PAD_START) ? 1u << 9 : 0) | // Minus, Plus
        ((b & PAD_L3) ? 1u << 10 : 0) | ((b & PAD_R3) ? 1u << 11 : 0) |
        ((b & PAD_HOME) ? 1u << 12 : 0) | ((b & PAD_MISC) ? 1u << 13 : 0));  // Home, Capture
    pad_put_le16(out, bits);
    out[2] = pad_hat(b);
    out[3] = in->lx; out[4] = in->ly; out[5] = in->rx; out[6] = in->ry;
    out[7] = 0;
}

const pad_mode_t mode_switch = {
    .id = "switch", .name = "Switch wired pad", .family = "switch",
    .hosts = "Nintendo Switch, PC",
    .link = PAD_LINK_USB_HID,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_desc = report_desc, .report_desc_len = sizeof(report_desc),
    .manufacturer = "HORI CO.,LTD.", .product = "POKKEN CONTROLLER",
    .report_len = SWITCH_LEN, .interval_ms = 5,
    .build = build,
};
