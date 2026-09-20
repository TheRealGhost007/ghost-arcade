#include "safefile.h"
#include "prefs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static ArcadePrefs sPrefs = {70, true, false, false};
static bool sLoaded = false;

static bool PrefsPath(char *out, size_t size, bool makeDir) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.config", home);
    }
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/ghost-arcade", base);
    if (makeDir) { mkdir(base, 0755); if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false; }
    snprintf(out, size, "%s/prefs.ini", dir);
    return true;
}

ArcadePrefs *Prefs_Get(void) {
    if (!sLoaded) Prefs_Load();
    return &sPrefs;
}

void Prefs_Load(void) {
    sLoaded = true;
    sPrefs = (ArcadePrefs){70, true, false, false};
    char path[700];
    if (!PrefsPath(path, sizeof(path), false)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if (strcmp(key, "music_volume") == 0) {
            int v = atoi(val);
            sPrefs.musicVolume = v < 0 ? 0 : (v > 100 ? 100 : v);
        } else if (strcmp(key, "screen_shake") == 0) sPrefs.screenShake = atoi(val) != 0;
        else if (strcmp(key, "reduced_flashing") == 0) sPrefs.reducedFlashing = atoi(val) != 0;
        else if (strcmp(key, "fullscreen") == 0) sPrefs.fullscreen = atoi(val) != 0;
    }
    fclose(f);
}

bool Prefs_Save(void) {
    char path[700];
    if (!PrefsPath(path, sizeof(path), true)) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Ghost Arcade preferences, shared by every game\nmusic_volume=%d\nscreen_shake=%d\nreduced_flashing=%d\nfullscreen=%d\n",
            sPrefs.musicVolume, sPrefs.screenShake ? 1 : 0, sPrefs.reducedFlashing ? 1 : 0, sPrefs.fullscreen ? 1 : 0);
    SafeFile_Close(f);
    return true;
}

bool Prefs_HandleRow(int idx, bool left, bool right, bool confirm) {
    ArcadePrefs *p = Prefs_Get();
    bool changed = false;
    if (idx == 0) {
        int v = p->musicVolume;
        if (right) v += 10;
        if (left) v -= 10;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != p->musicVolume) { p->musicVolume = v; changed = true; }
    } else if (idx == 1 && (confirm || left || right)) { p->screenShake = !p->screenShake; changed = true; }
    else if (idx == 2 && (confirm || left || right)) { p->reducedFlashing = !p->reducedFlashing; changed = true; }
    else if (idx == 3 && (confirm || left || right)) { p->fullscreen = !p->fullscreen; changed = true; }
    if (changed) Prefs_Save();
    return changed;
}
