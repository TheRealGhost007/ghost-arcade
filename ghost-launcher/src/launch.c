#define _DEFAULT_SOURCE /* flock, realpath, kill */
#include "launch.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/file.h>

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

static struct {
    SelfUpdateRun run;
    pid_t pid;
    int exitCode;
    bool cancelled;
} sUpdate;

static void ForgetPidFile(pid_t pid);

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
        if (sUpdate.run == SU_RUN_RUNNING && pid == sUpdate.pid) {
            sUpdate.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            sUpdate.run = SU_RUN_ENDED;
            ForgetPidFile(pid);
            continue; /* an update is not playtime either */
        }
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

static void SpawnSync(const char *flag);

void Launch_SyncDetached(void) { SpawnSync("--check-update"); }
void Launch_CheckUpdateNow(void) { SpawnSync("--check-update-now"); }

static void SpawnSync(const char *flag) {
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
        execlp("ghost-sync", "ghost-sync", "--quiet", flag, (char *)NULL);

        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            char fallback[600];
            snprintf(fallback, sizeof(fallback), "%s/.local/bin/ghost-sync", home);
            execl(fallback, "ghost-sync", "--quiet", flag, (char *)NULL);
        }
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0); /* the intermediate child exits at once */
}

int Launch_RunningCount(void) {
    int n = 0;
    for (int i = 0; i < MAX_ACTIVE_LAUNCHES; i++) if (sActive[i].active) n++;
    return n;
}

void Launch_InstallLogPath(char *out, size_t size) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdgData && xdgData[0] != '\0') snprintf(out, size, "%s/ghost-launcher/install.log", xdgData);
    else if (home && home[0] != '\0') snprintf(out, size, "%s/.local/share/ghost-launcher/install.log", home);
    else if (size > 0) out[0] = '\0';
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

    char logPath[700];
    Launch_InstallLogPath(logPath, sizeof(logPath));
    if (!logPath[0]) snprintf(logPath, sizeof(logPath), "/dev/null");

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
        if (log > STDERR_FILENO) close(log);
        if (devnull > STDERR_FILENO) close(devnull);
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

/* ------------------------------------------------------------ Update now */

static void DataFile(const char *name, char *out, size_t size) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdgData && xdgData[0] != '\0') snprintf(out, size, "%s/ghost-launcher/%s", xdgData, name);
    else if (home && home[0] != '\0') snprintf(out, size, "%s/.local/share/ghost-launcher/%s", home, name);
    else if (size > 0) out[0] = '\0';
}

void Launch_UpdateLogPath(char *out, size_t size) { DataFile("update.log", out, size); }

/* The folder a data file goes in, and the one above it, made if missing. */
static void MakeDataDir(const char *file) {
    char dir[700];
    snprintf(dir, sizeof(dir), "%s", file);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = '\0';
    char *up = strrchr(dir, '/');
    if (up && up != dir) {
        *up = '\0';
        mkdir(dir, 0755);
        *up = '/';
    }
    mkdir(dir, 0755);
}

static void WritePidFile(pid_t pid) {
    char path[700];
    DataFile("update.pid", path, sizeof(path));
    if (!path[0]) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%ld\n", (long)pid);
    fclose(f);
}

static long ReadPidFile(long long *mtime) {
    char path[700];
    DataFile("update.pid", path, sizeof(path));
    if (mtime) {
        struct stat st;
        *mtime = path[0] && stat(path, &st) == 0 ? (long long)st.st_mtime : 0;
    }
    FILE *f = path[0] ? fopen(path, "r") : NULL;
    if (!f) return 0;
    long pid = 0;
    if (fscanf(f, "%ld", &pid) != 1) pid = 0;
    fclose(f);
    return pid;
}

static void RemovePidFile(void) {
    char path[700];
    DataFile("update.pid", path, sizeof(path));
    if (path[0]) unlink(path);
}

static void ForgetPidFile(pid_t pid) {
    if (ReadPidFile(NULL) == (long)pid) RemovePidFile();
}

/* update.lock, held (flock) by whatever runs an update for as long as it
 * runs: this launcher's child and everything it starts, or a terminal's
 * install.sh --update. -1 when it cannot be opened. */
