/* Headless tests for Phase 6, the owner's extras: the Daily challenge desk
 * (src/desk.c: day arithmetic across month and year ends, streaks with
 * gaps, the featured pick, reading runs.log and today's boards, the desk's
 * rows and keys), attract mode (src/attract.c: the idle timer, the tour and
 * the wake rules, the waking press swallowed) and Chipforge as a jukebox
 * (src/jukebox.c: the note window taken from a Song; Chipforge's Songs tab
 * picking the hall's song). test_main.c's main() points XDG_DATA_HOME and
 * XDG_CONFIG_HOME at a temp folder first. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "../src/desk.h"
#include "../src/attract.h"
#include "../src/jukebox.h"
#include "../src/hallmusic.h"
#include "../src/gamepage.h"
#include "../src/screens.h"
#include "../src/hall.h"
#include "../src/settings.h"
#include "../src/keymap.h"
#include "daily.h"
#include "musicsynth.h"

/* ------------------------------------------------------------- helpers */

static Input Blank(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.lastDevice = in.hintDevice = DEV_KEYBOARD;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input Key(InputKey k) {
    Input in = Blank();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    in.any = true;
    return in;
}

static Input ShiftKey(InputKey k) {
    Input in = Key(k);
    in.shift = true;
    return in;
}

static Input Wheel(float x, float y, float notches) {
    Input in = Blank();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = true;
    in.wheel = notches;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static void WriteUnder(const char *env, const char *rel, const char *text) {
    char path[800], dir[800];
    snprintf(path, sizeof(path), "%s/%s", getenv(env), rel);
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = dir + strlen(getenv(env)) + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(dir, 0755);
        *p = '/';
    }
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "extras: fixture write"); return; }
    fputs(text, f);
    fclose(f);
}

static void RemoveUnder(const char *env, const char *rel) {
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", getenv(env), rel);
    remove(path);
}

static int Day(int y, int m, int d) { return Desk_DayNumber(y, m, d); }

static void Played(DeskDays *days, const int *list, int n) {
    Desk_DaysClear(days);
    for (int i = 0; i < n; i++) Desk_DaysAdd(days, list[i]);
}

/* ---------------------------------------------------------------- days */

static void test_desk_days(void) {
    CHECK(Day(1970, 1, 1) == 0 && Day(2026, 10, 14) == 20740 && Day(2000, 2, 29) == 11016 && Day(2028, 2, 29) == 21243,
          "desk days: day numbers match the calendar (1970-01-01 is day 0)");
    CHECK(Day(2026, 12, 31) + 1 == Day(2027, 1, 1) && Day(2026, 1, 31) + 1 == Day(2026, 2, 1) && Day(2026, 4, 30) + 1 == Day(2026, 5, 1),
          "desk days: month and year ends are one day apart");
    CHECK(Day(2026, 2, 28) + 1 == Day(2026, 3, 1) && Day(2028, 2, 28) + 2 == Day(2028, 3, 1) && Day(2100, 2, 28) + 1 == Day(2100, 3, 1) &&
              Day(2000, 2, 28) + 2 == Day(2000, 3, 1),
          "desk days: leap years, and the century rule (2100 is not one, 2000 is)");
    bool round = true;
    int prev = Day(1999, 12, 31);
    for (int n = Day(2000, 1, 1); n <= Day(2101, 1, 1); n++) {
        int y, m, d;
        Desk_Civil(n, &y, &m, &d);
        if (Day(y, m, d) != n || n != prev + 1) round = false;
        prev = n;
    }
    CHECK(round, "desk days: every day from 2000 to 2101 goes there and back");

    int n = -1;
    CHECK(Desk_ParseMode("daily-20261014", &n) && n == Day(2026, 10, 14), "desk modes: daily-YYYYMMDD is its day");
    CHECK(Desk_ParseMode("daily-20280229", &n) && !Desk_ParseMode("daily-20260229", &n) && !Desk_ParseMode("daily-20260431", &n) &&
              !Desk_ParseMode("daily-21000229", &n) && Desk_ParseMode("daily-20000229", &n),
          "desk modes: only real dates (no 29 Feb 2026 or 31 April)");
    CHECK(!Desk_ParseMode("classic", &n) && !Desk_ParseMode("daily-2026101", &n) && !Desk_ParseMode("daily-2026101x", &n) &&
              !Desk_ParseMode("daily-20261300", &n) && !Desk_ParseMode(NULL, &n),
          "desk modes: anything else is no daily");
    char mode[24], words[48];
    Desk_ModeOf(Day(2027, 1, 1), mode, sizeof(mode));
    CHECK(strcmp(mode, "daily-20270101") == 0, "desk modes: a day's board name");
    Desk_DateWords(Day(2026, 10, 14), words, sizeof(words));
    CHECK(strcmp(words, "Wednesday 14 October") == 0 && Desk_Weekday(0) == 3 && Desk_Weekday(Day(2027, 1, 1)) == 4 && Desk_Weekday(-1) == 2,
          "desk days: weekdays (14 Oct 2026 a Wednesday, 1 Jan 1970 a Thursday)");
}

static void test_desk_calendar(void) {
    static DeskDays days;
    int list[] = {Day(2026, 10, 3), Day(2026, 10, 1), Day(2026, 10, 3), Day(2026, 9, 30)};
    Played(&days, list, 4);
    CHECK(days.count == 3 && days.day[0] == Day(2026, 9, 30) && days.day[2] == Day(2026, 10, 3), "desk calendar: sorted, each day once");
    CHECK(Desk_DaysHas(&days, Day(2026, 10, 1)) && !Desk_DaysHas(&days, Day(2026, 10, 2)), "desk calendar: which days were played");
    Desk_DaysClear(&days);
    for (int i = 0; i < DESK_MAX_DAYS + 10; i++) Desk_DaysAdd(&days, 1000 + i);
    CHECK(days.count == DESK_MAX_DAYS && days.day[0] == 1010 && days.day[DESK_MAX_DAYS - 1] == 1000 + DESK_MAX_DAYS + 9,
          "desk calendar: a full one forgets its oldest days");
    Desk_DaysAdd(&days, 5);
    CHECK(days.day[0] == 1010 && !Desk_DaysHas(&days, 5), "desk calendar: a day older than all kept is not taken");

    Desk_DaysClear(&days);
    int today = Day(2026, 10, 14);
    const char *log = "2026-10-14 09:00:00|daily-20261014|1800|3\n"
                      "2026-10-14 09:10:00|daily-20261014|900|1\r\n"
                      "2026-10-14 00:05:00|daily-20261013|500|1\n" /* begun before midnight: its board's day counts */
                      "2026-10-12 20:00:00|classic|4000|5\n"
                      "2026-10-15 20:00:00|daily-20261015|10|1\n" /* a clock ahead once: never a day to come */
                      "junk line\n|daily-20261001|\n2026-10-10 10:00:00|daily-20261099|1|1\n"
                      "2026-10-09 10:00:00|daily-20261009";
    int runs = Desk_DaysFromRuns(&days, log, today);
    CHECK(runs == 2 && days.count == 4 && Desk_DaysHas(&days, today) && Desk_DaysHas(&days, today - 1) && Desk_DaysHas(&days, Day(2026, 10, 1)) &&
              Desk_DaysHas(&days, Day(2026, 10, 9)) && !Desk_DaysHas(&days, today + 1),
          "desk calendar: runs.log's daily runs by their board's day, up to today; today's counted");
}

