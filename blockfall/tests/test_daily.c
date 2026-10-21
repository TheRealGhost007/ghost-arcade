/* The Daily challenge (3.6.0, REWORK_PLAN.md "Daily challenge"), with no
 * window: the day (keys, dates, day numbers across month and year ends, the
 * words), the twist (from the date alone: the same all day, every twist
 * once a week, never two days running, each with its board, mode, power-
 * ups and start level); the same date gives the same pieces and power-ups
 * and a different date differs; the rules on a daily (the two-minute
 * clock in every twist's mode, the fast start's level and the lines that
 * count up from it, power-ups only on the Power twist, Dig's rows on the
 * clock, a restart keeping the rules and the seed's pieces, Game_Timed);
 * what a daily records (the day's board, a shared one, never a normal
 * board; runs.log's key); the streak file (round trip, best and runs,
 * tolerance, a full log) and the streak logic (today or yesterday keeps
 * it, a gap ends it, the best streak); the panel's spec, the music, the
 * sounds and the HUD on a daily; the results card and its Daily button;
 * the pause menu's words; the menu item's line and the play tag. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "daily.h"
#include "ghostlink.h"
#include "../src/challenge.h"
#include "../src/danger.h"
#include "../src/game.h"
#include "../src/hudfx.h"
#include "../src/layout.h"
#include "../src/pausemenu.h"
#include "../src/persist.h"
#include "../src/results.h"
#include "../src/runrecord.h"
#include "../src/soundplan.h"
#include "../src/stats.h"
#include "sfx.h"

#define FRAME (1.0f / 60.0f)

static uint64_t SeedOf(int date) { return Daily_SeedForDate(date / 10000, (date / 100) % 100, date % 100); }

static Game Daily(int date) {
    Game g;
    DailyTwist t = Daily_TwistOf(date);
    Game_InitDaily(&g, Daily_Config(t), Daily_Rules(date, t), SeedOf(date));
    return g;
}

/* A date with this twist, on or after `from` (the rotation covers every
 * twist within a week). */
static int DateWith(DailyTwist t, int from) {
    int day = Daily_DayNumber(from);
    for (int i = 0; i < 14; i++)
        if (Daily_TwistOf(Daily_DateOfDay(day + i)) == t) return Daily_DateOfDay(day + i);
    return from;
}

static void RemoveData(const char *rel) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_DATA_HOME"), rel);
    remove(path);
}

/* ------------------------------------------------------------------ days */

static void test_days(void) {
    char key[24];
    Daily_BoardKey(20261020, key, sizeof(key));
    CHECK(strcmp(key, "daily-20261020") == 0, "daily key: the day's board, as the shared code names it");
    char shared[24];
    Daily_ModeForDate(2026, 10, 20, shared, sizeof(shared));
    CHECK(strcmp(key, shared) == 0, "daily key: the same string ghost-common's Daily_Mode gives");
    CHECK(Daily_DateFromMode("daily-20261020") == 20261020 && Daily_DateFromMode("daily-20270101") == 20270101,
          "daily date: read back from the board key");
    CHECK(Daily_DateFromMode("daily-2026102") == 0 && Daily_DateFromMode("daily-20261032") == 0 && Daily_DateFromMode("marathon") == 0 &&
              Daily_DateFromMode("daily-2026102a") == 0 && Daily_DateFromMode(NULL) == 0 && Daily_DateFromMode("daily-20260229") == 0,
          "daily date: anything else is 0 (a short key, 32 October, 29 February 2026)");
    CHECK(Daily_DateValid(20280229) && !Daily_DateValid(20270229) && !Daily_DateValid(20260431) && Daily_DateValid(20261231),
          "daily date: leap days and month ends");
    CHECK(Daily_DayNumber(19700101) == 0 && Daily_DayNumber(20261014) == 20740 && Daily_DayNumber(20000229) == 11016,
          "daily days: day numbers match the calendar (as the launcher's desk counts them)");
    CHECK(Daily_DateOfDay(Daily_DayNumber(20261031) + 1) == 20261101 && Daily_DateOfDay(Daily_DayNumber(20261231) + 1) == 20270101 &&
              Daily_DateOfDay(Daily_DayNumber(20260301) - 1) == 20260228 && Daily_DateOfDay(Daily_DayNumber(20280301) - 1) == 20280229,
          "daily days: yesterday and tomorrow across month and year ends");
    char words[40];
    Daily_DateWords(20261014, words, sizeof(words));
    CHECK(strcmp(words, "Wednesday 14 October") == 0, "daily words: the day as the desk says it");
    Daily_DateWords(20270101, words, sizeof(words));
    CHECK(strcmp(words, "Friday 1 January") == 0, "daily words: a Friday in January");
    Daily_DateWords(0, words, sizeof(words));
    CHECK(strcmp(words, "today") == 0, "daily words: a bad date is just today");
}

