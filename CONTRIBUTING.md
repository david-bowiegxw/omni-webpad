# Contributing / 参与贡献

Issues and pull requests in English or Chinese are welcome. 欢迎用中文提交问题和 PR。

## Hardware reports are the most valuable contribution

Most controller types and boards have only been verified by tests and cross-compilation. If you try one, open an issue with:

- board and firmware version, controller type selected;
- host (console model and system version, or OS and game/launcher);
- what happened, and the output of ⚙ → **Diagnostics**;
- phone and browser if the problem is on the touch side.

"Works" reports count too. Please leave out console flash dumps, account or device identifiers and private Wi-Fi credentials.

## Code

- `npm test` must pass: it builds the simulator and runs the core tests and the browser tests. See *Develop* in the README.
- A new controller type is one file in `firmware/core/modes/` plus an entry in `firmware/core/mode.c`, a skin entry in `web/index.html` if it needs different labels, and assertions for its report layout in `tests/core_test.c`. Keep personalities free of SDK includes so they run in the simulator.
- When changing touch handling, keep the tests for rotated coordinates, simultaneous sticks, release on cancel/blur and control bounds.
- Do not claim console compatibility from tests alone. Say what was run on hardware and what was not.

修改触摸布局时请保留横竖屏、多指、取消触摸与断线回中的测试；新增手柄类型请同时补充报文布局的断言，并说明是否经过实机验证。
