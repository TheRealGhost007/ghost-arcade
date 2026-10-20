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
#include <dirent.h>
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
#include "../src/input.h"
#include "../src/keymap.h"
#include "../src/launcherini.h"
#include "../src/prefswatch.h"
#include "../src/settings.h"

#include "check.h"

int gChecks = 0;
int gFailures = 0;

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

/* The board download asks for regular boards and two weeks of dailies,
 * #1s first, so games late in the alphabet keep their boards however many
 * old dailies the server has piled up. */
static void test_board_download(void) {
    struct tm noon = {0};
    noon.tm_year = 2026 - 1900; noon.tm_mon = 9; noon.tm_mday = 14; noon.tm_hour = 12; noon.tm_isdst = -1;
    time_t now = mktime(&noon);
    char cutoff[24];
    Sync_DailyCutoff(now, cutoff, sizeof(cutoff));
    CHECK(strcmp(cutoff, "daily-20260930") == 0, "download: the daily cut-off is 14 days back, as the games keep them");
    CHECK(Sync_KeepBoard("classic", cutoff) && Sync_KeepBoard("power-14x20", cutoff) && Sync_KeepBoard("daily-20261014", cutoff) &&
              Sync_KeepBoard("daily-20260930", cutoff) && !Sync_KeepBoard("daily-20260929", cutoff) && !Sync_KeepBoard("daily-20250101", cutoff),
          "download: regular boards stay, dailies from the cut-off on stay, older ones go");
    CHECK(Sync_KeepBoard("daily-x", cutoff) && !Sync_KeepBoard(NULL, cutoff), "download: only a real daily-YYYYMMDD is ever dropped");

    char q[1024];
    size_t n = Sync_BoardQuery(now, q, sizeof(q));
    CHECK(n == strlen(q) && n > 0, "download: the query fits");
    CHECK(strstr(q, "game=in.(blockfall,coilrush,brickburst,skyraid,ghostmaze,rockdrift,lanehop,crawlshot,moondrop,gemdive,girderclimb)") != NULL,
          "download: every known game, and only those");
    CHECK(strstr(q, "&or=(mode.not.like.daily-*,mode.gte.daily-20260930)") != NULL, "download: regular boards, and dailies no older than the cut-off");
    CHECK(strstr(q, "&order=rank.asc,game.asc,mode.asc") != NULL && strstr(q, "&limit=1000") != NULL,
          "download: #1s first, so a cut answer trims every board a little instead of dropping the last games");
    CHECK(Sync_BoardQuery(now, q, 40) == 0, "download: a query that does not fit is not sent");

    /* What comes back, #1s first; an old daily the server sent anyway. */
    const char *csv =
        "game,mode,username,score,played_on\n"
        "blockfall,classic,ghost,900,2026-10-01\n"
        "girderclimb,classic,anna,50,2026-10-02\n"
        "blockfall,daily-20261013,ghost,70,2026-10-13\n"
        "blockfall,daily-20250101,old,99,2025-01-01\n"
        "blockfall,classic,anna,800,2026-10-01\n"
        "girderclimb,classic,ghost,40,2026-10-02\n"
        "blockfall,classic,trg,700,2026-10-01\n";
    SyncRow rows[16];
    int count = Sync_ParseBoardCsv(csv, rows, 16);
    count = Sync_DropOldDailies(rows, count, cutoff);
    CHECK(count == 6, "download: an old daily is dropped even if the server sends it");
    Sync_GroupBoards(rows, count);
    const char *want[6][2] = {{"blockfall", "ghost"}, {"blockfall", "anna"}, {"blockfall", "trg"}, {"blockfall", "ghost"}, {"girderclimb", "anna"}, {"girderclimb", "ghost"}};
    bool grouped = true;
    for (int i = 0; i < 6; i++) grouped &= strcmp(rows[i].game, want[i][0]) == 0 && strcmp(rows[i].username, want[i][1]) == 0;
    CHECK(grouped && strcmp(rows[3].mode, "daily-20261013") == 0, "download: grouped by game and board, each board in its rank order");
    CHECK(Sync_WriteGlobalCache(rows, count) == 2, "download: one file per game after grouping");
    char buf[512];
    CHECK(ReadFile("data/ghost-launcher/scores/global/blockfall.txt", buf, sizeof(buf)) &&
              strcmp(buf, "classic|ghost|900|2026-10-01\nclassic|anna|800|2026-10-01\nclassic|trg|700|2026-10-01\ndaily-20261013|ghost|70|2026-10-13\n") == 0,
          "download: the file reads as before, board by board, best first");
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
      while (ff && fgets(ln, sizeof(ln), ff)) { if (strncmp(ln, "enabled", 7) == 0) enabledLines++; }
      if (ff) fclose(ff); }
    CHECK(enabledLines == 1, "config: never more than one enabled= line");

    /* A hand-edited file with a few comments is longer than eight lines;
     * toggling the choice in the launcher must not drop the lines past
     * that, which is where a custom url/key would typically sit. */
    WriteFile("config/ghost-launcher/online.conf",
              "# Ghost Arcade online settings\n# line 2\n# line 3\n# line 4\n# line 5\n# line 6\n# line 7\n# line 8\n"
              "check_updates=0\nupdate_repo=someone/fork\nurl=https://custom.example\nkey=k-custom\nenabled=0\n");
    CHECK(SyncConfig_SaveEnabled(true), "config: choice saves on a long file");
    SyncConfig_Load(&cfg);
    CHECK(cfg.enabled && strcmp(cfg.url, "https://custom.example") == 0 && strcmp(cfg.key, "k-custom") == 0
          && !cfg.checkUpdates && strcmp(cfg.updateRepo, "someone/fork") == 0,
          "config: every setting past the eighth line survives toggling the choice");
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

/* A save that does not reach the disk says so: the launcher shows
 * "Couldn't save" instead of pretending. Both ways it can fail: the folder
 * refuses the new file, or the last step (the rename over the old file)
 * fails, which is where a full disk shows up too. */
