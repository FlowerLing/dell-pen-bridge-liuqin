#!/system/bin/sh
[ "$KSU" = true ] || abort '此包仅针对 KernelSU 验证。'
[ "$ARCH" = arm64 ] || abort '需要 ARM64 设备。'
ui_print '检查当前设备和触控驱动…'
/data/adb/ksu/bin/busybox sh "$MODPATH/bin/check.sh" || abort '设备不匹配，未启用桥接。'
[ -f "$MODPATH/bin/dell-pen-bridge" ] || abort '包内程序缺失。'
ACTUAL=$(sha256sum "$MODPATH/bin/dell-pen-bridge") || abort '无法计算包内程序校验值。'
EXPECTED=$(tr -d '\r\n' < "$MODPATH/bin/dell-pen-bridge.sha256") || abort '无法读取校验文件。'
case "$EXPECTED" in ''|*[!0-9a-f]*) abort '校验文件格式错误。' ;; esac
[ "${#EXPECTED}" -eq 64 ] || abort '校验文件长度错误。'
[ "${ACTUAL%% *}" = "$EXPECTED" ] || abort '包内程序校验失败。'
set_perm_recursive "$MODPATH" 0 0 0755 0644 || abort '模块权限设置失败。'
set_perm "$MODPATH/bin/dell-pen-bridge" 0 0 0755 || abort '程序权限设置失败。'
for script in "$MODPATH"/*.sh "$MODPATH"/bin/*.sh; do
  set_perm "$script" 0 0 0755 || abort '脚本权限设置失败。'
done
OLD=/data/adb/modules/dell_pen_bridge_liuqin
if [ -f "$OLD/bin/control.sh" ]; then
  ui_print '停止旧版桥接…'
  /data/adb/ksu/bin/busybox sh "$OLD/bin/control.sh" stop || abort '旧版停止失败，请先重启后再更新。'
  [ ! -f "$OLD/paused" ] || touch "$MODPATH/paused"
fi
ui_print '安装完成后重启。无需改动 boot 或触控驱动。'
ui_print '按键动作在焦点笔 Plus 中配置；本模块动作按钮用于暂停/恢复。'
