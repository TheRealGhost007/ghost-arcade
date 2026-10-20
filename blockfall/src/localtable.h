#ifndef LOCALTABLE_H
#define LOCALTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include "ghostlink.h"

/* This machine's own score table, for the boards that stay off the shared one
 * (custom sizes): $XDG_DATA_HOME/blockfall/scores.txt, in the shared tables'
 * row format (mode|username|score|date) and with their rules (sorted by
 * mode, best first, the top GHOSTLINK_TOP_N per mode). Unlike the shared
 * code it never drops what it could not read: a row it does not understand
 * is kept as it was, and a file longer than LOCALTABLE_MAX_ROWS is left
 * alone rather than rewritten from part of it. No raylib. */

#define LOCALTABLE_FILE "scores.txt"
#define LOCALTABLE_MAX_ROWS 4096

/* Up to maxOut rows for one mode, best first. Returns the count. */
int LocalTable_Load(const char *path, const char *mode, GhostScore *out, int maxOut);
/* Adds a run. Returns its rank in the mode (1 = top), or 0 if it did not make
 * the top ten or the file could not be read in full or written. Scores <= 0
 * are ignored. */
int LocalTable_Submit(const char *path, const char *mode, const char *username, long score, const char *date);
/* Data rows in the file (comments and blank lines aside); 0 when missing. */
int LocalTable_CountRows(const char *path);

#endif