/* ----------------------------------------------------------------- twist */

static void test_twist(void) {
    int today = 20261020;
    bool same = true, inRange = true, never2 = true, weekly = true;
    int day0 = Daily_DayNumber(today);
    for (int d = day0 - 400; d < day0 + 1000; d++) {
        int date = Daily_DateOfDay(d);
        DailyTwist t = Daily_TwistOf(date);
        if (t != Daily_TwistOf(date)) same = false;
        if ((int)t < 0 || t >= TWIST_COUNT) inRange = false;
        else if (t == Daily_TwistOf(Daily_DateOfDay(d - 1))) never2 = false;
    }
    for (int week = 0; week < 300; week++) { /* weeks begin on day 0, 1 January 1970 (no date is earlier) */
        int seen = 0;
        for (int d = week * 7; d < week * 7 + 7; d++) seen |= 1 << Daily_TwistOf(Daily_DateOfDay(d));
        if (seen != (1 << TWIST_COUNT) - 1) weekly = false;
    }
    CHECK(same && inRange, "twist: the same all day, always one of the seven");
    CHECK(weekly, "twist: every twist has its day each week");
    CHECK(never2, "twist: never the same twist two days running, across weeks too");
    DailyTwist a = Daily_TwistOf(today), b = Daily_TwistOf(Daily_DateOfDay(day0 + 7)), c = Daily_TwistOf(Daily_DateOfDay(day0 + 14));
    CHECK(!(a == b && b == c), "twist: weeks are not all alike");
    CHECK(Daily_TwistOf(0) == TWIST_CLASSIC && Daily_TwistOf(20261032) == TWIST_CLASSIC, "twist: a bad date plays the classic twist");

    /* Each twist's board, mode and rules. */
    GameConfig cfg = Daily_Config(TWIST_WIDE);
    CHECK(cfg.mode == MODE_ULTRA && cfg.width == 14 && cfg.height == 20 && cfg.modernScoring, "twist: Ultra on Wide is Ultra on 14x20 under modern rules");
    cfg = Daily_Config(TWIST_TALL);
    CHECK(cfg.mode == MODE_ULTRA && cfg.width == 8 && cfg.height == 26, "twist: Tall is 8x26");
    cfg = Daily_Config(TWIST_TINY);
    CHECK(cfg.mode == MODE_ULTRA && cfg.width == 6 && cfg.height == 14, "twist: Tiny is 6x14");
    cfg = Daily_Config(TWIST_POWER);
    CHECK(cfg.mode == MODE_POWER && cfg.width == 10 && cfg.height == 20, "twist: Power Ultra is Power on the classic board");
    cfg = Daily_Config(TWIST_DIG);
    CHECK(cfg.mode == MODE_DIG && cfg.width == 10 && cfg.height == 20, "twist: Dig on the clock is Dig on the classic board");
    cfg = Daily_Config(TWIST_FAST);
    CHECK(cfg.mode == MODE_ULTRA && cfg.width == 10 && cfg.height == 20, "twist: the fast start is Ultra on the classic board");
    DailyRules r = Daily_Rules(20261020, TWIST_FAST);
    CHECK(r.on && r.date == 20261020 && r.twist == TWIST_FAST && r.startLevel == DAILY_FAST_LEVEL && !r.powerUps, "twist rules: the fast start's level");
    r = Daily_Rules(20261020, TWIST_POWER);
    CHECK(r.powerUps && r.startLevel == 1, "twist rules: Power Ultra rolls power-ups");
    r = Daily_Rules(20261020, TWIST_CLASSIC);
    CHECK(!r.powerUps && r.startLevel == 1, "twist rules: the plain twists have no power-ups and start at 1");
    bool named = true;
    for (int t = 0; t < TWIST_COUNT; t++)
        if (!Daily_TwistName((DailyTwist)t)[0] || !Daily_TwistAbout((DailyTwist)t)[0]) named = false;
    CHECK(named && strcmp(Daily_TwistName(TWIST_WIDE), "Ultra on Wide") == 0 && strcmp(Daily_TwistName(TWIST_FAST), "Fast start") == 0,
          "twist words: every twist has a name and a sentence");
    CHECK(strcmp(Daily_TwistName((DailyTwist)99), "Ultra") == 0, "twist words: an unknown twist reads as the classic one");
}

