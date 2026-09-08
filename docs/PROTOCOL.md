# Protocol and implementation notes

The board serves a gzip-compressed, self-contained HTML page at `http://192.168.4.1`. Wi-Fi AP credentials in `src/main.c` are demonstration defaults. There is no cloud service, controller ownership lock, or per-request authentication.

## Browser → board

`GET /state?m=0&lx=128&ly=128&rx=128&ry=128&ax=512&ay=512&az=512&gz=512`

Fields are ordered as shown. `m` is a 17-bit button mask. Stick values range from 0 to 255 (128 center). Motion fields range from 0 to 1023 (512 neutral). The v0.1 `GET /state?m=…` form is retained and centers all analog axes. `/status` returns diagnostic information without extending the input lease.

State responses contain `usb`, `operational` (F4 initialization observed), `l`, `r` (motor outputs), and `q` (motor state change counter). `usb` is USB enumeration, not evidence of gameplay compatibility. `operational` is a handshake diagnostic, not authentication.

| Bit | Button | Bit | Button |
| --- | --- | --- | --- |
| 0 | SELECT | 1 | L3 |
| 2 | R3 | 3 | START |
| 4 | Up | 5 | Right |
| 6 | Down | 7 | Left |
| 8 | L2 | 9 | R2 |
| 10 | L1 | 11 | R1 |
| 12 | Triangle | 13 | Circle |
| 14 | Cross | 15 | Square |
| 16 | PS | | |

The browser sends immediately on input changes if no request is pending, otherwise coalesces to the latest state after the active request. It sends 50 ms idle heartbeats. HTTP keep-alive avoids recreating connections after each sample. Network RTT is measured at the browser and includes its response parsing, not the PS3 input or rendering pipeline. Requests time out after 300 ms; the board resets all inputs after a 400 ms lease.

Short button taps are latched until an HTTP response acknowledges the sample. This helps preserve taps between updates but does not guarantee observation by every game frame. Only one controlling page is supported; multiple pages will overwrite each other's state.

## Board → PS3

`packet.c` generates a 49-byte DS3 input report including report ID 1. TinyUSB supplies that ID when sending the remaining 48 bytes. USB polling attempts occur at 8 ms intervals when ready; Wi-Fi and browser transport are separate from this interval.

- Buttons: bytes 2–4.
- Left/right sticks: bytes 6–9.
- Pressure fields: bytes 14–25, digital 0/255 only.
- Experimental motion: big-endian 16-bit values at bytes 41, 43, 45 and 47.
- Feature handling: 01, EF, F2, F5, F7, F8; F4 marks initialization.
- Output report 1: small motor at payload byte 2, large motor at byte 4, LEDs at byte 9 (excluding report ID).

Motion converts browser acceleration/rotation rates into those fields. Scaling, axis conventions and calibration remain experimental. The original DS3 is not a six-independent-channel IMU report; the page sends three acceleration fields and one gyro field. Native sensor access usually fails on local HTTP. Output durations are not reproduced; vibration is an optional 80 ms pulse on changed motor state, not full motor emulation.

## Layout and touch coordinates

The logical canvas is 800 × 360. It scales uniformly to fit the viewport with margins. In portrait, the canvas rotates clockwise by 90 degrees and the pointer transform is inverted before stick calculations. D-pad and face buttons have separate, non-overlapping rectangles. Each stick owns one pointer ID, uses the touch-down location as origin, clamps radially, and applies no artificial axis deadzone. Local rendering updates on pointer events; network requests use latest-state coalescing.

Disconnect, pointer cancellation, lost pointer capture, blur, hidden pages, and resize reset touch input. Fullscreen and motion permission are separate explicit actions so fullscreen cannot consume the activation needed for sensor permission.
