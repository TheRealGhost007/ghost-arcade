#include "cave.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const kValid = "#B:.o*PXfg";

bool Cave_Parse(const char *text, Cave *out, const char **err) {
    static const char *reason;
    memset(out, 0, sizeof(*out));
    out->quota = -1;
    out->timeLimit = -1;
    bool inMap = false;
    int row = 0, players = 0, exits = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t rawLen = eol ? (size_t)(eol - p) : strlen(p);
        size_t len = rawLen;
        if (len > 0 && p[len - 1] == '\r') len--;
        if (!inMap) {
            if (len == 3 && strncmp(p, "map", 3) == 0) inMap = true;
            else if (len > 5 && strncmp(p, "name=", 5) == 0) snprintf(out->name, sizeof(out->name), "%.*s", (int)(len - 5), p + 5);
            else if (len > 5 && strncmp(p, "gems=", 5) == 0) out->quota = atoi(p + 5);
            else if (len > 5 && strncmp(p, "time=", 5) == 0) out->timeLimit = atoi(p + 5);
            else if (len >= 9 && strncmp(p, "solution=", 9) == 0) {
                size_t n = len - 9;
                if (n >= CAVE_SOLUTION_MAX) { reason = "solution too long"; if (err) *err = reason; return false; }
                memcpy(out->solution, p + 9, n);
                out->solution[n] = '\0';
            }
        } else if (len > 0) {
            if (row >= CAVE_ROWS) { reason = "too many map rows"; if (err) *err = reason; return false; }
            if (len != CAVE_COLS) { reason = "a map row is not 40 wide"; if (err) *err = reason; return false; }
            for (int i = 0; i < CAVE_COLS; i++) {
                char c = p[i];
                if (!strchr(kValid, c)) { reason = "unknown map character"; if (err) *err = reason; return false; }
                if (c == 'P') players++;
                if (c == 'X') exits++;
                out->map[row][i] = c;
            }
            out->map[row][CAVE_COLS] = '\0';
            row++;
        }
        p = eol ? eol + 1 : p + rawLen; /* advance by what was really there, not the trimmed length */
    }
    if (row != CAVE_ROWS) { reason = "the map is not 22 rows"; if (err) *err = reason; return false; }
    if (players != 1) { reason = "a cave needs exactly one P"; if (err) *err = reason; return false; }
    if (exits != 1) { reason = "a cave needs exactly one X"; if (err) *err = reason; return false; }
    if (out->quota < 0 || out->timeLimit <= 0) { reason = "missing gems= or time="; if (err) *err = reason; return false; }
    if (out->name[0] == '\0') snprintf(out->name, sizeof(out->name), "Untitled");
    return true;
}

bool Cave_LoadFile(const char *path, Cave *out, const char **err) {
    FILE *f = fopen(path, "rb");
    if (!f) { if (err) *err = "cannot open the file"; return false; }
    static char buf[CAVE_SOLUTION_MAX + 4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return Cave_Parse(buf, out, err);
}

bool Cave_Write(const char *path, const Cave *cave) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "name=%s\ngems=%d\ntime=%d\nsolution=%s\nmap\n", cave->name, cave->quota, cave->timeLimit, cave->solution);
    for (int r = 0; r < CAVE_ROWS; r++) fprintf(f, "%s\n", cave->map[r]);
    fclose(f);
    return true;
}
