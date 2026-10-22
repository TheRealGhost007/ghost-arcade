/* Headless tests for Update now (src/selfupdate.c, and its process half in
 * src/launch.c): reading install.sh's log (ANSI codes, markers, lines cut
 * across reads), the steps, why it failed in plain words, why it cannot
 * start, update.txt only believed for this build, and the real background
 * run, with tools/fake-update.sh standing in for install.sh through
 * GHOST_LAUNCHER_UPDATER: no test ever runs git or the real installer.
 * test_main.c's main() points XDG_DATA_HOME at a temp folder first. */
#define _DEFAULT_SOURCE /* usleep, realpath */
#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include "check.h"
#include "../src/selfupdate.h"
#include "../src/launch.h"
#include "../src/screens.h"
#include "../src/hall.h"
#include "../src/keymap.h"

#define SHA_OLD "1111111111111111111111111111111111111111"
#define SHA_NEW "89abcdef0123456789abcdef0123456789abcdef"
#define SHA_GH "2222222222222222222222222222222222222222"

static void FeedText(SelfUpdateLog *l, const char *text) { SelfUpdate_LogFeed(l, text, strlen(text)); }

static SelfUpdateLog LogOf(const char *text) {
    SelfUpdateLog l;
    SelfUpdate_LogReset(&l);
    FeedText(&l, text);
    return l;
}

static bool AllAscii(const char *s) {
    for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return false;
    return true;
}

static void test_clean_and_steps(void) {
    char out[SU_LINE_LEN];
    SelfUpdate_CleanLine("\x1b[1;36m==>\x1b[0m Building the games", out, sizeof(out));
    CHECK(strcmp(out, "==> Building the games") == 0, "update log: colour codes are taken out");
    SelfUpdate_CleanLine("\x1b[1;31mxx\x1b[0m  Install those packages", out, sizeof(out));
    CHECK(strcmp(out, "Install those packages") == 0, "update log: the xx marker goes");
    SelfUpdate_CleanLine("!!  Missing: raylib", out, sizeof(out));
    CHECK(strcmp(out, "Missing: raylib") == 0, "update log: the !! marker goes");
    SelfUpdate_CleanLine("Receiving 10%\rReceiving 100%, done.", out, sizeof(out));
    CHECK(strcmp(out, "Receiving 100%, done.") == 0, "update log: a redrawn line keeps what a terminal would show");
    SelfUpdate_CleanLine("\ttab \x07" "bell caf\xc3\xa9 \x1b]0;title\x07" "end  ", out, sizeof(out));
    CHECK(AllAscii(out) && strstr(out, "tab") == out, "update log: only printable ASCII is kept");
    SelfUpdate_CleanLine("xxl is a word", out, sizeof(out));
    CHECK(strcmp(out, "xxl is a word") == 0, "update log: only a marker on its own is taken out");

    CHECK(SelfUpdate_StepOf("==> Fetching the newest version") == SU_STEP_FETCH, "update steps: fetching");
    CHECK(SelfUpdate_StepOf("==> Building the six games (8 jobs)") == SU_STEP_BUILD, "update steps: building");
    CHECK(SelfUpdate_StepOf("==> Running the test suites") == SU_STEP_TEST, "update steps: testing");
    CHECK(SelfUpdate_StepOf("==> Installing into ~/.local") == SU_STEP_INSTALL, "update steps: installing");
    CHECK(SelfUpdate_StepOf("==> Installed. Start the arcade") == SU_STEP_DONE, "update steps: installed");
    CHECK(SelfUpdate_StepOf("Building without the arrow") == SU_STEP_NONE && SelfUpdate_StepOf("==> Something else") == SU_STEP_NONE,
          "update steps: only install.sh's own lines count");
    CHECK(strcmp(SelfUpdate_StepLabel(SU_STEP_FETCH), "Getting the newest version") == 0 && strcmp(SelfUpdate_StepLabel(SU_STEP_TEST), "Testing") == 0,
          "update steps: their names on the screen");
    CHECK(SelfUpdate_CanCancel(SU_STEP_NONE) && SelfUpdate_CanCancel(SU_STEP_TEST) && !SelfUpdate_CanCancel(SU_STEP_INSTALL) &&
          !SelfUpdate_CanCancel(SU_STEP_DONE), "update steps: Stop only before Installing");
}

