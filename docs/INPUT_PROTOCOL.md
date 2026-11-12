# Dell PN7522W 按键输入实测与开发接口

记录日期：2026-11-12。对象为戴尔 PN7522W 与小米平板 6 Pro（liuqin）。
这是本项目的实测记录，不是戴尔公布的通用协议。

## 先区分两条输入通道

- 两个侧键：通过屏幕的笔感应通道进入触控驱动，桥接程序读取状态，再生成虚拟按键。需要笔尖在屏幕感应范围内。
- 顶部蓝牙键：通过蓝牙 HID 键盘直接发送组合键，需要蓝牙连接，不依赖笔尖靠近屏幕。

每个侧键可以区分单击、双击、长按，但它们不是三种独立的硬件键码。顶部键的三种操作则有三个不同键码。

## 从笔到焦点笔 Plus：三层信号不是同一个东西

1. **笔与触控驱动之间**：本项目确认的是触控驱动收到的侧键状态字段（b77 的 0x02/0x01），没有把它当成经过完整解析的原始无线通信协议。原驱动能收到状态，却没有为侧键生成标准按键事件。
2. **桥接程序到 Android**：桥接通过 uinput 创建 `Xiaomi Smart Pen Keyboard`，生成 Linux `KEY_PAGEDOWN=109` / `KEY_PAGEUP=104` 的按下和松开。Android 将其映射为 `KEYCODE_PAGE_DOWN=93` / `KEYCODE_PAGE_UP=92`。
3. **焦点笔 Plus 收到的输入**：Plus 在系统框架里收到 Android KeyEvent。已审阅版本的 `isPenButton` 接受设备名 `Xiaomi Smart Pen Keyboard` 与 Android 93/92 的组合，再区分单击、双击、长按执行配置动作。

**对应小米官方笔的是第3层的按键接口**：本项目刻意生成 Plus 原本支持的设备名和按键。它并不意味着戴尔笔的底层无线协议、笔迹数据或全部官方笔功能已经变成小米官方笔。
没有同一台平板上的官方笔原始采集对照，不能据此宣称两支笔在全部底层字段上完全一致。

顶部键另走蓝牙键盘通道：Dell PN7522W Keyboard → Meta+F20/F19/F18 → 本模块动作执行器，当前没有转换为 Plus 的笔键1/2。

