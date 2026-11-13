# Changelog

## 0.4.0 — OmniWebPad

The project is renamed from *PS3 Pico W Webpad* and is no longer tied to one console or one board.

### Controllers

- New controller types, selectable at runtime and stored on the board: Xbox 360 (XInput), original Xbox (XID), Nintendo Switch wired pad, DualShock 4, DualSense, generic HID gamepad and a Bluetooth LE gamepad. DualShock 3 is kept with byte-identical USB descriptors and reports.
- Host feedback for every type that has it: rumble, player LEDs and light bar colour are forwarded to the page.
- Analog triggers in the state model; a digital trigger press is a full pull.

### Boards

- Raspberry Pi Pico 2 W (RP2350) alongside the Pico W.
- ESP32-S2 and ESP32-S3 over native USB; ESP32-S3, ESP32-C3, ESP32-C6 and the classic ESP32 as Bluetooth LE gamepads.
- Firmware split into a portable core, a shared TinyUSB layer and per-platform code; a desktop simulator runs the same core for tests and UI work.

### Interface

- Binary WebSocket transport with an HTTP polling fallback; round-trip time, host link and feedback in a status pill.
- Skins for Xbox, PlayStation, Switch and generic pads.
- D-pad is one eight-way touch zone; face buttons support sliding and two-button presses.
- Two left-hand layouts (D-pad on top / stick on top), a wider canvas on tall phones.
- Settings page: controller type, Wi-Fi name and password, stick travel, rumble and key haptics, diagnostics, factory reset. Language follows the browser.
- Presses are held for at least 40 ms and queued on the board, so short taps always reach the host.

### Changed

- Default Wi-Fi is now `OmniWebPad-XXXX` / `omniwebpad`, and USB boards start as an Xbox 360 pad. Select *DualShock 3* in settings for the previous PS3 behaviour.
- Holding BOOTSEL (Pico) or BOOT (ESP32) for five seconds restores default settings.
- `/state` no longer accepts the `ax ay az gz` motion fields; motion travels in the WebSocket frame. `/status` moved to `/api/status`.
- Pico UF2 files are produced by picotool through the Pico SDK.

### Verification

Unit and browser tests cover the report formats and the whole phone-to-report path, and CI cross-compiles all seven boards. Apart from the DualShock 3 descriptors and reports inherited from the console-tested firmware, the new controller types and boards have not yet been run on hardware.

## 0.3.0 — first public pre-release

- Fix overlapping controls and replace portrait reflow with rotation of a landscape controller layout.
- Move analog sticks inward/below the D-pad and face buttons; separate L3/R3.
- Use a touch-relative origin, adjustable travel, multi-touch ownership and release handling.
- Coalesce latest inputs, acknowledge short taps, remove the extra 24 ms delay, and reuse HTTP connections.
- Correct operational status; clear output feedback on USB unmount.
- Make motion/vibration opt-in and explain browser restrictions.
- Add English/Chinese docs and UI, portable CMake, CI and mobile interaction tests.

Earlier private v0.1 recovery controls worked on a CECHL00 PS3; v0.2 had user-reported UI defects.
