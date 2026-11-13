// Desktop unit tests for the portable firmware core.
#include "app.h"
#include "platform.h"
#include "server.h"
#include "sha1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- fake board

static uint32_t clock_ms = 1000;
static unsigned links = PAD_LINK_MASK_USB | PAD_LINK_MASK_BLE;
static uint8_t stored[256];
static bool stored_valid, rebooted, close_seen;
static uint8_t sent[8192];
static unsigned sent_len;

const unsigned char page_gz[] = {0x1f, 0x8b, 'p', 'a', 'g', 'e'};
const unsigned page_gz_len = sizeof(page_gz);

uint32_t plat_millis(void) { return clock_ms; }
const char *plat_board_name(void) { return "test"; }
unsigned plat_link_mask(void) { return links; }
bool plat_link_connected(void) { return true; }
void plat_reboot(void) { rebooted = true; }
bool plat_settings_read(uint8_t *data, unsigned len) {
    if (!stored_valid) return false;
    memcpy(data, stored, len);
    return true;
}
bool plat_settings_write(const uint8_t *data, unsigned len) {
    assert(len <= sizeof(stored));
    memcpy(stored, data, len);
    stored_valid = true;
    return true;
}
bool net_send(void *io, const void *data, unsigned len, bool is_static) {
    (void)io; (void)is_static;
    assert(sent_len + len < sizeof(sent));
    memcpy(sent + sent_len, data, len);
    sent_len += len;
    sent[sent_len] = 0;
    return true;
}
void net_close(void *io) { (void)io; close_seen = true; }

static void reset_output(void) { sent_len = 0; sent[0] = 0; close_seen = false; }
static bool feed(server_conn_t *c, const char *text) { return server_feed(c, (const uint8_t *)text, (unsigned)strlen(text)); }

// ---------------------------------------------------------------- HID parsing

// Sums the size of INPUT items per report ID. Returns bits for `wanted_id`.
static unsigned input_bits(const uint8_t *d, unsigned len, uint8_t wanted_id, int *depth_out) {
    unsigned size = 0, count = 0, bits = 0, i = 0;
    uint8_t id = 0;
    int depth = 0;
    while (i < len) {
        uint8_t prefix = d[i], n = prefix & 3;
        if (n == 3) n = 4;
        uint32_t value = 0;
        assert(i + 1 + n <= len);
        for (unsigned k = 0; k < n; k++) value |= (uint32_t)d[i + 1 + k] << (8 * k);
        switch (prefix & 0xfc) {
            case 0x74: size = value; break;
            case 0x94: count = value; break;
            case 0x84: id = (uint8_t)value; break;
            case 0x80: if (id == wanted_id) bits += size * count; break;
            case 0xa0: depth++; break;
            case 0xc0: depth--; break;
            default: break;
        }
        i += 1u + n;
    }
    *depth_out = depth;
    return bits;
}

static void test_descriptors(void) {
    for (unsigned i = 0; i < pad_mode_count; i++) {
        const pad_mode_t *m = pad_modes[i];
        assert(m->id && m->name && m->family && m->hosts && m->build);
        assert(strlen(m->id) < sizeof(app_settings.mode));
        assert(m->report_len && m->report_len + (m->report_id ? 1 : 0) <= PAD_REPORT_MAX);
        assert(pad_mode_find(m->id) == m);
        if (m->report_desc) {
            int depth;
            unsigned bits = input_bits(m->report_desc, m->report_desc_len, m->report_id, &depth);
            assert(depth == 0);
            assert(bits == m->report_len * 8u);
        }
        if (m->link == PAD_LINK_BLE_HID) continue;
        const uint8_t *dev = m->device_desc, *cfg = m->config_desc;
        assert(dev[0] == 18 && dev[1] == 1 && dev[7] == 64 && dev[17] == 1);
        assert((dev[14] != 0) == (m->manufacturer != NULL));
        assert((dev[15] != 0) == (m->product != NULL));
        assert((dev[16] != 0) == (m->serial != NULL));
        assert(cfg[1] == 2 && (cfg[2] | cfg[3] << 8) == m->config_len && (cfg[7] & 0x80));
        // Walk the configuration: lengths add up, two interrupt endpoints.
        unsigned offset = 0, endpoints = 0, hid_len = 0;
        while (offset < m->config_len) {
            assert(cfg[offset] >= 2);
            if (cfg[offset + 1] == 5) {
                endpoints++;
                assert(cfg[offset + 3] == 3 && (cfg[offset + 4] | cfg[offset + 5] << 8) >= m->report_len + (m->report_id ? 1 : 0));
            }
            if (cfg[offset + 1] == 0x21 && m->link == PAD_LINK_USB_HID) hid_len = cfg[offset + 7] | cfg[offset + 8] << 8;
            offset += cfg[offset];
        }
        assert(offset == m->config_len && endpoints == 2);
        if (m->link == PAD_LINK_USB_HID) assert(hid_len == m->report_desc_len);
        else assert(!m->report_desc);
    }
    puts("descriptors: lengths, report sizes and endpoints PASS");
}

