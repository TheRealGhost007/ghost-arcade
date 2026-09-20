/* Headless tests for the launcher's raylib-free modules: the score-file
 * reader and profiles (src/), and ghost-sync's data layer (sync/): config,
 * install id, local score parsing/validation, upload CSV, board CSV parsing
 * and the global cache files. No raylib, no libcurl, no network -- everything
 * runs against a throwaway XDG_DATA_HOME / XDG_CONFIG_HOME. */
#define _DEFAULT_SOURCE /* usleep */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../sync/syncdata.h"
#include "../sync/online_config.h"
#include "../src/scores.h"
#include "../src/profile.h"
#include "../src/manifest.h"
#include "../src/launch.h"
#include "achievements.h"
#include "daily.h"
#include "tuning.h"
#include "howto.h"
#include "safefile.h"
#include "../src/runstats.h"
#include "../src/adminlock.h"
#include "../src/sha256.h"
#include "../sync/updatecheck.h"
#include <time.h>
#include "prefs.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static char gTmpRoot[512];

static void WriteFile(const char *relPath, const char *content) {
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", gTmpRoot, relPath);
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "fixture write failed"); return; }
    fputs(content, f);
    fclose(f);
}

static bool ReadFile(const char *relPath, char *out, size_t outSize) {
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", gTmpRoot, relPath);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(out, 1, outSize - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void test_validators(void) {
    CHECK(Sync_IsSlug("coilrush", 32), "slug: plain name ok");
    CHECK(Sync_IsSlug("daily-20260919", 32), "slug: digits and dashes ok");
    CHECK(!Sync_IsSlug("", 32), "slug: empty rejected");
    CHECK(!Sync_IsSlug("Coilrush", 32), "slug: uppercase rejected");
    CHECK(!Sync_IsSlug("../etc", 32), "slug: path traversal rejected");
    CHECK(!Sync_IsSlug("a/b", 32), "slug: slash rejected");
    CHECK(!Sync_IsSlug("a b", 32), "slug: space rejected");
    CHECK(!Sync_IsSlug("abcdefghijklmnopqrstuvwxyz0123456", 32), "slug: over-long rejected");

    CHECK(Sync_IsDate("2026-09-19"), "date: valid");
    CHECK(!Sync_IsDate("unknown"), "date: placeholder rejected");
    CHECK(!Sync_IsDate("2026-13-01"), "date: month 13 rejected");
    CHECK(!Sync_IsDate("2026-00-10"), "date: month 0 rejected");
    CHECK(!Sync_IsDate("2026-09-32"), "date: day 32 rejected");
    CHECK(!Sync_IsDate("2026/09/19"), "date: wrong separators rejected");
    CHECK(!Sync_IsDate("2026-9-19"), "date: unpadded rejected");
}

static void test_parse_local_line(void) {
    SyncRow r;
    CHECK(Sync_ParseLocalLine("coilrush", "classic|ghost|120|2026-09-19\n", &r), "line: good row parses");
    CHECK(strcmp(r.game, "coilrush") == 0 && strcmp(r.mode, "classic") == 0, "line: game and mode");
    CHECK(strcmp(r.username, "ghost") == 0 && r.score == 120 && strcmp(r.date, "2026-09-19") == 0, "line: fields");

    CHECK(!Sync_ParseLocalLine("coilrush", "# comment", &r), "line: comment skipped");
    CHECK(!Sync_ParseLocalLine("coilrush", "\n", &r), "line: blank skipped");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|120", &r), "line: missing field rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|0|2026-09-19", &r), "line: zero score rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|-4|2026-09-19", &r), "line: negative score rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|12x|2026-09-19", &r), "line: non-numeric score rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|999999999999|2026-09-19", &r), "line: absurd score rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "classic|ghost|120|unknown", &r), "line: bad date rejected");
    CHECK(!Sync_ParseLocalLine("coilrush", "Classic Mode|ghost|120|2026-09-19", &r), "line: bad mode rejected");
    CHECK(!Sync_ParseLocalLine("../evil", "classic|ghost|120|2026-09-19", &r), "line: bad game slug rejected");

    CHECK(Sync_ParseLocalLine("coilrush", "classic||50|2026-09-19", &r) && strcmp(r.username, "PLAYER") == 0,
          "name: empty becomes PLAYER");
    CHECK(Sync_ParseLocalLine("coilrush", "classic|   |50|2026-09-19", &r) && strcmp(r.username, "PLAYER") == 0,
          "name: all-spaces becomes PLAYER");
    CHECK(Sync_ParseLocalLine("coilrush", "classic|  padded  |50|2026-09-19", &r) && strcmp(r.username, "padded") == 0,
          "name: trimmed");
    CHECK(Sync_ParseLocalLine("coilrush", "classic|tab\there|50|2026-09-19", &r) && strcmp(r.username, "tabhere") == 0,
          "name: control characters stripped");
    CHECK(Sync_ParseLocalLine("coilrush", "classic|caf\xc3\xa9|50|2026-09-19", &r) && strcmp(r.username, "caf??") == 0,
          "name: non-ASCII bytes neutralised (never sends broken UTF-8)");
    CHECK(Sync_ParseLocalLine("coilrush",
                              "classic|0123456789012345678901234567890123456789|50|2026-09-19", &r),
          "name: over-long accepted");
    CHECK(strlen(r.username) == 32, "name: clipped to the server's 32-char limit");
}