static void test_saves_report_failure(void) {
    char cfg[800], data[800], blocker[900], inside[1000];
    snprintf(cfg, sizeof(cfg), "%s/config/ghost-launcher", gTmpRoot);
    snprintf(data, sizeof(data), "%s/data/ghost-launcher", gTmpRoot);
    mkdir(data, 0755);
    Manifest m;
    Manifest_Load(&m, "/nonexistent-assets");
    Profiles p;
    Profiles_Load(&p);
    if (geteuid() != 0) { /* root writes anywhere */
        chmod(cfg, 0555);
        chmod(data, 0555);
        CHECK(!Manifest_Save(&m), "saves: a catalog folder that cannot be written fails the save");
        CHECK(!Profiles_Save(&p), "saves: so does a profiles folder");
        chmod(cfg, 0755);
        chmod(data, 0755);
    }
    /* A folder (not empty) where the file should be: the rename fails. */
    snprintf(blocker, sizeof(blocker), "%s/profiles.txt", data);
    remove(blocker); /* the saved copy; p still holds it and saves it back below */
    mkdir(blocker, 0755);
    snprintf(inside, sizeof(inside), "%s/keep", blocker);
    FILE *f = fopen(inside, "w");
    if (f) fclose(f);
    CHECK(!Profiles_Save(&p), "saves: a failed last step is a failed save, not a quiet success");
    remove(inside);
    rmdir(blocker);
    CHECK(Profiles_Save(&p), "saves: profiles save again once it can");
    snprintf(blocker, sizeof(blocker), "%s/games.txt", cfg);
    char keep[1000];
    snprintf(keep, sizeof(keep), "%s.kept", blocker);
    rename(blocker, keep);
    mkdir(blocker, 0755);
    snprintf(inside, sizeof(inside), "%s/keep", blocker);
    f = fopen(inside, "w");
    if (f) fclose(f);
    CHECK(!Manifest_Save(&m), "saves: the catalog's failed last step too");
    remove(inside);
    rmdir(blocker);
    rename(keep, blocker);
    CHECK(Manifest_Save(&m), "saves: the catalog saves again once it can");
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

/* Everything the tests wrote, however many files that is: a fixed list of
 * names fell behind as tests were added and left a folder in /tmp per run.
 * Never follows a symlink, and only ever acts inside our mkdtemp folder. */
static void RemoveTree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                char child[1024];
                snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
                RemoveTree(child);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

static void Cleanup(void) {
    if (strstr(gTmpRoot, "/ghost-launcher-test-")) RemoveTree(gTmpRoot);
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
    static const char *const kSlugs[12] = {"blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift", "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb", "chipforge"};
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
    static const char *const kSlugs[12] = {"blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift", "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb", "chipforge"};
    bool all = true, sane = true;
    for (int i = 0; i < 12; i++) {
        const HowTo *h = HowTo_ForSlug(kSlugs[i]);
        if (!h) { all = false; continue; }
        int controls = 0;
        while (controls < HOWTO_MAX_CONTROLS && h->controls[controls][0]) {
            if (!h->controls[controls][1] || !h->controls[controls][1][0]) sane = false;
            controls++;
        }
        if (controls < 3 || controls >= HOWTO_MAX_CONTROLS || strlen(h->goal) < 20 || strlen(h->tip) < 10 || strlen(h->goal) > 240 || strlen(h->tip) > 200) sane = false;
    }
    CHECK(all && HowTo_Count() == 12, "howto: every game and Chipforge has a card");
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

/* ------------------------------------------------------------ input */

/* One frame of a held (or newly pressed) keyboard key. */
static void RawKey(InputRaw *raw, InputKey k, bool pressed, bool down) {
    memset(raw, 0, sizeof(*raw));
    raw->focused = true;
    raw->keyPressed[k] = pressed;
    raw->keyDown[k] = down;
}

/* Holds k for `frames` frames after a press at 60 fps; returns the frame
 * of the first repeat (0 = the press itself) and counts every step. */
static int HoldKey(InputState *st, InputKey k, int frames, int *steps) {
    InputRaw raw;
    Input in;
    int first = -1;
    *steps = 0;
    for (int f = 0; f <= frames; f++) {
        RawKey(&raw, k, f == 0, true);
        Input_Step(st, &raw, 1.0f / 60.0f, &in);
        if (in.keyRepeat[k]) {
            (*steps)++;
            if (f > 0 && first < 0) first = f;
        }
    }
    RawKey(&raw, k, false, false);
    Input_Step(st, &raw, 1.0f / 60.0f, &in);
    return first;
}

static void test_input_repeat(void) {
    InputState st;
    Input_Init(&st);
    int steps = 0;
    int first = HoldKey(&st, IK_RIGHT, 60, &steps);
    CHECK(first >= 24 && first <= 25, "input: a held arrow first repeats after 0.4 s");
    CHECK(steps >= 7 && steps <= 8, "input: then about ten steps a second (press + 6-7 repeats in the first second)");
    first = HoldKey(&st, IK_RIGHT, 24 + 60, &steps);
    CHECK(steps >= 11 && steps <= 12, "input: a second of repeating is about ten steps");
    first = HoldKey(&st, IK_RIGHT, 20, &steps);
    CHECK(first < 0 && steps == 1, "input: let go before 0.4 s and it moved once");
    first = HoldKey(&st, IK_BACKSPACE, 40, &steps);
    CHECK(first >= 24 && first <= 25, "input: Backspace repeats");
    first = HoldKey(&st, IK_D, 120, &steps);
    CHECK(first < 0 && steps == 1, "input: letter keys never repeat (D would drop a coin per step)");
    first = HoldKey(&st, IK_DELETE, 40, &steps);
    CHECK(first >= 24 && first <= 25, "input: Delete repeats (a text field deletes on while it is held)");
    first = HoldKey(&st, IK_W, 120, &steps);
    CHECK(first < 0 && steps == 1, "input: only the arrows, the d-pad, Backspace and Delete repeat");

    /* The hall's Remove reads the press alone: holding Delete there asks once. */
    InputRaw rawDel;
    Input inDel;
    int removes = 0, deletes = 0;
    for (int f = 0; f < 90; f++) {
        RawKey(&rawDel, IK_DELETE, f == 0, true);
        Input_Step(&st, &rawDel, 1.0f / 60.0f, &inDel);
        if (Keymap_Pressed(KA_ADMIN_REMOVE, &inDel)) removes++;
        if (Keymap_Pressed(KA_TEXT_DELETE, &inDel)) deletes++;
    }
    RawKey(&rawDel, IK_DELETE, false, false);
    Input_Step(&st, &rawDel, 1.0f / 60.0f, &inDel);
    CHECK(removes == 1 && deletes > 5, "keymap: held Delete removes one machine but deletes text on and on");

    /* A hitch (one very long frame) gives one step, not a burst. */
    InputRaw raw;
    Input in;
    RawKey(&raw, IK_LEFT, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    RawKey(&raw, IK_LEFT, false, true);
    Input_Step(&st, &raw, 2.0f, &in);
    bool hitch = in.keyRepeat[IK_LEFT];
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(hitch && !in.keyRepeat[IK_LEFT], "input: a two-second frame repeats once, then waits");
    RawKey(&raw, IK_LEFT, false, false);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);

    /* A raylib tap shorter than a frame (pressed but no longer down) counts. */
    RawKey(&raw, IK_UP, true, false);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.key[IK_UP] && in.keyRepeat[IK_UP], "input: a tap within one frame still counts");

    /* The screen changes while Right (or Backspace) is held: the hold stays
     * with the old screen until the key is pressed again. */
    RawKey(&raw, IK_RIGHT, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    for (int f = 0; f < 30; f++) { RawKey(&raw, IK_RIGHT, false, true); Input_Step(&st, &raw, 1.0f / 60.0f, &in); }
    bool wasRepeating = in.keyRepeat[IK_RIGHT] || st.keyRep[IK_RIGHT].armed;
    Input_DropHolds(&st);
    int leaked = 0;
    for (int f = 0; f < 90; f++) { RawKey(&raw, IK_RIGHT, false, true); Input_Step(&st, &raw, 1.0f / 60.0f, &in); if (in.keyRepeat[IK_RIGHT]) leaked++; }
    CHECK(wasRepeating && leaked == 0, "input: after a screen change a held arrow does not repeat on");
    RawKey(&raw, IK_RIGHT, false, false);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    RawKey(&raw, IK_RIGHT, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.keyRepeat[IK_RIGHT], "input: ...and the next press works at once");
    RawKey(&raw, IK_RIGHT, false, false);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
}

static void PadFrame(InputState *st, Input *in, PadButton b, bool down, bool focused, float dt) {
    InputRaw raw;
    memset(&raw, 0, sizeof(raw));
    raw.focused = focused;
    if (b < PB_COUNT) raw.padDown[b] = down && focused;
    Input_Step(st, &raw, dt, in);
}

static void test_input_pad(void) {
    InputState st;
    Input_Init(&st);
    Input in;
    PadFrame(&st, &in, PB_COUNT, false, true, 1.0f / 60.0f); /* the window has focus */

    int confirms = 0;
    for (int f = 0; f < 30; f++) {
        PadFrame(&st, &in, PB_A, true, true, 1.0f / 60.0f);
        if (Keymap_Pressed(KA_HALL_PLAY, &in)) confirms++;
        CHECK(!in.key[IK_ENTER] && !in.key[IK_SPACE], "pad: A is not the Enter or Space key");
    }
    PadFrame(&st, &in, PB_A, false, true, 1.0f / 60.0f);
    CHECK(confirms == 1, "pad: holding A confirms exactly once");

    PadFrame(&st, &in, PB_B, true, true, 1.0f / 60.0f);
    CHECK(Keymap_Pressed(KA_BACK, &in) && !Keymap_Pressed(KA_UI_PRESS, &in), "pad: B is back");
    PadFrame(&st, &in, PB_B, false, true, 1.0f / 60.0f);

    int steps = 0, first = -1;
    for (int f = 0; f <= 60; f++) {
        PadFrame(&st, &in, PB_RIGHT, true, true, 1.0f / 60.0f);
        if (in.padRepeat[PB_RIGHT]) { steps++; if (f > 0 && first < 0) first = f; }
    }
    PadFrame(&st, &in, PB_RIGHT, false, true, 1.0f / 60.0f);
    CHECK(first >= 24 && first <= 25 && steps >= 7 && steps <= 8, "pad: the d-pad repeats like the arrows");
    PadFrame(&st, &in, PB_LB, true, true, 1.0f / 60.0f);
    bool lb = in.pad[PB_LB];
    steps = 0;
    for (int f = 0; f < 60; f++) { PadFrame(&st, &in, PB_LB, true, true, 1.0f / 60.0f); if (in.padRepeat[PB_LB]) steps++; }
    PadFrame(&st, &in, PB_LB, false, true, 1.0f / 60.0f);
    CHECK(lb && steps == 0, "pad: LB is read, and shoulder buttons never repeat");

    /* Leaving a game with B held: the launcher gets focus with B already
     * down. That is not a press made here. */
    PadFrame(&st, &in, PB_B, true, false, 1.0f / 60.0f);
    CHECK(!in.pad[PB_B], "pad: ignored while the window is in the background");
    PadFrame(&st, &in, PB_B, true, true, 1.0f / 60.0f);
    bool fired = in.pad[PB_B];
    PadFrame(&st, &in, PB_B, true, true, 1.0f / 60.0f);
    fired = fired || in.pad[PB_B];
    CHECK(!fired, "pad: a button held while focus comes back does not fire");
    PadFrame(&st, &in, PB_B, false, true, 1.0f / 60.0f);
    PadFrame(&st, &in, PB_B, true, true, 1.0f / 60.0f);
    CHECK(in.pad[PB_B], "pad: the next real press does");
    PadFrame(&st, &in, PB_B, false, true, 1.0f / 60.0f);

    PadFrame(&st, &in, PB_LEFT, true, false, 1.0f / 60.0f);
    steps = 0;
    for (int f = 0; f < 90; f++) { PadFrame(&st, &in, PB_LEFT, true, true, 1.0f / 60.0f); if (in.padRepeat[PB_LEFT]) steps++; }
    CHECK(steps == 0, "pad: a d-pad held since before focus came back never starts repeating");
    PadFrame(&st, &in, PB_LEFT, false, true, 1.0f / 60.0f);

    PadFrame(&st, &in, PB_DOWN, true, true, 1.0f / 60.0f);
    for (int f = 0; f < 10; f++) PadFrame(&st, &in, PB_DOWN, true, true, 1.0f / 60.0f);
    Input_DropHolds(&st);
    steps = 0;
    for (int f = 0; f < 90; f++) { PadFrame(&st, &in, PB_DOWN, true, true, 1.0f / 60.0f); if (in.padRepeat[PB_DOWN] || in.pad[PB_DOWN]) steps++; }
    CHECK(steps == 0, "pad: after a screen change a held d-pad neither fires nor repeats");
    PadFrame(&st, &in, PB_DOWN, false, true, 1.0f / 60.0f);
}

static void test_input_keys_and_device(void) {
    InputState st;
    Input_Init(&st);
    InputRaw raw;
    Input in;

    RawKey(&raw, IK_ENTER, true, true);
    raw.alt = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(Keymap_Pressed(KA_FULLSCREEN, &in), "keys: Alt+Enter is the fullscreen key");
    CHECK(!Keymap_Pressed(KA_TEXT_SAVE, &in) && !Keymap_Pressed(KA_HALL_PLAY, &in) && !Keymap_Pressed(KA_UI_PRESS, &in) &&
          !Keymap_Pressed(KA_FORM_NEXT, &in), "keys: Alt+Enter does nothing else (no game, no save)");
    RawKey(&raw, IK_ENTER, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(Keymap_Pressed(KA_TEXT_SAVE, &in) && Keymap_Pressed(KA_HALL_PLAY, &in) && Keymap_Pressed(KA_FORM_NEXT, &in) &&
          !Keymap_Pressed(KA_FULLSCREEN, &in), "keys: plain Enter plays and saves");
    RawKey(&raw, IK_F11, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(Keymap_Pressed(KA_FULLSCREEN, &in) && !Keymap_Pressed(KA_HALL_PLAY, &in), "keys: F11 is fullscreen");
    RawKey(&raw, IK_SPACE, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(Keymap_Pressed(KA_HALL_PLAY, &in) && !Keymap_Pressed(KA_TEXT_SAVE, &in) && !Keymap_Pressed(KA_FORM_NEXT, &in),
          "keys: Space plays, but types a space in a text field");
    RawKey(&raw, IK_ESCAPE, true, true);
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(Keymap_Pressed(KA_BACK, &in) && Keymap_Pressed(KA_TEXT_CANCEL, &in), "keys: Esc is back, and puts a name back");

    /* The device used last. */
    Input_Init(&st);
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && !in.any, "device: the keyboard until something is used");
    raw.mouseMoved = true;
    raw.mouseX = 100.0f;
    raw.mouseY = 50.0f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_MOUSE && in.any && in.mouseInside, "device: moving the mouse");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.padDown[PB_X] = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_PAD, "device: a pad button");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.padDown[PB_X] = true; /* still held: not a new use of the pad */
    raw.chars[0] = 'q';
    raw.charCount = 1;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && in.charCount == 1 && in.chars[0] == 'q', "device: typing is the keyboard");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.wheel = -1.0f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_MOUSE, "device: the wheel is the mouse");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.mouseMoved = true;
    raw.keyPressed[IK_L] = raw.keyDown[IK_L] = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD, "device: a key pressed while the mouse drifts is the keyboard");
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && !in.any, "device: remembered while nothing happens");

    /* Hints follow the keyboard or the pad, whichever was used last; the
     * mouse leaves them as they were. */
    CHECK(in.hintDevice == DEV_KEYBOARD, "hint device: the keyboard to start with");
    raw.padDown[PB_A] = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    raw.padDown[PB_A] = false;
    raw.mouseMoved = true;
    raw.mouseX = 300.0f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.lastDevice == DEV_MOUSE && in.hintDevice == DEV_PAD, "hint device: a pad player who nudges the mouse keeps pad hints");
    raw.mouseMoved = false;
    raw.keyPressed[IK_D] = raw.keyDown[IK_D] = true;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    raw.keyPressed[IK_D] = raw.keyDown[IK_D] = false;
    CHECK(in.hintDevice == DEV_KEYBOARD, "hint device: a key turns them back to keys");
    CHECK(Input_PadRepeats(PB_X) && !Input_PadRepeats(PB_A) && !Input_PadRepeats(PB_Y),
          "pad: X repeats (the name board's delete), A and Y do not");

    raw.mouseX = -1.0f;
    raw.mouseY = 10.0f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(!in.mouseInside, "mouse: the letterbox is outside the screen");
    raw.mouseX = 959.5f;
    raw.mouseY = 639.5f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.mouseInside, "mouse: the bottom-right pixel is inside");
    raw.mouseX = 960.0f;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(!in.mouseInside, "mouse: x = 960 is past the edge");
    raw.charCount = 999;
    Input_Step(&st, &raw, 1.0f / 60.0f, &in);
    CHECK(in.charCount == INPUT_MAX_CHARS, "input: a runaway character count is capped");
}

