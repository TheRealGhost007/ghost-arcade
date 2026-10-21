#define _POSIX_C_SOURCE 200809L /* fileno, access */
#include "selfupdate.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void SelfUpdate_LogReset(SelfUpdateLog *l) { memset(l, 0, sizeof(*l)); }

static bool StartsWith(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static void Trim(char *s) {
    size_t n = strlen(s), start = 0;
    while (start < n && s[start] == ' ') start++;
    while (n > start && s[n - 1] == ' ') n--;
    memmove(s, s + start, n - start);
    s[n - start] = '\0';
}

void SelfUpdate_CleanLine(const char *in, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    if (!in) return;
    /* A carriage return redraws the line (progress meters): what comes after
     * the last one is what a terminal would show. */
    const char *cr = strrchr(in, '\r');
    if (cr && cr[1] != '\0') in = cr + 1;
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < size; p++) {
        if (*p == 0x1b) { /* ESC [ ... final byte, or ESC and one more */
            if (p[1] == '[') {
                p += 2;
                while (*p && !(*p >= 0x40 && *p <= 0x7e)) p++;
                if (!*p) break;
            } else if (p[1]) {
                p++;
            }
            continue;
        }
        if (*p == '\t') out[o++] = ' ';
        else if (*p >= 32 && *p <= 126) out[o++] = (char)*p;
    }
    out[o] = '\0';
    Trim(out);
    /* install.sh's markers for a warning and for the end. */
    if ((StartsWith(out, "xx") || StartsWith(out, "!!")) && (out[2] == ' ' || out[2] == '\0')) {
        memmove(out, out + 2, strlen(out + 2) + 1);
        Trim(out);
    }
}

SelfUpdateStep SelfUpdate_StepOf(const char *line) {
    if (!line || !StartsWith(line, "==>")) return SU_STEP_NONE;
    const char *s = line + 3;
    while (*s == ' ') s++;
    if (StartsWith(s, "Fetching") || StartsWith(s, "Getting")) return SU_STEP_FETCH;
    if (StartsWith(s, "Building")) return SU_STEP_BUILD;
    if (StartsWith(s, "Running the test") || StartsWith(s, "Testing")) return SU_STEP_TEST;
    if (StartsWith(s, "Installing")) return SU_STEP_INSTALL;
    if (StartsWith(s, "Installed")) return SU_STEP_DONE;
    return SU_STEP_NONE;
}

const char *SelfUpdate_StepLabel(SelfUpdateStep s) {
    switch (s) {
        case SU_STEP_FETCH: return "Getting the newest version";
        case SU_STEP_BUILD: return "Building";
        case SU_STEP_TEST: return "Testing";
        case SU_STEP_INSTALL: return "Installing";
        case SU_STEP_DONE: return "Done";
        default: return "Starting";
    }
}

static bool HasAny(const char *line, const char *const *words) {
    for (; *words; words++) if (strstr(line, *words)) return true;
    return false;
}

/* What install.sh and git say when the checkout cannot simply move forward,
 * when GitHub is out of reach, and when there is no checkout to update. */
static const char *const kLocal[] = {"changes of its own", "Not possible to fast-forward", "would be overwritten",
                                     "commit your changes or stash", "divergent branches", "Diverging branches",
                                     "unrelated histories", NULL};
static const char *const kNetwork[] = {"Could not download the newest version", "Could not resolve host", "unable to access",
                                       "Could not read from remote", "Connection timed out", "Network is unreachable",
                                       "Failed to connect", "Connection refused", NULL};
/* Sign-in trouble says "Could not read from remote" too: it is looked for first. */
static const char *const kAuth[] = {"Permission denied", "Host key verification failed", "Authentication failed",
                                    "could not read Username", "terminal prompts disabled", NULL};
static const char *const kNoCheckout[] = {"needs a git checkout", "not a git repository", "does not follow a branch",
                                          "no tracking information", NULL};

/* git's own "error:" and "fatal:" lines say what went wrong: when the
 * tail is full, other lines make room first. */
static bool KeyLine(const char *line) { return StartsWith(line, "error:") || StartsWith(line, "fatal:"); }

static void KeepLine(SelfUpdateLog *l, const char *line) {
    if (l->tailCount == SU_TAIL_LINES) {
        int drop = 0; /* the oldest line that is not a key line, else the oldest */
        while (drop < SU_TAIL_LINES && KeyLine(l->tail[drop])) drop++;
        if (drop == SU_TAIL_LINES) drop = 0;
        memmove(l->tail[drop], l->tail[drop + 1], sizeof(l->tail[0]) * (size_t)(SU_TAIL_LINES - 1 - drop));
        l->tailCount--;
    }
    snprintf(l->tail[l->tailCount++], SU_LINE_LEN, "%.*s", SU_LINE_LEN - 1, line); /* cut on purpose */
}

