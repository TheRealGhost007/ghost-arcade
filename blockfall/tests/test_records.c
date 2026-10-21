/* What a run records and the files behind it: which runs count, save.dat's
 * classic best, Sprint ranked by time, custom sizes kept on this machine, the
 * shared table's row cap, the settings file, and auto-repeat. Every file here
 * lives in the temp HOME test_main.c makes before anything runs. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "../src/game.h"
#include "../src/runrecord.h"
#include "../src/localtable.h"
#include "../src/persist.h"
#include "../src/repeat.h"
#include "../src/controls.h"
#include "ghostlink.h"

static Game Played(GameMode mode, int w, int h, long score, int pieces) {
    Game g;
    Game_InitConfig(&g, (GameConfig){mode, w, h, true}, 1, 0, 1, 0);
    g.score = score;
    g.piecesLocked = pieces;
    return g;
}

static bool SlugLike(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 24) return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return false;
    return true;
}

static void test_classify(void) {
    Game g = Played(MODE_MARATHON, 10, 20, 0, 0);
    RunRecord r = Record_Classify(&g, false);
    CHECK(!r.counts && !r.toBoard && !r.toSave, "run: an R tap before anything landed is not a run");
    g = Played(MODE_MARATHON, 10, 20, 0, 6);
    CHECK(!Record_Classify(&g, false).counts, "run: pieces that scored nothing are not a run");
    g = Played(MODE_MARATHON, 10, 20, 40, 0);
    CHECK(!Record_Classify(&g, false).counts, "run: soft-drop points with nothing locked are not a run");

    g = Played(MODE_MARATHON, 10, 20, 12000, 40);
    r = Record_Classify(&g, false);
    CHECK(r.counts && r.toBoard && !r.toSave && !r.local && r.unit == UNIT_POINTS && r.value == 12000 && strcmp(r.key, "marathon-10x20") == 0,
          "run: a classic Marathon counts and goes on the shared marathon-10x20 board (3.2.0: its fresh board under modern rules), never into save.dat");
    r = Record_Classify(&g, true);
    CHECK(!r.counts && !r.toBoard && !r.toSave, "run: a tuned (F2) playtest records nothing, save.dat included");

    static const GameMode kModes[] = {MODE_SPRINT, MODE_ULTRA, MODE_ZEN, MODE_POWER};
    for (int i = 0; i < 4; i++) {
        g = Played(kModes[i], 10, 20, 99999, 300);
        g.won = true;
        g.elapsed = 61.0f;
        CHECK(!Record_Classify(&g, false).toSave, "run: no other mode on the classic board touches save.dat");
    }
    g = Played(MODE_MARATHON, 18, 30, 99999, 300);
    CHECK(!Record_Classify(&g, false).toSave, "run: nor does a Marathon on another board");

    g = Played(MODE_ZEN, 10, 20, 5000, 30);
    r = Record_Classify(&g, false);
    CHECK(r.counts && !r.toBoard, "run: Zen is logged as a run but keeps no score");

    g = Played(MODE_SPRINT, 10, 20, 24212, 90);
    g.linesCleared = 36;
    r = Record_Classify(&g, false);
    CHECK(r.counts && !r.toBoard, "sprint: an unfinished Sprint is a run, but never goes on the board");
    g.won = true;
    g.linesCleared = 40;
    g.elapsed = 72.4f;
    r = Record_Classify(&g, false);
    CHECK(r.toBoard && r.unit == UNIT_TIME && strcmp(r.key, "sprint40") == 0 && r.value == RECORD_TIME_BASE - 7240,
          "sprint: a finished Sprint goes on Sprint 40 with its time");

    g = Played(MODE_POWER, 14, 20, 5000, 30);
    r = Record_Classify(&g, false);
    CHECK(r.toBoard && !r.local && strcmp(r.key, "power-14x20") == 0, "run: a preset board goes on the shared table");
    g = Played(MODE_POWER, 12, 22, 5000, 30);
    r = Record_Classify(&g, false);
    CHECK(r.toBoard && r.local && strcmp(r.key, "power-12x22") == 0, "run: a custom size stays on this machine");
}

static void test_sprint_time(void) {
    /* The bug: points decided the Sprint table. Time decides it now. */
    long fast = Record_SprintValue(70.0f), slow = Record_SprintValue(150.0f);
    CHECK(fast > slow && Record_Beats(fast, slow) && !Record_Beats(slow, fast), "sprint: the faster run ranks higher");
    CHECK(Record_SprintValue(72.4f) == 9992760L, "sprint: 1:12.40 is stored as 9 992 760");
    CHECK(Record_ValueCentis(9992760L) == 7240, "sprint: and reads back as 7240 hundredths");
    CHECK(Record_SprintValue(0.0f) <= 100000000L && Record_SprintValue(1e9f) == 1, "sprint: values stay inside what the world board accepts");
    char buf[32];
    Record_FormatTime(7240, buf, sizeof(buf));
    CHECK(strcmp(buf, "1:12.40") == 0, "sprint: times show as m:ss.cc");
    Record_FormatTime(5821, buf, sizeof(buf));
    CHECK(strcmp(buf, "0:58.21") == 0, "sprint: under a minute too");
    Record_FormatValue(UNIT_TIME, Record_SprintValue(83.05f), buf, sizeof(buf));
    CHECK(strcmp(buf, "1:23.05") == 0, "sprint: a board value shows as its time");
    Record_FormatValue(UNIT_POINTS, 74372, buf, sizeof(buf));
    CHECK(strcmp(buf, "74372") == 0, "points show as points");
    Record_FormatGap(UNIT_TIME, Record_SprintValue(70.0f), Record_SprintValue(71.2f), buf, sizeof(buf));
    CHECK(strcmp(buf, "1.20 s") == 0, "sprint: a gap between times reads in seconds");
    Record_FormatGap(UNIT_POINTS, 30000, 14430, buf, sizeof(buf));
    CHECK(strcmp(buf, "15570") == 0, "a gap between scores reads in points");
}

