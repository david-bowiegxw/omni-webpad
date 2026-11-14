// Microsoft Xbox 360 wired controller (XInput). The native pad of Windows,
// and supported out of the box by Steam, Linux xpad, Android, macOS games and
// most emulators. An Xbox 360 console itself requires a security chip
// handshake and will not accept this device.
#include "../mode.h"
#include <string.h>

#define XINPUT_LEN 20

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0xff, 0xff, 0xff, 0x045e, 0x028e, 0x0114, 1, 2, 3)};
static const uint8_t config_desc[] = {
    9, 0x02, USB_LE16(48), 1, 1, 0, 0xa0, 250,
    9, 0x04, 0, 0, 2, 0xff, 0x5d, 0x01, 0,
    // Vendor descriptor describing the 20 byte IN and 8 byte OUT reports.
    16, 0x21, 0x10, 0x01, 0x01, 0x24, 0x81, 0x14, 0x03, 0x00, 0x03, 0x13, 0x02, 0x00, 0x03, 0x00,
    7, 0x05, 0x81, 0x03, USB_LE16(32), 4,
    7, 0x05, 0x02, 0x03, USB_LE16(32), 8,
};

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    (void)now_ms;
    uint32_t b = in->buttons;
    memset(out, 0, XINPUT_LEN);
    out[0] = 0x00; out[1] = XINPUT_LEN;
    out[2] = (uint8_t)(((b & PAD_UP) ? 0x01 : 0) | ((b & PAD_DOWN) ? 0x02 : 0) | ((b & PAD_LEFT) ? 0x04 : 0) |
                       ((b & PAD_RIGHT) ? 0x08 : 0) | ((b & PAD_START) ? 0x10 : 0) | ((b & PAD_SELECT) ? 0x20 : 0) |
                       ((b & PAD_L3) ? 0x40 : 0) | ((b & PAD_R3) ? 0x80 : 0));
    out[3] = (uint8_t)(((b & PAD_L1) ? 0x01 : 0) | ((b & PAD_R1) ? 0x02 : 0) | ((b & PAD_HOME) ? 0x04 : 0) |
                       ((b & PAD_SOUTH) ? 0x10 : 0) | ((b & PAD_EAST) ? 0x20 : 0) | ((b & PAD_WEST) ? 0x40 : 0) |
                       ((b & PAD_NORTH) ? 0x80 : 0));
    out[4] = in->lt; out[5] = in->rt;
    pad_put_le16(out + 6, (uint16_t)pad_axis_s16(in->lx, false));
    pad_put_le16(out + 8, (uint16_t)pad_axis_s16(in->ly, true));    // up is positive
    pad_put_le16(out + 10, (uint16_t)pad_axis_s16(in->rx, false));
    pad_put_le16(out + 12, (uint16_t)pad_axis_s16(in->ry, true));
}

static void out_packet(const uint8_t *buf, uint16_t len) {
    if (len >= 5 && buf[0] == 0x00 && buf[1] == 0x08) pad_set_rumble(buf[3], buf[4]);
    if (len >= 3 && buf[0] == 0x01 && buf[1] == 0x03) {
        // LED animation index: 2..5 blink then hold player 1..4, 6..9 hold.
        uint8_t pattern = buf[2];
        pad_set_led(pattern >= 2 && pattern <= 9 ? (uint8_t)(1u << ((pattern - 2) & 3)) : 0);
    }
}

const pad_mode_t mode_xinput = {
    .id = "xinput", .name = "Xbox 360", .family = "xbox",
    .hosts = "Windows, Steam, Linux, Android, emulators",
    .link = PAD_LINK_USB_RAW,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .manufacturer = "Microsoft", .product = "Controller", .serial = "08FEC93",
    .report_len = XINPUT_LEN, .interval_ms = 4,
    .build = build, .out = out_packet,
};
