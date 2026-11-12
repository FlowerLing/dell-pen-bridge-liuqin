# Dell Pen Bridge for Xiaomi Pad 6 Pro

## 顶部蓝牙键 WebUI（0.2.0）

已完成正式安装、重启自启及 WebUI 保存与三种实体操作测试，见 [验证记录](VALIDATION.md)。

在 KernelSU 模块页面打开本模块的 WebUI，可分别设置顶部键单击、双击、长按。
默认关闭顶部功能，三个动作均为“不执行”；启用并保存后，下次按键生效，不必保持页面打开。
提供返回、主页、最近任务、音量、媒体控制、截图（取决于系统支持）、电源键短按/长按和自定义 Shell 命令。
电源短按模拟系统电源键（亮屏/息屏），长按跟随系统设置，可能打开电源菜单或语音助手；不执行直接关机/重启命令。
自定义命令以 Root 权限运行，最多 4096 个 UTF-8 字节，支持多行；保存不会执行命令。
每个动作最长约八秒，执行期间的新动作忽略；停止监听时结束当前动作进程组。
请不要用自定义命令启动常驻程序，主动脱离进程组的后台程序不在清理保证范围内。

顶部键需要蓝牙连接，不要求笔尖靠近屏幕。单击发送 Meta+F20、双击 Meta+F19、长按约一秒 Meta+F18。
长按约三秒的实测曾触发断连重连，不能据此认定配对记录被清空；使用长按动作时约一秒就松开。
监听器根据蓝牙设备名称和标识识别 Dell PN7522W，自动处理断开重连；此功能不是其他品牌笔的通用映射器。
开启顶部功能时会独占该笔的蓝牙键盘事件，避免原快捷键同时触发；关闭或停止后释放。
侧键仍由原版焦点笔 Plus 处理。模块动作按钮暂停/恢复两部分，顶部配置开关只控制顶部。

配置保存在 `/data/adb/dell_pen_bridge_liuqin/top.conf`，目录仅 Root 可访问，升级保留。
卸载保留配置以便重装；需要彻底清理时可自行删除该专用目录。状态与短日志位于 `/dev/dell_pen_top_liuqin`。
WebUI 完全离线，不加载外部网页或脚本。只有从支持 WebUI 的 KernelSU 管理器打开才能保存。


