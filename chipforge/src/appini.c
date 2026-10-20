/* Chipforge's own config file. See appini.h. */
#define _POSIX_C_SOURCE 200809L /* mkdir */
#include "appini.h"
#include "safefile.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void AppIni_Default(AppIni *a) {
    a->timingMs = APPINI_TIMING_DEFAULT;
}

int AppIni_ClampTiming(int ms) {
    return ms < APPINI_TIMING_MIN ? APPINI_TIMING_MIN : (ms > APPINI_TIMING_MAX ? APPINI_TIMING_MAX : ms);
}

bool AppIni_Path(char *out, size_t size, bool create) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[512];
    if (xdg && xdg[0]) {
        snprintf(base, sizeof(base), "%s", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.config", home);
    }
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/chipforge", base);
    if (create) {
        /* The config home itself usually exists; on a fresh account it may
         * not, and then its parents are $HOME's own business. */
        mkdir(base, 0755);
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    }
    int n = snprintf(out, size, "%s/config.ini", dir);
    return n > 0 && (size_t)n < size;
}

static char *appini_Trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

bool AppIni_Load(AppIni *a) {
    AppIni_Default(a);
    char path[700];
    if (!AppIni_Path(path, sizeof(path), false)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    /* fgets hands a line longer than the buffer over in pieces, and a later
     * piece is no line of its own: the tail of a long comment could read as
     * "timing_ms=0", and a padded value would lose its number to the next
     * piece. So only a line that fits whole is read; a longer one is junk and
     * skipped from start to end. fgets puts its terminator in the last byte
     * only when the buffer filled, which tells a cut line from a short one
     * even when the line holds a NUL. */
    char line[256];
    bool atLineStart = true;
    for (;;) {
        line[sizeof(line) - 1] = 'x';
        if (!fgets(line, sizeof(line), f)) break;
        bool cut = line[sizeof(line) - 1] == '\0' && line[sizeof(line) - 2] != '\n';
        bool whole = atLineStart && !cut;
        atLineStart = !cut;
        if (!whole) continue;
        char *s = appini_Trim(line);
        if (!s[0] || s[0] == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = appini_Trim(s), *val = appini_Trim(eq + 1);
        if (strcmp(key, "timing_ms") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            /* A value that is not a number keeps the default rather than
             * becoming 0 ms, which would quietly undo a calibration. */
            if (end != val && *end == '\0') a->timingMs = AppIni_ClampTiming(v > 100000 ? 100000 : (v < -100000 ? -100000 : (int)v));
        }
    }
    fclose(f);
    return true;
}

bool AppIni_Save(const AppIni *a) {
    char path[700];
    if (!AppIni_Path(path, sizeof(path), true)) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Chipforge settings\n"
               "# timing_ms: how late you hear the music, from Settings > Timing (0..%d)\n"
               "timing_ms=%d\n",
            APPINI_TIMING_MAX, AppIni_ClampTiming(a->timingMs));
    return SafeFile_Close(f);
}
