#!/bin/bash
# fixtures.sh HOME: fills a fresh run.sh temp folder with everything the
# launcher reads, so the screens have data and nothing real is touched:
#   - the real catalog (assets/games.txt) with every path moved into HOME:
#     ~/bin/<game> is a do-nothing script ("installed"), ~/src/<game> a
#     source folder; icons are read from the repo;
#   - online.conf with sharing and update checks off (enabled=0 also means
#     the first-run question is already answered);
#   - prefs.ini, a player name, playtime, scores, world scores, trophies,
#     runs.log for Stats, and update.txt.
# SHOTS_UNINSTALLED="gemdive ..." leaves those machines uninstalled (default
# gemdive; use commas inside a script's '#! env' line); SHOTS_UPDATE=
# behind|current|unknown picks update.txt (default behind); SHOTS_FIRSTRUN=1
# leaves the share-scores question unanswered, so the launcher opens on it;
# SHOTS_CHECKS=1 turns update checks on (check_updates=1); SHOTS_EXTRA_GAMES=N
# adds N more machines ("Extra 1".., installed), for screens that scroll;
# SHOTS_NAME=X is the arcade name (default TRG; no spaces), e.g. one longer
# than new names may be.
# Your row (launcher.ini): SHOTS_ORDER=yours|recent|most|az, SHOTS_START_ON=
# last|first, SHOTS_LAST_PLAYED=slug, SHOTS_FAVS=slug,slug, SHOTS_HIDDEN=
# slug,slug. SHOTS_HALL_SONG=1 puts three songs in the Chipforge library and
# gives the hall one ("Ghost Arcade (launcher)"); SHOTS_GEMDELVE=1 adds the
# old Gemdelve playtime the launcher folds into Gemdive once.
# SHOTS_SPRINT_TIMES=1 adds Blockfall's Sprint 40 (time) and old Sprint boards.
# The daily desk (Phase 6): SHOTS_DAILIES=1 adds daily runs to runs.log (a
# three-day streak through today, a best of six before it) and today's daily
# boards, so two of the row's seven daily games are played today. Attract
# mode: SHOTS_ATTRACT=off|1min|2min (launcher.ini attract_mode).
set -eu
H=$1
case "$H" in */ghost-shots.*) ;; *) echo "fixtures.sh: $H is not a run.sh temp folder" >&2; exit 1 ;; esac
HERE=$(cd "$(dirname "$0")" && pwd)
LAUNCHER=$(cd "$HERE/../.." && pwd)
ARCADE=$(cd "$LAUNCHER/.." && pwd)
DATA=$H/.local/share
CONF=$H/.config
GL=$DATA/ghost-launcher
mkdir -p "$GL/scores/global" "$GL/achievements" "$CONF/ghost-launcher" "$CONF/ghost-arcade" "$H/bin" "$H/src"
ln -s "$LAUNCHER/assets" "$GL/assets"
UNINSTALLED=" $(printf '%s' "${SHOTS_UNINSTALLED-gemdive}" | tr ',' ' ') "
day() { date -d "-$1 days" +%F; }

{
    echo "# Ghost Launcher catalog for the screenshot harness (made from assets/games.txt)"
    grep -v '^#' "$LAUNCHER/assets/games.txt" | while IFS='|' read -r name exec icon desc src; do
        [ -n "$name" ] || continue
        slug=$(basename "$exec")
        realicon="$ARCADE/$slug/assets/icons/$slug.png"
        [ -f "$realicon" ] || realicon=""
        mkdir -p "$H/src/$slug"
        printf 'install:\n\t@true\n' > "$H/src/$slug/Makefile"
        case "$UNINSTALLED" in
            *" $slug "*) ;;
            *) printf '#!/bin/sh\nexit 0\n' > "$H/bin/$slug"; chmod +x "$H/bin/$slug" ;;
        esac
        echo "$name|~/bin/$slug|$realicon|$desc|~/src/$slug"
    done
    for ((i = 1; i <= ${SHOTS_EXTRA_GAMES-0}; i++)); do
        printf '#!/bin/sh\nexit 0\n' > "$H/bin/extra$i"
        chmod +x "$H/bin/extra$i"
        echo "Extra $i|~/bin/extra$i||A machine that is only here to fill the hall.|"
    done
} > "$CONF/ghost-launcher/games.txt"

checks=${SHOTS_CHECKS-0}
if [ "${SHOTS_FIRSTRUN-0}" = 1 ]; then
    printf '# no enabled= line yet: the first-run question\ncheck_updates=%s\n' "$checks" > "$CONF/ghost-launcher/online.conf"
