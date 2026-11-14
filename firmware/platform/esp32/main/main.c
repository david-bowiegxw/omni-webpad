// ESP32 family: Wi-Fi access point + BSD sockets, with native USB (S2/S3)
// and/or a Bluetooth LE gamepad (S3, C3, C6, classic ESP32) as the link.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "app.h"
#include "ble_link.h"
#include "platform.h"
#include "server.h"
#include "sock_net.h"
#include <string.h>

#if SOC_USB_OTG_SUPPORTED
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "usb_link.h"
#define HAVE_USB 1
#else
#define HAVE_USB 0
#endif

#define FACTORY_RESET_HOLD_MS 5000
static const char *TAG = "omniwebpad";

// ---------------------------------------------------------------- platform

uint32_t plat_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
const char *plat_board_name(void) { return CONFIG_IDF_TARGET; }

unsigned plat_link_mask(void) {
    return (HAVE_USB ? PAD_LINK_MASK_USB : 0) | (ble_link_available() ? PAD_LINK_MASK_BLE : 0);
}

bool plat_link_connected(void) {
#if HAVE_USB
    if (usb_link_connected()) return true;
#endif
    return ble_link_connected();
}

void plat_reboot(void) {
#if HAVE_USB
    if (app_mode && app_mode->link != PAD_LINK_BLE_HID) tud_disconnect();
#endif
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

bool plat_settings_read(uint8_t *data, unsigned len) {
    nvs_handle_t nvs;
    size_t size = len;
    if (nvs_open("omniwebpad", NVS_READONLY, &nvs) != ESP_OK) return false;
    bool ok = nvs_get_blob(nvs, "settings", data, &size) == ESP_OK && size == len;
    nvs_close(nvs);
    return ok;
}

bool plat_settings_write(const uint8_t *data, unsigned len) {
    nvs_handle_t nvs;
    if (nvs_open("omniwebpad", NVS_READWRITE, &nvs) != ESP_OK) return false;
    bool ok = nvs_set_blob(nvs, "settings", data, len) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

// ---------------------------------------------------------------- setup

static void wifi_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();          // 192.168.4.1 with a DHCP server
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    wifi_config_t config = {0};
    size_t ssid_len = strlen(app_settings.ssid);
    memcpy(config.ap.ssid, app_settings.ssid, ssid_len);
    config.ap.ssid_len = (uint8_t)ssid_len;
    strlcpy((char *)config.ap.password, app_settings.pass, sizeof(config.ap.password));
    config.ap.channel = 6;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#if HAVE_USB
static void usb_start(void) {
    // Route the internal full-speed PHY to the USB-OTG controller.
    static usb_phy_handle_t phy;
    usb_phy_config_t config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
    };
    ESP_ERROR_CHECK(usb_new_phy(&config, &phy));
    if (!tusb_init()) ESP_LOGE(TAG, "TinyUSB init failed");
}
#endif

static void gpio_setup(void) {
#if CONFIG_OMNIWEBPAD_BUTTON_GPIO >= 0
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << CONFIG_OMNIWEBPAD_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&button);
#endif
#if CONFIG_OMNIWEBPAD_LED_GPIO >= 0
    gpio_config_t led = {.pin_bit_mask = 1ULL << CONFIG_OMNIWEBPAD_LED_GPIO, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&led);
#endif
}

static bool button_pressed(void) {
#if CONFIG_OMNIWEBPAD_BUTTON_GPIO >= 0
    return gpio_get_level(CONFIG_OMNIWEBPAD_BUTTON_GPIO) == 0;
#else
    return false;
#endif
}

static void led_set(bool on) {
#if CONFIG_OMNIWEBPAD_LED_GPIO >= 0
#if CONFIG_OMNIWEBPAD_LED_ACTIVE_LOW
    on = !on;
#endif
    gpio_set_level(CONFIG_OMNIWEBPAD_LED_GPIO, on);
#else
    (void)on;
#endif
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        memcpy(pad_identity, mac, 6);
        pad_identity[0] = (uint8_t)((pad_identity[0] | 0x02) & 0xfe);   // locally administered, unicast
    }

    app_init();
    gpio_setup();
    wifi_start();
#if HAVE_USB
    bool usb_mode = app_mode && app_mode->link != PAD_LINK_BLE_HID;
    if (usb_mode) usb_start();
#endif
    if (app_mode && app_mode->link == PAD_LINK_BLE_HID) ble_link_start();
    if (!sock_net_start(80, false)) ESP_LOGE(TAG, "cannot listen on port 80");
    ESP_LOGW(TAG, "%s: mode %s, Wi-Fi \"%s\", http://192.168.4.1", CONFIG_IDF_TARGET, app_mode ? app_mode->id : "none", app_settings.ssid);

    uint32_t last_slow = 0, held_since = 0;
    for (;;) {
        sock_net_poll(1, -1);                    // sleeps up to 1 ms, wakes early on traffic
#if HAVE_USB
        if (usb_mode) {
            tud_task_ext(0, false);
            usb_link_task();
        }
#endif
        ble_link_task();
        server_poll();
        app_poll();

        uint32_t now = plat_millis();
        if (now - last_slow >= 100) {
            last_slow = now;
            if (!button_pressed()) held_since = 0;
            else if (!held_since) held_since = now ? now : 1;
            else if (now - held_since > FACTORY_RESET_HOLD_MS) {
                app_settings_t defaults;
                app_settings_defaults(&defaults);
                app_settings_save(&defaults);
                plat_reboot();
            }
            led_set(held_since ? (now / 100) & 1 : plat_link_connected() ? true : (now / 500) & 1);
        }
    }
}