/* ------------------------------------------------------------- the pieces */

/* The pieces that come in, locked straight down, with their power-ups. */
static void Sequence(Game *g, int n, PieceType types[], PowerUp powers[]) {
    for (int i = 0; i < n; i++) {
        types[i] = g->current;
        powers[i] = g->curPower;
        Game_HardDrop(g);
        if (g->phase != GS_PLAYING) { Board_Init(&g->board, g->cfg.width, g->cfg.height); g->phase = GS_PLAYING; }
    }
}

static void test_same_pieces(void) {
    int power = DateWith(TWIST_POWER, 20261020);
    Game a = Daily(power), b = Daily(power);
    PieceType ta[60], tb[60];
    PowerUp pa[60], pb[60];
    Sequence(&a, 60, ta, pa);
    Sequence(&b, 60, tb, pb);
    CHECK(memcmp(ta, tb, sizeof(ta)) == 0 && memcmp(pa, pb, sizeof(pa)) == 0, "daily pieces: the same date gives the same sixty pieces and power-ups");
    CHECK(a.daily.on && b.daily.on && a.daily.twist == TWIST_POWER, "daily pieces: both are the day's Power Ultra");
    int withPower = 0;
    for (int i = 0; i < 60; i++) if (pa[i] != PU_NONE) withPower++;
    CHECK(withPower > 0, "daily pieces: Power Ultra's pieces carry power-ups");

    /* The run's own seed, not the clock: the game started twice a frame
     * apart is still the same. */
    Game c = Daily(power);
    Game_Update(&c, FRAME);
    CHECK(c.current == ta[0], "daily pieces: the first piece is the date's, whatever the clock");

    /* Another day differs. */
    int other = Daily_DateOfDay(Daily_DayNumber(power) + 7);
    while (Daily_TwistOf(other) != TWIST_POWER) other = Daily_DateOfDay(Daily_DayNumber(other) + 7);
    Game d = Daily(other);
    PieceType td[60];
    PowerUp pd[60];
    Sequence(&d, 60, td, pd);
    CHECK(memcmp(ta, td, sizeof(ta)) != 0, "daily pieces: a different date gives different pieces");
    CHECK(SeedOf(power) != SeedOf(other) && Daily_SeedForDate(2026, 10, 20) == Daily_SeedForDate(2026, 10, 20),
          "daily seed: the shared code's seed is the date's");

    /* The normal modes draw from the same bag rule: a daily on Ultra gets
     * the pieces an Ultra with that seed would. */
    int plain = DateWith(TWIST_CLASSIC, 20261020);
    Game e = Daily(plain), f;
    Game_InitConfig(&f, (GameConfig){MODE_ULTRA, 10, 20, true}, SeedOf(plain), 0, 1, 0);
    PieceType te[30], tf[30];
    PowerUp pe[30], pf[30];
    Sequence(&e, 30, te, pe);
    Sequence(&f, 30, tf, pf);
    CHECK(memcmp(te, tf, sizeof(te)) == 0, "daily pieces: the day's seed gives the pieces any run with that seed gets");
    int none = 0;
    for (int i = 0; i < 30; i++) if (pe[i] == PU_NONE) none++;
    CHECK(none == 30, "daily pieces: no power-ups outside the Power twist");
}

/* ----------------------------------------------------------------- rules */

