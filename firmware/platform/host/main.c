// Desktop simulator: the real server core and controller personalities with
// the radio and USB replaced by a TCP port and stdout. Used by the tests and
// handy for working on the web UI without hardware.
//
//   omniwebpad-sim [--port 8787] [--board NAME] [--links usb,ble] [--settings FILE]
//
// stdout: "REPORT <mode> <hex>" whenever the outgoing report changes.
// stdin:  "rumble <strong> <weak>", "led <mask>", "color <r> <g> <b>",
//         "link <0|1>" simulate the host side.
#include "app.h"
#include "platform.h"
#include "server.h"
#include "sock_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *board = "simulator", *settings_path;
static unsigned links = PAD_LINK_MASK_USB;
static bool link_up = true, reboot_requested;

uint32_t plat_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
const char *plat_board_name(void) { return board; }
unsigned plat_link_mask(void) { return links; }
bool plat_link_connected(void) { return link_up; }
void plat_reboot(void) { reboot_requested = true; }

bool plat_settings_read(uint8_t *data, unsigned len) {
    FILE *f = settings_path ? fopen(settings_path, "rb") : NULL;
    if (!f) return false;
    bool ok = fread(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

bool plat_settings_write(const uint8_t *data, unsigned len) {
    FILE *f = settings_path ? fopen(settings_path, "wb") : NULL;
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

static void command(char *line) {
    unsigned a = 0, b = 0, c = 0;
    if (sscanf(line, "rumble %u %u", &a, &b) == 2) pad_set_rumble((uint8_t)a, (uint8_t)b);
    else if (sscanf(line, "led %u", &a) == 1) pad_set_led((uint8_t)a);
    else if (sscanf(line, "color %u %u %u", &a, &b, &c) == 3) pad_set_color((uint8_t)a, (uint8_t)b, (uint8_t)c);
    else if (sscanf(line, "link %u", &a) == 1) link_up = a != 0;
}

int main(int argc, char **argv) {
    unsigned port = 8787;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--port")) port = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--board")) board = argv[i + 1];
        else if (!strcmp(argv[i], "--settings")) settings_path = argv[i + 1];
        else if (!strcmp(argv[i], "--links")) {
            links = (strstr(argv[i + 1], "usb") ? PAD_LINK_MASK_USB : 0) | (strstr(argv[i + 1], "ble") ? PAD_LINK_MASK_BLE : 0);
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    int stdin_fd = isatty(0) ? -1 : 0;

    for (;;) {
        app_init();
        if (!app_mode || !sock_net_start((uint16_t)port, true)) {
            fprintf(stderr, "cannot start on port %u\n", port);
            return 1;
        }
        printf("READY %s http://127.0.0.1:%u\n", app_mode->id, port);

        uint8_t last[PAD_REPORT_MAX] = {0}, report[PAD_REPORT_MAX];
        bool first = true;
        uint32_t last_send = 0;
        reboot_requested = false;
        while (!reboot_requested) {
            if (sock_net_poll(1, stdin_fd)) {
                // Unbuffered: several commands may arrive in one read.
                static char pending[256];
                static size_t used;
                ssize_t n = read(stdin_fd, pending + used, sizeof(pending) - 1 - used);
                if (n <= 0) stdin_fd = -1;
                else {
                    used += (size_t)n;
                    pending[used] = 0;
                    char *line = pending, *end;
                    while ((end = strchr(line, '\n'))) { *end = 0; command(line); line = end + 1; }
                    used = strlen(line);
                    memmove(pending, line, used + 1);
                    if (used == sizeof(pending) - 1) used = 0;
                }
            }
            server_poll();
            app_poll();
            uint32_t now = plat_millis();
            if (now - last_send < app_mode->interval_ms) continue;
            last_send = now;
            pad_state_t state;
            app_next_state(&state);
            // A fixed clock keeps time stamps and counters out of the comparison.
            if (app_mode->reset) app_mode->reset();
            memset(report, 0, sizeof(report));
            app_mode->build(&state, 0, report);
            app_reports_sent++;
            if (first || memcmp(report, last, sizeof(report))) {
                first = false;
                memcpy(last, report, sizeof(report));
                printf("REPORT %s ", app_mode->id);
                for (unsigned i = 0; i < app_mode->report_len; i++) printf("%02x", report[i]);
                printf("\n");
            }
        }
        sock_net_stop();
        pad_feedback_reset();
        printf("REBOOT\n");
    }
}
