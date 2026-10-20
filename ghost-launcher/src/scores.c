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

bool Scores_FilePath(const char *slug, bool global, char *out, size_t outSize) {
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

bool Scores_IsTimeBoard(const char *slug, const char *mode) {
    if (!slug || !mode || strcmp(slug, "blockfall") != 0) return false;
    return strcmp(mode, "sprint40") == 0 || strncmp(mode, "sprint40-", 9) == 0;
}

void Scores_FormatScore(bool timed, long score, char *out, size_t size) {
    if (!timed) { snprintf(out, size, "%ld", score); return; }
    long centis = (score > 0 && score <= SCORES_TIME_BASE) ? SCORES_TIME_BASE - score : 0;
    snprintf(out, size, "%ld:%02ld.%02ld", centis / 6000, (centis / 100) % 60, centis % 100);
}

ScoreBest Scores_LoadBest(const char *slug, bool global) {
    ScoreBest best = {0}, fastest = {0};

    char path[700];
    if (!Scores_FilePath(slug, global, path, sizeof(path))) return best;
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
        ScoreBest *into = Scores_IsTimeBoard(slug, fields[0]) ? &fastest : &best;
        if (score <= 0 || (into->found && score <= into->score)) continue;
        into->found = true;
        into->timed = into == &fastest;
        into->score = score;
        snprintf(into->mode, sizeof(into->mode), "%s", fields[0]);
        snprintf(into->username, sizeof(into->username), "%s", fields[1]);
    }
    fclose(f);
    return best.found ? best : fastest;
}