static void test_rules(void) {
    /* The clock, in every twist. */
    for (int t = 0; t < TWIST_COUNT; t++) {
        int date = DateWith((DailyTwist)t, 20261020);
        Game g = Daily(date);
        char msg[96];
        snprintf(msg, sizeof(msg), "daily rules: %s starts with two minutes on the clock and is timed", Daily_TwistName((DailyTwist)t));
        CHECK(g.timeLeft == DAILY_SECONDS && Game_Timed(&g) && g.daily.on && g.daily.date == date, msg);
        snprintf(msg, sizeof(msg), "daily rules: %s plays its twist's board", Daily_TwistName((DailyTwist)t));
        GameConfig cfg = Daily_Config((DailyTwist)t);
        CHECK(g.cfg.mode == cfg.mode && g.cfg.width == cfg.width && g.cfg.height == cfg.height && g.board.w == cfg.width, msg);
    }
    Game ultra;
    Game_InitConfig(&ultra, (GameConfig){MODE_ULTRA, 10, 20, true}, 5, 0, 1, 0);
    Game marathon;
    Game_InitConfig(&marathon, (GameConfig){MODE_MARATHON, 10, 20, true}, 5, 0, 1, 0);
    CHECK(Game_Timed(&ultra) && !Game_Timed(&marathon) && marathon.timeLeft == 0.0f && !marathon.daily.on,
          "daily rules: Ultra is timed, a Marathon is not, and a normal run is never a daily");

    /* Time up ends the run, won, in a mode that never had a clock. */
    Game g = Daily(DateWith(TWIST_POWER, 20261020));
    g.practice.noGravity = false;
    g.timeLeft = 0.5f;
    int frames = 0;
    while (g.phase == GS_PLAYING && frames < 120) {
        Game_Update(&g, FRAME);
        if (g.grounded) Game_HardDrop(&g); /* keep it from stacking up */
        frames++;
    }
    CHECK(g.phase == GS_GAMEOVER && g.won && g.timeLeft == 0.0f && frames >= 29 && frames <= 32,
          "daily rules: the clock runs out and the Power twist ends as Ultra does, won");
    CHECK(Results_KindOf(&g) == END_TIME_UP, "daily rules: time up on the results card");

    /* The fast start: level 8, and the lines count up from it. */
    Game fast = Daily(DateWith(TWIST_FAST, 20261020));
    CHECK(fast.level == DAILY_FAST_LEVEL && fast.fallInterval == Game_FallInterval(DAILY_FAST_LEVEL), "daily rules: the fast start begins at level 8, at its speed");
    Board_Clear(&fast.board);
    for (int r = BOARD_TOTAL_H - 10; r < BOARD_TOTAL_H; r++)
        for (int x = 0; x < fast.board.w - 1; x++) fast.board.cells[r][x] = PIECE_J;
    for (int i = 0; i < 3; i++) { /* three vertical Is down the right: twelve rows... ten are there */
        fast.current = PIECE_I;
        fast.rotation = 1;
        fast.px = fast.board.w - 3;
        fast.py = BOARD_HIDDEN;
        Game_HardDrop(&fast);
    }
    CHECK(fast.linesCleared == 10 && fast.level == DAILY_FAST_LEVEL + 1, "daily rules: ten lines from a fast start is level 9, not 2");

    /* Dig on the clock: the rows, the rises, and the clock still ends it. */
    Game dig = Daily(DateWith(TWIST_DIG, 20261020));
    CHECK(Game_GarbageRows(&dig.board) == DIG_START_ROWS && dig.digInterval > 0.0f && dig.level == 1, "daily rules: Dig's rows and its first wait");
    dig.timeLeft = 0.2f;
    for (int i = 0; i < 20 && dig.phase == GS_PLAYING; i++) Game_Update(&dig, FRAME);
    CHECK(dig.phase == GS_GAMEOVER && dig.won, "daily rules: Dig on the clock ends on the clock, won");

    /* A restart keeps the rules, and the day's seed gives the same pieces. */
    int power = DateWith(TWIST_POWER, 20261020);
    Game p = Daily(power), q = Daily(power);
    PieceType tp[20], tq[20];
    PowerUp pp[20], pq[20];
    Sequence(&p, 20, tp, pp);
    Game_Restart(&p, SeedOf(power));
    CHECK(p.daily.on && p.daily.powerUps && p.daily.date == power && p.timeLeft == DAILY_SECONDS && p.score == 0 && p.phase == GS_PLAYING,
          "daily restart: still the day's daily, with a fresh clock");
    Sequence(&p, 20, tp, pp);
    Sequence(&q, 20, tq, pq);
    CHECK(memcmp(tp, tq, sizeof(tp)) == 0 && memcmp(pp, pq, sizeof(pp)) == 0, "daily restart: the same pieces and power-ups again");
    Game_Restart(&fast, SeedOf(DateWith(TWIST_FAST, 20261020)));
    CHECK(fast.level == DAILY_FAST_LEVEL && fast.linesCleared == 0, "daily restart: the fast start starts fast again");

    /* The level is clamped: a twist can never ask for 0 or 99. */
    DailyRules odd = Daily_Rules(20261020, TWIST_CLASSIC);
    odd.startLevel = 0;
    Game z;
    Game_InitDaily(&z, Daily_Config(TWIST_CLASSIC), odd, 1);
    odd.startLevel = 99;
    Game hi;
    Game_InitDaily(&hi, Daily_Config(TWIST_CLASSIC), odd, 1);
    CHECK(z.level == 1 && hi.level == DAILY_LEVEL_MAX, "daily rules: the start level stays inside 1 to 20");
}

/* --------------------------------------------------------------- records */

