// Original Xbox "Controller S" (XID). Plugs into an original Xbox through a
// controller-port-to-USB adapter cable; the console needs no authentication.
#include "../mode.h"
#include <string.h>

#define XID_LEN 20

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0110, 0, 0, 0, 0x045e, 0x0289, 0x0121, 0, 0, 0)};
static const uint8_t config_desc[] = {
    9, 0x02, USB_LE16(32), 1, 1, 0, 0x80, 50,
    9, 0x04, 0, 0, 2, 0x58, 0x42, 0x00, 0,
    7, 0x05, 0x81, 0x03, USB_LE16(32), 4,
    7, 0x05, 0x02, 0x03, USB_LE16(32), 4,
};
static const uint8_t xid_desc[] = {0x10, 0x42, 0x00, 0x01, 0x01, 0x02, 0x14, 0x06,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t caps_in[] = {0x00, 0x14, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t caps_out[] = {0x00, 0x06, 0xff, 0xff, 0xff, 0xff};

static uint8_t last[XID_LEN] = {0x00, XID_LEN};

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    (void)now_ms;
    uint32_t b = in->buttons;
    memset(out, 0, XID_LEN);
    out[1] = XID_LEN;
    out[2] = (uint8_t)(((b & PAD_UP) ? 0x01 : 0) | ((b & PAD_DOWN) ? 0x02 : 0) | ((b & PAD_LEFT) ? 0x04 : 0) |
                       ((b & PAD_RIGHT) ? 0x08 : 0) | ((b & PAD_START) ? 0x10 : 0) | ((b & PAD_SELECT) ? 0x20 : 0) |
                       ((b & PAD_L3) ? 0x40 : 0) | ((b & PAD_R3) ? 0x80 : 0));
    // Face and shoulder buttons are pressure sensitive: A B X Y Black White.
    out[4] = (b & PAD_SOUTH) ? 255 : 0;
    out[5] = (b & PAD_EAST) ? 255 : 0;
    out[6] = (b & PAD_WEST) ? 255 : 0;
    out[7] = (b & PAD_NORTH) ? 255 : 0;
    out[8] = (b & PAD_R1) ? 255 : 0;
    out[9] = (b & PAD_L1) ? 255 : 0;
    out[10] = in->lt; out[11] = in->rt;
    pad_put_le16(out + 12, (uint16_t)pad_axis_s16(in->lx, false));
    pad_put_le16(out + 14, (uint16_t)pad_axis_s16(in->ly, true));
    pad_put_le16(out + 16, (uint16_t)pad_axis_s16(in->rx, false));
    pad_put_le16(out + 18, (uint16_t)pad_axis_s16(in->ry, true));
    memcpy(last, out, XID_LEN);
}

static void rumble(const uint8_t *buf, uint16_t len) {
    // 00 06 left(lo hi) right(lo hi); left is the strong motor.
    if (len >= 6 && buf[1] == 0x06) pad_set_rumble(buf[3], buf[5]);
}

static int control_in(const pad_setup_t *s, uint8_t *buf, uint16_t cap) {
    const uint8_t *src = NULL;
    uint16_t size = 0;
    if (s->bmRequestType == 0xc1 && s->bRequest == 0x06 && s->wValue == 0x4200) {
        src = xid_desc; size = sizeof(xid_desc);
    } else if (s->bmRequestType == 0xc1 && s->bRequest == 0x01 && s->wValue == 0x0100) {
        src = caps_in; size = sizeof(caps_in);
    } else if (s->bmRequestType == 0xc1 && s->bRequest == 0x01 && s->wValue == 0x0200) {
        src = caps_out; size = sizeof(caps_out);
    } else if (s->bmRequestType == 0xa1 && s->bRequest == 0x01 && s->wValue == 0x0100) {
        src = last; size = XID_LEN;
    } else {
        return -1;
    }
    if (size > cap) size = cap;
    memcpy(buf, src, size);
    return size;
}

static bool control_out(const pad_setup_t *s, const uint8_t *buf, uint16_t len) {
    if (s->bmRequestType != 0x21 || s->bRequest != 0x09 || s->wValue != 0x0200) return false;
    rumble(buf, len);
    return true;
}

const pad_mode_t mode_xid = {
    .id = "xid", .name = "Xbox (original)", .family = "xbox",
    .hosts = "Original Xbox console",
    .link = PAD_LINK_USB_RAW,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_len = XID_LEN, .interval_ms = 4,
    .build = build, .control_in = control_in, .control_out = control_out, .out = rumble,
};
