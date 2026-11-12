#!/system/bin/sh
MODDIR=$(CDPATH= cd "$(dirname "$0")/.." && pwd) || exit 1
BB=/data/adb/ksu/bin/busybox
RUNDIR=${DELL_TOP_RUNTIME:-/dev/dell_pen_top_liuqin}
DATA=${DELL_TOP_DATA:-/data/adb/dell_pen_bridge_liuqin}
NATIVE=$MODDIR/bin/dell-top-button
alive() {
  PID=$(cat "$RUNDIR/pid" 2>/dev/null)
  case "$PID" in ''|*[!0-9]*) return 1;; esac
  [ "$PID" -gt 1 ] || return 1
  actual=$(readlink "/proc/$PID/exe" 2>/dev/null)
  [ "$actual" = "$NATIVE" ]
}
case "${1:-status}" in
  start)
    for flag in disable remove paused; do [ ! -e "$MODDIR/$flag" ] || exit 0; done
    [ ! -L "$RUNDIR" ] || exit 1
    mkdir -p "$RUNDIR"; chmod 700 "$RUNDIR"
    exec 9> "$RUNDIR/control.lock"; "$BB" flock -x 9 || exit 1
    alive && exit 0
    [ ! -L "$DATA" ] || exit 1
    mkdir -p "$DATA" || exit 1
    chmod 700 "$DATA"
    rm -f "$RUNDIR/stop"
    nohup "$BB" setsid "$NATIVE" "$MODDIR" "$RUNDIR" "$DATA/top.conf" 9>&- > "$RUNDIR/run.log" 2>&1 < /dev/null &
    ;;
  stop)
    [ -d "$RUNDIR" ] || exit 0
    touch "$RUNDIR/stop"
    if alive; then kill -TERM "$PID"; fi
    attempts=0
    while alive && [ "$attempts" -lt 15 ]; do sleep 0.2; attempts=$((attempts+1)); done
    if alive; then kill -KILL "$PID"; fi
    ;;
  status) "$BB" sh "$MODDIR/bin/top-config.sh" status;;
  *) exit 2;;
esac
