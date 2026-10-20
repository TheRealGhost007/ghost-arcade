/* Headless tests for Chipforge: the song format and synth (ghost-common), the
 * tracker model, the file browser, the transcriber, the composer, the recipe
 * screen's model, the in-key tools, the picker (Fill, Chords, Vary and Grow),
 * the arranger, playing along, and that the design brief keeps up with the
 * code. No raylib: builds and runs anywhere, like the games' suites.
 *
 * Each area keeps its tests in its own file and is compiled here by inclusion,
 * so the areas share one CHECK counter and one binary without four people
 * editing one file. Before anything runs, XDG_DATA_HOME and XDG_CONFIG_HOME
 * are pointed at a fresh temp folder so no test can touch the real library or
 * the real music.ini; the folder is removed at the end. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "version.h"

#include "test_song.c"
#include "test_editor.c"
#include "test_browser.c"
#include "test_transcribe.c"
#include "test_compose.c"
#include "test_recipe.c"
#include "test_keymode.c"
#include "test_pick.c"
#include "test_arrange.c"
#include "test_take.c"
#include "test_jam.c"

/* Reads a file of the chipforge folder whole, found from this file's own
 * path: `make test` builds tests/test_main.c from chipforge/ and runs it
 * there, and an absolute path works from anywhere. NULL when unreadable. */
static char *docs_read_project_file(const char *name) {
    char path[2048];
    const char *slash = strrchr(__FILE__, '/');
    if (slash) snprintf(path, sizeof(path), "%.*s/../%s", (int)(slash - __FILE__), __FILE__, name);
    else snprintf(path, sizeof(path), "../%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *text = NULL;
    long size = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
    if (size >= 0 && fseek(f, 0, SEEK_SET) == 0 && (text = malloc((size_t)size + 1)) != NULL) {
        size_t got = fread(text, 1, (size_t)size, f);
        text[got] = '\0';
    }
    fclose(f);
    return text;
}

/* DESIGN.md is the brief a builder reads instead of main.c, so a key or a
 * screen missing from it reads as free to take: 1.1 shipped F10 (with F12
 * and Ctrl+M inside it), the Timing row and its test while the brief still
 * said 1.0.0 and gave Settings only the shared rows. */
static void test_docs_all(void) {
    char *design = docs_read_project_file("DESIGN.md");
    CHECK(design != NULL, "docs: DESIGN.md is readable beside the tests");
    if (!design) return;
    CHECK(strstr(design, "Version " CHIPFORGE_VERSION ".") != NULL, "docs: the brief's version is version.h's");
    static const char *const kMustName[] = {
        "`F10`", "`F12`", "`Ctrl+M`", "**CALIBRATE**", "row 0 is Timing", "`Player_ClickTest`",
    };
    for (size_t i = 0; i < sizeof(kMustName) / sizeof(kMustName[0]); i++) {
        char msg[96];
        snprintf(msg, sizeof(msg), "docs: the brief names %s", kMustName[i]);
        CHECK(strstr(design, kMustName[i]) != NULL, msg);
    }
    CHECK(strstr(design, "only the `UI_ROW_DONE` row") == NULL, "docs: the brief no longer gives Settings only the shared rows");
    free(design);
}

/* Removes the sandbox: depth-first so folders are empty when their turn comes.
 * Symlinks are removed, never followed, so nothing outside the tree is touched. */
static void RemoveTree(const char *path) {
    if (!path || !path[0] || strcmp(path, "/") == 0) return;
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                char child[2048];
                snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
                RemoveTree(child);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char root[1024];
    snprintf(root, sizeof(root), "%s/chipforge-tests-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(root)) {
        printf("FAIL: could not create a temp folder for the tests\n");
        return 1;
    }
    char data[1100], config[1100];
    snprintf(data, sizeof(data), "%s/data", root);
    snprintf(config, sizeof(config), "%s/config", root);
    setenv("XDG_DATA_HOME", data, 1);
    setenv("XDG_CONFIG_HOME", config, 1);
    /* Browser_StartDir falls back to $HOME, so keep that inside the sandbox too. */
    setenv("HOME", root, 1);

    test_song_all();
    test_editor_all();
    test_browser_all();
    test_transcribe_all();
    test_compose_all();
    test_recipe_all();
    test_keymode_all();
    test_pick_all();
    test_arrange_all();
    test_take_all();
    test_jam_all();
    test_docs_all();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    RemoveTree(root);
    return gFailures ? 1 : 0;
}