/* -------------------------------------------------------- prefs watch */

static void WritePrefs(const char *content) {
    char dir[800];
    snprintf(dir, sizeof(dir), "%s/config/ghost-arcade", gTmpRoot);
    mkdir(dir, 0755);
    WriteFile("config/ghost-arcade/prefs.ini", content);
}

static void test_prefswatch(void) {
    WritePrefs("music_volume=70\nscreen_shake=1\nreduced_flashing=0\nfullscreen=0\n");
    Prefs_Load();
    PrefsWatch w;
    PrefsWatch_Init(&w, true);

    /* A game the player started changes two settings. */
    WritePrefs("# written by a game\nmusic_volume=30\nscreen_shake=1\nreduced_flashing=1\nfullscreen=0\n");
    CHECK(!PrefsWatch_Update(&w, true, false, false) && Prefs_Get()->musicVolume == 70, "prefswatch: nothing happened, nothing is read");
    CHECK(!PrefsWatch_Update(&w, false, false, false), "prefswatch: losing focus reads nothing");
    CHECK(PrefsWatch_Update(&w, true, false, false), "prefswatch: getting focus back reads prefs.ini");
    CHECK(Prefs_Get()->musicVolume == 30 && Prefs_Get()->reducedFlashing, "prefswatch: ...and sees the game's changes");

    WritePrefs("music_volume=40\nscreen_shake=0\nreduced_flashing=1\nfullscreen=0\n");
    CHECK(PrefsWatch_Update(&w, true, true, false) && Prefs_Get()->musicVolume == 40 && !Prefs_Get()->screenShake,
          "prefswatch: a game exiting reads it too");

    /* F11 in the launcher right after a game changed the file. */
    WritePrefs("music_volume=10\nscreen_shake=1\nreduced_flashing=0\nfullscreen=0\n");
    CHECK(PrefsWatch_Update(&w, true, false, true), "prefswatch: read again before the launcher writes");
    ArcadePrefs *p = Prefs_Get();
    p->fullscreen = !p->fullscreen; /* what winscale.c does on F11 */
    Prefs_Save();
    char buf[400];
    CHECK(ReadFile("config/ghost-arcade/prefs.ini", buf, sizeof(buf)) && strstr(buf, "music_volume=10\n") && strstr(buf, "screen_shake=1\n") &&
          strstr(buf, "reduced_flashing=0\n") && strstr(buf, "fullscreen=1\n"),
          "prefswatch: F11 keeps the volume, shake and flashing a game just set");
    p->fullscreen = false;
    Prefs_Save();
}

