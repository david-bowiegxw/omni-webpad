// Everything the portable core needs from a board. Implemented once per
// platform (Pico SDK, ESP-IDF, desktop simulator).
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint32_t plat_millis(void);
const char *plat_board_name(void);
unsigned plat_link_mask(void);                 // PAD_LINK_MASK_* the board can do
bool plat_link_connected(void);                // USB configured / BLE subscribed
void plat_reboot(void);                        // does not return on real hardware
bool plat_settings_read(uint8_t *data, unsigned len);
bool plat_settings_write(const uint8_t *data, unsigned len);

// Byte-stream transport for the HTTP/WebSocket server. `io` is the opaque
// handle the platform passed to server_open(). Static data stays valid
// forever and may be sent without copying. Returns false when the data could
// not be queued; the server then closes the connection.
bool net_send(void *io, const void *data, unsigned len, bool is_static);
void net_close(void *io);                      // flush, then close
