#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "desk.h"
#include "screens.h"
#include "keymap.h"
#include "runstats.h"
#include "daily.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------- days */

/* Howard Hinnant's days-from-civil: exact for every date, leap years and
 * the century rule included, with no time zone to trip over. */
int Desk_DayNumber(int year, int month, int day) {
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void Desk_Civil(int dayNumber, int *year, int *month, int *day) {
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

bool Desk_ParseMode(const char *mode, int *dayNumber) {
    if (!Boards_IsDaily(mode)) return false;
    int y = 0, m = 0, d = 0;
    for (int i = 6; i < 10; i++) y = y * 10 + (mode[i] - '0');
    for (int i = 10; i < 12; i++) m = m * 10 + (mode[i] - '0');
    for (int i = 12; i < 14; i++) d = d * 10 + (mode[i] - '0');
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    int n = Desk_DayNumber(y, m, d), cy, cm, cd;
    Desk_Civil(n, &cy, &cm, &cd);
    if (cy != y || cm != m || cd != d) return false; /* 31 April, 29 Feb 2026 */
    *dayNumber = n;
    return true;
}

void Desk_ModeOf(int dayNumber, char *out, size_t size) {
    int y, m, d;
    Desk_Civil(dayNumber, &y, &m, &d);
    char mode[32]; /* room for any year the compiler can imagine */
    Daily_ModeForDate(y, m, d, mode, (int)sizeof(mode));
    snprintf(out, size, "%s", mode);
}

int Desk_Today(time_t now) {
    struct tm t;
    if (!localtime_r(&now, &t)) return 0;
    return Desk_DayNumber(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

int Desk_Weekday(int dayNumber) { return ((dayNumber % 7) + 7 + 3) % 7; /* 1 January 1970 was a Thursday */ }

void Desk_DateWords(int dayNumber, char *out, size_t size) {
    static const char *const kDays[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    static const char *const kMonths[12] = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
    int y, m, d;
    Desk_Civil(dayNumber, &y, &m, &d);
    snprintf(out, size, "%s %d %s", kDays[Desk_Weekday(dayNumber)], d, kMonths[m - 1]);
}

/* ---------------------------------------------------------------- games */

static const char *const kDailyGames[] = {"brickburst", "skyraid", "rockdrift", "lanehop", "crawlshot", "moondrop", "girderclimb", "blockfall"};
#define DAILY_GAMES ((int)(sizeof(kDailyGames) / sizeof(kDailyGames[0])))

int Desk_GameCount(void) { return DAILY_GAMES; }

const char *Desk_GameSlug(int i) { return i >= 0 && i < DAILY_GAMES ? kDailyGames[i] : ""; }

bool Desk_IsDaily(const char *slug) {
    for (int i = 0; slug && i < DAILY_GAMES; i++) if (strcmp(slug, kDailyGames[i]) == 0) return true;
    return false;
}

static int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* A round's running order: the daily games shuffled with the games' own
 * daily seed for the round's first day. A round is DESK_ROUND days, one a
 * game (it was a week while there were seven). */
static void RoundOrder(int round, int order[DAILY_GAMES]) {
    int y, m, d;
    Desk_Civil(round * DESK_ROUND, &y, &m, &d);
    uint64_t seed = Daily_SeedForDate(y, m, d);
    for (int i = 0; i < DAILY_GAMES; i++) order[i] = i;
    for (int i = DAILY_GAMES - 1; i > 0; i--) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        int j = (int)((seed >> 33) % (uint64_t)(i + 1));
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
}

/* Every game once a round, in an order the date alone decides; a round
 * never opens with the game the last one ended on, so no game is featured
 * two days running. */
int Desk_Featured(int dayNumber) {
    int round = FloorDiv(dayNumber, DESK_ROUND), order[DAILY_GAMES], before[DAILY_GAMES];
    RoundOrder(round, order);
    RoundOrder(round - 1, before); /* its last game never moves: only the first two ever swap */
    if (order[0] == before[DAILY_GAMES - 1]) {
        order[0] = order[1];
        order[1] = before[DAILY_GAMES - 1];
    }
    return order[dayNumber - round * DESK_ROUND];
}

_Static_assert(DAILY_GAMES == DESK_ROUND, "a featured round is one day per daily game");

/* ------------------------------------------------------------- calendar */

void Desk_DaysClear(DeskDays *d) { d->count = 0; }

/* The first place whose day is not before dayNumber. */
static int LowerBound(const DeskDays *d, int dayNumber) {
    int lo = 0, hi = d->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (d->day[mid] < dayNumber) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool Desk_DaysHas(const DeskDays *d, int dayNumber) {
    int at = LowerBound(d, dayNumber);
    return at < d->count && d->day[at] == dayNumber;
}

void Desk_DaysAdd(DeskDays *d, int dayNumber) {
    int at = LowerBound(d, dayNumber);
    if (at < d->count && d->day[at] == dayNumber) return;
    if (d->count == DESK_MAX_DAYS) {
        if (at == 0) return; /* older than everything kept */
        memmove(&d->day[0], &d->day[1], (size_t)(at - 1) * sizeof(d->day[0]));
        d->day[at - 1] = dayNumber;
        return;
    }
    memmove(&d->day[at + 1], &d->day[at], (size_t)(d->count - at) * sizeof(d->day[0]));
    d->day[at] = dayNumber;
    d->count++;
}

/* One line: its board's day, if it is a daily up to today. 1 for today. */
static int AddRunLine(DeskDays *d, const char *line, int today) {
    const char *bar = strchr(line, '|');
    if (!bar) return 0;
    char mode[SCORES_MODE_LEN];
    size_t n = strcspn(bar + 1, "|\r\n");
    if (n >= sizeof(mode)) return 0;
    memcpy(mode, bar + 1, n);
    mode[n] = '\0';
    int day;
    if (!Desk_ParseMode(mode, &day) || day > today) return 0;
    Desk_DaysAdd(d, day);
    return day == today;
}

int Desk_DaysFromRuns(DeskDays *d, const char *text, int today) {
    int runs = 0;
    const char *p = text ? text : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[256];
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) - 1 ? n : sizeof(line) - 1), p);
        runs += AddRunLine(d, line, today);
        p = nl ? nl + 1 : p + n;
    }
    return runs;
}

/* A game's runs.log, line by line: it is kept for ever and may be long. */
static int DaysFromFile(DeskDays *d, const char *slug, int today) {
    char path[800], line[256];
    if (!RunStats_LogPath(slug, path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int runs = 0;
    while (fgets(line, sizeof(line), f)) runs += AddRunLine(d, line, today);
    fclose(f);
    return runs;
}

/* A streak lives until a whole day passes without a daily: until today is
 * over, yesterday's still counts. */
DeskStreak Desk_Streak(const DeskDays *d, int today) {
    DeskStreak s = {0, 0, Desk_DaysHas(d, today)};
    int run = 0, prev = INT_MIN;
    for (int i = 0; i < d->count && d->day[i] <= today; i++) {
        run = (prev != INT_MIN && d->day[i] == prev + 1) ? run + 1 : 1;
        prev = d->day[i];
        if (run > s.best) s.best = run;
    }
    int end = s.today ? today : today - 1;
    int at = LowerBound(d, end);
    if (at >= d->count || d->day[at] != end) return s;
    s.current = 1;
    while (at > 0 && d->day[at - 1] == d->day[at] - 1) {
        s.current++;
        at--;
    }
    return s;
}

void Desk_Dots(const DeskDays *d, int today, bool out[DESK_DOTS]) {
    for (int k = 0; k < DESK_DOTS; k++) out[k] = Desk_DaysHas(d, today - (DESK_DOTS - 1) + k);
}

/* ----------------------------------------------------------------- desk */

static bool SlugOf(const GameEntry *e, char *slug, size_t size) { return Scores_SlugFromExec(e->exec, slug, size) && Desk_IsDaily(slug); }

/* Today's board for a row: your best here, the world's #1 and your place. */
static void TodaysBoard(DeskRow *r, const char *mode, const Profiles *profiles, const char *gameName) {
    static Boards bs; /* tens of kilobytes: one, off the stack */
    const char *plays = profiles ? Profiles_GetUsername(profiles, gameName) : "";
    const char *arcade = profiles ? Profiles_GetOwnUsername(profiles, PROFILE_DEFAULT_KEY) : "";
    BoardYou you = {{plays, arcade}};
    Boards_Load(&bs, r->slug, &you, mode);
    int at = Boards_Find(&bs, mode);
    if (at < 0) return;
    const Board *b = &bs.b[at];
    if (b->localCount > 0) {
        r->hasBest = true;
        r->best = b->local[0].score;
        r->played = true;
    }
    if (b->worldCount > 0) {
        r->hasWorld = true;
        r->world = b->world[0].score;
        snprintf(r->worldName, sizeof(r->worldName), "%s", b->world[0].name);
    }
    r->rank = b->worldRank;
}

/* Every daily game in the catalog adds to the calendar, each program once;
 * runs[] holds today's runs per catalog index. */
static void ReadCalendar(DeskDays *days, const Manifest *m, int today, int runs[MANIFEST_MAX_GAMES]) {
    for (int i = 0; m && i < m->count; i++) {
        char slug[SCORES_SLUG_LEN], other[SCORES_SLUG_LEN];
        runs[i] = 0;
        if (!SlugOf(&m->games[i], slug, sizeof(slug))) continue;
        int seen = -1;
        for (int j = 0; j < i && seen < 0; j++) if (SlugOf(&m->games[j], other, sizeof(other)) && strcmp(slug, other) == 0) seen = j;
        runs[i] = seen >= 0 ? runs[seen] : DaysFromFile(days, slug, today);
    }
}

void Desk_Load(Desk *d, const DeskSource *src) {
    memset(d, 0, sizeof(*d));
    d->today = Desk_Today(src->now);
    Desk_ModeOf(d->today, d->mode, sizeof(d->mode));
    d->featured = Desk_Featured(d->today);
    static DeskDays days; /* sixteen kilobytes: off the stack */
    int runs[MANIFEST_MAX_GAMES] = {0};
    Desk_DaysClear(&days);
    const Manifest *m = src->manifest;
    ReadCalendar(&days, m, d->today, runs);
    int n = m ? (src->lineup ? src->lineup->count : m->count) : 0;
    for (int place = 0; place < n && d->count < DESK_MAX_ROWS; place++) {
        int i = src->lineup ? src->lineup->index[place] : place;
        if (i < 0 || i >= m->count) continue;
        DeskRow *r = &d->rows[d->count];
        memset(r, 0, sizeof(*r));
        if (!SlugOf(&m->games[i], r->slug, sizeof(r->slug))) continue;
        r->index = i;
        snprintf(r->name, sizeof(r->name), "%.39s", m->games[i].name);
        r->installed = src->installed ? src->installed[i] : true;
        r->featured = strcmp(r->slug, Desk_GameSlug(d->featured)) == 0;
        r->runsToday = runs[i];
        r->played = runs[i] > 0;
        TodaysBoard(r, d->mode, src->profiles, m->games[i].name);
        if (r->played) {
            Desk_DaysAdd(&days, d->today); /* a score on today's board with no runs.log line still counts */
            d->played++;
        }
        d->count++;
    }
    d->streak = Desk_Streak(&days, d->today);
    Desk_Dots(&days, d->today, d->dots);
}

void Desk_BoardLine(const Desk *d, char *out, size_t size) {
    if (d->count == 0) snprintf(out, size, "%s", "");
    else snprintf(out, size, "%d of %d today", d->played, d->count);
}

int Desk_StartRow(const Desk *d, int catalogIndex) {
    int featured = -1, first = -1;
    for (int r = 0; r < d->count; r++) {
        if (!d->rows[r].installed) continue;
        if (d->rows[r].index == catalogIndex) return r;
        if (d->rows[r].featured && featured < 0) featured = r;
        if (first < 0) first = r;
    }
    return featured >= 0 ? featured : first;
}

/* --------------------------------------------------------------- screen */

#define NOT_INSTALLED "Not installed. Install it from its machine first."

void Desk_Layout(const Desk *d, const KitScroll *sc, KitList *l) {
    Kit_Clear(l, 0);
    Screens_AddBack(l);
    for (int k = 0; k < DESK_VISIBLE && sc->top + k < d->count; k++) {
        int r = sc->top + k;
        KitRect rect = {DESK_X, (float)(DESK_ROW_Y + k * DESK_ROW_PITCH), DESK_RIGHT - DESK_X, DESK_ROW_H};
        KitItem *it = Kit_Add(l, WID_DESK_ROW + r, KIT_ROW, rect, d->rows[r].name);
        if (!it) continue;
        it->flags |= KIT_SELFDRAWN; /* render.c draws its columns */
        if (!d->rows[r].installed) Kit_Disable(it, NOT_INSTALLED);
    }
    Screens_AddClose(l);
}

int Desk_RowOf(int widgetId) {
    int r = widgetId - WID_DESK_ROW;
    return r >= 0 && r < DESK_MAX_ROWS ? r : -1;
}

/* The next row that can be played that way, round the ends; -1 for none. */
static int NextRow(const Desk *d, int from, int dir) {
    for (int step = 1; step <= d->count; step++) {
        /* With no row yet, from the end the press points away from. */
        int r = from < 0 ? (dir > 0 ? step - 1 : d->count - step) : ((from + dir * step) % d->count + d->count) % d->count;
        if (d->rows[r].installed) return r;
    }
    return -1;
}

/* The wheel moved the list: keys that were on a row now out of sight come
 * to the nearest row still shown. */
static void KeepInSight(const Desk *d, const KitScroll *sc, KitState *k) {
    int at = Desk_RowOf(k->focus);
    if (at < 0 || (at >= sc->top && at < sc->top + DESK_VISIBLE)) return;
    int want = at < sc->top ? sc->top : sc->top + DESK_VISIBLE - 1, best = -1;
    for (int r = sc->top; r < sc->top + DESK_VISIBLE && r < d->count; r++) {
        if (!d->rows[r].installed) continue;
        if (best < 0 || (r > want ? r - want : want - r) < (best > want ? best - want : want - best)) best = r;
    }
    if (best >= 0) k->focus = WID_DESK_ROW + best;
}

bool Desk_Navigate(const Desk *d, KitScroll *sc, KitState *k, const Input *in, bool wheelFree) {
    if (d->count == 0) return false;
    int dir = Keymap_Pressed(KA_UI_DOWN, in) ? 1 : (Keymap_Pressed(KA_UI_UP, in) ? -1 : 0);
    if (dir != 0) {
        int at = Desk_RowOf(k->focus), to = NextRow(d, at, dir);
        if (to < 0 || to == at) return false;
        k->focus = WID_DESK_ROW + to;
        Kit_ScrollShow(sc, to, d->count, DESK_VISIBLE);
        return true;
    }
    if (!wheelFree || !Kit_ScrollWheel(sc, d->count, DESK_VISIBLE, in->wheel)) return false;
    int before = k->focus;
    KeepInSight(d, sc, k);
    return k->focus != before;
}

void Desk_Help(const Desk *d, int row, char *out, size_t size) {
    out[0] = '\0';
    if (row < 0 || row >= d->count) return;
    const DeskRow *r = &d->rows[row];
    if (!r->installed) snprintf(out, size, "%s", NOT_INSTALLED);
    else if (r->played) snprintf(out, size, "Back to the hall, where the coin drops for another go at today's %.20s.", r->name);
    else snprintf(out, size, "Back to the hall, where the coin drops for today's %.20s.", r->name);
}

void Desk_Hint(InputDevice hints, char *out, size_t size) {
    bool pad = hints == DEV_PAD;
    InputDevice dev = pad ? DEV_PAD : DEV_KEYBOARD;
    char press[24], back[24];
    Keymap_Hint(KA_UI_PRESS, dev, press, sizeof(press));
    Keymap_Hint(KA_BACK, dev, back, sizeof(back));
    snprintf(out, size, "%s choose   %s play its daily   %s back", pad ? "D-pad" : "Up/Down", press, back);
}