/* -------------------------------------------------------- launcher.ini */

static void LiniWrite(const char *content) { WriteFile("config/ghost-launcher/launcher.ini", content); }

static void RemoveLini(void) {
    char p[900];
    snprintf(p, sizeof(p), "%s/config/ghost-launcher/launcher.ini", gTmpRoot);
    remove(p);
}

static void test_launcherini(void) {
    static LauncherIni ini;
    char buf[2048];
    RemoveLini();
    CHECK(LauncherIni_Load(&ini) && ini.count == 0, "launcher.ini: a missing file is an empty one");
    CHECK(LauncherIni_Get(&ini, "hall_sounds") == NULL && LauncherIni_GetInt(&ini, "hall_sounds", 7, 0, 10) == 7,
          "launcher.ini: absent keys give the default");
    CHECK(LauncherIni_StoreInt(&ini, "hall_sounds", 4), "launcher.ini: first store creates the file");
    CHECK(ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) && buf[0] == '#' && strstr(buf, "\nhall_sounds=4\n"),
          "launcher.ini: a new file gets a header comment and the key");

    LiniWrite("# my notes\n\nfuture_key = from a newer launcher  \n; old style comment\nhall_sounds = 7\nstart_on=last\n");
    CHECK(LauncherIni_Load(&ini) && ini.count == 6, "launcher.ini: every line is kept");
    CHECK(LauncherIni_GetInt(&ini, "hall_sounds", 0, 0, 10) == 7 && strcmp(LauncherIni_Get(&ini, "future_key"), "from a newer launcher") == 0,
          "launcher.ini: spaces around keys and values are ignored");
    CHECK(LauncherIni_Set(&ini, "hall_sounds", "3") && LauncherIni_Set(&ini, "button_hints", "xbox") && LauncherIni_Save(&ini),
          "launcher.ini: set and save");
    CHECK(ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
          strcmp(buf, "# my notes\n\nfuture_key = from a newer launcher\n; old style comment\nhall_sounds=3\nstart_on=last\nbutton_hints=xbox\n") == 0,
          "launcher.ini: a change stays where the key was; comments, blanks and unknown keys survive; new keys go last");

    LiniWrite("a=1\nb=2\na=3");
    LauncherIni_Load(&ini);
    CHECK(strcmp(LauncherIni_Get(&ini, "a"), "3") == 0, "launcher.ini: a key written twice by hand: the last wins");
    CHECK(LauncherIni_Set(&ini, "a", "5") && LauncherIni_Save(&ini) && ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
          strcmp(buf, "a=5\nb=2\n") == 0, "launcher.ini: setting it leaves one copy, where the first was");

    LiniWrite("n=12x\nbig=99999\nneg=-5\nempty=\non=On\noff=no\nodd=maybe\n");
    LauncherIni_Load(&ini);
    CHECK(LauncherIni_GetInt(&ini, "n", 4, 0, 10) == 4 && LauncherIni_GetInt(&ini, "empty", 4, 0, 10) == 4, "launcher.ini: a value that is not a number gives the default");
    CHECK(LauncherIni_GetInt(&ini, "big", 4, 0, 10) == 10 && LauncherIni_GetInt(&ini, "neg", 4, 0, 10) == 0, "launcher.ini: numbers are clamped");
    CHECK(LauncherIni_GetBool(&ini, "on", false) && !LauncherIni_GetBool(&ini, "off", true) && LauncherIni_GetBool(&ini, "odd", true) &&
          !LauncherIni_GetBool(&ini, "odd", false), "launcher.ini: on/off words, anything else gives the default");

    int before = ini.count;
    CHECK(!LauncherIni_Set(&ini, "Bad", "1") && !LauncherIni_Set(&ini, "a b", "1") && !LauncherIni_Set(&ini, "", "1") &&
          !LauncherIni_Set(&ini, "a=b", "1") && !LauncherIni_Set(&ini, "k", "two\nlines") && !LauncherIni_Set(&ini, "k", NULL) &&
          !LauncherIni_Set(&ini, "a234567890123456789012345678901234567890123456789012345678901234", "1") && ini.count == before,
          "launcher.ini: keys and values that could break the file are refused");
    CHECK(LauncherIni_Set(&ini, "fav.gemdive", "1") && strcmp(LauncherIni_Get(&ini, "fav.gemdive"), "1") == 0, "launcher.ini: dotted per-game keys");

    /* A very long line is cut, and its tail does not turn into lines. */
    char longLine[1500];
    memset(longLine, 'x', sizeof(longLine));
    memcpy(longLine, "# ", 2);
    longLine[sizeof(longLine) - 1] = '\0';
    char content[1700];
    snprintf(content, sizeof(content), "%s\nk=v\n", longLine);
    LiniWrite(content);
    CHECK(LauncherIni_Load(&ini) && ini.count == 2 && strcmp(LauncherIni_Get(&ini, "k"), "v") == 0 && strlen(ini.lines[0]) == LINI_LINE_LEN - 1,
          "launcher.ini: an over-long line is cut short");
    CHECK(ini.truncated && !LauncherIni_StoreInt(&ini, "k", 2) && ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
          strlen(buf) == strlen(content), "launcher.ini: ...and that file is never saved over, so the long line survives");
    LiniWrite("hall_sounds=3\n");
    CHECK(LauncherIni_Load(&ini) && !ini.truncated, "launcher.ini: a file that fits is not marked cut");
    snprintf(content, sizeof(content), "%.*s\nk=v\n", LINI_LINE_LEN - 1, longLine);
    LiniWrite(content);
    CHECK(LauncherIni_Load(&ini) && !ini.truncated && strlen(ini.lines[0]) == LINI_LINE_LEN - 1, "launcher.ini: a line of exactly the longest length is whole");

    /* A full file still updates keys it has. */
    static char many[LINI_MAX_LINES * 12 + 64];
    size_t used = 0;
    for (int i = 0; i < LINI_MAX_LINES + 20; i++) used += (size_t)snprintf(many + used, sizeof(many) - used, "k%d=%d\n", i, i);
    LiniWrite(many);
    LauncherIni_Load(&ini);
    CHECK(ini.count == LINI_MAX_LINES && LauncherIni_Set(&ini, "k5", "x") && !LauncherIni_Set(&ini, "brand_new", "1"),
          "launcher.ini: at most LINI_MAX_LINES lines; a full file refuses new keys");
    CHECK(ini.truncated && !LauncherIni_Save(&ini) && !LauncherIni_Store(&ini, "k5", "y"), "launcher.ini: a file with more lines than fit is not saved");
    {
        static char after[sizeof(many)];
        CHECK(ReadFile("config/ghost-launcher/launcher.ini", after, sizeof(after)) && strcmp(after, many) == 0,
              "launcher.ini: ...so none of its lines are lost");
    }

    /* Store re-reads first: another launcher's change is not written over. */
    LiniWrite("hall_sounds=7\n");
    LauncherIni_Load(&ini);
    LiniWrite("hall_sounds=7\nstart_on=first\n");
    CHECK(LauncherIni_Store(&ini, "hall_sounds", "2") && ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
          strcmp(buf, "hall_sounds=2\nstart_on=first\n") == 0 && strcmp(LauncherIni_Get(&ini, "start_on"), "first") == 0,
          "launcher.ini: a store keeps a change made since this copy loaded");

    /* A file that cannot be read is never replaced. */
    RemoveLini();
    char p[900];
    snprintf(p, sizeof(p), "%s/config/ghost-launcher/launcher.ini", gTmpRoot);
    mkdir(p, 0755);
    CHECK(!LauncherIni_Load(&ini) && !LauncherIni_Store(&ini, "hall_sounds", "1"), "launcher.ini: unreadable means no save");
    rmdir(p);
    RemoveLini();
}