static void test_desk_streak(void) {
    static DeskDays days;
    DeskStreak s = Desk_Streak(&days, Day(2026, 10, 14));
    Desk_DaysClear(&days);
    s = Desk_Streak(&days, Day(2026, 10, 14));
    CHECK(s.current == 0 && s.best == 0 && !s.today, "desk streak: nothing played, nothing to count");

    int month[] = {Day(2026, 1, 30), Day(2026, 1, 31), Day(2026, 2, 1)};
    Played(&days, month, 3);
    s = Desk_Streak(&days, Day(2026, 2, 1));
    CHECK(s.current == 3 && s.best == 3 && s.today, "desk streak: across a month's end");

    int year[] = {Day(2026, 12, 30), Day(2026, 12, 31), Day(2027, 1, 1)};
    Played(&days, year, 3);
    s = Desk_Streak(&days, Day(2027, 1, 2));
    CHECK(s.current == 3 && s.best == 3 && !s.today, "desk streak: across a year's end, still alive the day after until it is over");
    s = Desk_Streak(&days, Day(2027, 1, 3));
    CHECK(s.current == 0 && s.best == 3, "desk streak: a whole day missed ends it; the best stays");

    int leap[] = {Day(2028, 2, 28), Day(2028, 2, 29), Day(2028, 3, 1)};
    Played(&days, leap, 3);
    s = Desk_Streak(&days, Day(2028, 3, 1));
    int noLeap[] = {Day(2026, 2, 28), Day(2026, 3, 1)};
    CHECK(s.current == 3, "desk streak: through 29 February");
    Played(&days, noLeap, 2);
    s = Desk_Streak(&days, Day(2026, 3, 1));
    CHECK(s.current == 2, "desk streak: 28 February to 1 March in a year with no leap day");

    int gaps[] = {Day(2026, 10, 1), Day(2026, 10, 2), Day(2026, 10, 3), Day(2026, 10, 4), Day(2026, 10, 5),
                  Day(2026, 10, 7), Day(2026, 10, 8), Day(2026, 10, 20)};
    Played(&days, gaps, 8);
    s = Desk_Streak(&days, Day(2026, 10, 8));
    CHECK(s.current == 2 && s.best == 5 && s.today, "desk streak: a gap starts it again; the best run is kept; a day to come is not counted");
    s = Desk_Streak(&days, Day(2026, 10, 9));
    CHECK(s.current == 2 && !s.today, "desk streak: today not played yet keeps yesterday's");

    bool dots[DESK_DOTS];
    Desk_Dots(&days, Day(2026, 10, 14), dots);
    CHECK(dots[DESK_DOTS - 1] == false && dots[DESK_DOTS - 7] && dots[DESK_DOTS - 8] && !dots[DESK_DOTS - 9] && dots[DESK_DOTS - 10] &&
              dots[0] && dots[DESK_DOTS - 14],
          "desk dots: fourteen days, today last (1 Oct is the first of them on the 14th)");
}

static void test_desk_featured(void) {
    int today = Day(2026, 10, 14);
    bool same = true, inRange = true, never2 = true, weekly = true;
    for (int d = today - 400; d < today + 1000; d++) {
        int f = Desk_Featured(d);
        if (f != Desk_Featured(d)) same = false;
        if (f < 0 || f >= Desk_GameCount()) inRange = false;
        else if (f == Desk_Featured(d - 1)) never2 = false;
    }
    for (int week = -3; week < 200; week++) { /* weeks begin on day 0, a Thursday */
        int seen = 0;
        for (int d = week * 7; d < week * 7 + 7; d++) seen |= 1 << Desk_Featured(d);
        if (seen != (1 << Desk_GameCount()) - 1) weekly = false;
    }
    CHECK(same && inRange, "featured: the same game all day, always a daily game");
    CHECK(weekly, "featured: every daily game has its day each week");
    CHECK(never2, "featured: never the same game two days running, across weeks too");
    int a = Desk_Featured(today), b = Desk_Featured(today + 7), c = Desk_Featured(today + 14);
    CHECK(!(a == b && b == c), "featured: weeks are not all alike");
    CHECK(Desk_IsDaily("skyraid") && Desk_IsDaily("girderclimb") && !Desk_IsDaily("blockfall") && !Desk_IsDaily("chipforge") &&
              !Desk_IsDaily(NULL) && Desk_GameCount() == 7 && strcmp(Desk_GameSlug(99), "") == 0,
          "desk games: the seven with a daily");
}

/* ----------------------------------------------------------------- desk */

static Manifest sCat;

static void AddGame(const char *name, const char *slug) {
    GameEntry *e = &sCat.games[sCat.count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->exec, sizeof(e->exec), "~/bin/%s", slug);
}

static void DeskCatalog(void) {
    memset(&sCat, 0, sizeof(sCat));
    AddGame("Blockfall", "blockfall");
    AddGame("Brickburst", "brickburst");
    AddGame("Skyraid", "skyraid");
    AddGame("Rockdrift", "rockdrift");
    AddGame("Lanehop", "lanehop");
    AddGame("Crawlshot", "crawlshot");
    AddGame("Moondrop", "moondrop");
    AddGame("Girderclimb", "girderclimb");
    AddGame("Chipforge", "chipforge");
}

