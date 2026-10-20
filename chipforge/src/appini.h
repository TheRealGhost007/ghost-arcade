#ifndef APPINI_H
#define APPINI_H

#include <stdbool.h>
#include <stddef.h>

/* Chipforge's own settings, in $XDG_CONFIG_HOME/chipforge/config.ini (or
 * ~/.config/chipforge/config.ini), the place each game keeps its own. The
 * arcade-wide prefs.ini is shared by every game and the launcher, so a
 * setting only Chipforge has does not belong there. Written through safefile,
 * so a crash leaves the old file or the new one. No raylib.
 *
 *   timing_ms   how late you hear the music, measured by the Timing test:
 *               what you play along with is caught that much earlier */

#define APPINI_TIMING_DEFAULT 40
#define APPINI_TIMING_MIN 0
#define APPINI_TIMING_MAX 300
#define APPINI_TIMING_STEP 5   /* Left/Right in Settings */

typedef struct {
    int timingMs;
} AppIni;

void AppIni_Default(AppIni *a);
/* Defaults first, then whatever the file says (values clamped; unknown
 * lines, junk and lines of 255 characters or more skipped whole). False when
 * there is no file to read. */
bool AppIni_Load(AppIni *a);
bool AppIni_Save(const AppIni *a);
/* The file's path; with create, its folder is made first. */
bool AppIni_Path(char *out, size_t size, bool create);
int AppIni_ClampTiming(int ms);

#endif