/* ---------------------------------------------------- online.conf writer */

static void test_online_conf_writer(void) {
    char buf[1024];
    WriteFile("config/ghost-launcher/online.conf",
              "# Ghost Arcade online settings\n  url = https://custom.example\nenabled=1\n# check_updates=1 (a note)\ncheck_updates_extra=7\nkey=k1\r\ncheck_updates=1\n");
    CHECK(SyncConfig_SetValue("check_updates", "0"), "online.conf: a line saves");
    CHECK(ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) &&
          strcmp(buf, "# Ghost Arcade online settings\n  url = https://custom.example\nenabled=1\n# check_updates=1 (a note)\ncheck_updates_extra=7\nkey=k1\r\ncheck_updates=0\n") == 0,
          "online.conf: only that line changes; comments, look-alike keys, spacing and CRLF are kept");
    SyncConfig cfg;
    SyncConfig_Load(&cfg);
    CHECK(!cfg.checkUpdates && cfg.enabled && strcmp(cfg.url, "https://custom.example") == 0 && strcmp(cfg.key, "k1") == 0,
          "online.conf: the launcher reads back what it wrote");

    WriteFile("config/ghost-launcher/online.conf", "enabled=1\n# x\n enabled = 0\nurl=https://a.example");
    CHECK(SyncConfig_SetValue("enabled", "1") && ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) &&
          strcmp(buf, "enabled=1\n# x\nurl=https://a.example\n") == 0, "online.conf: duplicates fold into the first line");
    CHECK(SyncConfig_SetValue("update_repo", "someone/fork") && ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) &&
          strcmp(buf, "enabled=1\n# x\nurl=https://a.example\nupdate_repo=someone/fork\n") == 0,
          "online.conf: a new key goes at the end, after a newline the last line lacked");

    CHECK(!SyncConfig_SetValue("Bad", "1") && !SyncConfig_SetValue("a b", "1") && !SyncConfig_SetValue("", "1") &&
          !SyncConfig_SetValue("url", "https://x\nkey=evil") && !SyncConfig_SetValue("url", NULL),
          "online.conf: keys and values that could add lines are refused");
    CHECK(ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) && strcmp(buf, "enabled=1\n# x\nurl=https://a.example\nupdate_repo=someone/fork\n") == 0,
          "online.conf: a refused write leaves the file alone");

    remove_online_conf_for_test();
    CHECK(SyncConfig_SetValue("check_updates", "0") && ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) && strcmp(buf, "check_updates=0\n") == 0,
          "online.conf: a missing file is created with the one line");
    SyncConfig_Load(&cfg);
    CHECK(!cfg.decided && !cfg.enabled, "online.conf: writing another key does not answer the share-scores question");

    /* A file far too big to be online.conf is not rewritten. */
    static char huge[70000];
    memset(huge, '#', sizeof(huge) - 2);
    huge[sizeof(huge) - 2] = '\n';
    huge[sizeof(huge) - 1] = '\0';
    WriteFile("config/ghost-launcher/online.conf", huge);
    CHECK(!SyncConfig_SetValue("enabled", "1"), "online.conf: an oversized file is refused, not truncated");
    char p[900];
    snprintf(p, sizeof(p), "%s/config/ghost-launcher/online.conf", gTmpRoot);
    struct stat st;
    CHECK(stat(p, &st) == 0 && st.st_size == (off_t)(sizeof(huge) - 1), "online.conf: ...and left as it was");
    remove_online_conf_for_test();
}

