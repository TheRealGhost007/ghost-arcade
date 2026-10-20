#ifndef LAUNCHERINI_H
#define LAUNCHERINI_H

#include <stdbool.h>
#include <stddef.h>

/* Settings only the launcher uses (~/.config/ghost-launcher/launcher.ini).
 * They must never go into the shared prefs.ini: every game rewrites that
 * file with just its four keys, which would delete anything else.
 *
 * The file is kept as its lines, so comments, blank lines, the order and
 * keys this build does not know (written by a newer launcher) all survive a
 * save. key=value, '#' or ';' starts a comment, surrounding spaces ignored.
 * A file with more than LINI_MAX_LINES lines, or a line longer than
 * LINI_LINE_LEN - 1, is still read (what fits), but never saved over: a save
 * would drop the rest. No raylib. */

/* Room for the row's keys (lineup.h): four per machine for a full catalog
 * of MANIFEST_MAX_GAMES, and the settings besides. */
#define LINI_MAX_LINES 512
#define LINI_LINE_LEN 512
#define LINI_KEY_LEN 64

typedef struct {
    char lines[LINI_MAX_LINES][LINI_LINE_LEN];
    int count;
    bool truncated; /* the file did not fit: Save refuses */
} LauncherIni;

/* $XDG_CONFIG_HOME/ghost-launcher/launcher.ini; false if there is no HOME. */
bool LauncherIni_Path(char *out, size_t size);
/* A missing file reads as an empty one. False if it exists but cannot be read. */
bool LauncherIni_Load(LauncherIni *ini);
/* Crash-safe (safefile). False if it could not be written, or if the
 * loaded file did not fit (ini->truncated). */
bool LauncherIni_Save(const LauncherIni *ini);

/* The value, or NULL if the key is absent. If a hand-edited file has the key
 * twice, the last one wins (as a plain reader would see it). */
const char *LauncherIni_Get(const LauncherIni *ini, const char *key);
/* A whole number in [min, max]; def when absent or not a number. */
int LauncherIni_GetInt(const LauncherIni *ini, const char *key, int def, int min, int max);
/* 1/0, on/off, yes/no, true/false; def otherwise. */
bool LauncherIni_GetBool(const LauncherIni *ini, const char *key, bool def);

/* In memory only: replaces the key's line where it stands (dropping any
 * other copies of it) or appends one. Keys are lowercase letters, digits,
 * '_', '.' and '-'; values are one line. False if either is not storable or
 * the file is full. */
bool LauncherIni_Set(LauncherIni *ini, const char *key, const char *value);
bool LauncherIni_SetInt(LauncherIni *ini, const char *key, int value);
/* In memory only: every copy of the key goes. False if it was not there. */
bool LauncherIni_Unset(LauncherIni *ini, const char *key);

/* Reads the file again, sets one key and saves, leaving the fresh copy in
 * *ini. Every change goes through here, so a change made by another copy of
 * the launcher (or by hand) since this one loaded is never written over. */
bool LauncherIni_Store(LauncherIni *ini, const char *key, const char *value);
bool LauncherIni_StoreInt(LauncherIni *ini, const char *key, int value);

#endif