static int OpenLock(void) {
    char path[700];
    DataFile("update.lock", path, sizeof(path));
    if (!path[0]) return -1;
    MakeDataDir(path);
    return open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
}

/* Is pid the install.sh --update a launcher started? Numbers are reused,
 * so its arguments must say so: "--update" and a script path ending in
 * "/install.sh" (or the GHOST_LAUNCHER_UPDATER script's name). */
static bool IsLauncherUpdate(long pid) {
    if (pid <= 1) return false;
    if (kill((pid_t)pid, 0) != 0 && errno != EPERM) return false;
    char path[64], args[4096];
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(args, 1, sizeof(args) - 1, f);
    fclose(f);
    args[n] = '\0';
    const char *override = getenv("GHOST_LAUNCHER_UPDATER");
    const char *want = "install.sh";
    if (override && override[0]) {
        const char *slash = strrchr(override, '/');
        want = slash ? slash + 1 : override;
    }
    bool update = false, script = false;
    for (size_t at = 0; at < n; at += strlen(args + at) + 1) { /* NUL-separated */
        const char *arg = args + at, *slash = strrchr(arg, '/');
        if (strcmp(arg, "--update") == 0) update = true;
        if (strcmp(slash ? slash + 1 : arg, want) == 0) script = true;
    }
    return update && script;
}

bool Launch_UpdateElsewhere(UpdateElsewhere *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (sUpdate.run == SU_RUN_RUNNING) return false; /* ours: not "elsewhere" */
    int fd = OpenLock();
    if (fd < 0) return false;
    bool held = false;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) held = errno == EWOULDBLOCK;
    else flock(fd, LOCK_UN);
    close(fd);
    long long started = 0;
    long pid = ReadPidFile(&started);
    if (!held) {
        if (pid != 0) RemovePidFile(); /* nothing runs: whatever it names is gone (or reused) */
        return false;
    }
    if (out) {
        out->running = true;
        out->launcherLog = IsLauncherUpdate(pid); /* else a terminal's: update.log is not its output */
        out->startedAt = out->launcherLog ? started : 0;
    }
    return true;
}

/* The environment for the updater, made before fork (setenv in the child
 * is not safe in a process with threads): git must fail, not wait, when it
 * would ask for a password, and install.sh is told the lock is held. */
static char **UpdaterEnv(void) {
    extern char **environ;
    size_t n = 0;
    while (environ && environ[n]) n++;
    char **env = malloc((n + 4) * sizeof(char *));
    if (!env) return NULL;
    size_t o = 0;
    bool ssh = false;
    for (size_t i = 0; i < n; i++) {
        const char *e = environ[i];
        if (strncmp(e, "GIT_TERMINAL_PROMPT=", 20) == 0 || strncmp(e, "GHOST_UPDATE_LOCKED=", 20) == 0) continue;
        if (strncmp(e, "GIT_SSH_COMMAND=", 16) == 0 || strncmp(e, "GIT_SSH=", 8) == 0) ssh = true;
        env[o++] = (char *)e;
    }
    env[o++] = (char *)"GIT_TERMINAL_PROMPT=0";
    env[o++] = (char *)"GHOST_UPDATE_LOCKED=1";
    if (!ssh) env[o++] = (char *)"GIT_SSH_COMMAND=ssh -o BatchMode=yes";
    env[o] = NULL;
    return env;
}

static void SayAndExit(const char *msg, int code) {
    ssize_t ignored = write(STDERR_FILENO, msg, strlen(msg));
    (void)ignored;
    _exit(code);
}

