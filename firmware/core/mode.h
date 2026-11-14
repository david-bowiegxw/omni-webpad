// A controller personality: the descriptors a host sees and the translation
// between the canonical pad state and that controller's wire format.
// Personalities are plain C with no USB stack dependency so they can be
// unit-tested on a desktop and reused by every board.
#pragma once
#include "pad.h"

typedef enum {
    PAD_LINK_USB_HID,   // standard HID class interface
    PAD_LINK_USB_RAW,   // vendor interface with interrupt IN/OUT endpoints
    PAD_LINK_BLE_HID,   // HID over GATT
} pad_link_t;

enum { PAD_REPORT_INPUT = 1, PAD_REPORT_OUTPUT = 2, PAD_REPORT_FEATURE = 3 };

typedef struct {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
} pad_setup_t;

#define PAD_REPORT_MAX 64

typedef struct pad_mode {
    const char *id;        // stable identifier used by the API and settings
    const char *name;      // what the board pretends to be
    const char *family;    // UI skin: "ps", "xbox", "switch" or "generic"
    const char *hosts;     // short human-readable list of targets
    pad_link_t link;

    const uint8_t *device_desc;                         // 18 bytes
    const uint8_t *config_desc;  uint16_t config_len;
    const uint8_t *report_desc;  uint16_t report_desc_len;
    const char *manufacturer, *product, *serial;        // NULL = no string

    uint8_t report_id;     // 0 = reports are not numbered
    uint8_t report_len;    // input payload length, excluding the report ID
    uint8_t interval_ms;   // minimum spacing between input reports, per pad

    // Several pads behind one device. `players` is how many the descriptors
    // can carry; player n is the n-th HID interface, or the n-th vendor
    // interface that has endpoints. `multi` is the variant a single-pad
    // personality turns into when more than one player is configured.
    uint8_t players;       // 0 or 1: a single pad
    const struct pad_mode *multi;
    // Builds the configuration descriptor for `players` pads when it is not
    // fixed (config_desc is then NULL). Returns its length.
    uint16_t (*config)(uint8_t players, uint8_t *buf, uint16_t cap);
    // Packet announcing that a player slot gained or lost its controller
    // (wireless receivers). Returns its length, 0 for none.
    uint8_t (*presence)(bool attached, uint8_t *out);

    void (*reset)(void);
    void (*build)(const pad_state_t *in, uint32_t now_ms, uint8_t *out);
    // HID GET_REPORT / SET_REPORT. Buffers never include the report ID.
    uint16_t (*get_report)(uint8_t type, uint8_t id, uint8_t *buf, uint16_t cap);
    void (*set_report)(uint8_t player, uint8_t type, uint8_t id, const uint8_t *buf, uint16_t len);
    // Raw links: control IN returns a length or -1 to stall; control OUT
    // receives the data stage; out() receives interrupt OUT packets.
    int (*control_in)(const pad_setup_t *setup, uint8_t *buf, uint16_t cap);
    bool (*control_out)(const pad_setup_t *setup, const uint8_t *buf, uint16_t len);
    void (*out)(uint8_t player, const uint8_t *buf, uint16_t len);
} pad_mode_t;

#define PAD_LINK_MASK_USB (1u << PAD_LINK_USB_HID | 1u << PAD_LINK_USB_RAW)
#define PAD_LINK_MASK_BLE (1u << PAD_LINK_BLE_HID)

extern const pad_mode_t *const pad_modes[];
extern const unsigned pad_mode_count;

const pad_mode_t *pad_mode_find(const char *id);
// First personality usable with the given link mask, preferring `wanted`.
const pad_mode_t *pad_mode_select(const char *wanted, unsigned link_mask);
// Most players a personality can serve (1 when it has no multi variant).
uint8_t pad_mode_max_players(const pad_mode_t *mode);

// Shared report map for the generic USB and Bluetooth LE gamepads.
#define PAD_GENERIC_REPORT_LEN 9
extern const uint8_t pad_generic_report_desc[];
extern const uint16_t pad_generic_report_desc_len;
extern const uint8_t pad_ble_report_desc[];
extern const uint16_t pad_ble_report_desc_len;
void pad_generic_build(const pad_state_t *in, uint32_t now_ms, uint8_t *out);

// USB descriptor building blocks (raw bytes, little endian).
#define USB_LE16(v) (uint8_t)((v) & 0xff), (uint8_t)((v) >> 8)
#define USB_DEVICE_DESC(bcd_usb, cls, sub, proto, vid, pid, bcd_dev, mfr, prod, ser) \
    18, 0x01, USB_LE16(bcd_usb), cls, sub, proto, 64, USB_LE16(vid), USB_LE16(pid), \
    USB_LE16(bcd_dev), mfr, prod, ser, 1
#define PAD_CONFIG_MAX 256   // largest runtime-built configuration descriptor
// Configuration with one HID interface, interrupt IN 0x81 and OUT 0x02.
#define USB_HID_CONFIG_LEN 41
#define USB_HID_CONFIG_DESC(attributes, report_len, interval) \
    9, 0x02, USB_LE16(USB_HID_CONFIG_LEN), 1, 1, 0, attributes, 250, \
    9, 0x04, 0, 0, 2, 0x03, 0, 0, 0, \
    9, 0x21, USB_LE16(0x0111), 0, 1, 0x22, USB_LE16(report_len), \
    7, 0x05, 0x02, 0x03, USB_LE16(64), interval, \
    7, 0x05, 0x81, 0x03, USB_LE16(64), interval