/* ------------------------------------------------- Settings: saving */

/* Shared rows: prefs.ini is read again first, so a game's change made
 * while the launcher had an old copy is kept; only the one value moves. */
static void test_settings_shared_prefs(void) {
    char buf[512];
    WritePrefs("music_volume=30\nscreen_shake=0\nreduced_flashing=1\nfullscreen=0\n");
    Prefs_Load(); /* the launcher's copy */
    WritePrefs("# Ghost Arcade preferences, shared by every game\nmusic_volume=40\nscreen_shake=1\nreduced_flashing=1\nfullscreen=0\n"); /* a game saved */
    CHECK(Settings_SaveShared(SP_FULLSCREEN, 1), "settings: fullscreen saves");
    CHECK(ReadFile("config/ghost-arcade/prefs.ini", buf, sizeof(buf)) && strstr(buf, "music_volume=40\n") && strstr(buf, "screen_shake=1\n") &&
          strstr(buf, "reduced_flashing=1\n") && strstr(buf, "fullscreen=1\n"),
          "settings: a game's music and shake survive the launcher saving fullscreen");
    CHECK(Settings_SaveShared(SP_MUSIC, 120) && Settings_SharedValue(SP_MUSIC) == 100, "settings: music volume is kept in 0-100");
    CHECK(Settings_SaveShared(SP_SHAKE, 0) && Settings_SaveShared(SP_FLASHING, 0) && ReadFile("config/ghost-arcade/prefs.ini", buf, sizeof(buf)) &&
          strstr(buf, "music_volume=100\nscreen_shake=0\nreduced_flashing=0\nfullscreen=1\n"), "settings: each switch saves; the four lines stay together");
    Prefs_Load();
    CHECK(Settings_SharedValue(SP_MUSIC) == 100 && !Settings_SharedValue(SP_SHAKE) && !Settings_SharedValue(SP_FLASHING) && Settings_SharedValue(SP_FULLSCREEN),
          "settings: what is read back is what was saved (as every game will read it)");
    /* A folder where prefs.ini goes: the shared Prefs_Save opens its temp
     * copy fine and then cannot rename it, which it does not report. */
    char blocker[800], inside[900];
    snprintf(blocker, sizeof(blocker), "%s/config/ghost-arcade/prefs.ini", gTmpRoot);
    remove(blocker);
    mkdir(blocker, 0755);
    snprintf(inside, sizeof(inside), "%s/keep", blocker);
    FILE *f = fopen(inside, "w");
    if (f) fclose(f);
    CHECK(!Settings_SaveShared(SP_FLASHING, 1), "settings: a shared setting that did not reach prefs.ini says Couldn't save");
    CHECK(!Settings_SharedValue(SP_FLASHING), "settings: ...and shows what the games will read, not the change");
    remove(inside);
    rmdir(blocker);
    CHECK(Settings_SaveShared(SP_FLASHING, 1) && Settings_SharedValue(SP_FLASHING), "settings: it saves again once it can");
    WritePrefs("music_volume=70\nscreen_shake=1\nreduced_flashing=0\nfullscreen=0\n");
    Prefs_Load();
}