else
    printf 'enabled=0\ncheck_updates=%s\n' "$checks" > "$CONF/ghost-launcher/online.conf"
fi
printf '# Ghost Arcade preferences, shared by every game\nmusic_volume=70\nscreen_shake=1\nreduced_flashing=0\nfullscreen=0\n' > "$CONF/ghost-arcade/prefs.ini"
printf '# Ghost Launcher profiles\n__DEFAULT__|%s\nBlockfall|Arcade\n' "${SHOTS_NAME-TRG}" > "$GL/profiles.txt"
printf '# Ghost Launcher playtime: name|seconds\n__TOTAL__|9120.0\nBlockfall|2296.0\nBrickburst|1361.0\nChipforge|1120.0\nCoilrush|610.0\nSkyraid|425.0\nLanehop|300.0\nGirderclimb|92.0\n' > "$GL/playtime.txt"

# Boards for the machine pages (Phase 5): each file's best is what the hall's
# card shows (Blockfall: Arcade 12840 here, Ghost 20480 in the world;
# Brickburst: TRG 3625 both), with more rows under it, and today's daily.
daily() { printf 'daily-%s' "$(date -d "-$1 days" +%Y%m%d)"; }
printf 'classic|Arcade|12840|%s\nclassic|Arcade|11200|%s\nclassic|Arcade|8000|%s\nclassic|TRG|5200|%s\nmarathon|TRG|9100|%s\n' \
    "$(day 2)" "$(day 4)" "$(day 6)" "$(day 8)" "$(day 5)" > "$GL/scores/blockfall.txt"
printf 'normal|TRG|3625|%s\nnormal|TRG|2950|%s\n%s|TRG|1800|%s\n' "$(day 1)" "$(day 3)" "$(daily 0)" "$(day 0)" > "$GL/scores/brickburst.txt"
printf 'classic|TRG|410|%s\n' "$(day 3)" > "$GL/scores/coilrush.txt"
printf 'normal|TRG|2210|%s\n' "$(day 6)" > "$GL/scores/skyraid.txt"
{
    printf 'classic|Ghost|20480|%s\nclassic|Moth|18200|%s\nclassic|Arcade|12840|%s\nclassic|Pix|11950|%s\n' "$(day 1)" "$(day 3)" "$(day 2)" "$(day 4)"
    printf 'classic|Zed|9800|%s\nclassic|Nova|7600|%s\nclassic|Kit|6100|%s\nclassic|Rune|5000|%s\n' "$(day 5)" "$(day 6)" "$(day 7)" "$(day 9)"
    printf 'marathon|Ghost|15000|%s\nmarathon|TRG|9100|%s\n' "$(day 3)" "$(day 5)"
} > "$GL/scores/global/blockfall.txt"
printf 'normal|TRG|3625|%s\nnormal|Ghost|3100|%s\nnormal|Moth|2400|%s\n%s|Ghost|2600|%s\n%s|TRG|1800|%s\n%s|Moth|2200|%s\n' \
    "$(day 1)" "$(day 2)" "$(day 4)" "$(daily 0)" "$(day 0)" "$(daily 0)" "$(day 0)" "$(daily 1)" "$(day 1)" > "$GL/scores/global/brickburst.txt"
# SHOTS_SPRINT_TIMES=1: Blockfall's time-ranked Sprint 40 boards (a row stores
# 10000000 minus the time in hundredths: 9992760 is 1:12.40) beside its old
# points-ranked Sprint board.
if [ -n "${SHOTS_SPRINT_TIMES-}" ]; then
    printf 'sprint|TRG|7620|%s\nsprint40|TRG|9992760|%s\n' "$(day 20)" "$(day 1)" >> "$GL/scores/blockfall.txt"
    printf 'sprint40|Ghost|9994179|%s\nsprint40|TRG|9992760|%s\n' "$(day 2)" "$(day 1)" >> "$GL/scores/global/blockfall.txt"
fi
touch -d '-10 minutes' "$GL/scores/global" # the boards came down a while ago (Settings > Online's last sync)

printf 'r0|%s\ns1|%s\nl3|%s\n' "$(day 9)" "$(day 7)" "$(day 2)" > "$GL/achievements/blockfall.txt"
printf 'r0|%s\ns1|%s\n' "$(day 4)" "$(day 1)" > "$GL/achievements/brickburst.txt"
printf 'r0|%s\n' "$(day 3)" > "$GL/achievements/coilrush.txt"

