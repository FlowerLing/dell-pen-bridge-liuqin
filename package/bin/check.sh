#!/system/bin/sh
# Exact-build guard. Run at installation AND before each activation.
fail() { echo "不启动：$*"; exit 1; }
[ "$(id -u)" = 0 ] || fail "需要 Root"
[ "$(/system/bin/getprop ro.product.device)" = liuqin ] || fail "仅支持小米平板 6 Pro（liuqin）"
[ "$(/system/bin/getprop ro.build.version.incremental)" = 'OS4.0.5.27.XMYCNXM' ] || fail "系统版本已变更，需要重新验证"
[ "$(uname -r)" = '5.10.237-Fuutao-Qn_miao-source-gc28e50058' ] || fail "内核版本与验证版本不同"
EXPECTED='6260743c41349c432dd388d0367bbb430aae43be7a8a3dabd8707507d37bd24a'
ACTUAL=$(sha256sum /vendor/lib/modules/nt36532_touch.ko 2>/dev/null)
[ "${ACTUAL%% *}" = "$EXPECTED" ] || fail "触控驱动已变更，需要重新适配"
[ -c /dev/uinput ] || fail "系统不提供 uinput"
[ -w /sys/kernel/tracing/kprobe_events ] || fail "内核采样接口不可用"
[ -d /sys/kernel/tracing/instances ] || fail "独立采样接口不可用"
grep -q '^nt36532_touch ' /proc/modules || fail "触控驱动尚未加载"
/system/bin/pm path xiaomismartpen.plus 2>/dev/null | grep -q '^package:' || fail "请先安装焦点笔 Plus"
echo '设备与触控驱动校验通过'
