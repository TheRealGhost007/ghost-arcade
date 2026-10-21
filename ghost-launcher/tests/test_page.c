/* Headless tests for Phase 5: a machine's page (src/gamepage.c) and its
 * score boards (src/boards.c), admin's Add / Edit form with every field at
 * once (src/gameform.c), and what a rename leaves behind and carries over
 * (src/machines.c, with profile.c's and lineup.c's moves). test_main.c's
 * main() points XDG_DATA_HOME and XDG_CONFIG_HOME at a temp folder first. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../src/boards.h"
#include "../src/gamepage.h"
#include "../src/gameform.h"
#include "../src/machines.h"
#include "../src/lineup.h"
#include "../src/keymap.h"
#include "song.h"

/* ------------------------------------------------------------- helpers */

static Input Blank(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.lastDevice = in.hintDevice = DEV_KEYBOARD;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input Key(InputKey k) {
    Input in = Blank();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input ShiftKey(InputKey k) {
    Input in = Key(k);
    in.shift = true;
    return in;
}

static Input Pad(PadButton b) {
    Input in = Blank();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = in.hintDevice = DEV_PAD;
    return in;
}

/* Letters as the keyboard types them: a char, and its key (Space too). */
static Input Typed(const char *s) {
    Input in = Blank();
    for (int i = 0; s[i] && i < INPUT_MAX_CHARS; i++) {
        in.chars[in.charCount++] = (unsigned char)s[i];
        if (s[i] == ' ') in.key[IK_SPACE] = in.keyDown[IK_SPACE] = true;
    }
    return in;
}

static Input Mouse(float x, float y, bool pressed, bool released) {
    Input in = Blank();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = true;
    in.mouseMoved = true;
    in.mousePressed[MB_LEFT] = pressed;
    in.mouseDown[MB_LEFT] = pressed;
    in.mouseReleased[MB_LEFT] = released;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static const KitEvent kNoEvent = {KIT_EV_NONE, KIT_NONE, 0};

static void WriteUnder(const char *env, const char *rel, const char *text) {
    char path[800], dir[800];
    snprintf(path, sizeof(path), "%s/%s", getenv(env), rel);
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = dir + strlen(getenv(env)) + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(dir, 0755);
        *p = '/';
    }
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "page: fixture write"); return; }
    fputs(text, f);
    fclose(f);
}

static void RemoveUnder(const char *env, const char *rel) {
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", getenv(env), rel);
    remove(path);
}

static bool FileHas(const char *env, const char *rel, const char *text) {
    char path[800], buf[8192];
    snprintf(path, sizeof(path), "%s/%s", getenv(env), rel);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, text) != NULL;
}

/* Noon local time on a day of 2026, so day arithmetic never meets midnight. */
static time_t Noon(int month, int day) {
    struct tm t = {0};
    t.tm_year = 2026 - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void DateAgo(int days, char *out, size_t size) {
    time_t t = time(NULL) - (time_t)days * 86400;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, size, "%Y-%m-%d", &tm);
}

/* -------------------------------------------------------------- boards */

static void test_boards_assembly(void) {
    static Boards bs;
    Boards_Clear(&bs);
    BoardYou you = {{"Arcade", "TRG"}};
    Boards_AddText(&bs,
                   "# Ghost Arcade scores\n"
                   "classic|Arcade|12840|2026-10-12\n"
                   "classic|trg|5200|2026-10-06\n"
                   "marathon|TRG|9100|2026-10-09\n"
                   "garbage\n"
                   "classic|x|0|2026-10-01\n"
                   "classic|x|abc|2026-10-01\n"
                   "|x|5|2026-10-01\n"
                   "daily-20261014|TRG|1800|2026-10-14\n"
                   "daily-20260901|TRG|99|2026-09-01\n",
                   false, &you, "daily-20260930");
    Boards_AddText(&bs,
                   "classic|Ghost|20480|2026-10-13\nclassic|Moth|18200|2026-10-11\nclassic|ARCADE|12840|2026-10-12\n"
                   "classic|A|900|d\nclassic|B|800|d\nclassic|C|800|d\nclassic|D|700|d\nclassic|E|600|d\nclassic|F|500|d\n"
                   "classic|G|400|d\nclassic|H|300|d\nclassic|I|200|d\n"
                   "daily-20261013|Moth|2200|2026-10-13\ndaily-20261014|Ghost|2600|2026-10-14\n",
                   true, &you, "daily-20260930");
    Boards_Finish(&bs);
    CHECK(bs.count == 4 && strcmp(bs.b[0].mode, "classic") == 0 && strcmp(bs.b[1].mode, "marathon") == 0 &&
              strcmp(bs.b[2].mode, "daily-20261014") == 0 && strcmp(bs.b[3].mode, "daily-20261013") == 0,
          "boards: regular boards A-Z, then the dailies newest first; one older than the cut-off is gone");
    const Board *classic = &bs.b[0];
    CHECK(classic->localCount == 2 && classic->local[0].score == 12840 && classic->local[1].score == 5200 && classic->local[0].you &&
              classic->local[1].you,
          "boards: this computer's rows best first, junk lines skipped, both your names (any case) are you");
    CHECK(classic->worldCount == BOARD_TOP && classic->world[0].score == 20480 && classic->world[9].score == 400 &&
              strcmp(classic->world[4].name, "B") == 0 && strcmp(classic->world[5].name, "C") == 0,
          "boards: the world's best ten, ties in the order the server sent them");
    CHECK(classic->worldRank == 3 && !classic->world[0].you && classic->world[2].you, "boards: your place on the world board");
    CHECK(bs.b[1].localCount == 1 && bs.b[1].worldCount == 0 && bs.b[1].worldRank == 0, "boards: a board only this computer has");
    CHECK(bs.b[2].localCount == 1 && bs.b[2].worldCount == 1 && bs.b[2].worldRank == 0 && bs.b[3].localCount == 0,
          "boards: a daily played here, and one only the world played");
    CHECK(Boards_Find(&bs, "marathon") == 1 && Boards_Find(&bs, "zen") == -1 && Boards_Find(&bs, NULL) == -1, "boards: found by mode");

    Boards_Clear(&bs);
    for (int i = 0; i < BOARD_MAX + 10; i++) {
        char line[64];
        snprintf(line, sizeof(line), "mode-%02d|TRG|%d|2026-10-01\n", i, i + 1);
        Boards_AddText(&bs, line, false, NULL, NULL);
    }
    Boards_AddText(&bs, NULL, true, NULL, NULL);
    Boards_Finish(&bs);
    CHECK(bs.count == BOARD_MAX, "boards: a file of too many boards keeps what fits");

    char w[48];
    Boards_ModeWords("classic", "daily-20261014", w, sizeof(w));
    bool words = strcmp(w, "Classic") == 0;
    Boards_ModeWords("power-14x20", "daily-20261014", w, sizeof(w));
    words &= strcmp(w, "Power 14x20") == 0;
    Boards_ModeWords("daily-20261014", "daily-20261014", w, sizeof(w));
    words &= strcmp(w, "Today's daily") == 0;
    Boards_ModeWords("daily-20261013", "daily-20261014", w, sizeof(w));
    words &= strcmp(w, "Daily 13 Oct") == 0;
    Boards_ModeWords("daily-20260105", NULL, w, sizeof(w));
    words &= strcmp(w, "Daily 5 Jan") == 0;
    CHECK(words, "boards: Classic, Power 14x20, Today's daily, Daily 13 Oct");
    CHECK(Boards_IsDaily("daily-20261014") && !Boards_IsDaily("daily-2026101x") && !Boards_IsDaily("daily-x") && !Boards_IsDaily("dailyz") &&
              !Boards_IsDaily(NULL),
          "boards: only daily-YYYYMMDD is a daily");
}

static void test_boards_files(void) {
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/pagetest.txt", "classic|TRG|300|2026-10-01\nclassic|TRG|200|2026-10-02\n");
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/pagetest.txt", "classic|Ghost|900|2026-10-01\nclassic|trg|300|2026-10-01\n");
    static Boards bs;
    BoardYou you = {{"TRG", NULL}};
    Boards_Load(&bs, "pagetest", &you, NULL);
    CHECK(bs.count == 1 && bs.b[0].localCount == 2 && bs.b[0].worldCount == 2 && bs.b[0].worldRank == 2,
          "boards: read from this computer's file and the online one");
    Boards_Load(&bs, "nosuchgame", &you, NULL);
    CHECK(bs.count == 0, "boards: no files, no boards");
    Boards_Load(&bs, "", &you, NULL);
    CHECK(bs.count == 0, "boards: no slug, no boards");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/pagetest.txt");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/pagetest.txt");
}

