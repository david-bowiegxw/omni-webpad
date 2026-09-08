# PS3 Pico W Webpad：用手机触屏模拟 PS3 手柄

[English](README.md) · [下载固件](https://github.com/david-bowiegxw/ps3-picow-webpad/releases) · [反馈问题](https://github.com/david-bowiegxw/ps3-picow-webpad/issues)

**一块 Raspberry Pi Pico W、一根 USB 数据线，加上手机浏览器，就能向 PS3 发送 DualShock 3 手柄输入。** 手机连接开发板自建的 Wi-Fi，Pico W 通过 USB 接入 PlayStation 3。基本按键不需要安装手机 App、焊接、登录 PSN 或连接互联网。

![PS3 手机虚拟手柄横屏界面：方向键、双摇杆、动作键和肩键](docs/controller-landscape.png)

截图来自模拟开发板响应的浏览器预览，图中 RTT 不是实机性能基准。

```text
手机触屏 → 本地 Wi-Fi → Pico W → USB 数据线 → PS3
```

这是实验性的触屏手柄及恢复菜单辅助工具，通过 USB 模拟 DS3；不是蓝牙直连 PS3，也不把游戏画面串流到手机。

## 功能与 v0.3 修复

- 始终按横屏手柄布局显示。浏览器锁定竖屏时旋转整个界面，并同步转换触摸坐标。
- 左上方向键、右上 △○×□、中下方对称双摇杆、顶部肩键、中央 SELECT / PS / START。
- 两个摇杆可独立多指操作，以按下位置为起点，松手回中；L3/R3 独立摆放，不挡住摇杆。
- 摇杆行程可调。去掉 v0.2 固定圆心造成的起始跳动与额外 24 毫秒等待。
- 短按保留到请求确认；连续移动只发送最新状态，避免堆积请求；开发板复用 HTTP 连接。
- 完整 17 个数字按钮，包括 PS 和 Start + Select 组合键。
- 抑制长按复制、文字选中和拖动菜单；失焦、取消触摸或断线时松键。开发板超过 400 毫秒未收到指令会回到中立状态。
- 中英文界面、可选全屏、实验性六轴字段映射与简化振动反馈。

## 验证范围

| 内容 | 当前状态 |
| --- | --- |
| v0.1 USB 识别、恢复模式按键 | 已由用户在 CECHL00 实机验证，可完成恢复流程 |
| v0.2 手机界面 | 用户反馈重叠、横屏与摇杆手感问题，已被新版替代 |
| v0.3 布局、触摸 | 已做多种手机尺寸浏览器自动化测试、数据包测试与固件交叉编译 |
| v0.3 游戏内操作与真实延迟 | 待 PS3 实机验证；界面显示的请求往返时间不等于游戏端到端延迟 |
| 六轴 | 实验映射；轴方向、校准与游戏兼容性尚未实机验证 |
| 振动 | 浏览器支持时，根据马达状态变化产生短振动；不是完整双马达反馈 |
| 开发板 | 仅提供原版 **Pico W（RP2040）** 固件，不提供 Pico 2 W、无无线版 Pico 或 ESP32 固件 |

## 下载与刷写

1. 到 [v0.3.0 Release](https://github.com/david-bowiegxw/ps3-picow-webpad/releases/tag/v0.3.0) 下载 `PS3-PicoW-Webpad-v0.3.0.uf2`。当前为待实机验证的预发布版。
2. 按住 **BOOTSEL**，把 Pico W 用数据线接入电脑，再松开按钮。
3. 将 UF2 拖到 `RPI-RP2` 磁盘。磁盘消失表示完成刷写并重启。
4. 把 Pico W 接到 PS3 的 USB 口，等待热点出现，通常需预留约 5–10 秒。
5. 手机连接 **`PS3-PicoW`**，密码 **`PicoPad123`**。出现“无互联网”提示时保持连接。
6. 用完整浏览器打开 **`http://192.168.4.1`**，直接显示横屏手柄。按 **PS** 注册手柄。
7. 横着握住手机，在摇杆区域按住并拖动，松手回中。设置中的“摇杆行程”越小，相同距离推动得越满。

**公开版使用新的演示 Wi-Fi 密码。** 从旧私用版本升级时，可能需要忘记手机中已保存的热点再重新连接。可修改 `src/main.c` 中的 `WIFI_PASSWORD` 并自行编译；加入该热点的人都能发送控制指令，请同时只开一个控制页面。

本项目只提供手柄固件，不包含 PS3 系统固件或破解流程。恢复模式兼容性不代表所有机型都已验证。

## 横屏、六轴与振动说明

- **横屏布局**不依赖浏览器方向锁定 API。竖屏时界面顺时针旋转，请顺着界面横握手机。
- **全屏**可点击左下按钮；iPhone 可尝试 Safari 的“共享 → 添加到主屏幕”。系统边缘手势可能仍然生效。
- **六轴**通常要求 HTTPS 安全环境，部分手机还要求明确授权。Pico W 当前提供 HTTP，因此多数现代手机浏览器在这里不能读取动作传感器。按钮和摇杆不受影响。已保留实验接口，但不宣称原生 Sixaxis 等效。
- **振动**默认关闭，可在设置中开启。iPhone Safari 不提供通用网页 Vibration API；支持的 Android 浏览器可在 PS3 马达状态变化时振动 80 毫秒。不能还原持续振动和双马达强度。
- **压感**：按钮及 L2/R2 只有 0/255 两级，不具备真实模拟压感。

参考：[MDN 动作传感器权限](https://developer.mozilla.org/en-US/docs/Web/API/DeviceMotionEvent/requestPermission_static)、[Vibration API](https://developer.mozilla.org/en-US/docs/Web/API/Vibration_API)。

## 从源码编译

需要 Git、CMake、Python 3、`arm-none-eabi` GCC 工具链和 **Pico SDK 2.2.0**。本地发布构建使用 macOS ARM64 / GCC 14.2.1。固件编译本身不需要 Node.js。

```sh
git clone https://github.com/david-bowiegxw/ps3-picow-webpad.git
cd ps3-picow-webpad
git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git pico-sdk
git -C pico-sdk submodule update --init --depth 1 lib/tinyusb lib/lwip lib/cyw43-driver
cmake -S . -B build -DPICO_SDK_PATH="$PWD/pico-sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

产物为 `build/ps3-picow.uf2`。若工具链不在 PATH，在配置命令加上 `-DPICO_TOOLCHAIN_PATH=/你的工具链目录`。BTstack 未初始化的警告可以预期：此项目使用 Wi-Fi 和 USB，不使用蓝牙。只下载所需 SDK 子模块以节省磁盘空间。

CMake 自动压缩网页并生成固件中的页面数据，不需要手动执行嵌入脚本。UF2 由 Python 脚本生成，不下载 picotool。SDK、工具链及构建产物不会提交到 Git。

## 测试和预览

```sh
cc -std=c11 -Wall -Wextra -I src tools/test_packet.c src/packet.c -o /tmp/webpad-packet-test
/tmp/webpad-packet-test
node tools/check.cjs
npm install
npx playwright install chromium
npm test
python3 tools/preview.py
```

预览打开 `http://127.0.0.1:8787`，它模拟开发板响应，不控制 PS3。设置 `CHROME_PATH` 为现有浏览器可执行文件路径，可以免下载 Chromium。自动化测试覆盖六种横竖屏尺寸、元素边界、按钮重叠、双摇杆多指、旋转后方向、短按、松手回中及请求不并发堆积。

实现细节见 [协议文档](docs/PROTOCOL.md)。反馈问题时请提供手机型号、浏览器版本、屏幕方向、开发板、固件版本、PS3 型号与界面请求 RTT。

## 常见问题

**没有热点 / PS 键无效：** 检查数据线和开发板型号，等待热点启动，访问 `/status` 查看 USB 状态。USB 枚举不等于完成全部主机握手。

**摇杆依然慢：** 调小摇杆行程、靠近 Pico W、关闭其他控制页面并观察 RTT。本地摇杆动画立即响应，但游戏输入仍受 Wi-Fi 与主机处理影响，尚无硬件延迟基准。

**想接实体手柄：** 可查看 [DualSense-to-DualShock3-Sixaxis](https://github.com/ivveshermosilla/dualsense-to-dualshock3-sixaxis)、[RosettaPad](https://github.com/ihasTaco/RosettaPad)、[Bluepad32](https://github.com/ricardoquesada/bluepad32)。它们解决的是相关转接场景，并非这套手机网页控制器的直接替代版本。

## 许可证与致谢

原创代码采用 [MIT](LICENSE)。DS3 协议表保留 Ivves Hermosilla 的 MIT 声明，DHCP 服务保留 Damien P. George / MicroPython 声明。Pico SDK、TinyUSB、lwIP、CYW43 使用各自的许可证。

**完整 UF2 并非全部 MIT**：CYW43 的 Raspberry Pi 授权限制相关组件用于 Raspberry Pi 芯片。详情见 [第三方声明](THIRD_PARTY_NOTICES.md) 及 Release 附带的许可证文件。

独立项目，与 Sony、Raspberry Pi 无隶属关系；品牌名称仅用于说明兼容目标。