static void test_upload_json(void) {
    SyncRow rows[2];
    Sync_ParseLocalLine("coilrush", "classic|ghost|120|2026-09-19", &rows[0]);
    Sync_ParseLocalLine("coilrush", "maze|say \"hi\" \\ ok|75|2026-09-20", &rows[1]);

    char body[1024];
    size_t n = Sync_BuildUploadJson(rows, 2, "11111111-2222-4333-8444-555555555555", body, sizeof(body));
    CHECK(n > 0 && n == strlen(body), "json: length reported");
    const char *expected =
        "{\"p_install_id\":\"11111111-2222-4333-8444-555555555555\",\"p_rows\":["
        "{\"game\":\"coilrush\",\"mode\":\"classic\",\"username\":\"ghost\",\"score\":120,\"played_on\":\"2026-09-19\"},"
        "{\"game\":\"coilrush\",\"mode\":\"maze\",\"username\":\"say \\\"hi\\\" / ok\",\"score\":75,\"played_on\":\"2026-09-20\"}]}";
    CHECK(strcmp(body, expected) == 0, "json: exact body, quotes escaped, backslash neutralised");

    /* A name made of nothing but quotes cannot break out of its string. */
    Sync_ParseLocalLine("coilrush", "classic|\"\"\"\"\"\"\"\"|5|2026-09-19", &rows[0]);
    n = Sync_BuildUploadJson(rows, 1, "x", body, sizeof(body));
    int bare = 0, escaped = 0;
    for (size_t i = 1; i < n; i++) if (body[i] == '"') { if (body[i - 1] == '\\') escaped++; else bare++; }
    /* 12 structural strings (7 keys + 5 string values) = 24 bare quotes; the name's 8 are all escaped */
    CHECK(n > 0 && bare == 24 && escaped == 8, "json: every quote in a hostile name is escaped");

    char tiny[40];
    CHECK(Sync_BuildUploadJson(rows, 1, "x", tiny, sizeof(tiny)) == 0, "json: reports 0 when the buffer is too small");
    CHECK(Sync_BuildUploadJson(rows, 0, "x", body, sizeof(body)) > 0 && strstr(body, "\"p_rows\":[]}") != NULL, "json: an empty batch is still valid JSON");

    CHECK(Sync_KnownGameCount() == 11 && Sync_IsKnownGame("gemdive") && !Sync_IsKnownGame("gemdelve") && !Sync_IsKnownGame("../x") && !Sync_IsKnownGame(NULL),
          "known games: exactly ours");
    char noisy[] = "ok\x1b[31m\x07\nnext\xff";
    Sync_MakePrintable(noisy);
    CHECK(strcmp(noisy, "ok?[31m? next?") == 0, "printable: escape codes from the network are defanged");
}

static void test_board_csv(void) {
    const char *csv =
        "game,mode,username,score,played_on\n"
        "blockfall,marathon,ghost,9000,2026-09-18\n"
        "coilrush,classic,\"comma, name\",300,2026-09-19\r\n"
        "coilrush,classic,\"quote \"\"q\"\"\",200,2026-09-19\n"
        "coilrush,maze,pipe|name,150,2026-09-19\n"
        "../../etc,classic,evil,999,2026-09-19\n"
        "coilrush,BAD MODE,evil,999,2026-09-19\n"
        "coilrush,classic,evil,notanumber,2026-09-19\n"
        "coilrush,classic,evil,50,yesterday\n"
        "coilrush,classic,short\n"
        "coilrush,wrap,last,10,2026-09-17";
    SyncRow rows[16];
    int n = Sync_ParseBoardCsv(csv, rows, 16);
    CHECK(n == 5, "board: header skipped, 5 valid rows kept, 5 hostile/broken rows dropped");
    CHECK(strcmp(rows[0].game, "blockfall") == 0 && rows[0].score == 9000, "board: first row");
    CHECK(strcmp(rows[1].username, "comma, name") == 0, "board: quoted comma handled, CRLF tolerated");
    CHECK(strcmp(rows[2].username, "quote \"q\"") == 0, "board: doubled quotes unescaped");
    CHECK(strcmp(rows[3].username, "pipe/name") == 0, "board: separator from the server neutralised");
    CHECK(strcmp(rows[4].username, "last") == 0 && rows[4].score == 10, "board: final row without newline parsed");

    const char *hostile =
        "game,mode,username,score,played_on\n"
        "zzz-invented-game,classic,evil,999,2026-09-19\n"
        "coilrush,classic,evil,999999999999,2026-09-19\n"
        "coilrush,classic,\"\x1b[2J\x07 wipe\",40,2026-09-19\n";
    n = Sync_ParseBoardCsv(hostile, rows, 16);
    CHECK(n == 1, "board: invented games and impossible scores from the server are dropped");
    bool clean = true;
    for (const char *p = rows[0].username; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) clean = false;
    CHECK(clean, "board: a downloaded name keeps no control or escape bytes");

    CHECK(Sync_ParseBoardCsv("", rows, 16) == 0, "board: empty response");
    CHECK(Sync_ParseBoardCsv("game,mode,username,score,played_on\n", rows, 16) == 0, "board: header only");
    CHECK(Sync_ParseBoardCsv(csv, rows, 2) == 2, "board: maxRows respected");
}

static void test_cache_and_local_files(void) {
    char buf[2048];
    SyncRow rows[8];
    const char *csv =
        "game,mode,username,score,played_on\n"
        "blockfall,marathon,ghost,9000,2026-09-18\n"
        "coilrush,classic,anna,300,2026-09-19\n"
        "coilrush,maze,ghost,150,2026-09-19\n";
    int n = Sync_ParseBoardCsv(csv, rows, 8);
    CHECK(Sync_WriteGlobalCache(rows, n) == 2, "cache: one file per game");

    CHECK(ReadFile("data/ghost-launcher/scores/global/coilrush.txt", buf, sizeof(buf)), "cache: coilrush file exists");
    CHECK(strcmp(buf, "classic|anna|300|2026-09-19\nmaze|ghost|150|2026-09-19\n") == 0,
          "cache: same row format the games already read");
    CHECK(ReadFile("data/ghost-launcher/scores/global/blockfall.txt", buf, sizeof(buf)) &&
          strcmp(buf, "marathon|ghost|9000|2026-09-18\n") == 0, "cache: games don't bleed into each other");

    /* A second, smaller sync replaces the file rather than appending. */
    n = Sync_ParseBoardCsv("h\ncoilrush,wrap,solo,5,2026-09-19\n", rows, 8);
    Sync_WriteGlobalCache(rows, n);
    CHECK(ReadFile("data/ghost-launcher/scores/global/coilrush.txt", buf, sizeof(buf)) &&
          strcmp(buf, "wrap|solo|5|2026-09-19\n") == 0, "cache: rewritten whole, no stale rows");

    /* The cache round-trips through the local-file loader. */
    char path[800];
    snprintf(path, sizeof(path), "%s/data/ghost-launcher/scores/global/coilrush.txt", gTmpRoot);
    CHECK(Sync_LoadLocalFile(path, "coilrush", rows, 8) == 1 && rows[0].score == 5, "local: loader reads the same format");
    CHECK(Sync_LoadLocalFile("/nonexistent/file.txt", "coilrush", rows, 8) == 0, "local: missing file is just empty");

    WriteFile("data/ghost-launcher/scores/mixed.txt",
              "# Ghost Arcade scores for mixed: mode|username|score|date\n"
              "classic|a|30|2026-09-19\n"
              "garbage\n"
              "classic|b|0|2026-09-19\n"
              "wrap|c|20|2026-09-19\n");
    snprintf(path, sizeof(path), "%s/data/ghost-launcher/scores/mixed.txt", gTmpRoot);
    CHECK(Sync_LoadLocalFile(path, "mixed", rows, 8) == 2, "local: only uploadable rows loaded");
    CHECK(Sync_LoadLocalFile(path, "mixed", rows, 1) == 1, "local: maxRows respected");
}

