#ifndef PREFS_H
#define PREFS_H

#include <stdbool.h>

/* Arcade-wide preferences, shared by every Ghost Arcade game and the launcher
 * (~/.config/ghost-arcade/prefs.ini). Per-game things (key bindings, that
 * game's effects volume) stay in each game's own config; what lives here is
 * what a player wants the same everywhere. No raylib. */
#define PREFS_ROWS 4 /* the rows the shared settings screens add: music, shake, flashing, fullscreen */

typedef struct {
    int musicVolume;     /* 0-100 */
    bool screenShake;
    bool reducedFlashing;
    bool fullscreen;
} ArcadePrefs;

void Prefs_Load(void);
bool Prefs_Save(void);
ArcadePrefs *Prefs_Get(void);

/* One settings row's input. idx 0 music volume (left/right), 1 screen shake,
 * 2 reduced flashing and 3 fullscreen (confirm, or left/right, toggles). Returns true if a
 * value changed (and has already been saved). */
bool Prefs_HandleRow(int idx, bool left, bool right, bool confirm);

#endif
