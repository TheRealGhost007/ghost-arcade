/* The Ghost Arcade data contract, ghost-common/ghostlink.c: the player's name
 * from the launcher, local score tables, the world-board cache ghost-sync
 * writes, and the background sync trigger. These lived in Coilrush's suite
 * until Coilrush left the arcade (2026-10-21); Brickburst is only the sample
 * game here. They run against a throwaway XDG_DATA_HOME, so they can never
 * touch the real launcher data, and they run last: the sync test clobbers
 * PATH and HOME. */
#define _POSIX_C_SOURCE 200809L /* setenv, mkdtemp, localtime_r */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ghostlink.h"

void test_ghostlink_all(int *checks, int *failures);

static int *sChecks, *sFailures;

#define CHECK(cond, msg) do { \
    (*sChecks)++; \
    if (!(cond)) { \
        (*sFailures)++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static char gTmpRoot[512];

static void WriteFile(const char *relPath, const char *content) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", gTmpRoot, relPath);
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "ghostlink: test fixture write failed"); return; }
    fputs(content, f);
    fclose(f);
}

static void test_ghostlink_username(void) {
    char name[GHOSTLINK_NAME_LEN];

    CHECK(!GhostLink_GetUsername("Brickburst", name, sizeof(name)), "profile: no launcher dir -> not found");
    CHECK(strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0, "profile: falls back to PLAYER");

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "%s/ghost-launcher", gTmpRoot);
    CHECK(mkdir(cmd, 0755) == 0, "profile: fixture dir created");

    WriteFile("ghost-launcher/profiles.txt",
              "# Ghost Launcher per-game profiles: game_name|username\n"
              "Blockfall|blockmaster\n"
              "Brickburst|TheRealGhost\n");
    CHECK(GhostLink_GetUsername("Brickburst", name, sizeof(name)), "profile: entry found");
    CHECK(strcmp(name, "TheRealGhost") == 0, "profile: right game's name returned");
    CHECK(GhostLink_GetUsername("Blockfall", name, sizeof(name)) && strcmp(name, "blockmaster") == 0,
          "profile: lookup is per game");
    CHECK(!GhostLink_GetUsername("Nope", name, sizeof(name)) && strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0,
          "profile: unknown game -> PLAYER");

    WriteFile("ghost-launcher/profiles.txt", "Brickburst|\n");
    CHECK(!GhostLink_GetUsername("Brickburst", name, sizeof(name)) && strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0,
          "profile: empty name -> PLAYER");

    WriteFile("ghost-launcher/profiles.txt", "Brickburst|a|b\n");
    CHECK(GhostLink_GetUsername("Brickburst", name, sizeof(name)) && strcmp(name, "a/b") == 0,
          "profile: separator inside a name is neutralised");

    WriteFile("ghost-launcher/profiles.txt", "Blockfall|blockmaster\n" GHOSTLINK_ARCADE_KEY "|Ghost\n");
    CHECK(GhostLink_GetUsername("Brickburst", name, sizeof(name)) && strcmp(name, "Ghost") == 0,
          "profile: no entry of its own -> the arcade-wide name");
    CHECK(GhostLink_GetUsername("Blockfall", name, sizeof(name)) && strcmp(name, "blockmaster") == 0,
          "profile: an override still beats the arcade name");
    WriteFile("ghost-launcher/profiles.txt", GHOSTLINK_ARCADE_KEY "|Ghost\nBrickburst|Snek\n");
    CHECK(GhostLink_GetUsername("Brickburst", name, sizeof(name)) && strcmp(name, "Snek") == 0,
          "profile: override wins even when it comes after the arcade row");
    WriteFile("ghost-launcher/profiles.txt", "Brickburst|\n" GHOSTLINK_ARCADE_KEY "|Ghost\n");
    CHECK(GhostLink_GetUsername("Brickburst", name, sizeof(name)) && strcmp(name, "Ghost") == 0,
          "profile: blank override falls back to the arcade name");

    char tiny[4];
    WriteFile("ghost-launcher/profiles.txt", "Brickburst|LongUserName\n");
    GhostLink_GetUsername("Brickburst", tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 3, "profile: output truncated safely to the buffer");
}

