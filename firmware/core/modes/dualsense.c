// Sony DualSense (CFI-ZCT1). For PCs and phones: Steam, SDL, Linux
// hid-playstation, Android 12+, macOS/iOS. A PS5 console does not accept
// unlicensed controllers, so this personality is not for consoles.
#include "../mode.h"
#include <string.h>

#define DS5_LEN 63

void ps_calibration(uint8_t *out);
void ps_motion(const pad_state_t *in, uint8_t *gyro, uint8_t *accel);
uint8_t ps_face_and_hat(uint32_t buttons);
uint8_t ps_shoulders(uint32_t buttons);

static const uint8_t report_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,             // Generic Desktop, Game Pad, Application
    0x85, 0x01,                                     //   Report ID 1
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,   // X Y Z Rz Rx Ry
    0x06, 0x00, 0xff, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,               // sequence number
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01,
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, //   Hat switch
    0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0f, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x0f, 0x81, 0x02,             //   15 buttons
    0x06, 0x00, 0xff, 0x09, 0x21, 0x95, 0x0d, 0x81, 0x02,               // 13 vendor bits
    0x06, 0x00, 0xff, 0x09, 0x22, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x34, 0x81, 0x02,             //   52 vendor bytes
    0x85, 0x02, 0x09, 0x23, 0x95, 0x2f, 0x91, 0x02, //   Output 0x02, 47 bytes
    0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xb1, 0x02, //   Feature 0x05 calibration
    0x85, 0x09, 0x09, 0x24, 0x95, 0x13, 0xb1, 0x02, //   Feature 0x09 pairing
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3f, 0xb1, 0x02, //   Feature 0x20 firmware
    0xc0,
};

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x054c, 0x0ce6, 0x0100, 1, 2, 0)};
static const uint8_t config_desc[] = {USB_HID_CONFIG_DESC(0xc0, sizeof(report_desc), 4)};

static uint8_t sequence;

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    uint32_t b = in->buttons;
    memset(out, 0, DS5_LEN);
    out[0] = in->lx; out[1] = in->ly; out[2] = in->rx; out[3] = in->ry;
    out[4] = in->lt; out[5] = in->rt;
    out[6] = sequence++;
    out[7] = ps_face_and_hat(b);
    out[8] = ps_shoulders(b);
    out[9] = (uint8_t)(((b & PAD_HOME) ? 0x01 : 0) | ((b & PAD_MISC) ? 0x02 : 0));
    ps_motion(in, out + 15, out + 21);
    uint32_t timestamp = now_ms * 3000u;            // 0.33 us units
    pad_put_le16(out + 27, (uint16_t)timestamp);
    pad_put_le16(out + 29, (uint16_t)(timestamp >> 16));
    out[32] = 0x80; out[36] = 0x80;                 // no fingers on the touchpad
    out[52] = 0x2a;                                 // battery full, charged
}

static uint16_t get_report(uint8_t type, uint8_t id, uint8_t *buf, uint16_t cap) {
    uint8_t tmp[PAD_REPORT_MAX] = {0};
    uint16_t size = 0;
    if (type != PAD_REPORT_FEATURE) return 0;
    switch (id) {
        case 0x05: ps_calibration(tmp); size = 40; break;
        case 0x09:
            for (unsigned i = 0; i < 6; i++) tmp[i] = pad_identity[5 - i];
            tmp[6] = 0x08; tmp[7] = 0x25; tmp[8] = 0x00;
            size = 19;
            break;
        case 0x20:   // build date/time, then hardware and firmware versions
            memcpy(tmp, "Jun 19 2023", 11);
            memcpy(tmp + 11, "14:47:34", 8);
            pad_put_le16(tmp + 19, 0x0003); pad_put_le16(tmp + 21, 0x0044);
            pad_put_le16(tmp + 23, 0x0413); pad_put_le16(tmp + 25, 0x0000);   // hardware
            pad_put_le16(tmp + 27, 0x0010); pad_put_le16(tmp + 29, 0x0110);   // firmware
            pad_put_le16(tmp + 43, 0x0224);                                   // update version
            size = 63;
            break;
        default: return 0;
    }
    if (size > cap) size = cap;
    memcpy(buf, tmp, size);
    return size;
}

static void set_report(uint8_t type, uint8_t id, const uint8_t *buf, uint16_t len) {
    if (type != PAD_REPORT_OUTPUT || id != 0x02 || len < 47) return;
    // valid_flag0, valid_flag1, right (weak) motor, left (strong) motor ... LEDs, RGB
    if (buf[0] & 0x03) pad_set_rumble(buf[3], buf[2]);
    if (buf[1] & 0x10) pad_set_led(buf[43] & 0x1f);
    if (buf[1] & 0x04) pad_set_color(buf[44], buf[45], buf[46]);
}

static void reset(void) { sequence = 0; }

const pad_mode_t mode_dualsense = {
    .id = "dualsense", .name = "DualSense", .family = "ps",
    .hosts = "PC, Steam, Android, macOS, iOS",
    .link = PAD_LINK_USB_HID,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_desc = report_desc, .report_desc_len = sizeof(report_desc),
    .manufacturer = "Sony Interactive Entertainment", .product = "DualSense Wireless Controller",
    .report_id = 1, .report_len = DS5_LEN, .interval_ms = 4,
    .reset = reset, .build = build, .get_report = get_report, .set_report = set_report,
};