static void test_settings_launcher_ini(void) {
    static LauncherIni ini;
    char buf[1024];
    RemoveLini();
    LauncherIni_Load(&ini);
    CHECK(Settings_HallSounds(&ini) == 10, "settings: hall sounds start at full (the old fixed volumes)");
    LiniWrite("# notes\nstart_on=last\nhall_sounds=7\nfav.blockfall=1\n");
    LauncherIni_Load(&ini);
    CHECK(Settings_HallSounds(&ini) == 7, "settings: hall sounds read from launcher.ini");
    CHECK(Settings_SaveHallSounds(&ini, 0) && ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
          strcmp(buf, "# notes\nstart_on=last\nhall_sounds=0\nfav.blockfall=1\n") == 0 && Settings_HallSounds(&ini) == 0,
          "settings: hall sounds 0 (mute) saves in place; the other lines stay");
    CHECK(Settings_SaveHallSounds(&ini, 15) && Settings_HallSounds(&ini) == 10, "settings: hall sounds are kept in 0-10");
    char prefsBuf[256];
    CHECK(ReadFile("config/ghost-arcade/prefs.ini", prefsBuf, sizeof(prefsBuf)) && !strstr(prefsBuf, "hall_sounds"),
          "settings: the launcher's own setting never goes into the games' prefs.ini");
    char longLine[700];
    memset(longLine, 'x', sizeof(longLine) - 1);
    longLine[0] = '#';
    longLine[sizeof(longLine) - 1] = '\0';
    char content[800];
    snprintf(content, sizeof(content), "%s\nhall_sounds=4\n", longLine);
    LiniWrite(content);
    LauncherIni_Load(&ini);
    CHECK(!Settings_SaveHallSounds(&ini, 2), "settings: a launcher.ini too big to keep says Couldn't save");
    LauncherIni_Load(&ini);
    CHECK(Settings_HallSounds(&ini) == 4, "settings: ...and the file keeps what it had");
    RemoveLini();

    /* Button hints: auto unless the file says keys or pad; saved in place. */
    LauncherIni_Load(&ini);
    CHECK(Settings_ButtonHints(&ini) == HINTS_AUTO, "settings: button hints start on Auto");
    LiniWrite("# notes\nbutton_hints=pad\nhall_sounds=7\n");
    LauncherIni_Load(&ini);
    CHECK(Settings_ButtonHints(&ini) == HINTS_PAD, "settings: button hints read from launcher.ini");
    CHECK(Settings_SaveButtonHints(&ini, HINTS_KEYS) && ReadFile("config/ghost-launcher/launcher.ini", buf, sizeof(buf)) &&
              strcmp(buf, "# notes\nbutton_hints=keys\nhall_sounds=7\n") == 0 && Settings_ButtonHints(&ini) == HINTS_KEYS,
          "settings: button hints save in place; the other lines stay");
    LiniWrite("button_hints=PlayStation\n");
    LauncherIni_Load(&ini);
    CHECK(Settings_ButtonHints(&ini) == HINTS_AUTO, "settings: a button hints value it does not know is Auto");
    RemoveLini();
}

static void test_settings_online(void) {
    char buf[1024];
    WriteFile("config/ghost-launcher/online.conf", "# mine\nenabled=0\nurl=https://custom.example\nkey=k1\ncheck_updates=1\nupdate_repo=me/fork\n");
    CHECK(Settings_SaveOnline("enabled", true) && ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) &&
          strcmp(buf, "# mine\nenabled=1\nurl=https://custom.example\nkey=k1\ncheck_updates=1\nupdate_repo=me/fork\n") == 0,
          "settings: Share my scores writes its one line; server, key, repo and comments stay");
    CHECK(Settings_SaveOnline("check_updates", false) && ReadFile("config/ghost-launcher/online.conf", buf, sizeof(buf)) &&
          strcmp(buf, "# mine\nenabled=1\nurl=https://custom.example\nkey=k1\ncheck_updates=0\nupdate_repo=me/fork\n") == 0,
          "settings: Check for updates too");
    SyncConfig cfg;
    SyncConfig_Load(&cfg);
    CHECK(cfg.enabled && cfg.decided && !cfg.checkUpdates && strcmp(cfg.url, "https://custom.example") == 0, "settings: ghost-sync reads them back");
    remove_online_conf_for_test();
    CHECK(Settings_SaveOnline("check_updates", true), "settings: a missing online.conf is made");
    SyncConfig_Load(&cfg);
    CHECK(!cfg.decided, "settings: and the share question is still unanswered");
    remove_online_conf_for_test();
}