static void test_log_reading(void) {
    SelfUpdateLog l;
    SelfUpdate_LogReset(&l);
    FeedText(&l, "==> Fetch");
    CHECK(l.step == SU_STEP_NONE, "update log: half a line is not read yet");
    FeedText(&l, "ing the newest version\n==> Buil");
    CHECK(l.step == SU_STEP_FETCH, "update log: a line cut across two reads is read whole");
    FeedText(&l, "ding\ncc main.c\n==> Running the test suites\n==> Building again\n");
    CHECK(l.step == SU_STEP_TEST, "update log: steps never go back");
    CHECK(l.tailCount == 1 && strcmp(l.tail[0], "cc main.c") == 0, "update log: \"==>\" lines are steps, not log lines");
    FeedText(&l, "one\ntwo\n\n   \nthree\nfour\nfive\n");
    CHECK(l.tailCount == SU_TAIL_LINES && strcmp(l.tail[0], "two") == 0 && strcmp(l.tail[SU_TAIL_LINES - 1], "five") == 0,
          "update log: the last few lines with something in them");
    FeedText(&l, "==> Version: " SHA_NEW "\n==> Version: not-a-sha\n");
    CHECK(strcmp(l.version, SHA_NEW) == 0, "update log: the installed commit is picked up, and only a real one");

    char big[3000];
    memset(big, 'a', sizeof(big) - 2);
    big[sizeof(big) - 2] = '\n';
    big[sizeof(big) - 1] = '\0';
    FeedText(&l, big);
    CHECK(strlen(l.tail[SU_TAIL_LINES - 1]) < SU_LINE_LEN, "update log: a very long line is cut, never overflows");

    /* Polling a real file a piece at a time, as the launcher does. */
    char path[600];
    snprintf(path, sizeof(path), "%s/ghost-launcher/poll-test.log", getenv("XDG_DATA_HOME"));
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "update log: test file");
    if (!f) return;
    fputs("==> Fetching the newest version\n==> Buil", f);
    fflush(f);
    SelfUpdate_LogReset(&l);
    CHECK(SelfUpdate_LogPoll(&l, path) && l.step == SU_STEP_FETCH, "update log poll: reads what is there");
    fputs("ding\n", f);
    fflush(f);
    CHECK(SelfUpdate_LogPoll(&l, path) && l.step == SU_STEP_BUILD, "update log poll: only the new part, joined to the half line");
    CHECK(!SelfUpdate_LogPoll(&l, path), "update log poll: nothing new, nothing read");
    fclose(f);
    f = fopen(path, "w"); /* started again: shorter than before */
    if (f) { fputs("==> Fetching\n", f); fclose(f); }
    CHECK(SelfUpdate_LogPoll(&l, path) && l.step == SU_STEP_FETCH, "update log poll: a log started again is read from the top");
    CHECK(!SelfUpdate_LogPoll(&l, "/nonexistent/update.log"), "update log poll: no file, nothing read");
    remove(path);
}

static void test_classify(void) {
    SelfUpdateLog l = LogOf("==> Fetching\n==> Building\n==> Running the test suites\n==> Installing into ~/.local\n==> Version: " SHA_NEW "\n==> Installed.\n");
    CHECK(SelfUpdate_Classify(&l, 0, false) == SU_FAIL_NONE, "update result: exit 0 and Installed = done");
    CHECK(SelfUpdate_Classify(&l, 1, false) == SU_FAIL_OTHER, "update result: Installed but a failing exit is not success");
    SelfUpdateLog half = LogOf("==> Fetching\n==> Building\n");
    CHECK(SelfUpdate_Classify(&half, 0, false) == SU_FAIL_OTHER, "update result: exit 0 without Installed is not success either");

    SelfUpdateLog pk = LogOf("!!  Missing: raylib libcurl\n    On Arch this installs them:\nxx  Install those packages, then run ./install.sh again.\n");
    CHECK(SelfUpdate_Classify(&pk, 1, false) == SU_FAIL_PACKAGES && strcmp(pk.missing, "raylib libcurl") == 0,
          "update result: missing packages, named");
    SelfUpdateLog lc = LogOf("==> Fetching the newest version\nxx  Your copy has changes of its own (local edits or commits), so it was not updated.\n");
    CHECK(SelfUpdate_Classify(&lc, 1, false) == SU_FAIL_LOCAL, "update result: local changes");
    SelfUpdateLog git = LogOf("==> Fetching the newest version\nerror: Your local changes to the following files would be overwritten by merge:\n");
    CHECK(SelfUpdate_Classify(&git, 1, false) == SU_FAIL_LOCAL, "update result: git's own words for local changes");
    SelfUpdateLog ff = LogOf("==> Fetching\nfatal: Not possible to fast-forward, aborting.\n");
    CHECK(SelfUpdate_Classify(&ff, 128, false) == SU_FAIL_LOCAL, "update result: not a fast-forward");
    SelfUpdateLog nw = LogOf("==> Fetching\nfatal: unable to access 'https://github.com/x/y/': Could not resolve host: github.com\n");
    CHECK(SelfUpdate_Classify(&nw, 1, false) == SU_FAIL_NETWORK, "update result: no network");
    SelfUpdateLog fe = LogOf("==> Fetching\nsomething odd\n");
    CHECK(SelfUpdate_Classify(&fe, 1, false) == SU_FAIL_FETCH, "update result: fetching failed some other way");
    SelfUpdateLog nc = LogOf("xx  --update needs a git checkout (git clone, not a downloaded zip).\n");
    CHECK(SelfUpdate_Classify(&nc, 1, false) == SU_FAIL_NOT_CHECKOUT, "update result: not a checkout");
    SelfUpdateLog bd = LogOf("==> Fetching\n==> Building\nmain.c:1: error: x\n");
    CHECK(SelfUpdate_Classify(&bd, 2, false) == SU_FAIL_BUILD, "update result: build failed");
    SelfUpdateLog ts = LogOf("==> Fetching\n==> Building\n==> Running the test suites\nFAIL: x\n");
    CHECK(SelfUpdate_Classify(&ts, 2, false) == SU_FAIL_TESTS, "update result: tests failed");
    SelfUpdateLog in = LogOf("==> Fetching\n==> Building\n==> Running the test suites\n==> Installing into ~/.local\n");
    CHECK(SelfUpdate_Classify(&in, 2, false) == SU_FAIL_INSTALL, "update result: install failed");
    SelfUpdateLog none = LogOf("");
    CHECK(SelfUpdate_Classify(&none, 127, false) == SU_FAIL_START, "update result: could not even start");
    CHECK(SelfUpdate_Classify(&none, -1, false) == SU_FAIL_OTHER, "update result: killed before saying anything");
    CHECK(SelfUpdate_Classify(&ts, -1, true) == SU_FAIL_CANCELLED, "update result: stopped before Installing");
    CHECK(SelfUpdate_Classify(&in, -1, true) == SU_FAIL_INSTALL, "update result: a stop as Installing began is not 'nothing changed'");

    char a[128], b[128];
    SelfUpdate_FailWords(SU_FAIL_LOCAL, &lc, a, b, sizeof(a));
    CHECK(strcmp(a, "Your copy of the code has changes of its own, so it was not updated.") == 0 && strcmp(b, "Nothing was installed.") == 0,
          "update words: local changes, as the owner asked");
    SelfUpdate_FailWords(SU_FAIL_PACKAGES, &pk, a, b, sizeof(a));
    CHECK(strstr(a, "raylib libcurl") != NULL, "update words: the missing packages are named");
    bool plain = true;
    for (int f = SU_FAIL_NONE; f <= SU_FAIL_OTHER; f++) {
        SelfUpdate_FailWords((SelfUpdateFail)f, &pk, a, b, sizeof(a));
        if (!a[0] || !AllAscii(a) || !AllAscii(b) || strlen(a) > 96 || strlen(b) > 96) plain = false;
    }
    CHECK(plain, "update words: every result has short plain ASCII words (no em dashes)");
    SelfUpdate_FailWords(SU_FAIL_BUILD, &bd, a, b, sizeof(a));
    CHECK(strstr(a, "nothing was installed") != NULL, "update words: a failed build says nothing was installed");
}

