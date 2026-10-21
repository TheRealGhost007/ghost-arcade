#ifndef DESK_H
#define DESK_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "kit.h"
#include "input.h"
#include "boards.h"
#include "manifest.h"
#include "lineup.h"
#include "profile.h"

/* The Daily challenge desk, without raylib: which games have a daily, the
 * days you played one (for the streak and the last fourteen days), the game
 * featured today, and a row for each daily game on your row: played today,
 * your best today, the world's best today and your place on that board.
 * Everything comes from files the games and ghost-sync already write: each
 * game's runs.log (kept for ever, so a streak can run past two weeks) and
 * its local and world score files (today's daily board). It opens from the
 * Arcade menu, Shift+D, a machine page's Daily challenge button and the
 * chalkboard on the hall's wall; D on the hall still starts the lit
 * machine's daily at once. render.c draws it; main.c does what it asks. */

#define DESK_DOTS 14          /* the last fourteen days, today last */
#define DESK_MAX_DAYS 4096    /* days with a daily played: about eleven years */
#define DESK_MAX_ROWS 16
#define DESK_VISIBLE 8        /* the eight daily games at once (Blockfall joined in its 3.6.0); more would scroll */
#define DESK_ROUND 8          /* days in a featured round: every daily game once */

/* --- days ---
 * A day is a whole number of days since 1970-01-01 in the calendar, so
 * month and year ends, leap days and gaps are plain arithmetic. */
int Desk_DayNumber(int year, int month, int day);
void Desk_Civil(int dayNumber, int *year, int *month, int *day);
/* "daily-YYYYMMDD" naming a real date: its day. */
bool Desk_ParseMode(const char *mode, int *dayNumber);
void Desk_ModeOf(int dayNumber, char *out, size_t size);
/* Today in local time, as the games date their dailies. */
int Desk_Today(time_t now);
/* 0 Monday .. 6 Sunday. */
int Desk_Weekday(int dayNumber);
/* "Wednesday 14 October". */
void Desk_DateWords(int dayNumber, char *out, size_t size);

/* --- the daily games ---
 * The games whose seed changes what you play, so a shared daily seed means
 * something; by program name. */
int Desk_GameCount(void);
const char *Desk_GameSlug(int i);
bool Desk_IsDaily(const char *slug);
/* The game featured on a day (a Desk_GameSlug index): from the date alone,
 * so everyone sees the same one whatever their row holds. Each game has a
 * day in every round of DESK_ROUND days (rounds begin on day 0; eight
 * games, so a week was one game short), and none two days running. */
int Desk_Featured(int dayNumber);

/* --- the days you played a daily --- */
typedef struct {
    int day[DESK_MAX_DAYS]; /* sorted, each once */
    int count;
} DeskDays;

void Desk_DaysClear(DeskDays *d);
/* A full calendar forgets its oldest day first. */
void Desk_DaysAdd(DeskDays *d, int dayNumber);
bool Desk_DaysHas(const DeskDays *d, int dayNumber);
/* One runs.log's text ("YYYY-MM-DD HH:MM:SS|mode|score|level" lines): the
 * day of every daily run, by its board's date, up to today (a clock that
 * was wrong once never adds a day to come). Returns today's daily runs. */
int Desk_DaysFromRuns(DeskDays *d, const char *text, int today);

typedef struct {
    int current;  /* days in a row up to today, or up to yesterday while today is still open */
    int best;     /* the longest run of days ever */
    bool today;   /* a daily is played today */
} DeskStreak;

DeskStreak Desk_Streak(const DeskDays *d, int today);
/* out[0] is thirteen days ago, out[DESK_DOTS - 1] today. */
void Desk_Dots(const DeskDays *d, int today, bool out[DESK_DOTS]);

/* --- the desk --- */
typedef struct {
    int index;                  /* catalog index */
    char name[40];
    char slug[SCORES_SLUG_LEN];
    bool installed, featured;
    int runsToday;              /* today's daily runs in its runs.log */
    bool played;                /* ...or a score on today's local board */
    bool hasBest, hasWorld;
    long best;                  /* your best on today's board, this computer */
    long world;                 /* the world's #1 today */
    char worldName[BOARD_NAME_LEN];
    int rank;                   /* your place on today's world board, 0 = not on it */
} DeskRow;

typedef struct {
    int today;
    char mode[24];              /* today's board: "daily-YYYYMMDD" */
    DeskStreak streak;
    bool dots[DESK_DOTS];
    int featured;               /* Desk_GameSlug index */
    DeskRow rows[DESK_MAX_ROWS];
    int count;                  /* daily games on your row, in its order */
    int played;                 /* ...played today */
} Desk;

typedef struct {
    const Manifest *manifest;
    const Lineup *lineup;     /* the row: which machines, in which order (NULL = the catalog) */
    const bool *installed;    /* per catalog index (NULL = all) */
    const Profiles *profiles; /* your names, to find you on the world board (NULL = none) */
    time_t now;
} DeskSource;

/* Reads every daily game's runs.log (the streak counts games hidden from
 * the row too) and today's boards of the ones on the row. */
void Desk_Load(Desk *d, const DeskSource *src);
/* "2 of 7 today" for the hall's chalkboard; "" when no daily game is on
 * the row (then there is no chalkboard). */
void Desk_BoardLine(const Desk *d, char *out, size_t size);
/* Where the desk opens: on this machine's row when it has one that can be
 * played, else today's featured game, else the first that can. -1 for none. */
int Desk_StartRow(const Desk *d, int catalogIndex);

/* --- the screen --- */
#define DESK_X 80
#define DESK_RIGHT 880
#define DESK_TITLE_Y 56
#define DESK_INTRO_Y 108
#define DESK_STRIP_Y 132      /* Streak, Best streak, Featured today, the dots: labels; values 14 px under */
#define DESK_CELL_BEST 272     /* the strip's Best streak and Featured today */
#define DESK_CELL_FEATURED 400
#define DESK_DOT_X 640
#define DESK_DOT_PITCH 17
#define DESK_DOT_SIZE 13
#define DESK_HEAD_Y 204       /* the table's column names */
#define DESK_ROW_Y 220
#define DESK_ROW_PITCH 38     /* 44 and 40 until eight games had to fit above the note (rows end at 524) */
#define DESK_ROW_H 36
#define DESK_NOTE_Y 536
#define DESK_HELP_Y 560
#define DESK_COL_TODAY 360
#define DESK_COL_BEST 488
#define DESK_COL_WORLD 616
#define DESK_COL_RANK 800

/* Back, a row for each daily game that shows (rows that are not installed
 * are faint, with the reason), and the X. The desk moves its own keys
 * (Desk_Navigate), so the list has no navigation of its own. */
void Desk_Layout(const Desk *d, const KitScroll *sc, KitList *l);
/* The row (its place on the desk) a widget id is, or -1. */
int Desk_RowOf(int widgetId);
/* Up/Down and the d-pad from row to row, skipping faint ones, round the
 * ends, the list scrolling to keep the keys in sight; the wheel scrolls it.
 * True if the focus moved. */
bool Desk_Navigate(const Desk *d, KitScroll *sc, KitState *k, const Input *in, bool wheelFree);
/* The sentence for a row (the keys' or the pointer's). */
void Desk_Help(const Desk *d, int row, char *out, size_t size);
/* The hint line, from the key table. */
void Desk_Hint(InputDevice hints, char *out, size_t size);

#endif