接口依据为已审阅的 [XiaomiSmartPenPlus PenKeyController.java](https://github.com/BaerYiTo/XiaomiSmartPenPlus/blob/1639ac230456de9151e1faa9205f2ad3cd0846dc/app/src/main/java/xiaomismartpen/plus/PenKeyController.java)，不是对未来版本或全部官方笔型号的保证。

## 两个侧键：原始状态与桥接输出

当前驱动的笔数据 `b77` 低两位作为按键状态：

| 实体侧键 | 状态位 | 虚拟 Linux 键码（十进制） | Android 键码（十进制） | 焦点笔 Plus |
| --- | --- | --- | --- | --- |
| 上侧键，离笔尖较远 | `0x02` | `KEY_PAGEDOWN` = 109 | `KEYCODE_PAGE_DOWN` = 93 | 笔键 1 |
| 下侧键，靠近笔尖 | `0x01` | `KEY_PAGEUP` = 104 | `KEYCODE_PAGE_UP` = 92 | 笔键 2 |

`0x00` 表示没有侧键按下。实现保留两位状态，但两个键同时按下的 `0x03` 组合未单独完成实机验证。
虚拟输入设备名称为 **`Xiaomi Smart Pen Keyboard`**；这些 PAGE 键是本项目生成的，不是戴尔笔原生发送的键码。

| 操作 | 上侧键的模块输出 | 下侧键的模块输出 |
| --- | --- | --- |
| 单击 | `PAGEDOWN DOWN → UP` 一组 | `PAGEUP DOWN → UP` 一组 |
| 双击 | 两组连续 `PAGEDOWN DOWN → UP` | 两组连续 `PAGEUP DOWN → UP` |
| 长按 | `PAGEDOWN DOWN`，保持后 `UP` | `PAGEUP DOWN`，保持后 `UP` |

这轮实测：上侧键单击约 0.18 秒、长按约 1.20 秒；下侧键单击约 0.28 秒、长按约 1.50 秒。
这些只是当次人工操作时长，**不是手势判定阈值**。单击、双击、长按的最终识别由焦点笔 Plus 完成。
本轮侧键监听的是桥接后的事件；原始状态位来自此前驱动分析及采集，不能把这两层证据混为一谈。

桥接会在离开感应范围、异常/过期记录或 500 毫秒无新有效数据时释放按键，因此不应把离屏造成的释放误认为真实松手。

### 侧键驱动适配位置

原始验证环境：

- ROM：`OS4.0.5.27.XMYCNXM`
- 内核：`5.10.237-Fuutao-Qn_miao-source-gc28e50058`
- `nt36532_touch.ko` SHA256：`6260743c41349c432dd388d0367bbb430aae43be7a8a3dabd8707507d37bd24a`
- 探测符号：`nt36532_touch:nvt_ts_work_func$5ae3b2be10c8de59fcf1b76a64203422+0x7fc`

| 采样字段 | 当前探测点的栈偏移 | 本实现用途 |
| --- | --- | --- |
| b66 | `+210($stack):x8` | `1` 为有效笔帧，`255` 按离笔处理 |
| b71、b72 | `+215`、`+216`，均为 x8 | 压力相关字段，用于有效感应状态判断 |
| b75、b76 | `+219`、`+220`，均为 x8 | 距离相关字段，用于有效感应状态判断 |
| b77 | `+221($stack):x8` | `b77 & 0x03` 提取侧键状态 |

本实现将 b71/b72/b75/b76 全零的有效笔帧视为无有效感应并释放侧键。
这些符号、偏移、字段解释是这份驱动的适配细节，不能直接套用到其他内核或驱动。
v0.1.3 起移除了版本硬性检查，但固定探测位置并未因此变成通用协议，错误适配可能造成系统异常。

## 顶部蓝牙键：三种独立组合键

设备名：**`Dell PN7522W Keyboard`**。
已观测身份：总线 `BUS_BLUETOOTH`（`0x0005`），Vendor `0x413c`，Product `0x8207`。
还会出现一个名为 `Dell PN7522W` 的输入节点；本次按键事件来自 Keyboard 节点。

| 操作 | Linux 输入组合 | 功能键十进制编码 | 功能键 MSC_SCAN |
| --- | --- | --- | --- |
| 单击 | `KEY_LEFTMETA + KEY_F20` | 190 | `0x0007006f` |
| 双击 | `KEY_LEFTMETA + KEY_F19` | 189 | `0x0007006e` |
| 长按约一秒 | `KEY_LEFTMETA + KEY_F18` | 188 | `0x0007006d` |

共同修饰键 `KEY_LEFTMETA` 的 Linux 编码为 125，MSC_SCAN 为 `0x000700e3`。
这里列的是 **Linux evdev 编码**，不要直接当作 Android `KeyEvent` 数值传给 `input keyevent`。

以单击为例，实测事件顺序如下（省略时间、事件节点编号）：

```text
EV_MSC MSC_SCAN      000700e3
EV_KEY KEY_LEFTMETA  DOWN
EV_MSC MSC_SCAN      0007006f
EV_KEY KEY_F20       DOWN
EV_SYN SYN_REPORT   00000000
EV_MSC MSC_SCAN      000700e3
EV_KEY KEY_LEFTMETA  UP
EV_MSC MSC_SCAN      0007006f
EV_KEY KEY_F20       UP
EV_SYN SYN_REPORT   00000000
```

双击与长按分别替换为 F19/`0007006e`、F18/`0007006d`。
本轮三种操作均表现为很短的按下/松开组合，按下到松开的间隔均不到 1 毫秒；**F18 的 DOWN 到 UP 时长并不是手指实际长按时长**。
笔已经完成了手势分类，开发者应按 F20/F19/F18 分发动作，不要再用这段脉冲长度判断长按。

本项目在 Meta 按下时接受功能键 value=1，忽略 value=2 自动重复和重复 DOWN；在松开、丢帧及设备重连后重置状态。
过期事件不执行，避免唤醒后补发旧动作。

### 长按过久与重连

一次约三秒的人工长按测试中，先收到 Meta+F18，随后两个戴尔蓝牙节点被移除，再重新出现。
指示灯同时闪烁；重连后单击再次收到 Meta+F20。
这证明发生过断开重连，**不证明配对记录被清除，也没有精确测出触发阈值**。
动作使用建议是按住约一秒后松开，软件映射无法阻止笔固件自身的配对行为。

## 开发者采集与接入建议

1. `adb devices` 确认连接。多设备时始终使用 `adb -s <设备编号>`。
2. 用 `adb shell getevent -pl` 查设备名；权限不足时在已 Root 的测试设备使用 `adb shell su -c "getevent -pl"`。
3. 找到对应节点后，用 `adb shell getevent -lt /dev/input/eventN` 监听。`eventN` 不是固定编号，重连或重启后必须重新查找。
4. 笔尖保持感应范围内测侧键；连接蓝牙、笔尖远离屏幕测顶部键。每次明确标注单击/双击/长按，间隔数秒。
5. 顶部监听开启时，本模块会对该蓝牙键盘使用 `EVIOCGRAB`，其他 getevent 监听者可能收不到事件。独立采集前先在 WebUI 关闭顶部功能。
6. 采集侧键的虚拟输出时应保持侧键桥接运行。不要与桥接程序争读同一个 `trace_pipe`，读取会消费事件并影响桥接。
7. 自定义动作程序应处理设备移除、重连、丢帧、重复事件与退出清理，不应硬编码某个 event 编号。

源码入口：

- [侧键采样设置](../package/bin/control.sh)
- [侧键状态解析与虚拟输入](../src/dell_pen_bridge.c)
- [顶部蓝牙键设备发现、解码与动作调度](../src/dell_top_button.c)
- [动作配置与执行](../package/bin/top-config.sh)
- [顶部解码测试](../tests/top_decode.c)

本文只保留必要事件样例和通用设备标识，未公开个人设备序列号、蓝牙地址、完整笔迹或原始系统日志。
