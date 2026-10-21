#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>
#include <stddef.h>
#include "stats.h"

/* Forks, detaches (setsid), and execs the given executable path with no
 * arguments. Returns immediately in the parent -- the launcher keeps
 * running so more games can be started. outError receives a short reason
 * on failure (e.g. "not found", "not executable"). gameName and now (a
 * monotonic clock reading, e.g. raylib's GetTime()) are recorded so
 * Launch_ReapFinished can credit playtime once the game exits. */
bool Launch_Game(const char *execPath, const char *gameName, double now,
                  char *outError, int outErrorSize);
/* Same, with one command-line argument for the game (e.g. "--daily"), or NULL. */
bool Launch_GameArg(const char *execPath, const char *gameName, double now, const char *arg,
                     char *outError, int outErrorSize);

/* Non-blocking reap of any finished game processes. For each one that
 * exited, credits its elapsed run time (now - launch time) to `stats` via
 * Stats_AddGameTime and returns true (so the caller knows to save stats);
 * false if nothing changed. Safe/cheap to call every frame. */
bool Launch_ReapFinished(double now, PlayStats *stats);

/* Best-effort accounting for games still running when the launcher itself
 * is about to quit: credits elapsed time so far for every still-active
 * launch. Does not touch the child processes themselves (they keep
 * running independently). */
void Launch_CreditStillRunning(double now, PlayStats *stats);

/* Games the launcher started that are still running (leaving asks first
 * while there are any). */
int Launch_RunningCount(void);

typedef enum {
    INSTALL_IDLE,
    INSTALL_RUNNING,
    INSTALL_DONE,
    INSTALL_FAILED
} InstallStatus;

/* Installs a game from its source folder by running `make -C <sourceDir>
 * install` in the background (one install at a time). Output goes to
 * $XDG_DATA_HOME/ghost-launcher/install.log. Returns false with a reason in
 * outError if it couldn't even start. Progress is picked up by
 * Launch_ReapFinished(); poll it with Launch_InstallStatus(). */
bool Launch_InstallGame(const char *sourceDir, const char *gameName,
                        char *outError, int outErrorSize);

/* Where an install writes its output: $XDG_DATA_HOME/ghost-launcher/install.log,
 * or under ~/.local/share without it; "" with neither. */
void Launch_InstallLogPath(char *out, size_t size);

/* Current install state; *gameNameOut (may be NULL) receives the name it
 * belongs to. DONE/FAILED stick until Launch_InstallAcknowledge(). */
InstallStatus Launch_InstallStatus(const char **gameNameOut);
void Launch_InstallAcknowledge(void);

/* Starts the ghost-sync helper fully detached (double fork, no stdio, never
 * reaped by us, never counted as playtime) so the online boards are fresh
 * by the time the player looks at them. Silently does nothing if the
 * helper isn't installed. */
void Launch_SyncDetached(void);
/* Asks GitHub right now whether a newer version exists (same detached helper). */
void Launch_CheckUpdateNow(void);

/* --- Update now: the arcade's own install.sh --update, in the background --- */
typedef enum {
    SU_RUN_IDLE,
    SU_RUN_RUNNING,
    SU_RUN_ENDED, /* exited (or killed): Launch_UpdateStatus gives how */
} SelfUpdateRun;

/* Runs <sourceDir>/install.sh --update with no shell and no terminal: its
 * own session, stdin /dev/null, stdout and stderr to update.log (opened
 * here first: one that cannot be written is the reason it did not start),
 * git never asking for a password. It takes update.lock (flock) and the
 * updater keeps holding it, so only one update runs at a time, from any
 * launcher or terminal. GHOST_LAUNCHER_UPDATER, when set, names the script
 * to run instead (tests and tools/fake-update.sh), still in sourceDir.
 * False with a reason if it could not start. */
bool Launch_UpdateStart(const char *sourceDir, char *outError, int outErrorSize);
/* RUNNING until Launch_ReapFinished sees it end; then ENDED with the exit
 * status in *exitCode (-1 when a signal ended it) and *cancelled set when
 * Launch_UpdateCancel stopped it. ENDED sticks until acknowledged. */
SelfUpdateRun Launch_UpdateStatus(int *exitCode, bool *cancelled);
void Launch_UpdateAcknowledge(void);
/* Stops it: the whole process group (make, git, the tests) gets SIGTERM, or
 * SIGKILL when hard. False when nothing is running. */
bool Launch_UpdateCancel(bool hard);
/* $XDG_DATA_HOME/ghost-launcher/update.log (~/.local/share without it). */
void Launch_UpdateLogPath(char *out, size_t size);
/* An update that is not this launcher's own, still running: something
 * holds update.lock (another launcher's updater, or install.sh --update in
 * a terminal). launcherLog: update.pid names a live launcher-started
 * updater, so update.log is its output and can be followed; startedAt is
 * when it began (unix seconds, 0 unknown). out may be NULL. A stale
 * update.pid (nothing holds the lock) is removed. */
typedef struct {
    bool running;
    bool launcherLog;
    long long startedAt;
} UpdateElsewhere;
bool Launch_UpdateElsewhere(UpdateElsewhere *out);

/* Replaces this process with the launcher at installedPath (falling back
 * to "ghost-launcher" on PATH). Only returns when both fail. Never
 * /proc/self/exe: after an update that is the deleted old file. */
bool Launch_RestartLauncher(const char *installedPath);

#endif