// ---------------------------------------------------------------- reports

static void build(const char *id, const pad_state_t *s, uint8_t *out) {
    const pad_mode_t *m = pad_mode_find(id);
    assert(m);
    if (m->reset) m->reset();
    memset(out, 0xee, PAD_REPORT_MAX);
    m->build(s, 0, out);
}

static void test_reports(void) {
    uint8_t r[PAD_REPORT_MAX];
    pad_state_t s;

    assert(pad_hat(0) == 8 && pad_hat(PAD_UP) == 0 && pad_hat(PAD_UP | PAD_RIGHT) == 1 && pad_hat(PAD_RIGHT) == 2);
    assert(pad_hat(PAD_DOWN) == 4 && pad_hat(PAD_LEFT) == 6 && pad_hat(PAD_LEFT | PAD_UP) == 7);
    assert(pad_hat(PAD_LEFT | PAD_RIGHT) == 8 && pad_hat(PAD_UP | PAD_DOWN | PAD_LEFT) == 6);
    assert(pad_axis_s16(128, false) == 0 && pad_axis_s16(0, false) == -32768 && pad_axis_s16(255, false) == 32767);
    assert(pad_axis_s16(0, true) == 32767 && pad_axis_s16(255, true) == -32767 && pad_axis_s16(128, true) == 0);

    // DualShock 3: same layout the 0.3 firmware sent (offsets exclude the report ID).
    pad_neutral(&s);
    build("ds3", &s, r);
    assert(r[5] == 128 && r[8] == 128 && r[40] == 2 && r[41] == 0 && r[46] == 2 && r[47] == 0 && r[28] == 2);
    s.buttons = PAD_HOME; build("ds3", &s, r);
    assert(r[3] == 1 && r[1] == 0 && r[2] == 0);
    for (unsigned i = 4; i < 16; i++) {
        pad_neutral(&s); s.buttons = 1u << i; pad_normalize(&s); build("ds3", &s, r);
        for (unsigned j = 0; j < 12; j++) assert(r[13 + j] == (j == i - 4 ? 255 : 0));
    }
    pad_neutral(&s); s.buttons = PAD_SELECT | PAD_START; s.lx = 0; s.ly = 255; s.rx = 17; s.ry = 238; s.lt = 77;
    pad_normalize(&s); build("ds3", &s, r);
    assert(r[1] == 9 && r[2] == 1 && r[5] == 0 && r[6] == 255 && r[7] == 17 && r[8] == 238 && r[17] == 77);
    s.has_motion = true; s.accel[0] = 1000; s.accel[1] = -1000; s.accel[2] = 20000; s.gyro[2] = -100;
    build("ds3", &s, r);
    assert((r[40] << 8 | r[41]) == 625 && (r[42] << 8 | r[43]) == 399 && (r[44] << 8 | r[45]) == 1023 && (r[46] << 8 | r[47]) == 461);

    // DualShock 4
    pad_neutral(&s); build("ds4", &s, r);
    assert(r[0] == 128 && r[3] == 128 && r[4] == 8 && r[5] == 0 && r[6] == 0 && r[29] == 0x1b && r[34] == 0x80 && r[38] == 0x80);
    assert((int16_t)(r[20] | r[21] << 8) == 8192);
    s.buttons = PAD_SOUTH | PAD_UP | PAD_L1 | PAD_START | PAD_HOME | PAD_MISC | PAD_R2; pad_normalize(&s); build("ds4", &s, r);
    assert(r[4] == (0x20 | 0) && r[5] == (0x01 | 0x08 | 0x20) && (r[6] & 3) == 3 && r[7] == 0 && r[8] == 255);
    s.buttons = PAD_WEST | PAD_EAST | PAD_NORTH | PAD_DOWN | PAD_LEFT | PAD_R1 | PAD_L2 | PAD_SELECT | PAD_L3 | PAD_R3;
    s.rt = 0; pad_normalize(&s); build("ds4", &s, r);
    assert(r[4] == (0x10 | 0x40 | 0x80 | 5) && r[5] == (0x02 | 0x04 | 0x10 | 0x40 | 0x80) && r[7] == 255);

    // DualSense
    pad_neutral(&s); build("dualsense", &s, r);
    assert(r[0] == 128 && r[4] == 0 && r[7] == 8 && r[8] == 0 && r[9] == 0 && r[32] == 0x80 && r[36] == 0x80 && r[52] == 0x2a);
    s.buttons = PAD_NORTH | PAD_RIGHT | PAD_R1 | PAD_HOME; s.lt = 9; pad_normalize(&s); build("dualsense", &s, r);
    assert(r[4] == 9 && r[7] == (0x80 | 2) && r[8] == (0x02 | 0x04) && r[9] == 1);

    // Xbox 360
    pad_neutral(&s); build("xinput", &s, r);
    static const uint8_t xinput_idle[20] = {0x00, 0x14};
    assert(memcmp(r, xinput_idle, 20) == 0);
    s.buttons = PAD_SOUTH | PAD_NORTH | PAD_UP | PAD_START | PAD_L1 | PAD_HOME | PAD_R3; s.lx = 255; s.ly = 0; s.rt = 200;
    pad_normalize(&s); build("xinput", &s, r);
    assert(r[2] == (0x01 | 0x10 | 0x80) && r[3] == (0x01 | 0x04 | 0x10 | 0x80) && r[4] == 0 && r[5] == 200);
    assert(r[6] == 0xff && r[7] == 0x7f && r[8] == 0xff && r[9] == 0x7f && r[10] == 0 && r[11] == 0);
    s.buttons = PAD_EAST | PAD_WEST | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_SELECT | PAD_R1 | PAD_L3; s.rt = 0;
    pad_normalize(&s); build("xinput", &s, r);
    assert(r[2] == (0x02 | 0x04 | 0x08 | 0x20 | 0x40) && r[3] == (0x02 | 0x20 | 0x40));

    // Original Xbox
    pad_neutral(&s); s.buttons = PAD_SOUTH | PAD_R1 | PAD_L1 | PAD_START; s.lt = 40; s.ry = 255; pad_normalize(&s); build("xid", &s, r);
    assert(r[0] == 0 && r[1] == 20 && r[2] == 0x10 && r[4] == 255 && r[5] == 0 && r[8] == 255 && r[9] == 255 && r[10] == 40);
    assert(r[18] == 0x01 && r[19] == 0x80);   // stick down = -32767

    // Switch: positional mapping (south = B, east = A, west = Y, north = X).
    pad_neutral(&s); build("switch", &s, r);
    static const uint8_t switch_idle[8] = {0, 0, 8, 128, 128, 128, 128, 0};
    assert(memcmp(r, switch_idle, 8) == 0);
    s.buttons = PAD_SOUTH | PAD_L2 | PAD_START | PAD_HOME | PAD_UP | PAD_LEFT; pad_normalize(&s); build("switch", &s, r);
    assert(r[0] == (0x02 | 0x40) && r[1] == (0x02 | 0x10) && r[2] == 7);
    s.buttons = PAD_EAST | PAD_WEST | PAD_NORTH | PAD_MISC | PAD_SELECT | PAD_R1; s.lt = 0; pad_normalize(&s); build("switch", &s, r);
    assert(r[0] == (0x04 | 0x01 | 0x08 | 0x20) && r[1] == (0x20 | 0x01));

    // Generic / BLE: Linux gamepad button order.
    pad_neutral(&s); s.buttons = PAD_SOUTH | PAD_WEST | PAD_NORTH | PAD_HOME | PAD_DOWN; s.rt = 3; pad_normalize(&s); build("hid", &s, r);
    assert(r[0] == (0x01 | 0x08 | 0x10) && r[1] == (0x02 | 0x10) && r[2] == 4 && r[7] == 0 && r[8] == 3);
    build("ble", &s, r);
    assert(r[0] == 0x19 && r[2] == 4);
    puts("reports: ds3, ds4, dualsense, xinput, xid, switch, hid, ble PASS");
}

