/* Browser (browser.c) tests: a throwaway folder tree made with mkdtemp, so
 * the listing, descent, ascent and filters are checked against real dirent
 * results rather than mocks. Compiled by inclusion from test_main.c. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "browser.h"

/* test_main.c has included the system headers before this file, so the
 * feature macro above may come too late to expose mkdtemp under -std=c17;
 * declaring it here (the POSIX prototype) keeps the suite building either way. */
char *mkdtemp(char *template);

/* Scratch buffers are wider than the inputs they join so the compiler can see
 * the joins never truncate. */
#define BROWSER_TEST_PATH (BROWSER_PATH_LEN + BROWSER_NAME_LEN + 2)

static bool browser_touch(const char *dir, const char *name) {
    char path[BROWSER_TEST_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs("x", f);
    fclose(f);
    return true;
}

static void browser_unlink(const char *dir, const char *name) {
    char path[BROWSER_TEST_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    unlink(path);
}

/* Builds: a.mp3 B.WAV c.txt .hidden.mp3 sub/inner.ogg. Returns false if the
 * temp tree could not be made (the checks that need it are then skipped). */
static bool browser_make_tree(char *root, size_t size) {
    const char *tmp = getenv("TMPDIR");
    char tpl[BROWSER_PATH_LEN];
    snprintf(tpl, sizeof(tpl), "%s/chipforge-browser-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(tpl)) return false;
    snprintf(root, size, "%s", tpl);

    char sub[BROWSER_TEST_PATH];
    snprintf(sub, sizeof(sub), "%s/sub", root);
    bool ok = mkdir(sub, 0700) == 0;
    ok = browser_touch(root, "a.mp3") && ok;
    ok = browser_touch(root, "B.WAV") && ok;
    ok = browser_touch(root, "c.txt") && ok;
    ok = browser_touch(root, ".hidden.mp3") && ok;
    ok = browser_touch(sub, "inner.ogg") && ok;
    return ok;
}

static void browser_remove_tree(const char *root) {
    char sub[BROWSER_TEST_PATH];
    snprintf(sub, sizeof(sub), "%s/sub", root);
    browser_unlink(sub, "inner.ogg");
    rmdir(sub);
    browser_unlink(root, "a.mp3");
    browser_unlink(root, "B.WAV");
    browser_unlink(root, "c.txt");
    browser_unlink(root, ".hidden.mp3");
    rmdir(root);
}

static int browser_index_of(const Browser *b, const char *name) {
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->names[i], name) == 0) return i;
    }
    return -1;
}

static void browser_test_listing(const char *root) {
    int extCount = 0;
    const char *const *exts = Browser_AudioExts(&extCount);
    CHECK(extCount >= 4, "audio extension list has the raylib formats");

    Browser b;
    CHECK(Browser_Open(&b, root, exts, extCount), "Browser_Open on the temp folder succeeds");
    CHECK(b.count == 3, "audio listing has sub, a.mp3, B.WAV");
    CHECK(!b.truncated, "small listing is not truncated");
    CHECK(b.selected == 0, "selection starts at the top");
    if (b.count == 3) {
        CHECK(strcmp(b.names[0], "sub") == 0 && b.isDir[0], "folder sub is listed first and flagged as a folder");
        CHECK(strcmp(b.names[1], "a.mp3") == 0 && !b.isDir[1], "a.mp3 is second and a file");
        CHECK(strcmp(b.names[2], "B.WAV") == 0 && !b.isDir[2], "B.WAV is third: upper-case extension still matches");
    }
    CHECK(browser_index_of(&b, ".hidden.mp3") < 0, "hidden files are skipped");
    CHECK(browser_index_of(&b, "c.txt") < 0, "non-audio files are filtered out");

    /* No filter: every regular file shows up. */
    CHECK(Browser_Open(&b, root, NULL, 0), "Browser_Open without a filter succeeds");
    CHECK(b.count == 4, "unfiltered listing has sub plus three files");
    CHECK(browser_index_of(&b, "c.txt") >= 0, "unfiltered listing includes c.txt");
    CHECK(browser_index_of(&b, ".hidden.mp3") < 0, "hidden files are skipped even without a filter");
    CHECK(b.count > 0 && strcmp(b.names[0], "sub") == 0, "folder still sorts first without a filter");
}

