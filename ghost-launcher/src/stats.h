#ifndef STATS_H
#define STATS_H

#include <stdbool.h>
#include <stddef.h>
#include "manifest.h"

typedef struct {
    char name[MANIFEST_FIELD_LEN];
    double seconds;
} StatEntry;

typedef struct {
    StatEntry entries[MANIFEST_MAX_GAMES];
    int count;
    double totalLauncherSeconds;
} PlayStats;

/* Loads from $XDG_DATA_HOME/ghost-launcher/playtime.txt (starts empty if
 * the file doesn't exist yet). */
bool Stats_Load(PlayStats *s);
bool Stats_Save(const PlayStats *s);

/* Adds to (creating if needed) the cumulative time for a game name. */
void Stats_AddGameTime(PlayStats *s, const char *gameName, double seconds);
void Stats_AddLauncherTime(PlayStats *s, double seconds);

double Stats_GetGameTime(const PlayStats *s, const char *gameName);

/* "1h 4m", "38m": how the launcher writes a time played. */
void Stats_FormatTime(double seconds, char *out, size_t size);

/* Moves the time kept under `from` (a name a game used to have) onto `into`
 * and drops `from`. False when there was nothing under `from`. */
bool Stats_Fold(PlayStats *s, const char *from, const char *into);

#endif