runs() { # runs SLUG SCORE DAYSAGO...
    local slug=$1 score=$2
    shift 2
    mkdir -p "$DATA/$slug"
    for d in "$@"; do printf '%s 20:15:00|classic|%s|3\n' "$(day "$d")" "$score"; done >> "$DATA/$slug/runs.log"
}
runs blockfall 4200 0 0 1 2 2 3 5 8 12
runs brickburst 1800 0 1 1 4 6
runs coilrush 300 3 9
runs skyraid 2210 6 13
runs lanehop 900 2

if [ "${SHOTS_GEMDELVE-0}" = 1 ]; then printf 'Gemdelve|334.2\nGemdive|84.1\n' >> "$GL/playtime.txt"; fi

dailyruns() { # dailyruns SLUG SCORE DAYSAGO...: daily runs on those days' boards
    local slug=$1 score=$2
    shift 2
    mkdir -p "$DATA/$slug"
    for d in "$@"; do printf '%s 19:30:00|%s|%s|2\n' "$(day "$d")" "$(daily "$d")" "$score"; done >> "$DATA/$slug/runs.log"
}
if [ "${SHOTS_DAILIES-0}" = 1 ]; then
    dailyruns brickburst 1800 0 1 2 5 6 7 8 9 10
    dailyruns skyraid 2900 0 0 12
    printf '%s|TRG|2900|%s\n' "$(daily 0)" "$(day 0)" >> "$GL/scores/skyraid.txt"
    printf '%s|Ghost|5100|%s\n%s|Moth|4400|%s\n%s|TRG|2900|%s\n' "$(daily 0)" "$(day 0)" "$(daily 0)" "$(day 0)" "$(daily 0)" "$(day 0)" \
        > "$GL/scores/global/skyraid.txt"
    printf '%s|Pix|900|%s\n' "$(daily 0)" "$(day 0)" > "$GL/scores/global/lanehop.txt"
fi

ini=$CONF/ghost-launcher/launcher.ini
{
    if [ -n "${SHOTS_ORDER-}" ]; then echo "row_order=$SHOTS_ORDER"; fi
    if [ -n "${SHOTS_START_ON-}" ]; then echo "start_on=$SHOTS_START_ON"; fi
    if [ -n "${SHOTS_LAST_PLAYED-}" ]; then echo "last_played=$SHOTS_LAST_PLAYED"; fi
    for f in $(printf '%s' "${SHOTS_FAVS-}" | tr ',' ' '); do echo "fav.$f=1"; done
    for h in $(printf '%s' "${SHOTS_HIDDEN-}" | tr ',' ' '); do echo "hide.$h=1"; done
    if [ -n "${SHOTS_ATTRACT-}" ]; then echo "attract_mode=$SHOTS_ATTRACT"; fi
} > "$ini.new"
if [ -s "$ini.new" ]; then mv "$ini.new" "$ini"; else rm -f "$ini.new"; fi

if [ "${SHOTS_HALL_SONG-0}" = 1 ]; then
    lib=$DATA/ghost-arcade/music
    mkdir -p "$lib"
    song() { printf 'GSONG 1\nname %s\nbpm 96\norder 0\npattern 0\n00 A-1 A-3 C-3 K..\n' "$2" > "$lib/$1"; }
    song ghost-arcade-launcher.gsong "Ghost Arcade (launcher)"
    song lobby.gsong "Lobby"
    song night-drive.gsong "Night drive"
    printf '# Which Chipforge song each Ghost Arcade game plays: slug=file.gsong\nghostarcade=ghost-arcade-launcher.gsong\n' > "$CONF/ghost-arcade/music.ini"
fi

current=0123456789abcdef0123456789abcdef01234567
latest=89abcdef0123456789abcdef0123456789abcdef
checked=$(( $(date +%s) - 7200 ))
case "${SHOTS_UPDATE-behind}" in
    behind) printf 'status=behind\nbehind=1\ncurrent=%s\nlatest=%s\ntitle=Launcher: one input reader and a safer main loop\nchecked=%s\n' "$current" "$latest" "$checked" ;;
    current) printf 'status=current\nbehind=0\ncurrent=%s\nlatest=%s\ntitle=Launcher: one input reader and a safer main loop\nchecked=%s\n' "$current" "$current" "$checked" ;;
    *) printf 'status=unknown\nbehind=0\ncurrent=\nlatest=\ntitle=\nchecked=0\n' ;;
esac > "$GL/update.txt"