static void test_feedback(void) {
    const pad_mode_t *m;
    uint8_t buf[64];
    pad_feedback_reset();
    uint32_t q = pad_feedback.sequence;

    m = pad_mode_find("xinput");
    m->out((const uint8_t[]){0x00, 0x08, 0x00, 0xaa, 0x55, 0, 0, 0}, 8);
    assert(pad_feedback.rumble_strong == 0xaa && pad_feedback.rumble_weak == 0x55 && pad_feedback.sequence == q + 1);
    m->out((const uint8_t[]){0x00, 0x08, 0x00, 0xaa, 0x55, 0, 0, 0}, 8);
    assert(pad_feedback.sequence == q + 1);
    m->out((const uint8_t[]){0x01, 0x03, 0x07}, 3);
    assert(pad_feedback.led == 2);

    m = pad_mode_find("ds3"); m->reset();
    memset(buf, 0, sizeof(buf)); buf[2] = 1; buf[4] = 0x80; buf[9] = 0x02;
    m->set_report(PAD_REPORT_OUTPUT, 1, buf, 48);
    assert(pad_feedback.rumble_strong == 0x80 && pad_feedback.rumble_weak == 255 && pad_feedback.led == 1);
    assert(m->get_report(PAD_REPORT_FEATURE, 0xf2, buf, 64) == 17 && memcmp(buf + 3, pad_identity, 6) == 0);
    assert(m->get_report(PAD_REPORT_FEATURE, 0x55, buf, 64) == 0);
    assert(m->get_report(PAD_REPORT_INPUT, 1, buf, 64) == 48);

    m = pad_mode_find("ds4");
    memset(buf, 0, sizeof(buf)); buf[0] = 0x03; buf[3] = 10; buf[4] = 20; buf[5] = 1; buf[6] = 2; buf[7] = 3;
    m->set_report(PAD_REPORT_OUTPUT, 5, buf, 31);
    assert(pad_feedback.rumble_strong == 20 && pad_feedback.rumble_weak == 10 && pad_feedback.b == 3);
    assert(m->get_report(PAD_REPORT_FEATURE, 0x02, buf, 64) == 36 && m->get_report(PAD_REPORT_FEATURE, 0xa3, buf, 64) == 48);
    assert(m->get_report(PAD_REPORT_FEATURE, 0x12, buf, 64) == 15 && buf[0] == pad_identity[5]);

    m = pad_mode_find("dualsense");
    memset(buf, 0, sizeof(buf)); buf[0] = 0x03; buf[1] = 0x14; buf[2] = 7; buf[3] = 8; buf[43] = 4; buf[44] = 9;
    m->set_report(PAD_REPORT_OUTPUT, 2, buf, 47);
    assert(pad_feedback.rumble_strong == 8 && pad_feedback.rumble_weak == 7 && pad_feedback.led == 4 && pad_feedback.r == 9);
    assert(m->get_report(PAD_REPORT_FEATURE, 0x05, buf, 64) == 40 && m->get_report(PAD_REPORT_FEATURE, 0x09, buf, 64) == 19);
    assert(m->get_report(PAD_REPORT_FEATURE, 0x20, buf, 64) == 63);

    m = pad_mode_find("xid");
    pad_setup_t setup = {0xc1, 0x06, 0x4200, 0, 16};
    assert(m->control_in(&setup, buf, 64) == 16 && buf[0] == 0x10 && buf[1] == 0x42);
    setup = (pad_setup_t){0xc1, 0x01, 0x0100, 0, 20}; assert(m->control_in(&setup, buf, 64) == 20);
    setup = (pad_setup_t){0xa1, 0x01, 0x0100, 0, 20}; assert(m->control_in(&setup, buf, 64) == 20 && buf[1] == 20);
    setup = (pad_setup_t){0xc1, 0x77, 0, 0, 0}; assert(m->control_in(&setup, buf, 64) == -1);
    setup = (pad_setup_t){0x21, 0x09, 0x0200, 0, 6};
    assert(m->control_out(&setup, (const uint8_t[]){0, 6, 0, 0x33, 0, 0x44}, 6));
    assert(pad_feedback.rumble_strong == 0x33 && pad_feedback.rumble_weak == 0x44);
    puts("feedback: rumble, LEDs, feature and control requests PASS");
}

