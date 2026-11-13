#pragma once
#include <stdint.h>
// SHA-1 and Base64, only for the WebSocket opening handshake (RFC 6455).
void sha1(const uint8_t *data, unsigned len, uint8_t out[20]);
unsigned base64_encode(const uint8_t *data, unsigned len, char *out);   // returns length, NUL terminated