static void test_desk_load(void) {
    DeskCatalog();
    time_t now = time(NULL);
    int today = Desk_Today(now);
    char mode[24], yday[24], line[512], date[16];
    Desk_ModeOf(today, mode, sizeof(mode));
    Desk_ModeOf(today - 1, yday, sizeof(yday));
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    /* Brickburst today (runs.log) and the two days before (Skyraid);
     * Lanehop only on today's local board; Skyraid's world board has you 2nd. */
    snprintf(line, sizeof(line), "%s 10:00:00|%s|1800|3\n%s 10:05:00|normal|2950|3\n", date, mode, date);
    WriteUnder("XDG_DATA_HOME", "brickburst/runs.log", line);
    char d2[24];
    Desk_ModeOf(today - 2, d2, sizeof(d2));
    snprintf(line, sizeof(line), "2026-01-01 10:00:00|%s|700|1\n2026-01-01 10:00:00|%s|650|1\n", yday, d2);
    WriteUnder("XDG_DATA_HOME", "skyraid/runs.log", line);
    snprintf(line, sizeof(line), "%s|TRG|1800|%s\nnormal|TRG|2950|%s\n", mode, date, date);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/brickburst.txt", line);
    snprintf(line, sizeof(line), "%s|Ghost|2600|%s\n%s|trg|1800|%s\n%s|Moth|2200|%s\n", mode, date, mode, date, yday, date);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/brickburst.txt", line);
    snprintf(line, sizeof(line), "%s|TRG|400|%s\n", mode, date);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/lanehop.txt", line);

    Profiles prof;
    memset(&prof, 0, sizeof(prof));
    Profiles_SetUsername(&prof, PROFILE_DEFAULT_KEY, "TRG");
    bool installed[MANIFEST_MAX_GAMES];
    for (int i = 0; i < MANIFEST_MAX_GAMES; i++) installed[i] = true;
    installed[6] = false; /* Moondrop */
    /* The row: Skyraid first, Rockdrift hidden (off the row). */
    Lineup row;
    memset(&row, 0, sizeof(row));
    int order[] = {2, 0, 1, 4, 5, 6, 7, 8};
    for (int p = 0; p < 8; p++) row.index[row.count++] = order[p];
    static Desk desk;
    DeskSource src = {&sCat, &row, installed, &prof, now};
    Desk_Load(&desk, &src);
    CHECK(desk.today == today && strcmp(desk.mode, mode) == 0 && desk.count == 6, "desk: a row for each daily game on your row (not Blockfall, Chipforge or a hidden one)");
    CHECK(strcmp(desk.rows[0].name, "Skyraid") == 0 && strcmp(desk.rows[1].name, "Brickburst") == 0 && strcmp(desk.rows[2].name, "Lanehop") == 0 &&
              desk.rows[0].index == 2,
          "desk: in your row's order");
    const DeskRow *bb = &desk.rows[1], *sky = &desk.rows[0], *lane = &desk.rows[2], *moon = &desk.rows[4];
    CHECK(bb->played && bb->runsToday == 1 && bb->hasBest && bb->best == 1800 && bb->hasWorld && bb->world == 2600 &&
              strcmp(bb->worldName, "Ghost") == 0 && bb->rank == 2,
          "desk: played today, your best, the world's #1 and your place (names matched without case)");
    CHECK(!sky->played && !sky->hasBest && !sky->hasWorld && sky->rank == 0, "desk: not played today: nothing to show");
    CHECK(lane->played && lane->runsToday == 0 && lane->best == 400, "desk: a score on today's board counts as played without a runs.log line");
    CHECK(!moon->installed && strcmp(moon->name, "Moondrop") == 0, "desk: a machine not installed is on the desk, faint");
    CHECK(desk.played == 2 && desk.streak.today && desk.streak.current == 3 && desk.streak.best == 3, "desk: today's count and the streak over every game");
    CHECK(desk.dots[DESK_DOTS - 1] && desk.dots[DESK_DOTS - 2] && desk.dots[DESK_DOTS - 3] && !desk.dots[DESK_DOTS - 4], "desk: the last fourteen days");
    int featured = -1;
    for (int r = 0; r < desk.count; r++) if (desk.rows[r].featured) featured = r;
    CHECK(desk.featured == Desk_Featured(today) &&
              (featured < 0 ? true : strcmp(desk.rows[featured].slug, Desk_GameSlug(desk.featured)) == 0),
          "desk: the featured row is today's pick");
    char words[48];
    Desk_BoardLine(&desk, words, sizeof(words));
    CHECK(strcmp(words, "2 of 6 today") == 0, "desk: the chalkboard's line");

    /* The pick never depends on the row's order. */
    Lineup other;
    memset(&other, 0, sizeof(other));
    for (int p = 7; p >= 0; p--) other.index[other.count++] = order[p];
    static Desk again;
    src.lineup = &other;
    Desk_Load(&again, &src);
    CHECK(again.featured == desk.featured && again.count == desk.count && strcmp(again.rows[0].name, "Chipforge") != 0,
          "desk: the same featured game whatever your row");

    CHECK(Desk_StartRow(&desk, 1) == 1, "desk start: on the machine it was opened for");
    CHECK(Desk_StartRow(&desk, 6) != 4 && Desk_StartRow(&desk, 0) >= 0, "desk start: never on a machine that is not installed");
    static Desk none;
    memset(&none, 0, sizeof(none));
    Desk_BoardLine(&none, words, sizeof(words));
    CHECK(strcmp(words, "") == 0 && Desk_StartRow(&none, 0) == -1, "desk: no daily game on the row, no chalkboard");

    RemoveUnder("XDG_DATA_HOME", "brickburst/runs.log");
    RemoveUnder("XDG_DATA_HOME", "skyraid/runs.log");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/brickburst.txt");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/brickburst.txt");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/lanehop.txt");
}

static void FakeDesk(Desk *d, int rows, int notInstalled) {
    memset(d, 0, sizeof(*d));
    d->count = rows;
    for (int r = 0; r < rows; r++) {
        snprintf(d->rows[r].name, sizeof(d->rows[r].name), "Game %d", r);
        d->rows[r].index = r;
        d->rows[r].installed = r != notInstalled;
    }
}

