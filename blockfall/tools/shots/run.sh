#!/bin/bash
# run.sh BINARY OUT_DIR SCRIPT...  (adapted from ghost-launcher/tools/shots/run.sh)
# Runs the screenshot driver (built by `make shots`) once per script, each in
# its own temp HOME made by fixtures.sh under ${TMPDIR:-/tmp}, and writes the
# PNGs and a log per script into OUT_DIR. While each run lasts it asks Hyprland
# (and the X server) every 50 ms whether a window of that process is mapped:
# one that is, is killed at once and the run fails. Exit status: 0 when every
# run passed. SHOTS_KEEP=1 keeps the temp HOMEs; otherwise only failed ones are.
# A script can set fixture options for itself with lines such as
#   #! env SHOTS_FIRSTRUN=1 SHOTS_SETUP=1,0,10,20
# (SHOTS_* names only, values without spaces; see fixtures.sh).
set -u
if [ $# -lt 3 ]; then echo "usage: run.sh BINARY OUT_DIR SCRIPT..." >&2; exit 2; fi
BIN=$(realpath "$1"); OUT=$2; shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
TMPBASE=${TMPDIR:-/tmp}
mkdir -p "$TMPBASE"
HYPR=0
if command -v hyprctl >/dev/null 2>&1 && hyprctl clients -j >/dev/null 2>&1; then HYPR=1; fi
[ $HYPR = 1 ] || echo "run.sh: no hyprctl here, so the never-mapped check cannot run" >&2
XPROP=0
if [ -n "${DISPLAY-}" ] && command -v xprop >/dev/null 2>&1; then XPROP=1; fi
mapped_now() {
    if [ $HYPR = 1 ] && hyprctl clients -j 2>/dev/null | grep -Eq "\"pid\": *$1,|blockfall-shots-$1\""; then return 0; fi
    if [ $XPROP = 1 ] && xprop -name "blockfall-shots-$1" WM_STATE 2>/dev/null | grep -Eq "window state: (Normal|Iconic)"; then return 0; fi
    return 1
}
status=0
passed=0
for script in "$@"; do
    name=$(basename "$script" .txt)
    opts=()
    for kv in $(sed -n 's/^#![[:space:]]*env[[:space:]]\+//p' "$script"); do
        if [[ ! $kv =~ ^SHOTS_[A-Z_]+=[A-Za-z0-9._,-]*$ ]]; then echo "$name: FAILED (bad env $kv)"; status=1; continue 2; fi
        opts+=("$kv")
    done
    T=$(mktemp -d "$TMPBASE/bf-shots.XXXXXX")
    if ! env "${opts[@]}" "$HERE/fixtures.sh" "$T"; then echo "$name: FAILED (fixtures)"; status=1; continue; fi
    log="$OUT/$name.log"
    env "${opts[@]}" HOME="$T" XDG_DATA_HOME="$T/.local/share" XDG_CONFIG_HOME="$T/.config" XDG_CACHE_HOME="$T/.cache" \
        PATH=/usr/bin:/bin SHOTS_REAL_HOME="${SHOTS_REAL_HOME:-$HOME}" SHOTS_SCRIPT="$(realpath "$script")" SHOTS_OUT="$OUT" \
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
        "$BIN" > "$log" 2>&1 &
    pid=$!
    samples=0 mapped=0 timedout=0 start=$SECONDS
    while kill -0 "$pid" 2>/dev/null; do
        if mapped_now "$pid"; then mapped=1; kill "$pid" 2>/dev/null; break; fi
        [ $HYPR = 1 ] && samples=$((samples + 1))
        if [ $((SECONDS - start)) -gt 180 ]; then timedout=1; kill "$pid" 2>/dev/null; break; fi
        sleep 0.05
    done
    wait "$pid"; rc=$?
    # Files the game must (or must not) have written by the time it exited:
    #   #! after-file PATH TEXT     #! after-nofile PATH TEXT   (PATH under the temp HOME)
    while IFS= read -r line; do
        kind=${line%% *}; rest=${line#* }; path=${rest%% *}; text=${rest#* }
        case "$path" in /*|*..*) echo "[run.sh] AFTER FAIL: bad path $path" >> "$log"; continue ;; esac
        if [ "$kind" = after-file ]; then
            if grep -qF -- "$text" "$T/$path" 2>/dev/null; then echo "[run.sh] after ok: $path has '$text'" >> "$log"
            else echo "[run.sh] AFTER FAIL: $path lacks '$text'" >> "$log"; fi
        else
            if grep -qF -- "$text" "$T/$path" 2>/dev/null; then echo "[run.sh] AFTER FAIL: $path has '$text'" >> "$log"
            else echo "[run.sh] after ok: $path lacks '$text'" >> "$log"; fi
        fi
    done < <(sed -n 's/^#![[:space:]]*\(after-file\|after-nofile\)[[:space:]]\+/\1 /p' "$script")
    shots=$(grep -c '\] shot ' "$log")
    fails=$(grep -c 'EXPECT FAIL\|GAME FAIL\|SHOT FAILED\|WRITE FAILED\|QUIT MISMATCH\|HANG\|AFTER FAIL\|runtime error\|ERROR: AddressSanitizer' "$log")
    verdict=ok
    if [ $mapped = 1 ]; then verdict="FAILED: THE WINDOW WAS MAPPED"
    elif [ $timedout = 1 ]; then verdict="FAILED: timed out"
    elif [ $rc != 0 ] || [ "$fails" != 0 ]; then verdict="FAILED (rc=$rc, see $log)"
    elif [ $HYPR = 1 ] && [ $samples -lt 3 ]; then verdict="FAILED: too short to check the window ($samples samples)"
    fi
    echo "$name: $verdict  shots=$shots hyprctl-samples=$samples mapped=$mapped"
    grep 'EXPECT FAIL\|GAME FAIL\|SHOT FAILED\|QUIT MISMATCH\|HANG\|AFTER FAIL\| is .*not \|were .*not ' "$log" | sed 's/^/    /'
    if [ "$verdict" = ok ]; then passed=$((passed + 1)); else status=1; fi
    if [ "$verdict" = ok ] && [ -z "${SHOTS_KEEP-}" ]; then
        case "$T" in */bf-shots.*) rm -rf "$T" ;; esac
    else
        echo "    temp HOME kept: $T"
    fi
done
echo "shots: $passed of $# scripts passed, PNGs in $OUT"
exit $status
