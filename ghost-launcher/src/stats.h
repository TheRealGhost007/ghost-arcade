#ifndef STATS_H
#define STATS_H

#include <stdbool.h>
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

#endif