// ---------------------------------------------------------------- app

static void test_queue(void) {
    pad_state_t s, out;
    stored_valid = false;
    app_init();
    assert(strcmp(app_mode->id, "xinput") == 0 && strncmp(app_settings.ssid, "OmniWebPad-", 11) == 0);

    // A tap that arrives as two messages in one burst is still seen once.
    pad_neutral(&s); s.buttons = PAD_SOUTH; app_input(&s);
    s.buttons = 0; app_input(&s);
    app_next_state(&out); assert(out.buttons == PAD_SOUTH);
    app_next_state(&out); assert(out.buttons == 0);
    app_next_state(&out); assert(out.buttons == 0);

    // Analog-only changes coalesce to the newest value.
    for (unsigned i = 0; i < 100; i++) { s.lx = (uint8_t)i; app_input(&s); }
    app_next_state(&out); assert(out.lx == 99);

    // The lease releases everything when the client goes quiet.
    s.buttons = PAD_EAST; s.lx = 3; app_input(&s);
    app_next_state(&out); assert(out.buttons == PAD_EAST);
    clock_ms += APP_LEASE_MS + 1;
    app_next_state(&out); assert(out.buttons == 0 && out.lx == 128);

    // Overflow keeps the newest edges.
    for (unsigned i = 0; i < 40; i++) { s.buttons = (i & 1) ? PAD_UP : 0; app_input(&s); }
    unsigned drained = 0;
    do { app_next_state(&out); drained++; } while (drained < 16);
    assert(out.buttons == PAD_UP);

    // Settings round trip and validation.
    app_settings_t next = app_settings;
    strcpy(next.mode, "switch"); strcpy(next.ssid, "My Pad"); strcpy(next.pass, "12345678");
    assert(app_settings_save(&next));
    app_init();
    assert(strcmp(app_mode->id, "switch") == 0 && strcmp(app_settings.ssid, "My Pad") == 0);
    strcpy(next.pass, "short"); assert(!app_settings_save(&next));
    strcpy(next.pass, "quote\"quote"); assert(!app_settings_save(&next));
    strcpy(next.pass, "12345678"); strcpy(next.mode, "nope"); assert(!app_settings_save(&next));
    stored[40] ^= 1; app_init();                      // corrupt blob -> defaults
    assert(strcmp(app_mode->id, "xinput") == 0);
    // A BLE-only board never starts a USB personality.
    stored_valid = false; links = PAD_LINK_MASK_BLE; app_init();
    assert(strcmp(app_mode->id, "ble") == 0);
    links = PAD_LINK_MASK_USB | PAD_LINK_MASK_BLE; app_init();
    puts("app: tap latch, coalescing, lease, overflow, settings PASS");
}

