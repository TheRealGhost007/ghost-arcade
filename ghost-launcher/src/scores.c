#include "scores.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_DIRNAME "ghost-launcher"

bool Scores_SlugFromExec(const char *execPath, char *out, size_t outSize) {
    const char *base = strrchr(execPath, '/');
    base = base ? base + 1 : execPath;

    size_t len = strlen(base);
    if (len == 0 || len >= outSize) return false;
    for (size_t i = 0; i < len; i++) {
        char c = base[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    memcpy(out, base, len + 1);
    return true;
}

static bool ScoresPath(const char *slug, bool global, char *out, size_t outSize) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *sub = global ? "scores/global" : "scores";
    if (xdgData && xdgData[0] != '\0') {
        snprintf(out, outSize, "%s/%s/%s/%s.txt", xdgData, APP_DIRNAME, sub, slug);
        return true;
    }
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return false;
    snprintf(out, outSize, "%s/.local/share/%s/%s/%s.txt", home, APP_DIRNAME, sub, slug);
    return true;
}

ScoreBest Scores_LoadBest(const char *slug, bool global) {
    ScoreBest best = {0};

    char path[700];
    if (!ScoresPath(slug, global, path, sizeof(path))) return best;
    FILE *f = fopen(path, "r");
    if (!f) return best;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        char *fields[4];
        int n = 0;
        char *cursor = line;
        while (n < 4) {
            fields[n++] = cursor;
            char *sep = strchr(cursor, '|');
            if (!sep) break;
            *sep = '\0';
            cursor = sep + 1;
        }
        if (n < 4) continue;

        long score = atol(fields[2]);
        if (score <= 0 || (best.found && score <= best.score)) continue;
        best.found = true;
        best.score = score;
        snprintf(best.mode, sizeof(best.mode), "%s", fields[0]);
        snprintf(best.username, sizeof(best.username), "%s", fields[1]);
    }
    fclose(f);
    return best;
}