static void test_ghostlink_scores(void) {
    GhostScore out[GHOSTLINK_TOP_N + 4];

    CHECK(GhostLink_LoadScores("brickburst", "classic", out, GHOSTLINK_TOP_N) == 0, "scores: empty before any run");
    CHECK(GhostLink_SubmitScore("brickburst", "classic", "ghost", 0, "2026-09-19") == 0, "scores: zero score ignored");
    CHECK(GhostLink_SubmitScore("brickburst", "classic", "ghost", -5, "2026-09-19") == 0, "scores: negative score ignored");

    CHECK(GhostLink_SubmitScore("brickburst", "classic", "ghost", 100, "2026-09-19") == 1, "scores: first run is rank 1");
    CHECK(GhostLink_SubmitScore("brickburst", "classic", "ghost", 300, "2026-09-19") == 1, "scores: better run takes rank 1");
    CHECK(GhostLink_SubmitScore("brickburst", "classic", "anna", 200, "2026-09-20") == 2, "scores: middle run slots in at 2");
    CHECK(GhostLink_SubmitScore("brickburst", "classic", "late", 200, "2026-09-21") == 3, "scores: a tie ranks below the earlier run");
    CHECK(GhostLink_SubmitScore("brickburst", "maze", "ghost", 50, "2026-09-19") == 1, "scores: modes rank independently");

    int n = GhostLink_LoadScores("brickburst", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 4, "scores: four classic rows persisted");
    CHECK(out[0].score == 300 && out[1].score == 200 && out[2].score == 200 && out[3].score == 100,
          "scores: loaded best-first");
    CHECK(strcmp(out[1].username, "anna") == 0 && strcmp(out[2].username, "late") == 0, "scores: tie order is stable");
    CHECK(strcmp(out[1].date, "2026-09-20") == 0, "scores: date round-trips");
    CHECK(strcmp(out[0].mode, "classic") == 0, "scores: mode round-trips");
    CHECK(GhostLink_LoadScores("brickburst", "maze", out, GHOSTLINK_TOP_N) == 1, "scores: maze table separate");
    CHECK(GhostLink_LoadScores("brickburst", "wrap", out, GHOSTLINK_TOP_N) == 0, "scores: untouched mode is empty");
    CHECK(GhostLink_LoadScores("otherslug", "classic", out, GHOSTLINK_TOP_N) == 0, "scores: files are per game slug");
    CHECK(GhostLink_LoadScores("brickburst", "classic", out, 2) == 2, "scores: maxOut respected");

    /* Fill past the cap: only the top N survive, and a run too low to make
     * the table reports rank 0. */
    for (int i = 0; i < GHOSTLINK_TOP_N + 3; i++) {
        GhostLink_SubmitScore("brickburst", "wrap", "filler", 1000 + i * 10, "2026-09-19");
    }
    n = GhostLink_LoadScores("brickburst", "wrap", out, GHOSTLINK_TOP_N + 4);
    CHECK(n == GHOSTLINK_TOP_N, "scores: table trimmed to the top N");
    CHECK(out[0].score == 1000 + (GHOSTLINK_TOP_N + 2) * 10, "scores: best kept at the top");
    CHECK(out[GHOSTLINK_TOP_N - 1].score == 1030, "scores: lowest three dropped");
    CHECK(GhostLink_SubmitScore("brickburst", "wrap", "weak", 5, "2026-09-19") == 0, "scores: below the cut -> rank 0");
    CHECK(GhostLink_LoadScores("brickburst", "wrap", out, GHOSTLINK_TOP_N + 4) == GHOSTLINK_TOP_N, "scores: still N rows after a miss");
    CHECK(GhostLink_LoadScores("brickburst", "classic", out, GHOSTLINK_TOP_N) == 4, "scores: trimming one mode leaves others alone");

    /* Hostile / sloppy input. */
    CHECK(GhostLink_SubmitScore("brickburst", "maze", "pipe|name", 75, "2026-09-19") == 1, "scores: name with separator accepted");
    n = GhostLink_LoadScores("brickburst", "maze", out, GHOSTLINK_TOP_N);
    CHECK(n == 2 && strcmp(out[0].username, "pipe/name") == 0, "scores: separator neutralised so the row still parses");
    CHECK(GhostLink_SubmitScore("brickburst", "maze", "", 60, "2026-09-19") == 2, "scores: empty name accepted");
    n = GhostLink_LoadScores("brickburst", "maze", out, GHOSTLINK_TOP_N);
    CHECK(n == 3 && strcmp(out[1].username, GHOSTLINK_DEFAULT_NAME) == 0, "scores: empty name stored as PLAYER");

    WriteFile("ghost-launcher/scores/junk.txt",
              "# comment\n"
              "\n"
              "classic|ok|40|2026-01-01\n"
              "not a row at all\n"
              "classic|missing-date|99\n"
              "classic|zero|0|2026-01-01\n"
              "classic|best|70|2026-01-02\n");
    n = GhostLink_LoadScores("junk", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 2, "scores: malformed and zero rows skipped");
    CHECK(out[0].score == 70 && out[1].score == 40, "scores: hand-edited file is sorted on load");
}