static void browser_test_navigation(const char *root) {
    int extCount = 0;
    const char *const *exts = Browser_AudioExts(&extCount);
    Browser b;
    char path[BROWSER_PATH_LEN] = "";
    char expect[BROWSER_TEST_PATH];

    CHECK(Browser_Open(&b, root, exts, extCount), "reopen for navigation");

    /* Enter on sub (row 0) descends. */
    b.selected = 0;
    CHECK(!Browser_Enter(&b, exts, extCount, path, sizeof(path)), "Enter on a folder returns false");
    snprintf(expect, sizeof(expect), "%s/sub", root);
    CHECK(strcmp(b.dir, expect) == 0, "Enter on sub makes it the current folder");
    CHECK(b.count == 1 && strcmp(b.names[0], "inner.ogg") == 0 && !b.isDir[0], "sub lists inner.ogg");

    /* Enter on the file picks it. */
    b.selected = 0;
    CHECK(Browser_Enter(&b, exts, extCount, path, sizeof(path)), "Enter on a file returns true");
    snprintf(expect, sizeof(expect), "%s/sub/inner.ogg", root);
    CHECK(strcmp(path, expect) == 0, "picked path is the full path of inner.ogg");
    CHECK(strstr(path, "//") == NULL, "picked path has no double slashes");

    /* Up returns to the temp folder and reselects sub. */
    CHECK(Browser_Up(&b, exts, extCount), "Browser_Up succeeds");
    CHECK(strcmp(b.dir, root) == 0, "Up lands in the temp folder");
    CHECK(b.count == 3, "listing after Up is the full audio listing again");
    CHECK(b.selected >= 0 && b.selected < b.count && strcmp(b.names[b.selected], "sub") == 0, "Up selects the folder we came from");

    /* Refresh keeps the selection by name. */
    b.selected = 2;
    CHECK(Browser_Refresh(&b, exts, extCount), "Browser_Refresh succeeds");
    CHECK(b.selected == 2 && strcmp(b.names[2], "B.WAV") == 0, "Refresh keeps the selected row");

    /* Up never goes above the root. */
    Browser r;
    Browser_Open(&r, "/", exts, extCount);
    CHECK(!Browser_Up(&r, exts, extCount), "Up at / returns false");
    CHECK(strcmp(r.dir, "/") == 0, "Up at / stays at /");
}

static void browser_test_selection(const char *root) {
    int extCount = 0;
    const char *const *exts = Browser_AudioExts(&extCount);
    Browser b;
    Browser_Open(&b, root, exts, extCount);
    Browser_MoveSelection(&b, 100);
    CHECK(b.selected == b.count - 1, "MoveSelection clamps at the bottom");
    Browser_MoveSelection(&b, -100);
    CHECK(b.selected == 0, "MoveSelection clamps at the top");
    Browser_MoveSelection(&b, 1);
    CHECK(b.selected == 1, "MoveSelection steps down by one");

    /* An empty listing keeps the selection at 0 and Enter does nothing. */
    Browser m;
    char missing[BROWSER_TEST_PATH];
    snprintf(missing, sizeof(missing), "%s/does-not-exist", root);
    CHECK(!Browser_Open(&m, missing, exts, extCount), "Browser_Open on a missing folder returns false");
    CHECK(m.count == 0, "missing folder gives an empty listing");
    CHECK(strcmp(m.dir, missing) == 0, "missing folder still sets dir");
    Browser_MoveSelection(&m, 5);
    CHECK(m.selected == 0, "MoveSelection on an empty listing stays at 0");
    char path[BROWSER_PATH_LEN] = "";
    CHECK(!Browser_Enter(&m, exts, extCount, path, sizeof(path)), "Enter on an empty listing returns false");
}

