// Sony DualShock 4 (CUH-ZCT1). Recognised natively by Windows/Steam, Linux
// hid-playstation, macOS and Android. A PS4 console also enumerates it, but
// drops unlicensed pads after about eight minutes because this firmware does
// not (and cannot) answer the console's signed authentication challenge.
#include "../mode.h"
#include <string.h>

#define DS4_LEN 63

static const uint8_t report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,             // Generic Desktop, Game Pad, Application
    0x85, 0x01,                                     //   Report ID 1
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, //   X, Y, Z, Rz
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01,
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, //   Hat switch
    0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0e, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x0e, 0x81, 0x02,             //   14 buttons
    0x06, 0x00, 0xff, 0x09, 0x20, 0x75, 0x06, 0x95, 0x01, 0x15, 0x00, 0x25, 0x7f,
    0x81, 0x02,                                     //   6-bit report counter
    0x05, 0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,             //   Rx, Ry (triggers)
    0x06, 0x00, 0xff, 0x09, 0x21, 0x95, 0x36, 0x81, 0x02,   // 54 vendor bytes
    0x85, 0x05, 0x09, 0x22, 0x95, 0x1f, 0x91, 0x02, //   Output 0x05, 31 bytes
    0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xb1, 0x02, //   Feature 0x02 calibration
    0x85, 0xa3, 0x09, 0x25, 0x95, 0x30, 0xb1, 0x02, //   Feature 0xA3 firmware
    0x06, 0x02, 0xff, 0x85, 0x12, 0x09, 0x21, 0x95, 0x0f, 0xb1, 0x02,   // Feature 0x12 pairing
    0x06, 0x80, 0xff, 0x85, 0x81, 0x09, 0x21, 0x95, 0x06, 0xb1, 0x02,   // Feature 0x81 address
    0xc0,
};

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x054c, 0x05c4, 0x0100, 1, 2, 0)};
static const uint8_t config_desc[] = {USB_HID_CONFIG_DESC(0xc0, sizeof(report_desc), 4)};

static uint8_t counter;

// Raw IMU scale advertised by the calibration report below.
#define GYRO_LSB_PER_DPS 16
#define ACCEL_LSB_PER_G 8192

void ps_calibration(uint8_t *out) {
    // Bias 0; +/- reference rate of 540 deg/s; +/- 1 g references.
    static const int16_t words[17] = {
        0, 0, 0,
        540 * GYRO_LSB_PER_DPS, -540 * GYRO_LSB_PER_DPS,
        540 * GYRO_LSB_PER_DPS, -540 * GYRO_LSB_PER_DPS,
        540 * GYRO_LSB_PER_DPS, -540 * GYRO_LSB_PER_DPS,
        540, 540,
        ACCEL_LSB_PER_G, -ACCEL_LSB_PER_G, ACCEL_LSB_PER_G, -ACCEL_LSB_PER_G,
        ACCEL_LSB_PER_G, -ACCEL_LSB_PER_G,
    };
    for (unsigned i = 0; i < 17; i++) pad_put_le16(out + 2 * i, (uint16_t)words[i]);
}

// PlayStation pads report y up and z towards the player.
void ps_motion(const pad_state_t *in, uint8_t *gyro, uint8_t *accel) {
    int32_t g[3] = {0, 0, 0}, a[3] = {0, ACCEL_LSB_PER_G, 0};
    if (in->has_motion) {
        g[0] = in->gyro[0]; g[1] = in->gyro[2]; g[2] = -in->gyro[1];
        a[0] = in->accel[0]; a[1] = in->accel[2]; a[2] = -in->accel[1];
        for (unsigned i = 0; i < 3; i++) {
            g[i] = g[i] * GYRO_LSB_PER_DPS / 10;
            a[i] = a[i] * ACCEL_LSB_PER_G / 1000;
        }
    }
    for (unsigned i = 0; i < 3; i++) {
        pad_put_le16(gyro + 2 * i, (uint16_t)pad_clamp(g[i], -32768, 32767));
        pad_put_le16(accel + 2 * i, (uint16_t)pad_clamp(a[i], -32768, 32767));
    }
}