static void ReadLine(SelfUpdateLog *l, const char *raw) {
    char line[SU_LINE_LEN * 4];
    SelfUpdate_CleanLine(raw, line, sizeof(line));
    if (!line[0]) return;
    if (StartsWith(line, "==>")) {
        const char *s = line + 3;
        while (*s == ' ') s++;
        if (StartsWith(s, "Version:")) {
            char sha[64];
            snprintf(sha, sizeof(sha), "%.63s", s + 8);
            Trim(sha);
            if (Update_IsSha(sha)) memcpy(l->version, sha, UPDATE_SHA_LEN); /* exactly 40 and the end */
            return;
        }
        SelfUpdateStep step = SelfUpdate_StepOf(line);
        if (step > l->step) l->step = step; /* steps only go forward */
        return;
    }
    const char *missing = strstr(line, "Missing:");
    if (missing) {
        snprintf(l->missing, sizeof(l->missing), "%.79s", missing + 8);
        Trim(l->missing);
    }
    /* install.sh's "--update needs git.": a package that is missing. */
    if (strstr(line, "needs git.") && !l->missing[0]) snprintf(l->missing, sizeof(l->missing), "git");
    if (HasAny(line, kLocal)) l->sawLocal = true;
    if (HasAny(line, kAuth)) l->sawAuth = true;
    if (HasAny(line, kNetwork)) l->sawNetwork = true;
    if (HasAny(line, kNoCheckout)) l->sawNoCheckout = true;
    KeepLine(l, line);
}

void SelfUpdate_LogFeed(SelfUpdateLog *l, const char *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\n') {
            l->partial[l->partialLen] = '\0';
            ReadLine(l, l->partial);
            l->partialLen = 0;
        } else if (c != '\0' && l->partialLen + 1 < sizeof(l->partial)) {
            l->partial[l->partialLen++] = c; /* a line longer than that is cut, not split */
        }
    }
    l->partial[l->partialLen] = '\0';
}

bool SelfUpdate_LogPoll(SelfUpdateLog *l, const char *path) {
    if (!path || !path[0]) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) == 0 && (long)st.st_size < l->offset) { /* started again */
        SelfUpdateLog fresh;
        SelfUpdate_LogReset(&fresh);
        *l = fresh;
    }
    bool any = false;
    if (fseek(f, l->offset, SEEK_SET) == 0) {
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
            SelfUpdate_LogFeed(l, buf, got);
            l->offset += (long)got;
            any = true;
        }
    }
    fclose(f);
    return any;
}

SelfUpdateFail SelfUpdate_Classify(const SelfUpdateLog *l, int exitCode, bool cancelled) {
    if (exitCode == 0 && l->step == SU_STEP_DONE) return SU_FAIL_NONE;
    /* A stop that landed as Installing began cannot claim nothing changed. */
    if (cancelled) return l->step >= SU_STEP_INSTALL ? SU_FAIL_INSTALL : SU_FAIL_CANCELLED;
    if (exitCode == 0) return SU_FAIL_OTHER; /* it ended "fine" without saying it installed anything */
    switch (l->step) {
        case SU_STEP_NONE:
            if (l->missing[0]) return SU_FAIL_PACKAGES;
            if (l->sawNoCheckout) return SU_FAIL_NOT_CHECKOUT;
            if ((exitCode == 126 || exitCode == 127) && l->tailCount == 0) return SU_FAIL_START;
            return SU_FAIL_OTHER;
        case SU_STEP_FETCH:
            if (l->sawLocal) return SU_FAIL_LOCAL;
            if (l->sawAuth) return SU_FAIL_AUTH;
            if (l->sawNetwork) return SU_FAIL_NETWORK;
            if (l->sawNoCheckout) return SU_FAIL_NOT_CHECKOUT;
            return SU_FAIL_FETCH;
        case SU_STEP_BUILD: return SU_FAIL_BUILD;
        case SU_STEP_TEST: return SU_FAIL_TESTS;
        case SU_STEP_INSTALL: return SU_FAIL_INSTALL;
        default: return SU_FAIL_OTHER; /* "Installed", yet it did not exit 0 */
    }
}

SelfUpdateFail SelfUpdate_ClassifyLog(const SelfUpdateLog *l) {
    bool installed = l->step == SU_STEP_DONE && l->version[0];
    return SelfUpdate_Classify(l, installed ? 0 : 1, false);
}

bool SelfUpdate_TerminalHelps(SelfUpdateFail f) {
    return f != SU_FAIL_LOCAL && f != SU_FAIL_NOT_CHECKOUT && f != SU_FAIL_CANCELLED && f != SU_FAIL_NONE;
}