static void MakeDir(const char *path) { mkdir(path, 0755); }

static void WriteScript(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(body, f);
    fclose(f);
    chmod(path, 0755);
}

static void test_why_not(void) {
    char root[600], dir[700], path[800], why[96];
    snprintf(root, sizeof(root), "%s/whynot", getenv("XDG_DATA_HOME"));
    MakeDir(root);
    snprintf(dir, sizeof(dir), "%s/checkout", root);
    SelfUpdateWhy w = {0, false, false, false, dir};

    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strcmp(why, "The folder it was built from is gone.") == 0, "update why: the checkout folder is gone");
    MakeDir(dir);
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "no install.sh") != NULL, "update why: no install.sh");
    snprintf(path, sizeof(path), "%s/install.sh", dir);
    WriteScript(path, "#!/bin/sh\n");
    chmod(path, 0644);
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "cannot be run") != NULL, "update why: install.sh that cannot be run");
    chmod(path, 0755);
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "not a git checkout") != NULL, "update why: not a git checkout");
    snprintf(path, sizeof(path), "%s/.git", dir);
    MakeDir(path);
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(why[0] == '\0', "update why: a checkout with install.sh can update");

    w.gamesRunning = 1;
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strcmp(why, "Close your games first.") == 0, "update why: games still open");
    w.gamesRunning = 0;
    w.installRunning = true;
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "install") != NULL, "update why: a machine still installing");
    w.updateRunning = true;
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strcmp(why, "An update is already running.") == 0, "update why: one at a time");
    w = (SelfUpdateWhy){0, false, false, true, dir};
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "started earlier") != NULL, "update why: another launcher's update still going");
    w = (SelfUpdateWhy){0, false, false, false, ""};
    SelfUpdate_WhyNot(&w, why, sizeof(why));
    CHECK(strstr(why, "does not know where") != NULL, "update why: a build that does not know its checkout");
}

