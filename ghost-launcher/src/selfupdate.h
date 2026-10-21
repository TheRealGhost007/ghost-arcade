#ifndef SELFUPDATE_H
#define SELFUPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include "../sync/updatecheck.h"

/* Update now, the raylib-free half: what the Updates screen needs to run
 * ../install.sh --update for the player and say how it went. launch.c
 * starts and reaps the process (Launch_Update*); this file reads its log
 * ($XDG_DATA_HOME/ghost-launcher/update.log), works out the step it is on
 * from install.sh's "==>" lines, names why it failed in plain words, says
 * why it cannot start, and keeps update.txt honest about which build it
 * describes. No raylib, no processes, so it is tested headless. */

/* install.sh's steps, in order. */
typedef enum {
    SU_STEP_NONE,    /* started, nothing said yet (the package check) */
    SU_STEP_FETCH,   /* "==> Fetching the newest version" */
    SU_STEP_BUILD,   /* "==> Building ..." */
    SU_STEP_TEST,    /* "==> Running the test suites" */
    SU_STEP_INSTALL, /* "==> Installing into ~/.local": too late to stop */
    SU_STEP_DONE,    /* "==> Installed. ..." */
} SelfUpdateStep;

#define SU_SHOWN_STEPS 4 /* FETCH .. INSTALL, the rows on the screen */

/* Why it did not finish. */
typedef enum {
    SU_FAIL_NONE,         /* it finished: installed */
    SU_FAIL_LOCAL,        /* the checkout has changes of its own: not a fast-forward */
    SU_FAIL_NETWORK,      /* GitHub could not be reached */
    SU_FAIL_AUTH,         /* GitHub wanted a sign-in (an ssh key, a password) */
    SU_FAIL_FETCH,        /* getting the newest version failed some other way */
    SU_FAIL_PACKAGES,     /* install.sh's "Missing: ..." */
    SU_FAIL_NOT_CHECKOUT, /* not a git checkout, or it follows no branch */
    SU_FAIL_BUILD,
    SU_FAIL_TESTS,
    SU_FAIL_INSTALL,
    SU_FAIL_START,        /* install.sh could not even be started */
    SU_FAIL_CANCELLED,    /* the player stopped it before Installing */
    SU_FAIL_OTHER,
} SelfUpdateFail;

#define SU_TAIL_LINES 4
#define SU_LINE_LEN 112
#define SU_MISSING_LEN 80

/* The log, read a piece at a time as it grows. */
typedef struct {
    SelfUpdateStep step;
    char missing[SU_MISSING_LEN];      /* "raylib libcurl" from "Missing: raylib libcurl" */
    char version[UPDATE_SHA_LEN];      /* "==> Version: <sha>": the commit just installed */
    bool sawLocal, sawNetwork, sawAuth, sawNoCheckout;
    char tail[SU_TAIL_LINES][SU_LINE_LEN]; /* the last lines worth showing, oldest first */
    int tailCount;
    char partial[SU_LINE_LEN * 4];     /* a line still being written */
    size_t partialLen;
    long offset;                       /* how far into the file Poll has read */
} SelfUpdateLog;

void SelfUpdate_LogReset(SelfUpdateLog *l);
/* Some more of the log. Lines end with '\n'; a piece may end mid-line. */
void SelfUpdate_LogFeed(SelfUpdateLog *l, const char *bytes, size_t n);
/* Reads what the file gained since the last call (a file that got shorter
 * was started again: it is read from the top). True if anything was read. */
bool SelfUpdate_LogPoll(SelfUpdateLog *l, const char *path);

/* One line as it may be shown: ANSI escape codes and the "xx" / "!!"
 * markers gone, tabs as spaces, anything but printable ASCII dropped,
 * trimmed. */
void SelfUpdate_CleanLine(const char *in, char *out, size_t size);
/* The step a cleaned "==>" line starts, or SU_STEP_NONE for any other line. */
SelfUpdateStep SelfUpdate_StepOf(const char *cleanLine);
/* "Getting the newest version", "Building", "Testing", "Installing". */
const char *SelfUpdate_StepLabel(SelfUpdateStep s);

/* How it ended. exitCode: the script's exit status, -1 when a signal ended
 * it. Success needs both exit 0 and the "Installed" line: nothing is
 * half-reported as success. A stop that came once Installing had begun is
 * an install failure, not "nothing was changed". */
SelfUpdateFail SelfUpdate_Classify(const SelfUpdateLog *l, int exitCode, bool cancelled);
/* How an update this launcher did not start ended, from its log alone
 * (there is no exit status to read): success only with the "Installed"
 * line and the Version it installed. */
SelfUpdateFail SelfUpdate_ClassifyLog(const SelfUpdateLog *l);
/* Whether "Or in a terminal: ./install.sh --update" could help: not when
 * the terminal would fail the same way (changes of its own, no checkout),
 * nor after the player stopped it. */
bool SelfUpdate_TerminalHelps(SelfUpdateFail f);
/* The sentence (two short lines; line1 may be "") for a failure. */
void SelfUpdate_FailWords(SelfUpdateFail f, const SelfUpdateLog *l, char *line0, char *line1, size_t size);
/* Stopping is allowed only before Installing begins. */
bool SelfUpdate_CanCancel(SelfUpdateStep s);

/* Why Update now cannot start, "" when it can. */
typedef struct {
    int gamesRunning;        /* games the launcher started, still open */
    bool installRunning;     /* a machine being installed */
    bool updateRunning;      /* this launcher's own update */
    bool updateElsewhere;    /* one started earlier (another launcher) still going */
    const char *sourceDir;   /* the build's checkout (BUILD_SOURCE_DIR), "" if unknown */
} SelfUpdateWhy;
void SelfUpdate_WhyNot(const SelfUpdateWhy *in, char *out, size_t size);

/* update.txt describes the build named in its current= line. One that is
 * not this build's (an older launcher wrote it, and the code has been
 * updated since) is treated as unknown: no NEW VERSION, no "1 new", no
 * "N changes newer". True when the file was about another build, so the
 * caller can ask GitHub again. */
bool SelfUpdate_Believe(UpdateState *u, const char *buildCommit);

/* After a successful update to newSha (may be ""): what update.txt should
 * now say, from what it said (u, as read from the file). True when GitHub
 * still has to be asked (the new build is not known to be the newest). */
bool SelfUpdate_AfterSuccess(UpdateState *u, const char *newSha);

/* "0:07", "1:05", "12:40". */
void SelfUpdate_Elapsed(double seconds, char *out, size_t size);

#endif
