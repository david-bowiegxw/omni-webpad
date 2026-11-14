// Bluetooth LE HID-over-GATT gamepad (NimBLE). Compiled to no-ops on
// targets without Bluetooth.
#pragma once
#include <stdbool.h>

bool ble_link_available(void);
void ble_link_start(void);
void ble_link_task(void);
bool ble_link_connected(void);
