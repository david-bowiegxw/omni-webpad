# Third-party notices

Original project code: MIT, see [LICENSE](LICENSE).

| Component | Origin | License / included notice |
| --- | --- | --- |
| DS3 USB descriptor and feature tables; protocol adaptation | [Ivves Hermosilla / dualsense-to-dualshock3-sixaxis](https://github.com/ivveshermosilla/dualsense-to-dualshock3-sixaxis) | [MIT](src/LICENSE-S3-MIT.txt) |
| DHCP server | [MicroPython](https://github.com/micropython/micropython), through [Pico examples](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point) | [MIT](src/LICENSE-DHCP.txt); original headers retained |
| lwIP example configuration | [Pico examples](https://github.com/raspberrypi/pico-examples) | Raspberry Pi BSD-3-Clause; see notices below |
| Pico SDK 2.2.0 | [Raspberry Pi](https://github.com/raspberrypi/pico-sdk/tree/2.2.0) | [BSD-3-Clause](licenses/Pico-SDK-BSD-3-Clause.txt) |
| TinyUSB | [TinyUSB](https://github.com/hathach/tinyusb) | [MIT](licenses/TinyUSB-MIT.txt) |
| lwIP | [lwIP](https://savannah.nongnu.org/projects/lwip/) | [BSD](licenses/lwIP-BSD.txt) |
| CYW43 driver | [Georgerobotics](https://github.com/georgerobotics/cyw43-driver) | [Raspberry Pi silicon license](licenses/CYW43-Raspberry-Pi.txt); [alternative upstream terms](licenses/CYW43-alternative.txt) |
| pico_printf | Pico SDK / Marco Paland | [MIT](licenses/printf-MIT.txt) |

The SDK and dependencies are fetched separately and are not vendored here. Firmware builds include those dependencies, including the SDK-provided CYW43 Wi-Fi firmware. Refer to the exact SDK and dependency source licenses when redistributing compiled firmware. The project MIT license does not relicense third-party components or authorize use of the CYW43 Raspberry Pi license on non-Raspberry-Pi silicon. Release downloads include these notices; retain them when redistributing.

PlayStation and DualShock are Sony trademarks. Raspberry Pi and Pico are Raspberry Pi trademarks. USB identification strings are used for protocol compatibility. This project is not endorsed by either company.