static void test_records(void) {
    int date = 20261020;
    Game g = Daily(date);
    g.piecesLocked = 3;
    g.score = 1234;
    RunRecord r = Record_Classify(&g, false);
    CHECK(r.counts && r.toBoard && !r.local && !r.toSave && r.unit == UNIT_POINTS && r.value == 1234 && strcmp(r.key, "daily-20261020") == 0,
          "daily record: a run goes on the day's shared board by points");
    char key[GHOSTLINK_MODE_LEN];
    Record_KeyOf(&g, key, sizeof(key));
    CHECK(strcmp(key, "daily-20261020") == 0, "daily record: the key of the run");
    Record_LogKeyOf(&g, key, sizeof(key));
    CHECK(strcmp(key, "daily-20261020") == 0, "daily record: runs.log carries the day's key, as the launcher's desk counts it");
    Game plain;
    Game_InitConfig(&plain, (GameConfig){MODE_POWER, 10, 20, true}, 5, 0, 1, 0);
    Record_LogKeyOf(&plain, key, sizeof(key));
    CHECK(strcmp(key, "power") == 0, "daily record: a normal run's keys are what they were");
    Record_KeyOf(&plain, key, sizeof(key));
    CHECK(strcmp(key, "power") == 0, "daily record: a normal run's board key too");
    RunRecord tuned = Record_Classify(&g, true);
    CHECK(!tuned.counts && !tuned.toBoard, "daily record: a tuned daily is a playtest like any other");
    g.score = 0;
    r = Record_Classify(&g, false);
    CHECK(!r.counts, "daily record: no points, nothing recorded");
    CHECK(!Record_ParseKey("daily-20261020", NULL, NULL, NULL, NULL), "daily record: the day's board is not a board the Scores browser lists");

    /* On the files: the daily board takes the run, the normal boards do not. */
    RemoveData("ghost-launcher/scores/blockfall.txt");
    RemoveData("blockfall/scores.txt");
    Game d = Daily(DateWith(TWIST_POWER, 20261020));
    d.piecesLocked = 5;
    d.score = 4200;
    r = Record_Classify(&d, false);
    bool shared = false;
    int rank = Record_Submit(&r, "TRG", "2026-10-20", &shared);
    CHECK(rank == 1 && shared, "daily record: the run goes on the shared table");
    CHECK(Record_LoadBestKey(r.key) == 4200, "daily record: the day's board's best");
    int yourRank = 0;
    CHECK(Record_LoadYourBestKey(r.key, "TRG", &yourRank) == 4200 && yourRank == 1, "daily record: your best today and its place");
    CHECK(Record_LoadYourBestKey(r.key, "Arcade", &yourRank) == 0 && yourRank == 0, "daily record: another profile has none");
    CHECK(Record_LoadBest(MODE_POWER, 10, 20, 0) == 0 && Record_LoadBest(MODE_ULTRA, 10, 20, 0) == 0 && Record_LoadBest(MODE_MARATHON, 10, 20, 0) == 0,
          "daily record: the Power, Ultra and Marathon boards are untouched");
    GhostScore rows[GHOSTLINK_TOP_N];
    CHECK(Record_LoadBoard("power", false, rows, GHOSTLINK_TOP_N) == 0 && Record_LoadBoard("ultra", false, rows, GHOSTLINK_TOP_N) == 0,
          "daily record: no row on a normal board");
    CHECK(Record_LoadBoard(r.key, false, rows, GHOSTLINK_TOP_N) == 1 && rows[0].score == 4200 && strcmp(rows[0].username, "TRG") == 0,
          "daily record: the row is on the day's board");
    BoardSize sizes[8];
    CHECK(Record_BoardsWithRows(MODE_POWER, false, sizes, 8) == 0 && Record_BoardsWithRows(MODE_ULTRA, false, sizes, 8) == 0,
          "daily record: the Scores browser's board lists see nothing");
    /* A second, better run the same day: the best moves, the old row stays. */
    d.score = 5100;
    r = Record_Classify(&d, false);
    Record_Submit(&r, "TRG", "2026-10-20", &shared);
    CHECK(Record_LoadBestKey(r.key) == 5100 && Record_LoadBoard(r.key, false, rows, GHOSTLINK_TOP_N) == 2, "daily record: the best counts, every run has its row");
    CHECK(Record_LoadBestKey("") == 0 && Record_LoadBestKey(NULL) == 0 && Record_LoadYourBestKey(NULL, "TRG", NULL) == 0, "daily record: no key, no best");
    /* The lifetime stats fold a daily into its mode (it is a real run). */
    LifeStats life;
    Stats_Clear(&life);
    CHECK(Stats_Fold(&life, &d, false, "2026-10-20") && life.mode[MODE_POWER].runs == 1, "daily record: the stats count it as a Power run");
    RemoveData("ghost-launcher/scores/blockfall.txt");
}

