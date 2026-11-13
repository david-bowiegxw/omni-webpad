# Protocol and implementation notes

The board runs a Wi-Fi access point at `192.168.4.1` and serves one self-contained, gzip-compressed page. There is no cloud service and no per-request authentication: the Wi-Fi password is the access control.

## Canonical pad state

Everything between the page and a controller personality uses one positional state (`firmware/core/pad.h`).

| Bit | Button | Bit | Button |
| --- | --- | --- | --- |
| 0 | Select / Share / Create / Back / View / − | 9 | R2 / RT / ZR |
| 1 | L3 | 10 | L1 / LB / L / White |
| 2 | R3 | 11 | R1 / RB / R / Black |
| 3 | Start / Options / Menu / + | 12 | North: △ / Xbox Y / Switch X |
| 4 | Up | 13 | East: ○ / Xbox B / Switch A |
| 5 | Right | 14 | South: ✕ / Xbox A / Switch B |
| 6 | Down | 15 | West: □ / Xbox X / Switch Y |
| 7 | Left | 16 | Home: PS / Guide / Home |
| 8 | L2 / LT / ZL | 17 | Misc: touchpad click / Capture |

Sticks are 0–255 with 128 centred and +y down. Triggers are 0–255. A digital trigger bit with a zero analog value becomes 255, and a non-zero analog value sets the bit, so both forms always agree. Optional motion is accelerometer x/y/z in milli-g and gyro x/y/z in 0.1 °/s, in a frame with x right, y away from the player and z out of the pad face.

## WebSocket `/ws`

Binary frames, little endian. Client frames must be masked and unfragmented (as browsers send them).

Client → board:

| Type | Bytes | Meaning |
| --- | --- | --- |
| `0x01` state | `01 seq buttons(u32) lx ly rx ry lt rt` — 12 bytes | Full pad state |
| `0x01` state + motion | the above, then `accel x,y,z (i16)` and `gyro x,y,z (i16)` — 24 bytes | Same, with motion |
| `0x02` ping | `02 token` | Asks for a status frame echoing `token` |

Board → client:

| Type | Bytes | Meaning |
| --- | --- | --- |
| `0x82` status | `82 token flags strong weak led r g b` — 9 bytes | `flags` bit 0: host link up (USB configured, or BLE subscribed). `strong`/`weak`: rumble motors. `led`: player LED mask. `r g b`: light bar |

A status frame is sent on connect (`token` 0), in reply to each ping, and unsolicited (`token` 0) whenever the link state or host feedback changes.

The page sends a state frame immediately on every button edge, throttles stick-only movement to one frame per 8 ms, and repeats the current state every 100 ms as a heartbeat. It pings once a second to measure the round trip and reconnects if no status arrives for 3 s.

## Input queue, tap latch and lease

Incoming states go into a 16-entry queue (`firmware/core/app.c`). The link takes one entry per outgoing report, so a press and release that arrive in the same TCP segment are still reported in order, each for at least one report. Consecutive states with identical buttons overwrite each other, which keeps stick movement from building a backlog. The page additionally holds every press for at least 40 ms.

If no state arrives for 500 ms, or the controlling WebSocket closes, the pad returns to neutral. Several pages may connect (up to four WebSockets); the last one to send is in control.

## HTTP API