static void browser_test_helpers(void) {
    CHECK(Browser_IsAudioName("x.MP3"), "x.MP3 is audio (case-insensitive)");
    /* The installed raylib (Arch raylib 6.0) is built without FLAC, so a
     * listed .flac would only ever fail to import. */
    CHECK(!Browser_IsAudioName("song.flac"), "song.flac is not audio: raylib cannot decode FLAC");
    CHECK(Browser_IsAudioName("clip.qoa"), "clip.qoa is audio");
    {
        int extCount = 0;
        const char *const *exts = Browser_AudioExts(&extCount);
        bool hasFlac = false;
        for (int i = 0; i < extCount; i++) if (strcmp(exts[i], "flac") == 0) hasFlac = true;
        CHECK(!hasFlac, "the audio extension list does not advertise flac");
    }
    CHECK(Browser_IsAudioName("a.b.wav"), "the last extension decides");
    CHECK(!Browser_IsAudioName("x.txt"), "x.txt is not audio");
    CHECK(!Browser_IsAudioName("mp3"), "a bare extension with no dot is not audio");
    CHECK(!Browser_IsAudioName("song."), "a trailing dot is not an extension");
    CHECK(!Browser_IsAudioName(""), "empty name is not audio");

    char title[64];
    Browser_TitleFromPath("/a/b/My Song.mp3", title, sizeof(title));
    CHECK(strcmp(title, "My Song") == 0, "title strips the folder and the extension");
    Browser_TitleFromPath("/a/.mp3", title, sizeof(title));
    CHECK(title[0] != '\0', "title of '/a/.mp3' is not empty");
    Browser_TitleFromPath("plain", title, sizeof(title));
    CHECK(strcmp(title, "plain") == 0, "title of a bare name without extension is the name");
    Browser_TitleFromPath("/x/take.one.wav", title, sizeof(title));
    CHECK(strcmp(title, "take.one") == 0, "only the last extension is stripped");

    char small[4];
    Browser_TitleFromPath("/a/abcdefgh.ogg", small, sizeof(small));
    CHECK(strcmp(small, "abc") == 0, "title is cut to size-1 characters");

    char start[BROWSER_PATH_LEN] = "";
    Browser_StartDir(start, sizeof(start));
    CHECK(start[0] == '/', "start dir is an absolute path");
    struct stat st;
    CHECK(stat(start, &st) == 0 && S_ISDIR(st.st_mode), "start dir exists and is a folder");
}

/* More matching entries than BROWSER_MAX: readdir order is arbitrary (hash
 * order on ext4, reverse creation order on tmpfs), so stopping at the first
 * BROWSER_MAX read used to show a random subset and could drop the subfolder.
 * The list must be the entries that sort first, folder included. */
#define BROWSER_TEST_FULL_FILES (BROWSER_MAX + 88)

static void browser_track_name(int i, char *out, size_t size) {
    snprintf(out, size, "track%03d.mp3", i);
}

static void browser_test_full_folder(const char *root) {
    char full[BROWSER_TEST_PATH], sub[BROWSER_TEST_PATH], name[32];
    snprintf(full, sizeof(full), "%s/full", root);
    snprintf(sub, sizeof(sub), "%s/full/AAA_folder", root);
    bool ok = mkdir(full, 0700) == 0 && mkdir(sub, 0700) == 0;
    for (int i = 0; ok && i < BROWSER_TEST_FULL_FILES; i++) {
        browser_track_name(i, name, sizeof(name));
        ok = browser_touch(full, name);
    }
    CHECK(ok, "full folder with a subfolder and 600 tracks created");
    if (ok) {
        int extCount = 0;
        const char *const *exts = Browser_AudioExts(&extCount);
        Browser b;
        CHECK(Browser_Open(&b, full, exts, extCount), "Browser_Open on the full folder succeeds");
        CHECK(b.count == BROWSER_MAX, "full folder lists exactly BROWSER_MAX entries");
        CHECK(b.truncated, "full folder is flagged truncated");
        CHECK(b.count > 0 && strcmp(b.names[0], "AAA_folder") == 0 && b.isDir[0], "the subfolder survives truncation and is first");
        bool firstOnes = b.count == BROWSER_MAX;
        for (int i = 1; firstOnes && i < b.count; i++) {
            browser_track_name(i - 1, name, sizeof(name));
            firstOnes = strcmp(b.names[i], name) == 0 && !b.isDir[i];
        }
        CHECK(firstOnes, "the files shown are track000 .. the alphabetically first BROWSER_MAX-1 of them");
    }
    for (int i = 0; i < BROWSER_TEST_FULL_FILES; i++) {
        browser_track_name(i, name, sizeof(name));
        browser_unlink(full, name);
    }
    rmdir(sub);
    rmdir(full);
}

void test_browser_all(void) {
    char root[BROWSER_PATH_LEN] = "";
    bool made = browser_make_tree(root, sizeof(root));
    CHECK(made, "temp folder tree created");
    if (made) {
        browser_test_listing(root);
        browser_test_navigation(root);
        browser_test_selection(root);
        browser_test_full_folder(root);
    }
    if (root[0]) browser_remove_tree(root);
    browser_test_helpers();
}
