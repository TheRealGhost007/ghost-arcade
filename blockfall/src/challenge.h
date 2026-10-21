#ifndef CHALLENGE_H
#define CHALLENGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"

/* The Daily challenge (3.6.0, REWORK_PLAN.md "Daily challenge"), with no
 * raylib so it is tested headlessly; scr_daily.c draws it.
 *  - Everyone gets the same pieces (and power-ups) today: the run is seeded
 *    from the shared daily code (ghost-common/daily.h: Daily_Seed, the same
 *    value all day, different every day, as the seven daily games use it)
 *    and goes on the day's own board, "daily-YYYYMMDD" (Daily_Mode), the
 *    shared table Ghost Launcher's desk and ghost-sync read.
 *  - A two-minute score attack with a twist the date decides (Daily_TwistOf,
 *    from the date alone, so everyone gets the same one: every twist once
 *    a week, in an order the week's own daily seed shuffles, and never the
 *    same twist two days running): Ultra on the classic board, Ultra on
 *    Wide, Power Ultra (power-ups on the clock), Tall, Tiny, a fast start
 *    (from level 8) and Dig on the clock.
 *  - Retry as often as you like; your best counts, and a streak shows: the
 *    days in a row with a daily played, kept in Blockfall's own file
 *    ($XDG_DATA_HOME/blockfall/daily.dat, a line per day: the day, your
 *    best and the runs). It lives until a whole day passes without one, as
 *    Ghost Launcher's desk counts it. The game's runs.log carries the
 *    day's board as its mode, so the desk counts Blockfall's dailies too.
 *  - Nothing here touches the normal boards: the key is the day's. */

typedef enum { TWIST_CLASSIC, TWIST_WIDE, TWIST_POWER, TWIST_TALL, TWIST_TINY, TWIST_FAST, TWIST_DIG, TWIST_COUNT } DailyTwist;
#define DAILY_FAST_LEVEL 8
#define DAILY_FILE "daily.dat"
#define DAILY_MAX_DAYS 1024 /* days with a daily played: nearly three years */
#define DAILY_TOP 5         /* rows of today's board the Daily screen shows, this machine and the world */

/* --- the day --- */
/* "daily-20261020" to 20261020; 0 for anything else. */
int Daily_DateFromMode(const char *mode);
/* "daily-20261020": the day's board key, as Daily_ModeForDate names it. */
void Daily_BoardKey(int date, char *out, size_t size);
/* A date (YYYYMMDD) is a real calendar day. */
bool Daily_DateValid(int date);
/* Days since 1970-01-01 for a date, and back, so yesterday is plain
 * arithmetic across month and year ends. */
int Daily_DayNumber(int date);
int Daily_DateOfDay(int dayNumber);
/* "Tuesday 20 October". */
void Daily_DateWords(int date, char *out, size_t size);

/* --- the twist --- */
/* The twist for a day (YYYYMMDD): each of the seven once a week (weeks
 * begin on day 0, 1 January 1970, a Thursday), in an order the week's first
 * day's daily seed shuffles; a week never opens with the twist the one
 * before ended on, so no twist comes two days running. */
DailyTwist Daily_TwistOf(int date);
/* "Ultra on Wide". */
const char *Daily_TwistName(DailyTwist t);
/* What it is, in a sentence. */
const char *Daily_TwistAbout(DailyTwist t);
/* The board and mode it plays on, and the rules the run bends. */
GameConfig Daily_Config(DailyTwist t);
DailyRules Daily_Rules(int date, DailyTwist t);

/* --- the file: the days you played --- */
typedef struct {
    int date;   /* YYYYMMDD */
    long best;  /* your best that day on this machine */
    int runs;   /* dailies played that day */
} DailyDay;

typedef struct {
    DailyDay day[DAILY_MAX_DAYS]; /* by date, each once */
    int count;
} DailyLog;

void DailyLog_Clear(DailyLog *l);
/* Reads the file; false (and *l cleared) when there is none yet. */
bool DailyLog_Load(DailyLog *l);
bool DailyLog_Save(const DailyLog *l);
/* A daily run ended on `date` with `score`: one more run that day, and the
 * best if it is. A full log forgets its oldest day first. */
void DailyLog_Add(DailyLog *l, int date, long score);
/* That day's line, or NULL. */
const DailyDay *DailyLog_Find(const DailyLog *l, int date);
/* Days in a row up to today, or up to yesterday while today is still open
 * (0 once a whole day has passed without one). */
int DailyLog_Streak(const DailyLog *l, int today);
/* The longest run of days ever, up to today. */
int DailyLog_BestStreak(const DailyLog *l, int today);

/* --- words --- */
/* The main menu's Daily item: its second line, "Ultra on Wide, best
 * 12 345, streak 3" ("no run yet" with no best today; no streak part at 0). */
void Daily_MenuWords(DailyTwist t, long bestToday, int streak, char *out, size_t size);
/* The tag over the well: "DAILY  Ultra on Wide  14x20" (no size on the
 * classic board). */
void Daily_PlayTag(const Game *g, char *out, size_t size);
/* The results card's second line: "Daily: Ultra on Wide 14x20". */
void Daily_CardSub(DailyTwist t, char *out, size_t size);

#endif