uint8_t ps_face_and_hat(uint32_t b) {
    return (uint8_t)(pad_hat(b) | ((b & PAD_WEST) ? 0x10 : 0) | ((b & PAD_SOUTH) ? 0x20 : 0) |
                     ((b & PAD_EAST) ? 0x40 : 0) | ((b & PAD_NORTH) ? 0x80 : 0));
}

uint8_t ps_shoulders(uint32_t b) {
    return (uint8_t)(((b & PAD_L1) ? 0x01 : 0) | ((b & PAD_R1) ? 0x02 : 0) | ((b & PAD_L2) ? 0x04 : 0) |
                     ((b & PAD_R2) ? 0x08 : 0) | ((b & PAD_SELECT) ? 0x10 : 0) | ((b & PAD_START) ? 0x20 : 0) |
                     ((b & PAD_L3) ? 0x40 : 0) | ((b & PAD_R3) ? 0x80 : 0));
}

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    uint32_t b = in->buttons;
    memset(out, 0, DS4_LEN);
    out[0] = in->lx; out[1] = in->ly; out[2] = in->rx; out[3] = in->ry;
    out[4] = ps_face_and_hat(b);
    out[5] = ps_shoulders(b);
    out[6] = (uint8_t)(((b & PAD_HOME) ? 0x01 : 0) | ((b & PAD_MISC) ? 0x02 : 0) | (counter++ << 2));
    out[7] = in->lt; out[8] = in->rt;
    pad_put_le16(out + 9, (uint16_t)(now_ms * 188));   // 5.33 us units
    out[11] = 0x0c;                                    // temperature-ish constant
    ps_motion(in, out + 12, out + 18);
    out[29] = 0x1b;                                    // USB cable, battery full
    out[34] = 0x80; out[38] = 0x80;                    // no fingers on the touchpad
}

static uint16_t get_report(uint8_t type, uint8_t id, uint8_t *buf, uint16_t cap) {
    uint8_t tmp[PAD_REPORT_MAX] = {0};
    uint16_t size = 0;
    if (type != PAD_REPORT_FEATURE) return 0;
    switch (id) {
        case 0x02: ps_calibration(tmp); size = 36; break;
        case 0x12:   // device address (little endian), 08 25 00, host address
            for (unsigned i = 0; i < 6; i++) tmp[i] = pad_identity[5 - i];
            tmp[6] = 0x08; tmp[7] = 0x25; tmp[8] = 0x00;
            size = 15;
            break;
        case 0x81:
            for (unsigned i = 0; i < 6; i++) tmp[i] = pad_identity[5 - i];
            size = 6;
            break;
        case 0xa3:   // build date, time, hardware and firmware versions
            memcpy(tmp, "Aug  3 2013", 11);
            memcpy(tmp + 16, "07:01:12", 8);
            tmp[33] = 0x01;
            pad_put_le16(tmp + 34, 0x3100);   // hardware version
            tmp[36] = 0x03;
            pad_put_le16(tmp + 40, 0x0049);   // firmware version
            tmp[42] = 0x05; tmp[45] = 0x80; tmp[46] = 0x03;
            size = 48;
            break;
        default: return 0;
    }
    if (size > cap) size = cap;
    memcpy(buf, tmp, size);
    return size;
}

static void set_report(uint8_t player, uint8_t type, uint8_t id, const uint8_t *buf, uint16_t len) {
    if (type != PAD_REPORT_OUTPUT || id != 0x05 || len < 8) return;
    // flags, 2 reserved, weak motor, strong motor, R, G, B
    if (buf[0] & 0x01) pad_set_rumble(player, buf[4], buf[3]);
    if (buf[0] & 0x02) pad_set_color(player, buf[5], buf[6], buf[7]);
}

static void reset(void) { counter = 0; }

const pad_mode_t mode_ds4 = {
    .id = "ds4", .name = "DualShock 4", .family = "ps",
    .hosts = "PC, Steam, Android, macOS, PS4 (8 min)",
    .link = PAD_LINK_USB_HID,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_desc = report_desc, .report_desc_len = sizeof(report_desc),
    .manufacturer = "Sony Computer Entertainment", .product = "Wireless Controller",
    .report_id = 1, .report_len = DS4_LEN, .interval_ms = 4,
    .reset = reset, .build = build, .get_report = get_report, .set_report = set_report,
};
