#!/system/bin/sh
MODDIR=${0%/*}
/data/adb/ksu/bin/busybox sh "$MODDIR/bin/top-control.sh" start
exec /data/adb/ksu/bin/busybox sh "$MODDIR/bin/control.sh" boot
