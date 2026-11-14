#include "sha1.h"
#include <string.h>

static uint32_t rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }

static void block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (unsigned i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (unsigned i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (unsigned i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const uint8_t *data, unsigned len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    uint8_t tail[128];
    unsigned full = len / 64, rest = len % 64;
    for (unsigned i = 0; i < full; i++) block(h, data + 64 * i);
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data + 64 * full, rest);
    tail[rest] = 0x80;
    unsigned total = rest < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (unsigned i = 0; i < 8; i++) tail[total - 1 - i] = (uint8_t)(bits >> (8 * i));
    block(h, tail);
    if (total == 128) block(h, tail + 64);
    for (unsigned i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i];
    }
}

unsigned base64_encode(const uint8_t *data, unsigned len, char *out) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned n = 0;
    for (unsigned i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out[n++] = table[v >> 18];
        out[n++] = table[(v >> 12) & 63];
        out[n++] = i + 1 < len ? table[(v >> 6) & 63] : '=';
        out[n++] = i + 2 < len ? table[v & 63] : '=';
    }
    out[n] = 0;
    return n;
}
