#define _POSIX_C_SOURCE 200809L /* setsid */
#include "safefile.h"
#include "ghostlink.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LAUNCHER_DIRNAME "ghost-launcher"

static bool GetLauncherDir(char *out, size_t outSize) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        snprintf(out, outSize, "%s/%s", xdgData, LAUNCHER_DIRNAME);
        return true;
    }
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return false;
    snprintf(out, outSize, "%s/.local/share/%s", home, LAUNCHER_DIRNAME);
    return true;
}

static void MkdirParents(const char *path) {
    char cursor[600] = {0};
    for (const char *p = path; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - path);
            if (len > 0 && len < sizeof(cursor)) {
                memcpy(cursor, path, len);
                cursor[len] = '\0';
                mkdir(cursor, 0755);
            }
            if (*p == '\0') break;
        }
    }
}

static char *TrimNewline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
    return s;
}

/* '|' is the field separator, so it can't appear inside a field. */
static void CopySanitized(char *out, size_t outSize, const char *in) {
    snprintf(out, outSize, "%s", in);
    for (char *p = out; *p; p++) {
        if (*p == '|') *p = '/';
        else if (*p == '\n' || *p == '\r') *p = ' ';
    }
}

bool GhostLink_GetUsername(const char *gameName, char *out, size_t outSize) {
    snprintf(out, outSize, "%s", GHOSTLINK_DEFAULT_NAME);

    char dir[512], path[600];
    if (!GetLauncherDir(dir, sizeof(dir))) return false;
    snprintf(path, sizeof(path), "%s/profiles.txt", dir);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    /* A per-game override beats the arcade-wide name wherever the two rows
     * sit in the file; a blank override means "use the arcade name". */
    bool foundOwn = false, foundArcade = false;
    char arcade[GHOSTLINK_NAME_LEN] = "";
    char line[512];
    while (!foundOwn && fgets(line, sizeof(line), f)) {
        TrimNewline(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        if (sep[1] == '\0') continue;

        if (strcmp(line, gameName) == 0) {
            CopySanitized(out, outSize, sep + 1);
            foundOwn = true;
        } else if (strcmp(line, GHOSTLINK_ARCADE_KEY) == 0) {
            CopySanitized(arcade, sizeof(arcade), sep + 1);
            foundArcade = true;
        }
    }
    fclose(f);

    if (!foundOwn && foundArcade) snprintf(out, outSize, "%s", arcade);
    return foundOwn || foundArcade;
}

static bool GetScoresPath(const char *slug, bool global, char *out, size_t outSize, bool createDirs) {
    char dir[512], scoresDir[560];
    if (!GetLauncherDir(dir, sizeof(dir))) return false;
    snprintf(scoresDir, sizeof(scoresDir), "%s/scores%s", dir, global ? "/global" : "");
    if (createDirs) MkdirParents(scoresDir);
    snprintf(out, outSize, "%s/%s.txt", scoresDir, slug);
    return true;
}

static int LoadAll(const char *slug, bool global, GhostScore *entries, int maxEntries) {
    char path[700];
    if (!GetScoresPath(slug, global, path, sizeof(path), false)) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int count = 0;
    char line[512];
    while (count < maxEntries && fgets(line, sizeof(line), f)) {
        TrimNewline(line);
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
        if (n < 4) continue; /* malformed row: skip rather than guess */

        GhostScore *e = &entries[count];
        snprintf(e->mode, sizeof(e->mode), "%s", fields[0]);
        snprintf(e->username, sizeof(e->username), "%s", fields[1]);
        /* The global board was downloaded: never let a control or escape byte through to the screen. */
        for (char *p = e->username; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) *p = '?';
        e->score = atol(fields[2]);
        snprintf(e->date, sizeof(e->date), "%s", fields[3]);
        if (e->score > 0) count++;
    }
    fclose(f);
    return count;
}

/* Mode ascending, then score descending. Ties keep their existing order
 * (insertion sort is stable), so an equal later score ranks below the
 * earlier one. */
static void SortEntries(GhostScore *entries, int count) {
    for (int i = 1; i < count; i++) {
        GhostScore key = entries[i];
        int j = i - 1;
        while (j >= 0) {
            int modeCmp = strcmp(entries[j].mode, key.mode);
            bool after = modeCmp > 0 || (modeCmp == 0 && entries[j].score < key.score);
            if (!after) break;
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
}

/* Daily boards are one mode per day, so without pruning the file grows
 * forever and would eventually hit the row cap (dropping real scores). */
static int PruneOldDailies(GhostScore *entries, int count) {
    time_t cutoffTime = time(NULL) - (time_t)GHOSTLINK_DAILY_KEEP_DAYS * 86400;
    struct tm t;
    char cutoff[GHOSTLINK_MODE_LEN];
    if (!localtime_r(&cutoffTime, &t)) return count;
    snprintf(cutoff, sizeof(cutoff), "daily-%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    int kept = 0;
    for (int i = 0; i < count; i++) {
        bool oldDaily = strncmp(entries[i].mode, "daily-", 6) == 0 && strlen(entries[i].mode) == 14 && strcmp(entries[i].mode, cutoff) < 0;
        if (!oldDaily) entries[kept++] = entries[i];
    }
    return kept;
}

int GhostLink_SubmitScore(const char *slug, const char *mode, const char *username,
                           long score, const char *date) {
    if (score <= 0) return 0;

    static GhostScore entries[GHOSTLINK_MAX_ENTRIES + 1];
    int count = LoadAll(slug, false, entries, GHOSTLINK_MAX_ENTRIES);
    count = PruneOldDailies(entries, count);

    GhostScore *added = &entries[count++];
    CopySanitized(added->mode, sizeof(added->mode), mode);
    CopySanitized(added->username, sizeof(added->username),
                  (username && username[0]) ? username : GHOSTLINK_DEFAULT_NAME);
    added->score = score;
    CopySanitized(added->date, sizeof(added->date), date ? date : "");
    char addedMode[GHOSTLINK_MODE_LEN];
    snprintf(addedMode, sizeof(addedMode), "%s", added->mode);

    SortEntries(entries, count);

    /* The new row is the LAST one among equal mode+score rows (stable sort,
     * it was appended last), which is what rank lookup below relies on. */
    int rank = 0;
    int posInMode = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].mode, addedMode) != 0) continue;
        posInMode++;
        if (entries[i].score >= score) rank = posInMode;
    }
    if (rank > GHOSTLINK_TOP_N) rank = 0;

    char path[700];
    if (!GetScoresPath(slug, false, path, sizeof(path), true)) return 0;
    FILE *f = SafeFile_Open(path);
    if (!f) return 0;

    fprintf(f, "# Ghost Arcade scores for %s: mode|username|score|date\n", slug);
    const char *currentMode = "";
    int keptInMode = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].mode, currentMode) != 0) {
            currentMode = entries[i].mode;
            keptInMode = 0;
        }
        if (keptInMode >= GHOSTLINK_TOP_N) continue;
        fprintf(f, "%s|%s|%ld|%s\n", entries[i].mode, entries[i].username,
                entries[i].score, entries[i].date);
        keptInMode++;
    }
    SafeFile_Close(f);
    return rank;
}