static void test_ghostlink_many_modes(void) {
    /* A file with far more rows than the old 128 cap, in many modes: recording
     * one more run must not lose the modes that sort last. */
    GhostScore out[GHOSTLINK_TOP_N];
    char mode[GHOSTLINK_MODE_LEN];
    for (int m = 0; m < 30; m++) {
        snprintf(mode, sizeof(mode), "size-%02d", m);
        for (int k = 0; k < 6; k++) GhostLink_SubmitScore("manymodes", mode, "ghost", 100 + k, "2026-09-20");
    }
    CHECK(GhostLink_SubmitScore("manymodes", "aaa-first", "ghost", 5, "2026-09-20") == 1, "many modes: a new board still records");
    CHECK(GhostLink_LoadScores("manymodes", "size-29", out, GHOSTLINK_TOP_N) == 6, "many modes: the last-sorting board survives (180+ rows)");
    CHECK(GhostLink_LoadScores("manymodes", "size-00", out, GHOSTLINK_TOP_N) == 6, "many modes: and so does the first");

    /* Daily boards: today's is kept, an ancient one is pruned, ordinary modes never are. */
    GhostLink_SubmitScore("dailies", "daily-20200101", "ghost", 50, "2020-01-01");
    GhostLink_SubmitScore("dailies", "arcade", "ghost", 70, "2020-01-01");
    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    snprintf(mode, sizeof(mode), "daily-%04d%02d%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
    GhostLink_SubmitScore("dailies", mode, "ghost", 60, "2026-09-20");
    CHECK(GhostLink_LoadScores("dailies", mode, out, GHOSTLINK_TOP_N) == 1, "dailies: today's board is kept");
    CHECK(GhostLink_LoadScores("dailies", "daily-20200101", out, GHOSTLINK_TOP_N) == 0, "dailies: a years-old daily board is pruned");
    CHECK(GhostLink_LoadScores("dailies", "arcade", out, GHOSTLINK_TOP_N) == 1, "dailies: normal boards are never pruned, however old");
}

static void test_ghostlink_global_and_sync(void) {
    GhostScore out[GHOSTLINK_TOP_N];
    CHECK(GhostLink_LoadGlobalScores("brickburst", "classic", out, GHOSTLINK_TOP_N) == 0,
          "global: empty before any sync (local scores don't leak in)");

    char dir[700];
    snprintf(dir, sizeof(dir), "%s/ghost-launcher/scores/global", gTmpRoot);
    CHECK(mkdir(dir, 0755) == 0, "global: fixture dir created");
    WriteFile("ghost-launcher/scores/global/brickburst.txt",
              "classic|worldbest|9000|2026-09-01\n"
              "classic|second|4000|2026-09-02\n"
              "maze|mazer|700|2026-09-03\n");
    int n = GhostLink_LoadGlobalScores("brickburst", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 2 && out[0].score == 9000 && strcmp(out[0].username, "worldbest") == 0,
          "global: reads the cache ghost-sync writes");
    CHECK(GhostLink_LoadGlobalScores("brickburst", "maze", out, GHOSTLINK_TOP_N) == 1, "global: per mode");
    n = GhostLink_LoadScores("brickburst", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 4 && out[0].score == 300, "global: the local table is untouched by the cache");

    /* With no helper anywhere on PATH or under HOME, triggering a sync must
     * return promptly and harmlessly (the game may be installed alone). */
    setenv("PATH", gTmpRoot, 1);
    setenv("HOME", gTmpRoot, 1);
    GhostLink_TriggerSync();
    GhostLink_TriggerSync();
    CHECK(true, "sync: trigger with no helper installed is a silent no-op");
}

static void RemoveTree(void) {
    /* Fixed, known set of files: no recursive delete needed. */
    const char *files[] = {
        "ghost-launcher/scores/global/brickburst.txt",
        "ghost-launcher/scores/brickburst.txt", "ghost-launcher/scores/junk.txt",
        "ghost-launcher/scores/manymodes.txt", "ghost-launcher/scores/dailies.txt",
        "ghost-launcher/profiles.txt",
    };
    char path[700];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", gTmpRoot, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/ghost-launcher/scores/global", gTmpRoot); rmdir(path);
    snprintf(path, sizeof(path), "%s/ghost-launcher/scores", gTmpRoot); rmdir(path);
    snprintf(path, sizeof(path), "%s/ghost-launcher", gTmpRoot); rmdir(path);
    if (rmdir(gTmpRoot) != 0) printf("note: %s was not empty, so it was left in place\n", gTmpRoot);
}

void test_ghostlink_all(int *checks, int *failures) {
    sChecks = checks;
    sFailures = failures;
    const char *tmpBase = getenv("TMPDIR");
    snprintf(gTmpRoot, sizeof(gTmpRoot), "%s/brickburst-ghostlink-XXXXXX", (tmpBase && tmpBase[0]) ? tmpBase : "/tmp");
    if (mkdtemp(gTmpRoot) == NULL) {
        CHECK(false, "ghostlink: could not create a temp dir");
        return;
    }
    setenv("XDG_DATA_HOME", gTmpRoot, 1);
    test_ghostlink_username();
    test_ghostlink_scores();
    test_ghostlink_many_modes();
    test_ghostlink_global_and_sync(); /* last: it clobbers PATH and HOME */
    RemoveTree();
}
