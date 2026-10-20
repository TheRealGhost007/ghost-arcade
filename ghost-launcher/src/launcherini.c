#define _POSIX_C_SOURCE 200809L
#include "launcherini.h"
#include "safefile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define LINI_HEADER "# Ghost Launcher settings, for the launcher only. The ones every game shares are in ~/.config/ghost-arcade/prefs.ini"

static bool ConfigDirs(char *base, size_t baseSize, char *dir, size_t dirSize) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) snprintf(base, baseSize, "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, baseSize, "%s/.config", home);
    }
    snprintf(dir, dirSize, "%s/ghost-launcher", base);
    return true;
}

bool LauncherIni_Path(char *out, size_t size) {
    char base[512], dir[600];
    if (!ConfigDirs(base, sizeof(base), dir, sizeof(dir))) return false;
    snprintf(out, size, "%s/launcher.ini", dir);
    return true;
}

/* The key of a key=value line, or false for comments, blanks and junk. */
static bool LineKey(const char *line, char *key, size_t keySize) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == ';' || *line == '\0') return false;
    const char *eq = strchr(line, '=');
    if (!eq) return false;
    size_t n = (size_t)(eq - line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
    if (n == 0 || n >= keySize) return false;
    memcpy(key, line, n);
    key[n] = '\0';
    return true;
}

static bool IsKey(const char *line, const char *key) {
    char k[LINI_KEY_LEN];
    return LineKey(line, k, sizeof(k)) && strcmp(k, key) == 0;
}

static void TrimEnd(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
}

bool LauncherIni_Load(LauncherIni *ini) {
    ini->count = 0;
    ini->truncated = false;
    char path[700];
    if (!LauncherIni_Path(path, sizeof(path))) return true;
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    char buf[LINI_LINE_LEN];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        bool whole = len > 0 && buf[len - 1] == '\n';
        if (!whole && !feof(f)) {
            /* Cut short: the rest must not come back as lines of its own. */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') ini->truncated = true;
        }
        TrimEnd(buf);
        if (ini->count < LINI_MAX_LINES) snprintf(ini->lines[ini->count++], LINI_LINE_LEN, "%s", buf);
        else ini->truncated = true;
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool LauncherIni_Save(const LauncherIni *ini) {
    if (ini->truncated) return false; /* writing what fit would delete the rest */
    char base[512], dir[600], path[700];
    if (!ConfigDirs(base, sizeof(base), dir, sizeof(dir))) return false;
    mkdir(base, 0755);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    snprintf(path, sizeof(path), "%s/launcher.ini", dir);
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    for (int i = 0; i < ini->count; i++) {
        fputs(ini->lines[i], f);
        fputc('\n', f);
    }
    return SafeFile_Close(f);
}

const char *LauncherIni_Get(const LauncherIni *ini, const char *key) {
    for (int i = ini->count - 1; i >= 0; i--) {
        if (!IsKey(ini->lines[i], key)) continue;
        const char *v = strchr(ini->lines[i], '=') + 1;
        while (*v == ' ' || *v == '\t') v++;
        return v;
    }
    return NULL;
}

int LauncherIni_GetInt(const LauncherIni *ini, const char *key, int def, int min, int max) {
    const char *v = LauncherIni_Get(ini, key);
    if (!v || !v[0]) return def;
    char *end;
    errno = 0;
    long n = strtol(v, &end, 10);
    if (*end != '\0' || errno != 0) return def;
    if (n < min) return min;
    if (n > max) return max;
    return (int)n;
}

bool LauncherIni_GetBool(const LauncherIni *ini, const char *key, bool def) {
    const char *v = LauncherIni_Get(ini, key);
    if (!v) return def;
    static const char *const kOn[] = {"1", "on", "yes", "true"}, *const kOff[] = {"0", "off", "no", "false"};
    for (int i = 0; i < 4; i++) {
        if (strcasecmp(v, kOn[i]) == 0) return true;
        if (strcasecmp(v, kOff[i]) == 0) return false;
    }
    return def;
}

static bool ValidKey(const char *key) {
    size_t n = key ? strlen(key) : 0;
    if (n == 0 || n >= LINI_KEY_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
    }
    return true;
}

static bool ValidValue(const char *value) {
    if (!value) return false;
    for (const char *p = value; *p; p++) if ((unsigned char)*p < 32 || *p == 127) return false;
    return true;
}

static void RemoveLine(LauncherIni *ini, int at) {
    memmove(ini->lines[at], ini->lines[at + 1], (size_t)(ini->count - at - 1) * LINI_LINE_LEN);
    ini->count--;
}

bool LauncherIni_Set(LauncherIni *ini, const char *key, const char *value) {
    if (!ValidKey(key) || !ValidValue(value)) return false;
    char line[LINI_LINE_LEN];
    int n = snprintf(line, sizeof(line), "%s=%s", key, value);
    if (n < 0 || n >= (int)sizeof(line)) return false;
    int at = -1;
    for (int i = 0; i < ini->count;) {
        if (!IsKey(ini->lines[i], key)) { i++; continue; }
        if (at < 0) at = i++;
        else RemoveLine(ini, i);
    }
    if (at < 0) {
        if (ini->count == 0) snprintf(ini->lines[ini->count++], LINI_LINE_LEN, "%s", LINI_HEADER);
        if (ini->count >= LINI_MAX_LINES) return false;
        at = ini->count++;
    }
    memcpy(ini->lines[at], line, (size_t)n + 1);
    return true;
}

bool LauncherIni_SetInt(LauncherIni *ini, const char *key, int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return LauncherIni_Set(ini, key, buf);
}

bool LauncherIni_Unset(LauncherIni *ini, const char *key) {
    bool found = false;
    for (int i = 0; i < ini->count;) {
        if (!IsKey(ini->lines[i], key)) { i++; continue; }
        RemoveLine(ini, i);
        found = true;
    }
    return found;
}

bool LauncherIni_Store(LauncherIni *ini, const char *key, const char *value) {
    if (!LauncherIni_Load(ini)) return false; /* unreadable: never overwrite what we could not read */
    if (!LauncherIni_Set(ini, key, value)) return false;
    return LauncherIni_Save(ini);
}

bool LauncherIni_StoreInt(LauncherIni *ini, const char *key, int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return LauncherIni_Store(ini, key, buf);
}