void SelfUpdate_FailWords(SelfUpdateFail f, const SelfUpdateLog *l, char *line0, char *line1, size_t size) {
    const char *a = "", *b = "";
    switch (f) {
        case SU_FAIL_NONE: a = "Ghost Arcade is up to date."; break;
        case SU_FAIL_LOCAL: a = "Your copy of the code has changes of its own, so it was not updated."; b = "Nothing was installed."; break;
        case SU_FAIL_NETWORK: a = "Could not reach GitHub to get the newest version."; b = "Check the network, then try again. Nothing was installed."; break;
        case SU_FAIL_AUTH: a = "This copy cannot sign in to GitHub from the launcher."; b = "Use the terminal line below. Nothing was installed."; break;
        case SU_FAIL_FETCH: a = "Getting the newest version failed."; b = "Nothing was installed."; break;
        case SU_FAIL_PACKAGES:
            if (l && l->missing[0]) {
                snprintf(line0, size, "Some packages it needs are missing: %.40s.", l->missing);
                snprintf(line1, size, "%s", "Install them, then try again. Nothing was installed.");
                return;
            }
            a = "Some packages it needs are missing.";
            b = "Install them, then try again. Nothing was installed.";
            break;
        case SU_FAIL_NOT_CHECKOUT: a = "This copy is not a git checkout that follows GitHub,"; b = "so it cannot update itself. Nothing was installed."; break;
        case SU_FAIL_BUILD: a = "The new version did not build, so nothing was installed."; b = "Your games still work as they did."; break;
        case SU_FAIL_TESTS: a = "The new version failed its tests, so it was not installed."; b = "Your games still work as they did."; break;
        case SU_FAIL_INSTALL: a = "Installing the new version failed part way."; b = "Try again, or run the command below in a terminal."; break;
        case SU_FAIL_START: a = "The updater could not be started."; b = "Nothing was installed."; break;
        case SU_FAIL_CANCELLED: a = "Nothing was installed."; break;
        case SU_FAIL_OTHER: a = "The update stopped with an error before it was done."; b = "The log below says what happened."; break;
    }
    snprintf(line0, size, "%s", a);
    snprintf(line1, size, "%s", b);
}

bool SelfUpdate_CanCancel(SelfUpdateStep s) { return s < SU_STEP_INSTALL; }

void SelfUpdate_WhyNot(const SelfUpdateWhy *in, char *out, size_t size) {
    const char *why = "";
    char path[800];
    struct stat st;
    const char *dir = in->sourceDir ? in->sourceDir : "";
    if (in->updateRunning) why = "An update is already running.";
    else if (in->updateElsewhere) why = "An update started earlier is still running.";
    else if (in->gamesRunning > 0) why = "Close your games first.";
    else if (in->installRunning) why = "Wait for the install to finish first.";
    else if (!dir[0]) why = "This build does not know where its code is.";
    else if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) why = "The folder it was built from is gone.";
    else {
        snprintf(path, sizeof(path), "%s/install.sh", dir);
        if (stat(path, &st) != 0) why = "The folder it was built from has no install.sh.";
        else if (access(path, X_OK) != 0) why = "install.sh there cannot be run.";
        else {
            snprintf(path, sizeof(path), "%s/.git", dir);
            if (stat(path, &st) != 0) why = "The folder it was built from is not a git checkout.";
        }
    }
    snprintf(out, size, "%s", why);
}

bool SelfUpdate_Believe(UpdateState *u, const char *buildCommit) {
    bool ours = Update_IsSha(buildCommit) && strcmp(u->current, buildCommit) == 0;
    if (ours) return false;
    bool stale = u->current[0] != '\0';
    /* Nothing in it is about this build: as if it had never been checked. */
    u->status = UPDATE_UNKNOWN;
    u->behind = 0;
    u->latest[0] = '\0';
    u->title[0] = '\0';
    u->checkedAt = 0;
    snprintf(u->current, sizeof(u->current), "%s", Update_IsSha(buildCommit) ? buildCommit : "");
    return stale;
}

bool SelfUpdate_AfterSuccess(UpdateState *u, const char *newSha) {
    bool known = Update_IsSha(newSha);
    if (known && Update_IsSha(u->latest) && strcmp(newSha, u->latest) == 0) {
        u->status = UPDATE_CURRENT; /* exactly the newest GitHub had */
        u->behind = 0;
        snprintf(u->current, sizeof(u->current), "%s", newSha);
        return false;
    }
    u->status = UPDATE_UNKNOWN;
    u->behind = 0;
    u->checkedAt = 0; /* not checked for this build yet: ghost-sync asks again */
    snprintf(u->current, sizeof(u->current), "%s", known ? newSha : "");
    return true;
}

void SelfUpdate_Elapsed(double seconds, char *out, size_t size) {
    long s = seconds > 0.0 ? (long)seconds : 0;
    if (s >= 3600) snprintf(out, size, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(out, size, "%ld:%02ld", s / 60, s % 60);
}
