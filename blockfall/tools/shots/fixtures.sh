#!/bin/bash
# fixtures.sh HOME: fills a fresh run.sh temp folder with what Blockfall reads,
# so screens have data and nothing real is touched.
#   SHOTS_FIRSTRUN=1     no howto_seen flag (the game opens on How to play)
#   SHOTS_SETUP=m,p,w,h  config.ini's remembered play setup (mode, preset, w, h)
#   SHOTS_SHAKE=0|1, SHOTS_FLASH=0|1  prefs.ini screen_shake / reduced_flashing
#   SHOTS_NOSCORES=1     empty score tables
#   SHOTS_NOACH=1        no achievements yet (a finished run then earns a toast)
#   SHOTS_SAVE=N         save.dat's classic best (default 24680)
#   SHOTS_OWNER=1        the owner's tables today: classic top 12248, one old
#                        points-ranked Sprint row, nothing else
#   SHOTS_TUNED=1        the F2 panel's game speed set to 0.5 (a playtest)
#   SHOTS_MOTION=reduced config.ini's Motion setting is Reduced (2.2.0)
#   SHOTS_HIDECARD=1     config.ini hides the controls card in play (2.3.0)
#   SHOTS_READY=1        READY-GO at the start of a run (2.4.0). Without it
#                        config.ini turns it off, so the scripts written before
#                        it start playing at once, as they always did
#   SHOTS_SNAP=1         config.ini's Piece motion is Snap (2.4.0)
#   SHOTS_QUICKR=1       config.ini: R restarts at once (2.5.0)
#   SHOTS_COUNTDOWN=1    config.ini: the countdown after a pause is on (2.5.0)
#   SHOTS_NOGHOST=1      config.ini: the ghost piece is hidden (2.5.0)
#   SHOTS_WIPE=1         (read by the driver) the block wipe into and out of
#                        a run is drawn; without it every change is a cut
#   SHOTS_CONFIG=k-V,..  config.ini lines of your own, key-VALUE pairs
#                        (hard_drop-ESCAPE,quit-SPACE: a 2.1.0 swap) (3.0.0).
#                        136-old-config-rebound.txt moves every key off its
#                        default this way, on top of SHOTS_OLDCONFIG
#   SHOTS_OLDCONFIG=1    config.ini is the owner's 2.0.0 file as it is (no
#                        line from a later build), before any other option
set -eu
H=$1
case "$H" in */bf-shots.*) ;; *) echo "fixtures.sh: $H is not a run.sh temp folder" >&2; exit 1 ;; esac
DATA=$H/.local/share
CONF=$H/.config
GL=$DATA/ghost-launcher
mkdir -p "$GL/scores/global" "$GL/achievements" "$CONF/ghost-arcade" "$CONF/blockfall" "$DATA/blockfall"
day() { date -d "-$1 days" +%F; }
printf '# Ghost Arcade preferences, shared by every game\nmusic_volume=70\nscreen_shake=%s\nreduced_flashing=%s\nfullscreen=0\n' \
    "${SHOTS_SHAKE-1}" "${SHOTS_FLASH-0}" > "$CONF/ghost-arcade/prefs.ini"
printf '# Ghost Launcher profiles\n__DEFAULT__|TRG\n' > "$GL/profiles.txt"
printf 'high_score=%s\nhigh_level=12\nhigh_lines=118\n' "${SHOTS_SAVE-24680}" > "$DATA/blockfall/save.dat"
[ -n "${SHOTS_FIRSTRUN-}" ] || echo 1 > "$DATA/blockfall/howto_seen"
if [ -n "${SHOTS_OLDCONFIG-}" ]; then
    printf '# blockfall settings\nleft=LEFT\nleft2=A\nright=RIGHT\nright2=D\nsoft_drop=DOWN\nsoft_drop2=S\nrotate=UP\nrotate2=W\nhard_drop=SPACE\nhold=C\npause=P\nrestart=R\nquit=ESCAPE\nvolume=50\naudio_enabled=1\nsetup_mode=3\nsetup_preset=1\nsetup_width=10\nsetup_height=20\n' \
        > "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_SETUP-}" ]; then
    IFS=, read -r m p w h <<< "$SHOTS_SETUP"
    printf 'setup_mode=%s\nsetup_preset=%s\nsetup_width=%s\nsetup_height=%s\n' "$m" "$p" "$w" "$h" >> "$CONF/blockfall/config.ini"