static void test_install_id(void) {
    char a[SYNC_UUID_LEN], b[SYNC_UUID_LEN], small[8];
    CHECK(Sync_InstallId(a, sizeof(a)), "id: created on first use");
    CHECK(strlen(a) == 36 && a[8] == '-' && a[13] == '-' && a[18] == '-' && a[23] == '-', "id: uuid shape");
    CHECK(a[14] == '4', "id: version 4");
    CHECK(a[19] == '8' || a[19] == '9' || a[19] == 'a' || a[19] == 'b', "id: RFC 4122 variant");
    CHECK(Sync_InstallId(b, sizeof(b)) && strcmp(a, b) == 0, "id: stable across calls");
    CHECK(!Sync_InstallId(small, sizeof(small)), "id: refuses a buffer that can't hold it");

    WriteFile("data/ghost-launcher/install_id", "not-a-uuid\n");
    CHECK(Sync_InstallId(b, sizeof(b)) && strlen(b) == 36 && strcmp(b, "not-a-uuid") != 0, "id: corrupt file replaced");
    char again[SYNC_UUID_LEN];
    CHECK(Sync_InstallId(again, sizeof(again)) && strcmp(again, b) == 0, "id: replacement persisted");
}

static void remove_online_conf_for_test(void) {
    char p[900];
    snprintf(p, sizeof(p), "%s/config/ghost-launcher/online.conf", gTmpRoot);
    remove(p);
}

static void test_config(void) {
    SyncConfig cfg;
    SyncConfig_Load(&cfg);
    CHECK(!cfg.enabled && !cfg.decided, "config: online sharing is off until the player opts in");
    CHECK(strcmp(cfg.url, ONLINE_DEFAULT_URL) == 0 && strcmp(cfg.key, ONLINE_DEFAULT_KEY) == 0, "config: built-in defaults");
    CHECK(strncmp(cfg.url, "https://", 8) == 0, "config: default endpoint is https");
    CHECK(strstr(cfg.key, "secret") == NULL && strstr(cfg.key, "service_role") == NULL,
          "config: shipped key is not a secret key");

    WriteFile("config/ghost-launcher/online.conf",
              "# my settings\n"
              "  url = https://example.test/  \n"
              "key=abc123\n"
              "nonsense line\n");
    SyncConfig_Load(&cfg);
    CHECK(strcmp(cfg.url, "https://example.test") == 0, "config: url override, trimmed, trailing slash dropped");
    CHECK(strcmp(cfg.key, "abc123") == 0 && !cfg.enabled && !cfg.decided, "config: key override; without an enabled= line it is still off and undecided");

    WriteFile("config/ghost-launcher/online.conf", "enabled=0\nurl=\n");
    SyncConfig_Load(&cfg);
    CHECK(!cfg.enabled && cfg.decided, "config: an explicit 0 is a decision");
    CHECK(strcmp(cfg.url, ONLINE_DEFAULT_URL) == 0, "config: empty value keeps the default");

    WriteFile("config/ghost-launcher/online.conf", "enabled=1\nurl=http://example.test\n");
    SyncConfig_Load(&cfg);
    CHECK(!cfg.enabled, "config: a url that is not https switches syncing off");

    /* Saving the choice keeps the other lines. */
    WriteFile("config/ghost-launcher/online.conf", "# note\nurl=https://example.test\nenabled=0\nkey=k1\n");
    CHECK(SyncConfig_SaveEnabled(true), "config: choice saves");
    SyncConfig_Load(&cfg);
    CHECK(cfg.enabled && cfg.decided && strcmp(cfg.url, "https://example.test") == 0 && strcmp(cfg.key, "k1") == 0, "config: opting in keeps url and key");
    SyncConfig_SaveEnabled(false);
    SyncConfig_Load(&cfg);
    CHECK(!cfg.enabled && cfg.decided && strcmp(cfg.key, "k1") == 0, "config: opting out again");
    int enabledLines = 0;
    { char p[900]; snprintf(p, sizeof(p), "%s/config/ghost-launcher/online.conf", gTmpRoot); FILE *ff = fopen(p, "r"); char ln[200];
      while (ff && fgets(ln, sizeof(ln), ff)) if (strncmp(ln, "enabled", 7) == 0) enabledLines++; if (ff) fclose(ff); }
    CHECK(enabledLines == 1, "config: never more than one enabled= line");
    remove_online_conf_for_test();
}

static void test_scores_reader(void) {
    char slug[SCORES_SLUG_LEN];
    CHECK(Scores_SlugFromExec("~/.local/bin/coilrush", slug, sizeof(slug)) && strcmp(slug, "coilrush") == 0,
          "slug: basename of the exec path");
    CHECK(Scores_SlugFromExec("blockfall", slug, sizeof(slug)) && strcmp(slug, "blockfall") == 0, "slug: bare name");
    CHECK(!Scores_SlugFromExec("/opt/Games/My Game.sh", slug, sizeof(slug)), "slug: unsafe basename rejected");
    CHECK(!Scores_SlugFromExec("/usr/bin/", slug, sizeof(slug)), "slug: trailing slash has no basename");
    char tiny[4];
    CHECK(!Scores_SlugFromExec("/bin/coilrush", tiny, sizeof(tiny)), "slug: refuses a buffer that is too small");

    WriteFile("data/ghost-launcher/scores/reader.txt",
              "# Ghost Arcade scores for reader: mode|username|score|date\n"
              "classic|Ghost|210|2026-09-19\n"
              "maze|Ghost|950|2026-09-19\n"
              "broken line\n"
              "wrap|Ghost|0|2026-09-19\n"
              "wrap|Anna|400|2026-09-18\n");
    ScoreBest b = Scores_LoadBest("reader", false);
    CHECK(b.found && b.score == 950 && strcmp(b.mode, "maze") == 0 && strcmp(b.username, "Ghost") == 0,
          "best: highest row across all modes, junk ignored");
    CHECK(!Scores_LoadBest("reader", true).found, "best: local file is not the global board");
    CHECK(!Scores_LoadBest("no-such-game", false).found, "best: missing file -> not found");

    WriteFile("data/ghost-launcher/scores/global/reader.txt", "classic|worldbest|12840|2026-09-01\n");
    b = Scores_LoadBest("reader", true);
    CHECK(b.found && b.score == 12840 && strcmp(b.username, "worldbest") == 0, "best: reads the global cache");
}