static void test_believe(void) {
    UpdateState u = {UPDATE_BEHIND, 5, SHA_OLD, SHA_GH, "Newer", 1789000000};
    CHECK(!SelfUpdate_Believe(&u, SHA_OLD) && u.status == UPDATE_BEHIND && u.behind == 5, "update.txt: about this build, believed");
    /* The owner's case: update.txt from the old build, the launcher already newer. */
    u = (UpdateState){UPDATE_BEHIND, 5, SHA_OLD, SHA_GH, "Newer", 1789000000};
    CHECK(SelfUpdate_Believe(&u, SHA_GH), "update.txt: about another build, so stale");
    CHECK(u.status == UPDATE_UNKNOWN && u.behind == 0 && u.checkedAt == 0 && strcmp(u.current, SHA_GH) == 0 && !u.latest[0],
          "update.txt: stale = unknown: no NEW VERSION, no changes newer");
    UpdateState empty = {0};
    CHECK(!SelfUpdate_Believe(&empty, SHA_OLD) && empty.status == UPDATE_UNKNOWN, "update.txt: never written is not stale");
    u = (UpdateState){UPDATE_BEHIND, 2, SHA_OLD, SHA_GH, "x", 1};
    CHECK(SelfUpdate_Believe(&u, "") && u.status == UPDATE_UNKNOWN && !u.current[0], "update.txt: a build with no commit believes nothing");

    u = (UpdateState){UPDATE_BEHIND, 3, SHA_OLD, SHA_NEW, "Newest", 1789000000};
    CHECK(!SelfUpdate_AfterSuccess(&u, SHA_NEW) && u.status == UPDATE_CURRENT && u.behind == 0 && strcmp(u.current, SHA_NEW) == 0 &&
          u.checkedAt == 1789000000, "update.txt after Update now: at GitHub's newest = current, nothing to ask");
    u = (UpdateState){UPDATE_BEHIND, 3, SHA_OLD, SHA_GH, "Newest", 1789000000};
    CHECK(SelfUpdate_AfterSuccess(&u, SHA_NEW) && u.status == UPDATE_UNKNOWN && u.checkedAt == 0 && strcmp(u.current, SHA_NEW) == 0,
          "update.txt after Update now: something else = ask GitHub again");
    u = (UpdateState){UPDATE_BEHIND, 3, SHA_OLD, SHA_GH, "Newest", 1789000000};
    CHECK(SelfUpdate_AfterSuccess(&u, "") && u.status == UPDATE_UNKNOWN && !u.current[0], "update.txt after Update now: no commit said = unknown");
    u = (UpdateState){UPDATE_BEHIND, 3, SHA_OLD, SHA_GH, "Newest", 1789000000};
    SelfUpdate_AfterSuccess(&u, SHA_NEW);
    CHECK(SelfUpdate_Believe(&u, SHA_OLD) && u.status == UPDATE_UNKNOWN, "update.txt after Update now: the old launcher no longer shows the notice");

    char t[16];
    SelfUpdate_Elapsed(7.9, t, sizeof(t));
    CHECK(strcmp(t, "0:07") == 0, "update elapsed: seconds");
    SelfUpdate_Elapsed(65, t, sizeof(t));
    CHECK(strcmp(t, "1:05") == 0, "update elapsed: minutes");
    SelfUpdate_Elapsed(3725, t, sizeof(t));
    CHECK(strcmp(t, "1:02:05") == 0, "update elapsed: hours");
    SelfUpdate_Elapsed(-3, t, sizeof(t));
    CHECK(strcmp(t, "0:00") == 0, "update elapsed: never negative");
}

/* Runs to its end (10 s at most), reaping as the launcher's frames do. */
static SelfUpdateRun WaitForUpdate(int *code, bool *cancelled) {
    PlayStats stats;
    memset(&stats, 0, sizeof(stats));
    for (int i = 0; i < 200 && Launch_UpdateStatus(NULL, NULL) == SU_RUN_RUNNING; i++) {
        Launch_ReapFinished(0.0, &stats);
        usleep(50 * 1000);
    }
    CHECK(stats.count == 0, "update run: never credited as playtime");
    return Launch_UpdateStatus(code, cancelled);
}

static SelfUpdateFail RunFake(const char *dir, const char *fail, SelfUpdateLog *l) {
    char err[160] = "", log[700];
    int code = 0;
    bool cancelled = false;
    if (fail) setenv("FAKE_UPDATE_FAIL", fail, 1);
    else unsetenv("FAKE_UPDATE_FAIL");
    Launch_UpdateAcknowledge();
    if (!Launch_UpdateStart(dir, err, sizeof(err))) {
        printf("    could not start: %s\n", err);
        return SU_FAIL_START;
    }
    SelfUpdateRun run = WaitForUpdate(&code, &cancelled);
    Launch_UpdateLogPath(log, sizeof(log));
    SelfUpdate_LogReset(l);
    SelfUpdate_LogPoll(l, log);
    unsetenv("FAKE_UPDATE_FAIL");
    return run == SU_RUN_ENDED ? SelfUpdate_Classify(l, code, cancelled) : SU_FAIL_OTHER;
}

static bool FileExists(const char *path) { struct stat st; return stat(path, &st) == 0; }