fi
if [ "${SHOTS_MOTION-}" = reduced ]; then
    printf 'reduced_motion=1\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_HIDECARD-}" ]; then
    printf 'hide_controls=1\n' >> "$CONF/blockfall/config.ini"
fi
if [ -z "${SHOTS_READY-}" ]; then
    printf 'ready_go=0\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_SNAP-}" ]; then
    printf 'piece_snap=1\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_QUICKR-}" ]; then
    printf 'quick_restart=1\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_COUNTDOWN-}" ]; then
    printf 'resume_countdown=1\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_NOGHOST-}" ]; then
    printf 'ghost_piece=0\n' >> "$CONF/blockfall/config.ini"
fi
if [ -n "${SHOTS_CONFIG-}" ]; then
    IFS=, read -ra pairs <<< "$SHOTS_CONFIG"
    for kv in "${pairs[@]}"; do printf '%s=%s\n' "${kv%%-*}" "${kv#*-}" >> "$CONF/blockfall/config.ini"; done
fi
if [ -n "${SHOTS_TUNED-}" ]; then
    mkdir -p "$CONF/ghost-arcade/tuning"
    printf 'speed=0.5\n' > "$CONF/ghost-arcade/tuning/blockfall.ini"
fi
if [ -n "${SHOTS_OWNER-}" ]; then
    printf '# Ghost Arcade scores for blockfall: mode|username|score|date\nmarathon|TRG|12248|%s\nsprint|TRG|7620|%s\nmarathon-14x20|TRG|9300|%s\n' \
        "$(day 25)" "$(day 20)" "$(day 3)" > "$GL/scores/blockfall.txt"
elif [ -z "${SHOTS_NOSCORES-}" ]; then
    {
        printf 'marathon|TRG|24680|%s\nmarathon|Arcade|18200|%s\nmarathon|TRG|12040|%s\nmarathon|TRG|9100|%s\nmarathon|Arcade|4410|%s\n' \
            "$(day 0)" "$(day 2)" "$(day 3)" "$(day 9)" "$(day 12)"
        printf 'power|TRG|31500|%s\npower|TRG|17250|%s\n' "$(day 1)" "$(day 4)"
        printf 'sprint|TRG|7620|%s\nultra|TRG|11900|%s\n' "$(day 1)" "$(day 6)"
        printf 'marathon-14x20|TRG|15300|%s\npower-24x36|TRG|8800|%s\n' "$(day 2)" "$(day 5)"
    } > "$GL/scores/blockfall.txt"
    {
        printf 'marathon|Ghost|88120|%s\nmarathon|Moth|61400|%s\nmarathon|Pix|40250|%s\nmarathon|TRG|24680|%s\nmarathon|Zed|19800|%s\n' \
            "$(day 1)" "$(day 2)" "$(day 3)" "$(day 0)" "$(day 5)"
        printf 'marathon|Nova|17600|%s\nmarathon|Kit|9100|%s\nmarathon|Rune|5000|%s\n' "$(day 6)" "$(day 8)" "$(day 9)"
        printf 'power|Ghost|45000|%s\npower|TRG|31500|%s\n' "$(day 2)" "$(day 1)"
        printf 'sprint40|Ghost|9994179|%s\nsprint40|Moth|9992760|%s\n' "$(day 1)" "$(day 2)"
    } > "$GL/scores/global/blockfall.txt"
fi
if [ -z "${SHOTS_NOACH-}" ]; then printf 'r0|%s\ns1|%s\n' "$(day 9)" "$(day 7)" > "$GL/achievements/blockfall.txt"; fi
