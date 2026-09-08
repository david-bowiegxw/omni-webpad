# Changelog

## 0.3.0 — first public pre-release

- Fix overlapping controls and replace portrait reflow with rotation of a landscape controller layout.
- Move analog sticks inward/below the D-pad and face buttons; separate L3/R3.
- Use a touch-relative origin, adjustable travel, multi-touch ownership and release handling.
- Coalesce latest inputs, acknowledge short taps, remove the extra 24 ms delay, and reuse HTTP connections.
- Correct operational status; clear output feedback on USB unmount.
- Make motion/vibration opt-in and explain browser restrictions.
- Add English/Chinese docs and UI, portable CMake, CI and mobile interaction tests.
- Public Wi-Fi demonstration password is `PicoPad123`.

v0.3 requires on-console validation. Earlier private v0.1 recovery controls worked on CECHL00; v0.2 had user-reported UI defects. Prior private binaries are not part of this public source release.