static void test_real_run(void) {
    char fake[PATH_MAX], dir[700], path[800], err[160], marker[800];
    if (!realpath("tools/fake-update.sh", fake)) {
        CHECK(false, "update run: tools/fake-update.sh is there (run the tests from ghost-launcher/)");
        return;
    }
    /* A checkout whose own install.sh would leave a mark: it must never run. */
    snprintf(dir, sizeof(dir), "%s/fakecheckout", getenv("XDG_DATA_HOME"));
    MakeDir(dir);
    snprintf(marker, sizeof(marker), "%s/REAL_INSTALL_RAN", dir);
    snprintf(path, sizeof(path), "%s/install.sh", dir);
    WriteScript(path, "#!/bin/sh\ntouch REAL_INSTALL_RAN\n");
    setenv("GHOST_LAUNCHER_UPDATER", fake, 1);
    unsetenv("FAKE_UPDATE_SLEEP");

    SelfUpdateLog l;
    CHECK(Launch_UpdateStatus(NULL, NULL) == SU_RUN_IDLE, "update run: idle to begin with");
    CHECK(RunFake(dir, NULL, &l) == SU_FAIL_NONE && l.step == SU_STEP_DONE, "update run: the fake updater runs to Installed");
    CHECK(strcmp(l.version, SHA_NEW) == 0, "update run: the new commit is read from the log");
    CHECK(!FileExists(marker), "update run: GHOST_LAUNCHER_UPDATER ran instead of the checkout's install.sh");
    char pidFile[700];
    snprintf(pidFile, sizeof(pidFile), "%s/ghost-launcher/update.pid", getenv("XDG_DATA_HOME"));
    CHECK(!FileExists(pidFile) && !Launch_UpdateElsewhere(NULL), "update run: its pid file goes when it ends");

    CHECK(RunFake(dir, "test", &l) == SU_FAIL_TESTS, "update run: failing tests are reported as such");
    CHECK(RunFake(dir, "build", &l) == SU_FAIL_BUILD && l.tailCount > 0 && strstr(l.tail[l.tailCount - 1], "Error 1") != NULL,
          "update run: a failed build, with make's last words");
    CHECK(RunFake(dir, "packages", &l) == SU_FAIL_PACKAGES && strcmp(l.missing, "raylib libcurl") == 0, "update run: missing packages, named");
    CHECK(RunFake(dir, "local", &l) == SU_FAIL_LOCAL, "update run: local changes");
    CHECK(RunFake(dir, "network", &l) == SU_FAIL_NETWORK, "update run: no network");
    CHECK(RunFake(dir, "install", &l) == SU_FAIL_INSTALL, "update run: install failed");

    /* Stop: the whole group goes at once, well before its steps would end. */
    setenv("FAKE_UPDATE_SLEEP", "5", 1);
    Launch_UpdateAcknowledge();
    CHECK(Launch_UpdateStart(dir, err, sizeof(err)), "update run: a slow one starts");
    CHECK(!Launch_UpdateStart(dir, err, sizeof(err)) && strstr(err, "already") != NULL, "update run: one at a time");
    CHECK(!Launch_UpdateElsewhere(NULL), "update run: our own is not 'another launcher's'");
    usleep(300 * 1000);
    time_t before = time(NULL);
    CHECK(Launch_UpdateCancel(false), "update run: Stop is sent");
    int code = 0;
    bool cancelled = false;
    CHECK(WaitForUpdate(&code, &cancelled) == SU_RUN_ENDED && cancelled && time(NULL) - before < 4, "update run: stopped, quickly");
    char log[700];
    Launch_UpdateLogPath(log, sizeof(log));
    SelfUpdate_LogReset(&l);
    SelfUpdate_LogPoll(&l, log);
    CHECK(SelfUpdate_Classify(&l, code, cancelled) == SU_FAIL_CANCELLED, "update run: stopped before Installing = nothing installed");
    Launch_UpdateAcknowledge();
    CHECK(!Launch_UpdateCancel(false), "update run: nothing to stop afterwards");
    unsetenv("FAKE_UPDATE_SLEEP");

    /* No terminal: a script that reads stdin gets end-of-file at once, never a wait. */
    char reader[800];
    snprintf(reader, sizeof(reader), "%s/reads-stdin.sh", dir);
    WriteScript(reader, "#!/bin/sh\nread answer\necho \"==> Installing into ~/.local\"\necho \"==> Installed. got '$answer'\"\n");
    setenv("GHOST_LAUNCHER_UPDATER", reader, 1);
    CHECK(RunFake(dir, NULL, &l) == SU_FAIL_NONE, "update run: stdin is /dev/null, so nothing waits for an answer");
    snprintf(reader, sizeof(reader), "%s/not-there.sh", dir);
    setenv("GHOST_LAUNCHER_UPDATER", reader, 1);
    CHECK(!Launch_UpdateStart(dir, err, sizeof(err)) && err[0], "update run: an updater that cannot run is refused with a reason");
    unsetenv("GHOST_LAUNCHER_UPDATER");
    CHECK(!FileExists(marker), "update run: the checkout's own install.sh never ran");
}

