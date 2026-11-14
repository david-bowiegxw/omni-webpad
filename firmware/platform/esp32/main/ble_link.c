#include "ble_link.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED

#include "app.h"
#include "platform.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

void ble_store_config_init(void);

#define DEVICE_NAME "OmniWebPad"
#define APPEARANCE_GAMEPAD 0x03c4
#define NO_CONNECTION 0xffff

enum { CHR_MANUFACTURER, CHR_PNP_ID, CHR_BATTERY, CHR_HID_INFO, CHR_REPORT_MAP, CHR_CONTROL_POINT,
       CHR_PROTOCOL_MODE, CHR_INPUT_REPORT, DSC_REPORT_REFERENCE };

static const char *TAG = "ble";
static uint8_t own_addr_type;
static volatile uint16_t connection = NO_CONNECTION;
static volatile bool subscribed;
static uint16_t input_handle;
static uint8_t last_report[PAD_REPORT_MAX];
static uint32_t last_send;

static int gap_event(struct ble_gap_event *event, void *arg);

static int access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle;
    // PnP ID: vendor ID source = USB-IF, then VID, PID and version (pid.codes test ID).
    static const uint8_t pnp_id[] = {0x02, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01};
    static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x02};      // HID 1.11, normally connectable
    static const uint8_t battery = 100, protocol_mode = 1;           // report protocol
    static const uint8_t report_reference[] = {0x01, 0x01};          // report ID 1, input
    const void *data = NULL;
    uint16_t len = 0;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR || ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) return 0;
    switch ((intptr_t)arg) {
        case CHR_MANUFACTURER: data = DEVICE_NAME; len = sizeof(DEVICE_NAME) - 1; break;
        case CHR_PNP_ID: data = pnp_id; len = sizeof(pnp_id); break;
        case CHR_BATTERY: data = &battery; len = 1; break;
        case CHR_HID_INFO: data = hid_info; len = sizeof(hid_info); break;
        case CHR_REPORT_MAP: data = pad_ble_report_desc; len = pad_ble_report_desc_len; break;
        case CHR_PROTOCOL_MODE: data = &protocol_mode; len = 1; break;
        case CHR_INPUT_REPORT: data = last_report; len = PAD_GENERIC_REPORT_LEN; break;
        case DSC_REPORT_REFERENCE: data = report_reference; len = sizeof(report_reference); break;
        default: return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(ctxt->om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

#define CHR(uuid16, id, chr_flags) \
    {.uuid = BLE_UUID16_DECLARE(uuid16), .access_cb = access_cb, .arg = (void *)(intptr_t)(id), .flags = (chr_flags)}

static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180a),   // Device Information
     .characteristics = (struct ble_gatt_chr_def[]){
         CHR(0x2a29, CHR_MANUFACTURER, BLE_GATT_CHR_F_READ),
         CHR(0x2a50, CHR_PNP_ID, BLE_GATT_CHR_F_READ),
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180f),   // Battery
     .characteristics = (struct ble_gatt_chr_def[]){
         CHR(0x2a19, CHR_BATTERY, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),   // Human Interface Device
     .characteristics = (struct ble_gatt_chr_def[]){
         CHR(0x2a4a, CHR_HID_INFO, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC),
         CHR(0x2a4b, CHR_REPORT_MAP, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC),
         CHR(0x2a4c, CHR_CONTROL_POINT, BLE_GATT_CHR_F_WRITE_NO_RSP),
         CHR(0x2a4e, CHR_PROTOCOL_MODE, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP),
         {.uuid = BLE_UUID16_DECLARE(0x2a4d), .access_cb = access_cb, .arg = (void *)(intptr_t)CHR_INPUT_REPORT,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
          .val_handle = &input_handle,
          .descriptors = (struct ble_gatt_dsc_def[]){
              {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = access_cb,
               .arg = (void *)(intptr_t)DSC_REPORT_REFERENCE},
              {0}}},
         {0}}},
    {0},
};

static void advertise(void) {
    static const ble_uuid16_t hid_service = BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = APPEARANCE_GAMEPAD;
    fields.appearance_is_present = 1;
    fields.uuids16 = &hid_service;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = sizeof(DEVICE_NAME) - 1;
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) { ESP_LOGE(TAG, "adv fields rc=%d", rc); return; }
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "adv start rc=%d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                connection = event->connect.conn_handle;
                app_log("ble connect");
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            connection = NO_CONNECTION;
            subscribed = false;
            pad_feedback_reset();
            app_log("ble disconnect %d", event->disconnect.reason);
            advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == input_handle) {
                subscribed = event->subscribe.cur_notify;
                if (subscribed) {
                    // Ask for a short connection interval (7.5-15 ms) for low latency.
                    struct ble_gap_upd_params fast = {.itvl_min = 6, .itvl_max = 12, .latency = 0, .supervision_timeout = 400};
                    ble_gap_update_params(event->subscribe.conn_handle, &fast);
                }
            }
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            // The host forgot us but we still hold its bond: drop ours and pair again.
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        default:
            break;
    }
    return 0;
}

static void on_sync(void) {
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_addr_type)) {
        ESP_LOGE(TAG, "no usable address");
        return;
    }
    advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset %d", reason); }

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ble_link_available(void) { return true; }
bool ble_link_connected(void) { return connection != NO_CONNECTION && subscribed; }

void ble_link_start(void) {
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // "Just works" bonding: HID hosts require an encrypted, bonded link.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) {
        ESP_LOGE(TAG, "GATT registration failed");
        return;
    }
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_device_appearance_set(APPEARANCE_GAMEPAD);
    ble_store_config_init();

    pad_state_t neutral;
    pad_neutral(&neutral);
    pad_generic_build(&neutral, 0, last_report);
    nimble_port_freertos_init(host_task);
}

void ble_link_task(void) {
    if (!app_mode || app_mode->link != PAD_LINK_BLE_HID || !ble_link_connected()) return;
    uint32_t now = plat_millis();
    if (now - last_send < app_mode->interval_ms) return;
    uint8_t report[PAD_REPORT_MAX];
    pad_state_t state;
    app_next_state(0, &state);
    app_mode->build(&state, now, report);
    // Notify on change, plus a slow refresh in case a notification was dropped.
    if (memcmp(report, last_report, app_mode->report_len) == 0 && now - last_send < 500) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, app_mode->report_len);
    if (!om || ble_gatts_notify_custom(connection, input_handle, om) != 0) return;   // retried next tick
    memcpy(last_report, report, app_mode->report_len);
    last_send = now;
    app_reports_sent++;
}

#else   // no Bluetooth on this target

bool ble_link_available(void) { return false; }
void ble_link_start(void) {}
void ble_link_task(void) {}
bool ble_link_connected(void) { return false; }

#endif
