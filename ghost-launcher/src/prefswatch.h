#ifndef PREFSWATCH_H
#define PREFSWATCH_H

#include <stdbool.h>

/* Keeps the launcher's copy of the shared prefs.ini (ghost-common prefs.c)
 * fresh. Every game writes that file, and prefs.c only reads it once, so
 * without this a setting changed in a game while the launcher was open came
 * back the next time the launcher saved (F11 rewrote all four keys from its
 * stale copy). The file is read again:
 *   - when the launcher window gets focus back,
 *   - after a game the launcher started has exited,
 *   - right before the launcher writes it (F11 / Alt+Enter, and later the
 *     Settings screen).
 * Raylib-free: the caller passes in what happened this frame. */
typedef struct {
    bool focused;
} PrefsWatch;

void PrefsWatch_Init(PrefsWatch *w, bool focused);
/* Returns true if prefs.ini was read again this frame. */
bool PrefsWatch_Update(PrefsWatch *w, bool focused, bool gameExited, bool aboutToWrite);

#endif
