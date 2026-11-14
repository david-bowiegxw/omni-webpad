// Generic HID gamepad, used over USB and over Bluetooth LE. Button usages
// follow the order Linux and Android expect from an unknown gamepad, so the
// pad maps correctly there without a vendor specific driver.
#include "../mode.h"
#include <string.h>

// Buttons 1..16 -> A B C X Y Z L1 R1 L2 R2 Select Start Home L3 R3 Misc.
#define GENERIC_BODY \
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, \
    0x75, 0x01, 0x95, 0x10, 0x81, 0x02,             /* 16 buttons */ \
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, \
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, /* Hat switch */ \
    0x65, 0x00, 0x35, 0x00, 0x45, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03, /* padding */ \
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, \
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,   /* X Y Z Rz */ \
    0x05, 0x02, 0x09, 0xc5, 0x09, 0xc4, \
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02    /* Brake, Accelerator */

const uint8_t pad_generic_report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, GENERIC_BODY, 0xc0,
};
const uint16_t pad_generic_report_desc_len = sizeof(pad_generic_report_desc);

const uint8_t pad_ble_report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, GENERIC_BODY, 0xc0,
};
const uint16_t pad_ble_report_desc_len = sizeof(pad_ble_report_desc);

void pad_generic_build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    (void)now_ms;
    uint32_t b = in->buttons;
    uint16_t bits = (uint16_t)(
        ((b & PAD_SOUTH) ? 1u << 0 : 0) | ((b & PAD_EAST) ? 1u << 1 : 0) |
        ((b & PAD_WEST) ? 1u << 3 : 0) | ((b & PAD_NORTH) ? 1u << 4 : 0) |
        ((b & PAD_L1) ? 1u << 6 : 0) | ((b & PAD_R1) ? 1u << 7 : 0) |
        ((b & PAD_L2) ? 1u << 8 : 0) | ((b & PAD_R2) ? 1u << 9 : 0) |
        ((b & PAD_SELECT) ? 1u << 10 : 0) | ((b & PAD_START) ? 1u << 11 : 0) |
        ((b & PAD_HOME) ? 1u << 12 : 0) | ((b & PAD_L3) ? 1u << 13 : 0) |
        ((b & PAD_R3) ? 1u << 14 : 0) | ((b & PAD_MISC) ? 1u << 15 : 0));
    pad_put_le16(out, bits);
    out[2] = pad_hat(b);
    out[3] = in->lx; out[4] = in->ly; out[5] = in->rx; out[6] = in->ry;
    out[7] = in->lt; out[8] = in->rt;
}

// 0x1209:0x0001 is the pid.codes identifier reserved for testing. Replace it
// with your own allocation before distributing hardware.
static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x1209, 0x0001, 0x0100, 1, 2, 3)};
static const uint8_t config_desc[] = {USB_HID_CONFIG_DESC(0x80, sizeof(pad_generic_report_desc), 2)};

// Several players: one IN-only HID interface per pad, so every host lists
// them as separate gamepads. The configuration is built for the configured
// player count; the bcdDevice differs from the single pad so hosts do not
// reuse what they cached for it.
static const uint8_t multi_device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x1209, 0x0001, 0x0200, 1, 2, 3)};

static uint16_t multi_config(uint8_t players, uint8_t *buf, uint16_t cap) {
    uint16_t len = (uint16_t)(9 + players * 25u);
    if (len > cap) return 0;
    const uint8_t head[] = {9, 0x02, USB_LE16(len), players, 1, 0, 0x80, 250};
    memcpy(buf, head, sizeof(head));
    for (uint8_t i = 0; i < players; i++) {
        const uint8_t itf[] = {
            9, 0x04, i, 0, 1, 0x03, 0, 0, 0,
            9, 0x21, USB_LE16(0x0111), 0, 1, 0x22, USB_LE16(sizeof(pad_generic_report_desc)),
            7, 0x05, (uint8_t)(0x81 + i), 0x03, USB_LE16(64), 2,
        };
        memcpy(buf + 9 + i * 25u, itf, sizeof(itf));
    }
    return len;
}

const pad_mode_t mode_hid_multi = {
    .id = "hid", .name = "Generic gamepads", .family = "generic",
    .hosts = "PC, Android, Linux, macOS, ChromeOS",
    .link = PAD_LINK_USB_HID,
    .device_desc = multi_device_desc, .config = multi_config,
    .report_desc = pad_generic_report_desc, .report_desc_len = sizeof(pad_generic_report_desc),
    .manufacturer = "OmniWebPad", .product = "OmniWebPad Gamepads", .serial = "0002",
    .report_len = PAD_GENERIC_REPORT_LEN, .interval_ms = 2, .players = PAD_MAX_PLAYERS,
    .build = pad_generic_build,
};

const pad_mode_t mode_hid = {
    .id = "hid", .name = "Generic gamepad", .family = "generic",
    .hosts = "PC, Android, Linux, macOS, ChromeOS",
    .link = PAD_LINK_USB_HID,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_desc = pad_generic_report_desc, .report_desc_len = sizeof(pad_generic_report_desc),
    .manufacturer = "OmniWebPad", .product = "OmniWebPad Gamepad", .serial = "0001",
    .report_len = PAD_GENERIC_REPORT_LEN, .interval_ms = 2,
    .build = pad_generic_build, .multi = &mode_hid_multi,
};

const pad_mode_t mode_ble = {
    .id = "ble", .name = "Bluetooth gamepad", .family = "generic",
    .hosts = "Windows, Android, Linux, Steam Deck, macOS",
    .link = PAD_LINK_BLE_HID,
    .report_desc = pad_ble_report_desc, .report_desc_len = sizeof(pad_ble_report_desc),
    .manufacturer = "OmniWebPad", .product = "OmniWebPad",
    .report_id = 1, .report_len = PAD_GENERIC_REPORT_LEN, .interval_ms = 8,
    .build = pad_generic_build,
};
