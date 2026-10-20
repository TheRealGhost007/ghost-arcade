#!/bin/bash
# run.sh BINARY OUT_DIR SCRIPT...
# Runs the screenshot driver (built by `make shots`) once per script, each in
# its own temp HOME made by fixtures.sh, and writes the PNGs and a log per
# script into OUT_DIR. While each run lasts it asks Hyprland every 50 ms
# whether a window of that process (by pid, or by the unique title the
# driver gives it) is mapped: one that is, is killed at once and the run
# fails. Exit status: 0 when every run passed.
# SHOTS_KEEP=1 keeps the temp HOMEs; otherwise only failed ones are kept.
# A script can set fixture options for itself with lines such as
#   #! env SHOTS_FIRSTRUN=1 SHOTS_UPDATE=current
# (SHOTS_* names only, values without spaces; see fixtures.sh).
set -u
if [ $# -lt 3 ]; then echo "usage: run.sh BINARY OUT_DIR SCRIPT..." >&2; exit 2; fi
BIN=$(realpath "$1")
OUT=$2
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

HYPR=0
if command -v hyprctl >/dev/null 2>&1 && hyprctl clients -j >/dev/null 2>&1; then HYPR=1; fi
[ $HYPR = 1 ] || echo "run.sh: no hyprctl here, so the never-mapped check cannot run" >&2
# raylib's GLFW talks X11 (Xwayland) here: the X server is asked as well,
# since a window manager gives every window it maps a WM_STATE.
XPROP=0
if [ -n "${DISPLAY-}" ] && command -v xprop >/dev/null 2>&1; then XPROP=1; fi

mapped_now() {
    if [ $HYPR = 1 ] && hyprctl clients -j 2>/dev/null | grep -Eq "\"pid\": *$1,|ghost-launcher-shots-$1\""; then return 0; fi
    if [ $XPROP = 1 ] && xprop -name "ghost-launcher-shots-$1" WM_STATE 2>/dev/null | grep -Eq "window state: (Normal|Iconic)"; then return 0; fi
    return 1
}

status=0
for script in "$@"; do
    name=$(basename "$script" .txt)
    opts=()
    for kv in $(sed -n 's/^#![[:space:]]*env[[:space:]]\+//p' "$script"); do
        if [[ ! $kv =~ ^SHOTS_[A-Z_]+=[A-Za-z0-9._,-]*$ ]]; then echo "$name: FAILED (bad '#! env' option: $kv)"; status=1; continue 2; fi
        opts+=("$kv")
    done
    T=$(mktemp -d "${TMPDIR:-/tmp}/ghost-shots.XXXXXX")
    if ! env "${opts[@]}" "$HERE/fixtures.sh" "$T"; then echo "$name: FAILED (fixtures)"; status=1; continue; fi
    log="$OUT/$name.log"
    env "${opts[@]}" HOME="$T" XDG_DATA_HOME="$T/.local/share" XDG_CONFIG_HOME="$T/.config" XDG_CACHE_HOME="$T/.cache" \
        SHOTS_REAL_HOME="$HOME" SHOTS_SCRIPT="$(realpath "$script")" SHOTS_OUT="$OUT" \
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
        "$BIN" > "$log" 2>&1 &
    pid=$!
    samples=0 mapped=0 timedout=0 start=$SECONDS
    while kill -0 "$pid" 2>/dev/null; do
        if mapped_now "$pid"; then
            mapped=1
            kill "$pid" 2>/dev/null
            break
        fi
        [ $HYPR = 1 ] && samples=$((samples + 1))
        if [ $((SECONDS - start)) -gt 120 ]; then timedout=1; kill "$pid" 2>/dev/null; break; fi
        sleep 0.05
    done
    wait "$pid"
    rc=$?
    shots=$(grep -c '\] shot ' "$log")
    fails=$(grep -c 'EXPECT FAIL\|SHOT FAILED\|WRITE FAILED\|WRITE REFUSED\|CHMOD FAILED\|QUIT MISMATCH\|HANG\|runtime error\|AddressSanitizer' "$log")
    verdict=ok
    if [ $mapped = 1 ]; then verdict="FAILED: THE WINDOW WAS MAPPED"
    elif [ $timedout = 1 ]; then verdict="FAILED: timed out"
    elif [ $rc != 0 ] || [ "$fails" != 0 ]; then verdict="FAILED (rc=$rc, see $log)"
    elif [ $HYPR = 1 ] && [ $samples -lt 3 ]; then verdict="FAILED: too short to check the window ($samples samples)"
    fi
    checks=$(grep -o 'expects=[0-9]*/[0-9]*' "$log" | tail -1)
    echo "$name: $verdict  shots=$shots ${checks:-expects=?} hyprctl-samples=$samples"
    grep 'EXPECT FAIL\|SHOT FAILED\|QUIT MISMATCH\|CHMOD FAILED\|app .* is \|HANG' "$log" | sed 's/^/    /'
    [ "$verdict" = ok ] || status=1
    if [ "$verdict" = ok ] && [ -z "${SHOTS_KEEP-}" ]; then
        case "$T" in */ghost-shots.*) rm -rf "$T" ;; esac
    else
        echo "    temp HOME kept: $T"
    fi
done
exit $status
