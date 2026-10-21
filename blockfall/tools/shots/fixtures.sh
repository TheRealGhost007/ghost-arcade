#!/bin/bash
# fixtures.sh HOME: fills a fresh run.sh temp folder with what Blockfall reads,
# so screens have data and nothing real is touched.
#   SHOTS_FIRSTRUN=1     no howto_seen flag (the game opens on How to play)
#   SHOTS_SETUP=m,p,w,h  config.ini's remembered play setup (mode, preset, w, h)
#   SHOTS_SHAKE=0|1, SHOTS_FLASH=0|1  prefs.ini screen_shake / reduced_flashing
#   SHOTS_NOSCORES=1     empty score tables
#   SHOTS_NOACH=1        no achievements yet (a finished run then earns a toast)
#   SHOTS_SAVE=N         save.dat's classic best (default 24680)
#   SHOTS_OWNER=1        the owner's tables today: classic top 12248 (the
#                        old-rules marathon board since 3.2.0), one old
#                        points-ranked Sprint row, nothing else: the fresh
#                        classic Marathon board (marathon-10x20) is empty
#                        The default tables carry the same rows under
#                        marathon (old rules) and marathon-10x20 (3.2.0's
#                        board), so a classic run still has a best to beat
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
#   SHOTS_PACE=1         a Sprint best pace on the classic board (3.1.0):
#                        splits.dat with 0:25.50, 0:51.00, 1:16.50 and the
#                        finish 1:40.00, and that finish as this machine's
#                        Sprint 40 best
#   SHOTS_STATS=1        a lifetime stats file (3.3.0): runs in every mode
#                        but Zen, counting since 2026-10-11, with the best
#                        pieces per second in Sprint and the biggest combo
#                        and most quads in the Marathon. Without it there
#                        is no stats.dat (the Stats screen's empty state)
#   SHOTS_STATS=marathon the same, but the Marathon's best pieces per
#                        second (2.41) beats Sprint's, so the Marathon holds
#                        all three records (3.6.1)
#   SHOTS_DAILY=YYYYMMDD the Daily challenge's day (3.6.0; the driver reads
#                        it too: 20261020 unless set)
#   SHOTS_DAILYLOG=1     the daily played on that day and the two before
#                        (daily.dat: a streak of 3, three runs today, best
#                        9 800), and today's daily board: here TRG 9 800 and
#                        Arcade 6 200; the world Ghost 21 400, Moth 15 000,
#                        TRG 9 800, Pix 4 000. Without it there is no
#                        daily.dat and today's board is empty
#   SHOTS_DAILYLAUNCH=1  (read by the driver) the game starts with --daily
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
        for m in marathon marathon-10x20; do
            printf "$m|TRG|24680|%s\n$m|Arcade|18200|%s\n$m|TRG|12040|%s\n$m|TRG|9100|%s\n$m|Arcade|4410|%s\n" \
                "$(day 0)" "$(day 2)" "$(day 3)" "$(day 9)" "$(day 12)"
        done
        printf 'power|TRG|31500|%s\npower|TRG|17250|%s\n' "$(day 1)" "$(day 4)"
        printf 'sprint|TRG|7620|%s\nultra|TRG|11900|%s\n' "$(day 1)" "$(day 6)"
        printf 'marathon-14x20|TRG|15300|%s\npower-24x36|TRG|8800|%s\n' "$(day 2)" "$(day 5)"
    } > "$GL/scores/blockfall.txt"
    {
        for m in marathon marathon-10x20; do
            printf "$m|Ghost|88120|%s\n$m|Moth|61400|%s\n$m|Pix|40250|%s\n$m|TRG|24680|%s\n$m|Zed|19800|%s\n" \
                "$(day 1)" "$(day 2)" "$(day 3)" "$(day 0)" "$(day 5)"
            printf "$m|Nova|17600|%s\n$m|Kit|9100|%s\n$m|Rune|5000|%s\n" "$(day 6)" "$(day 8)" "$(day 9)"
        done
        printf 'power|Ghost|45000|%s\npower|TRG|31500|%s\n' "$(day 2)" "$(day 1)"
        printf 'sprint40|Ghost|9994179|%s\nsprint40|Moth|9992760|%s\n' "$(day 1)" "$(day 2)"
    } > "$GL/scores/global/blockfall.txt"