/* ----------------------------------------------------------------- streak */

static void test_log(void) {
    RemoveData("blockfall/daily.dat");
    DailyLog l;
    CHECK(!DailyLog_Load(&l) && l.count == 0, "daily log: none yet");
    CHECK(DailyLog_Streak(&l, 20261020) == 0 && DailyLog_BestStreak(&l, 20261020) == 0 && !DailyLog_Find(&l, 20261020), "daily log: no days, no streak");
    DailyLog_Add(&l, 20261018, 900);
    DailyLog_Add(&l, 20261020, 1200);
    DailyLog_Add(&l, 20261019, 500);
    DailyLog_Add(&l, 20261020, 800);
    DailyLog_Add(&l, 20261020, 3000);
    CHECK(l.count == 3 && l.day[0].date == 20261018 && l.day[1].date == 20261019 && l.day[2].date == 20261020, "daily log: a line a day, by date");
    const DailyDay *today = DailyLog_Find(&l, 20261020);
    CHECK(today && today->runs == 3 && today->best == 3000, "daily log: the runs and the best of the day");
    CHECK(DailyLog_Find(&l, 20261019)->runs == 1 && DailyLog_Find(&l, 20261019)->best == 500, "daily log: another day's");
    CHECK(DailyLog_Streak(&l, 20261020) == 3, "daily streak: three days up to today");
    CHECK(DailyLog_Streak(&l, 20261021) == 3, "daily streak: still three while tomorrow is open (yesterday's counts)");
    CHECK(DailyLog_Streak(&l, 20261022) == 0, "daily streak: a whole day without one ends it");
    CHECK(DailyLog_Streak(&l, 20261019) == 2, "daily streak: as of the day before");
    CHECK(DailyLog_BestStreak(&l, 20261020) == 3 && DailyLog_BestStreak(&l, 20261030) == 3, "daily streak: the best streak stays");
    DailyLog_Add(&l, 20261025, 100);
    CHECK(DailyLog_Streak(&l, 20261025) == 1 && DailyLog_BestStreak(&l, 20261025) == 3, "daily streak: a new run of one after a gap, the best kept");
    DailyLog_Add(&l, 20261031, 100);
    DailyLog_Add(&l, 20261101, 100);
    DailyLog_Add(&l, 20261102, 100);
    DailyLog_Add(&l, 20261103, 100);
    CHECK(DailyLog_Streak(&l, 20261103) == 4 && DailyLog_BestStreak(&l, 20261103) == 4, "daily streak: across a month end");
    DailyLog_Add(&l, 20261231, 100);
    DailyLog_Add(&l, 20270101, 100);
    CHECK(DailyLog_Streak(&l, 20270101) == 2 && DailyLog_Streak(&l, 20270102) == 2 && DailyLog_Streak(&l, 20270103) == 0, "daily streak: across a year end");
    CHECK(DailyLog_BestStreak(&l, 20261020) == 3, "daily streak: the best as of an earlier day ignores what came after");
    DailyLog_Add(&l, 20261332, 100);
    DailyLog_Add(&l, 0, 100);
    CHECK(l.count == 10, "daily log: a date that is not a day is not added");

    /* The round trip, and tolerance. */
    CHECK(DailyLog_Save(&l), "daily log: saved");
    DailyLog back;
    bool sameBack = DailyLog_Load(&back) && back.count == l.count;
    for (int i = 0; sameBack && i < l.count; i++)
        if (back.day[i].date != l.day[i].date || back.day[i].best != l.day[i].best || back.day[i].runs != l.day[i].runs) sameBack = false;
    CHECK(sameBack, "daily log: read back the same");
    char path[700];
    snprintf(path, sizeof(path), "%s/blockfall/daily.dat", getenv("XDG_DATA_HOME"));
    FILE *f = fopen(path, "a");
    if (f) { fputs("not a line\n20261105|abc\n20261106|700|2|extra\n\n20261399|5|1\n", f); fclose(f); }
    CHECK(DailyLog_Load(&back) && back.count == l.count + 1 && DailyLog_Find(&back, 20261106) && DailyLog_Find(&back, 20261106)->best == 700 &&
              !DailyLog_Find(&back, 20261105) && !DailyLog_Find(&back, 20261399),
          "daily log: a line it does not know, or a day that is not one, is skipped; a longer line is read");
    f = fopen(path, "r");
    char first[128] = "";
    if (f) { if (!fgets(first, sizeof(first), f)) first[0] = '\0'; fclose(f); }
    CHECK(first[0] == '#', "daily log: the file starts with a comment naming it");

    /* A full log forgets its oldest day. */
    DailyLog full;
    DailyLog_Clear(&full);
    int day0 = Daily_DayNumber(20200101);
    for (int i = 0; i < DAILY_MAX_DAYS; i++) DailyLog_Add(&full, Daily_DateOfDay(day0 + i), 1);
    CHECK(full.count == DAILY_MAX_DAYS && full.day[0].date == 20200101, "daily log: full");
    DailyLog_Add(&full, Daily_DateOfDay(day0 + DAILY_MAX_DAYS), 1);
    CHECK(full.count == DAILY_MAX_DAYS && full.day[0].date == Daily_DateOfDay(day0 + 1) &&
              full.day[DAILY_MAX_DAYS - 1].date == Daily_DateOfDay(day0 + DAILY_MAX_DAYS),
          "daily log: a day past a full log drops the oldest");
    DailyLog_Add(&full, 20191231, 1);
    CHECK(full.count == DAILY_MAX_DAYS && full.day[0].date == Daily_DateOfDay(day0 + 1), "daily log: a day older than everything kept is not added");
    CHECK(DailyLog_Streak(&full, Daily_DateOfDay(day0 + DAILY_MAX_DAYS)) == DAILY_MAX_DAYS, "daily log: a streak as long as the log");
    RemoveData("blockfall/daily.dat");
}

