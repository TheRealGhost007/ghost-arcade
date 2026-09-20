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
    char mode[SCORES_MODE_LEN];
    char username[SCORES_NAME_LEN];
    long score;
} ScoreBest;

/* The score-file slug is the game's binary name: the basename of the
 * catalog's exec path ("~/.local/bin/coilrush" -> "coilrush"). Returns
 * false if that isn't a safe file name (lowercase letters, digits, '-'). */
bool Scores_SlugFromExec(const char *execPath, char *out, size_t outSize);

/* Highest-scoring row across all modes in one score file. found = false if
 * the file is missing or has no valid rows. */
ScoreBest Scores_LoadBest(const char *slug, bool global);

#endif