static void test_keys(void) {
    char key[GHOSTLINK_MODE_LEN];
    static const struct { GameMode m; int w, h; const char *want; } kKeys[] = {
        {MODE_MARATHON, 10, 20, "marathon-10x20"}, {MODE_SPRINT, 10, 20, "sprint40"}, {MODE_ULTRA, 10, 20, "ultra"},
        {MODE_POWER, 14, 20, "power-14x20"}, {MODE_SPRINT, 24, 36, "sprint40-24x36"}, {MODE_ZEN, 6, 12, "zen-6x12"},
    };
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
        Record_BoardKey(kKeys[i].m, kKeys[i].w, kKeys[i].h, key, sizeof(key));
        CHECK(strcmp(key, kKeys[i].want) == 0, "keys: each mode and board has its own key");
    }
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++)
            for (int m = 0; m < MODE_COUNT; m++) {
                Record_BoardKey((GameMode)m, w, h, key, sizeof(key));
                if (!SlugLike(key)) CHECK(false, "keys: every key is one the world board accepts");
            }
    Record_OldSprintKey(10, 20, key, sizeof(key));
    CHECK(strcmp(key, "sprint") == 0, "keys: the old Sprint board keeps its key");
    Record_OldSprintKey(14, 20, key, sizeof(key));
    CHECK(strcmp(key, "sprint-14x20") == 0, "keys: on every board");
    Record_OldClassicKey(key, sizeof(key));
    CHECK(strcmp(key, "marathon") == 0, "keys: the classic Marathon's old-rules board keeps the bare key (3.2.0)");
    bool never = true;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++)
            for (int m = 0; m < MODE_COUNT; m++) {
                char k2[GHOSTLINK_MODE_LEN];
                Record_BoardKey((GameMode)m, w, h, k2, sizeof(k2));
                if (strcmp(k2, "marathon") == 0) never = false;
            }
    CHECK(never, "keys: no run on any board is ever written to the old-rules marathon key again");
    CHECK(Record_HasOldBoard(MODE_MARATHON, 10, 20) && Record_HasOldBoard(MODE_SPRINT, 14, 20) && !Record_HasOldBoard(MODE_MARATHON, 14, 20) &&
              !Record_HasOldBoard(MODE_ULTRA, 10, 20),
          "keys: old boards: the classic Marathon's and every Sprint's");
    Record_OldKey(MODE_MARATHON, 10, 20, key, sizeof(key));
    CHECK(strcmp(key, "marathon") == 0, "keys: Record_OldKey gives the Marathon's");
    Record_OldKey(MODE_SPRINT, 14, 20, key, sizeof(key));
    CHECK(strcmp(key, "sprint-14x20") == 0, "keys: and Sprint's");
    Record_LogKey(MODE_SPRINT, 10, 20, key, sizeof(key));
    CHECK(strcmp(key, "sprint") == 0, "keys: runs.log keeps the key it always had");
    CHECK(Record_PresetOf(10, 20) == 0 && Record_PresetOf(6, 14) == 4 && Record_PresetOf(12, 22) == -1, "keys: presets are found by size");
    CHECK(strcmp(Record_Preset(BOARD_PRESET_CUSTOM)->name, "Custom") == 0, "keys: the last preset is Custom");
}