/* Blockfall's Sprint 40 boards are ranked by time (blockfall/src/runrecord.h):
 * a row stores 10 000 000 minus the time in hundredths, so 9 992 760 is
 * 1:12.40. The launcher shows the time wherever it shows those boards, and
 * never lets one stand in for a points best. */
static void test_blockfall_times(void) {
    CHECK(Scores_IsTimeBoard("blockfall", "sprint40") && Scores_IsTimeBoard("blockfall", "sprint40-14x20"),
          "times: Blockfall's Sprint 40 boards are ranked by time");
    CHECK(!Scores_IsTimeBoard("blockfall", "sprint") && !Scores_IsTimeBoard("blockfall", "sprint-14x20") && !Scores_IsTimeBoard("blockfall", "marathon") &&
              !Scores_IsTimeBoard("blockfall", "sprint400") && !Scores_IsTimeBoard("coilrush", "sprint40") && !Scores_IsTimeBoard(NULL, "sprint40"),
          "times: the old Sprint board, other boards and other games are points");
    char v[32];
    Scores_FormatScore(true, 9992760, v, sizeof(v));
    bool ok = strcmp(v, "1:12.40") == 0;
    Scores_FormatScore(true, 9994179, v, sizeof(v));
    ok &= strcmp(v, "0:58.21") == 0;
    Scores_FormatScore(false, 9992760, v, sizeof(v));
    ok &= strcmp(v, "9992760") == 0;
    Scores_FormatScore(true, 0, v, sizeof(v));
    ok &= strcmp(v, "0:00.00") == 0;
    CHECK(ok, "times: 9 992 760 reads 1:12.40; points stay points");

    char w[48];
    Boards_ModeWordsFor("blockfall", "sprint40", NULL, w, sizeof(w));
    ok = strcmp(w, "Sprint 40") == 0;
    Boards_ModeWordsFor("blockfall", "sprint40-14x20", NULL, w, sizeof(w));
    ok &= strcmp(w, "Sprint 40 14x20") == 0;
    Boards_ModeWordsFor("blockfall", "sprint", NULL, w, sizeof(w));
    ok &= strcmp(w, "Sprint (old)") == 0;
    Boards_ModeWordsFor("blockfall", "sprint-8x26", NULL, w, sizeof(w));
    ok &= strcmp(w, "Sprint (old) 8x26") == 0;
    Boards_ModeWordsFor("blockfall", "power-14x20", NULL, w, sizeof(w));
    ok &= strcmp(w, "Power 14x20") == 0;
    Boards_ModeWordsFor("coilrush", "sprint", NULL, w, sizeof(w));
    ok &= strcmp(w, "Sprint") == 0;
    CHECK(ok, "times: board names Sprint 40 and Sprint (old), only for Blockfall");
    /* Blockfall 3.2.0: the classic Marathon's fresh board and its old-rules one. */
    Boards_ModeWordsFor("blockfall", "marathon-10x20", NULL, w, sizeof(w));
    ok = strcmp(w, "Marathon") == 0;
    Boards_ModeWordsFor("blockfall", "marathon", NULL, w, sizeof(w));
    ok &= strcmp(w, "Classic (old rules)") == 0;
    Boards_ModeWordsFor("blockfall", "marathon-14x20", NULL, w, sizeof(w));
    ok &= strcmp(w, "Marathon 14x20") == 0;
    Boards_ModeWordsFor("coilrush", "marathon", NULL, w, sizeof(w));
    ok &= strcmp(w, "Marathon") == 0;
    CHECK(ok, "times: Blockfall's marathon-10x20 is Marathon and its bare marathon Classic (old rules); other games' marathon is just Marathon");

    /* The page's boards and the card's best, from files. */
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/blockfall.txt",
               "marathon|TRG|12248|2026-09-19\nsprint|TRG|7620|2026-09-20\nsprint40|TRG|9992760|2026-10-14\nsprint40|TRG|9990000|2026-10-14\n");
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/blockfall.txt", "sprint40|Ghost|9994179|2026-10-13\n");
    static Boards bs;
    BoardYou you = {{"TRG", NULL}};
    Boards_Load(&bs, "blockfall", &you, NULL);
    int at = Boards_Find(&bs, "sprint40"), old = Boards_Find(&bs, "sprint"), classic = Boards_Find(&bs, "marathon");
    CHECK(at >= 0 && bs.b[at].timed && old >= 0 && !bs.b[old].timed && classic >= 0 && !bs.b[classic].timed && strcmp(bs.slug, "blockfall") == 0,
          "times: the loaded Sprint 40 board knows it is timed, the others do not");
    CHECK(at >= 0 && bs.b[at].localCount == 2 && bs.b[at].local[0].score == 9992760 && bs.b[at].worldCount == 1,
          "times: fastest first on this computer's side, the world's beside it");
    ScoreBest b = Scores_LoadBest("blockfall", false);
    CHECK(b.found && !b.timed && b.score == 12248 && strcmp(b.mode, "marathon") == 0, "times: the card's Your best stays a points best, not a time's big number");
    b = Scores_LoadBest("blockfall", true);
    Scores_FormatScore(b.timed, b.score, v, sizeof(v));
    CHECK(b.found && b.timed && strcmp(v, "0:58.21") == 0, "times: with only a time board, the best is the fastest time, shown as a time");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/blockfall.txt");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/blockfall.txt");
}

/* --------------------------------------------------------------- words */

static void test_page_words(void) {
    time_t now = Noon(10, 14);
    char w[32];
    GamePage_Today(now, w, sizeof(w));
    CHECK(strcmp(w, "daily-20261014") == 0, "page: today's daily board");
    static const struct { const char *date, *want; } kDays[] = {
        {"2026-10-14", "Today"}, {"2026-10-13", "Yesterday"}, {"2026-10-11", "3 days ago"}, {"2026-10-08", "6 days ago"},
        {"2026-10-07", "7 Oct"}, {"2026-09-19", "19 Sep"},   {"2025-09-19", "19 Sep 2025"}, {"", "Never"}, {"junk", "Never"},
    };
    bool days = true;
    for (size_t i = 0; i < sizeof(kDays) / sizeof(kDays[0]); i++) {
        GamePage_DayWords(kDays[i].date, now, w, sizeof(w));
        if (strcmp(w, kDays[i].want) != 0) { days = false; printf("  %s -> %s\n", kDays[i].date, w); }
    }
    GamePage_DayWords(NULL, now, w, sizeof(w));
    CHECK(days && strcmp(w, "Never") == 0, "page: last played reads Today, Yesterday, 3 days ago, 19 Sep, Never");
}

/* ---------------------------------------------------------- page data */