static void test_desk_screen(void) {
    static Desk d;
    FakeDesk(&d, 7, 2);
    KitScroll sc = {0, 0.0f};
    KitList l;
    Desk_Layout(&d, &sc, &l);
    int rows = 0;
    bool inside = true;
    for (int i = 0; i < l.count; i++) {
        if (Desk_RowOf(l.items[i].id) < 0) continue;
        rows++;
        KitRect r = l.items[i].r;
        if (r.x < DESK_X || r.x + r.w > DESK_RIGHT || r.y + r.h > DESK_NOTE_Y) inside = false;
    }
    CHECK(rows == 7 && inside && l.nav == 0 && l.items[l.count - 1].id == WID_CLOSE && Kit_Get(&l, WID_BACK),
          "desk layout: a row each above the note, Back and the X on top; the desk moves its own keys");
    CHECK(Kit_Get(&l, WID_DESK_ROW + 2)->flags & KIT_DISABLED, "desk layout: not installed is faint");
    CHECK(Kit_FirstFocus(&l) == WID_DESK_ROW + 0 && Desk_RowOf(WID_DESK_ROW + 3) == 3 && Desk_RowOf(WID_CLOSE) == -1, "desk layout: ids");

    KitState k;
    Kit_Init(&k);
    k.focus = WID_DESK_ROW + 1;
    Input down = Key(IK_DOWN), up = Key(IK_UP);
    CHECK(Desk_Navigate(&d, &sc, &k, &down, true) && k.focus == WID_DESK_ROW + 3, "desk keys: Down skips a faint row");
    k.focus = WID_DESK_ROW + 6;
    CHECK(Desk_Navigate(&d, &sc, &k, &down, true) && k.focus == WID_DESK_ROW + 0, "desk keys: round from the last to the first");
    CHECK(Desk_Navigate(&d, &sc, &k, &up, true) && k.focus == WID_DESK_ROW + 6, "desk keys: Up from the first to the last");
    Input none = Blank();
    CHECK(!Desk_Navigate(&d, &sc, &k, &none, true), "desk keys: nothing pressed, nothing moves");

    static Desk many;
    FakeDesk(&many, 11, -1);
    k.focus = WID_DESK_ROW + 6;
    sc.top = 0;
    CHECK(Desk_Navigate(&many, &sc, &k, &down, true) && k.focus == WID_DESK_ROW + 7 && sc.top == 1, "desk keys: past the last row shown, the list scrolls");
    Desk_Layout(&many, &sc, &l);
    CHECK(Kit_Get(&l, WID_DESK_ROW + 7) && !Kit_Get(&l, WID_DESK_ROW + 0), "desk layout: only the rows in sight");
    k.focus = WID_DESK_ROW + 1;
    Input wheel = Wheel(400, 300, -3.0f);
    CHECK(Desk_Navigate(&many, &sc, &k, &wheel, true) && sc.top == 4 && k.focus == WID_DESK_ROW + 4, "desk wheel: scrolls, keys come along into sight");
    CHECK(!Desk_Navigate(&many, &sc, &k, &wheel, false), "desk wheel: not when a control has it");
    k.focus = WID_DESK_ROW + 10;
    sc.top = 4;
    CHECK(Desk_Navigate(&many, &sc, &k, &down, true) && k.focus == WID_DESK_ROW + 0 && sc.top == 0, "desk keys: round to the top scrolls back");

    char help[160], hint[160];
    Desk_Help(&d, 2, help, sizeof(help));
    CHECK(strstr(help, "Not installed") != NULL, "desk help: a faint row says why");
    d.rows[1].played = true;
    Desk_Help(&d, 1, help, sizeof(help));
    CHECK(strstr(help, "another go") != NULL && Kit_TextWidth(help, 8) <= 800, "desk help: Enter's sentence fits the line");
    Desk_Hint(DEV_KEYBOARD, hint, sizeof(hint));
    CHECK(strcmp(hint, "Up/Down choose   Enter play its daily   Esc back") == 0, "desk hint: keys");
    Desk_Hint(DEV_PAD, hint, sizeof(hint));
    CHECK(strcmp(hint, "D-pad choose   A play its daily   B back") == 0, "desk hint: the pad");
}

/* The desk's keys, the menu's item, the chalkboard. */
static void test_desk_ways_in(void) {
    Input d = Key(IK_D), sd = ShiftKey(IK_D);
    CHECK(Keymap_Pressed(KA_HALL_DAILY, &d) && !Keymap_Pressed(KA_OPEN_DAILY, &d), "desk keys: D alone still plays the lit machine's daily");
    CHECK(Keymap_Pressed(KA_OPEN_DAILY, &sd) && !Keymap_Pressed(KA_HALL_DAILY, &sd) && Screens_Shortcut(&sd) == KA_OPEN_DAILY,
          "desk keys: Shift+D opens the desk, from the hall and the menu");
    CHECK(Keymap_OnCard(KA_OPEN_DAILY), "desk keys: on a reference card");
    CHECK(strcmp(Screens_MenuLabel(AM_DAILY), "Daily challenge") == 0 && Screens_MenuItemFor(WID_MENU_ITEM + AM_DAILY) == AM_DAILY,
          "desk menu: an item of its own");
    KitList l;
    Screens_Menu(&l, false, DEV_KEYBOARD);
    const KitItem *row = Kit_Get(&l, WID_MENU_ITEM + AM_DAILY), *leave = Kit_Get(&l, WID_MENU_ITEM + AM_LEAVE);
    CHECK(row && 20 + Kit_TextWidth(row->label, 16) + 12 <= row->r.w && leave->r.y + leave->r.h <= KIT_SCREEN_H - 56,
          "desk menu: its words fit, and Leave stays above the menu's hint line");

    static Manifest m;
    memset(&m, 0, sizeof(m));
    m.count = 3;
    for (int i = 0; i < 3; i++) snprintf(m.games[i].name, MANIFEST_FIELD_LEN, "Game %d", i);
    HallView v = {.manifest = &m, .place = 1, .scroll = 1.0f, .device = DEV_KEYBOARD, .play = HP_PLAY, .dailyReason = "", .arcadeName = "TRG"};
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CHALKBOARD), "chalkboard: none before the desk is read");
    v.deskLine = ""; /* Desk_BoardLine with no daily game on the row */
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CHALKBOARD), "chalkboard: none without a daily game on the row");
    v.deskLine = "2 of 7 today";
    Screens_Hall(&l, &v);
    const KitItem *chalk = Kit_Get(&l, WID_CHALKBOARD);
    bool clear = true;
    for (int i = 0; chalk && i < l.count; i++) {
        const KitItem *it = &l.items[i];
        if (it->id == WID_CHALKBOARD) continue;
        if (it->r.x < chalk->r.x + chalk->r.w && chalk->r.x < it->r.x + it->r.w && it->r.y < chalk->r.y + chalk->r.h && chalk->r.y < it->r.y + it->r.h) clear = false;
    }
    CHECK(chalk && (chalk->flags & KIT_NOFOCUS) && chalk == &l.items[0] && clear, "chalkboard: on the wall, behind everything, clear of the machines at rest");
    bool slidingClear = true;
    for (float s = 0.0f; s <= 2.0f; s += 0.05f) {
        for (int p = 0; p < 3; p++) {
            KitRect r = Hall_CabinetRect(s, p);
            if (r.x < CHALK_X + CHALK_W && CHALK_X < r.x + r.w && r.y < CHALK_Y + CHALK_H) slidingClear = false;
        }
    }
    CHECK(slidingClear, "chalkboard: no machine reaches it at any point of a slide");
    KitEvent ev = {KIT_EV_PRESS, WID_CHALKBOARD, 0};
    HallInput hin = {0};
    Input blank = Blank();
    CHECK(Hall_Read(&hin, &blank, &ev, 1, 3, false).verb == HV_DESK, "chalkboard: a click opens the desk");
    v.coinInFlight = true;
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CHALKBOARD), "chalkboard: nothing but the X takes a click while the coin drops");

    v.coinInFlight = false;
    v.cardSlide = 60.0f;
    Screens_Hall(&l, &v);
    CHECK(Kit_Get(&l, WID_CARD)->r.y == HALL_CARD_Y + 60 && Kit_Get(&l, WID_CARD_FAVOURITE)->r.y == HALL_FAV_Y + 60,
          "attract: the card and its Favourite slide away together");
    v.cardSlide = (float)(KIT_SCREEN_H - HALL_CARD_Y);
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CARD) && !Kit_Get(&l, WID_CARD_FAVOURITE) && Kit_Get(&l, WID_STRIP_PLAY), "attract: once it is gone, so are its buttons");
}

/* -------------------------------------------------------------- attract */

