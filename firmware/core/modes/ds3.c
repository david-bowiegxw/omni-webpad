// Sony DualShock 3 / Sixaxis. Works on a PS3 console without authentication.
#include "../mode.h"
#include "ds3_tables.h"
#include <string.h>

#define DS3_LEN 48

static uint8_t host_addr[6], ef_byte, last[DS3_LEN];
static bool operational;

static const uint8_t device_desc[] = {USB_DEVICE_DESC(0x0200, 0, 0, 0, 0x054c, 0x0268, 0x0100, 1, 2, 0)};
static const uint8_t config_desc[] = {USB_HID_CONFIG_DESC(0xa0, sizeof(hidReportDescriptor), 1)};

static uint16_t motion(int32_t value, int32_t per_thousand) {
    return (uint16_t)pad_clamp(512 + value * per_thousand / 1000, 0, 1023);
}

static void build(const pad_state_t *in, uint32_t now_ms, uint8_t *out) {
    (void)now_ms;
    uint32_t mask = in->buttons;
    memset(out, 0, DS3_LEN);
    out[1] = (uint8_t)mask;
    out[2] = (uint8_t)(mask >> 8);
    out[3] = (mask & PAD_HOME) ? 1 : 0;
    out[5] = in->lx; out[6] = in->ly; out[7] = in->rx; out[8] = in->ry;
    // Pressure order: D-pad U/R/D/L, L2/R2/L1/R1, triangle/circle/cross/square.
    for (unsigned i = 0; i < 12; i++) out[13 + i] = (mask & (1u << (i + 4))) ? 255 : 0;
    out[17] = in->lt; out[18] = in->rt;
    out[28] = 2; out[29] = 5; out[30] = 0x10;   // USB powered, battery full, cable
    // Sixaxis: 10-bit big-endian, 512 = rest, roughly 113 counts per g.
    if (in->has_motion) {
        pad_put_be16(out + 40, motion(in->accel[0], 113));
        pad_put_be16(out + 42, motion(in->accel[1], 113));
        pad_put_be16(out + 44, motion(in->accel[2], 113));
        pad_put_be16(out + 46, motion(in->gyro[2], 512));
    } else {
        for (unsigned i = 40; i < 48; i += 2) pad_put_be16(out + i, 512);
    }
    memcpy(last, out, DS3_LEN);
}

static uint16_t get_report(uint8_t type, uint8_t id, uint8_t *buf, uint16_t cap) {
    const uint8_t *src = NULL;
    uint16_t size = 0;
    uint8_t tmp[PAD_REPORT_MAX];
    if (type == PAD_REPORT_INPUT) { src = last; size = DS3_LEN; }
    else if (type == PAD_REPORT_FEATURE) {
        switch (id) {
            case 0x01: src = OUTPUT_0x01; size = sizeof(OUTPUT_0x01); break;
            case 0xef: src = OUTPUT_0xEF; size = sizeof(OUTPUT_0xEF); break;
            case 0xf2: src = BT_INFO; size = sizeof(BT_INFO); break;
            case 0xf5: src = OUTPUT_0xF5; size = sizeof(OUTPUT_0xF5); break;
            case 0xf7: src = OUTPUT_0xF7; size = sizeof(OUTPUT_0xF7); break;
            case 0xf8: src = OUTPUT_0xF8; size = sizeof(OUTPUT_0xF8); break;
            default: break;
        }
    }
    if (!src) return 0;
    memcpy(tmp, src, size);
    if (type == PAD_REPORT_FEATURE) {
        if (id == 0xef || id == 0xf8) tmp[6] = ef_byte;
        // The report ID is added by the USB layer; F2's address starts at wire offset 4.
        if (id == 0xf2) memcpy(tmp + 3, pad_identity, 6);
        if (id == 0xf5) { tmp[0] = 1; memcpy(tmp + 1, host_addr, 6); }
    }
    if (size > cap) size = cap;
    memcpy(buf, tmp, size);
    return size;
}

static void set_report(uint8_t player, uint8_t type, uint8_t id, const uint8_t *buf, uint16_t len) {
    if (type == PAD_REPORT_FEATURE) {
        if (id == 0xef && len > 6) ef_byte = buf[6];
        if (id == 0xf5 && len >= 7) memcpy(host_addr, buf + 1, 6);
        if (id == 0xf4 && len >= 2 && buf[0] == 0x42) operational = true;
    }
    if (id == 1 && len >= 10) {
        pad_set_rumble(player, buf[4], buf[2] ? 255 : 0);   // large motor is analog, small is on/off
        pad_set_led(player, (buf[9] >> 1) & 0x0f);
    }
}

static void reset(void) {
    operational = false;
    ef_byte = 0;
    memset(host_addr, 0xaa, sizeof(host_addr));
}

bool ds3_operational(void) { return operational; }

const pad_mode_t mode_ds3 = {
    .id = "ds3", .name = "DualShock 3", .family = "ps", .hosts = "PS3, PC",
    .link = PAD_LINK_USB_HID,
    .device_desc = device_desc, .config_desc = config_desc, .config_len = sizeof(config_desc),
    .report_desc = hidReportDescriptor, .report_desc_len = sizeof(hidReportDescriptor),
    .manufacturer = "Sony", .product = "PLAYSTATION(R)3 Controller",
    .report_id = 1, .report_len = DS3_LEN, .interval_ms = 8,
    .reset = reset, .build = build, .get_report = get_report, .set_report = set_report,
};