static void test_page_data(void) {
    char d0[16], d1[16], d3[16], line[256];
    DateAgo(0, d0, sizeof(d0));
    DateAgo(1, d1, sizeof(d1));
    DateAgo(3, d3, sizeof(d3));
    snprintf(line, sizeof(line), "classic|Zap|4100|%s\nwave|Zap|900|%s\n", d1, d3);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/crawlshot.txt", line);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/crawlshot.txt", "classic|Ghost|9000|2026-10-01\nclassic|ZAP|4100|2026-10-01\n");
    snprintf(line, sizeof(line), "%s 20:15:00|classic|4100|3\n%s 20:15:00|classic|900|1\n%s 21:00:00|wave|50|1\n", d3, d3, d1);
    WriteUnder("XDG_DATA_HOME", "crawlshot/runs.log", line);
    WriteUnder("XDG_DATA_HOME", "ghost-launcher/achievements/crawlshot.txt", "r0|2026-10-01\n");
    GameEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "Crawlshot");
    snprintf(e.exec, sizeof(e.exec), "~/bin/crawlshot");
    snprintf(e.desc, sizeof(e.desc), "Twelve segments come down.");
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    Stats_AddGameTime(&stats, "Crawlshot", 125.0);
    Profiles prof;
    memset(&prof, 0, sizeof(prof));
    Profiles_SetUsername(&prof, PROFILE_DEFAULT_KEY, "TRG");
    Profiles_SetUsername(&prof, "Crawlshot", "Zap");
    static GamePageData d;
    GamePageSource src = {&e, 7, &stats, &prof, 0, NULL, "Built-in theme", time(NULL), NULL};
    GamePage_Load(&d, &src);
    CHECK(d.index == 7 && !d.tool && strcmp(d.slug, "crawlshot") == 0 && d.best.found && d.best.score == 4100 && d.runs == 3 &&
              strcmp(d.last, d1) == 0,
          "page data: best, runs and the last day played, from the games' files");
    CHECK(d.trophies == 1 && d.trophyTotal == 6 && d.howto && strcmp(d.playsAs, "Zap") == 0 && d.ownName,
          "page data: trophies, the how-to card, the name it plays under");
    CHECK(d.boards.count == 2 && d.boards.b[0].worldRank == 2 && d.boards.b[0].local[0].you, "page data: its boards, you on them");

    src.played = (long long)time(NULL); /* started from the launcher today: later than runs.log's last */
    GamePage_Load(&d, &src);
    CHECK(strcmp(d.last, d0) == 0, "page data: the later of runs.log and the launcher's last start");

    PageStripCell cells[PAGE_STRIP_MAX];
    int n = GamePage_Strip(&d, time(NULL), cells);
    CHECK(n == 5 && strcmp(cells[0].label, "Best") == 0 && strcmp(cells[0].value, "4100") == 0 && strcmp(cells[0].detail, "Classic") == 0 &&
              strcmp(cells[1].value, "2m") == 0 && strcmp(cells[2].value, "3") == 0 && strcmp(cells[3].value, "Today") == 0 &&
              strcmp(cells[4].value, "1/6") == 0,
          "page strip: Best 4100 Classic, Time played 2m, Runs 3, Last played Today, Trophies 1/6");
    bool fits = true;
    static const char *const kWide[] = {"12840", "12h 59m", "1234", "6 days ago", "6/6"};
    for (int i = 0; i + 1 < n; i++) {
        int w = Kit_TextWidth(kWide[i], 16) + (i == 0 ? 10 + Kit_TextWidth("Marathon", 8) : 0);
        if (cells[i].x + w > cells[i + 1].x - 12 || cells[i].x >= cells[i + 1].x) fits = false;
    }
    CHECK(fits && cells[4].x + Kit_TextWidth("6/6", 16) <= PAGE_X + 800, "page strip: wide but real values fit their cells");
    memset(&stats, 0, sizeof(stats));
    snprintf(e.exec, sizeof(e.exec), "~/bin/nosuchgame");
    src.played = 0;
    GamePage_Load(&d, &src);
    n = GamePage_Strip(&d, time(NULL), cells);
    CHECK(!d.best.found && cells[0].dim && strcmp(cells[0].value, "None yet") == 0 && cells[1].dim && strcmp(cells[1].value, "0m") == 0 &&
              cells[2].dim && cells[3].dim && strcmp(cells[3].value, "Never") == 0 && !d.howto && cells[4].dim &&
              strcmp(cells[4].value, "None") == 0,
          "page strip: a machine never played says so, faint (its time too); no trophies reads None, not 0/0");
    d.seconds = 1120;
    n = GamePage_Strip(&d, time(NULL), cells);
    CHECK(!cells[1].dim && strcmp(cells[1].value, "18m") == 0 && cells[3].dim && strcmp(cells[3].value, "Not recorded") == 0,
          "page strip: time played with no date known says Not recorded, never Never");

    /* Chipforge: songs, and who plays what. */
    WriteUnder("XDG_DATA_HOME", "ghost-arcade/music/zap.gsong", "GSONG 1\nname Zap Tune\nbpm 96\norder 0\npattern 0\n00 A-1 A-3 C-3 K..\n");
    WriteUnder("XDG_CONFIG_HOME", "ghost-arcade/music.ini", "crawlshot=zap.gsong\nskyraid=missing.gsong\n");
    static Manifest m;
    memset(&m, 0, sizeof(m));
    const char *names[4][2] = {{"Crawlshot", "crawlshot"}, {"Skyraid", "skyraid"}, {"Chipforge", "chipforge"}, {"Odd One", "Odd One"}};
    for (int i = 0; i < 4; i++) {
        snprintf(m.games[i].name, MANIFEST_FIELD_LEN, "%s", names[i][0]);
        snprintf(m.games[i].exec, MANIFEST_FIELD_LEN, "~/bin/%s", names[i][1]);
    }
    m.count = 4;
    snprintf(e.name, sizeof(e.name), "Chipforge");
    snprintf(e.exec, sizeof(e.exec), "~/bin/chipforge");
    GamePageSource tool = {&e, 2, &stats, &prof, 0, &m, "Ghost Arcade (launcher)", time(NULL), NULL};
    GamePage_Load(&d, &tool);
    CHECK(d.tool && d.songs == 1 && strcmp(d.hallSong, "Ghost Arcade (launcher)") == 0 && d.boards.count == 0, "page data: Chipforge counts its songs");
    CHECK(d.useCount == 3 && strcmp(d.uses[0].machine, "The hall") == 0 && d.uses[0].custom && strcmp(d.uses[1].song, "Zap Tune") == 0 &&
              d.uses[1].custom && strcmp(d.uses[2].machine, "Skyraid") == 0 && strcmp(d.uses[2].song, "Built-in theme") == 0 && !d.uses[2].custom,
          "page data: who plays what; a song that does not load is the built-in theme; no tool, no odd program names");
    n = GamePage_Strip(&d, time(NULL), cells);
    CHECK(n == 4 && strcmp(cells[0].value, "1") == 0 && strcmp(cells[0].detail, "song") == 0 && strcmp(cells[2].label, "Last opened") == 0 &&
              strcmp(cells[3].value, "Ghost Arcade (launcher)") == 0,
          "page strip: Chipforge's songs, time, last opened and the hall's song");
    RemoveUnder("XDG_DATA_HOME", "ghost-arcade/music/zap.gsong");
    RemoveUnder("XDG_CONFIG_HOME", "ghost-arcade/music.ini");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/crawlshot.txt");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/scores/global/crawlshot.txt");
    RemoveUnder("XDG_DATA_HOME", "crawlshot/runs.log");
    RemoveUnder("XDG_DATA_HOME", "ghost-launcher/achievements/crawlshot.txt");
}

/* --------------------------------------------------------- page layout */

static Boards sBoards;

static GamePageView PageView(void) {
    Boards_Clear(&sBoards);
    Boards_AddText(&sBoards, "classic|TRG|5|d\nmarathon|TRG|6|d\ndaily-20261014|TRG|7|d\n", false, NULL, NULL);
    Boards_Finish(&sBoards);
    GamePageView v = {
        .name = "Blockfall", .tool = false, .play = HP_PLAY, .dailyReason = "", .favourite = false, .canFavourite = true,
        .admin = false, .place = 3, .count = 12, .boards = &sBoards, .today = "daily-20261014", .device = DEV_KEYBOARD,
    };
    return v;
}

static bool Inside(KitRect r, float x0, float y0, float x1, float y1) { return r.x >= x0 && r.y >= y0 && r.x + r.w <= x1 && r.y + r.h <= y1; }