static int LoadForMode(const char *slug, bool global, const char *mode, GhostScore *out, int maxOut) {
    static GhostScore entries[GHOSTLINK_MAX_ENTRIES];
    int count = LoadAll(slug, global, entries, GHOSTLINK_MAX_ENTRIES);
    SortEntries(entries, count);

    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++) {
        if (strcmp(entries[i].mode, mode) == 0) out[n++] = entries[i];
    }
    return n;
}

int GhostLink_LoadScores(const char *slug, const char *mode, GhostScore *out, int maxOut) {
    return LoadForMode(slug, false, mode, out, maxOut);
}

int GhostLink_LoadGlobalScores(const char *slug, const char *mode, GhostScore *out, int maxOut) {
    return LoadForMode(slug, true, mode, out, maxOut);
}

void GhostLink_TriggerSync(void) {
    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /* Double fork: the grandchild is re-parented to init, so the game
         * never has to reap it and no zombie is left behind. */
        pid_t worker = fork();
        if (worker != 0) _exit(0);

        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }

        execlp("ghost-sync", "ghost-sync", "--quiet", (char *)NULL);

        /* Not on PATH (e.g. launched from a bare .desktop environment): try
         * the default `make install` location before giving up. */
        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            char fallback[600];
            snprintf(fallback, sizeof(fallback), "%s/.local/bin/ghost-sync", home);
            execl(fallback, "ghost-sync", "--quiet", (char *)NULL);
        }
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0); /* the intermediate child exits at once */
}