static void test_profiles(void) {
    Profiles p;
    CHECK(Profiles_Load(&p) && p.count == 0, "profiles: start empty");
    CHECK(Profiles_GetUsername(&p, "Coilrush")[0] == '\0', "profiles: nothing set -> empty, never NULL");

    Profiles_SetUsername(&p, PROFILE_DEFAULT_KEY, "Ghost");
    CHECK(strcmp(Profiles_GetUsername(&p, "Coilrush"), "Ghost") == 0, "profiles: arcade name covers a game with no entry");
    CHECK(strcmp(Profiles_GetUsername(&p, "A Game Added Next Year"), "Ghost") == 0, "profiles: ...including future games");
    CHECK(Profiles_GetOwnUsername(&p, "Coilrush")[0] == '\0', "profiles: GetOwn has no fallback");

    Profiles_SetUsername(&p, "Blockfall", "The Real Dev Ghost");
    CHECK(strcmp(Profiles_GetUsername(&p, "Blockfall"), "The Real Dev Ghost") == 0, "profiles: per-game override wins");
    CHECK(strcmp(Profiles_GetUsername(&p, "Coilrush"), "Ghost") == 0, "profiles: override doesn't leak to other games");

    Profiles_SetUsername(&p, "Blockfall", "");
    CHECK(strcmp(Profiles_GetUsername(&p, "Blockfall"), "Ghost") == 0, "profiles: blank override falls back to the arcade name");

    CHECK(Profiles_Save(&p), "profiles: saved");
    Profiles q;
    CHECK(Profiles_Load(&q), "profiles: reloaded");
    CHECK(strcmp(Profiles_GetOwnUsername(&q, PROFILE_DEFAULT_KEY), "Ghost") == 0, "profiles: arcade name round-trips");
    CHECK(strcmp(Profiles_GetUsername(&q, "Coilrush"), "Ghost") == 0, "profiles: fallback works after reload");
}

static void test_manifest_install_field(void) {
    WriteFile("config/ghost-launcher/games.txt",
              "# old four-field row, then a five-field one\n"
              "Blockfall|~/.local/bin/blockfall|~/icon.png|A falling-block puzzle game\n"
              "Coilrush|~/.local/bin/coilrush|~/c.png|Snake|~/Work/coilrush\n"
              "NoExec||x|y|z\n");
    Manifest m;
    CHECK(Manifest_Load(&m, "/nonexistent-assets"), "manifest: loads");
    CHECK(m.count == 2, "manifest: rows without an exec are dropped");
    CHECK(m.games[0].install[0] == '\0', "manifest: four-field catalogs still load, install empty");
    CHECK(strcmp(m.games[0].desc, "A falling-block puzzle game") == 0, "manifest: fourth field intact");
    CHECK(strcmp(m.games[1].install, "~/Work/coilrush") == 0, "manifest: fifth field is the install folder");

    CHECK(Manifest_AddGame(&m, "Brickburst", "~/.local/bin/brickburst", "", "Bricks", "~/Work/brickburst"), "manifest: add with install");
    CHECK(Manifest_AddGame(&m, "Plain", "/bin/true", NULL, NULL, NULL), "manifest: NULL optional fields accepted");
    CHECK(Manifest_Save(&m), "manifest: saved");

    Manifest again;
    CHECK(Manifest_Load(&again, "/nonexistent-assets") && again.count == 4, "manifest: round-trips");
    CHECK(strcmp(again.games[2].install, "~/Work/brickburst") == 0, "manifest: install folder survives a save");
    CHECK(again.games[0].install[0] == '\0' && again.games[3].install[0] == '\0', "manifest: empty install stays empty");
    CHECK(strcmp(again.games[3].name, "Plain") == 0 && strcmp(again.games[3].exec, "/bin/true") == 0,
          "manifest: trailing empty fields don't shift the others");
}

/* Drives the real background installer against two throwaway Makefiles. */
static InstallStatus WaitForInstall(void) {
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    for (int i = 0; i < 100 && Launch_InstallStatus(NULL) == INSTALL_RUNNING; i++) {
        Launch_ReapFinished(0.0, &stats);
        usleep(50 * 1000);
    }
    CHECK(stats.count == 0, "install: never credited as playtime");
    return Launch_InstallStatus(NULL);
}

