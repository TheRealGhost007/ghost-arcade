#include "safefile.h"
#include "persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define APP_DIRNAME "moondrop"

static void JoinPath(char *out, size_t outSize, const char *a, const char *b) {
    snprintf(out, outSize, "%s/%s", a, b);
}

/* Returns $XDG_CONFIG_HOME/moondrop or $XDG_DATA_HOME/moondrop,
 * creating intermediate directories as needed. `kind` is "config" or
 * "share" and picks the XDG var + fallback under $HOME. */
static bool GetAppDir(char *out, size_t outSize, const char *kind) {
    const char *xdgVar = (strcmp(kind, "config") == 0) ? getenv("XDG_CONFIG_HOME")
                                                         : getenv("XDG_DATA_HOME");
    char base[512];
    if (xdgVar && xdgVar[0] != '\0') {
        snprintf(base, sizeof(base), "%s", xdgVar);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') return false;
        const char *fallback = (strcmp(kind, "config") == 0) ? ".config" : ".local/share";
        snprintf(base, sizeof(base), "%s/%s", home, fallback);
    }

    /* mkdir -p for base (it usually already exists) and base/moondrop. */
    char cursor[600] = {0};
    for (const char *p = base; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - base);
            if (len > 0) {
                memcpy(cursor, base, len);
                cursor[len] = '\0';
                mkdir(cursor, 0755); /* ignore EEXIST and any other error here */
            }
            if (*p == '\0') break;
        }
    }
    JoinPath(out, outSize, base, APP_DIRNAME);
    if (mkdir(out, 0755) != 0 && errno != EEXIST) {
        /* Non-fatal: writes will just fail later and we keep in-memory state. */
    }
    return true;
}

static bool GetFilePath(char *out, size_t outSize, const char *kind, const char *filename) {
    char dir[512];
    if (!GetAppDir(dir, sizeof(dir), kind)) return false;
    JoinPath(out, outSize, dir, filename);
    return true;
}

void Settings_Default(Settings *s) {
    memset(s, 0, sizeof(*s));
    snprintf(s->left, KEYNAME_LEN, "LEFT");
    snprintf(s->left2, KEYNAME_LEN, "A");
    snprintf(s->right, KEYNAME_LEN, "RIGHT");
    snprintf(s->right2, KEYNAME_LEN, "D");
    snprintf(s->thrust, KEYNAME_LEN, "UP");
    snprintf(s->thrust2, KEYNAME_LEN, "W");
    snprintf(s->pause, KEYNAME_LEN, "P");
    snprintf(s->restart, KEYNAME_LEN, "R");
    snprintf(s->quit, KEYNAME_LEN, "ESCAPE");
    s->volumePercent = 70;
    s->audioEnabled = true;
    s->glow = true;
}

static char *TrimInPlace(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                        s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static void SetField(Settings *s, const char *key, const char *value) {
    if (strcmp(key, "left") == 0) snprintf(s->left, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "left2") == 0) snprintf(s->left2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "right") == 0) snprintf(s->right, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "right2") == 0) snprintf(s->right2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "thrust") == 0) snprintf(s->thrust, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "thrust2") == 0) snprintf(s->thrust2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "pause") == 0) snprintf(s->pause, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "restart") == 0) snprintf(s->restart, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "quit") == 0) snprintf(s->quit, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "volume") == 0) {
        int v = atoi(value);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        s->volumePercent = v;
    } else if (strcmp(key, "audio_enabled") == 0) {
        s->audioEnabled = (atoi(value) != 0);
    } else if (strcmp(key, "glow") == 0) {
        s->glow = (atoi(value) != 0);
    }
}

bool Settings_Load(Settings *s) {
    Settings_Default(s);
    char path[600];
    if (!GetFilePath(path, sizeof(path), "config", "config.ini")) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = TrimInPlace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = TrimInPlace(trimmed);
        char *value = TrimInPlace(eq + 1);
        if (value[0] != '\0') SetField(s, key, value);
    }
    fclose(f);
    return true;
}

bool Settings_Save(const Settings *s) {
    char path[600];
    if (!GetFilePath(path, sizeof(path), "config", "config.ini")) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f,
        "# moondrop settings\n"
        "# Key names: A-Z, 0-9, LEFT, RIGHT, UP, DOWN, SPACE, ESCAPE, ENTER,\n"
        "# LEFT_SHIFT, RIGHT_SHIFT, LEFT_CONTROL, TAB, etc.\n"
        "left=%s\n"
        "left2=%s\n"
        "right=%s\n"
        "right2=%s\n"
        "thrust=%s\n"
        "thrust2=%s\n"
        "pause=%s\n"
        "restart=%s\n"
        "quit=%s\n"
        "volume=%d\n"
        "audio_enabled=%d\n"
        "glow=%d\n",
        s->left, s->left2, s->right, s->right2, s->thrust, s->thrust2,
        s->pause, s->restart, s->quit,
        s->volumePercent, s->audioEnabled ? 1 : 0, s->glow ? 1 : 0);

    SafeFile_Close(f);
    return true;
}

void SaveData_Default(SaveData *d) {
    d->highScore = 0;
    d->bestLevel = 1;
}

bool SaveData_Load(SaveData *d) {
    SaveData_Default(d);
    char path[600];
    if (!GetFilePath(path, sizeof(path), "share", "save.dat")) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = TrimInPlace(line);
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = TrimInPlace(trimmed);
        char *value = TrimInPlace(eq + 1);
        if (strcmp(key, "high_score") == 0) d->highScore = atol(value);
        else if (strcmp(key, "best_level") == 0) d->bestLevel = atoi(value);
    }
    fclose(f);
    return true;
}

bool SaveData_Save(const SaveData *d) {
    char path[600];
    if (!GetFilePath(path, sizeof(path), "share", "save.dat")) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "high_score=%ld\nbest_level=%d\n", d->highScore, d->bestLevel);
    SafeFile_Close(f);
    return true;
}
