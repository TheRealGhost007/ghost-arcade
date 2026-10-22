#ifndef SCORES_H
#define SCORES_H

#include <stdbool.h>
#include <stddef.h>

/* Read-only view of the Ghost Arcade score files the games write (see
 * ROADMAP.md, "data contract"):
 *   scores/<slug>.txt          this machine's runs
 *   scores/global/<slug>.txt   the online board ghost-sync last downloaded
 * Rows are mode|username|score|date. No raylib dependency. */

#define SCORES_SLUG_LEN 64
#define SCORES_MODE_LEN 24
#define SCORES_NAME_LEN 64

typedef struct {
    bool found;
    bool timed; /* a time board's row (see below): show it with Scores_FormatScore */
    char mode[SCORES_MODE_LEN];
    char username[SCORES_NAME_LEN];
    long score;
} ScoreBest;

/* Boards ranked by time rather than points: Blockfall's Sprint 40
 * ("sprint40", "sprint40-14x20"). A row's score there is SCORES_TIME_BASE
 * minus the finish time in hundredths of a second, so the faster run is still
 * the bigger number every table sorts first; it is shown as a time. */
#define SCORES_TIME_BASE 10000000L
bool Scores_IsTimeBoard(const char *slug, const char *mode);
/* A board value as words: "74372", or on a time board "1:12.40". */
void Scores_FormatScore(bool timed, long score, char *out, size_t size);

/* The score-file slug is the game's binary name: the basename of the
 * catalog's exec path ("~/.local/bin/blockfall" -> "blockfall"). Returns
 * false if that isn't a safe file name (lowercase letters, digits, '-'). */
bool Scores_SlugFromExec(const char *execPath, char *out, size_t outSize);

/* Highest-scoring row across all modes in one score file. Time boards are
 * left out (their numbers are not points), unless they are all the file has:
 * then it is the fastest time, with timed set. found = false if the file is
 * missing or has no valid rows. */
ScoreBest Scores_LoadBest(const char *slug, bool global);

/* Where scores/<slug>.txt (or scores/global/<slug>.txt) is. False without
 * a home folder. */
bool Scores_FilePath(const char *slug, bool global, char *out, size_t outSize);

#endif
