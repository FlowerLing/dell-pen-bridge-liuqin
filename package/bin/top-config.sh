#!/system/bin/sh
# Configuration is data, never sourced as shell code.
MODDIR=$(CDPATH= cd "$(dirname "$0")/.." && pwd) || exit 1
BB=/data/adb/ksu/bin/busybox
DATA=${DELL_TOP_DATA:-/data/adb/dell_pen_bridge_liuqin}
RUNDIR=${DELL_TOP_RUNTIME:-/dev/dell_pen_top_liuqin}
CONFIG=$DATA/top.conf
umask 077
[ ! -L "$DATA" ] && [ ! -L "$RUNDIR" ] || exit 1
mkdir -p "$DATA" "$RUNDIR" || exit 1
chmod 700 "$DATA" "$RUNDIR"
valid_action() { case "$1" in none|back|home|recents|volume_up|volume_down|play_pause|next|previous|screenshot|custom) return 0;; *) return 1;; esac; }
read_config() {
  enabled=0; single=none; double=none; long=none; sc=; dc=; lc=
  if [ -f "$CONFIG" ]; then
    { read -r enabled; read -r single; read -r double; read -r long; read -r sc; read -r dc; read -r lc; } < "$CONFIG"
  fi
  case "$enabled" in 0|1) ;; *) enabled=0;; esac
  valid_action "$single" || single=none
  valid_action "$double" || double=none
  valid_action "$long" || long=none
}
case "${1:-get}" in
  get)
    read_config
    printf '%s\n' "$enabled" "$single" "$double" "$long" "$sc" "$dc" "$lc"
    ;;
  save)
    [ "$#" -eq 8 ] || exit 2
    case "$2" in 0|1) ;; *) exit 2;; esac
    valid_action "$3" && valid_action "$4" && valid_action "$5" || exit 2
    for command in "$6" "$7" "$8"; do
      [ "${#command}" -le 5464 ] || exit 2
      case "$command" in *[!A-Za-z0-9+/=]*) exit 2;; esac
      printf '%s' "$command" | "$BB" base64 -d >/dev/null 2>&1 || exit 2
    done
    exec 9> "$DATA/config.lock"; "$BB" flock -x 9 || exit 1
    TEMP=$("$BB" mktemp "$DATA/top.conf.XXXXXX") || exit 1
    printf '%s\n' "$2" "$3" "$4" "$5" "$6" "$7" "$8" > "$TEMP" || exit 1
    mv -f "$TEMP" "$CONFIG" || exit 1
    echo '已保存，下次按键生效'
    ;;
  status)
    pid=$(cat "$RUNDIR/pid" 2>/dev/null)
    case "$pid" in ''|*[!0-9]*) pid=0;; esac
    if [ "$pid" -gt 1 ] && [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = "$MODDIR/bin/dell-top-button" ] && [ -f "$RUNDIR/status.json" ]; then cat "$RUNDIR/status.json"; else echo '{"running":false,"enabled":false,"connected":false,"events":0,"last":"none"}'; fi
    ;;
  run)
    for flag in disable remove paused; do [ ! -e "$MODDIR/$flag" ] || exit 0; done
    read_config; [ "$enabled" = 1 ] || exit 0
    case "$2" in single) action=$single; code=$sc;; double) action=$double; code=$dc;; long) action=$long; code=$lc;; *) exit 2;; esac
    exec 8> "$DATA/action.lock"; "$BB" flock -n 8 || exit 0
    case "$action" in
      none) exit 0;;
      custom)
        command=$(printf '%s' "$code" | "$BB" base64 -d) || exit 2
        [ -n "$command" ] || exit 0
        "$BB" timeout -s KILL 8 /system/bin/sh -c "$command" >/dev/null 2>&1
        ;;
      *)
        case "$action" in back) key=4;; home) key=3;; recents) key=187;; volume_up) key=24;; volume_down) key=25;; play_pause) key=85;; next) key=87;; previous) key=88;; screenshot) key=120;; *) exit 2;; esac
        "$BB" timeout -s KILL 8 /system/bin/input keyevent "$key" >/dev/null 2>&1
        ;;
    esac
    result=$?
    printf '%s %s exit=%s\n' "$2" "$action" "$result" > "$RUNDIR/last-action.txt"
    exit "$result"
    ;;
  *) exit 2;;
esac
