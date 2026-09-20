#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>
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

/* Current install state; *gameNameOut (may be NULL) receives the name it
 * belongs to. DONE/FAILED stick until Launch_InstallAcknowledge(). */
InstallStatus Launch_InstallStatus(const char **gameNameOut);
void Launch_InstallAcknowledge(void);

/* Starts the ghost-sync helper fully detached (double fork, no stdio, never
 * reaped by us, never counted as playtime) so the online boards are fresh
 * by the time the player looks at them. Silently does nothing if the
 * helper isn't installed. */
void Launch_SyncDetached(void);

#endif
