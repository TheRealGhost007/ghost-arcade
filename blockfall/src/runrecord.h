#ifndef RUNRECORD_H
#define RUNRECORD_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "ghostlink.h"

/* What a finished (or abandoned) run records, decided in one place with no
 * raylib so it can be tested headlessly:
 *  - a run counts (runs.log, achievements) only if a piece locked and it
 *    scored, and never when it was a tuned (F2) playtest;
 *  - save.dat keeps meaning the classic 10x20 Marathon and nothing else;
 *  - Sprint is ranked by time on its own "sprint40" boards, and only a
 *    finished Sprint goes on them (the old points-ranked "sprint" rows stay,
 *    shown as "Sprint (old)");
 *  - the five preset boards go on the shared table Ghost Launcher and
 *    ghost-sync read; custom sizes stay on this machine, in Blockfall's own
 *    table (localtable.h), and so never reach the world boards. */

#define RECORD_SLUG "blockfall"

/* The board presets on Play setup; the last one is Custom (any size). */
typedef struct { const char *name; int w, h; } BoardPreset;
#define BOARD_PRESET_COUNT 6
#define BOARD_PRESET_CUSTOM (BOARD_PRESET_COUNT - 1)
const BoardPreset *Record_Preset(int i);
/* The preset with exactly this size, or -1 for a custom size. */
int Record_PresetOf(int w, int h);

/* A Sprint 40 board stores RECORD_TIME_BASE minus the finish time in
 * hundredths of a second, so the faster run is still the bigger number the
 * shared tables (and the world board) rank first. Ghost Launcher decodes
 * the same way (its scores.h). */
#define RECORD_TIME_BASE 10000000L
long Record_SprintValue(float seconds);
long Record_ValueCentis(long value);

typedef enum { UNIT_POINTS, UNIT_TIME } RecordUnit;
RecordUnit Record_UnitOf(GameMode mode);
/* "74372", or a time as "1:12.40" (hundredths: always m:ss.cc). */
void Record_FormatTime(long centis, char *out, size_t size);
void Record_FormatValue(RecordUnit unit, long value, char *out, size_t size);
/* How far `worse` is behind `better`: "15570", or "1.20 s". */
void Record_FormatGap(RecordUnit unit, long better, long worse, char *out, size_t size);

/* The board a run goes on: "marathon" for the classic 10x20, "sprint40",
 * "power-14x20", ... (Zen has none; its key is still made, for the log). */
void Record_BoardKey(GameMode mode, int w, int h, char *out, size_t size);
/* Sprint's board before 2.1.0, ranked by points: "sprint", "sprint-14x20". */
void Record_OldSprintKey(int w, int h, char *out, size_t size);
/* runs.log keeps the key it always had ("sprint" for Sprint). */
void Record_LogKey(GameMode mode, int w, int h, char *out, size_t size);

typedef struct {
    bool counts;   /* a real run: runs.log and achievements */
    bool toBoard;  /* goes on a score table */
    bool local;    /* a custom size: this machine's own table, never the shared one */
    bool toSave;   /* save.dat's classic best */
    RecordUnit unit;
    long value;    /* what the board ranks: the score, or the Sprint time value */
    char key[GHOSTLINK_MODE_LEN];
} RunRecord;
RunRecord Record_Classify(const Game *g, bool tuned);

/* Every board ranks higher values first (times included, see above), and
 * 0 means "no best yet". */
bool Record_Beats(long value, long best);
/* The classic board's one Best: save.dat's (scores from before the tables
 * existed live only there) or the table's, whichever is higher. */
long Record_ClassicBest(long saveDatBest, long tableBest);

/* The run's end gets exactly one sound (it used to stack the game-over sting
 * on a clear, a level-up and two jingles). */
typedef enum { END_SOUND_GAME_OVER, END_SOUND_CLEARED, END_SOUND_NEW_BEST } EndSound;
EndSound Record_EndSound(bool won, bool newBest);

/* ---- files (raylib-free, but they read and write the score tables) ---- */

/* The shared table's rows, counted as ghostlink counts them. ghostlink reads
 * at most GHOSTLINK_MAX_ENTRIES rows and rewrites the file from what it
 * read, so Blockfall never adds to a file that is already that full: the run
 * goes to this machine's table instead and nothing is dropped. */
int Record_SharedRows(void);
bool Record_SharedHasRoom(void);

/* This machine's best on a board, 0 for none: the shared table's top and
 * Blockfall's own table's (a custom size, or a run kept there because the
 * shared file was full). The classic Marathon also weighs save.dat. */
long Record_LoadBest(GameMode mode, int w, int h, long saveDatBest);
/* The player's own best on a board on this machine, 0 for none, and *rank,
 * the place it takes on this machine's board (0 for none): the player's
 * first row there, or for the classic Marathon save.dat's best when that is
 * higher (it predates the tables, so it has no name; it is the best to
 * beat). What the menus show as "Your best": on a machine with more than
 * one profile, the board's best (Record_LoadBest) can be someone else's. */
long Record_LoadYourBest(GameMode mode, int w, int h, const char *username, long saveDatBest, int *rank);
/* A board's rows for the Scores screen, best first. This machine: both
 * tables merged. World: what ghost-sync last downloaded. */
int Record_LoadBoard(const char *key, bool world, GhostScore *out, int maxOut);
/* Puts a run on its board; returns its rank there (1 = top), 0 if it did not
 * make the top ten or could not be saved. *shared says which table took it. */
int Record_Submit(const RunRecord *r, const char *username, const char *date, bool *shared);
/* Where a row is on a board's rows (best first): the first with this name,
 * value and date, 1-based; 0 when it is not there. */
int Record_FindRow(const GhostScore *rows, int count, const char *username, long value, const char *date);

/* A board key's parts: "marathon" is the classic 10x20 Marathon,
 * "sprint40-14x20" Sprint 40 on 14x20, "sprint" Sprint's old points board.
 * False for anything else (an unknown or malformed key). */
bool Record_ParseKey(const char *key, GameMode *mode, bool *oldSprint, int *w, int *h);
/* The sizes a mode (or Sprint's old points board) has rows on: on this
 * machine (both tables) or on the world board ghost-sync last downloaded.
 * The presets first in their own order, then the rest by width, then
 * height. Returns how many, at most max. */
typedef struct { int w, h; } BoardSize;
int Record_BoardsWithRows(GameMode mode, bool oldSprint, BoardSize *out, int max);

#endif
