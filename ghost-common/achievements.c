#define _POSIX_C_SOURCE 200809L
#include "safefile.h"
#include "achievements.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const AchDef kDefs[] = {
    {"blockfall", "r0", "First Stack", "Finish a run of Blockfall", ACH_RUNS, 1},
    {"blockfall", "s1", "Tidy Rows", "Score 2,000 in one run", ACH_SCORE, 2000},
    {"blockfall", "s2", "Skyscraper", "Score 20,000 in one run", ACH_SCORE, 20000},
    {"blockfall", "l3", "Picking Up Speed", "Reach level 5", ACH_LEVEL, 5},
    {"blockfall", "l4", "Terminal Velocity", "Reach level 10", ACH_LEVEL, 10},
    {"blockfall", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"brickburst", "r0", "First Break", "Finish a run of Brickburst", ACH_RUNS, 1},
    {"brickburst", "s1", "Demolition", "Score 1,000 in one run", ACH_SCORE, 1000},
    {"brickburst", "s2", "Wrecking Crew", "Score 6,000 in one run", ACH_SCORE, 6000},
    {"brickburst", "l3", "Rubble", "Reach level 5", ACH_LEVEL, 5},
    {"brickburst", "l4", "Bulldozer", "Reach level 10", ACH_LEVEL, 10},
    {"brickburst", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"skyraid", "r0", "Scramble", "Finish a run of Skyraid", ACH_RUNS, 1},
    {"skyraid", "s1", "Ace", "Score 1,500 in one run", ACH_SCORE, 1500},
    {"skyraid", "s2", "Squadron Leader", "Score 6,000 in one run", ACH_SCORE, 6000},
    {"skyraid", "l3", "Holding the Line", "Reach wave 5", ACH_LEVEL, 5},
    {"skyraid", "l4", "Last Defender", "Reach wave 10", ACH_LEVEL, 10},
    {"skyraid", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"rockdrift", "r0", "Liftoff", "Finish a run of Rockdrift", ACH_RUNS, 1},
    {"rockdrift", "s1", "Rock Breaker", "Score 2,000 in one run", ACH_SCORE, 2000},
    {"rockdrift", "s2", "Field Clearer", "Score 10,000 in one run", ACH_SCORE, 10000},
    {"rockdrift", "l3", "Belt Runner", "Reach wave 5", ACH_LEVEL, 5},
    {"rockdrift", "l4", "Deep Space", "Reach wave 10", ACH_LEVEL, 10},
    {"rockdrift", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"crawlshot", "r0", "First Splat", "Finish a run of Crawlshot", ACH_RUNS, 1},
    {"crawlshot", "s1", "Bug Hunter", "Score 3,000 in one run", ACH_SCORE, 3000},
    {"crawlshot", "s2", "Exterminator", "Score 12,000 in one run", ACH_SCORE, 12000},
    {"crawlshot", "l3", "Garden Guard", "Reach wave 5", ACH_LEVEL, 5},
    {"crawlshot", "l4", "Fumigator", "Reach wave 10", ACH_LEVEL, 10},
    {"crawlshot", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"moondrop", "r0", "One Small Step", "Finish a run of Moondrop", ACH_RUNS, 1},
    {"moondrop", "s1", "Soft Touch", "Score 500 in one run", ACH_SCORE, 500},
    {"moondrop", "s2", "Pinpoint", "Score 2,000 in one run", ACH_SCORE, 2000},
    {"moondrop", "l3", "Frequent Flyer", "Reach level 5", ACH_LEVEL, 5},
    {"moondrop", "l4", "Lunar Legend", "Reach level 10", ACH_LEVEL, 10},
    {"moondrop", "r5", "Regular", "Play 25 runs", ACH_RUNS, 25},
    {"pondkings", "r0", "First Faceoff", "Finish a game of Pond Kings", ACH_RUNS, 1},
    {"pondkings", "s1", "Top Shelf", "Score 2,000 points in one Journey", ACH_SCORE, 2000},
    {"pondkings", "s2", "Hometown Hero", "Score 6,000 points in one Journey", ACH_SCORE, 6000},
    {"pondkings", "l3", "Town Team", "Win 3 games in one Journey", ACH_LEVEL, 3},
    {"pondkings", "l4", "Pond Kings", "Win all 9 games of a Journey", ACH_LEVEL, 9},
    {"pondkings", "r5", "Regular", "Play 25 games", ACH_RUNS, 25},
};
#define DEF_COUNT ((int)(sizeof(kDefs) / sizeof(kDefs[0])))