/* --------------------------------------------------------- around a run */

static void test_around(void) {
    /* The panel: Ultra's, in every twist's mode. */
    Game g = Daily(DateWith(TWIST_POWER, 20261020));
    HudSpec s = Layout_SpecFor(&g, true);
    CHECK(s.stat[0] == STAT_TIME_LEFT && s.stat[1] == STAT_SCORE && s.stat[2] == STAT_LINES && s.stat[3] == STAT_BEST && s.mainChars == 4 && s.pipsStat < 0,
          "daily panel: the clock first, as Ultra's, on the Power twist");
    CHECK(s.bars, "daily panel: Power Ultra keeps the Freeze and Slow bars");
    Game dig = Daily(DateWith(TWIST_DIG, 20261020));
    s = Layout_SpecFor(&dig, true);
    CHECK(s.stat[0] == STAT_TIME_LEFT && s.stat[2] == STAT_DUG && s.rise, "daily panel: Dig on the clock shows the clock, Dug and the Next row bar");
    HudSpec plain = Layout_SpecFor(&g, true);
    g.daily.on = false;
    HudSpec power = Layout_SpecFor(&g, true);
    g.daily.on = true;
    CHECK(power.stat[0] == STAT_SCORE && plain.stat[0] == STAT_TIME_LEFT, "daily panel: a normal Power run's panel is unchanged");

    /* The music follows the clock, the warning and the ticks come. */
    g.timeLeft = 60.0f;
    float mid = Danger_Intensity(&g, 20, false);
    g.timeLeft = 5.0f;
    float late = Danger_Intensity(&g, 20, false);
    CHECK(late == 1.0f && mid > 0.5f && mid < 0.75f, "daily music: full in the last ten seconds, as Ultra's");
    g.timeLeft = 60.0f;
    SoundPlan sp;
    SoundPlan_Reset(&sp, &g);
    SoundEvents none;
    memset(&none, 0, sizeof(none));
    SoundPlan_React(&sp, &g, &none);
    SoundCue cues[SOUND_CUES];
    SoundPlan_Take(&sp, cues, SOUND_CUES);
    g.timeLeft = 9.9f;
    SoundPlan_React(&sp, &g, &none);
    int n = SoundPlan_Take(&sp, cues, SOUND_CUES);
    bool warned = false;
    for (int i = 0; i < n; i++) if (cues[i].how == SND_JINGLE && cues[i].id == JINGLE_WARNING) warned = true;
    CHECK(warned, "daily sound: WARNING at ten seconds, as Ultra's");
    g.timeLeft = 4.9f;
    SoundPlan_React(&sp, &g, &none);
    n = SoundPlan_Take(&sp, cues, SOUND_CUES);
    bool ticked = false;
    for (int i = 0; i < n; i++) if (cues[i].how == SND_PITCHED && cues[i].id == SFX_MOVE) ticked = true;
    CHECK(ticked, "daily sound: the last five seconds tick");
    HudFx h;
    HudFx_Reset(&h, &g);
    g.timeLeft = 3.9f;
    HudFx_React(&h, &g, false);
    CHECK(h.nudge[STAT_TIME_LEFT] == 0.0f, "daily hud: the clock nudges each of its last seconds");

    /* The results card and the pause menu. */
    g.piecesLocked = 12;
    g.score = 2400;
    g.won = true;
    g.phase = GS_GAMEOVER;
    RunFacts f = {false, 1800, 1, 3, 2, NULL, "Classic 10x20"};
    ResultsCard c;
    Results_Build(&c, &g, &f);
    CHECK(strcmp(c.title, "TIME UP") == 0 && strcmp(c.sub, "Daily: Power Ultra on Classic 10x20") == 0 && c.daily && c.newBest &&
              strcmp(c.bestLine, "Old best 1 800") == 0 && strcmp(c.rankLine, "#1 on this machine    World: would be #2") == 0,
          "daily card: Daily and the twist, the day's best and the world's place");
    f.best = 0;
    f.worldRows = 0;
    Results_Build(&c, &g, &f);
    CHECK(strcmp(c.bestLine, "No best before this one") == 0 && strstr(c.rankLine, "World: waits for a sync"), "daily card: the first run of the day");
    f.best = 5000;
    Results_Build(&c, &g, &f);
    CHECK(strcmp(c.bestLine, "Best 5 000, 2 600 to go") == 0, "daily card: short of today's best");
    Game wide = Daily(DateWith(TWIST_WIDE, 20261020));
    wide.piecesLocked = 1;
    wide.score = 10;
    Results_Build(&c, &wide, &f);
    CHECK(strcmp(c.sub, "Daily: Ultra on Wide 14x20") == 0, "daily card: Ultra on Wide says it once");
    Game tiny = Daily(DateWith(TWIST_TINY, 20261020));
    Results_Build(&c, &tiny, &f);
    CHECK(strcmp(c.sub, "Daily: Ultra on Tiny 6x14") == 0, "daily card: Tiny");
    Results r;
    Results_Begin(&r, &c, true);
    KitList l;
    Results_Layout(&r, &l);
    const KitItem *setup = Kit_Get(&l, RI_SETUP), *scores = Kit_Get(&l, RI_SCORES);
    CHECK(setup && strcmp(setup->label, "Daily") == 0 && scores && strcmp(scores->label, "Scores") == 0, "daily card: Change setup reads Daily");
    c.daily = false;
    Results_Begin(&r, &c, true);
    Results_Layout(&r, &l);
    CHECK(strcmp(Kit_Get(&l, RI_SETUP)->label, "Change setup") == 0, "daily card: a normal card keeps Change setup");
    char title[64], body[96], act[24];
    g.phase = GS_PLAYING;
    g.won = false;
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(body, "It counts for 2 400 on today's daily board.") == 0 && PauseMenu_ShouldAsk(&g), "daily pause: what leaving counts for");
    g.daily.on = false;
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(body, "It counts for 2 400 on Classic 10x20.") == 0, "daily pause: a normal run's words are unchanged");
    g.daily.on = true;

    /* The words on the menu and over the well. */
    char words[64];
    Daily_MenuWords(TWIST_WIDE, 12345, 3, words, sizeof(words));
    CHECK(strcmp(words, "Ultra on Wide, best 12 345, streak 3") == 0, "daily menu: the twist, your best, the streak");
    Daily_MenuWords(TWIST_DIG, 0, 0, words, sizeof(words));
    CHECK(strcmp(words, "Dig on the clock, no run yet") == 0, "daily menu: no run yet and no streak");
    Daily_MenuWords(TWIST_FAST, 0, 12, words, sizeof(words));
    CHECK(strcmp(words, "Fast start, no run yet, streak 12") == 0, "daily menu: a streak with no run yet today");
    Daily_PlayTag(&g, words, sizeof(words));
    CHECK(strcmp(words, "DAILY  Power Ultra") == 0, "daily tag: on the classic board, no size");
    Daily_PlayTag(&wide, words, sizeof(words));
    CHECK(strcmp(words, "DAILY  Ultra on Wide  14x20") == 0, "daily tag: the size off the classic board");
    CHECK(Hud_PassesBest(2000, 1800, UNIT_POINTS, MODE_POWER, true, false), "daily hud: passing the day's best is a NEW BEST like any other");
}

void test_daily_all(void) {
    test_days();
    test_twist();
    test_same_pieces();
    test_rules();
    test_records();
    test_log();
    test_around();
}