fi
if [ -n "${SHOTS_PACE-}" ]; then
    printf '# Blockfall Sprint best pace: board|at 10|at 20|at 30|finish (hundredths of a second)\nsprint40|2550|5100|7650|10000\n' > "$DATA/blockfall/splits.dat"
    printf 'sprint40|TRG|9990000|%s\n' "$(day 4)" >> "$GL/scores/blockfall.txt"
fi
if [ -n "${SHOTS_STATS-}" ]; then
    {
        printf '# Blockfall lifetime stats: every run that counted on this machine since the day below (3.3.0)\n'
        printf 'since=2026-10-11\n'
        printf '[marathon]\nruns=14\nlines=596\npieces=1830\ncentis=185040\nclears=160|85|34|41\ntspin_clears=6|4|0|0\ntspins=12\nperfect=1\npowers=0\nholds=140\n'
        # SHOTS_STATS=marathon: the Marathon holds all three records, so
        # every All modes tile names it (the widest mode name, 3.6.1)
        if [ "$SHOTS_STATS" = marathon ]; then MPPS='2410|2026-10-18'; else MPPS='1870|2026-10-15'; fi
        printf 'best_pps=%s\nbest_combo=6|2026-10-18\nmost_quads=9|2026-10-12\n' "$MPPS"
        printf '[sprint]\nruns=9\nlines=328\npieces=820\ncentis=72030\nclears=40|60|24|24\ntspin_clears=0|0|0|0\ntspins=0\nperfect=0\npowers=0\nholds=61\n'
        printf 'best_pps=2340|2026-10-19\nbest_combo=4|2026-10-19\nmost_quads=7|2026-10-19\n'
        printf '[ultra]\nruns=5\nlines=200\npieces=520\ncentis=60000\nclears=70|30|10|10\ntspin_clears=2|1|0|0\ntspins=3\nperfect=0\npowers=0\nholds=22\n'
        printf 'best_pps=2050|2026-10-14\nbest_combo=3|2026-10-14\nmost_quads=4|2026-10-16\n'
        printf '[power]\nruns=5\nlines=128\npieces=400\ncentis=30000\nclears=50|20|6|5\ntspin_clears=0|0|0|0\ntspins=0\nperfect=0\npowers=31\nholds=18\n'
        printf 'best_pps=1500|2026-10-13\nbest_combo=2|2026-10-13\nmost_quads=2|2026-10-17\n'
    } > "$DATA/blockfall/stats.dat"
fi
if [ -n "${SHOTS_DAILYLOG-}" ]; then
    D=${SHOTS_DAILY-20261020}
    D1=$(date -d "$D -1 day" +%Y%m%d)
    D2=$(date -d "$D -2 day" +%Y%m%d)
    DD=$(date -d "$D" +%F)
    printf '# Blockfall Daily challenge: the days a daily was played on this machine (3.6.0): day|your best|runs\n%s|7100|2\n%s|8800|1\n%s|9800|3\n' \
        "$D2" "$D1" "$D" > "$DATA/blockfall/daily.dat"
    printf 'daily-%s|TRG|9800|%s\ndaily-%s|Arcade|6200|%s\n' "$D" "$DD" "$D" "$DD" >> "$GL/scores/blockfall.txt"
    printf 'daily-%s|Ghost|21400|%s\ndaily-%s|Moth|15000|%s\ndaily-%s|TRG|9800|%s\ndaily-%s|Pix|4000|%s\n' \
        "$D" "$DD" "$D" "$DD" "$D" "$DD" "$D" "$DD" >> "$GL/scores/global/blockfall.txt"
fi
if [ -z "${SHOTS_NOACH-}" ]; then printf 'r0|%s\ns1|%s\n' "$(day 9)" "$(day 7)" > "$GL/achievements/blockfall.txt"; fi
