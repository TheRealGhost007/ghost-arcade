/* A folder listing for picking a recording to import. POSIX dirent/stat only,
 * no raylib, so the suite can exercise it headless. */
#define _POSIX_C_SOURCE 200809L
#include "browser.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* What raylib 6's LoadWave decodes as installed (Arch raylib 6.0): its
 * compile-time default set. FLAC is deliberately absent: the system library
 * is built without SUPPORT_FILEFORMAT_FLAC, so a listed .flac could only ever
 * fail to import. */
static const char *const kAudioExts[] = { "wav", "ogg", "mp3", "qoa" };
#define AUDIO_EXT_COUNT ((int)(sizeof(kAudioExts) / sizeof(kAudioExts[0])))

typedef struct {
    bool isDir;
    char name[BROWSER_NAME_LEN];
} Entry;

/* Folders first, then files; within each group case-insensitively by name.
 * The plain strcmp tie-break keeps "a.mp3" and "A.mp3" in a stable order so
 * the listing does not shuffle between refreshes. */
static int CompareEntries(const void *pa, const void *pb) {
    const Entry *a = (const Entry *)pa, *b = (const Entry *)pb;
    if (a->isDir != b->isDir) return a->isDir ? -1 : 1;
    int c = strcasecmp(a->name, b->name);
    if (c != 0) return c;
    return strcmp(a->name, b->name);
}

/* Index of the entry that sorts last among the first n. */
static int LastEntry(const Entry *entries, int n) {
    int last = 0;
    for (int i = 1; i < n; i++) {
        if (CompareEntries(&entries[i], &entries[last]) > 0) last = i;
    }
    return last;
}

/* The text after the last '.', or NULL when there is no extension ("mp3",
 * ".hidden" and "name." all count as having none). */
static const char *ExtensionOf(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || dot[1] == '\0') return NULL;
    return dot + 1;
}

static bool ExtMatches(const char *name, const char *const *exts, int extCount) {
    if (!exts || extCount <= 0) return true;
    const char *ext = ExtensionOf(name);
    if (!ext) return false;
    for (int i = 0; i < extCount; i++) {
        if (exts[i] && strcasecmp(ext, exts[i]) == 0) return true;
    }
    return false;
}

/* dir + "/" + name without doubling the slash when dir is "/" or already
 * ends in one. Returns false if it does not fit. */
static bool JoinPath(const char *dir, const char *name, char *out, size_t size) {
    size_t dl = strlen(dir);
    bool slash = dl > 0 && dir[dl - 1] == '/';
    int n = snprintf(out, size, "%s%s%s", dir, slash ? "" : "/", name);
    return n >= 0 && (size_t)n < size;
}

static void EmptyListing(Browser *b) {
    b->count = 0;
    b->selected = 0;
    b->truncated = false;
}

bool Browser_Open(Browser *b, const char *dir, const char *const *exts, int extCount) {
    if (!b) return false;
    if (!dir || !dir[0]) dir = "/";
    snprintf(b->dir, sizeof(b->dir), "%s", dir);
    EmptyListing(b);

    DIR *d = opendir(b->dir);
    if (!d) return false;

    /* Collected into a scratch array so the two parallel arrays in Browser
     * can be sorted together; BROWSER_MAX entries is 66 KB, too big for the
     * stack of a tight embedded build but fine on the heap. */
    Entry *entries = calloc(BROWSER_MAX, sizeof(Entry));
    if (!entries) { closedir(d); return false; }

    /* readdir order is arbitrary (hash order on ext4, reverse creation order
     * on tmpfs), so a folder with more than BROWSER_MAX matches must not just
     * stop at the first BROWSER_MAX read: that showed a random subset of a
     * list the UI presents sorted, and could drop the subfolders, leaving them
     * unreachable. Instead the scratch array keeps the BROWSER_MAX entries
     * that sort first: once full, a new entry displaces the one sorting last
     * only when it sorts before it. Folders sort before every file, so a file
     * never pushes a folder out. */
    int n = 0;
    int last = -1; /* index of the entry sorting last, maintained once the array is full */
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue; /* hidden, plus "." and ".." */
        size_t nameLen = strlen(name);
        if (nameLen >= BROWSER_NAME_LEN) continue; /* could not be reopened from a truncated name */

        /* stat rather than d_type: d_type is DT_UNKNOWN on some file systems
         * and we want symlinks to folders to behave like folders. */
        /* The same limit Browser_Enter has: an entry it could not open is not listed. */
        char full[BROWSER_PATH_LEN];
        if (!JoinPath(b->dir, name, full, sizeof(full))) continue;
        struct stat st;
        if (stat(full, &st) != 0) continue;

        bool isDir;
        if (S_ISDIR(st.st_mode)) isDir = true;
        else if (S_ISREG(st.st_mode)) {
            if (!ExtMatches(name, exts, extCount)) continue;
            isDir = false;
        } else continue; /* sockets, fifos, devices: nothing to import */

        Entry e;
        e.isDir = isDir;
        memcpy(e.name, name, nameLen + 1); /* fits: checked above */
        if (n < BROWSER_MAX) {
            entries[n++] = e;
            if (n == BROWSER_MAX) last = LastEntry(entries, n);
        } else {
            b->truncated = true;
            if (CompareEntries(&e, &entries[last]) < 0) {
                entries[last] = e;
                last = LastEntry(entries, n);
            }
        }
    }
    closedir(d);

    qsort(entries, (size_t)n, sizeof(Entry), CompareEntries);
    for (int i = 0; i < n; i++) {
        b->isDir[i] = entries[i].isDir;
        snprintf(b->names[i], sizeof(b->names[i]), "%s", entries[i].name);
    }
    b->count = n;
    free(entries);
    return true;
}