int Ach_Count(void) { return DEF_COUNT; }
const AchDef *Ach_At(int i) { return (i >= 0 && i < DEF_COUNT) ? &kDefs[i] : NULL; }

const AchDef *Ach_ForGame(const char *slug, int i) {
    int n = 0;
    for (int k = 0; k < DEF_COUNT; k++) if (strcmp(kDefs[k].slug, slug) == 0) { if (n == i) return &kDefs[k]; n++; }
    return NULL;
}

int Ach_CountForGame(const char *slug) {
    int n = 0;
    for (int k = 0; k < DEF_COUNT; k++) if (strcmp(kDefs[k].slug, slug) == 0) n++;
    return n;
}

static bool DirPath(char *out, size_t size, bool make) {
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char a[600], b[700];
    snprintf(a, sizeof(a), "%s/ghost-launcher", base);
    snprintf(b, sizeof(b), "%s/achievements", a);
    if (make) { mkdir(base, 0755); mkdir(a, 0755); if (mkdir(b, 0755) != 0 && errno != EEXIST) return false; }
    snprintf(out, size, "%s", b);
    return true;
}

static bool FilePath(char *out, size_t size, const char *slug, const char *ext, bool make) {
    char dir[700];
    if (!DirPath(dir, sizeof(dir), make)) return false;
    snprintf(out, size, "%s/%s.%s", dir, slug, ext);
    return true;
}

bool Ach_IsUnlocked(const char *slug, const char *id, char *date) {
    char path[800];
    if (!FilePath(path, sizeof(path), slug, "txt", false)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[96];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = '\0';
        if (strcmp(line, id) != 0) continue;
        found = true;
        if (date) { char *d = bar + 1; d[strcspn(d, "\r\n")] = '\0'; snprintf(date, 12, "%s", d); }
        break;
    }
    fclose(f);
    return found;
}

int Ach_UnlockedForGame(const char *slug) {
    int n = 0, total = Ach_CountForGame(slug);
    for (int i = 0; i < total; i++) { const AchDef *d = Ach_ForGame(slug, i); if (d && Ach_IsUnlocked(slug, d->id, NULL)) n++; }
    return n;
}

#define TOAST_MAX 6
static char sToasts[TOAST_MAX][64];
static int sToastCount = 0;

bool Ach_PopToast(char *title, size_t size) {
    if (sToastCount == 0) return false;
    snprintf(title, size, "%s", sToasts[0]);
    for (int i = 1; i < sToastCount; i++) memcpy(sToasts[i - 1], sToasts[i], sizeof(sToasts[0]));
    sToastCount--;
    return true;
}

static long ReadRuns(const char *slug) {
    char path[800];
    if (!FilePath(path, sizeof(path), slug, "runs", false)) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long n = 0;
    if (fscanf(f, "%ld", &n) != 1) n = 0;
    fclose(f);
    return n < 0 ? 0 : n;
}

int Ach_CheckRun(const char *slug, long score, int level) {
    char path[800];
    if (!FilePath(path, sizeof(path), slug, "runs", true)) return 0;
    long runs = ReadRuns(slug) + 1;
    FILE *rf = SafeFile_Open(path);
    if (rf) { fprintf(rf, "%ld\n", runs); SafeFile_Close(rf); }

    char date[12];
    time_t now = time(NULL);
    struct tm t;
    if (localtime_r(&now, &t) == NULL || strftime(date, sizeof(date), "%Y-%m-%d", &t) == 0) snprintf(date, sizeof(date), "unknown");
    char listPath[800];
    if (!FilePath(listPath, sizeof(listPath), slug, "txt", true)) return 0;

    int unlocked = 0;
    for (int k = 0; k < DEF_COUNT; k++) {
        const AchDef *d = &kDefs[k];
        if (strcmp(d->slug, slug) != 0) continue;
        bool met = (d->kind == ACH_RUNS && runs >= d->threshold) || (d->kind == ACH_SCORE && score >= d->threshold) || (d->kind == ACH_LEVEL && (long)level >= d->threshold);
        if (!met || Ach_IsUnlocked(slug, d->id, NULL)) continue;
        FILE *f = fopen(listPath, "a");
        if (!f) continue;
        fprintf(f, "%s|%s\n", d->id, date);
        fclose(f);
        unlocked++;
        if (sToastCount < TOAST_MAX) snprintf(sToasts[sToastCount++], sizeof(sToasts[0]), "%s", d->title);
    }
    return unlocked;
}