static void test_update_screens(void) {
    KitList l;
    UpdatesLayout v = {.mode = UPV_IDLE, .offerUpdate = true, .whyNot = ""};
    Screens_Updates(&l, &v);
    const KitItem *now = Kit_Get(&l, WID_UPDATE_NOW), *chk = Kit_Get(&l, WID_CHECK_NOW);
    CHECK(now && chk && Kit_CanFocus(now) && now->r.y < chk->r.y, "updates screen: Update now above Check now");
    CHECK(Kit_Navigate(&l, WID_CHECK_NOW, KIT_UP) == WID_UPDATE_NOW && Kit_Navigate(&l, WID_UPDATE_NOW, KIT_DOWN) == WID_CHECK_NOW,
          "updates screen: the keys and the d-pad move between them");
    v.whyNot = "Close your games first.";
    Screens_Updates(&l, &v);
    now = Kit_Get(&l, WID_UPDATE_NOW);
    CHECK(now && (now->flags & KIT_DISABLED) && strcmp(now->reason, "Close your games first.") == 0, "updates screen: faint, with the reason");
    v.offerUpdate = false;
    Screens_Updates(&l, &v);
    CHECK(!Kit_Get(&l, WID_UPDATE_NOW) && Kit_Get(&l, WID_CHECK_NOW), "updates screen: no update waiting, no Update now");

    v = (UpdatesLayout){.mode = UPV_RUNNING, .canCancel = true};
    Screens_Updates(&l, &v);
    const KitItem *stop = Kit_Get(&l, WID_UPDATE_CANCEL);
    CHECK(stop && Kit_CanFocus(stop) && !Kit_Get(&l, WID_CHECK_NOW), "updates screen: running, Stop only");
    v.canCancel = false;
    Screens_Updates(&l, &v);
    stop = Kit_Get(&l, WID_UPDATE_CANCEL);
    CHECK(stop && (stop->flags & KIT_DISABLED) && stop->reason[0], "updates screen: no Stop while Installing");
    v = (UpdatesLayout){.mode = UPV_DONE};
    Screens_Updates(&l, &v);
    CHECK(Kit_Get(&l, WID_UPDATE_RESTART) && Kit_Get(&l, WID_UPDATE_LATER) && Kit_FirstFocus(&l) == WID_UPDATE_RESTART,
          "updates screen: done, Restart the launcher and Later");
    v = (UpdatesLayout){.mode = UPV_FAILED, .whyNot = ""};
    Screens_Updates(&l, &v);
    CHECK(Kit_Get(&l, WID_UPDATE_RETRY) && Kit_Get(&l, WID_UPDATE_BACK), "updates screen: failed, Try again and Back");
    bool inside = true;
    for (int i = 0; i < l.count; i++)
        if (l.items[i].r.x < 0 || l.items[i].r.x + l.items[i].r.w > KIT_SCREEN_W) inside = false;
    CHECK(inside, "updates screen: every button on the screen");

    /* While it updates, nothing plays: the hall's line says so. */
    HallStatusIn in = {.updating = true, .installed = true};
    HallStatus st;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Updating Ghost Arcade") == 0 && st.dots, "updating: the card says so");
    in.note = UPDATING_WHY;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, UPDATING_WHY) == 0, "updating: a press that did nothing says why");

    /* U on the Updates screen; Shift+U is not it. */
    Input u;
    memset(&u, 0, sizeof(u));
    u.key[IK_U] = u.keyRepeat[IK_U] = u.keyDown[IK_U] = true;
    CHECK(Keymap_Pressed(KA_UPDATE_NOW, &u) && Keymap_OnCard(KA_UPDATE_NOW), "updating: U is Update now, on the Updates card");
    u.shift = true;
    CHECK(!Keymap_Pressed(KA_UPDATE_NOW, &u), "updating: Shift+U is not");
}


/* The audit's findings: git missing, sign-in trouble, git's key line kept,
 * the stopped words, a followed update judged from its log alone. */