static bool Overlap(KitRect a, KitRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void test_page_layout(void) {
    GamePage p;
    memset(&p, 0, sizeof(p));
    GamePageView v = PageView();
    KitList l;
    GamePage_Layout(&p, &v, &l);
    CHECK(Kit_Get(&l, WID_BACK) && l.items[l.count - 1].id == WID_CLOSE && Kit_FirstFocus(&l) == WID_PAGE_PLAY,
          "page: Back, the X on top, and Play has the keys first");
    static const int kLeft[] = {WID_PAGE_PLAY, WID_PAGE_DAILY, WID_PAGE_NAME, WID_PAGE_FAV};
    bool column = true;
    for (int i = 0; i < 4; i++) {
        const KitItem *b = Kit_Get(&l, kLeft[i]);
        if (!b || !Kit_CanFocus(b) || !Inside(b->r, PAGE_X, PAGE_DEMO_Y + PAGE_DEMO_H, PAGE_X + PAGE_LEFT_W, PAGE_HELP_Y - 4)) column = false;
        if (b && Kit_TextWidth(b->label, b->textSize) + 20 + Kit_TextWidth(b->key, 8) + 24 > b->r.w) column = false;
        for (int j = i + 1; b && j < 4; j++) if (Overlap(b->r, Kit_Get(&l, kLeft[j])->r)) column = false;
    }
    CHECK(column, "page: Play, Daily challenge, Name for this game, Favourite under the screen, each with room for its words and key");
    int focusable = 0;
    for (int i = 0; i < l.count; i++) focusable += Kit_CanFocus(&l.items[i]);
    CHECK(focusable == 4 && (l.nav & KIT_NAV_Y) && !(l.nav & KIT_NAV_X), "page: the keys move up and down those four only");
    CHECK(strcmp(Kit_Get(&l, WID_PAGE_PLAY)->label, "Play") == 0 && !Kit_Get(&l, WID_PAGE_PLAY)->key[0] &&
              strcmp(Kit_Get(&l, WID_PAGE_DAILY)->key, "D") == 0 && strcmp(Kit_Get(&l, WID_PAGE_NAME)->key, "Shift+U") == 0 &&
              strcmp(Kit_Get(&l, WID_PAGE_FAV)->key, "F") == 0 && Kit_Get(&l, WID_PAGE_FAV)->icon == KIT_ICON_STAR,
          "page: the hall's keys for this machine at the buttons' right");
    v.device = DEV_PAD;
    GamePage_Layout(&p, &v, &l);
    CHECK(strcmp(Kit_Get(&l, WID_PAGE_DAILY)->key, "Y") == 0 && !Kit_Get(&l, WID_PAGE_NAME)->key[0] && !Kit_Get(&l, WID_PAGE_FAV)->key[0],
          "page: after pad use, the pad's button, and none where it has none");
    CHECK(!(Kit_Get(&l, WID_PAGE_PLAY)->flags & KIT_ALIGN_LEFT) && (Kit_Get(&l, WID_PAGE_DAILY)->flags & KIT_ALIGN_LEFT) &&
              (Kit_Get(&l, WID_PAGE_NAME)->flags & KIT_ALIGN_LEFT) && (Kit_Get(&l, WID_PAGE_FAV)->flags & KIT_ALIGN_LEFT),
          "page: the three small buttons keep their words in one column with or without a key; Play stays centred");
    v.device = DEV_KEYBOARD;

    const KitItem *tabs = Kit_Get(&l, WID_PAGE_TABS);
    CHECK(tabs && tabs->kind == KIT_TABS && tabs->cellCount == 3 && tabs->value == 0 && !Kit_CanFocus(tabs) && (tabs->flags & KIT_WRAP) &&
              Kit_Bounds(tabs).x + Kit_Bounds(tabs).w <= PAGE_RIGHT_X + PAGE_RIGHT_W && !Kit_Get(&l, WID_PAGE_BOARD),
          "page: About, Scores, Trophies across the right; no board on About");
    p.tab = GT_SCORES;
    GamePage_StartBoard(&p, &sBoards, "marathon");
    GamePage_Layout(&p, &v, &l);
    const KitItem *board = Kit_Get(&l, WID_PAGE_BOARD);
    CHECK(board && board->kind == KIT_STEPPER && board->value == 1 && strcmp(board->label, "Marathon") == 0 && !Kit_CanFocus(board) &&
              (board->flags & KIT_WRAP) && Inside(board->r, PAGE_RIGHT_X, PAGE_TABS_Y + PAGE_TABS_H, PAGE_RIGHT_X + PAGE_RIGHT_W, PAGE_ROWS_Y - 18),
          "page: on Scores, < Marathon >: the board of its best score first");
    float w = board->r.w;
    GamePage_SetBoard(&p, &sBoards, 0);
    GamePage_Layout(&p, &v, &l);
    CHECK(Kit_Get(&l, WID_PAGE_BOARD)->r.w == w && strcmp(Kit_Get(&l, WID_PAGE_BOARD)->label, "Classic") == 0,
          "page: the board's arrows hold still from board to board");

    const KitItem *prev = Kit_Get(&l, WID_PAGE_PREV), *next = Kit_Get(&l, WID_PAGE_NEXT), *x = Kit_Get(&l, WID_CLOSE);
    CHECK(prev && next && !Kit_CanFocus(prev) && prev->r.x < next->r.x && next->r.x + next->r.w < x->r.x && prev->r.y == x->r.y &&
              !(prev->flags & KIT_DISABLED) && !Kit_Get(&l, WID_PAGE_EDIT) && !Kit_Get(&l, WID_PAGE_REMOVE),
          "page: < 4 / 12 > left of the X; no Edit or Remove without admin");
    v.count = 1;
    v.admin = true;
    GamePage_Layout(&p, &v, &l);
    const KitItem *edit = Kit_Get(&l, WID_PAGE_EDIT), *remove = Kit_Get(&l, WID_PAGE_REMOVE);
    CHECK((Kit_Get(&l, WID_PAGE_PREV)->flags & KIT_DISABLED) && edit && remove && !Kit_CanFocus(edit) && edit->r.x + edit->r.w < remove->r.x &&
              remove->r.x + remove->r.w <= PAGE_X + 800 && edit->r.y > CLOSE_BTN_Y + CLOSE_BTN_SIZE && edit->r.x > PAGE_X + Kit_TextWidth("Girderclimb", 24),
          "page: one machine, no flipping; admin's Edit and Remove right of the title, clear of the header");
    bool onScreen = true;
    for (int i = 0; i < l.count; i++) if (!Inside(Kit_Bounds(&l.items[i]), 0, 0, KIT_SCREEN_W, KIT_SCREEN_H)) onScreen = false;
    CHECK(onScreen, "page: everything on the screen");
    v.admin = false;
    v.count = 12;

    v.play = HP_INSTALL;
    GamePage_Layout(&p, &v, &l);
    bool states = strcmp(Kit_Get(&l, WID_PAGE_PLAY)->label, "Install") == 0 && Kit_CanFocus(Kit_Get(&l, WID_PAGE_PLAY));
    v.play = HP_INSTALLING;
    GamePage_Layout(&p, &v, &l);
    states &= strcmp(Kit_Get(&l, WID_PAGE_PLAY)->label, "Installing...") == 0 && (Kit_Get(&l, WID_PAGE_PLAY)->flags & KIT_DISABLED);
    v.play = HP_CANNOT;
    GamePage_Layout(&p, &v, &l);
    states &= (Kit_Get(&l, WID_PAGE_PLAY)->flags & KIT_DISABLED) && strstr(Kit_Get(&l, WID_PAGE_PLAY)->reason, "no folder");
    CHECK(states, "page: Play reads Install, Installing... (faint), or is faint with the reason");
    v.play = HP_PLAY;
    v.dailyReason = "Blockfall has no daily challenge.";
    v.canFavourite = false;
    v.favourite = true;
    GamePage_Layout(&p, &v, &l);
    CHECK((Kit_Get(&l, WID_PAGE_DAILY)->flags & KIT_DISABLED) && strcmp(Kit_Get(&l, WID_PAGE_DAILY)->reason, "Blockfall has no daily challenge.") == 0 &&
              (Kit_Get(&l, WID_PAGE_FAV)->flags & KIT_DISABLED) && Kit_Get(&l, WID_PAGE_FAV)->value == 1,
          "page: Daily challenge faint with the reason; Favourite faint for a program launcher.ini cannot keep");
    CHECK(GamePage_KeepFocus(&l, WID_PAGE_DAILY) == WID_PAGE_PLAY && GamePage_KeepFocus(&l, WID_PAGE_NAME) == WID_PAGE_NAME &&
              GamePage_KeepFocus(&l, KIT_NONE) == WID_PAGE_PLAY,
          "page: turned to a machine where the keys' button is faint, they go to Play; a live one keeps them");
    v.play = HP_INSTALLING;
    GamePage_Layout(&p, &v, &l);
    CHECK(GamePage_KeepFocus(&l, WID_PAGE_PLAY) == WID_PAGE_NAME, "page: ...and to the first live button when Play is faint too");
    v.play = HP_PLAY;
    v = PageView();
    v.tool = true;
    p.tab = GT_TROPHIES;
    GamePage_Layout(&p, &v, &l);
    tabs = Kit_Get(&l, WID_PAGE_TABS);
    CHECK(strcmp(Kit_Get(&l, WID_PAGE_PLAY)->label, "Open Chipforge") == 0 && !Kit_Get(&l, WID_PAGE_DAILY) && !Kit_Get(&l, WID_PAGE_NAME) &&
              Kit_Get(&l, WID_PAGE_FAV) && tabs->cellCount == 2 && strcmp(tabs->cells[1], "Songs") == 0 && tabs->value == 0,
          "page: Chipforge: Open Chipforge and Favourite; About and Songs (a game's tab it lacks shows About)");
    CHECK(GamePage_KeepFocus(&l, WID_PAGE_NAME) == WID_PAGE_PLAY && GamePage_KeepFocus(&l, WID_PAGE_FAV) == WID_PAGE_FAV,
          "page: turned to Chipforge with the keys on Name for this game, they go to Open Chipforge");
    CHECK(GamePage_ClickSettling(10.0, 10.0) && GamePage_ClickSettling(10.0, 10.0 + PAGE_CLICK_SETTLE - 0.05) &&
              !GamePage_ClickSettling(10.0, 10.0 + PAGE_CLICK_SETTLE) && !GamePage_ClickSettling(-1.0e9, 5.0) && !GamePage_ClickSettling(10.0, 9.0),
          "page: opened by a click, a second click this soon is the same double-click; later ones (and keys' pages) press buttons");
}

/* ----------------------------------------------------------- page keys */

static GamePageAction Read(GamePage *p, const GamePageView *v, Input in) { return GamePage_Read(p, v, &kNoEvent, &in); }

static void test_page_read(void) {
    GamePage p;
    memset(&p, 0, sizeof(p));
    GamePageView v = PageView();
    Input back = Blank();
    back.mousePressed[MB_BACK] = true;
    CHECK(Read(&p, &v, Key(IK_ESCAPE)).verb == GA_BACK && Read(&p, &v, Pad(PB_B)).verb == GA_BACK && Read(&p, &v, Key(IK_I)).verb == GA_BACK &&
              Read(&p, &v, back).verb == GA_BACK,
          "page keys: Esc, pad B, the mouse's back button, and I that opened it, go back");
    GamePageAction a = Read(&p, &v, Key(IK_PAGE_UP));
    GamePageAction b = Read(&p, &v, Pad(PB_RT));
    CHECK(a.verb == GA_FLIP && a.value == -1 && b.verb == GA_FLIP && b.value == 1 && Read(&p, &v, Key(IK_PAGE_DOWN)).value == 1 &&
              Read(&p, &v, Pad(PB_LT)).value == -1,
          "page keys: PgUp/PgDn and LT/RT, the machine before or after");
    bool tabs = Read(&p, &v, Key(IK_RIGHT)).verb == GA_TAB && p.tab == GT_SCORES;
    tabs &= Read(&p, &v, Pad(PB_RIGHT)).verb == GA_TAB && p.tab == GT_TROPHIES;
    tabs &= Read(&p, &v, Key(IK_RIGHT)).verb == GA_TAB && p.tab == GT_ABOUT;
    tabs &= Read(&p, &v, Key(IK_LEFT)).verb == GA_TAB && p.tab == GT_TROPHIES;
    CHECK(tabs, "page keys: Left/Right (and the d-pad) go round the tabs");
    KitEvent tabEv = {KIT_EV_CHANGE, WID_PAGE_TABS, 1};
    a = GamePage_Read(&p, &v, &tabEv, &(Input){0});
    CHECK(a.verb == GA_TAB && p.tab == GT_SCORES, "page keys: Tab, LB/RB or a click on a tab (the kit's change) opens it");
    v.tool = true;
    p.tab = GT_ABOUT;
    tabs = Read(&p, &v, Key(IK_RIGHT)).verb == GA_TAB && p.tab == GT_SONGS && Read(&p, &v, Key(IK_RIGHT)).verb == GA_TAB && p.tab == GT_ABOUT;
    CHECK(tabs && Read(&p, &v, ShiftKey(IK_U)).verb == GA_NONE, "page keys: Chipforge's two tabs; no name of its own");
    v.tool = false;

    p.tab = GT_ABOUT;
    GamePage_StartBoard(&p, &sBoards, NULL);
    CHECK(Read(&p, &v, Key(IK_X)).verb == GA_NONE && strcmp(p.board, "classic") == 0, "page keys: X does nothing off the Scores tab");
    p.tab = GT_SCORES;
    bool boards = Read(&p, &v, Key(IK_X)).verb == GA_BOARD && strcmp(p.board, "marathon") == 0;
    boards &= Read(&p, &v, Pad(PB_X)).verb == GA_BOARD && strcmp(p.board, "daily-20261014") == 0;
    boards &= Read(&p, &v, Key(IK_X)).verb == GA_BOARD && strcmp(p.board, "classic") == 0;
    CHECK(boards, "page keys: on Scores, X or pad X steps round the boards");
    KitEvent boardEv = {KIT_EV_CHANGE, WID_PAGE_BOARD, 2};
    CHECK(GamePage_Read(&p, &v, &boardEv, &(Input){0}).verb == GA_BOARD && strcmp(p.board, "daily-20261014") == 0,
          "page keys: the board's arrows, clicked");
    static Boards none;
    Boards_Clear(&none);
    v.boards = &none;
    GamePage_StartBoard(&p, &none, "classic");
    CHECK(Read(&p, &v, Key(IK_X)).verb == GA_NONE && GamePage_Board(&p, &none) == -1 && p.board[0] == '\0', "page keys: no boards, nothing to step");
    v.boards = &sBoards;
    snprintf(p.board, sizeof(p.board), "gone");
    CHECK(GamePage_Board(&p, &sBoards) == 0, "page keys: a board that went away shows the first");

    CHECK(Read(&p, &v, Key(IK_D)).verb == GA_DAILY && Read(&p, &v, Pad(PB_Y)).verb == GA_DAILY && Read(&p, &v, Key(IK_F)).verb == GA_FAVOURITE &&
              Read(&p, &v, ShiftKey(IK_U)).verb == GA_NAME && Read(&p, &v, Key(IK_U)).verb == GA_NONE,
          "page keys: D (pad Y), F and Shift+U, as on the hall");
    CHECK(Read(&p, &v, Key(IK_E)).verb == GA_NONE && Read(&p, &v, Key(IK_DELETE)).verb == GA_NONE, "page keys: E and Delete only for admin");
    v.admin = true;
    CHECK(Read(&p, &v, Key(IK_E)).verb == GA_EDIT && Read(&p, &v, Key(IK_DELETE)).verb == GA_REMOVE, "page keys: admin's E edits, Delete asks to remove");
    static const struct { int id; GamePageVerb verb; int value; } kPress[] = {
        {WID_BACK, GA_BACK, 0}, {WID_PAGE_PLAY, GA_PLAY, 0}, {WID_PAGE_DAILY, GA_DAILY, 0}, {WID_PAGE_NAME, GA_NAME, 0},
        {WID_PAGE_FAV, GA_FAVOURITE, 0}, {WID_PAGE_EDIT, GA_EDIT, 0}, {WID_PAGE_REMOVE, GA_REMOVE, 0}, {WID_PAGE_PREV, GA_FLIP, -1},
        {WID_PAGE_NEXT, GA_FLIP, 1},
    };
    bool presses = true;
    for (size_t i = 0; i < sizeof(kPress) / sizeof(kPress[0]); i++) {
        KitEvent ev = {KIT_EV_PRESS, kPress[i].id, 0};
        Input in = Key(IK_ESCAPE); /* a key on the same frame: the button wins, one action a frame */
        a = GamePage_Read(&p, &v, &ev, &in);
        if (a.verb != kPress[i].verb || a.value != kPress[i].value) presses = false;
    }
    CHECK(presses, "page keys: each button, and a key on the same frame waits");
    v.admin = false;
    KitEvent editEv = {KIT_EV_PRESS, WID_PAGE_EDIT, 0};
    CHECK(GamePage_Read(&p, &v, &editEv, &(Input){0}).verb == GA_NONE, "page keys: no Edit without admin, even clicked");
}

static void test_page_lines(void) {
    GamePageView v = PageView();
    static GamePageData d;
    memset(&d, 0, sizeof(d));
    char out[160];
    GamePage_Help(&v, &d, WID_PAGE_PLAY, out, sizeof(out));
    bool help = strstr(out, "coin drops") != NULL;
    v.play = HP_INSTALL;
    GamePage_Help(&v, &d, WID_PAGE_PLAY, out, sizeof(out));
    help &= strstr(out, "installs it") != NULL;
    v.play = HP_PLAY;
    v.tool = true;
    GamePage_Help(&v, &d, WID_PAGE_PLAY, out, sizeof(out));
    help &= strstr(out, "Chipforge opens") != NULL;
    v.tool = false;
    GamePage_Help(&v, &d, WID_PAGE_NAME, out, sizeof(out));
    help &= strstr(out, "No name yet") != NULL;
    snprintf(d.playsAs, sizeof(d.playsAs), "Zap");
    d.ownName = true;
    GamePage_Help(&v, &d, WID_PAGE_NAME, out, sizeof(out));
    help &= strstr(out, "Plays as Zap here") != NULL;
    d.ownName = false;
    GamePage_Help(&v, &d, WID_PAGE_NAME, out, sizeof(out));
    help &= strstr(out, "your arcade name") != NULL;
    v.favourite = true;
    GamePage_Help(&v, &d, WID_PAGE_FAV, out, sizeof(out));
    help &= strstr(out, "take the star off") != NULL;
    GamePage_Help(&v, &d, WID_PAGE_TABS, out, sizeof(out));
    CHECK(help && out[0] == '\0', "page: a sentence for each button, the name it plays under; none for the rest");
    GamePage p;
    memset(&p, 0, sizeof(p));
    char keys[200], pad[200];
    GamePage_Hint(&p, &v, DEV_KEYBOARD, keys, sizeof(keys));
    GamePage_Hint(&p, &v, DEV_PAD, pad, sizeof(pad));
    CHECK(strstr(keys, "Up/Down choose") && strstr(keys, "Left/Right tab") && strstr(keys, "PgUp/PgDn machine") && strstr(keys, "Esc back") &&
              !strstr(keys, "board") && strstr(pad, "D-pad choose") && strstr(pad, "A press") && strstr(pad, "LB/RB tab") &&
              strstr(pad, "LT/RT machine") && strstr(pad, "B back"),
          "page: the hint line names the keys, or the pad's buttons");
    p.tab = GT_SCORES;
    GamePage_Hint(&p, &v, DEV_KEYBOARD, keys, sizeof(keys));
    GamePage_Hint(&p, &v, DEV_PAD, pad, sizeof(pad));
    CHECK(strstr(keys, "X board") && strstr(pad, "X board") && Kit_TextWidth(keys, 8) <= 800 && Kit_TextWidth(pad, 8) <= 800,
          "page: on Scores it names the board key too, and still fits the line");
}

/* ----------------------------------------------------------------- form */

static Manifest sForm;

static void FormCatalog(void) {
    memset(&sForm, 0, sizeof(sForm));
    Manifest_AddGame(&sForm, "Blockfall", "~/bin/blockfall", "", "Falling blocks", "~/src/blockfall");
    Manifest_AddGame(&sForm, "Coilrush", "~/bin/coilrush", "", "Snake", "");
}

/* One frame of the form: its layout, then the input. */
static GameFormResult Step(GameForm *f, KitState *k, const CarryOver *c, Input in) {
    KitList l;
    GameForm_Layout(f, c, &l);
    return GameForm_Update(f, k, &l, &in, NULL, &sForm);
}

static void test_form_keys(void) {
    FormCatalog();
    GameForm f;
    KitState k;
    Kit_Init(&k);
    GameForm_OpenAdd(&f);
    CHECK(f.editing == -1 && f.active == 0 && f.field.editing && !GameForm_Changed(&f) && GameForm_Text(&f, 4)[0] == '\0',
          "form: Add opens empty, typing in the name");
    CHECK(Step(&f, &k, NULL, Key(IK_ENTER)) == GF_NONE && f.active == 0 && f.errorField == 0 && strstr(f.error, "name first"),
          "form: Enter on an empty name is refused, as it always was");
    Step(&f, &k, NULL, Typed("Bump er"));
    CHECK(strcmp(GameForm_Text(&f, 0), "Bump er") == 0 && f.error[0] == '\0' && GameForm_Changed(&f),
          "form: letters and Space go into the field (Space presses nothing), and the error goes");
    Step(&f, &k, NULL, Key(IK_ENTER));
    CHECK(f.active == 1 && strcmp(f.text[0], "Bump er") == 0, "form: Enter goes on to the program");
    Step(&f, &k, NULL, Key(IK_ENTER));
    CHECK(f.active == 1 && strstr(f.error, "program first"), "form: an empty program is refused too");
    Step(&f, &k, NULL, Typed("~/bin/bump"));
    Step(&f, &k, NULL, Key(IK_ENTER));
    Step(&f, &k, NULL, Key(IK_ENTER));
    Step(&f, &k, NULL, Key(IK_ENTER));
    CHECK(f.active == 4, "form: the optional fields may stay empty");
    Step(&f, &k, NULL, Key(IK_ENTER));
    CHECK(f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_SAVE, "form: Enter on the last field goes to Save");
    Step(&f, &k, NULL, Key(IK_ENTER));
    CHECK(Step(&f, &k, NULL, Blank()) == GF_SAVE, "form: and Enter there saves, when it is let go");
    char v[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    GameForm_Values(&f, v);
    CHECK(strcmp(v[0], "Bump er") == 0 && strcmp(v[1], "~/bin/bump") == 0, "form: the values it saves");

    /* Tab, Shift+Tab, Up and Down. */
    GameForm_OpenAdd(&f);
    Kit_Init(&k);
    Step(&f, &k, NULL, Key(IK_TAB));
    bool moves = f.active == 1;
    Step(&f, &k, NULL, ShiftKey(IK_TAB));
    moves &= f.active == 0;
    Step(&f, &k, NULL, ShiftKey(IK_TAB));
    moves &= f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_CANCEL;
    Step(&f, &k, NULL, Key(IK_TAB));
    moves &= f.active == 0;
    CHECK(moves, "form: Tab and Shift+Tab go round the fields and the buttons");
    for (int i = 0; i < 4; i++) Step(&f, &k, NULL, Key(IK_DOWN));
    moves = f.active == 4;
    Step(&f, &k, NULL, Key(IK_DOWN));
    moves &= f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_SAVE;
    Step(&f, &k, NULL, Key(IK_DOWN));
    moves &= f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_SAVE;
    Step(&f, &k, NULL, Key(IK_RIGHT));
    moves &= k.focus == WID_FORM_CANCEL;
    Step(&f, &k, NULL, Key(IK_UP));
    moves &= f.active == 4;
    Step(&f, &k, NULL, Pad(PB_UP));
    moves &= f.active == 3;
    Step(&f, &k, NULL, Pad(PB_RB));
    moves &= f.active == 4;
    CHECK(moves, "form: Down to Save (nothing below it), Right to Cancel, Up back to the fields; the pad's d-pad and LB/RB too");
    Step(&f, &k, NULL, Key(IK_UP));
    Step(&f, &k, NULL, Typed("abc"));
    CHECK(strcmp(GameForm_Text(&f, 3), "abc") == 0 && GameForm_Look(&f, KIT_LOOK_FOCUSED) == KIT_LOOK_NORMAL,
          "form: typing goes to the field with the keys; the buttons show no focus meanwhile");
    f.active = FORM_ON_BUTTONS;
    CHECK(GameForm_Look(&f, KIT_LOOK_FOCUSED) == KIT_LOOK_FOCUSED, "form: ...and do once the keys are on them");
    GameForm_OpenAdd(&f);
    CHECK(Step(&f, &k, NULL, Key(IK_ESCAPE)) == GF_CANCEL && Step(&f, &k, NULL, Pad(PB_B)) == GF_CANCEL, "form: Esc and pad B ask to leave");

    /* The mouse: a click puts the cursor where it lands, a long field showing its end. */
    GameForm_OpenAdd(&f);
    Kit_Init(&k);
    snprintf(f.text[2], sizeof(f.text[2]), "%s", "/a/very/long/path/to/an/icon/that/does/not/fit/the/box.png");
    snprintf(f.text[3], sizeof(f.text[3]), "%s", "A description much longer than the box it is shown in at rest");
    KitRect box = GameForm_BoxRect(2);
    int view, lead;
    bool cut = GameForm_RestCut(2, f.text[2], &view, &lead);
    CHECK(cut && lead == 3 && view == (int)strlen(f.text[2]) - (GameForm_Chars() - 3) && GameForm_RestCut(3, f.text[3], &view, &lead) &&
              lead == 0 && view == 0 && !GameForm_RestCut(0, "Short", &view, &lead),
          "form: at rest a long path shows its end after ..., a long description its start");
    GameForm_RestCut(2, f.text[2], &view, &lead);
    Step(&f, &k, NULL, Mouse(box.x + FORM_PAD + 3 * FORM_CHAR_W + 1, box.y + 10, true, false));
    CHECK(f.active == 2 && f.field.cursor == view, "form: a click on a field gives it the keys, the cursor at the letter clicked");
    Step(&f, &k, NULL, Mouse(box.x + FORM_PAD + 1, box.y + 10, false, true));
    KitRect save = {FORM_X, FORM_BUTTONS_Y, FORM_BUTTON_W, FORM_BUTTON_H};
    Step(&f, &k, NULL, Mouse(save.x + 20, save.y + 10, true, false));
    GameFormResult r = Step(&f, &k, NULL, Mouse(save.x + 20, save.y + 10, false, true));
    CHECK(r == GF_NONE && f.active == 0 && strstr(f.error, "name first"), "form: Save checks it all: an empty name gets the keys and says why");
    KitRect x = {CLOSE_BTN_X, CLOSE_BTN_Y, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE};
    Step(&f, &k, NULL, Mouse(x.x + 5, x.y + 5, true, false));
    CHECK(Step(&f, &k, NULL, Mouse(x.x + 5, x.y + 5, false, true)) == GF_QUIT, "form: the X quits");
}

static void test_form_checks(void) {
    FormCatalog();
    GameForm f;
    int field;
    char why[96];
    GameForm_OpenAdd(&f);
    snprintf(f.field.text, sizeof(f.field.text), "  coilRUSH  ");
    snprintf(f.text[1], sizeof(f.text[1]), "~/bin/x");
    CHECK(!GameForm_Validate(&f, &sForm, &field, why, sizeof(why)) && field == 0 && strstr(why, "already called Coilrush"),
          "form: no two machines with one name, whatever the case and spaces");
    GameForm_OpenEdit(&f, &sForm.games[1], 1);
    CHECK(GameForm_Validate(&f, &sForm, &field, why, sizeof(why)) && !GameForm_Changed(&f) && strcmp(GameForm_Text(&f, 4), "") == 0 &&
              strcmp(GameForm_Text(&f, 3), "Snake") == 0,
          "form: Edit opens with the machine's fields; its own name is no clash");
    snprintf(f.text[3], sizeof(f.text[3]), "a|b");
    CHECK(!GameForm_Validate(&f, &sForm, &field, why, sizeof(why)) && field == 3 && strstr(why, "|"), "form: '|' is refused in any field");
    snprintf(f.text[3], sizeof(f.text[3]), "Snake");
    snprintf(f.text[1], sizeof(f.text[1]), "   ");
    CHECK(!GameForm_Validate(&f, &sForm, &field, why, sizeof(why)) && field == 1, "form: a program of spaces is no program");
    char v[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    snprintf(f.text[1], sizeof(f.text[1]), " ~/bin/coilrush ");
    GameForm_Values(&f, v);
    CHECK(strcmp(v[1], "~/bin/coilrush") == 0 && strcmp(v[0], "Coilrush") == 0, "form: what it saves has no spaces at the ends");
    CHECK(strcmp(GameForm_Prompt(0), "Name") == 0 && strcmp(GameForm_Prompt(5), "") == 0 && GameForm_Chars() >= 40,
          "form: prompts, and boxes wide enough for a path");
}

/* A rename: what stays under the old name, the switch, Tab through it. */
static void test_form_rename(void) {
    FormCatalog();
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    Stats_AddGameTime(&stats, "Blockfall", 2296);
    Profiles prof;
    memset(&prof, 0, sizeof(prof));
    Profiles_SetUsername(&prof, "Blockfall", "Arcade");
    static LauncherIni ini;
    memset(&ini, 0, sizeof(ini));
    GameForm f;
    KitState k;
    Kit_Init(&k);
    GameForm_OpenEdit(&f, &sForm.games[0], 0);
    CarryOver c;
    GameForm_WhatStays(&f, &sForm, &stats, &prof, &ini, &c);
    CHECK(!c.renamed && !Machines_CanCarry(&c), "rename: nothing stays behind while the name is the same");
    KitList l;
    GameForm_Layout(&f, &c, &l);
    CHECK(!Kit_Get(&l, WID_FORM_CARRY), "rename: no switch for nothing");
    Step(&f, &k, &c, Typed(" Two"));
    GameForm_WhatStays(&f, &sForm, &stats, &prof, &ini, &c);
    CHECK(c.renamed && c.seconds == 2296 && strcmp(c.ownName, "Arcade") == 0 && Machines_CanCarry(&c), "rename: its playtime and its own name are kept under the old one");
    GameForm_Layout(&f, &c, &l);
    const KitItem *carry = Kit_Get(&l, WID_FORM_CARRY);
    CHECK(carry && carry->kind == KIT_TOGGLE && carry->value == 1, "rename: Carry them over, on at first");
    for (int i = 0; i < 5; i++) Step(&f, &k, &c, Key(IK_TAB));
    CHECK(f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_CARRY, "rename: Tab reaches the switch after the fields");
    Step(&f, &k, &c, Key(IK_LEFT));
    CHECK(!f.carry, "rename: Left turns it off");
    Step(&f, &k, &c, Key(IK_DOWN));
    CHECK(k.focus == WID_FORM_SAVE, "rename: Down from the switch to Save");
    Step(&f, &k, &c, Key(IK_UP));
    Step(&f, &k, &c, Key(IK_UP));
    CHECK(f.active == 4, "rename: Up from the switch to the last field");
    Step(&f, &k, &c, Key(IK_ENTER));
    CHECK(f.active == FORM_ON_BUTTONS && k.focus == WID_FORM_SAVE, "rename: Enter on the last field skips the switch to Save, so it never flips by habit");
    Step(&f, &k, &c, Key(IK_RIGHT));
    bool row = k.focus == WID_FORM_CANCEL;
    Step(&f, &k, &c, Key(IK_RIGHT));
    row &= k.focus == WID_FORM_CANCEL;
    Step(&f, &k, &c, Pad(PB_RIGHT));
    row &= k.focus == WID_FORM_CANCEL;
    Step(&f, &k, &c, Key(IK_LEFT));
    row &= k.focus == WID_FORM_SAVE;
    Step(&f, &k, &c, Key(IK_LEFT));
    row &= k.focus == WID_FORM_SAVE && !f.carry;
    CHECK(row, "rename: Left and Right stay on Save and Cancel (Right from Cancel never climbs to the switch)");
    char stays[128], scores[128], moves[128];
    Machines_CarryWords(&c, "Blockfall", stays, scores, moves, sizeof(stays));
    CHECK(strcmp(stays, "Its 38m of play and its own name (Arcade) are kept under Blockfall.") == 0 && scores[0] == '\0' &&
              strcmp(moves, "Its playtime and its own name move with it.") == 0,
          "rename: the words say what stays, and what moves");
}

/* ------------------------------------------------------------- machines */

static void test_machines_status(void) {
    CHECK(strcmp(Machines_StatusWords(Machines_Status(true, false)), "Installed") == 0 &&
              strcmp(Machines_StatusWords(Machines_Status(false, false)), "Not installed") == 0 &&
              strcmp(Machines_StatusWords(Machines_Status(false, true)), "Installing") == 0 &&
              strcmp(Machines_StatusWords(Machines_Status(true, true)), "Installing") == 0,
          "machines: Installed, Not installed, Installing");
    CarryOver c;
    Machines_WhatStays(&c, "Blockfall", "Blockfall", "~/bin/blockfall", "~/bin/Block Fall", NULL, NULL, NULL);
    CHECK(!c.renamed && !c.programChanged && !Machines_CanCarry(&c), "machines: a program name that is no slug yet changes nothing");
    char stays[128], scores[128], moves[128];
    Machines_WhatStays(&c, "Blockfall", "Blockfall", "~/bin/blockfall", "/usr/games/blockfall2", NULL, NULL, NULL);
    Machines_CarryWords(&c, "Blockfall", stays, scores, moves, sizeof(stays));
    CHECK(c.programChanged && !c.rowKeys && !Machines_CanCarry(&c) && stays[0] == '\0' &&
              strcmp(scores, "Scores and trophies follow the program's file name: they stay with blockfall.") == 0 && moves[0] == '\0',
          "machines: a new program warns that scores and trophies stay with the old one");
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    Stats_AddGameTime(&stats, "Blockfall", 61);
    Machines_WhatStays(&c, "Blockfall", "Blockfall Two", "~/bin/blockfall", "~/bin/blockfall", &stats, NULL, NULL);
    Machines_CarryWords(&c, "Blockfall", stays, scores, moves, sizeof(stays));
    CHECK(strcmp(stays, "Its 1m of play is kept under Blockfall.") == 0 && strcmp(moves, "Its playtime moves with it.") == 0,
          "machines: playtime alone");
    Profiles prof;
    memset(&prof, 0, sizeof(prof));
    Profiles_SetUsername(&prof, "Blockfall", "Zed");
    Machines_WhatStays(&c, "Blockfall", "Blockfall Two", "~/bin/blockfall", "~/bin/blockfall", NULL, &prof, NULL);
    Machines_CarryWords(&c, "Blockfall", stays, scores, moves, sizeof(stays));
    CHECK(strcmp(stays, "Its own name (Zed) is kept under Blockfall.") == 0, "machines: a name alone");
}

static void test_machines_carry(void) {
    WriteUnder("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini",
               "hall_sounds=4\nfav.blockfall=1\nplace.blockfall=2\nplayed.blockfall=1700000000\nlast_played=blockfall\nfav.coilrush=1\n");
    static LauncherIni ini;
    LauncherIni_Load(&ini);
    CHECK(Row_HasKeys(&ini, "blockfall") && Row_HasKeys(&ini, "coilrush") && !Row_HasKeys(&ini, "skyraid") && Row_Played(&ini, "blockfall") == 1700000000LL &&
              Row_Played(&ini, "coilrush") == 0,
          "carry: the row's keys under a program name, and when it was last started");
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    Stats_AddGameTime(&stats, "Blockfall", 100);
    Stats_AddGameTime(&stats, "Blockfall Two", 5);
    Profiles prof;
    memset(&prof, 0, sizeof(prof));
    Profiles_SetUsername(&prof, PROFILE_DEFAULT_KEY, "TRG");
    Profiles_SetUsername(&prof, "Blockfall", "Zed");
    Profiles_SetUsername(&prof, "Blockfall Two", "Old");
    CarryOver c;
    Machines_WhatStays(&c, "Blockfall", "Blockfall Two", "~/bin/blockfall", "~/bin/blockfall2", &stats, &prof, &ini);
    CHECK(c.renamed && c.programChanged && c.rowKeys && Machines_CanCarry(&c), "carry: a new name and a new program, with something under both");
    char stays[128], scores[128], moves[128];
    Machines_CarryWords(&c, "Blockfall", stays, scores, moves, sizeof(stays));
    CHECK(strcmp(moves, "Its playtime, its own name and your row settings move with it.") == 0, "carry: the words list all three");
    CHECK(Machines_Carry(&c, "Blockfall", "Blockfall Two", &stats, &prof, &ini), "carry: done, every file saved");
    CHECK(Stats_GetGameTime(&stats, "Blockfall") == 0.0 && Stats_GetGameTime(&stats, "Blockfall Two") == 105.0 &&
              FileHas("XDG_DATA_HOME", "ghost-launcher/playtime.txt", "Blockfall Two|105.0") &&
              !FileHas("XDG_DATA_HOME", "ghost-launcher/playtime.txt", "Blockfall|"),
          "carry: the playtime adds to the new name's, in playtime.txt too");
    CHECK(strcmp(Profiles_GetOwnUsername(&prof, "Blockfall Two"), "Zed") == 0 && Profiles_GetOwnUsername(&prof, "Blockfall")[0] == '\0' &&
              strcmp(Profiles_GetOwnUsername(&prof, PROFILE_DEFAULT_KEY), "TRG") == 0 && FileHas("XDG_DATA_HOME", "ghost-launcher/profiles.txt", "Blockfall Two|Zed") &&
              !FileHas("XDG_DATA_HOME", "ghost-launcher/profiles.txt", "Blockfall|"),
          "carry: the game's own name moves, replacing what the new name had");
    CHECK(FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "fav.blockfall2=1") && FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "place.blockfall2=2") &&
              FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "played.blockfall2=1700000000") &&
              FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "last_played=blockfall2") &&
              !FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "fav.blockfall=") &&
              FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "fav.coilrush=1") && FileHas("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini", "hall_sounds=4"),
          "carry: the star, place and last start move to the new program; other lines stay");
    CHECK(!Profiles_Rename(&prof, "Nobody", "X") && !Profiles_Rename(&prof, "X", "X") && Row_CarryKeys(&ini, "skyraid", "skyraid2") &&
              !Row_CarryKeys(&ini, "skyraid", "skyraid"),
          "carry: nothing to move is no move");

    /* playtime.txt cannot be saved: false, and memory is what the disk has. */
    const char *data = getenv("XDG_DATA_HOME");
    char path[700], moved[720];
    snprintf(path, sizeof(path), "%s/ghost-launcher/playtime.txt", data);
    snprintf(moved, sizeof(moved), "%s.away", path);
    rename(path, moved);
    mkdir(path, 0755);
    Stats_AddGameTime(&stats, "Coilrush", 30);
    Machines_WhatStays(&c, "Coilrush", "Coil", "~/bin/coilrush", "~/bin/coilrush", &stats, NULL, NULL);
    CHECK(!Machines_Carry(&c, "Coilrush", "Coil", &stats, NULL, NULL) && Stats_GetGameTime(&stats, "Coil") == 0.0,
          "carry: a save that failed says so, and memory follows the disk");
    rmdir(path);
    rename(moved, path);
    RemoveUnder("XDG_CONFIG_HOME", "ghost-launcher/launcher.ini");
}