| Request | Result |
| --- | --- |
| `GET /` | The page |
| `GET /api/info` | Firmware version, board, active mode, link state, Wi-Fi name, and every controller type with `available` for this board |
| `GET /api/status` | Diagnostics: current state, report counter, host feedback, recent USB/BLE events |
| `POST /api/config?mode=ID` | Select a controller type |
| `POST /api/config?ssid=NAME&pass=PASSWORD` | Change the access point (name 1–32, password 8–63 printable ASCII, no `"` or `\`) |
| `POST /api/config?reset=1` | Factory defaults |
| `GET /state?m=MASK&lx=&ly=&rx=&ry=&lt=&rt=` | Polling input for scripts and browsers without WebSocket. Only `m` is required. Replies `{"usb":bool,"l":strong,"r":weak,"q":sequence}` |

`/api/config` answers `{"ok":true,"reboot":true}` and restarts the board about 0.7 s later when something changed; errors are `{"ok":false,"error":"…"}` with status 400. Parameters are URL-encoded in the query string; a request body is ignored.

Settings are stored in the last flash sector on Pico boards and in NVS on ESP32. Holding BOOTSEL/BOOT for five seconds while running restores defaults.

## Controller personalities

Each file in `firmware/core/modes/` provides descriptors and a `build()` that turns the canonical state into that controller's input report. Offsets below exclude the HID report ID.

| ID | USB ID | Interface | Input report |
| --- | --- | --- | --- |
| `xinput` | 045E:028E | Vendor FF/5D/01, interrupt IN/OUT | 20 bytes: `00 14`, buttons (2), LT, RT, LX, LY, RX, RY (i16, up positive) |
| `xid` | 045E:0289 | Vendor 58/42, interrupt IN/OUT | 20 bytes: `00 14`, digital buttons, analog A B X Y Black White LT RT, sticks (i16) |
| `switch` | 0F0D:0092 | HID | 8 bytes: buttons (u16), hat, LX LY RX RY, vendor |
| `ds3` | 054C:0268 | HID, report 1 | 48 bytes: buttons 1–3, sticks 5–8, pressures 13–24, status 28–30, motion 40–47 (big endian) |
| `ds4` | 054C:05C4 | HID, report 1 | 63 bytes: sticks 0–3, hat+face 4, shoulders 5, PS/touchpad/counter 6, L2 R2 7–8, timestamp 9–10, gyro 12–17, accel 18–23, status 29, touch 34/38 |
| `dualsense` | 054C:0CE6 | HID, report 1 | 63 bytes: sticks 0–3, L2 R2 4–5, sequence 6, hat+face 7, shoulders 8, PS/touchpad 9, gyro 15–20, accel 21–26, timestamp 27–30, touch 32/36, status 52 |
| `hid` | 1209:0001 | HID | 9 bytes: buttons (u16), hat, LX LY RX RY, brake, accelerator |
| `ble` | — | HID over GATT, report 1 | Same 9 bytes as `hid` |

Host → controller traffic that is understood:

- `xinput`: OUT `00 08 00 strong weak` rumble, `01 03 pattern` LED.
- `xid`: vendor requests for the XID descriptor (`C1 06 4200`) and capabilities (`C1 01 0100/0200`), `GET_REPORT`, rumble by `SET_REPORT` or OUT endpoint.
- `ds3`: feature reports 01, EF, F2, F5, F7, F8 and F4 (enable); output report 1 for motors and LEDs.
- `ds4`: feature reports 02 (calibration), 12 (addresses), 81, A3 (firmware); output report 05 for motors and light bar. The console authentication reports F0–F2 are not implemented.
- `dualsense`: feature reports 05 (calibration), 09 (pairing), 20 (firmware); output report 02 for motors, player LEDs and light bar.

The generic layout orders button usages the way Linux and Android assign them to an unknown gamepad (A, B, –, X, Y, –, L1, R1, L2, R2, Select, Start, Home, L3, R3, Misc), with triggers on Simulation Brake/Accelerator. `1209:0001` is the pid.codes test identifier; replace it before distributing hardware.

The DualShock 4 and DualSense calibration reports advertise 16 LSB per °/s and 8192 LSB per g, with zero bias.

## USB layer

`firmware/usb/usb_link.c` answers TinyUSB's descriptor callbacks from the active personality. HID personalities use TinyUSB's HID class; `xinput` and `xid` use a small application class driver that opens one interrupt IN and one interrupt OUT endpoint and forwards control requests. Reports are sent no faster than each personality's `interval_ms`, and only when the previous transfer has completed. A pressed button requests remote wake-up while the host is suspended.

## Bluetooth LE

ESP32 targets with a Bluetooth radio expose Device Information (PnP ID), Battery and HID services through NimBLE. Pairing is "just works" with bonding; bonds are kept in NVS. Notifications are sent when the report changes, at most every 8 ms, and the firmware asks the host for a 7.5–15 ms connection interval. Wi-Fi and Bluetooth share one radio, so expect more latency than over USB.

## Layout and touch coordinates

The logical canvas is 360 units tall and 720–900 wide depending on the screen's aspect ratio; left and right clusters are anchored to their edges. In portrait the canvas rotates 90° clockwise and pointer coordinates are transformed back before any hit testing. Each stick owns one pointer, uses the touch-down point as its origin and clamps radially. The D-pad and the face cluster are single touch zones that derive buttons from the pointer position, which is what allows sliding. Pointer cancellation, lost capture, blur, a hidden page and resize all release input.
