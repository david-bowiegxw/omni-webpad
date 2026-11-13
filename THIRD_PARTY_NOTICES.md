# Third-party notices

Original project code: MIT, see [LICENSE](LICENSE).

| Component | Origin | License / included notice |
| --- | --- | --- |
| DS3 USB descriptor and feature tables; protocol adaptation | [Ivves Hermosilla / dualsense-to-dualshock3-sixaxis](https://github.com/ivveshermosilla/dualsense-to-dualshock3-sixaxis) | [MIT](licenses/LICENSE-S3-MIT.txt) |
| DHCP server | [MicroPython](https://github.com/micropython/micropython), through [Pico examples](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point) | [MIT](licenses/LICENSE-DHCP.txt); original headers retained |
| lwIP example configuration; BOOTSEL button read routine | [Pico examples](https://github.com/raspberrypi/pico-examples) | Raspberry Pi [BSD-3-Clause](licenses/Pico-SDK-BSD-3-Clause.txt) |
| Pico SDK 2.2.0 | [Raspberry Pi](https://github.com/raspberrypi/pico-sdk/tree/2.2.0) | [BSD-3-Clause](licenses/Pico-SDK-BSD-3-Clause.txt) |
| TinyUSB | [TinyUSB](https://github.com/hathach/tinyusb) | [MIT](licenses/TinyUSB-MIT.txt) |
| lwIP | [lwIP](https://savannah.nongnu.org/projects/lwip/) | [BSD](licenses/lwIP-BSD.txt) |
| CYW43 driver | [Georgerobotics](https://github.com/georgerobotics/cyw43-driver) | [Raspberry Pi silicon license](licenses/CYW43-Raspberry-Pi.txt); [alternative upstream terms](licenses/CYW43-alternative.txt) |
| pico_printf | Pico SDK / Marco Paland | [MIT](licenses/printf-MIT.txt) |
| ESP-IDF, including its lwIP port, Wi-Fi/Bluetooth libraries and the Apache NimBLE host (ESP32 builds only) | [Espressif](https://github.com/espressif/esp-idf) | Apache-2.0 and component licenses; see the [ESP-IDF copyright notes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/COPYRIGHT.html) |
| TinyUSB component for ESP-IDF (ESP32-S2/S3 builds only) | [espressif/tinyusb](https://github.com/espressif/tinyusb) | [MIT](licenses/TinyUSB-MIT.txt) |

The SDK and dependencies are fetched separately and are not vendored here. Firmware builds include those dependencies, including the SDK-provided CYW43 Wi-Fi firmware. Refer to the exact SDK and dependency source licenses when redistributing compiled firmware. The project MIT license does not relicense third-party components or authorize use of the CYW43 Raspberry Pi license on non-Raspberry-Pi silicon. Release downloads include these notices; retain them when redistributing.

The CYW43 terms apply to Pico W / Pico 2 W builds only. ESP32 builds contain Espressif's binary radio libraries under the ESP-IDF terms.

PlayStation, DualShock and DualSense are Sony trademarks; Xbox is a Microsoft trademark; Nintendo Switch is a Nintendo trademark; HORI is a HORI trademark; Raspberry Pi and Pico are Raspberry Pi trademarks; ESP32 is an Espressif trademark. USB identification strings are used for protocol compatibility. This project is not endorsed by any of these companies.