static AttractIn Hall(int setting, int place, int count) {
    AttractIn in = {setting, true, false, false, false, place, count, NULL};
    return in;
}

/* Frames of 1/60 s for so many seconds; the last event other than NONE. */
static AttractEvent Run(Attract *a, const AttractIn *in, float seconds) {
    AttractEvent last = ATTRACT_NONE;
    for (int i = 0; i < (int)(seconds * 60.0f + 0.5f); i++) {
        AttractEvent e = Attract_Step(a, in, 1.0f / 60.0f);
        if (e != ATTRACT_NONE) last = e;
    }
    return last;
}

static void test_attract_timer(void) {
    Attract a;
    Attract_Init(&a);
    CHECK(!Attract_Settling(&a), "attract: nothing to settle from at the start");
    AttractIn in = Hall(ATTRACT_1MIN, 4, 12);
    CHECK(Run(&a, &in, 59.5f) == ATTRACT_NONE && !a.on, "attract: not before a minute");
    CHECK(Run(&a, &in, 0.6f) == ATTRACT_BEGAN && a.on && a.place == 4 && Attract_ShownPlace(&a, 4) == 4, "attract: after a minute, on your machine");
    Attract_Init(&a);
    in.setting = ATTRACT_2MIN;
    CHECK(Run(&a, &in, 100.0f) == ATTRACT_NONE && Run(&a, &in, 20.1f) == ATTRACT_BEGAN, "attract: two minutes when set so");
    Attract_Init(&a);
    in.setting = ATTRACT_OFF;
    CHECK(Run(&a, &in, 600.0f) == ATTRACT_NONE && !a.on, "attract: never when off");

    in.setting = ATTRACT_1MIN;
    Attract_Init(&a);
    Run(&a, &in, 50.0f);
    AttractIn touch = in;
    touch.input = true;
    CHECK(Attract_Step(&a, &touch, 1.0f / 60.0f) == ATTRACT_NONE && a.idle == 0.0f, "attract: any input starts the minute again, and is not swallowed");
    CHECK(Run(&a, &in, 59.0f) == ATTRACT_NONE, "attract: ...a whole minute from that press");
    AttractIn held = in;
    held.held = true;
    Run(&a, &held, 120.0f);
    CHECK(!a.on && a.idle == 0.0f, "attract: a key held down is someone there");
    AttractIn away = in;
    away.onHall = false;
    Run(&a, &away, 120.0f);
    CHECK(!a.on, "attract: only on the hall (not under the menu, a box, the coin or another screen)");
    AttractIn game = in;
    game.gameRunning = true;
    Run(&a, &game, 120.0f);
    CHECK(!a.on, "attract: never while a game the launcher started runs (the quiet hall wins)");
    AttractIn empty = in;
    empty.count = 0;
    Run(&a, &empty, 120.0f);
    CHECK(!a.on, "attract: never on an empty row");

    /* Shift alone is a key too (the one people press to wake a screen):
     * its press is an edge of its own, outside `any`. */
    InputState st;
    Input_Init(&st);
    InputRaw raw;
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    Input out;
    Input_Step(&st, &raw, 1.0f / 60.0f, &out);
    raw.shift = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &out);
    bool first = out.modPressed && !out.any && out.shift;
    Input_Step(&st, &raw, 1.0f / 60.0f, &out);
    bool once = !out.modPressed;
    raw.shift = false;
    raw.focused = false;
    Input_Step(&st, &raw, 1.0f / 60.0f, &out);
    raw.alt = true; /* Alt still down from the Alt+Tab that brought the window back */
    raw.focused = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &out);
    CHECK(first && once && !out.modPressed, "input: Shift, Ctrl or Alt going down is a press of its own, once, and not one held as focus returns");
}

static void test_attract_tour(void) {
    Attract a;
    Attract_Init(&a);
    bool shown[6] = {true, false, true, true, false, true}; /* 1 and 4 not installed */
    AttractIn in = Hall(ATTRACT_1MIN, 3, 6);
    in.shown = shown;
    Run(&a, &in, 60.05f);
    CHECK(a.on && a.place == 3, "attract tour: starts on your machine");
    CHECK(Run(&a, &in, 5.9f) == ATTRACT_NONE && a.place == 3, "attract tour: six seconds on each");
    CHECK(Run(&a, &in, 0.2f) == ATTRACT_GLIDED && a.place == 5, "attract tour: on to the next installed machine");
    Run(&a, &in, 6.0f);
    CHECK(a.place == 0, "attract tour: round the end of the row");
    Run(&a, &in, 6.0f);
    CHECK(a.place == 2 && Attract_ShownPlace(&a, 3) == 2, "attract tour: past one not installed; the lit machine is the tour's");
    CHECK(Attract_Next(2, 6, shown) == 3 && Attract_Next(5, 6, NULL) == 0, "attract next: the next with something to show");
    bool lone[3] = {false, true, false};
    CHECK(Attract_Next(1, 3, lone) == 1 && Attract_Next(0, 1, NULL) == 0, "attract next: nothing else to show, it stays");

    AttractIn wake = in;
    wake.input = true;
    CHECK(Attract_Step(&a, &wake, 1.0f / 60.0f) == ATTRACT_WOKE && !a.on && Attract_ShownPlace(&a, 3) == 3,
          "attract wake: any key, button or move wakes it, back on your machine");
    CHECK(Attract_Settling(&a), "attract wake: a click right after waking is part of it (the row is still gliding back)");
    CHECK(Attract_Step(&a, &wake, 1.0f / 60.0f) == ATTRACT_NONE, "attract wake: only the press that woke it is swallowed");
    Run(&a, &in, ATTRACT_WAKE_SETTLE);
    CHECK(!Attract_Settling(&a), "attract wake: clicks count again a moment later");
    CHECK(Run(&a, &in, 59.0f) == ATTRACT_NONE && !a.on, "attract wake: a whole quiet minute again before the next tour");

    Attract_Init(&a);
    Run(&a, &in, 60.05f);
    in.count = 2; /* the row got shorter under it */
    Run(&a, &in, 0.1f);
    CHECK(a.place == 1, "attract tour: kept on the row when it gets shorter");
    in.count = 0;
    CHECK(Attract_Step(&a, &in, 1.0f / 60.0f) == ATTRACT_NONE && !a.on, "attract: an emptied row ends it without a wake");

    Attract_Init(&a);
    in = Hall(ATTRACT_1MIN, 0, 3);
    int frames = 0;
    while (Attract_Step(&a, &in, 1.0f / 60.0f) != ATTRACT_BEGAN && frames < 10000) frames++;
    CHECK(a.cardOut < 0.1f, "attract card: in place as it begins");
    Run(&a, &in, 0.2f);
    CHECK(a.cardOut > 0.45f && a.cardOut < 0.6f, "attract card: slides away over 0.4 s");
    Run(&a, &in, 0.3f);
    CHECK(a.cardOut == 1.0f, "attract card: gone");
    wake = in;
    wake.input = true;
    Attract_Step(&a, &wake, 1.0f / 60.0f);
    Run(&a, &in, 0.3f);
    CHECK(a.cardOut == 0.0f, "attract card: back within a quarter second of waking");
}