static void test_installer(void) {
    char err[128] = "", dir[800], marker[900];
    const char *name = NULL;

    CHECK(Launch_InstallStatus(&name) == INSTALL_IDLE, "install: idle to begin with");

    snprintf(dir, sizeof(dir), "%s/nowhere", gTmpRoot);
    CHECK(!Launch_InstallGame(dir, "Ghost Game", err, sizeof(err)), "install: refuses a folder with no Makefile");
    CHECK(strstr(err, "No Makefile") != NULL, "install: says why");
    CHECK(Launch_InstallStatus(NULL) == INSTALL_IDLE, "install: a refusal leaves the state idle");

    snprintf(dir, sizeof(dir), "%s/goodsrc", gTmpRoot);
    mkdir(dir, 0755);
    WriteFile("goodsrc/Makefile", "install:\n\t@touch installed.marker\n");
    CHECK(Launch_InstallGame(dir, "Good Game", err, sizeof(err)), "install: starts");
    CHECK(Launch_InstallStatus(&name) == INSTALL_RUNNING && strcmp(name, "Good Game") == 0, "install: running, tagged with the game");
    CHECK(!Launch_InstallGame(dir, "Other", err, sizeof(err)), "install: one at a time");
    CHECK(WaitForInstall() == INSTALL_DONE, "install: make exit 0 -> done");
    snprintf(marker, sizeof(marker), "%s/installed.marker", dir);
    CHECK(access(marker, F_OK) == 0, "install: `make install` really ran in the source folder");
    Launch_InstallAcknowledge();
    CHECK(Launch_InstallStatus(NULL) == INSTALL_IDLE, "install: acknowledged back to idle");

    snprintf(dir, sizeof(dir), "%s/badsrc", gTmpRoot);
    mkdir(dir, 0755);
    WriteFile("badsrc/Makefile", "install:\n\t@echo boom >&2; false\n");
    CHECK(Launch_InstallGame(dir, "Bad Game", err, sizeof(err)), "install: failing build still starts");
    CHECK(WaitForInstall() == INSTALL_FAILED, "install: make exit != 0 -> failed");
    char log[256] = "";
    CHECK(ReadFile("data/ghost-launcher/install.log", log, sizeof(log)) && strstr(log, "boom") != NULL,
          "install: output captured in install.log");
    Launch_InstallAcknowledge();
}

static void Cleanup(void) {
    const char *files[] = {
        "data/ghost-launcher/scores/global/coilrush.txt", "data/ghost-launcher/scores/global/blockfall.txt",
        "data/ghost-launcher/scores/mixed.txt", "data/ghost-launcher/install_id",
        "data/ghost-launcher/scores/reader.txt", "data/ghost-launcher/scores/global/reader.txt",
        "data/ghost-launcher/profiles.txt", "data/ghost-launcher/install.log",
        "config/ghost-launcher/games.txt",
        "goodsrc/Makefile", "goodsrc/installed.marker", "badsrc/Makefile",
        "config/ghost-launcher/online.conf",
    };
    const char *dirs[] = {
        "data/ghost-launcher/scores/global", "data/ghost-launcher/scores", "data/ghost-launcher", "data",
        "config/ghost-launcher", "config", "goodsrc", "badsrc", "",
    };
    char path[800];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", gTmpRoot, files[i]);
        unlink(path);
    }
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", gTmpRoot, dirs[i]);
        rmdir(path);
    }
}


static void test_daily(void) {
    CHECK(Daily_SeedForDate(2026, 9, 20) == Daily_SeedForDate(2026, 9, 20), "daily: seed is stable for a date");
    CHECK(Daily_SeedForDate(2026, 9, 20) != Daily_SeedForDate(2026, 9, 21), "daily: seed differs by day");
    CHECK(Daily_SeedForDate(2026, 9, 20) != 0, "daily: seed is never zero");
    char mode[32];
    Daily_ModeForDate(2026, 9, 5, mode, sizeof(mode));
    CHECK(strcmp(mode, "daily-20260905") == 0, "daily: mode string zero-padded");
    bool valid = strlen(mode) <= 24;
    for (const char *p = mode; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) valid = false;
    CHECK(valid, "daily: mode string fits the score-table contract");
    char *off[] = {"game", NULL}, *on[] = {"game", "--daily", NULL};
    Daily_Init(1, off);
    CHECK(!Daily_Active(), "daily: off without the flag");
    Daily_Init(2, on);
    CHECK(Daily_Active(), "daily: on with --daily");
    CHECK(Daily_Seed() != 0 && strncmp(Daily_Mode(), "daily-", 6) == 0, "daily: seed and mode available when active");
}

static void test_achievements(void) {
    static const char *const kSlugs[11] = {"blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift", "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb"};
    /* The table: six per game, every id unique, thresholds sensible. */
    int total = 0;
    bool unique = true, sane = true;
    for (int g = 0; g < 11; g++) {
        int n = Ach_CountForGame(kSlugs[g]);
        CHECK(n == 6, "achievements: six for each game");
        total += n;
        long lastScore = 0;
        for (int i = 0; i < n; i++) {
            const AchDef *d = Ach_ForGame(kSlugs[g], i);
            if (!d || !d->title[0] || !d->desc[0] || d->threshold <= 0) sane = false;
            if (d && d->kind == ACH_SCORE) { if (d->threshold <= lastScore) sane = false; lastScore = d->threshold; }
        }
    }
    for (int a = 0; a < Ach_Count(); a++) for (int b = a + 1; b < Ach_Count(); b++) if (strcmp(Ach_At(a)->id, Ach_At(b)->id) == 0 && strcmp(Ach_At(a)->slug, Ach_At(b)->slug) == 0) unique = false;
    CHECK(total == Ach_Count() && unique && sane, "achievements: table is complete, ids unique per game, score tiers ascend");
    CHECK(Ach_CountForGame("nonesuch") == 0 && Ach_ForGame("nonesuch", 0) == NULL, "achievements: an unknown game has none");

    /* Nothing earned yet. */
    CHECK(Ach_UnlockedForGame("blockfall") == 0 && !Ach_IsUnlocked("blockfall", "r0", NULL), "achievements: none to begin with");

    /* A first run earns the first-run one, and only that. */
    int got = Ach_CheckRun("blockfall", 100, 1);
    CHECK(got == 1 && Ach_IsUnlocked("blockfall", "r0", NULL), "achievements: the first run earns the first-run achievement");
    char date[12] = "";
    CHECK(Ach_IsUnlocked("blockfall", "r0", date) && strlen(date) == 10 && date[4] == '-', "achievements: with a date");
    char toast[64];
    CHECK(Ach_PopToast(toast, sizeof(toast)) && strcmp(toast, "First Stack") == 0 && !Ach_PopToast(toast, sizeof(toast)), "achievements: announced once");

    /* A big run earns several at once, and never twice. */
    got = Ach_CheckRun("blockfall", 25000, 11);
    CHECK(got == 4, "achievements: two score tiers and two level tiers at once");
    CHECK(Ach_UnlockedForGame("blockfall") == 5, "achievements: five of six");
    CHECK(Ach_CheckRun("blockfall", 25000, 11) == 0, "achievements: nothing new the second time");
    int toasts = 0;
    while (Ach_PopToast(toast, sizeof(toast))) toasts++;
    CHECK(toasts == 4, "achievements: one toast per unlock");

    /* Runs count up to the regular one. */
    int n = 0;
    for (int i = 0; i < 30; i++) n += Ach_CheckRun("blockfall", 0, 1);
    CHECK(n == 1 && Ach_IsUnlocked("blockfall", "r5", NULL), "achievements: 25 runs earn Regular, whatever they scored");
    CHECK(Ach_UnlockedForGame("blockfall") == 6, "achievements: all six");

    /* Games are independent. */
    CHECK(Ach_UnlockedForGame("coilrush") == 0, "achievements: another game is untouched");
    CHECK(Ach_CheckRun("moondrop", 600, 2) == 2, "achievements: a first run that also scores well earns both");
    CHECK(Ach_CheckRun("moondrop", 0, 0) == 0, "achievements: a poor run earns nothing new");
    /* Junk in the file is ignored. */
    char p[800];
    snprintf(p, sizeof(p), "%s/data/ghost-launcher/achievements/lanehop.txt", gTmpRoot);
    FILE *f = fopen(p, "w");
    if (f) { fputs("garbage\n||\nr0|2026-01-01\n\xff\xfe\n", f); fclose(f); }
    CHECK(Ach_IsUnlocked("lanehop", "r0", NULL) && Ach_UnlockedForGame("lanehop") == 1, "achievements: a damaged file is read as far as it makes sense");
}

