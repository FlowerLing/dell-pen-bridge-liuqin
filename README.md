# Dell Pen Bridge for Xiaomi Pad 6 Pro

让**戴尔 PN7522W 的两个侧键**在已验证的小米平板 6 Pro 系统上，通过
原版 [焦点笔 Plus](https://github.com/BaerYiTo/XiaomiSmartPenPlus)
自定义动作。安装后由平板独立运行，日常使用无需电脑或 USB。

**仅适配下表这一套系统、内核和触控驱动，不是通用触控笔兼容模块。**
安装器和每次启动都会检查这些条件；不匹配时拒绝运行。请勿修改校验值
强行安装，其他版本需要重新分析与验证。

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
3. 从本仓库 **Releases** 下载 `Dell-PN7522W-liuqin-KernelSU-v0.1.2.zip`。
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
写入原始笔迹或坐标。只有在校验通过后才启动私有跟踪实例；退出时仅
清理本模块的实例、探针和虚拟设备。

## 排查

- 刚安装或更新后没有效果：先重启，确认 KernelSU 中模块已启用。
- 侧键没有动作：确认笔尖在感应范围内、模块没有暂停、Plus 已启用
  且系统框架作用域正确，并检查对应手势是否配置了动作。
- 系统或内核更新后拒绝启动：这是版本保护，需要重新适配。

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
a computer, enforces exact-build guards, and does not replace boot images or
vendor driver files. Version 0.1.2 is a documentation/licensing packaging
revision of the device-tested 0.1.1 runtime. Download the installable ZIP from
Releases, install it in KernelSU, reboot, and configure actions in
XiaomiSmartPenPlus. Other devices, kernels and drivers are unsupported until
separately analyzed and tested.
