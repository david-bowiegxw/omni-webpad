#include "usb_link.h"
#include "app.h"
#include "platform.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include <string.h>

static uint32_t last_send;

// TinyUSB 0.21 added an "in interrupt" argument to usbd_edpt_xfer().
#if TUSB_VERSION_NUMBER >= 2100
#define EDPT_XFER(rhport, ep, buf, len) usbd_edpt_xfer(rhport, ep, buf, len, false)
#else
#define EDPT_XFER(rhport, ep, buf, len) usbd_edpt_xfer(rhport, ep, buf, len)
#endif

static bool is_usb(void) { return app_mode && app_mode->link != PAD_LINK_BLE_HID; }
static bool is_raw(void) { return app_mode && app_mode->link == PAD_LINK_USB_RAW; }

// ---------------------------------------------------------------- descriptors

uint8_t const *tud_descriptor_device_cb(void) { return app_mode->device_desc; }

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return app_mode->config_desc;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return app_mode->report_desc;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t str[48];
    (void)langid;
    if (index == 0) {
        str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        str[1] = 0x0409;
        return str;
    }
    const char *s = index == 1 ? app_mode->manufacturer : index == 2 ? app_mode->product
                  : index == 3 ? app_mode->serial : NULL;
    if (!s) return NULL;
    unsigned n = (unsigned)strlen(s);
    if (n > 47) n = 47;
    for (unsigned i = 0; i < n; i++) str[i + 1] = (uint8_t)s[i];
    str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return str;
}

void tud_mount_cb(void) {
    if (app_mode->reset) app_mode->reset();
    app_log("usb mount");
}

void tud_umount_cb(void) {
    pad_feedback_reset();
    app_log("usb unmount");
}

// ---------------------------------------------------------------- HID class

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type, uint8_t *buffer, uint16_t len) {
    (void)instance;
    uint16_t n = app_mode->get_report ? app_mode->get_report((uint8_t)type, id, buffer, len) : 0;
    if (type == HID_REPORT_TYPE_FEATURE) app_log("get %02X n=%u", id, n);
    return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type, uint8_t const *buffer, uint16_t len) {
    (void)instance;
    uint8_t kind = (uint8_t)type;
    // Reports arriving on the interrupt OUT endpoint still carry their ID.
    if (id == 0 && kind != PAD_REPORT_FEATURE) {
        kind = PAD_REPORT_OUTPUT;
        if (app_mode->report_id && len) { id = buffer[0]; buffer++; len--; }
    }
    if (kind == PAD_REPORT_FEATURE) app_log("set %02X n=%u %02X", id, len, len ? buffer[0] : 0);
    if (app_mode->set_report) app_mode->set_report(kind, id, buffer, len);
}

// ---------------------------------------------------------------- raw class
// One vendor interface with an interrupt IN and an interrupt OUT endpoint,
// as used by the Xbox family.

static uint8_t raw_ep_in, raw_ep_out;
CFG_TUD_MEM_SECTION static struct {
    TUD_EPBUF_DEF(in, 64);
    TUD_EPBUF_DEF(out, 64);
    TUD_EPBUF_DEF(ctrl, 64);
} raw_buf;

static void raw_init(void) {}
static bool raw_deinit(void) { return true; }

static void raw_reset(uint8_t rhport) {
    (void)rhport;
    raw_ep_in = raw_ep_out = 0;
}

static uint16_t raw_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
    if (!is_raw()) return 0;                 // let the HID class claim the interface
    uint16_t used = itf->bLength;
    uint8_t const *p = tu_desc_next(itf);
    uint8_t found = 0;
    while (used < max_len && tu_desc_type(p) != TUSB_DESC_INTERFACE) {
        if (tu_desc_type(p) == TUSB_DESC_ENDPOINT && found < itf->bNumEndpoints) {
            tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
            TU_ASSERT(usbd_edpt_open(rhport, ep), 0);
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) raw_ep_in = ep->bEndpointAddress;
            else raw_ep_out = ep->bEndpointAddress;
            found++;
        }
        used = (uint16_t)(used + tu_desc_len(p));
        p = tu_desc_next(p);
    }
    if (raw_ep_out) EDPT_XFER(rhport, raw_ep_out, raw_buf.out, 64);
    return used;
}

static bool raw_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (!is_raw()) return false;
    pad_setup_t setup = {request->bmRequestType, request->bRequest, request->wValue, request->wIndex, request->wLength};
    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        int n = app_mode->control_in ? app_mode->control_in(&setup, raw_buf.ctrl, 64) : -1;
        app_log("ctl %02X %02X %04X n=%d", setup.bmRequestType, setup.bRequest, setup.wValue, n);
        if (n < 0) return false;
        return tud_control_xfer(rhport, request, raw_buf.ctrl, (uint16_t)n);
    }
    if (stage == CONTROL_STAGE_SETUP) {
        if (!app_mode->control_out || request->wLength > 64) return false;
        return tud_control_xfer(rhport, request, raw_buf.ctrl, request->wLength);
    }
    if (stage == CONTROL_STAGE_DATA) return app_mode->control_out(&setup, raw_buf.ctrl, request->wLength);
    return true;
}

// Vendor-type requests bypass the class driver table in TinyUSB.
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    return raw_control(rhport, stage, request);
}

static bool raw_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t bytes) {
    if (ep == raw_ep_out) {
        if (result == XFER_RESULT_SUCCESS && app_mode->out) app_mode->out(raw_buf.out, (uint16_t)bytes);
        EDPT_XFER(rhport, raw_ep_out, raw_buf.out, 64);
    }
    return true;
}

static const usbd_class_driver_t raw_driver = {
    .name = "PAD",
    .init = raw_init,
    .deinit = raw_deinit,
    .reset = raw_reset,
    .open = raw_open,
    .control_xfer_cb = raw_control,
    .xfer_cb = raw_xfer,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
    *count = 1;
    return &raw_driver;
}

static bool raw_send(const uint8_t *report, uint8_t len) {
    if (!raw_ep_in || !usbd_edpt_claim(0, raw_ep_in)) return false;
    memcpy(raw_buf.in, report, len);
    return EDPT_XFER(0, raw_ep_in, raw_buf.in, len);
}

// ---------------------------------------------------------------- reports

bool usb_link_connected(void) { return is_usb() && tud_mounted() && !tud_suspended(); }

void usb_link_task(void) {
    if (!is_usb()) return;
    uint32_t now = plat_millis();
    if (tud_suspended()) {
        // Any pressed button wakes a sleeping host, like a real pad.
        static uint32_t last_wake;
        pad_state_t peek;
        app_next_state(&peek);
        if (peek.buttons && now - last_wake >= 100) {
            last_wake = now;
            tud_remote_wakeup();
        }
        return;
    }
    if (now - last_send < app_mode->interval_ms || !tud_ready()) return;
    if (is_raw() ? (!raw_ep_in || usbd_edpt_busy(0, raw_ep_in)) : !tud_hid_ready()) return;
    uint8_t report[PAD_REPORT_MAX];
    pad_state_t state;
    app_next_state(&state);
    app_mode->build(&state, now, report);
    bool sent = is_raw() ? raw_send(report, app_mode->report_len)
                         : tud_hid_report(app_mode->report_id, report, app_mode->report_len);
    if (sent) {
        last_send = now;
        app_reports_sent++;
    }
}
