#include "launch.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#define MAX_ACTIVE_LAUNCHES 16

typedef struct {
    pid_t pid;
    char gameName[MANIFEST_FIELD_LEN];
    double startTime;
    bool active;
} ActiveLaunch;

static ActiveLaunch sActive[MAX_ACTIVE_LAUNCHES];

static struct {
    InstallStatus status;
    pid_t pid;
    char gameName[MANIFEST_FIELD_LEN];
} sInstall;

bool Launch_GameArg(const char *execPath, const char *gameName, double now, const char *arg,
                     char *outError, int outErrorSize) {
    struct stat st;
    if (stat(execPath, &st) != 0) {
        snprintf(outError, outErrorSize, "Can't find %s", execPath);
        return false;
    }
    if (!(st.st_mode & S_IXUSR)) {
        snprintf(outError, outErrorSize, "%s isn't executable", execPath);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(outError, outErrorSize, "Couldn't start it: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        setsid();
        char *argv[] = {(char *)execPath, (char *)arg, NULL};
        execv(execPath, argv); /* a NULL arg simply ends the list early */
        _exit(127);
    }

    for (int i = 0; i < MAX_ACTIVE_LAUNCHES; i++) {
        if (!sActive[i].active) {
            sActive[i].active = true;
            sActive[i].pid = pid;
            sActive[i].startTime = now;
            snprintf(sActive[i].gameName, MANIFEST_FIELD_LEN, "%s", gameName);
            break;
        }
        /* If the tracking table is full we simply don't track this launch
         * for playtime purposes; the game still launches fine. */
    }

    return true;
}

bool Launch_ReapFinished(double now, PlayStats *stats) {
    bool changed = false;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (sInstall.status == INSTALL_RUNNING && pid == sInstall.pid) {
            bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            sInstall.status = ok ? INSTALL_DONE : INSTALL_FAILED;
            continue; /* an install is not playtime */
        }
        for (int i = 0; i < MAX_ACTIVE_LAUNCHES; i++) {
            if (sActive[i].active && sActive[i].pid == pid) {
                double elapsed = now - sActive[i].startTime;
                if (elapsed > 0.0) Stats_AddGameTime(stats, sActive[i].gameName, elapsed);
                sActive[i].active = false;
                changed = true;
                break;
            }
        }
    }
    return changed;
}

void Launch_CreditStillRunning(double now, PlayStats *stats) {
    for (int i = 0; i < MAX_ACTIVE_LAUNCHES; i++) {
        if (sActive[i].active) {
            double elapsed = now - sActive[i].startTime;
            if (elapsed > 0.0) Stats_AddGameTime(stats, sActive[i].gameName, elapsed);
            sActive[i].active = false;
        }
    }
}

void Launch_SyncDetached(void) {
    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /* The grandchild is re-parented to init, so it never shows up in
         * Launch_ReapFinished()'s waitpid(-1) and leaves no zombie. */
        pid_t worker = fork();
        if (worker != 0) _exit(0);

        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execlp("ghost-sync", "ghost-sync", "--quiet", (char *)NULL);

        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            char fallback[600];
            snprintf(fallback, sizeof(fallback), "%s/.local/bin/ghost-sync", home);
            execl(fallback, "ghost-sync", "--quiet", (char *)NULL);
        }
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0); /* the intermediate child exits at once */
}

bool Launch_InstallGame(const char *sourceDir, const char *gameName,
                        char *outError, int outErrorSize) {
    if (sInstall.status == INSTALL_RUNNING) {
        snprintf(outError, outErrorSize, "Still installing %s", sInstall.gameName);
        return false;
    }

    char makefile[700];
    snprintf(makefile, sizeof(makefile), "%s/Makefile", sourceDir);
    struct stat st;
    if (stat(makefile, &st) != 0) {
        snprintf(outError, outErrorSize, "No Makefile in %s", sourceDir);
        return false;
    }

    char logPath[700] = "/dev/null";
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdgData && xdgData[0] != '\0') snprintf(logPath, sizeof(logPath), "%s/ghost-launcher/install.log", xdgData);
    else if (home && home[0] != '\0') snprintf(logPath, sizeof(logPath), "%s/.local/share/ghost-launcher/install.log", home);

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(outError, outErrorSize, "Couldn't start the install: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        setsid();
        int log = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        if (log >= 0) {
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
        }
        /* argv, not a shell string: the folder name is never interpreted. */
        execlp("make", "make", "-C", sourceDir, "install", (char *)NULL);
        _exit(127);
    }

    sInstall.status = INSTALL_RUNNING;
    sInstall.pid = pid;
    snprintf(sInstall.gameName, sizeof(sInstall.gameName), "%s", gameName);
    return true;
}

InstallStatus Launch_InstallStatus(const char **gameNameOut) {
    if (gameNameOut) *gameNameOut = sInstall.gameName;
    return sInstall.status;
}

void Launch_InstallAcknowledge(void) {
    if (sInstall.status != INSTALL_RUNNING) sInstall.status = INSTALL_IDLE;
}

bool Launch_Game(const char *execPath, const char *gameName, double now, char *outError, int outErrorSize) {
    return Launch_GameArg(execPath, gameName, now, NULL, outError, outErrorSize);
}
