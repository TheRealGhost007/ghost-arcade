#!/bin/bash
# monkey.sh BINARY OUT_DIR RUNS FRAMES [FIRST_SEED]
# Random-input runs of the real launcher through the screenshot harness: each
# run is a script of about FRAMES frames of mixed keys (letters, arrows, held
# arrows, Alt+Enter, F11, typing), pad buttons and the stick, mouse moves,
# clicks, side buttons and the wheel, focus flips, a game exiting, installs
# finishing and a game rewriting prefs.ini. A run fails on a crash, a hang,
# ASan/UBSan output, or if the launcher has quit or cannot draw by the end
# (the last line is a shot) -- unless its keys found Leave the arcade, which
# ends it early and is fine. Seeds make every run repeatable: seed N writes
# OUT_DIR/scripts/monkey-N.txt. The X button is never clicked, so a run
# always lasts its length; everything else, admin keys included, is fair game
# (the catalog it edits is the temp HOME's).
set -u
if [ $# -lt 4 ]; then echo "usage: monkey.sh BINARY OUT_DIR RUNS FRAMES [FIRST_SEED]" >&2; exit 2; fi
BIN=$1 OUT=$2 RUNS=$3 FRAMES=$4 SEED0=${5:-1}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/scripts"

KEYS=(LEFT RIGHT UP DOWN ENTER SPACE ESC BACKSPACE DELETE TAB HOME END PAGE_UP PAGE_DOWN F11
      A B C D E F G H I J K L M N O P Q R S T U V W X Y Z 1 2 MINUS PERIOD SLASH KP_ENTER)
CHORDS=("ALT ENTER" "SHIFT U" "SHIFT TAB" "CTRL V" "CTRL A" "SHIFT A" "RIGHT_ALT ENTER" "SHIFT BACKSLASH" "SHIFT D")
HELD=(LEFT RIGHT UP DOWN BACKSPACE W S DELETE)
PADB=(A B X Y LB RB LT RT VIEW MENU LS RS UP DOWN LEFT RIGHT)
MOUSEB=(left right middle back forward)
WORDS=(TRG ghost "a b" "x|y" ~/bin/blockfall "../etc" 1234567890 "Hello, World!" "" "zz")

pick() { local -n arr=$1; printf '%s' "${arr[RANDOM % ${#arr[@]}]}"; }

# Frames the driver spends typing STR: 2 a key, 4 with Shift (see Tap in driver.c).
text_frames() {
    local s=$1 n=0 i c
    for ((i = 0; i < ${#s}; i++)); do
        c=${s:i:1}
        case "$c" in [A-Z\)\!@\#\$%^\&*\(_+\>\<?\|:\"~{}]) n=$((n + 4)) ;; *) n=$((n + 2)) ;; esac
    done
    echo $n
}

# A point anywhere on the logical screen or the letterbox, but never on a
# way out, which would end the run early: the X (920..944, 16..40), the EXIT
# sign (20..72, 60..88) and the Arcade menu's Leave row (16..392, 432..472).
# The keys and the pad can still find Leave; a run that does ends there
# (SHOTS_QUIT_OK), which is not a failure.
point() {
    local x y
    while :; do
        x=$((RANDOM % 1000 - 20)) y=$((RANDOM % 680 - 20))
        if [ $x -ge 910 ] && [ $x -le 954 ] && [ $y -ge 6 ] && [ $y -le 50 ]; then continue; fi
        if [ $x -ge 10 ] && [ $x -le 82 ] && [ $y -ge 50 ] && [ $y -le 98 ]; then continue; fi
        if [ $x -ge 6 ] && [ $x -le 418 ] && [ $y -ge 422 ] && [ $y -le 482 ]; then continue; fi
        echo "$x $y"
        return
    done
}

gen() {
    local seed=$1 t=0 r
    RANDOM=$seed
    echo "#! env SHOTS_QUIT_OK=1"
    echo "# monkey run, seed $seed, about $FRAMES frames (made by tools/shots/monkey.sh)"
    echo "wait 10"
    local nextlog=100
    while [ $t -lt "$FRAMES" ]; do
        # Where the launcher is, now and then, for the run's log.
        if [ $t -ge $nextlog ]; then echo "log monkey"; nextlog=$((nextlog + 100)); fi
        r=$((RANDOM % 100))
        if   [ $r -lt 30 ]; then echo "tap $(pick KEYS)"; t=$((t + 2))
        elif [ $r -lt 35 ]; then echo "tap $(pick CHORDS)"; t=$((t + 4))
        elif [ $r -lt 40 ]; then local n=$((RANDOM % 50 + 5)); echo "hold $(pick HELD) $n"; t=$((t + n + 1))
        elif [ $r -lt 42 ]; then local w; w=$(pick WORDS); echo "text $w"; t=$((t + $(text_frames "$w")))
        elif [ $r -lt 43 ]; then echo "clip $(pick WORDS)|pasted $RANDOM"; t=$((t + 1))
        elif [ $r -lt 45 ]; then if [ $((RANDOM % 2)) = 0 ]; then echo "pad on"; else echo "pad off"; fi; t=$((t + 1))
        elif [ $r -lt 57 ]; then echo "pad tap $(pick PADB)"; t=$((t + 2))
        elif [ $r -lt 60 ]; then local n=$((RANDOM % 50 + 5)); echo "pad hold $(pick PADB) $n"; t=$((t + n + 1))
        elif [ $r -lt 62 ]; then echo "stick $((RANDOM % 3 - 1)) $((RANDOM % 3 - 1))"; t=$((t + 1))
        elif [ $r -lt 70 ]; then echo "mouse $(point)"; t=$((t + 1))
        elif [ $r -lt 77 ]; then echo "click $(pick MOUSEB) $(point)"; t=$((t + 3))
        elif [ $r -lt 79 ]; then echo "mdown $(pick MOUSEB)"; echo "mouse $(point)"; echo "mup $(pick MOUSEB)"; t=$((t + 3))
        elif [ $r -lt 82 ]; then echo "wheel $((RANDOM % 7 - 3))"; t=$((t + 1))
        elif [ $r -lt 84 ]; then echo "focus off"; local n=$((RANDOM % 20 + 1)); echo "wait $n"; echo "focus on"; t=$((t + n + 2))
        elif [ $r -lt 86 ]; then echo "gameexit"; t=$((t + 1))
        elif [ $r -lt 88 ]; then if [ $((RANDOM % 2)) = 0 ]; then echo "install ok"; else echo "install fail"; fi; t=$((t + 1))
        elif [ $r -lt 89 ]; then
            echo "write .config/ghost-arcade/prefs.ini music_volume=$((RANDOM % 11 * 10))\\nscreen_shake=$((RANDOM % 2))\\nreduced_flashing=$((RANDOM % 2))\\nfullscreen=$((RANDOM % 2))\\n"
        else local n=$((RANDOM % 40 + 1)); echo "wait $n"; t=$((t + n))
        fi
    done
    echo "pad off"
    echo "wait 5"
    echo "shot monkey-$seed-end"
}

scripts=()
for ((i = 0; i < RUNS; i++)); do
    seed=$((SEED0 + i))
    f="$OUT/scripts/monkey-$seed.txt"
    gen "$seed" > "$f"
    scripts+=("$f")
done
exec "$HERE/run.sh" "$BIN" "$OUT" "${scripts[@]}"