static void test_runstats(void) {
    struct tm tm = {0};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 8; tm.tm_mday = 20; tm.tm_hour = 15; tm.tm_isdst = -1;
    time_t now = mktime(&tm);
    CHECK(RunStats_DaysAgo("2026-09-20", now) == 0 && RunStats_DaysAgo("2026-09-19", now) == 1 && RunStats_DaysAgo("2026-09-06", now) == 14, "runstats: days ago");
    CHECK(RunStats_DaysAgo("garbage", now) == -1 && RunStats_DaysAgo(NULL, now) == -1 && RunStats_DaysAgo("1999-01-01", now) == -1, "runstats: bad dates are rejected");
    CHECK(RunStats_DaysAgo("2026-09-21", now) == -1 + 0 || RunStats_DaysAgo("2026-09-21", now) < 0, "runstats: the future is negative");

    char path[900];
    snprintf(path, sizeof(path), "%s/data/rstest", gTmpRoot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/data/rstest/runs.log", gTmpRoot);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("2026-09-20 10:00:00|classic|500|2\n2026-09-20 11:00:00|classic|0|1\n2026-09-19 09:00:00|wrap|120|1\n"
              "2026-08-01 09:00:00|wrap|50|1\njunk line\n\n2026-09-18|x|5|1\n", f);
        fclose(f);
    }
    RunSummary s;
    RunStats_Load("rstest", now, &s);
    CHECK(s.runs == 4 && s.zeroRuns == 1, "runstats: counts runs and zero-score runs, skipping junk");
    CHECK(strcmp(s.last, "2026-09-20") == 0 && s.perDay[0] == 2 && s.perDay[1] == 1 && s.perDay[2] == 0, "runstats: last date and per-day counts");
    RunStats_Load("no-such-game", now, &s);
    CHECK(s.runs == 0 && s.last[0] == '\0', "runstats: a missing log is an empty summary");
    RunStats_Load("../etc", now, &s);
    CHECK(s.runs == 0, "runstats: a bad slug is refused");
}

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define SHA_C "cccccccccccccccccccccccccccccccccccccccc"
#define SHA_T "1111111111111111111111111111111111111111"
#define SHA_P "2222222222222222222222222222222222222222"

/* One commit the way GitHub sends it: the commit hash, then a tree hash and a parent hash that must NOT be counted. */
#define COMMIT_JSON(sha, msg) "{\"sha\":\"" sha "\",\"node_id\":\"C_x\",\"commit\":{\"author\":{\"name\":\"n\",\"date\":\"2026-09-20T00:00:00Z\"}," \
    "\"message\":\"" msg "\",\"tree\":{\"sha\":\"" SHA_T "\",\"url\":\"u\"}},\"parents\":[{\"sha\":\"" SHA_P "\",\"url\":\"u\"}]}"

