#!/system/bin/sh
# All state is confined to this module and a private tmpfs directory.
MODDIR=$(CDPATH= cd "$(dirname "$0")/.." && pwd) || exit 1
RUNDIR=/dev/dell_pen_bridge_liuqin
TRACE=/sys/kernel/tracing
INSTANCE=$TRACE/instances/dell_pen_bridge_liuqin
GROUP=dell_pen_bridge_liuqin
BB=/data/adb/ksu/bin/busybox
NATIVE=$MODDIR/bin/dell-pen-bridge
MODE=${1:-status}

setup_runtime() {
  [ ! -L "$RUNDIR" ] || return 1
  if [ ! -d "$RUNDIR" ]; then mkdir -m 700 "$RUNDIR" || return 1; fi
  chown 0:0 "$RUNDIR" || return 1
  chmod 700 "$RUNDIR" || return 1
}

start_ticks() {
  # comm may contain spaces; remove it before counting the remaining fields.
  sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | awk '{print $20}'
}

owned_pid() {
  PID=
  [ -f "$1" ] || return 1
  read -r candidate ticks < "$1"
  case "$candidate:$ticks" in *[!0-9:]*|:|*:) return 1 ;; esac
  [ "$candidate" -gt 1 ] 2>/dev/null || return 1
  [ "$(start_ticks "$candidate")" = "$ticks" ] || return 1
  commandline=$(tr '\000' ' ' < "/proc/$candidate/cmdline" 2>/dev/null)
  case "$commandline" in *"$2"*) PID=$candidate; return 0 ;; esac
  return 1
}

native_alive() {
  if owned_pid "$RUNDIR/native.pid" "$NATIVE"; then return 0; fi
  # A signal can arrive before the PID file is committed. Only our actual
  # direct child with the expected executable may be used as a fallback.
  [ -n "${CHILD:-}" ] || return 1
  parent=$(sed 's/.*) //' "/proc/$CHILD/stat" 2>/dev/null | awk '{print $2}')
  [ "$parent" = "$$" ] || return 1
  commandline=$(tr '\000' ' ' < "/proc/$CHILD/cmdline" 2>/dev/null)
  case "$commandline" in *"$NATIVE"*) PID=$CHILD; return 0 ;; esac
  return 1
}
service_alive() { owned_pid "$RUNDIR/service.pid" "$MODDIR/bin/control.sh run"; }

stop_native() {
  if native_alive; then
    kill -TERM "$PID" 2>/dev/null
    tries=0
    while native_alive && [ "$tries" -lt 3 ]; do sleep 1; tries=$((tries+1)); done
    if native_alive; then kill -KILL "$PID" 2>/dev/null; fi
  fi
}

cleanup_trace() {
  # Never clear global events or other instances.
  if [ -d "$INSTANCE" ]; then
    echo 0 > "$INSTANCE/tracing_on" 2>/dev/null
    if [ -e "$INSTANCE/events/$GROUP/raw/enable" ]; then
      echo 0 > "$INSTANCE/events/$GROUP/raw/enable" 2>/dev/null
    fi
    rmdir "$INSTANCE" 2>/dev/null || return 1
  fi
  if grep -q "^p:$GROUP/raw " "$TRACE/kprobe_events"; then
    printf '%s\n' "-:$GROUP/raw" >> "$TRACE/kprobe_events" || return 1
  fi
  return 0
}

cleanup_run() {
  trap - EXIT HUP INT TERM
  stop_native
  # Never block cleanup waiting for an unverified live process.
  if [ -n "${CHILD:-}" ]; then
    child_state=$(sed 's/.*) //' "/proc/$CHILD/stat" 2>/dev/null | awk '{print $1}')
    case "$child_state" in ''|Z) wait "$CHILD" 2>/dev/null ;; esac
  fi
  if ! cleanup_trace; then echo '清理采样点失败，重启后会自动清除。'; fi
  rm -f "$RUNDIR/native.pid" "$RUNDIR/service.pid"
  echo '已停止，按键已释放。'
  # service.lock stays on disk. Unlinking a held lock permits a second owner.
}