static void test_settings_names(void) {
    char buf[1024], data[800];
    snprintf(data, sizeof(data), "%s/data/ghost-launcher", gTmpRoot);
    mkdir(data, 0755);
    WriteFile("data/ghost-launcher/profiles.txt", "__DEFAULT__|TRG\nBlockfall|Arcade\n");
    Profiles p;
    Profiles_Load(&p);
    CHECK(Settings_SaveName(&p, "Gemdive", "Digger") && ReadFile("data/ghost-launcher/profiles.txt", buf, sizeof(buf)) &&
          strstr(buf, "__DEFAULT__|TRG\n") && strstr(buf, "Blockfall|Arcade\n") && strstr(buf, "Gemdive|Digger\n"),
          "settings: a game's name saves beside the others");
    CHECK(Settings_SaveName(&p, "Blockfall", "") && strcmp(Profiles_GetUsername(&p, "Blockfall"), "TRG") == 0,
          "settings: Clear makes a game use the arcade name again");
    CHECK(Settings_SaveName(&p, PROFILE_DEFAULT_KEY, "Ghost") && strcmp(Profiles_GetUsername(&p, "Coilrush"), "Ghost") == 0,
          "settings: the arcade name");
    Profiles back;
    Profiles_Load(&back);
    CHECK(strcmp(Profiles_GetOwnUsername(&back, PROFILE_DEFAULT_KEY), "Ghost") == 0 && strcmp(Profiles_GetOwnUsername(&back, "Gemdive"), "Digger") == 0,
          "settings: the names read back as the games will read them");
    if (geteuid() != 0) {
        chmod(data, 0555);
        CHECK(!Settings_SaveName(&p, PROFILE_DEFAULT_KEY, "Nope") && strcmp(Profiles_GetOwnUsername(&p, PROFILE_DEFAULT_KEY), "Ghost") == 0,
              "settings: a name that cannot be saved says so, and shows the one on disk");
        chmod(data, 0755);
    }
}

static void test_settings_last_sync(void) {
    char global[800], file[900], tmp[920];
    snprintf(global, sizeof(global), "%s/data/ghost-launcher/scores/global", gTmpRoot);
    long long before = Settings_LastSync();
    CHECK(before > 0, "sync: the boards folder is there (made earlier), so there is a time");
    usleep(20000);
    snprintf(file, sizeof(file), "%s/settings-test.txt", global);
    snprintf(tmp, sizeof(tmp), "%s.tmp", file);
    FILE *f = fopen(tmp, "w");
    if (f) { fputs("normal|x|1|2026-01-01\n", f); fclose(f); }
    rename(tmp, file); /* as ghost-sync writes a board */
    long long after = Settings_LastSync();
    CHECK(after > before, "sync: a board written moves the time, even within the same second");
    remove(file);
    char saved[900];
    snprintf(saved, sizeof(saved), "%s.away", global);
    rename(global, saved);
    CHECK(Settings_LastSync() == 0, "sync: no boards yet: never");
    rename(saved, global);
}

static void test_path_status(void) {
    char bin[800], plain[800], dir[600];
    snprintf(dir, sizeof(dir), "%s/pathcheck", gTmpRoot);
    mkdir(dir, 0755);
    snprintf(bin, sizeof(bin), "%s/game", dir);
    snprintf(plain, sizeof(plain), "%s/icon.png", dir);
    FILE *f = fopen(bin, "w");
    if (f) fclose(f);
    chmod(bin, 0755);
    f = fopen(plain, "w");
    if (f) fclose(f);
    CHECK(Manifest_PathStatus(bin, PATH_PROGRAM) == PATH_FOUND, "path: a program that is there");
    CHECK(Manifest_PathStatus(plain, PATH_PROGRAM) == PATH_NOT_PROGRAM, "path: a file that is not executable");
    CHECK(Manifest_PathStatus(dir, PATH_PROGRAM) == PATH_NOT_PROGRAM, "path: a folder is not a program");
    CHECK(Manifest_PathStatus(plain, PATH_FILE) == PATH_FOUND && Manifest_PathStatus(dir, PATH_FILE) == PATH_NOT_FILE, "path: an icon file");
    CHECK(Manifest_PathStatus(dir, PATH_FOLDER) == PATH_FOUND && Manifest_PathStatus(plain, PATH_FOLDER) == PATH_NOT_FOLDER, "path: a source folder");
    CHECK(Manifest_PathStatus("/no/such/thing", PATH_FILE) == PATH_MISSING && Manifest_PathStatus("", PATH_FILE) == PATH_EMPTY,
          "path: missing, and empty (no note)");
    const char *oldHome = getenv("HOME");
    char home[800];
    snprintf(home, sizeof(home), "%s", oldHome ? oldHome : "");
    setenv("HOME", gTmpRoot, 1);
    CHECK(Manifest_PathStatus("~/pathcheck/game", PATH_PROGRAM) == PATH_FOUND, "path: ~ is the home folder");
    if (oldHome) setenv("HOME", home, 1); else unsetenv("HOME");
    CHECK(strcmp(Manifest_PathWords(PATH_NOT_PROGRAM), "not executable") == 0 && strcmp(Manifest_PathWords(PATH_FOUND), "found") == 0 &&
          Manifest_PathWords(PATH_EMPTY)[0] == '\0', "path: the words the form shows");
    remove(bin);
    remove(plain);
    rmdir(dir);
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
    test_board_download();
    test_install_id();
    test_config();
    test_scores_reader();
    test_profiles();
    test_manifest_install_field();
    test_saves_report_failure();
    test_installer();
    test_daily();
    test_achievements();
    test_input_repeat();
    test_input_pad();
    test_input_keys_and_device();
    test_prefswatch();
    test_launcherini();
    test_online_conf_writer();
    test_settings_shared_prefs();
    test_settings_launcher_ini();
    test_settings_online();
    test_settings_names();
    test_settings_last_sync();
    test_path_status();
    test_kit_all();
    test_settings_all();
    test_hall_all();
    test_row_all();
    test_page_all();
    test_extras_all();

    Cleanup();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