static void test_updatecheck(void) {
    CHECK(Update_IsSha(SHA_A) && !Update_IsSha("abc") && !Update_IsSha(NULL) && !Update_IsSha("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
          && !Update_IsSha("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"), "update: a sha is exactly 40 lowercase hex");
    CHECK(Update_IsRepo("TheRealGhost007/ghost-arcade") && Update_IsRepo("a/b"), "update: owner/name accepted");
    CHECK(!Update_IsRepo("noslash") && !Update_IsRepo("a/b/c") && !Update_IsRepo("/b") && !Update_IsRepo("a/") && !Update_IsRepo("a/../b")
          && !Update_IsRepo("a/b?x=1") && !Update_IsRepo("a b/c") && !Update_IsRepo("evil.com/x#") && !Update_IsRepo(NULL), "update: anything that could bend the URL is refused");

    UpdateState u;
    const char *three = "[" COMMIT_JSON(SHA_C, "Newest thing\\n\\nbody text") "," COMMIT_JSON(SHA_B, "Middle") "," COMMIT_JSON(SHA_A, "Oldest") "]";
    CHECK(Update_ParseCommits(three, SHA_C, &u) && u.status == UPDATE_CURRENT && u.behind == 0 && strcmp(u.latest, SHA_C) == 0, "update: built from the newest commit = current");
    CHECK(strcmp(u.title, "Newest thing") == 0, "update: title is the first line of the newest message");
    CHECK(Update_ParseCommits(three, SHA_A, &u) && u.status == UPDATE_BEHIND && u.behind == 2, "update: two commits behind (tree and parent hashes are not counted)");
    CHECK(Update_ParseCommits(three, SHA_B, &u) && u.behind == 1, "update: one behind");
    CHECK(Update_ParseCommits(three, SHA_T, &u) && u.status == UPDATE_UNKNOWN, "update: a tree hash is not a commit");
    CHECK(Update_ParseCommits(three, "0123456789abcdef0123456789abcdef01234567", &u) && u.status == UPDATE_UNKNOWN && strcmp(u.latest, SHA_C) == 0, "update: a build GitHub has never seen = unknown, not 'update available'");
    CHECK(Update_ParseCommits(three, "", &u) && u.status == UPDATE_UNKNOWN, "update: a build with no commit = unknown");

    /* Pretty-printed JSON parses the same. */
    const char *pretty = "[\n  {\n    \"sha\": \"" SHA_B "\",\n    \"node_id\": \"x\",\n    \"commit\": { \"message\": \"Pretty\" }\n  },\n  {\n    \"sha\" : \"" SHA_A "\" ,\n    \"node_id\" : \"y\"\n  }\n]";
    CHECK(Update_ParseCommits(pretty, SHA_A, &u) && u.behind == 1 && strcmp(u.title, "Pretty") == 0, "update: whitespace between tokens is fine");

    /* A commit message that tries to look like structure, and to smuggle escape codes. */
    const char *hostile = "[" COMMIT_JSON(SHA_B, "evil \\\"sha\\\":\\\"" SHA_A "\\\",\\\"node_id\\\": \\u001b[2J\\u0007 \\u00e9 end") "," COMMIT_JSON(SHA_A, "real") "]";
    CHECK(Update_ParseCommits(hostile, SHA_A, &u) && u.behind == 1, "update: a fake commit inside a message string is not counted");
    bool clean = u.title[0] != '\0';
    for (const char *p = u.title; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) clean = false;
    CHECK(clean && strlen(u.title) < UPDATE_TITLE_LEN, "update: the title keeps no control, escape or non-ASCII bytes");

    CHECK(!Update_ParseCommits("[]", SHA_A, &u) && !Update_ParseCommits("", SHA_A, &u) && !Update_ParseCommits(NULL, SHA_A, &u), "update: empty replies are failures");
    CHECK(!Update_ParseCommits("{\"message\":\"API rate limit exceeded\"}", SHA_A, &u), "update: an error object is a failure");
    Update_ParseCommits("[{\"sha\":\"" SHA_B "\",\"node_id\":\"x\",\"commit\":{\"message\":\"cut off mid str", SHA_A, &u);
    CHECK(strcmp(u.latest, SHA_B) == 0 && u.status == UPDATE_UNKNOWN, "update: a reply cut off mid-string does not crash or invent a result");
    char big[5000];
    memset(big, '"', sizeof(big) - 1); big[sizeof(big) - 1] = 0;
    CHECK(!Update_ParseCommits(big, SHA_A, &u), "update: a wall of quotes is survived");

    /* State file. */
    UpdateState s = {0}, back;
    Update_Load(&back);
    CHECK(back.status == UPDATE_UNKNOWN && back.checkedAt == 0, "update: no file yet = unknown");
    s.status = UPDATE_BEHIND; s.behind = 3; s.checkedAt = 1789000000;
    snprintf(s.current, sizeof(s.current), "%s", SHA_A); snprintf(s.latest, sizeof(s.latest), "%s", SHA_C); snprintf(s.title, sizeof(s.title), "Hello = world");
    CHECK(Update_Save(&s), "update: state saves");
    Update_Load(&back);
    CHECK(back.status == UPDATE_BEHIND && back.behind == 3 && strcmp(back.current, SHA_A) == 0 && strcmp(back.latest, SHA_C) == 0
          && strcmp(back.title, "Hello = world") == 0 && back.checkedAt == 1789000000, "update: state round-trips, '=' in a title included");
    WriteFile("data/ghost-launcher/update.txt", "status=behind\nbehind=-5\nlatest=not-a-sha\ntitle=\x1b[31mred\nchecked=-9\n");
    Update_Load(&back);
    CHECK(back.status == UPDATE_UNKNOWN && back.behind == 0 && back.latest[0] == '\0' && back.checkedAt == 0 && strchr(back.title, 27) == NULL, "update: a tampered state file is not believed");

    SyncConfig cfg;
    remove_online_conf_for_test();
    SyncConfig_Load(&cfg);
    CHECK(cfg.checkUpdates && strcmp(cfg.updateRepo, UPDATE_DEFAULT_REPO) == 0, "update: on by default, official repository");
    WriteFile("config/ghost-launcher/online.conf", "check_updates=0\nupdate_repo=evil.example/x/../y\n");
    SyncConfig_Load(&cfg);
    CHECK(!cfg.checkUpdates && strcmp(cfg.updateRepo, UPDATE_DEFAULT_REPO) == 0, "update: can be switched off; a malformed repo is ignored");
    remove_online_conf_for_test();
    { char p[900]; snprintf(p, sizeof(p), "%s/data/ghost-launcher/update.txt", gTmpRoot); remove(p); }
}

static void test_adminlock(void) {
    char hex[65];
    Sha256_Hex("abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256: the standard abc vector");
    Sha256_Hex("", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha256: the empty string");
    Sha256_Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0, "sha256: a message that needs a second padding block");
    char big[200];
    memset(big, 'a', sizeof(big));
    Sha256_Hex(big, 119, hex);
    char hex2[65];
    Sha256_Hex(big, 120, hex2);
    CHECK(strlen(hex) == 64 && strcmp(hex, hex2) != 0, "sha256: lengths either side of a block boundary differ");

    char a[65], b[65];
    AdminLock_Fingerprint("00000000000000000000000000000001", a);
    AdminLock_Fingerprint("00000000000000000000000000000002", b);
    CHECK(strlen(a) == 64 && strcmp(a, b) != 0, "adminlock: different machines, different fingerprints");
    CHECK(strstr(a, "00000000000000000000000000000001") == NULL, "adminlock: the fingerprint does not contain the machine id");
    /* printf 'ghost-launcher-admin-v1:00000000000000000000000000000001' | sha256sum */
    CHECK(strcmp(a, "80b5edb9395213b5735c8f7516de9cc9011eac4b9b9e3e13ffa7de5764aca311") == 0, "adminlock: matches what `make admin-lock` computes with sha256sum");
    /* The test binary is built without a fingerprint, like everyone else's build. */
    CHECK(!AdminLock_Configured() && !AdminLock_IsUnlocked(), "adminlock: a build with no fingerprint has no admin mode");
}

static void test_safefile(void) {
    char path[900], tmp[920], buf[64];
    snprintf(path, sizeof(path), "%s/safe.txt", gTmpRoot);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = SafeFile_Open(path);
    CHECK(f != NULL, "safefile: opens");
    if (f) fputs("first\n", f);
    CHECK(SafeFile_Close(f), "safefile: commits");
    f = fopen(path, "r");
    CHECK(f && fgets(buf, sizeof(buf), f) && strcmp(buf, "first\n") == 0, "safefile: content landed");
    if (f) fclose(f);
    CHECK(access(tmp, F_OK) != 0, "safefile: no temp file left behind");

    /* While a write is in flight the old file is untouched (a crash here loses only the new data). */
    f = SafeFile_Open(path);
    if (f) { fputs("second, unfinished", f); fflush(f); }
    char old[64] = "";
    FILE *g = fopen(path, "r");
    if (g) { if (!fgets(old, sizeof(old), g)) old[0] = 0; fclose(g); }
    CHECK(strcmp(old, "first\n") == 0, "safefile: the old file survives until commit");
    SafeFile_Close(f);
    g = fopen(path, "r");
    CHECK(g && fgets(buf, sizeof(buf), g) && strcmp(buf, "second, unfinished") == 0, "safefile: then the new one replaces it");
    if (g) fclose(g);

    char bad[900];
    snprintf(bad, sizeof(bad), "%s/no/such/dir/x.txt", gTmpRoot);
    CHECK(SafeFile_Open(bad) == NULL, "safefile: an unwritable path fails cleanly");
    CHECK(!SafeFile_Close(NULL), "safefile: closing NULL is a failure, not a crash");
    remove(path);
}

static void test_howto(void) {
    static const char *const kSlugs[11] = {"blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift", "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb"};
    bool all = true, sane = true;
    for (int i = 0; i < 11; i++) {
        const HowTo *h = HowTo_ForSlug(kSlugs[i]);
        if (!h) { all = false; continue; }
        int controls = 0;
        while (controls < HOWTO_MAX_CONTROLS && h->controls[controls][0]) {
            if (!h->controls[controls][1] || !h->controls[controls][1][0]) sane = false;
            controls++;
        }
        if (controls < 3 || controls >= HOWTO_MAX_CONTROLS || strlen(h->goal) < 20 || strlen(h->tip) < 10 || strlen(h->goal) > 240 || strlen(h->tip) > 200) sane = false;
    }
    CHECK(all && HowTo_Count() == 11, "howto: every game has a card");
    CHECK(sane, "howto: each card has a goal, a tip and a sensible number of controls");
    CHECK(HowTo_ForSlug("nope") == NULL && HowTo_ForSlug(NULL) == NULL, "howto: unknown slug is NULL");
    CHECK(!HowTo_Seen("lanehop"), "howto: not seen at first");
    HowTo_MarkSeen("lanehop");
    CHECK(HowTo_Seen("lanehop") && !HowTo_Seen("skyraid"), "howto: remembered per game");
    CHECK(HowTo_Seen("../evil") , "howto: a bad slug is treated as seen, never written");
}

static void test_tuning_and_prefs(void) {
    Tune_Init("testgame");
    CHECK(Tune_Count() == 1 && Tune_Speed() == 1.0f && !Tune_Modified(), "tuning: starts with just the speed knob, at default");
    float *g = Tune_Add("gravity", "Gravity", 22.0f, 10.0f, 40.0f, 1.0f);
    CHECK(g && *g == 22.0f && Tune_Count() == 2, "tuning: registering returns a live pointer at the default");
    CHECK(Tune_Add("gravity", "Gravity", 22.0f, 10.0f, 40.0f, 1.0f) == g && Tune_Count() == 2, "tuning: registering twice is the same knob");
    *g = 30.0f;
    CHECK(Tune_Modified() && Tune_Get("gravity", 0.0f) == 30.0f, "tuning: an edit shows as modified");
    CHECK(Tune_Save(), "tuning: saves");
    Tune_Init("testgame");
    float *g2 = Tune_Add("gravity", "Gravity", 22.0f, 10.0f, 40.0f, 1.0f);
    CHECK(*g2 == 30.0f && Tune_Modified(), "tuning: a saved value comes back next launch");
    float *lim = Tune_Add("lim", "Limit", 5.0f, 1.0f, 9.0f, 1.0f);
    CHECK(*lim == 5.0f, "tuning: unsaved knobs stay at default");
    Tune_ResetAll();
    CHECK(!Tune_Modified() && *g2 == 22.0f, "tuning: reset puts everything back");
    Tune_Save(); /* nothing differs: the file goes away */
    Tune_Init("testgame");
    CHECK(*Tune_Add("gravity", "Gravity", 22.0f, 10.0f, 40.0f, 1.0f) == 22.0f, "tuning: no leftover file after a reset");

    Prefs_Load();
    CHECK(!Prefs_Get()->fullscreen, "prefs: fullscreen off by default");
    CHECK(Prefs_HandleRow(3, false, false, true) && Prefs_Get()->fullscreen, "prefs: the fullscreen row toggles it");
    Prefs_Load();
    CHECK(Prefs_Get()->fullscreen, "prefs: fullscreen survives a reload");
    Prefs_HandleRow(3, false, false, true);
    CHECK(PREFS_ROWS == 4, "prefs: four shared settings rows");
}

int main(void) {
    const char *tmpBase = getenv("TMPDIR");
    snprintf(gTmpRoot, sizeof(gTmpRoot), "%s/ghost-launcher-test-XXXXXX", (tmpBase && tmpBase[0]) ? tmpBase : "/tmp");
    if (mkdtemp(gTmpRoot) == NULL) {
        printf("FAIL: could not create temp dir\n");
        return 1;
    }
    char path[800];
    snprintf(path, sizeof(path), "%s/data", gTmpRoot);
    setenv("XDG_DATA_HOME", path, 1);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/config", gTmpRoot);
    setenv("XDG_CONFIG_HOME", path, 1);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/config/ghost-launcher", gTmpRoot);
    mkdir(path, 0755);

    test_validators();
    test_tuning_and_prefs();
    test_adminlock();
    test_updatecheck();
    test_safefile();
    test_howto();
    test_runstats();
    test_parse_local_line();
    test_upload_json();
    test_board_csv();
    test_cache_and_local_files(); /* also creates data/ghost-launcher/scores for later fixtures */
    test_install_id();
    test_config();
    test_scores_reader();
    test_profiles();
    test_manifest_install_field();
    test_installer();
    test_daily();
    test_achievements();

    Cleanup();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