run_service() {
  setup_runtime || exit 1
  exec 9> "$RUNDIR/service.lock" || exit 1
  "$BB" flock -n 9 || { echo '已有桥接进程运行'; exit 0; }
  # Optional bounded staging test; normal module runs until explicitly stopped.
  LIMIT=${2:-0}
  case "$LIMIT" in ''|*[!0-9]*) echo '无效的运行时限'; exit 1 ;; esac
  [ "$LIMIT" -le 3600 ] || exit 1
  for flag in disable remove paused; do [ ! -e "$MODDIR/$flag" ] || exit 0; done
  "$BB" sh "$MODDIR/bin/check.sh" || exit 1
  native_alive && { echo '已有独立桥接进程，拒绝重复启动'; exit 1; }
  cleanup_trace || { echo '旧采样点清理失败'; exit 1; }
  if grep -Fq 'N: Name="Xiaomi Smart Pen Keyboard"' /proc/bus/input/devices; then
    echo '已有同名笔设备，请先停止其他测试程序或断开官方笔。'; exit 1
  fi
  rm -f "$RUNDIR/stop"
  printf '%s %s\n' "$$" "$(start_ticks $$)" > "$RUNDIR/service.pid.tmp"
  mv "$RUNDIR/service.pid.tmp" "$RUNDIR/service.pid"
  CHILD=
  trap cleanup_run EXIT
  trap 'exit 0' HUP INT TERM
  mkdir "$INSTANCE" || exit 1
  echo 0 > "$INSTANCE/tracing_on" || exit 1
  echo nop > "$INSTANCE/current_tracer" || exit 1
  echo boot > "$INSTANCE/trace_clock" || exit 1
  echo 128 > "$INSTANCE/buffer_size_kb" || exit 1
  printf '%s\n' 'p:dell_pen_bridge_liuqin/raw nt36532_touch:nvt_ts_work_func$5ae3b2be10c8de59fcf1b76a64203422+0x7fc b66=+210($stack):x8 b71=+215($stack):x8 b72=+216($stack):x8 b75=+219($stack):x8 b76=+220($stack):x8 b77=+221($stack):x8' >> "$TRACE/kprobe_events" || exit 1
  echo 'b66 == 1 || b66 == 255' > "$INSTANCE/events/$GROUP/raw/filter" || exit 1
  echo 1 > "$INSTANCE/events/$GROUP/raw/enable" || exit 1
  "$NATIVE" --trace "$INSTANCE/trace_pipe" --stop-file "$RUNDIR/stop" \
    --status "$RUNDIR/status.json" --lock "$RUNDIR/native.lock" \
    --max-seconds "$LIMIT" --cleanup-instance "$INSTANCE" &
  CHILD=$!
  printf '%s %s\n' "$CHILD" "$(start_ticks "$CHILD")" > "$RUNDIR/native.pid.tmp"
  mv "$RUNDIR/native.pid.tmp" "$RUNDIR/native.pid"
  # Let EventHub discover the device; tracing starts with no queued pen presses.
  sleep 1
  native_alive || { echo '按键程序启动失败'; exit 1; }
  echo 1 > "$INSTANCE/tracing_on" || exit 1
  echo '运行中：上侧键为笔键 1，下侧键为笔键 2。'
  while native_alive; do
    if [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ] || \
       [ -e "$MODDIR/paused" ] || [ -e "$RUNDIR/stop" ]; then break; fi
    sleep 2
  done
  exit 0
}

start_service() {
  setup_runtime || return 1
  for flag in disable remove paused; do
    if [ -e "$MODDIR/$flag" ]; then echo '模块当前已停用或暂停'; return 1; fi
  done
  if service_alive; then echo '已经运行'; return 0; fi
  "$BB" sh "$MODDIR/bin/check.sh" || return 1
  [ ! -e "$RUNDIR/run.log" ] || mv -f "$RUNDIR/run.log" "$RUNDIR/run.previous.log"
  nohup "$BB" setsid "$BB" sh "$MODDIR/bin/control.sh" run > "$RUNDIR/run.log" 2>&1 < /dev/null &
}

stop_service() {
  setup_runtime || return 1
  touch "$RUNDIR/stop"
  if service_alive; then kill -TERM "$PID" 2>/dev/null; fi
  tries=0
  while service_alive && [ "$tries" -lt 6 ]; do sleep 1; tries=$((tries+1)); done
  if service_alive; then
    stop_native
    if service_alive; then kill -KILL "$PID" 2>/dev/null; fi
  fi
  # Acquire the same service lock before orphan cleanup. Never race a live owner.
  exec 8> "$RUNDIR/service.lock" || return 1
  lock_tries=0
  until "$BB" flock -n 8; do
    lock_tries=$((lock_tries+1))
    [ "$lock_tries" -lt 8 ] || { echo '停止超时，未清理其他进程的资源'; return 1; }
    sleep 0.5
  done
  stop_native
  cleanup_trace || return 1
  rm -f "$RUNDIR/native.pid" "$RUNDIR/service.pid"
  echo '桥接已停止'
}

case "$MODE" in
  run) run_service "$@" ;;
  start) start_service ;;
  boot)
    tries=0
    while [ "$(/system/bin/getprop sys.boot_completed)" != 1 ] && [ "$tries" -lt 60 ]; do
      sleep 2; tries=$((tries+1))
    done
    [ "$(/system/bin/getprop sys.boot_completed)" = 1 ] && start_service
    ;;
  stop) stop_service ;;
  toggle)
    if [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ]; then
      echo '请先在 KernelSU 启用模块，或取消卸载。'; exit 1
    fi
    if [ -e "$MODDIR/paused" ]; then
      rm -f "$MODDIR/paused"
      start_service || exit 1
      sleep 2
      if native_alive; then echo '已恢复，动作仍在焦点笔 Plus 中设置。'; else cat "$RUNDIR/run.log"; exit 1; fi
    else
      touch "$MODDIR/paused"
      stop_service || exit 1
      echo '已暂停；再次点击动作按钮恢复。'
    fi
    ;;
  status)
    if native_alive; then echo '运行中'; else echo '未运行'; fi
    [ ! -f "$RUNDIR/status.json" ] || cat "$RUNDIR/status.json"
    ;;
  *) echo '用法：control.sh start|stop|status|toggle'; exit 2 ;;
esac
