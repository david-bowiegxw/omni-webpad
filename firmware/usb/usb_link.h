// TinyUSB device glue shared by every USB-capable board. The active
// personality (app_mode) supplies descriptors and report translation.
#pragma once
#include <stdbool.h>

void usb_link_task(void);        // call often; sends input reports when due
bool usb_link_connected(void);