/* Index of name in the listing, or -1. */
static int FindName(const Browser *b, const char *name) {
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->names[i], name) == 0) return i;
    }
    return -1;
}

bool Browser_Refresh(Browser *b, const char *const *exts, int extCount) {
    if (!b) return false;
    char keep[BROWSER_NAME_LEN] = "";
    int oldIndex = b->selected;
    if (b->count > 0 && b->selected >= 0 && b->selected < b->count) {
        snprintf(keep, sizeof(keep), "%s", b->names[b->selected]);
    }
    char dir[BROWSER_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s", b->dir);
    bool ok = Browser_Open(b, dir, exts, extCount);

    /* Prefer the same entry by name (something above it may have appeared or
     * gone); fall back to the same row, clamped. */
    int found = keep[0] ? FindName(b, keep) : -1;
    if (found >= 0) b->selected = found;
    else {
        b->selected = oldIndex;
        if (b->selected >= b->count) b->selected = b->count - 1;
        if (b->selected < 0) b->selected = 0;
    }
    return ok;
}

void Browser_MoveSelection(Browser *b, int delta) {
    if (!b) return;
    if (b->count <= 0) { b->selected = 0; return; }
    long s = (long)b->selected + delta;
    if (s < 0) s = 0;
    if (s > b->count - 1) s = b->count - 1;
    b->selected = (int)s;
}

bool Browser_Enter(Browser *b, const char *const *exts, int extCount, char *pathOut, size_t pathSize) {
    if (!b || b->count <= 0 || b->selected < 0 || b->selected >= b->count) return false;
    char full[BROWSER_PATH_LEN];
    if (!JoinPath(b->dir, b->names[b->selected], full, sizeof(full))) return false;

    if (b->isDir[b->selected]) {
        /* A folder we cannot read still becomes the current folder (empty
         * listing), so Up takes you back out of it. */
        Browser_Open(b, full, exts, extCount);
        return false;
    }
    if (pathOut && pathSize > 0) snprintf(pathOut, pathSize, "%s", full);
    return true;
}

bool Browser_Up(Browser *b, const char *const *exts, int extCount) {
    if (!b) return false;
    char cur[BROWSER_PATH_LEN];
    snprintf(cur, sizeof(cur), "%s", b->dir);

    /* Drop trailing slashes (except a lone "/") so the split below sees the
     * real last component. */
    size_t len = strlen(cur);
    while (len > 1 && cur[len - 1] == '/') cur[--len] = '\0';
    if (len <= 1) return false; /* already at the root */

    char *slash = strrchr(cur, '/');
    if (!slash) return false; /* a relative name with no parent to go to */

    char from[BROWSER_NAME_LEN];
    snprintf(from, sizeof(from), "%s", slash + 1);
    if (slash == cur) cur[1] = '\0'; /* "/music" -> "/" */
    else *slash = '\0';

    bool ok = Browser_Open(b, cur, exts, extCount);
    int found = FindName(b, from);
    if (found >= 0) b->selected = found;
    return ok;
}

bool Browser_IsAudioName(const char *name) {
    if (!name) return false;
    return ExtMatches(name, kAudioExts, AUDIO_EXT_COUNT);
}

const char *const *Browser_AudioExts(int *count) {
    if (count) *count = AUDIO_EXT_COUNT;
    return kAudioExts;
}

static bool IsDirectory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void Browser_StartDir(char *out, size_t size) {
    if (!out || size == 0) return;
    const char *home = getenv("HOME");
    if (home && home[0]) {
        char music[BROWSER_PATH_LEN];
        if (JoinPath(home, "Music", music, sizeof(music)) && IsDirectory(music)) {
            snprintf(out, size, "%s", music);
            return;
        }
        if (IsDirectory(home)) {
            snprintf(out, size, "%s", home);
            return;
        }
    }
    snprintf(out, size, "/");
}

void Browser_TitleFromPath(const char *path, char *out, size_t size) {
    if (!out || size == 0) return;
    const char *base = path ? path : "";
    const char *slash = strrchr(base, '/');
    if (slash) base = slash + 1;

    /* Everything up to the last dot; a name that is only an extension (".mp3")
     * or has no stem at all gets a placeholder rather than an empty title. */
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);
    if (stem > size - 1) stem = size - 1;
    if (stem == 0) {
        snprintf(out, size, "Imported");
        return;
    }
    memcpy(out, base, stem);
    out[stem] = '\0';
}
