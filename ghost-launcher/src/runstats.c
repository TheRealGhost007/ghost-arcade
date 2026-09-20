#define _POSIX_C_SOURCE 200809L
#include "runstats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int RunStats_DaysAgo(const char *date, time_t now) {
    int y, m, d;
    if (!date || sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
    struct tm t = {0}, today;
    if (!localtime_r(&now, &today)) return -1;
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12; t.tm_isdst = -1;
    today.tm_hour = 12; today.tm_min = 0; today.tm_sec = 0; today.tm_isdst = -1;
    time_t a = mktime(&t), b = mktime(&today);
    if (a == (time_t)-1 || b == (time_t)-1) return -1;
    double days = difftime(b, a) / 86400.0;
    return (int)(days + (days >= 0 ? 0.5 : -0.5));
}

void RunStats_Load(const char *slug, time_t now, RunSummary *out) {
    memset(out, 0, sizeof(*out));
    if (!slug || !slug[0] || strchr(slug, '/')) return;
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char path[800];
    snprintf(path, sizeof(path), "%s/%s/runs.log", base, slug);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        /* YYYY-MM-DD HH:MM:SS|mode|score|level */
        if (strlen(line) < 20 || line[4] != '-' || line[7] != '-') continue;
        char date[11];
        memcpy(date, line, 10);
        date[10] = '\0';
        int ago = RunStats_DaysAgo(date, now);
        if (ago < 0) continue;
        out->runs++;
        const char *p = strchr(line, '|');
        if (p) p = strchr(p + 1, '|');
        if (p && atol(p + 1) <= 0) out->zeroRuns++;
        if (strcmp(date, out->last) > 0) memcpy(out->last, date, 11);
        if (ago < RUNSTATS_DAYS) out->perDay[ago]++;
    }
    fclose(f);
}