// ---------------------------------------------------------------- server

static unsigned ws_frame(uint8_t *out, uint8_t opcode, const uint8_t *payload, unsigned len) {
    static const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    unsigned n = 0;
    out[n++] = (uint8_t)(0x80 | opcode);
    out[n++] = (uint8_t)(0x80 | len);
    memcpy(out + n, mask, 4); n += 4;
    for (unsigned i = 0; i < len; i++) out[n++] = payload[i] ^ mask[i & 3];
    return n;
}

static void test_server(void) {
    static server_conn_t c, d;
    pad_state_t out;
    uint8_t digest[20];
    char text[64];
    int io = 0;

    sha1((const uint8_t *)"abc", 3, digest);
    assert(digest[0] == 0xa9 && digest[19] == 0x9d);
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    sha1((const uint8_t *)key, (unsigned)strlen(key), digest);
    base64_encode(digest, 20, text);
    assert(strcmp(text, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
    base64_encode((const uint8_t *)"ab", 2, text); assert(strcmp(text, "YWI=") == 0);

    stored_valid = false; app_init();
    server_open(&c, &io);

    reset_output();
    assert(feed(&c, "GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    assert(strstr((char *)sent, "200 OK") && strstr((char *)sent, "Content-Encoding: gzip") && strstr((char *)sent, "Content-Length: 6\r\n"));
    assert(memcmp(sent + sent_len - 6, page_gz, 6) == 0);

    // Requests split at every byte boundary, and two pipelined requests.
    reset_output();
    const char *info = "GET /api/info HTTP/1.1\r\nHost: x\r\n\r\n";
    for (const char *p = info; *p; p++) assert(server_feed(&c, (const uint8_t *)p, 1));
    assert(strstr((char *)sent, "\"mode\":\"xinput\"") && strstr((char *)sent, "\"id\":\"dualsense\"") && sent[sent_len - 1] == '}');
    reset_output();
    assert(feed(&c, "GET /api/status HTTP/1.1\r\n\r\nGET /nothing HTTP/1.1\r\n\r\n"));
    assert(strstr((char *)sent, "\"log\":[") && strstr((char *)sent, "404 Not Found") && close_seen);

    // Legacy polling endpoint.
    server_open(&c, &io); reset_output();
    assert(feed(&c, "GET /state?m=16384&lx=0&ly=255&rx=128&ry=128 HTTP/1.1\r\n\r\n"));
    assert(strstr((char *)sent, "\"usb\":true"));
    app_next_state(&out); assert(out.buttons == PAD_SOUTH && out.lx == 0 && out.ly == 255);
    reset_output(); assert(feed(&c, "GET /state?m=999999999 HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "400 Bad Request"));
    server_open(&c, &io); reset_output();
    assert(feed(&c, "GET /state?m=1&lx=300 HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "400"));

    // Configuration.
    server_open(&c, &io); reset_output();
    assert(feed(&c, "GET /api/config?mode=ds4 HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "405"));
    server_open(&c, &io); reset_output();
    assert(feed(&c, "POST /api/config?mode=psx HTTP/1.1\r\nContent-Length: 0\r\n\r\n")); assert(strstr((char *)sent, "unknown mode"));
    server_open(&c, &io); reset_output();
    assert(feed(&c, "POST /api/config?pass=short HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "400") && !stored_valid);
    server_open(&c, &io); reset_output();
    assert(feed(&c, "POST /api/config?mode=ds4&ssid=My%20Pad+2&pass=p%40ssw0rd1 HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}"));
    assert(strstr((char *)sent, "\"reboot\":true") && stored_valid && !rebooted);
    clock_ms += 1000; app_poll(); assert(rebooted);
    app_init();
    assert(strcmp(app_mode->id, "ds4") == 0 && strcmp(app_settings.ssid, "My Pad 2") == 0 && strcmp(app_settings.pass, "p@ssw0rd1") == 0);
    links = PAD_LINK_MASK_USB; server_open(&c, &io); reset_output();
    assert(feed(&c, "POST /api/config?mode=ble HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "not supported"));
    links = PAD_LINK_MASK_USB | PAD_LINK_MASK_BLE;
    server_open(&c, &io); reset_output();
    assert(feed(&c, "POST /api/config?reset=1 HTTP/1.1\r\n\r\n")); assert(strstr((char *)sent, "\"reboot\":true"));
    app_init(); assert(strcmp(app_mode->id, "xinput") == 0);

    // Oversized head is rejected instead of overflowing.
    server_open(&c, &io);
    static char big[3000];
    memset(big, 'a', sizeof(big) - 1);
    assert(!feed(&c, big));
    server_closed(&c);

    // WebSocket upgrade, state, ping, close.
    server_open(&c, &io); reset_output();
    assert(feed(&c, "GET /ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSEC-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"));
    assert(strstr((char *)sent, "101 Switching Protocols") && strstr((char *)sent, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n"));
    assert(c.websocket && sent[sent_len - 11] == 0x82 && sent[sent_len - 10] == 9 && sent[sent_len - 9] == WS_MSG_STATUS && sent[sent_len - 7] == 1);

    uint8_t frame[64], state[24] = {WS_MSG_STATE, 7, 0x00, 0x40, 0x02, 0x00, 10, 20, 30, 40, 50, 0};
    unsigned n = ws_frame(frame, 2, state, 12);
    for (unsigned i = 0; i < n; i++) assert(server_feed(&c, frame + i, 1));
    app_next_state(&out);
    assert(out.buttons == (PAD_SOUTH | PAD_MISC | PAD_L2) && out.lx == 10 && out.ry == 40 && out.lt == 50 && !out.has_motion);
    state[12] = 0xe8; state[13] = 0x03; state[18] = 0xff; state[19] = 0xff;
    n = ws_frame(frame, 2, state, 24);
    n += ws_frame(frame + n, 2, (const uint8_t[]){WS_MSG_PING, 0x5a}, 2);   // two frames in one read
    reset_output(); assert(server_feed(&c, frame, n));
    app_next_state(&out); assert(out.has_motion && out.accel[0] == 1000 && out.gyro[0] == -1);
    assert(sent_len == 11 && sent[2] == WS_MSG_STATUS && sent[3] == 0x5a);
    n = ws_frame(frame, 2, state, 13); assert(server_feed(&c, frame, n));     // wrong length: ignored

    // Feedback changes are pushed to every socket once.
    server_open(&d, &io);
    assert(feed(&d, "GET /ws HTTP/1.1\r\nSec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n\r\n"));
    server_poll(); reset_output(); server_poll(); assert(sent_len == 0);
    pad_set_rumble(200, 100); server_poll();
    assert(sent_len == 22 && sent[5] == 200 && sent[6] == 100 && sent[11 + 5] == 200);
    reset_output(); server_poll(); assert(sent_len == 0);

    // Losing the controlling socket releases the pad; a spectator does not.
    state[2] = 0x10; n = ws_frame(frame, 2, state, 12); assert(server_feed(&c, frame, n));
    app_next_state(&out); assert(out.buttons & PAD_UP);
    server_closed(&d); app_next_state(&out); assert(out.buttons & PAD_UP);
    server_closed(&c); app_next_state(&out); assert(out.buttons == 0);

    // Unmasked client frames violate the protocol.
    server_open(&c, &io);
    assert(feed(&c, "GET /ws HTTP/1.1\r\nSec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n\r\n"));
    assert(!server_feed(&c, (const uint8_t[]){0x82, 0x01, 0x01}, 3));
    server_closed(&c);
    server_open(&c, &io); reset_output();
    assert(feed(&c, "GET /ws HTTP/1.1\r\nSec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n\r\n"));
    n = ws_frame(frame, 8, (const uint8_t[]){0x03, 0xe8}, 2); reset_output();
    assert(server_feed(&c, frame, n) && close_seen && sent[0] == 0x88);
    server_closed(&c);
    puts("server: http, config, websocket handshake/frames, status push PASS");
}

int main(void) {
    test_descriptors();
    test_reports();
    test_feedback();
    test_queue();
    test_server();
    return 0;
}
