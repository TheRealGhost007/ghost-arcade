#define _POSIX_C_SOURCE 200809L
#include "runlog.h"
#include "safefile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The log is append-only, so keep it from growing forever: past 256 KB the
 * oldest half is dropped (whole lines only). */
#define RUNLOG_MAX_BYTES (256 * 1024)
static void TrimIfHuge(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= RUNLOG_MAX_BYTES) return;
    FILE *in = fopen(path, "r");
    if (!in) return;
    long keep = RUNLOG_MAX_BYTES / 2;
    char *buf = (char *)malloc((size_t)keep + 1);
    if (!buf || fseek(in, -keep, SEEK_END) != 0) { free(buf); fclose(in); return; }
    size_t n = fread(buf, 1, (size_t)keep, in);
    fclose(in);
    buf[n] = '\0';
    char *start = strchr(buf, '\n'); /* skip the partial first line */
    if (start) {
        FILE *out = SafeFile_Open(path);
        if (out) { fputs(start + 1, out); SafeFile_Close(out); }
    }
    free(buf);
}

void RunLog_Append(const char *slug, const char *mode, long score, int level) {
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char dir[600], path[700];
    snprintf(dir, sizeof(dir), "%s/%s", base, slug);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return;
    snprintf(path, sizeof(path), "%s/runs.log", dir);

    char stamp[24];
    time_t now = time(NULL);
    struct tm t;
    if (localtime_r(&now, &t) == NULL || strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &t) == 0) snprintf(stamp, sizeof(stamp), "unknown");
    TrimIfHuge(path);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s|%s|%ld|%d\n", stamp, mode ? mode : "", score, level);
    fclose(f);
}