/* ---------------------------------------------------------------- keys */

static void test_page_keymap(void) {
    char keys[64], pad[64];
    Keymap_Names(KA_PAGE_BOARD, KB_KEY, keys, sizeof(keys));
    Keymap_Names(KA_PAGE_BOARD, KB_PAD, pad, sizeof(pad));
    bool ok = strcmp(keys, "X") == 0 && strcmp(pad, "X") == 0;
    Keymap_Names(KA_PAGE_PREV, KB_KEY, keys, sizeof(keys));
    Keymap_Names(KA_PAGE_PREV, KB_PAD, pad, sizeof(pad));
    ok &= strcmp(keys, "PgUp") == 0 && strcmp(pad, "LT") == 0;
    Keymap_Names(KA_PAGE_CLOSE, KB_KEY, keys, sizeof(keys));
    ok &= strcmp(keys, "I") == 0;
    CHECK(ok && Keymap_OnCard(KA_PAGE_BOARD) && Keymap_OnCard(KA_PAGE_PREV) && Keymap_OnCard(KA_PAGE_CLOSE),
          "keys: the page's X, PgUp/LT, PgDn/RT and I, on the page's card");
    bool card = false;
    for (int c = 0; c < Keymap_CardCount(); c++) {
        if (strcmp(Keymap_CardTitle(c), "This game's page") != 0) continue;
        KeymapLine lines[KEYMAP_CARD_LINES];
        int all = Keymap_CardLines(c, true, lines, KEYMAP_CARD_LINES), player = Keymap_CardLines(c, false, lines, KEYMAP_CARD_LINES);
        card = all == player + 2 && c < Keymap_CardCountFor(false);
    }
    CHECK(card, "keys: the page's card is everyone's; its Edit and Remove lines only admin's");
}

void test_page_all(void) {
    test_blockfall_times();
    test_boards_assembly();
    test_boards_files();
    test_page_words();
    test_page_data();
    test_page_layout();
    test_page_read();
    test_page_lines();
    test_form_keys();
    test_form_checks();
    test_form_rename();
    test_machines_status();
    test_machines_carry();
    test_page_keymap();
}