static void test_audit_words(void) {
    SelfUpdateLog ng = LogOf("xx  --update needs git.\n");
    CHECK(SelfUpdate_Classify(&ng, 1, false) == SU_FAIL_PACKAGES && strcmp(ng.missing, "git") == 0,
          "update result: git missing is a missing package, not 'not a checkout'");
    SelfUpdateLog mg = LogOf("!!  Missing: git\nxx  Install those packages, then run ./install.sh again.\n");
    CHECK(SelfUpdate_Classify(&mg, 1, false) == SU_FAIL_PACKAGES && strcmp(mg.missing, "git") == 0, "update result: install.sh's Missing: git");
    SelfUpdateLog au = LogOf("==> Fetching the newest version\ngit@github.com: Permission denied (publickey).\n"
                             "fatal: Could not read from remote repository.\n"
                             "xx  Could not download the newest version (no network, or GitHub did not answer).\n");
    CHECK(SelfUpdate_Classify(&au, 1, false) == SU_FAIL_AUTH, "update result: an ssh key GitHub refused is sign-in trouble, not the network");
    SelfUpdateLog hk = LogOf("==> Fetching\nHost key verification failed.\nfatal: Could not read from remote repository.\n");
    CHECK(SelfUpdate_Classify(&hk, 1, false) == SU_FAIL_AUTH, "update result: host key trouble too");
    char a[128], b[128];
    SelfUpdate_FailWords(SU_FAIL_AUTH, &au, a, b, sizeof(a));
    CHECK(strstr(a, "sign in") != NULL && strstr(b, "terminal") != NULL, "update words: sign-in trouble points at the terminal");
    SelfUpdate_FailWords(SU_FAIL_CANCELLED, &au, a, b, sizeof(a));
    CHECK(strcmp(a, "Nothing was installed.") == 0 && !b[0], "update words: stopped, said once (the headline says it was stopped)");

    SelfUpdateLog lc = LogOf("==> Fetching the newest version\nUpdating cbb1323..cd36efc\n"
                             "error: Your local changes to the following files would be overwritten by merge:\n"
                             "\tghost-launcher/src/main.c\nPlease commit your changes or stash them before you merge.\nAborting\n"
                             "xx  Your copy has changes of its own (local edits or commits), so it was not updated.\n");
    CHECK(lc.tailCount == SU_TAIL_LINES && strncmp(lc.tail[0], "error: Your local changes", 25) == 0 &&
          strstr(lc.tail[SU_TAIL_LINES - 1], "changes of its own") != NULL, "update log: git's error: line stays in the tail");
    SelfUpdateLog keys = LogOf("error: one\nerror: two\nerror: three\nerror: four\nerror: five\n");
    CHECK(keys.tailCount == SU_TAIL_LINES && strcmp(keys.tail[0], "error: two") == 0, "update log: all key lines: the oldest goes");

    SelfUpdateLog ok = LogOf("==> Fetching\n==> Installing into ~/.local\n==> Version: " SHA_NEW "\n==> Installed.\n");
    CHECK(SelfUpdate_ClassifyLog(&ok) == SU_FAIL_NONE, "followed update: Installed and its Version = done");
    SelfUpdateLog noVer = LogOf("==> Fetching\n==> Installing into ~/.local\n==> Installed.\n");
    CHECK(SelfUpdate_ClassifyLog(&noVer) != SU_FAIL_NONE, "followed update: no Version, no success claimed");
    SelfUpdateLog bd = LogOf("==> Fetching\n==> Building\nmain.c:1: error: x\n");
    CHECK(SelfUpdate_ClassifyLog(&bd) == SU_FAIL_BUILD, "followed update: a failed build");
    SelfUpdateLog lc2 = LogOf("==> Fetching\nxx  Your copy has changes of its own (local edits or commits), so it was not updated.\n");
    CHECK(SelfUpdate_ClassifyLog(&lc2) == SU_FAIL_LOCAL, "followed update: local changes");

    CHECK(!SelfUpdate_TerminalHelps(SU_FAIL_LOCAL) && !SelfUpdate_TerminalHelps(SU_FAIL_NOT_CHECKOUT) &&
          !SelfUpdate_TerminalHelps(SU_FAIL_CANCELLED) && SelfUpdate_TerminalHelps(SU_FAIL_NETWORK) &&
          SelfUpdate_TerminalHelps(SU_FAIL_AUTH) && SelfUpdate_TerminalHelps(SU_FAIL_TESTS),
          "update words: the terminal line only where the terminal could do better");

    /* The hall, when an update ended while Updates was not up. */
    HallStatusIn in = {.installed = true, .updateNews = HUN_FAILED};
    HallStatus st;
    Hall_Status(&in, &st);
    CHECK(strstr(st.text, "did not finish") != NULL && strstr(st.text, "Press V") != NULL && st.tone == HT_BAD && !strstr(st.text, "..."),
          "update news: a failure is said on the hall, with the key for Updates");
    in.updateNews = HUN_DONE;
    Hall_Status(&in, &st);
    CHECK(strstr(st.text, "updated") != NULL && strstr(st.text, "restart") != NULL && !strstr(st.text, "..."),
          "update news: a success says to restart, whole");
    in.device = DEV_PAD;
    Hall_Status(&in, &st);
    CHECK(strstr(st.text, "Updates") != NULL && strstr(st.text, "Press V") == NULL && !strstr(st.text, "..."), "update news: the pad is pointed at Updates, not a key");
    in = (HallStatusIn){.installed = true};
    Hall_Status(&in, &st);
    CHECK(strstr(st.text, "Press") == st.text, "update news: none, the usual prompt");

    KitList l;
    UpdatesLayout v = {.mode = UPV_DONE, .restartWhy = "Close your games first."};
    Screens_Updates(&l, &v);
    const KitItem *r = Kit_Get(&l, WID_UPDATE_RESTART);
    CHECK(r && (r->flags & KIT_DISABLED) && strcmp(r->reason, "Close your games first.") == 0 && Kit_Get(&l, WID_UPDATE_LATER),
          "updates screen: Restart waits for the games the launcher started");
    v = (UpdatesLayout){.mode = UPV_RUNNING, .canCancel = true, .elsewhere = true};
    Screens_Updates(&l, &v);
    r = Kit_Get(&l, WID_UPDATE_CANCEL);
    CHECK(r && (r->flags & KIT_DISABLED) && strstr(r->reason, "earlier") != NULL, "updates screen: one followed from earlier has no Stop");
}

static void DataPath(const char *name, char *out, size_t size) { snprintf(out, size, "%s/ghost-launcher/%s", getenv("XDG_DATA_HOME"), name); }

/* update.lock and update.log: one update at a time from anywhere, a log
 * that cannot be written stops it before it starts, the updater's
 * environment and a relative GHOST_LAUNCHER_UPDATER. */