static void test_bests_and_sounds(void) {
    CHECK(Record_ClassicBest(74372, 12248) == 74372 && Record_ClassicBest(5000, 20000) == 20000, "best: the classic board's Best is the higher of save.dat and the table");
    CHECK(!Record_Beats(13000, 74372), "best: 13 000 is not a new best when the Best is 74 372");
    CHECK(Record_Beats(1, 0) && !Record_Beats(0, 0), "best: any score beats no best yet, nothing beats it with 0");
    CHECK(Record_EndSound(false, false) == END_SOUND_GAME_OVER, "sound: a top-out ends on the game-over sting");
    CHECK(Record_EndSound(true, false) == END_SOUND_CLEARED, "sound: a finished Sprint or Ultra gets the cleared sting, not game over");
    CHECK(Record_EndSound(true, true) == END_SOUND_NEW_BEST && Record_EndSound(false, true) == END_SOUND_NEW_BEST, "sound: a new best gets the new-best jingle, alone");
}

/* ----------------------------------------------------------------- files */

static bool ReadFile(const char *path, char *out, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void WriteFile(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void SharedFile(char *out, size_t size) {
    snprintf(out, size, "%s/ghost-launcher/scores/blockfall.txt", getenv("XDG_DATA_HOME"));
}

static void MakeDirs(const char *dataHome) {
    char p[700];
    snprintf(p, sizeof(p), "%s/ghost-launcher", dataHome);
    mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/ghost-launcher/scores", dataHome);
    mkdir(p, 0755);
}

static void test_local_table(void) {
    char path[700];
    CHECK(Persist_DataFile(LOCALTABLE_FILE, path, sizeof(path)) && strstr(path, getenv("XDG_DATA_HOME")) == path, "local: the table lives in Blockfall's data folder");
    remove(path);
    CHECK(LocalTable_CountRows(path) == 0, "local: missing means empty");
    int rank = 0;
    for (int i = 1; i <= 12; i++) rank = LocalTable_Submit(path, "marathon-12x22", "TRG", (long)i * 100, "2026-10-14");
    CHECK(rank == 1, "local: the best run so far ranks first");
    CHECK(LocalTable_Submit(path, "marathon-12x22", "TRG", 50, "2026-10-14") == 0, "local: a run below the top ten does not rank");
    GhostScore rows[GHOSTLINK_TOP_N];
    int n = LocalTable_Load(path, "marathon-12x22", rows, GHOSTLINK_TOP_N);
    CHECK(n == GHOSTLINK_TOP_N && rows[0].score == 1200 && rows[9].score == 300, "local: the top ten are kept, best first");
    CHECK(LocalTable_Submit(path, "power-7x30", "TRG", 777, "2026-10-14") == 1 && LocalTable_Load(path, "marathon-12x22", rows, 20) == 10,
          "local: another board does not disturb this one");
    CHECK(LocalTable_Submit(path, "power-7x30", "TRG", 777, "2026-10-14") == 2, "local: an equal score ranks under the earlier one");

    /* A line it does not understand is kept, not dropped. */
    FILE *f = fopen(path, "a");
    fputs("this is not a row\n", f);
    fclose(f);
    LocalTable_Submit(path, "power-7x30", "TRG", 900, "2026-10-14");
    char text[16384];
    CHECK(ReadFile(path, text, sizeof(text)) && strstr(text, "this is not a row") != NULL, "local: lines it cannot read survive a rewrite");

    /* A file longer than it will read is left alone rather than rewritten from part of it. */
    char big[720];
    snprintf(big, sizeof(big), "%.700s.big", path);
    f = fopen(big, "w");
    for (int i = 0; i < LOCALTABLE_MAX_ROWS + 5; i++) fprintf(f, "m%d|TRG|%d|2026-10-14\n", i, i + 1);
    fclose(f);
    struct stat before, after;
    stat(big, &before);
    CHECK(LocalTable_Submit(big, "m1", "TRG", 99999, "2026-10-14") == 0, "local: a file too long to read in full is not touched");
    stat(big, &after);
    CHECK(before.st_size == after.st_size && LocalTable_CountRows(big) == LOCALTABLE_MAX_ROWS + 5, "local: every row is still there");
    remove(big);
    remove(path);
}

static void test_shared_and_bests(void) {
    const char *data = getenv("XDG_DATA_HOME");
    MakeDirs(data);
    char shared[800], local[700], save[700];
    SharedFile(shared, sizeof(shared));
    Persist_DataFile(LOCALTABLE_FILE, local, sizeof(local));
    Persist_DataFile("save.dat", save, sizeof(save));
    remove(local);

    /* The owner's state today: save.dat 74 372, the table's classic top 12 248. */
    WriteFile(shared, "# Ghost Arcade scores for blockfall: mode|username|score|date\n"
                      "marathon|TRG|12248|2026-09-19\nmarathon-14x20|TRG|9000|2026-09-20\nsprint|TRG|7620|2026-09-20\n");
    WriteFile(save, "high_score=74372\nhigh_level=11\nhigh_lines=101\n");
    SaveData sd;
    CHECK(SaveData_Load(&sd) && sd.highScore == 74372 && sd.highLevel == 11 && sd.highLines == 101, "save.dat: reads the classic best as it is");
    CHECK(Record_LoadBest(MODE_MARATHON, 10, 20, sd.highScore) == 0,
          "best: the classic Marathon's board is fresh under modern rules (3.2.0): neither save.dat's 74 372 nor the old marathon rows are its best");
    GhostScore oldTop;
    CHECK(Record_LoadBoard("marathon", false, &oldTop, 1) == 1 && oldTop.score == 12248 && Record_ClassicBest(sd.highScore, oldTop.score) == 74372,
          "best: the old-rules board still reads its rows, and the Classic best to show there is save.dat's 74 372");
    CHECK(Record_LoadBest(MODE_MARATHON, 14, 20, sd.highScore) == 9000, "best: other boards use their own table, not save.dat");
    CHECK(Record_LoadBest(MODE_SPRINT, 10, 20, sd.highScore) == 0, "best: Sprint 40 starts fresh (the old points rows are not times)");
    CHECK(Record_LoadBest(MODE_ZEN, 10, 20, sd.highScore) == 0, "best: Zen has none");

    /* A custom size goes to this machine's table, and its Best comes from there. */
    Game g = Played(MODE_ULTRA, 12, 22, 4321, 20);
    RunRecord r = Record_Classify(&g, false);
    bool wentShared = true;
    int rank = Record_Submit(&r, "TRG", "2026-10-14", &wentShared);
    char text[65536];
    CHECK(rank == 1 && !wentShared, "custom: the run lands on this machine's own table");
    CHECK(ReadFile(shared, text, sizeof(text)) && !strstr(text, "ultra-12x22"), "custom: and never on the shared table ghost-sync uploads");
    CHECK(Record_LoadBest(MODE_ULTRA, 12, 22, 0) == 4321, "custom: its Best is read back from there");

    /* A finished Sprint goes on the shared Sprint 40 board as a time. */
    g = Played(MODE_SPRINT, 10, 20, 6100, 100);
    g.won = true;
    g.elapsed = 72.4f;
    r = Record_Classify(&g, false);
    rank = Record_Submit(&r, "TRG", "2026-10-14", &wentShared);
    CHECK(rank == 1 && wentShared && ReadFile(shared, text, sizeof(text)) && strstr(text, "sprint40|TRG|9992760|"), "sprint: the time goes on the shared Sprint 40 board");
    CHECK(strstr(text, "sprint|TRG|7620|") != NULL, "sprint: the old points rows stay where they were");
    CHECK(Record_LoadBest(MODE_SPRINT, 10, 20, 0) == 9992760L, "sprint: and the best time is read back");

    /* The Scores screen's view of a board merges both tables. */
    WriteFile(local, "marathon-14x20|TRG|9500|2026-10-14\n");
    GhostScore rows[GHOSTLINK_TOP_N];
    int n = Record_LoadBoard("marathon-14x20", false, rows, GHOSTLINK_TOP_N);
    CHECK(n == 2 && rows[0].score == 9500 && rows[1].score == 9000, "board: this machine's rows from both tables, best first");

    /* "Your best" is the player's own (Play setup, the menu's Your bests):
     * the board's best can be another profile's. */
    WriteFile(shared, "# Ghost Arcade scores for blockfall: mode|username|score|date\n"
                      "marathon-10x20|Arcade|30000|2026-10-10\nmarathon-10x20|TRG|24680|2026-10-11\nmarathon-10x20|TRG|900|2026-10-12\n"
                      "marathon|TRG|70000|2026-09-01\nultra|Arcade|5000|2026-10-10\n");
    int place = -1;
    CHECK(Record_LoadYourBest(MODE_MARATHON, 14, 20, "TRG", 0, &place) == 9500 && place == 1, "yours: your row on a board, and its place");
    CHECK(Record_LoadYourBest(MODE_ULTRA, 10, 20, "TRG", 0, &place) == 0 && place == 0, "yours: another player's row is not yours");
    CHECK(Record_LoadBest(MODE_ULTRA, 10, 20, 0) == 5000, "yours: ...although it is that board's best");
    CHECK(Record_LoadYourBest(MODE_MARATHON, 10, 20, "TRG", 0, &place) == 24680 && place == 2, "yours: second behind another player");
    CHECK(Record_LoadYourBest(MODE_MARATHON, 10, 20, "TRG", 74372, &place) == 24680 && place == 2,
          "yours: save.dat's classic best is the old rules' and never counts on the fresh board (3.2.0), nor does an old marathon row");
    CHECK(Record_LoadBest(MODE_MARATHON, 10, 20, 74372) == 30000, "yours: the fresh board's best is its own top row");
    CHECK(Record_LoadYourBest(MODE_MARATHON, 10, 20, "Nobody", 0, &place) == 0 && place == 0, "yours: no row: none");
    CHECK(Record_LoadYourBest(MODE_ZEN, 10, 20, "TRG", 74372, &place) == 0 && place == 0, "yours: Zen keeps none");

    /* The shared table at ghostlink's row cap: Blockfall does not add to it
     * (ghostlink would rewrite it from the first 512 rows and lose the rest),
     * the run is kept on this machine instead. */
    FILE *f = fopen(shared, "w");
    for (int i = 0; i < GHOSTLINK_MAX_ENTRIES; i++) fprintf(f, "power-%dx%d|TRG|%d|2026-10-14\n", 6 + i % 19, 12 + i / 19, i + 1);
    fclose(f);
    CHECK(Record_SharedRows() == GHOSTLINK_MAX_ENTRIES && !Record_SharedHasRoom(), "cap: a full shared table is seen as full");
    g = Played(MODE_MARATHON, 10, 20, 500, 5);
    r = Record_Classify(&g, false);
    struct stat before, after;
    stat(shared, &before);
    rank = Record_Submit(&r, "TRG", "2026-10-14", &wentShared);
    stat(shared, &after);
    CHECK(!wentShared && rank >= 1 && before.st_size == after.st_size && Record_SharedRows() == GHOSTLINK_MAX_ENTRIES,
          "cap: nothing on the full shared table is lost, and the run is still kept");
    remove(shared);
    remove(local);
    remove(save);
}

static void test_settings_file(void) {
    char path[800];
    snprintf(path, sizeof(path), "%s/blockfall", getenv("XDG_CONFIG_HOME"));
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/blockfall/config.ini", getenv("XDG_CONFIG_HOME"));
    remove(path);

    Settings s, back;
    CHECK(!Settings_Load(&s), "settings: no file, no load");
    CHECK(strcmp(s.left, "LEFT") == 0 && strcmp(s.hardDrop, "SPACE") == 0 && strcmp(s.quit, "ESCAPE") == 0 && s.volumePercent == 70 && s.audioEnabled,
          "settings: the defaults");

    /* The owner's config.ini as 2.0.0 writes it loads unchanged. */
    WriteFile(path, "# blockfall settings\n# Key names: A-Z, 0-9, LEFT, RIGHT, UP, DOWN, SPACE, ESCAPE, ENTER,\n"
                    "left=LEFT\nleft2=A\nright=RIGHT\nright2=D\nsoft_drop=DOWN\nsoft_drop2=S\nrotate=UP\nrotate2=W\n"
                    "hard_drop=SPACE\nhold=C\npause=P\nrestart=R\nquit=ESCAPE\nvolume=50\naudio_enabled=1\n"
                    "setup_mode=3\nsetup_preset=1\nsetup_width=10\nsetup_height=20\n");
    CHECK(Settings_Load(&s), "settings: the file is found");
    CHECK(s.volumePercent == 50 && s.setupMode == 3 && s.setupPreset == 1 && s.setupWidth == 10 && s.setupHeight == 20 && strcmp(s.rotate2, "W") == 0,
          "settings: a 2.0.0 config.ini loads as it was");

    /* Saved and read back: the same. */
    snprintf(s.hardDrop, KEYNAME_LEN, "X");
    s.volumePercent = 30;
    s.audioEnabled = false;
    s.setupMode = 4;
    s.setupPreset = 5;
    s.setupWidth = 17;
    s.setupHeight = 33;
    CHECK(Settings_Save(&s) && Settings_Load(&back), "settings: saved");
    CHECK(memcmp(&s, &back, sizeof(s)) == 0, "settings: and read back exactly");

    /* Hand-edited junk: bad lines are skipped, numbers are clamped, the rest keep their defaults. */
    WriteFile(path, "garbage line\n=\nvolume=900\nsetup_width=2\nsetup_height=99\nsetup_mode=-4\nunknown=7\nhold=Z\n");
    CHECK(Settings_Load(&s), "settings: a messy file still loads");
    CHECK(s.volumePercent == 100 && s.setupWidth == 6 && s.setupHeight == 36 && s.setupMode == 0 && strcmp(s.hold, "Z") == 0 && strcmp(s.left, "LEFT") == 0,
          "settings: clamped, skipped, defaulted");
    remove(path);

    /* save.dat round trip, in the format every build reads. */
    SaveData d = {74372, 11, 101}, e;
    CHECK(SaveData_Save(&d) && SaveData_Load(&e) && e.highScore == 74372 && e.highLevel == 11 && e.highLines == 101, "save.dat: written and read back");
    char save[700], text[256];
    Persist_DataFile("save.dat", save, sizeof(save));
    CHECK(ReadFile(save, text, sizeof(text)) && strcmp(text, "high_score=74372\nhigh_level=11\nhigh_lines=101\n") == 0, "save.dat: the same three lines as ever");
    remove(save);
}

static void test_repeat(void) {
    const float dt = 1.0f / 60.0f;
    RepeatButton b = {0};
    CHECK(!RepeatButton_Update(&b, false, dt, 0.17f, 0.035f), "repeat: nothing while up");
    CHECK(RepeatButton_Update(&b, true, dt, 0.17f, 0.035f), "repeat: fires at once on the press");
    int fires = 0, frames = 0;
    while (frames < 60 && !RepeatButton_Update(&b, true, dt, 0.17f, 0.035f)) frames++;
    CHECK(frames + 1 >= 10 && frames + 1 <= 11, "repeat: then waits 0.17 s before repeating");
    for (int i = 0; i < 60; i++) if (RepeatButton_Update(&b, true, dt, 0.17f, 0.035f)) fires++;
    CHECK(fires >= 28 && fires <= 30, "repeat: then about every 0.035 s, timed in seconds rather than frames");
    CHECK(!RepeatButton_Update(&b, false, dt, 0.17f, 0.035f) && RepeatButton_Update(&b, true, dt, 0.17f, 0.035f), "repeat: a release and a new press fires at once again");

    /* Soft drop: no delay, a step every 0.03 s. */
    RepeatButton s = {0};
    fires = 0;
    for (int i = 0; i < 60; i++) if (RepeatButton_Update(&s, true, dt, 0.0f, 0.03f)) fires++;
    CHECK(fires >= 32 && fires <= 34, "repeat: soft drop steps about every 0.03 s");

    /* A long frame (a stall) fires once in that frame, and the timing carries on from the time left over. */
    RepeatButton k = {0};
    RepeatButton_Update(&k, true, dt, 0.17f, 0.035f);
    CHECK(RepeatButton_Update(&k, true, 0.5f, 0.17f, 0.035f), "repeat: a half-second frame passes the delay");
    CHECK(RepeatButton_Update(&k, true, dt, 0.17f, 0.035f), "repeat: and the leftover time keeps it repeating");
}

static void test_controls(void) {
    Settings s;
    Settings_Default(&s);
    CHECK(Controls_AllDifferent(&s), "controls: the defaults share no key");
    ControlWords w;
    Controls_Words(&s, &w);
    CHECK(strcmp(w.move, "Left Right") == 0 && strcmp(w.softDrop, "Down") == 0 && strcmp(w.rotate, "Up") == 0 && strcmp(w.hardDrop, "Space") == 0 &&
          strcmp(w.hold, "C") == 0 && strcmp(w.restart, "R") == 0 && strcmp(w.back, "Esc") == 0,
          "controls: with the default keys the card reads as it always did");
    CHECK(strcmp(w.rotateLeft, "Z") == 0 && strcmp(w.pause, "P Esc") == 0, "controls: Rotate left is Z, and Pause shows both its keys");

    /* A clash swaps the two actions (bug 18: two actions could share a key). */
    BindResult r = Controls_BindKey(&s, ACT_ROTATE, 0, "SPACE");
    CHECK(r.kind == BIND_SWAPPED && r.otherAction == ACT_HARD && r.otherSlot == 0, "controls: Space for Rotate takes it from Hard drop");
    CHECK(strcmp(s.rotate, "SPACE") == 0 && strcmp(s.hardDrop, "UP") == 0 && Controls_AllDifferent(&s), "controls: which gets Rotate's old key");
    CHECK(strcmp(r.note, "Space moved from Hard drop to Rotate right. Hard drop took Up.") == 0, "controls: and the note says so");
    r = Controls_BindKey(&s, ACT_ROTATE, 0, "SPACE");
    CHECK(r.kind == BIND_SAME && strcmp(s.rotate, "SPACE") == 0, "controls: binding a key a row already has changes nothing");
    r = Controls_BindKey(&s, ACT_LEFT, 1, "LEFT");
    CHECK(r.kind == BIND_SWAPPED && r.otherAction == ACT_LEFT && strcmp(s.left, "A") == 0 && strcmp(s.left2, "LEFT") == 0, "controls: an action's two keys swap too");
    r = Controls_BindKey(&s, ACT_HARD, 0, "X");
    CHECK(r.kind == BIND_SET && strcmp(s.hardDrop, "X") == 0, "controls: a free key just binds");

    /* The card shows your keys after a rebind (bug 23). */
    Controls_BindKey(&s, ACT_PAUSE, 1, "Q");
    Controls_BindKey(&s, ACT_PAUSE, 0, "LEFT_SHIFT");
    Controls_Words(&s, &w);
    CHECK(strcmp(w.rotate, "Space") == 0 && strcmp(w.hardDrop, "X") == 0 && strcmp(w.pause, "LShift Esc") == 0 && strcmp(w.pauseName, "Left Shift") == 0,
          "controls: the card and the hints name the keys you set");
    CHECK(strcmp(w.back, "Esc Q") == 0 && strcmp(w.backName, "Esc") == 0, "controls: Esc always works as Back, and your Back key too");

    /* Pause's second key to P swaps Pause's two keys: then Esc is the pause
     * key and P is what goes back (as 2.1.0's Back to P did). */
    Settings_Default(&s);
    r = Controls_BindKey(&s, ACT_PAUSE, 1, "P");
    CHECK(r.kind == BIND_SWAPPED && strcmp(s.pause, "ESCAPE") == 0 && strcmp(s.quit, "P") == 0, "controls: Pause's second key to P swaps its two keys");
    Controls_Words(&s, &w);
    CHECK(strcmp(w.pauseName, "Esc") == 0 && strcmp(w.backName, "P") == 0 && strcmp(w.back, "P") == 0, "controls: then the hints say Esc carries on and P leaves");
    CHECK(Controls_EscTaken(&s), "controls: Esc on Pause's first key is Esc taken");

    /* A swap never hands Esc to another action (3.0.0: Esc can never be
     * lost): Hard drop gets none instead, or, when Space is its only key,
     * nothing changes. A config.ini from before that gave Esc to Hard drop
     * still does what it did. */
    Settings_Default(&s);
    CHECK(!Controls_EscTaken(&s), "controls: with the defaults Esc is Back's");
    r = Controls_BindKey(&s, ACT_PAUSE, 1, "C");
    CHECK(r.kind == BIND_REFUSED && strcmp(s.hold, "C") == 0 && strcmp(s.quit, "ESCAPE") == 0 && !Controls_EscTaken(&s),
          "controls: Pause's Esc to C is refused while C is Hold's only key");
    CHECK(strcmp(r.note, "C is the only key Hold has, so it stays there.") == 0, "controls: and the note says why");
    Controls_BindKey(&s, ACT_HOLD, 1, "X");
    r = Controls_BindKey(&s, ACT_PAUSE, 1, "C");
    CHECK(r.kind == BIND_SWAPPED && r.withheld && strcmp(s.quit, "C") == 0 && strcmp(s.hold, "X") == 0 && Controls_IsNone(s.hold2) &&
              !Controls_EscTaken(&s),
          "controls: with a second key Hold gives up C, keeps X, and never gets Esc");
    CHECK(strcmp(r.note, "C moved from Hold to Pause. Esc still pauses.") == 0, "controls: Esc still pauses, the note says");
    Settings_Default(&s);
    snprintf(s.quit, KEYNAME_LEN, "SPACE");
    snprintf(s.hardDrop, KEYNAME_LEN, "ESCAPE"); /* 2.1.0's Back to Space */
    Controls_Words(&s, &w);
    CHECK(Controls_EscTaken(&s) && strcmp(w.hardDrop, "Esc") == 0 && strcmp(w.back, "Space") == 0 && strcmp(w.backName, "Space") == 0,
          "controls: an older config.ini with Esc on Hard drop: Esc drops and Space leaves, and the card says so");
    Settings_Default(&s);
    snprintf(s.quit, KEYNAME_LEN, "R");
    snprintf(s.restart, KEYNAME_LEN, "ESCAPE");
    Controls_Words(&s, &w);
    CHECK(strcmp(w.restart, "Esc") == 0 && strcmp(w.backName, "R") == 0 && strcmp(w.back, "R") == 0,
          "controls: Esc on Restart (an older config.ini): Esc restarts and R leaves, and the hints say so");
    Controls_BindKey(&s, ACT_RESTART, 0, "T");
    CHECK(!Controls_EscTaken(&s) && strcmp(s.restart, "T") == 0, "controls: once Restart has another key, Esc is Back's again");

    char name[24];
    Controls_Name("ESCAPE", name, sizeof(name));
    CHECK(strcmp(name, "Esc") == 0, "controls: ESCAPE reads as Esc");
    Controls_ShortName("BACKSPACE", name, sizeof(name));
    CHECK(strcmp(name, "Bksp") == 0, "controls: short names for the narrow panel");
    Controls_Name("", name, sizeof(name));
    CHECK(strcmp(name, "none") == 0, "controls: an empty binding says so");
    Controls_Name(KEY_NONE_NAME, name, sizeof(name));
    CHECK(strcmp(name, "none") == 0, "controls: so does NONE");
    CHECK(strcmp(Controls_ActionLabel(ACT_PAUSE), "Pause") == 0 && Controls_Key(&s, ACT_COUNT, 0) == NULL && Controls_Key(&s, ACT_PAUSE, 1) == s.quit,
          "controls: labels and fields line up (Pause's second key is config.ini's quit)");
}

void test_records_all(void) {
    test_controls();
    test_classify();
    test_sprint_time();
    test_keys();
    test_bests_and_sounds();
    test_local_table();
    test_shared_and_bests();
    test_settings_file();
    test_repeat();
}
