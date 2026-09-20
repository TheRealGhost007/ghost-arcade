#include "safefile.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define APP_DIRNAME "ghost-launcher"

static void MkdirParents(const char *path) {
    char cursor[600] = {0};
    for (const char *p = path; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - path);
            if (len > 0) {
                memcpy(cursor, path, len);
                cursor[len] = '\0';
                mkdir(cursor, 0755);
            }
            if (*p == '\0') break;
        }
    }
}

static bool GetStatsPath(char *out, size_t outSize) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    char dir[512];
    if (xdgData && xdgData[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/%s", xdgData, APP_DIRNAME);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') return false;
        snprintf(dir, sizeof(dir), "%s/.local/share/%s", home, APP_DIRNAME);
    }
    MkdirParents(dir);
    snprintf(out, outSize, "%s/playtime.txt", dir);
    return true;
}

static char *TrimNewline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
    return s;
}

bool Stats_Load(PlayStats *s) {
    memset(s, 0, sizeof(*s));

    char path[600];
    if (!GetStatsPath(path, sizeof(path))) return false;

    FILE *f = fopen(path, "r");
    if (!f) return true; /* no file yet is not an error; stats just start empty */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        TrimNewline(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        const char *name = line;
        double seconds = atof(sep + 1);

        if (strcmp(name, "__TOTAL__") == 0) {
            s->totalLauncherSeconds = seconds;
        } else if (s->count < MANIFEST_MAX_GAMES) {
            snprintf(s->entries[s->count].name, MANIFEST_FIELD_LEN, "%.*s", MANIFEST_FIELD_LEN - 1, name);
            s->entries[s->count].seconds = seconds;
            s->count++;
        }
    }
    fclose(f);
    return true;
}

bool Stats_Save(const PlayStats *s) {
    char path[600];
    if (!GetStatsPath(path, sizeof(path))) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f, "# Ghost Launcher playtime: name|seconds\n");
    fprintf(f, "__TOTAL__|%.1f\n", s->totalLauncherSeconds);
    for (int i = 0; i < s->count; i++) {
        fprintf(f, "%s|%.1f\n", s->entries[i].name, s->entries[i].seconds);
    }
    SafeFile_Close(f);
    return true;
}

void Stats_AddGameTime(PlayStats *s, const char *gameName, double seconds) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].name, gameName) == 0) {
            s->entries[i].seconds += seconds;
            return;
        }
    }
    if (s->count < MANIFEST_MAX_GAMES) {
        snprintf(s->entries[s->count].name, MANIFEST_FIELD_LEN, "%s", gameName);
        s->entries[s->count].seconds = seconds;
        s->count++;
    }
}

void Stats_AddLauncherTime(PlayStats *s, double seconds) {
    s->totalLauncherSeconds += seconds;
}

double Stats_GetGameTime(const PlayStats *s, const char *gameName) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].name, gameName) == 0) return s->entries[i].seconds;
    }
    return 0.0;
}