static void test_audit_process(void) {
    char fake[PATH_MAX], dir[700], err[160], lockPath[700], pidPath[700], logPath[700];
    if (!realpath("tools/fake-update.sh", fake)) return;
    snprintf(dir, sizeof(dir), "%s/fakecheckout", getenv("XDG_DATA_HOME"));
    MakeDir(dir);
    DataPath("update.lock", lockPath, sizeof(lockPath));
    DataPath("update.pid", pidPath, sizeof(pidPath));
    DataPath("update.log", logPath, sizeof(logPath));
    setenv("GHOST_LAUNCHER_UPDATER", fake, 1);
    unsetenv("FAKE_UPDATE_SLEEP");
    unsetenv("FAKE_UPDATE_FAIL");
    Launch_UpdateAcknowledge();

    /* Someone else (a terminal's install.sh --update) holds the lock. */
    int other = open(lockPath, O_RDWR | O_CREAT, 0644);
    CHECK(other >= 0 && flock(other, LOCK_EX | LOCK_NB) == 0, "update lock: taken by 'a terminal'");
    FILE *pf = fopen(pidPath, "w");
    if (pf) { fprintf(pf, "%ld\n", (long)getpid()); fclose(pf); } /* a live pid that is no updater */
    UpdateElsewhere e;
    CHECK(Launch_UpdateElsewhere(&e) && e.running && !e.launcherLog, "update lock: held elsewhere = an update elsewhere, not a launcher's log");
    FILE *lf = fopen(logPath, "w");
    if (lf) { fputs("the other run's log\n", lf); fclose(lf); }
    CHECK(!Launch_UpdateStart(dir, err, sizeof(err)) && strstr(err, "Another update") != NULL, "update lock: a second update is refused");
    char got[64] = "";
    lf = fopen(logPath, "r");
    if (lf) { if (!fgets(got, sizeof(got), lf)) got[0] = '\0'; fclose(lf); }
    CHECK(strstr(got, "other run") != NULL, "update lock: the other run's log is left alone");
    flock(other, LOCK_UN);
    close(other);
    CHECK(!Launch_UpdateElsewhere(&e) && !e.running, "update lock: released, nothing elsewhere");
    struct stat st;
    CHECK(stat(pidPath, &st) != 0, "update lock: a stale update.pid (a reused number) is removed");

    /* A launcher's updater left running: followed through its log. */
    pid_t child = fork();
    if (child == 0) {
        int fd = open(lockPath, O_RDWR | O_CREAT, 0644);
        if (fd < 0 || flock(fd, LOCK_EX) != 0) _exit(1);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); }
        setenv("FAKE_UPDATE_SLEEP", "0.3", 1);
        execl(fake, fake, "--update", (char *)NULL);
        _exit(127);
    }
    pf = fopen(pidPath, "w");
    if (pf) { fprintf(pf, "%ld\n", (long)child); fclose(pf); }
    bool seen = false;
    for (int i = 0; i < 40 && !seen; i++) {
        usleep(25 * 1000);
        seen = Launch_UpdateElsewhere(&e) && e.launcherLog && e.startedAt > 0;
    }
    CHECK(seen, "update elsewhere: a launcher's updater still running is found, with its log");
    int status = 0;
    waitpid(child, &status, 0);
    CHECK(!Launch_UpdateElsewhere(&e) && stat(pidPath, &st) != 0, "update elsewhere: when it ends, it is gone");

    /* update.log that cannot be written: refused, not the old log shown. */
    if (geteuid() != 0) {
        lf = fopen(logPath, "w");
        if (lf) { fputs("==> Installed. an old run\n", lf); fclose(lf); }
        chmod(logPath, 0444);
        err[0] = '\0';
        CHECK(!Launch_UpdateStart(dir, err, sizeof(err)) && strstr(err, "update.log") != NULL,
              "update log: one that cannot be written stops it before it starts, with why");
        CHECK(Launch_UpdateStatus(NULL, NULL) == SU_RUN_IDLE, "update log: nothing runs");
        chmod(logPath, 0644);
    }

    /* The updater's world: no prompts, the lock held for it and by it. */
    char script[800];
    snprintf(script, sizeof(script), "%s/env-check.sh", dir);
    WriteScript(script, "#!/bin/sh\n"
                        "echo \"prompt=$GIT_TERMINAL_PROMPT locked=$GHOST_UPDATE_LOCKED\"\n"
                        "if command -v flock >/dev/null 2>&1; then\n"
                        "  if flock -n \"$XDG_DATA_HOME/ghost-launcher/update.lock\" true; then echo lock=free; else echo lock=held; fi\n"
                        "else echo lock=held; fi\n"
                        "echo \"fds=$(ls /proc/$$/fd | tr '\\n' ' ')\"\n"
                        "echo '==> Installing into ~/.local'\necho '==> Version: " SHA_NEW "'\necho '==> Installed.'\n");
    setenv("GHOST_LAUNCHER_UPDATER", script, 1);
    SelfUpdateLog l;
    CHECK(RunFake(dir, NULL, &l) == SU_FAIL_NONE, "updater env: runs");
    FILE *rf = fopen(logPath, "r");
    char all[2048] = "";
    if (rf) { size_t n = fread(all, 1, sizeof(all) - 1, rf); all[n] = '\0'; fclose(rf); }
    CHECK(strstr(all, "prompt=0 locked=1") != NULL, "updater env: git never prompts, install.sh knows the lock is held");
    CHECK(strstr(all, "lock=held") != NULL, "updater env: the updater holds update.lock while it runs");
    char *fds = strstr(all, "fds=");
    int count = 0;
    if (fds) for (char *t = fds + 4; *t && *t != '\n'; t++) if (*t == ' ') count++;
    CHECK(fds && count <= 6, "updater env: no stray descriptors (0 1 2, the lock, the shell's and ls's own)");

    /* A relative override is run from where the launcher was, not the checkout. */
    setenv("GHOST_LAUNCHER_UPDATER", "tools/fake-update.sh", 1);
    CHECK(RunFake(dir, NULL, &l) == SU_FAIL_NONE, "updater: a relative GHOST_LAUNCHER_UPDATER still runs");
    unsetenv("GHOST_LAUNCHER_UPDATER");
}

void test_update_all(void) {
    test_clean_and_steps();
    test_log_reading();
    test_classify();
    test_why_not();
    test_believe();
    test_real_run();
    test_update_screens();
    test_audit_words();
    test_audit_process();
}
