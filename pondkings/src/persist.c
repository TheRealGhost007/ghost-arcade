#define _POSIX_C_SOURCE 200809L
#include "persist.h"
#include "game.h"
#include "keynames.h"
#include "safefile.h"
#include "teams.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const kKeyNames[KA_COUNT] = {"up", "down", "left", "right", "shoot", "pass", "check"};

void Settings_Default(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->sfxVolume = 80;
    s->sound = true;
    s->periodMinutes = 2;
    s->difficulty = DIFF_NORMAL;
    s->rivals = 0;
    s->venue = VENUE_POND;
    s->soVenue = VENUE_RINK;
    Input_DefaultKeys(0, s->keys[0]);
    Input_DefaultKeys(1, s->keys[1]);
    s->onlineOk = false;
}

static bool ConfigPath(char *out, size_t size, bool makeDir) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.config", home);
    }
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/pondkings", base);
    if (makeDir) {
        mkdir(base, 0755);
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    }
    snprintf(out, size, "%s/config.ini", dir);
    return true;
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool Settings_Load(Settings *s) {
    Settings_Default(s);
    char path[700];
    if (!ConfigPath(path, sizeof(path), false)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        const char *val = eq + 1;
        int v = atoi(val);
        if (strcmp(line, "sfx_volume") == 0) s->sfxVolume = Clamp(v, 0, 100);
        else if (strcmp(line, "sound") == 0) s->sound = v != 0;
        else if (strcmp(line, "period_minutes") == 0) s->periodMinutes = Clamp(v, 1, 3);
        else if (strcmp(line, "difficulty") == 0) s->difficulty = Clamp(v, 0, DIFF_COUNT - 1);
        else if (strcmp(line, "rivals") == 0) s->rivals = Clamp(v, 0, RIVAL_COUNT - 1);
        else if (strcmp(line, "venue") == 0) s->venue = Clamp(v, 0, VENUE_COUNT - 1);
        else if (strcmp(line, "shootout_venue") == 0) s->soVenue = Clamp(v, VENUE_RINK, VENUE_ARENA);
        else if (strcmp(line, "online_ok") == 0) s->onlineOk = v != 0;
        else {
            for (int h = 0; h < 2; h++)
                for (int a = 0; a < KA_COUNT; a++) {
                    char key[24];
                    snprintf(key, sizeof(key), "%s_%s", h ? "arrows" : "wasd", kKeyNames[a]);
                    if (strcmp(line, key) != 0) continue;
                    int code = Keys_CodeFromName(val);
                    if (code > 0 && !Input_KeyReserved(code)) s->keys[h][a] = code;
                }
        }
    }
    fclose(f);
    return true;
}

bool Settings_Save(const Settings *s) {
    char path[700];
    if (!ConfigPath(path, sizeof(path), true)) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Pond Kings settings\n");
    fprintf(f, "sfx_volume=%d\nsound=%d\nperiod_minutes=%d\ndifficulty=%d\nrivals=%d\nvenue=%d\nshootout_venue=%d\nonline_ok=%d\n",
            s->sfxVolume, s->sound ? 1 : 0, s->periodMinutes, s->difficulty, s->rivals, s->venue, s->soVenue, s->onlineOk ? 1 : 0);
    for (int h = 0; h < 2; h++)
        for (int a = 0; a < KA_COUNT; a++) {
            const char *name = Keys_NameFromCode(s->keys[h][a]);
            if (name && name[0]) fprintf(f, "%s_%s=%s\n", h ? "arrows" : "wasd", kKeyNames[a], name);
        }
    return SafeFile_Close(f);
}