static void test_attract_setting(void) {
    static LauncherIni ini;
    char path[700];
    LauncherIni_Path(path, sizeof(path));
    remove(path);
    LauncherIni_Load(&ini);
    CHECK(Attract_Setting(&ini) == ATTRACT_1MIN, "attract setting: a minute unless set");
    CHECK(Attract_SaveSetting(&ini, ATTRACT_2MIN) && Attract_Setting(&ini) == ATTRACT_2MIN, "attract setting: saved");
    LauncherIni_Load(&ini);
    CHECK(Attract_Setting(&ini) == ATTRACT_2MIN && strcmp(LauncherIni_Get(&ini, "attract_mode"), "2min") == 0, "attract setting: in launcher.ini");
    CHECK(Attract_SaveSetting(&ini, ATTRACT_OFF) && Attract_Setting(&ini) == ATTRACT_OFF, "attract setting: off");
    LauncherIni_Set(&ini, "attract_mode", "soon");
    CHECK(Attract_Setting(&ini) == ATTRACT_1MIN, "attract setting: a word it does not know is the default");
    CHECK(strcmp(Attract_SettingName(ATTRACT_2MIN), "2 min") == 0 && Attract_After(ATTRACT_OFF) == 0.0f, "attract setting: names");
    remove(path);

    Settings s;
    Settings_Init(&s);
    s.tab = ST_SCREEN;
    static Manifest m;
    static Profiles p;
    SettingsView v = {.manifest = &m, .profiles = &p, .attract = ATTRACT_2MIN};
    static SettingsPage page;
    Settings_Layout(&s, &v, &page);
    const KitItem *it = Kit_Get(&page.list, WID_SET_ATTRACT);
    const SettingsRow *row = Settings_RowOf(&page, WID_SET_ATTRACT);
    CHECK(it && it->kind == KIT_STEPPER && it->value == ATTRACT_2MIN && strcmp(it->label, "2 min") == 0 && (it->flags & KIT_WRAP) && row &&
              strcmp(row->label, "Attract mode") == 0 && row->r.y + row->r.h <= SET_HELP_Y,
          "attract setting: Settings > Screen has < Off | 1 min | 2 min >");
    KitEvent ev = {KIT_EV_CHANGE, WID_SET_ATTRACT, ATTRACT_OFF};
    SettingsAction act = Settings_Event(&s, &ev);
    CHECK(act.type == SA_ATTRACT && act.value == ATTRACT_OFF, "attract setting: a change is an action");
    bool fits = true;
    for (int c = 0; c < ATTRACT_SETTINGS; c++) {
        v.attract = c;
        Settings_Layout(&s, &v, &page);
        const SettingsRow *r = Settings_RowOf(&page, WID_SET_ATTRACT);
        if (!r || !r->note[0][0] || r->noteX + Kit_TextWidth(r->note[0], 8) > SET_ROW_X + SET_ROW_W) fits = false;
    }
    CHECK(fits, "attract setting: every choice's note ends inside its row");
}

/* -------------------------------------------------------------- jukebox */

static Song sSong;

/* Two patterns, played 0 then 1: the lead holds a note across the turn
 * from the first into the second, and from the end of the song round to
 * its start. */
static void TwoPatterns(void) {
    Song_Init(&sSong, "Test");
    sSong.bpm = 120; /* a step is 1/8 s */
    int second;
    Song_AddPattern(&sSong, &second);
    sSong.order[0] = 0;
    sSong.order[1] = (uint8_t)second;
    sSong.orderCount = 2;
    SongPattern *a = &sSong.patterns[0], *b = &sSong.patterns[second];
    a->tone[TRACK_LEAD][60] = 72;
    for (int i = 61; i < 64; i++) a->tone[TRACK_LEAD][i] = NOTE_HOLD;
    for (int i = 0; i < 4; i++) b->tone[TRACK_LEAD][i] = NOTE_HOLD; /* rings into the second pattern */
    b->tone[TRACK_BASS][62] = 45;
    b->tone[TRACK_BASS][63] = NOTE_HOLD;
    a->tone[TRACK_BASS][0] = NOTE_HOLD; /* ...and round into the song's start */
    a->tone[TRACK_BASS][1] = NOTE_HOLD;
    a->tone[TRACK_ARP][10] = 60;
    a->tone[TRACK_ARP][11] = NOTE_NONE;
    a->tone[TRACK_ARP][12] = NOTE_HOLD; /* a hold after silence holds nothing */
    a->drums[9] = DRUM_KICK | DRUM_HAT;
}

