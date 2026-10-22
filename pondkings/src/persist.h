#ifndef PERSIST_H
#define PERSIST_H

#include <stdbool.h>
#include "input.h"

/* Pond Kings' own settings: $XDG_CONFIG_HOME/pondkings/config.ini, plain
 * key=value lines. Music volume and fullscreen are the arcade-wide ones in
 * prefs.ini (ghost-common/prefs.h). */
typedef struct {
    int sfxVolume;     /* 0-100 */
    bool sound;        /* effects and music on */
    int periodMinutes; /* 1-3 */
    int difficulty;    /* quick games: Difficulty */
    int rivals;        /* quick games: index into the rival teams */
    int venue;         /* quick games: Venue */
    int soVenue;       /* shootout: VENUE_RINK or VENUE_ARENA */
    int keys[2][KA_COUNT]; /* the two keyboard halves */
    bool onlineOk;     /* the online notice has been read and accepted */
} Settings;

void Settings_Default(Settings *s);
bool Settings_Load(Settings *s);
bool Settings_Save(const Settings *s);

#endif
