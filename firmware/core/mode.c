#include "mode.h"
#include <string.h>

extern const pad_mode_t mode_ds3, mode_ds4, mode_dualsense, mode_xinput, mode_xid,
    mode_switch, mode_hid, mode_ble;

const pad_mode_t *const pad_modes[] = {
    &mode_xinput, &mode_switch, &mode_ds4, &mode_dualsense, &mode_ds3,
    &mode_xid, &mode_hid, &mode_ble,
};
const unsigned pad_mode_count = sizeof(pad_modes) / sizeof(pad_modes[0]);

const pad_mode_t *pad_mode_find(const char *id) {
    for (unsigned i = 0; id && i < pad_mode_count; i++)
        if (strcmp(pad_modes[i]->id, id) == 0) return pad_modes[i];
    return NULL;
}

const pad_mode_t *pad_mode_select(const char *wanted, unsigned link_mask) {
    const pad_mode_t *mode = pad_mode_find(wanted);
    if (mode && (link_mask & (1u << mode->link))) return mode;
    for (unsigned i = 0; i < pad_mode_count; i++)
        if (link_mask & (1u << pad_modes[i]->link)) return pad_modes[i];
    return NULL;
}