static void test_jukebox_window(void) {
    TwoPatterns();
    CHECK(Jukebox_Length(&sSong) == 128 && Jukebox_Length(NULL) == 1, "jukebox: two patterns are 128 steps");
    CHECK(Jukebox_Playhead(&sSong, 1.0) == 8.0 && Jukebox_Playhead(&sSong, 16.0 + 0.25) == 2.0 && Jukebox_Playhead(&sSong, -3.0) == 0.0,
          "jukebox: the playhead follows the song's tempo, round its end");
    JukeNote n = Jukebox_NoteAt(&sSong, TRACK_LEAD, 60);
    CHECK(n.note == 72 && n.start, "jukebox: a note struck");
    n = Jukebox_NoteAt(&sSong, TRACK_LEAD, 66);
    CHECK(n.note == 72 && !n.start, "jukebox: held into the next pattern");
    n = Jukebox_NoteAt(&sSong, TRACK_LEAD, 68);
    CHECK(n.note == NOTE_NONE, "jukebox: let go after the holds");
    n = Jukebox_NoteAt(&sSong, TRACK_BASS, 1);
    CHECK(n.note == 45 && !n.start && Jukebox_NoteAt(&sSong, TRACK_BASS, 129).note == 45, "jukebox: held round the song's end into its start");
    CHECK(Jukebox_NoteAt(&sSong, TRACK_ARP, 12).note == NOTE_NONE && Jukebox_NoteAt(&sSong, TRACK_ARP, 10).start, "jukebox: a hold after silence holds nothing");
    CHECK(Jukebox_NoteAt(&sSong, 7, 0).note == NOTE_NONE, "jukebox: a track that is not there");

    JukeWindow w;
    Jukebox_Window(&sSong, 66.0 / 8.0 + 0.0625, &w); /* half way into step 66 */
    CHECK(w.first == 58 && w.playhead > 66.4 && w.playhead < 66.6, "jukebox window: eight steps before the playhead");
    CHECK(w.tone[TRACK_LEAD][2].note == 72 && w.tone[TRACK_LEAD][2].start && w.tone[TRACK_LEAD][3].note == 72 && !w.tone[TRACK_LEAD][3].start &&
              w.tone[TRACK_LEAD][9].note == 72 && w.tone[TRACK_LEAD][10].note == NOTE_NONE,
          "jukebox window: a note, its holds, across the turn of the pattern");
    Jukebox_Window(&sSong, 63.0 / 8.0 + 8.0 + 0.01, &w); /* playhead at 127 (the last step): the window goes round */
    CHECK(w.first == 119 && w.tone[TRACK_BASS][7].note == 45 && w.tone[TRACK_BASS][7].start && w.tone[TRACK_BASS][9].note == 45 &&
              !w.tone[TRACK_BASS][9].start && w.tone[TRACK_BASS][11].note == NOTE_NONE && w.drums[8 + 9 + 1] == (DRUM_KICK | DRUM_HAT),
          "jukebox window: round the song's end, the held bass and the drums of its start");
    Jukebox_Window(&sSong, 61.0 / 8.0 + 0.01, &w); /* column 0 is step 53: nothing held in */
    CHECK(w.tone[TRACK_LEAD][7].start && w.lo[TRACK_LEAD] == 72 && w.hi[TRACK_LEAD] == 72 && w.lo[TRACK_ARP] == NOTE_NONE && w.any,
          "jukebox window: each track's range in sight");
    Jukebox_Window(&sSong, 64.0 / 8.0 + 0.01, &w); /* column 0 is step 56; lead struck at 60 */
    Jukebox_Window(&sSong, 70.0 / 8.0 + 0.01, &w); /* column 0 is step 62: the lead's note began before the window */
    CHECK(w.tone[TRACK_LEAD][0].note == 72 && !w.tone[TRACK_LEAD][0].start, "jukebox window: a note held in from before the window");

    Song empty;
    memset(&empty, 0, sizeof(empty));
    Jukebox_Window(&empty, 5.0, &w);
    CHECK(!w.any && w.first == 0 && w.tone[0][0].note == NOTE_NONE, "jukebox window: a song with nothing in it is quiet, not a crash");
    sSong.order[1] = 60; /* names no pattern */
    Jukebox_Window(&sSong, 66.0 / 8.0, &w);
    CHECK(w.tone[TRACK_LEAD][6].note == NOTE_NONE && w.tone[TRACK_LEAD][5].note == 72, "jukebox window: an order entry with no pattern is silence");

    static Song theme;
    Music_ThemeToSong(Music_FindTheme(HALL_MUSIC_SLUG), &theme);
    Jukebox_Window(&theme, 3.0, &w);
    CHECK(w.any && w.lo[TRACK_BASS] != NOTE_NONE && w.lo[TRACK_BASS] < w.lo[TRACK_LEAD], "jukebox window: the hall's theme has bass under the lead");
}

static void test_hall_tune(void) {
    RemoveUnder("XDG_CONFIG_HOME", "ghost-arcade/music.ini");
    static Song tune;
    HallMusic_Tune(&tune);
    CHECK(tune.bpm == Music_FindTheme(HALL_MUSIC_SLUG)->bpm && tune.orderCount == 1, "hall tune: the built-in theme when the hall has no song");
    WriteUnder("XDG_DATA_HOME", "ghost-arcade/music/hall.gsong", "GSONG 1\nname Hall Tune\nbpm 150\norder 0\npattern 0\n00 A-1 A-3 C-3 K..\n");
    Song_Assign(HALL_MUSIC_SLUG, "hall.gsong");
    HallMusic_Tune(&tune);
    CHECK(tune.bpm == 150 && strcmp(tune.name, "Hall Tune") == 0, "hall tune: the song music.ini gives the hall");
    WriteUnder("XDG_DATA_HOME", "ghost-arcade/music/hall.gsong", "not a song\n");
    HallMusic_Tune(&tune);
    CHECK(tune.bpm == Music_FindTheme(HALL_MUSIC_SLUG)->bpm, "hall tune: one that does not load is the theme, as music.c plays it");
    RemoveUnder("XDG_DATA_HOME", "ghost-arcade/music/hall.gsong");
    RemoveUnder("XDG_CONFIG_HOME", "ghost-arcade/music.ini");
}

/* --------------------------------------------------- Chipforge's Songs */

static GamePageData sTool;

static GamePageView ToolView(void) {
    GamePageView v = {
        .name = "Chipforge", .tool = true, .play = HP_PLAY, .dailyReason = "", .canFavourite = true,
        .place = 11, .count = 12, .boards = &sTool.boards, .today = "daily-20261014", .device = DEV_KEYBOARD,
        .data = &sTool, .hallMusic = true,
    };
    return v;
}

static void test_songs_library(void) {
    WriteUnder("XDG_DATA_HOME", "ghost-arcade/music/b-song.gsong", "GSONG 1\nname Lobby\nbpm 96\norder 0\npattern 0\n00 A-1 A-3 C-3 K..\n");
    WriteUnder("XDG_DATA_HOME", "ghost-arcade/music/a-song.gsong", "GSONG 1\nbpm 96\norder 0\npattern 0\n00 A-1 A-3 C-3 K..\n");
    GameEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "Chipforge");
    snprintf(e.exec, sizeof(e.exec), "~/bin/chipforge");
    static Manifest m;
    memset(&m, 0, sizeof(m));
    GamePageSource src = {&e, 11, NULL, NULL, 0, &m, "Lobby", time(NULL), "b-song.gsong"};
    GamePage_Load(&sTool, &src);
    CHECK(sTool.libraryCount == 2 && strcmp(sTool.library[0].file, "a-song.gsong") == 0 && strcmp(sTool.library[0].title, "Untitled") == 0 &&
              strcmp(sTool.library[1].title, "Lobby") == 0 && sTool.hallEntry == 2,
          "songs: the library by file name, titled as Chipforge titles it; the one the hall plays");
    CHECK(GamePage_SongCount(&sTool) == 3 && strcmp(GamePage_SongTitle(&sTool, 0), "Built-in theme") == 0 &&
              strcmp(GamePage_SongTitle(&sTool, 2), "Lobby") == 0,
          "songs: the built-in theme first, then the library");
    src.hallFile = NULL;
    GamePage_Load(&sTool, &src);
    CHECK(sTool.hallEntry == 0, "songs: no song of yours: the built-in theme is the hall's");
    src.hallFile = "gone.gsong";
    GamePage_Load(&sTool, &src);
    CHECK(sTool.hallEntry == -1, "songs: a song not in the list is marked nowhere");
    RemoveUnder("XDG_DATA_HOME", "ghost-arcade/music/a-song.gsong");
    RemoveUnder("XDG_DATA_HOME", "ghost-arcade/music/b-song.gsong");
}

