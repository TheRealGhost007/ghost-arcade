/* launch.h without processes, for the screenshot harness (see fake_launch.h). */
#define _POSIX_C_SOURCE 200809L
#include "launch.h"
#include "fake_launch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char sLog[8192];
static struct { bool running; char name[MANIFEST_FIELD_LEN]; double start; } sGame;
static bool sExitPending = false;
static struct { InstallStatus status; char name[MANIFEST_FIELD_LEN]; } sInstall;
static struct { SelfUpdateRun run; int exitCode; bool cancelled; } sUpdate;

static void Note(const char *what, const char *name, const char *extra) {
    size_t used = strlen(sLog);
    snprintf(sLog + used, sizeof(sLog) - used, "%s %s%s%s\n", what, name, extra ? " " : "", extra ? extra : "");
    fprintf(stderr, "[shots] fake %s %s%s%s\n", what, name, extra ? " " : "", extra ? extra : "");
}

const char *FakeLaunch_Log(void) { return sLog; }

/* As launch.c: the card names this file when an install fails. */
void Launch_InstallLogPath(char *out, size_t size) {
    const char *xdgData = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdgData && xdgData[0]) snprintf(out, size, "%s/ghost-launcher/install.log", xdgData);
    else if (home && home[0]) snprintf(out, size, "%s/.local/share/ghost-launcher/install.log", home);
    else if (size > 0) out[0] = '\0';
}
void FakeLaunch_ExitGame(void) { sExitPending = true; }

void FakeLaunch_FinishInstall(bool ok) {
    if (sInstall.status == INSTALL_RUNNING) sInstall.status = ok ? INSTALL_DONE : INSTALL_FAILED;
}

/* The same checks as the real one, so a missing game still says why. */
bool Launch_GameArg(const char *execPath, const char *gameName, double now, const char *arg, char *outError, int outErrorSize) {
    struct stat st;
    if (stat(execPath, &st) != 0) { snprintf(outError, outErrorSize, "Can't find %s", execPath); return false; }
    if (!(st.st_mode & S_IXUSR)) { snprintf(outError, outErrorSize, "%s isn't executable", execPath); return false; }
    sGame.running = true;
    snprintf(sGame.name, sizeof(sGame.name), "%s", gameName);
    sGame.start = now;
    Note("launch", gameName, arg);
    return true;
}

bool Launch_Game(const char *execPath, const char *gameName, double now, char *outError, int outErrorSize) {
    return Launch_GameArg(execPath, gameName, now, NULL, outError, outErrorSize);
}

bool Launch_ReapFinished(double now, PlayStats *stats) {
    if (!sExitPending) return false;
    sExitPending = false;
    if (!sGame.running) return false;
    sGame.running = false;
    if (now > sGame.start) Stats_AddGameTime(stats, sGame.name, now - sGame.start);
    Note("exit", sGame.name, NULL);
    return true;
}

void Launch_CreditStillRunning(double now, PlayStats *stats) {
    if (sGame.running && now > sGame.start) Stats_AddGameTime(stats, sGame.name, now - sGame.start);
    sGame.running = false;
}

int Launch_RunningCount(void) { return sGame.running ? 1 : 0; }

bool Launch_InstallGame(const char *sourceDir, const char *gameName, char *outError, int outErrorSize) {
    if (sInstall.status == INSTALL_RUNNING) { snprintf(outError, outErrorSize, "Still installing %s", sInstall.name); return false; }
    char makefile[700];
    snprintf(makefile, sizeof(makefile), "%s/Makefile", sourceDir);
    struct stat st;
    if (stat(makefile, &st) != 0) { snprintf(outError, outErrorSize, "No Makefile in %s", sourceDir); return false; }
    sInstall.status = INSTALL_RUNNING;
    snprintf(sInstall.name, sizeof(sInstall.name), "%s", gameName);
    Note("install", gameName, sourceDir);
    return true;
}

InstallStatus Launch_InstallStatus(const char **gameNameOut) {
    if (gameNameOut) *gameNameOut = sInstall.name;
    return sInstall.status;
}

void Launch_InstallAcknowledge(void) {
    if (sInstall.status != INSTALL_RUNNING) sInstall.status = INSTALL_IDLE;
}

void Launch_SyncDetached(void) { Note("sync", "scores --check-update", NULL); }
void Launch_CheckUpdateNow(void) { Note("sync", "--check-update-now", NULL); }

/* Update now: nothing runs. The script writes update.log itself (with
 * `write`) and ends it with `update CODE`; the log is emptied on start, as
 * the real one is. */
void Launch_UpdateLogPath(char *out, size_t size) {
    const char *xdgData = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdgData && xdgData[0]) snprintf(out, size, "%s/ghost-launcher/update.log", xdgData);
    else if (home && home[0]) snprintf(out, size, "%s/.local/share/ghost-launcher/update.log", home);
    else if (size > 0) out[0] = '\0';
}

bool Launch_UpdateStart(const char *sourceDir, char *outError, int outErrorSize) {
    if (sUpdate.run == SU_RUN_RUNNING) { snprintf(outError, outErrorSize, "An update is already running."); return false; }
    char log[700];
    Launch_UpdateLogPath(log, sizeof(log));
    FILE *f = log[0] ? fopen(log, "w") : NULL;
    if (f) fclose(f);
    sUpdate.run = SU_RUN_RUNNING;
    sUpdate.exitCode = 0;
    sUpdate.cancelled = false;
    Note("update", sourceDir, "--update");
    return true;
}

void FakeLaunch_FinishUpdate(int exitCode) {
    if (sUpdate.run != SU_RUN_RUNNING) return;
    sUpdate.run = SU_RUN_ENDED;
    sUpdate.exitCode = exitCode;
}

SelfUpdateRun Launch_UpdateStatus(int *exitCode, bool *cancelled) {
    if (exitCode) *exitCode = sUpdate.exitCode;
    if (cancelled) *cancelled = sUpdate.cancelled;
    return sUpdate.run;
}

void Launch_UpdateAcknowledge(void) { if (sUpdate.run == SU_RUN_ENDED) sUpdate.run = SU_RUN_IDLE; }

/* Stopping ends it at once, as a SIGTERM to install.sh would. */
bool Launch_UpdateCancel(bool hard) {
    if (sUpdate.run != SU_RUN_RUNNING) return false;
    sUpdate.cancelled = true;
    sUpdate.run = SU_RUN_ENDED;
    sUpdate.exitCode = -1;
    Note("update", "cancel", hard ? "hard" : NULL);
    return true;
}

/* An update that is not this launcher's, as the script says (`elsewhere`):
 * 0 none, 1 another launcher's (update.log is its output), 2 a terminal's. */
static int sElsewhere;
void FakeLaunch_SetElsewhere(int kind) { sElsewhere = kind; }

bool Launch_UpdateElsewhere(UpdateElsewhere *out) {
    if (out) {
        out->running = sElsewhere != 0;
        out->launcherLog = sElsewhere == 1;
        out->startedAt = 0;
    }
    return sElsewhere != 0 && sUpdate.run != SU_RUN_RUNNING;
}

/* The launcher is not replaced: the log says it would have been. */
bool Launch_RestartLauncher(const char *installedPath) {
    Note("restart", installedPath && installedPath[0] ? installedPath : "ghost-launcher", NULL);
    return true;
}
