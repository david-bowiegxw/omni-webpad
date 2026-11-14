// TCP transport for the server core on top of BSD sockets. Used by ESP-IDF
// (lwIP sockets) and by the desktop simulator.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool sock_net_start(uint16_t port, bool loopback_only);
void sock_net_stop(void);
// Waits up to timeout_ms for traffic, then accepts/reads/feeds the server.
// extra_fd (or -1) is also watched; returns true when it is readable.
bool sock_net_poll(unsigned timeout_ms, int extra_fd);
