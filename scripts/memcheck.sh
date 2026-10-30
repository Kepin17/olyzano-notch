#!/usr/bin/env bash
# Memory of Olyzano Notch at rest, from /proc/<pid>/smaps_rollup. PSS is the figure to watch: it is what the program really costs
# (the Qt, KDE and Mesa libraries it shares with other programs are divided between them); RSS counts them in full.
#   scripts/memcheck.sh [build/notch] [variant ...]       variants: default low software vulkan arena2 (default: default); "low" is what Low memory mode does
# Runs a throw-away copy in its own config folder (the capsule, nothing in the shelf), so a running Notch is left alone, and
# prints RSS / PSS / anonymous PSS / threads at 10, 25 and 40 seconds.
set -u
BIN=${1:-build/notch}
shift || true
VARIANTS=${*:-default}
TMP=$(mktemp -d)
mkdir -p "$TMP/c/notch" "$TMP/d"
printf '[General]\ncapsuleMode=capsule\n' > "$TMP/c/notch/activities.conf"
trap 'rm -rf "$TMP"' EXIT
for v in $VARIANTS; do
    env_extra=()
    case $v in
        low)      env_extra=(QT_QUICK_BACKEND=software QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=shm) ;;
        software) env_extra=(QT_QUICK_BACKEND=software) ;;
        vulkan)   env_extra=(QSG_RHI_BACKEND=vulkan) ;;
        arena2)   env_extra=(MALLOC_ARENA_MAX=2) ;;
        default)  ;;
        *) echo "unknown variant $v" >&2; continue ;;
    esac
    env "${env_extra[@]}" QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-wayland} XDG_CONFIG_HOME="$TMP/c" XDG_DATA_HOME="$TMP/d" "$BIN" >/dev/null 2>&1 &
    pid=$!
    for t in 10 25 40; do
        sleep $((t == 10 ? 10 : 15))
        kill -0 "$pid" 2>/dev/null || { echo "$v: the program stopped (is another Notch running? only one may)"; break; }
        printf '%-9s t=%2ss  RSS %4d MB  PSS %4d MB  anon %4d MB  threads %d\n' "$v" "$t" \
            $(( $(awk '/VmRSS/{print $2}' /proc/$pid/status) / 1024 )) \
            $(( $(awk '/^Pss:/{print $2}' /proc/$pid/smaps_rollup) / 1024 )) \
            $(( $(awk '/^Pss_Anon/{print $2}' /proc/$pid/smaps_rollup) / 1024 )) \
            "$(ls /proc/$pid/task | wc -l)"
    done
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    sleep 2
done
