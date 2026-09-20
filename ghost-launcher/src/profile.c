#include "safefile.h"
#include "profile.h"
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

static bool GetProfilesPath(char *out, size_t outSize) {
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
    snprintf(out, outSize, "%s/profiles.txt", dir);
    return true;
}

static char *TrimNewline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
    return s;
}

bool Profiles_Load(Profiles *p) {
    memset(p, 0, sizeof(*p));

    char path[600];
    if (!GetProfilesPath(path, sizeof(path))) return false;

    FILE *f = fopen(path, "r");
    if (!f) return true;

    char line[512];
    while (fgets(line, sizeof(line), f) && p->count < MANIFEST_MAX_GAMES) {
        TrimNewline(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';

        ProfileEntry *e = &p->entries[p->count];
        snprintf(e->gameName, MANIFEST_FIELD_LEN, "%.*s", MANIFEST_FIELD_LEN - 1, line);
        snprintf(e->username, PROFILE_USERNAME_LEN, "%.*s", PROFILE_USERNAME_LEN - 1, sep + 1);
        p->count++;
    }
    fclose(f);
    return true;
}

bool Profiles_Save(const Profiles *p) {
    char path[600];
    if (!GetProfilesPath(path, sizeof(path))) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f, "# Ghost Launcher profiles: game_name|username  (" PROFILE_DEFAULT_KEY " = every game)\n");
    for (int i = 0; i < p->count; i++) {
        fprintf(f, "%s|%s\n", p->entries[i].gameName, p->entries[i].username);
    }
    SafeFile_Close(f);
    return true;
}

void Profiles_SetUsername(Profiles *p, const char *gameName, const char *username) {
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].gameName, gameName) == 0) {
            snprintf(p->entries[i].username, PROFILE_USERNAME_LEN, "%s", username);
            return;
        }
    }
    if (p->count < MANIFEST_MAX_GAMES) {
        ProfileEntry *e = &p->entries[p->count];
        snprintf(e->gameName, MANIFEST_FIELD_LEN, "%s", gameName);
        snprintf(e->username, PROFILE_USERNAME_LEN, "%s", username);
        p->count++;
    }
}

const char *Profiles_GetOwnUsername(const Profiles *p, const char *key) {
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].gameName, key) == 0) return p->entries[i].username;
    }
    return "";
}

const char *Profiles_GetUsername(const Profiles *p, const char *gameName) {
    const char *own = Profiles_GetOwnUsername(p, gameName);
    if (own[0] != '\0') return own; /* a blank override means "use my arcade name" */
    return Profiles_GetOwnUsername(p, PROFILE_DEFAULT_KEY);
}