static void test_songs_tab(void) {
    memset(&sTool, 0, sizeof(sTool));
    sTool.tool = true;
    sTool.libraryCount = 10;
    for (int i = 0; i < 10; i++) {
        snprintf(sTool.library[i].file, sizeof(sTool.library[i].file), "s%d.gsong", i);
        snprintf(sTool.library[i].title, sizeof(sTool.library[i].title), "Song number %d with a long, long title", i);
    }
    sTool.hallEntry = 3;
    GamePage p;
    memset(&p, 0, sizeof(p));
    p.tab = GT_SONGS;
    GamePageView v = ToolView();
    KitList l;
    GamePage_Layout(&p, &v, &l);
    const KitItem *view = Kit_Get(&l, WID_PAGE_SONGS_VIEW), *hall = Kit_Get(&l, WID_PAGE_SONG + 3);
    int rows = 0;
    bool fits = true;
    for (int i = 0; i < l.count; i++) {
        const KitItem *it = &l.items[i];
        if (GamePage_SongOf(it->id) < 0) continue;
        rows++;
        int end = (int)it->r.x + 20 + Kit_TextWidth(it->label, 16) + (it->badge[0] ? 12 + Kit_TextWidth(it->badge, 8) + 12 : 0);
        if (end > it->r.x + it->r.w - 12 || it->r.y + it->r.h > PAGE_HELP_Y - 8) fits = false;
    }
    CHECK(view && view->value == SONGS_HALL && strcmp(view->label, "Hall song") == 0 && (view->flags & KIT_NOFOCUS), "songs tab: < Hall song > where Scores has its board");
    CHECK(rows == PAGE_SONGS_VISIBLE && fits && hall && strcmp(hall->badge, "In the hall") == 0 && !Kit_Get(&l, WID_PAGE_SONG + 1)->badge[0],
          "songs tab: a row a song, titles cut to leave room for In the hall");
    CHECK(l.nav == 0 && GamePage_SongsShown(&p, &v), "songs tab: the keys are the page's own there");

    KitEvent press = {KIT_EV_PRESS, WID_PAGE_SONG + 2, 0};
    Input blank = Blank();
    GamePageAction act = GamePage_Read(&p, &v, &press, &blank);
    CHECK(act.verb == GA_SONG && act.value == 2, "songs tab: a press (or a click) gives the hall that song");
    press.id = WID_PAGE_SONG + 0;
    CHECK(GamePage_Read(&p, &v, &press, &blank).value == 0, "songs tab: ...or the built-in theme back");

    KitState k;
    Kit_Init(&k);
    k.focus = WID_PAGE_FAV;
    Input down = Key(IK_DOWN), up = Key(IK_UP);
    CHECK(GamePage_SongKeys(&p, &v, &k, &l, &down, true) && k.focus == WID_PAGE_SONG + 0, "songs keys: down from the buttons into the songs");
    k.focus = WID_PAGE_SONG + 7;
    CHECK(GamePage_SongKeys(&p, &v, &k, &l, &down, true) && k.focus == WID_PAGE_SONG + 8 && p.songs.top == 1, "songs keys: the list scrolls to the keys");
    k.focus = WID_PAGE_SONG + 10;
    CHECK(GamePage_SongKeys(&p, &v, &k, &l, &down, true) && k.focus == WID_PAGE_PLAY, "songs keys: round from the last song to Play");
    CHECK(GamePage_SongKeys(&p, &v, &k, &l, &up, true) && k.focus == WID_PAGE_SONG + 10 && p.songs.top == 3, "songs keys: Up from Play to the last song");
    k.focus = WID_PAGE_SONG + 10;
    Input wheel = Wheel(600, 300, 2.0f);
    CHECK(GamePage_SongKeys(&p, &v, &k, &l, &wheel, true) && p.songs.top == 1 && k.focus == WID_PAGE_SONG + 8, "songs wheel: scrolls, the keys kept in sight");

    Input x = Key(IK_X);
    act = GamePage_Read(&p, &v, &(KitEvent){KIT_EV_NONE, KIT_NONE, 0}, &x);
    CHECK(act.verb == GA_VIEW && p.songsView == SONGS_WHO && !GamePage_SongsShown(&p, &v), "songs tab: X turns to who plays what");
    GamePage_Layout(&p, &v, &l);
    CHECK(!Kit_Get(&l, WID_PAGE_SONG + 1) && l.nav != 0 && strcmp(Kit_Get(&l, WID_PAGE_SONGS_VIEW)->label, "Who plays what") == 0,
          "songs tab: no song rows there; the buttons' keys as before");
    CHECK(!GamePage_SongKeys(&p, &v, &k, &l, &down, true), "songs keys: only while the songs show");
    KitEvent turn = {KIT_EV_CHANGE, WID_PAGE_SONGS_VIEW, SONGS_HALL};
    CHECK(GamePage_Read(&p, &v, &turn, &blank).verb == GA_VIEW && p.songsView == SONGS_HALL, "songs tab: the arrows turn it back");
    press.id = WID_PAGE_SONG + 2;
    p.tab = GT_ABOUT;
    CHECK(GamePage_Read(&p, &v, &press, &blank).verb != GA_SONG, "songs tab: a stray song id on another tab does nothing");

    char help[160], hint[200];
    p.tab = GT_SONGS;
    GamePage_Help(&v, &sTool, WID_PAGE_SONG + 3, help, sizeof(help));
    CHECK(strcmp(help, "The hall plays this now.") == 0, "songs help: the hall's own");
    v.hallMusic = false;
    GamePage_Help(&v, &sTool, WID_PAGE_SONG + 1, help, sizeof(help));
    CHECK(strstr(help, "Plays this in the hall") && strstr(help, "Hall music is off") && Kit_TextWidth(help, 8) <= 800, "songs help: says when hall music is off");
    GamePage_Hint(&p, &v, DEV_KEYBOARD, hint, sizeof(hint));
    CHECK(strstr(hint, "X list") != NULL && Kit_TextWidth(hint, 8) <= 800, "songs hint: X turns the list");

    GamePageView game = ToolView();
    game.tool = false;
    game.name = "Skyraid";
    p.tab = GT_SONGS;
    CHECK(!GamePage_SongsShown(&p, &game), "songs tab: a game has none");

    /* Shift+D opens the desk from a page as it does from the hall, whatever
     * the machine (D alone is the page's Daily challenge). */
    Input d = Key(IK_D), sd = ShiftKey(IK_D);
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    CHECK(GamePage_Read(&p, &game, &none, &d).verb == GA_DAILY && GamePage_Read(&p, &game, &none, &sd).verb == GA_DESK &&
              GamePage_Read(&p, &v, &none, &sd).verb == GA_DESK,
          "page keys: D is the page's Daily challenge, Shift+D the desk (Chipforge's page too)");
}

/* ------------------------------------------------------------------ all */

void test_extras_all(void) {
    test_desk_days();
    test_desk_calendar();
    test_desk_streak();
    test_desk_featured();
    test_desk_load();
    test_desk_screen();
    test_desk_ways_in();
    test_attract_timer();
    test_attract_tour();
    test_attract_setting();
    test_jukebox_window();
    test_hall_tune();
    test_songs_library();
    test_songs_tab();
}
