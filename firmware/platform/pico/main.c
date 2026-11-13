// Raspberry Pi Pico W / Pico 2 W: CYW43 Wi-Fi access point, lwIP raw TCP,
// TinyUSB device on the native USB port.
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/unique_id.h"
#include "hardware/flash.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "tusb.h"
#include "dhcpserver.h"
#include "app.h"
#include "platform.h"
#include "server.h"
#include "usb_link.h"
#include <string.h>

#define MAX_CLIENTS 6
#define IDLE_TIMEOUT_MS 10000
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FACTORY_RESET_HOLD_MS 5000

// ---------------------------------------------------------------- platform

uint32_t plat_millis(void) { return to_ms_since_boot(get_absolute_time()); }
const char *plat_board_name(void) { return PICO_BOARD; }
unsigned plat_link_mask(void) { return PAD_LINK_MASK_USB; }
bool plat_link_connected(void) { return usb_link_connected(); }

void plat_reboot(void) {
    tud_disconnect();
    watchdog_reboot(0, 0, 50);
    for (;;) tight_loop_contents();
}

bool plat_settings_read(uint8_t *data, unsigned len) {
    memcpy(data, (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET), len);
    return true;
}

static void flash_write(void *param) {
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, param, FLASH_PAGE_SIZE);
}

bool plat_settings_write(const uint8_t *data, unsigned len) {
    static uint8_t page[FLASH_PAGE_SIZE];
    if (len > sizeof(page)) return false;
    memset(page, 0xff, sizeof(page));
    memcpy(page, data, len);
    return flash_safe_execute(flash_write, page, 500) == PICO_OK;
}

// The BOOTSEL button shares the flash chip-select line, so reading it means
// briefly taking that pin away from the flash. Must run from RAM.
static bool __no_inline_not_in_flash_func(bootsel_pressed)(void) {
    const uint cs_pin = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[cs_pin].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) {}
#if PICO_RP2040
    bool pressed = !(sio_hw->gpio_hi_in & (1u << cs_pin));
#else
    bool pressed = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#endif
    hw_write_masked(&ioqspi_hw->io[cs_pin].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
}

// ---------------------------------------------------------------- network

typedef struct {
    struct tcp_pcb *pcb;
    const uint8_t *blob;          // static data still to be queued
    unsigned blob_left;
    bool close_requested;
    uint32_t last_activity;
    server_conn_t conn;
} client_t;

static client_t clients[MAX_CLIENTS];

static void pump(client_t *c) {
    while (c->blob_left) {
        unsigned n = tcp_sndbuf(c->pcb);
        if (n > c->blob_left) n = c->blob_left;
        if (n > 2 * TCP_MSS) n = 2 * TCP_MSS;
        if (!n || tcp_write(c->pcb, c->blob, (u16_t)n, 0) != ERR_OK) break;
        c->blob += n;
        c->blob_left -= n;
    }
    tcp_output(c->pcb);
}

bool net_send(void *io, const void *data, unsigned len, bool is_static) {
    client_t *c = io;
    if (!c->pcb || c->blob_left) return false;
    if (is_static) {
        c->blob = data;
        c->blob_left = len;
        pump(c);
        return true;
    }
    if (len > tcp_sndbuf(c->pcb) || tcp_write(c->pcb, data, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    tcp_output(c->pcb);
    return true;
}

void net_close(void *io) { ((client_t *)io)->close_requested = true; }

static void detach(client_t *c) {
    struct tcp_pcb *pcb = c->pcb;
    tcp_arg(pcb, NULL); tcp_sent(pcb, NULL); tcp_recv(pcb, NULL); tcp_poll(pcb, NULL, 0); tcp_err(pcb, NULL);
    c->pcb = NULL;
    server_closed(&c->conn);
}

static err_t finish(client_t *c, bool abort) {
    struct tcp_pcb *pcb = c->pcb;
    detach(c);
    if (abort || tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static void on_error(void *arg, err_t err) {
    client_t *c = arg;
    (void)err;
    if (!c) return;
    c->pcb = NULL;                 // lwIP already freed the pcb
    server_closed(&c->conn);
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    client_t *c = arg;
    (void)pcb; (void)len;
    pump(c);
    if (c->close_requested && !c->blob_left) return finish(c, false);
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    client_t *c = arg;
    (void)pcb;
    if (plat_millis() - c->last_activity > IDLE_TIMEOUT_MS) return finish(c, true);
    pump(c);
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    client_t *c = arg;
    (void)err;
    if (!p) return finish(c, false);
    bool ok = true;
    for (struct pbuf *q = p; q && ok; q = q->next) ok = server_feed(&c->conn, q->payload, q->len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    c->last_activity = plat_millis();
    if (!ok) return finish(c, true);
    if (c->close_requested && !c->blob_left) return finish(c, false);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    client_t *c = NULL;
    for (unsigned i = 0; i < MAX_CLIENTS && !c; i++)
        if (!clients[i].pcb) c = &clients[i];
    if (err != ERR_OK || !c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    c->pcb = pcb;
    c->blob_left = 0;
    c->close_requested = false;
    c->last_activity = plat_millis();
    server_open(&c->conn, c);
    tcp_arg(pcb, c); tcp_recv(pcb, on_recv); tcp_sent(pcb, on_sent); tcp_err(pcb, on_error);
    tcp_poll(pcb, on_poll, 2);
    tcp_nagle_disable(pcb);
    return ERR_OK;
}

// ---------------------------------------------------------------- main

static void fatal(unsigned blinks) {
    // Without Wi-Fi there is no UI; blink the on-board LED if it is reachable.
    for (;;) {
        for (unsigned i = 0; i < blinks; i++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); sleep_ms(150);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); sleep_ms(150);
        }
        sleep_ms(1000);
    }
}

int main(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    pad_identity[0] = 0x02;
    memcpy(pad_identity + 1, id.id + 3, 5);

    app_init();
    tusb_init();
    if (cyw43_arch_init()) return 1;
    cyw43_arch_enable_ap_mode(app_settings.ssid, app_settings.pass, CYW43_AUTH_WPA2_AES_PSK);
    // Power saving adds tens of milliseconds of input latency.
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);

    ip_addr_t ip, mask;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    static dhcp_server_t dhcp;
    dhcp_server_init(&dhcp, &ip, &mask);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, 80) != ERR_OK) fatal(2);
    pcb = tcp_listen_with_backlog(pcb, 4);
    if (!pcb) fatal(3);
    tcp_accept(pcb, on_accept);

    uint32_t last_slow = 0, held_since = 0;
    for (;;) {
        tud_task();
        cyw43_arch_poll();
        usb_link_task();
        server_poll();
        app_poll();

        uint32_t now = plat_millis();
        if (now - last_slow >= 100) {
            last_slow = now;
            if (!bootsel_pressed()) held_since = 0;
            else if (!held_since) held_since = now ? now : 1;
            else if (now - held_since > FACTORY_RESET_HOLD_MS) {
                app_settings_t defaults;
                app_settings_defaults(&defaults);
                app_settings_save(&defaults);
                plat_reboot();
            }
            // Solid: host connected. Slow blink: waiting. Fast blink: BOOTSEL held.
            bool led = held_since ? (now / 100) & 1 : plat_link_connected() ? true : (now / 500) & 1;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        }
        sleep_us(100);
    }
}
