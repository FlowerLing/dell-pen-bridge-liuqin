#!/system/bin/sh
fail() { echo "不启动：$*"; exit 1; }
[ "$(id -u)" = 0 ] || fail "需要 Root"
[ -c /dev/uinput ] || fail "系统不提供 uinput"
[ -w /sys/kernel/tracing/kprobe_events ] || fail "内核采样接口不可用"
[ -d /sys/kernel/tracing/instances ] || fail "独立采样接口不可用"
grep -q '^nt36532_touch ' /proc/modules || fail "触控驱动尚未加载"
/system/bin/pm path xiaomismartpen.plus 2>/dev/null | grep -q '^package:' || fail "请先安装焦点笔 Plus"
echo '运行条件检查通过；其他版本兼容性未知'