让**戴尔 PN7522W 的两个侧键**在已验证的小米平板 6 Pro 系统上，通过
原版 [焦点笔 Plus](https://github.com/BaerYiTo/XiaomiSmartPenPlus)
自定义动作。安装后由平板独立运行，日常使用无需电脑或 USB。

只在原项目列出的环境实测，其他版本能否使用未知。
v0.1.3 已移除型号、系统版本、内核版本和驱动哈希检查，保留 Root、ARM64、KernelSU、运行接口、驱动已加载及焦点笔 Plus 要求。
探测符号、偏移和数据布局仍按原驱动编写；取消检查不代表自动适配，其他环境可能无效、误触发、卡死或重启。
本版未进行新的实机测试。

| 项目 | 已验证版本 |
| --- | --- |
| 平板 | 小米平板 6 Pro，代号 `liuqin` |
| 触控笔 | Dell PN7522W |
| 系统 | HyperOS 移植版 `OS4.0.5.27.XMYCNXM` |
| 内核 | `5.10.237-Fuutao-Qn_miao-source-gc28e50058` |
| 触控驱动 | `nt36532_touch.ko`，SHA256 见下方 |
| Root 环境 | KernelSU；实测版本 32680 |
| 动作处理 | 焦点笔 Plus 1.2.0，LSPosed 2.2.1 (7912)，Zygisk Next 1.5.2 |

驱动 SHA256：

```text
6260743c41349c432dd388d0367bbb430aae43be7a8a3dabd8707507d37bd24a
```

## 安装与使用

1. 先安装并配置 KernelSU、Zygisk Next、LSPosed 和原版“焦点笔 Plus”。
2. 在 LSPosed 启用“焦点笔 Plus”，作用域勾选“系统框架”。
3. 从本仓库 **Releases** 下载 `Dell-PN7522W-liuqin-KernelSU-v0.2.0.zip`。
   GitHub 自动生成的 **Source code (zip)** 是源码，不能作为模块安装。
4. 在 KernelSU 管理器的模块页面选择模块 ZIP，安装完成后**重启平板**。
5. 在“焦点笔 Plus”里配置动作：
   - 上侧键（离笔尖较远）对应“笔键 1”。
   - 下侧键（靠近笔尖）对应“笔键 2”。
6. 让笔尖保持在屏幕感应范围内，分别测试两个侧键。

单击、长按、双击和按应用动作沿用“焦点笔 Plus”的设置。某个手势
设为“不执行”时不会产生动作；快速连续点按可能被识别为双击。

点击 KernelSU 中本模块的**动作**按钮可暂停或恢复，暂停状态会保留
到下次开机。禁用模块会停止桥接；重新启用后重启。卸载后重启即可。
连接小米官方笔或运行旧的电脑测试桥接程序前，请先暂停本模块，避免
同名输入设备冲突。

## 实测状态

v0.1.3 未进行新的实机测试，下面为旧版历史验证记录。

0.1.1 已完成正式 KernelSU 安装、重启自动启动、两个实体侧键测试；
独立运行测试中，断开 USB 后两个键也正常。发布版 0.1.2 只更新打包、
文档、许可和版本信息，**复用相同原生程序与全部 Android 脚本**；新的
0.1.2 ZIP 未单独安装测试。详见 [验证记录](package/VALIDATION.md)。

长期待机耗电尚未完整测量。没有验证笔尖远离屏幕时的侧键；不增加
原版“焦点笔 Plus”没有的橡皮擦等动作。

## 工作原理

这一版 Novatek 驱动已经接收到戴尔侧键状态，但没有将它们作为标准
Android 按键上报。模块在内核正式处理笔数据的路径设置私有 kprobe，
读取按钮状态，再通过 uinput 创建名为 `Xiaomi Smart Pen Keyboard`
的虚拟设备，向原版“焦点笔 Plus”发送按下与松开。

| 实体侧键 | 状态位 | Linux 按键 | Android 按键 | Plus 配置 |
| --- | --- | --- | --- | --- |
| 上侧键 | `0x02` | `KEY_PAGEDOWN` (109) | `PAGE_DOWN` (93) | 笔键 1 |
| 下侧键 | `0x01` | `KEY_PAGEUP` (104) | `PAGE_UP` (92) | 笔键 2 |

真实按压时长会保留。离开感应范围、超过 500 毫秒没有有效数据、遇到
过期/异常记录或服务停止时释放按键。模块不替换笔迹坐标、压力、倾角
处理，也不刷写 boot、vendor 驱动文件或改动 Plus 的 APK/配置。
运行时使用内核动态跟踪接口，并非完全不触及内核运行状态。

状态与简短日志保存在内存目录 `/dev/dell_pen_bridge_liuqin`，不会持续
写入原始笔迹或坐标。只有在运行条件检查通过后才启动私有跟踪实例；退出时仅
清理本模块的实例、探针和虚拟设备。

## 排查

- 刚安装或更新后没有效果：先重启，确认 KernelSU 中模块已启用。
- 侧键没有动作：确认笔尖在感应范围内、模块没有暂停、Plus 已启用
  且系统框架作用域正确，并检查对应手势是否配置了动作。
- 更换系统或内核后异常：其他版本兼容性未知，请禁用模块并重新分析驱动。

以 Root 执行以下命令可查看状态：

```sh
sh /data/adb/modules/dell_pen_bridge_liuqin/bin/control.sh status
cat /dev/dell_pen_bridge_liuqin/run.log
```

报告问题时请提供型号、系统版本、内核版本、模块版本与上述简短日志。
请去掉设备序列号等个人信息，不需要上传整套系统镜像或笔迹采集数据。

## 从源码构建

需要 Python 3.10+。重新编译还需要 Zig；已验证编译器为 **Zig 0.13.0**。
构建脚本只在电脑上生成文件，不连接或操作设备。

复用仓库中已验证的 ARM64 程序打包（不需要 Zig）：

```sh
python build_module.py --package-only
```

此模式不会编译源码；修改 C 源码后应使用下方重新编译流程。

重新编译并打包，使用 PATH 中的 `zig`：

```sh
python build_module.py
```

或指定编译器路径（Windows 示例）：

```powershell
python build_module.py --zig "C:\tools\zig\zig.exe"
```

输出位于 `dist/`，包括模块 ZIP、SHA256 文件与内容清单。打包会检查
二进制校验、文本 LF 换行、ZIP 完整性和执行权限。重新编译的输出受
编译器、路径及构建环境影响，不承诺与预编译文件逐字节相同。

仓库保留已验证程序的调试信息，其中包含通用本地构建目录路径。
未包含设备序列号、原始笔迹、个人日志或厂商驱动二进制。

## 许可与致谢

本项目使用 [MIT 许可](LICENSE)，静态运行库许可见
[第三方声明](THIRD_PARTY_NOTICES.md)。感谢
[BaerYiTo / XiaomiSmartPenPlus](https://github.com/BaerYiTo/XiaomiSmartPenPlus)
提供原版动作配置与执行能力；本项目使用其输入接口，没有修改或打包
其 APK。也感谢 KernelSU、LSPosed、Zygisk Next、Linux、Zig 与 musl。

本项目为独立社区适配，并非戴尔、小米或上述项目作者的官方组件。

## English summary

An independent KernelSU bridge for the Dell PN7522W barrel buttons on one
strictly verified Xiaomi Pad 6 Pro (`liuqin`) ROM/kernel/Novatek-driver build.
It reads the driver's existing button state through a private runtime kprobe
and sends uinput key events to the unmodified XiaomiSmartPenPlus app. The pen
must remain within the screen's sensing range. It runs on the tablet without
a computer, checks runtime prerequisites, and does not replace boot images or
vendor driver files. Version 0.1.2 is a documentation/licensing packaging
revision of the device-tested 0.1.1 runtime. Download the installable ZIP from
Releases, install it in KernelSU, reboot, and configure actions in
XiaomiSmartPenPlus. Version 0.1.3 removes device, ROM, kernel and driver-hash guards. Other builds are untested and may fail, misbehave or crash due to fixed probe offsets.