bool Launch_UpdateStart(const char *sourceDir, char *outError, int outErrorSize) {
    if (sUpdate.run == SU_RUN_RUNNING) {
        snprintf(outError, outErrorSize, "An update is already running.");
        return false;
    }
    if (!sourceDir || !sourceDir[0]) {
        snprintf(outError, outErrorSize, "This build does not know where its code is.");
        return false;
    }
    /* The script as an absolute path: the child runs it after chdir. */
    const char *override = getenv("GHOST_LAUNCHER_UPDATER");
    char script[PATH_MAX], wanted[800];
    if (override && override[0]) snprintf(wanted, sizeof(wanted), "%s", override);
    else snprintf(wanted, sizeof(wanted), "%s/install.sh", sourceDir);
    if (!realpath(wanted, script) || access(script, X_OK) != 0) {
        snprintf(outError, outErrorSize, "Can't run %.60s", wanted);
        return false;
    }

    /* One update at a time, from any launcher or terminal: the lock first,
     * and only then is the log started afresh. */
    int lock = OpenLock();
    if (lock < 0) {
        snprintf(outError, outErrorSize, "Could not open update.lock: %s", strerror(errno));
        return false;
    }
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
        bool busy = errno == EWOULDBLOCK;
        snprintf(outError, outErrorSize, busy ? "Another update is already running." : "Could not lock update.lock: %s", strerror(errno));
        close(lock);
        return false;
    }
    /* The log is opened here, not in the child: one that cannot be written
     * is said now, and the child never falls back to the launcher's own
     * stdout and stderr. */
    char logPath[700];
    Launch_UpdateLogPath(logPath, sizeof(logPath));
    if (!logPath[0]) {
        snprintf(outError, outErrorSize, "No folder for update.log (HOME is not set).");
        close(lock);
        return false;
    }
    int log = open(logPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log < 0) {
        snprintf(outError, outErrorSize, "Could not write update.log: %s", strerror(errno));
        close(lock);
        return false;
    }
    int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    char **env = UpdaterEnv();
    if (devnull < 0 || !env) {
        snprintf(outError, outErrorSize, "Couldn't start the update: %s", strerror(errno));
        if (devnull >= 0) close(devnull);
        free(env);
        close(log);
        close(lock);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(outError, outErrorSize, "Couldn't start the update: %s", strerror(errno));
        free(env);
        close(devnull);
        close(log);
        close(lock);
        return false;
    }

    if (pid == 0) {
        /* Only async-signal-safe calls from here to exec. */
        setsid(); /* its own session and process group: no terminal, and Cancel stops all of it */
        dup2(devnull, STDIN_FILENO);
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        /* The lock stays held by the updater and all it starts (the other
         * descriptors close on exec). */
        int flags = fcntl(lock, F_GETFD);
        if (flags >= 0) fcntl(lock, F_SETFD, flags & ~FD_CLOEXEC);
        if (chdir(sourceDir) != 0) SayAndExit("xx  Can't open the folder Ghost Arcade was built from.\n", 126);
        /* argv, not a shell string; the test suites stay on. */
        char *argv[] = {script, (char *)"--update", NULL};
        execve(script, argv, env);
        SayAndExit("xx  Can't run the updater script.\n", 127);
    }

    free(env);
    close(devnull);
    close(log);
    close(lock); /* the child's copy holds it */
    sUpdate.run = SU_RUN_RUNNING;
    sUpdate.pid = pid;
    sUpdate.exitCode = 0;
    sUpdate.cancelled = false;
    WritePidFile(pid);
    return true;
}

SelfUpdateRun Launch_UpdateStatus(int *exitCode, bool *cancelled) {
    if (exitCode) *exitCode = sUpdate.exitCode;
    if (cancelled) *cancelled = sUpdate.cancelled;
    return sUpdate.run;
}

void Launch_UpdateAcknowledge(void) {
    if (sUpdate.run == SU_RUN_ENDED) sUpdate.run = SU_RUN_IDLE;
}

bool Launch_UpdateCancel(bool hard) {
    if (sUpdate.run != SU_RUN_RUNNING || sUpdate.pid <= 0) return false;
    sUpdate.cancelled = true;
    int sig = hard ? SIGKILL : SIGTERM;
    if (kill(-sUpdate.pid, sig) != 0) kill(sUpdate.pid, sig); /* before its setsid(): just the one */
    return true;
}

bool Launch_RestartLauncher(const char *installedPath) {
    if (installedPath && installedPath[0]) execl(installedPath, installedPath, (char *)NULL);
    execlp("ghost-launcher", "ghost-launcher", (char *)NULL);
    return false;
}
