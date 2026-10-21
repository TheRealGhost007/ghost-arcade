#include "challenge.h"
#include "menumodel.h"
#include "persist.h"
#include "safefile.h"
#include "daily.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------- days */

static void Split(int date, int *y, int *m, int *d) {
    *y = date / 10000;
    *m = (date / 100) % 100;
    *d = date % 100;
}

/* Howard Hinnant's days-from-civil, as Ghost Launcher's desk counts days:
 * exact for every date, leap years and the century rule included. */
static int DayOf(int year, int month, int day) {
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void Civil(int dayNumber, int *year, int *month, int *day) {
    int z = dayNumber + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int m = mp < 10 ? mp + 3 : mp - 9;
    *year = yoe + era * 400 + (m <= 2);
    *month = m;
    *day = doy - (153 * mp + 2) / 5 + 1;
}

bool Daily_DateValid(int date) {
    int y, m, d;
    Split(date, &y, &m, &d);
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    int cy, cm, cd;
    Civil(DayOf(y, m, d), &cy, &cm, &cd);
    return cy == y && cm == m && cd == d; /* 31 April, 29 Feb 2026 */
}

int Daily_DayNumber(int date) {
    int y, m, d;
    Split(date, &y, &m, &d);
    return DayOf(y, m, d);
}

int Daily_DateOfDay(int dayNumber) {
    int y, m, d;
    Civil(dayNumber, &y, &m, &d);
    return y * 10000 + m * 100 + d;
}

int Daily_DateFromMode(const char *mode) {
    if (!mode || strncmp(mode, "daily-", 6) != 0 || strlen(mode) != 14) return 0;
    for (int i = 6; i < 14; i++) if (mode[i] < '0' || mode[i] > '9') return 0;
    int date = atoi(mode + 6);
    return Daily_DateValid(date) ? date : 0;
}

void Daily_BoardKey(int date, char *out, size_t size) {
    int y, m, d;
    Split(date, &y, &m, &d);
    char mode[32];
    Daily_ModeForDate(y, m, d, mode, (int)sizeof(mode));
    snprintf(out, size, "%s", mode);
}

void Daily_DateWords(int date, char *out, size_t size) {
    static const char *const kDays[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    static const char *const kMonths[12] = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
    if (!Daily_DateValid(date)) { snprintf(out, size, "today"); return; }
    int y, m, d;
    Split(date, &y, &m, &d);
    int n = DayOf(y, m, d);
    int weekday = ((n % 7) + 7 + 3) % 7; /* 1 January 1970 was a Thursday */
    snprintf(out, size, "%s %d %s", kDays[weekday], d, kMonths[m - 1]);
}

/* ---------------------------------------------------------------- twist */

typedef struct { const char *name; const char *about; GameMode mode; int w, h; bool power; int level; } TwistDef;
static const TwistDef kTwists[TWIST_COUNT] = {
    [TWIST_CLASSIC] = {"Ultra", "Two minutes on the classic 10x20 board: as many points as you can.", MODE_ULTRA, 10, 20, false, 1},
    [TWIST_WIDE] = {"Ultra on Wide", "Two minutes on the 14x20 board: more room, longer rows, more to stack.", MODE_ULTRA, 14, 20, false, 1},
    [TWIST_POWER] = {"Power Ultra", "Two minutes with power-ups on the pieces: everyone gets the same ones today.", MODE_POWER, 10, 20, true, 1},
    [TWIST_TALL] = {"Tall", "Two minutes on the narrow 8x26 board: deep, so dig in and keep it low.", MODE_ULTRA, 8, 26, false, 1},
    [TWIST_TINY] = {"Tiny", "Two minutes on the tiny 6x14 board: every piece counts.", MODE_ULTRA, 6, 14, false, 1},
    [TWIST_FAST] = {"Fast start", "Two minutes on the classic board from level 8: fast from the first piece.", MODE_ULTRA, 10, 20, false, DAILY_FAST_LEVEL},
    [TWIST_DIG] = {"Dig on the clock", "Two minutes of Dig: the rows rise, every garbage row dug pays, and the clock ends it.", MODE_DIG, 10, 20, false, 1},
};

static int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* A week's running order: the seven twists shuffled with the daily seed of
 * the week's first day (the same shuffle Ghost Launcher's desk uses for
 * its featured game, so the two never need to agree on anything else). */
static void WeekOrder(int week, int order[TWIST_COUNT]) {
    int y, m, d;
    Civil(week * 7, &y, &m, &d);
    uint64_t seed = Daily_SeedForDate(y, m, d);
    for (int i = 0; i < TWIST_COUNT; i++) order[i] = i;
    for (int i = TWIST_COUNT - 1; i > 0; i--) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        int j = (int)((seed >> 33) % (uint64_t)(i + 1));
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
}

DailyTwist Daily_TwistOf(int date) {
    if (!Daily_DateValid(date)) return TWIST_CLASSIC;
    int day = Daily_DayNumber(date);
    int week = FloorDiv(day, 7), order[TWIST_COUNT], before[TWIST_COUNT];
    WeekOrder(week, order);
    WeekOrder(week - 1, before); /* its last twist never moves: only the first two ever swap */
    if (order[0] == before[TWIST_COUNT - 1]) {
        order[0] = order[1];
        order[1] = before[TWIST_COUNT - 1];
    }
    return (DailyTwist)order[day - week * 7];
}

static const TwistDef *Def(DailyTwist t) { return ((int)t >= 0 && t < TWIST_COUNT) ? &kTwists[t] : &kTwists[TWIST_CLASSIC]; }

const char *Daily_TwistName(DailyTwist t) { return Def(t)->name; }
const char *Daily_TwistAbout(DailyTwist t) { return Def(t)->about; }

GameConfig Daily_Config(DailyTwist t) {
    const TwistDef *d = Def(t);
    return (GameConfig){d->mode, d->w, d->h, true};
}

DailyRules Daily_Rules(int date, DailyTwist t) {
    const TwistDef *d = Def(t);
    DailyRules r;
    memset(&r, 0, sizeof(r));
    r.on = true;
    r.date = date;
    r.twist = (int)t;
    r.powerUps = d->power;
    r.startLevel = d->level;
    return r;
}

/* ----------------------------------------------------------------- file */

void DailyLog_Clear(DailyLog *l) { l->count = 0; }

/* The first place whose day is not before date. */
static int LowerBound(const DailyLog *l, int date) {
    int lo = 0, hi = l->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (l->day[mid].date < date) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

const DailyDay *DailyLog_Find(const DailyLog *l, int date) {
    int at = LowerBound(l, date);
    return (at < l->count && l->day[at].date == date) ? &l->day[at] : NULL;
}

static DailyDay *Line(DailyLog *l, int date) {
    int at = LowerBound(l, date);
    if (at < l->count && l->day[at].date == date) return &l->day[at];
    if (l->count == DAILY_MAX_DAYS) {
        if (at == 0) return NULL; /* older than everything kept */
        memmove(&l->day[0], &l->day[1], (size_t)(at - 1) * sizeof(l->day[0]));
        at--;
    } else {
        memmove(&l->day[at + 1], &l->day[at], (size_t)(l->count - at) * sizeof(l->day[0]));
        l->count++;
    }
    l->day[at] = (DailyDay){date, 0, 0};
    return &l->day[at];
}

void DailyLog_Add(DailyLog *l, int date, long score) {
    if (!Daily_DateValid(date)) return;
    DailyDay *d = Line(l, date);
    if (!d) return;
    d->runs++;
    if (score > d->best) d->best = score;
}

bool DailyLog_Load(DailyLog *l) {
    DailyLog_Clear(l);
    char path[600];
    if (!Persist_DataFile(DAILY_FILE, path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int date = 0, runs = 0;
        long best = 0;
        if (sscanf(line, "%d|%ld|%d", &date, &best, &runs) < 2 || !Daily_DateValid(date)) continue; /* a line this build does not know */
        DailyDay *d = Line(l, date);
        if (!d) continue;
        d->best = best < 0 ? 0 : best;
        d->runs = runs < 1 ? 1 : runs;
    }
    fclose(f);
    return true;
}

bool DailyLog_Save(const DailyLog *l) {
    char path[600];
    if (!Persist_DataFile(DAILY_FILE, path, sizeof(path))) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Blockfall Daily challenge: the days a daily was played on this machine (3.6.0): day|your best|runs\n");
    for (int i = 0; i < l->count; i++) fprintf(f, "%d|%ld|%d\n", l->day[i].date, l->day[i].best, l->day[i].runs);
    return SafeFile_Close(f);
}

int DailyLog_Streak(const DailyLog *l, int today) {
    int todayN = Daily_DayNumber(today);
    int end = DailyLog_Find(l, today) ? todayN : todayN - 1;
    int at = LowerBound(l, Daily_DateOfDay(end));
    if (at >= l->count || l->day[at].date != Daily_DateOfDay(end)) return 0;
    int streak = 1;
    while (at > 0 && Daily_DayNumber(l->day[at - 1].date) == Daily_DayNumber(l->day[at].date) - 1) {
        streak++;
        at--;
    }
    return streak;
}

int DailyLog_BestStreak(const DailyLog *l, int today) {
    int best = 0, run = 0, prev = 0;
    bool any = false;
    for (int i = 0; i < l->count && l->day[i].date <= today; i++) {
        int n = Daily_DayNumber(l->day[i].date);
        run = (any && n == prev + 1) ? run + 1 : 1;
        prev = n;
        any = true;
        if (run > best) best = run;
    }
    return best;
}

/* ---------------------------------------------------------------- words */

void Daily_MenuWords(DailyTwist t, long bestToday, int streak, char *out, size_t size) {
    char best[40];
    if (bestToday > 0) {
        char n[24];
        Menu_Group(bestToday, n, sizeof(n));
        snprintf(best, sizeof(best), "best %s", n);
    } else {
        snprintf(best, sizeof(best), "no run yet");
    }
    if (streak > 0) snprintf(out, size, "%s, %s, streak %d", Daily_TwistName(t), best, streak);
    else snprintf(out, size, "%s, %s", Daily_TwistName(t), best);
}

void Daily_PlayTag(const Game *g, char *out, size_t size) {
    const char *name = Daily_TwistName((DailyTwist)g->daily.twist);
    if (g->cfg.width == BOARD_W && g->cfg.height == BOARD_H) snprintf(out, size, "DAILY  %s", name);
    else snprintf(out, size, "DAILY  %s  %dx%d", name, g->cfg.width, g->cfg.height);
}

void Daily_CardSub(DailyTwist t, char *out, size_t size) {
    const TwistDef *d = Def(t);
    char board[40];
    Menu_BoardName(d->w, d->h, board, sizeof(board));
    /* "Ultra on Wide on Wide 14x20" would say it twice */
    const char *name = (t == TWIST_WIDE || t == TWIST_TALL || t == TWIST_TINY) ? "Ultra" : d->name;
    snprintf(out, size, "Daily: %s on %s", name, board);
}
