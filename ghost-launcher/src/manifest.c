#include "safefile.h"
#include "manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

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

static bool GetConfigPath(char *out, size_t outSize) {
    const char *xdgConfig = getenv("XDG_CONFIG_HOME");
    char dir[512];
    if (xdgConfig && xdgConfig[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/%s", xdgConfig, APP_DIRNAME);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') return false;
        snprintf(dir, sizeof(dir), "%s/.config/%s", home, APP_DIRNAME);
    }
    MkdirParents(dir);
    snprintf(out, outSize, "%s/games.txt", dir);
    return true;
}

void Manifest_ExpandPath(const char *in, char *out, size_t outSize) {
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            snprintf(out, outSize, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, outSize, "%s", in);
}

static char *TrimNewline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
    return s;
}

static int ParseLines(FILE *f, Manifest *m) {
    char line[MANIFEST_FIELD_COUNT * MANIFEST_FIELD_LEN];
    while (m->count < MANIFEST_MAX_GAMES && fgets(line, sizeof(line), f)) {
        TrimNewline(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        GameEntry *e = &m->games[m->count];
        memset(e, 0, sizeof(*e));

        /* The install field is optional, so older 4-field catalogs load
         * unchanged. */
        char *fields[MANIFEST_FIELD_COUNT] = {e->name, e->exec, e->icon, e->desc, e->install};
        int fieldIdx = 0;
        char *cursor = line;
        while (fieldIdx < MANIFEST_FIELD_COUNT) {
            char *sep = strchr(cursor, '|');
            size_t len = sep ? (size_t)(sep - cursor) : strlen(cursor);
            if (len >= MANIFEST_FIELD_LEN) len = MANIFEST_FIELD_LEN - 1;
            memcpy(fields[fieldIdx], cursor, len);
            fields[fieldIdx][len] = '\0';
            fieldIdx++;
            if (!sep) break;
            cursor = sep + 1;
        }

        if (e->name[0] != '\0' && e->exec[0] != '\0') m->count++;
    }
    return m->count;
}

static void SeedDefaultIfMissing(const char *userPath, const char *assetsDir) {
    FILE *existing = fopen(userPath, "r");
    if (existing) {
        fclose(existing);
        return;
    }

    char defaultPath[512];
    snprintf(defaultPath, sizeof(defaultPath), "%s/games.txt", assetsDir);
    FILE *src = fopen(defaultPath, "r");
    if (!src) return;

    FILE *dst = SafeFile_Open(userPath);
    if (!dst) {
        fclose(src);
        return;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
    fclose(src);
    SafeFile_Close(dst);
}

bool Manifest_Load(Manifest *m, const char *assetsDir) {
    memset(m, 0, sizeof(*m));

    char path[600];
    if (!GetConfigPath(path, sizeof(path))) return false;

    SeedDefaultIfMissing(path, assetsDir);

    FILE *f = fopen(path, "r");
    if (!f) return false;
    ParseLines(f, m);
    fclose(f);
    return true;
}

bool Manifest_Save(const Manifest *m) {
    char path[600];
    if (!GetConfigPath(path, sizeof(path))) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f, "# Ghost Launcher game catalog: name|exec|icon|description|install-from\n");
    fprintf(f, "# Paths may start with ~/ for your home directory. install-from is optional:\n");
    fprintf(f, "# a source folder the launcher runs `make install` in when the game is missing.\n");
    for (int i = 0; i < m->count; i++) {
        const GameEntry *e = &m->games[i];
        fprintf(f, "%s|%s|%s|%s|%s\n", e->name, e->exec, e->icon, e->desc, e->install);
    }
    SafeFile_Close(f);
    return true;
}

bool Manifest_AddGame(Manifest *m, const char *name, const char *exec,
                       const char *icon, const char *desc, const char *install) {
    if (m->count >= MANIFEST_MAX_GAMES) return false;
    GameEntry *e = &m->games[m->count];
    snprintf(e->name, MANIFEST_FIELD_LEN, "%s", name);
    snprintf(e->exec, MANIFEST_FIELD_LEN, "%s", exec);
    snprintf(e->icon, MANIFEST_FIELD_LEN, "%s", icon ? icon : "");
    snprintf(e->desc, MANIFEST_FIELD_LEN, "%s", desc ? desc : "");
    snprintf(e->install, MANIFEST_FIELD_LEN, "%s", install ? install : "");
    m->count++;
    return true;
}

bool Manifest_RemoveGame(Manifest *m, int index) {
    if (index < 0 || index >= m->count) return false;
    for (int i = index; i < m->count - 1; i++) m->games[i] = m->games[i + 1];
    m->count--;
    return true;
}
