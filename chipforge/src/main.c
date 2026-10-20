#define _XOPEN_SOURCE 700 /* strcasecmp, realpath */
#include "raylib.h"
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "editor.h"
#include "player.h"
#include "browser.h"
#include "transcribe.h"
#include "recipe.h"
#include "compose.h"
#include "keymode.h"
#include "pick.h"
#include "jam.h"
#include "appini.h"
#include "render.h"
#include "version.h"
#include "song.h"
#include "musicsynth.h"
#include "ui.h"
#include "winscale.h"
#include "sfx.h"
#include "prefs.h"
#include "howto.h"

/* Chipforge: the arcade's music maker. main.c owns every piece of state and
 * maps keys to the editor's actions, the player and the file helpers; render.c
 * draws. The games never see any of this: they read the library and music.ini
 * through ghost-common's song.c. */

#define APP_SLUG "chipforge"
#define GAME_COUNT 12 /* the eleven games, then the launcher's hall */
/* Open song and the menu's count read the library up to this many songs, far
 * past any real library; past it the list says it was cut. */
#define MAX_SONGS 2048
#define MAX_PRESETS 16
#define RIGHT_LEN 48

/* The eleven games in the launcher's catalog order, then the launcher, whose
 * hall plays the song given to "ghostarcade". Both the assign screen and the
 * menu stat line walk this list. */
static const char *const kGameSlugs[GAME_COUNT] = {
    "blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift",
    "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb", "ghostarcade",
};
static const char *const kGameNames[GAME_COUNT] = {
    "Blockfall", "Coilrush", "Brickburst", "Skyraid", "Ghostmaze", "Rockdrift",
    "Lanehop", "Crawlshot", "Moondrop", "Gemdive", "Girderclimb", "Ghost Arcade (launcher)",
};

static const char *const kNotes_1_0_0[] = {
    "First release: a four-track chiptune tracker for the songs your games play",
    "Piano keys like FastTracker: Z to M for the current octave, Q to P above it; the drums track uses Z, X and C as pads",
    "Patterns of 64 steps in an order list, with holds, five waveforms, per-track volume, swing and undo",
    "Game themes: the twelve built-in tunes open as presets you can audition and rework",
    "Import a recording: a WAV, OGG or MP3 is listened to and turned into a song you can tidy up",
    "Assign a song to any game, or back to its own theme, from inside the editor with Ctrl+G",
    "Export to a WAV in your Music folder with Ctrl+E; songs live in your Ghost Arcade data folder",
    "Shares the arcade's music volume and fullscreen settings",
    NULL,
};
static const char *const kNotes_1_1_0[] = {
    "Make a song: pick a mood, a speed, a story and a beat, and a song plays while you change them; R in Game themes makes one like a theme",
    "Fill a track (Ctrl+F) with a named style: Left/Right to choose, R for a new take, Esc puts it back",
    "Chords (Ctrl+K): pick a progression by name or type one like 1 6 4 5, and the bass and arp follow",
    "Grow a loop into a song (F9) and vary a pattern (Ctrl+R) into a chorus, a lift, a drop or a fill",
    "In key (F4): the piano keys only play the song's scale; F3 changes the mood, Ctrl+Shift+K tidies stray notes",
    "Play along (F10): record over the loop, caught to the beat, with a count-in and a click",
    "Songs remember their key; a Timing setting matches recording to your speakers",
    NULL,
};
static const UiChangelogEntry kChangelog[] = {
    {"1.1.0", "2026-10-13", kNotes_1_1_0},
    {"1.0.0", "2026-10-12", kNotes_1_0_0},
};

/* Shown if ghost-common's how-to table has no chipforge entry yet, so the F1
 * card never comes up blank. */
static const HowTo kFallbackHowTo = {
    APP_SLUG, "Chipforge",
    "Write chiptunes for your games, or let Chipforge write one from a mood: four tracks of 64 steps per pattern, patterns in an order list. Save a song, then assign it to a game.",
    {{"Z..M / Q..P", "Enter notes (F4 keeps them in key)"}, {"F10", "Play along: record over the loop"},
     {"Space, Enter", "Play from the pattern, from the cursor"}, {"' and Delete", "Hold a note, clear a step"},
     {"Ctrl+F, Ctrl+K", "Fill this track, pick its chords"}, {"Ctrl+S, Ctrl+E", "Save, export a WAV"},
     {"Ctrl+G", "Assign the song to a game"}, {"Esc", "Stop, then back to the menu"}, {NULL, NULL}},
    "Games bring the lead and drums in as the action heats up, so keep the bass and arp interesting on their own.",
};

typedef enum {
    APP_MENU, APP_EDITOR, APP_BROWSE_SONGS, APP_BROWSE_AUDIO, APP_IMPORT, APP_PRESETS,
    APP_ASSIGN, APP_NAME, APP_SETTINGS, APP_HOWTO, APP_UPDATES, APP_RECIPE, APP_CALIBRATE
} AppState;

/* The Timing test (Settings > Timing): 16 clicks at 100 bpm, the first four
 * to find the beat. Without a sound device nothing ever plays, so the test
 * gives up after CAL_NO_SOUND seconds instead of waiting for ever. */
enum { CAL_BPM = 100, CAL_BEATS = 16, CAL_WARMUP = 4, CAL_MAX_TAPS = 64 };
#define CAL_NO_SOUND 2.0

/* The piano keys, as kPiano below lists them. */
#define PIANO_KEYS 29

typedef enum { CONFIRM_NONE, CONFIRM_UNSAVED, CONFIRM_DELETE_SONG } ConfirmKind;
/* Why the name prompt is open decides what happens after the save. The
 * chord line borrows the same one-line prompt and saves nothing. */
typedef enum { NAME_SAVE, NAME_RENAME, NAME_ASSIGN, NAME_LEAVE, NAME_CHORD_LINE } NameReason;

typedef struct {
    AppState state;
    Editor ed;
    char iconPath[600];

    /* menu */
    int menuIndex;
    char statLine[96];

    /* editor */
    ConfirmKind confirm;
    bool playLoop;            /* the last Play was Shift+Space (one pattern repeats) */
    bool scaleKeys;           /* in-key typing (F4): the white piano keys play the song's scale */
    Pick pick;                /* Fill / Chords / Vary / Grow while one is open (kind != PICK_NONE) */
    Jam jam;                  /* playing along (F10) while jam.active */
    JamClock jamClock;        /* the playhead the recording follows */
    int catchGrid;            /* 1, 2 or 4 steps (F12); kept between takes */
    bool clickOn;             /* the click while recording (Ctrl+M) */
    int recKeyNote[PIANO_KEYS];  /* the note each piano key put down while recording, NOTE_NONE when up */
    /* The press id Jam_NoteOn gave each piano key, -1 when up or refused. A
     * release ends exactly that press: two held keys can share a pitch after
     * an octave change, and ending "the note" would cut the wrong one. */
    int recKeyEvent[PIANO_KEYS];
    double recStartedAt;      /* Player_Now() at F10, to notice a loop that never starts (no sound device) */
    bool quitAfterLeave;      /* the window was asked to close with unsaved work */
    char nameError[SONG_FILE_LEN + 64]; /* why the name prompt refused, shown in place of its hints; room for the longest file name */
    AppState howtoReturn;

    /* song browser */
    char songNames[MAX_SONGS + 1][SONG_FILE_LEN]; /* one spare, to tell a cut list from a full one */
    const char *songRows[MAX_SONGS];
    char songRight[MAX_SONGS][RIGHT_LEN];
    const char *songRightRows[MAX_SONGS];
    int songCount, songSel;
    char songNote[96];        /* why the list stops short, or "" */
    char confirmTitle[96];

    /* audio browser */
    Browser browser;
    const char *audioRows[BROWSER_MAX];
    const char *audioRight[BROWSER_MAX];
    char browserTitle[BROWSER_PATH_LEN];

    /* import */
    char importPath[BROWSER_PATH_LEN];
    ImportPhase importPhase;
    int tempoIdx;             /* 0 auto, else 60 + (idx - 1) * 2 bpm */
    bool drums, chords;
    int importSel;
    TranscribeReport report;
    char importMessage[128];
    Song importSong;
    AppState importReturn;
    bool importSongFile;      /* the failure page is for a .gsong that would not open, not a recording */

    /* presets */
    int presetSel;
    char presetNames[MAX_PRESETS][SONG_NAME_LEN];
    const char *presetRows[MAX_PRESETS];
    int presetCount;
    char presetNote[96];

    /* make a song: the recipe the editor's song was composed from */
    Recipe recipe;
    int recipeRow;
    AppState recipeReturn;    /* the menu, or Game themes for "make one like it" */

    /* assign */
    int assignSel;
    char assignRight[GAME_COUNT][SONG_FILE_LEN];
    const char *assignRightRows[GAME_COUNT];
    bool assignHot[GAME_COUNT];

    /* name prompt */
    char nameBuf[SONG_NAME_LEN];
    NameReason nameReason;
    AppState nameReturn;

    /* settings, updates */
    int settingsRow, updatesScroll;
    AppIni ini;               /* Chipforge's own settings (the Timing) */

    /* the Timing test */
    CalibratePhase calPhase;
    double calTaps[CAL_MAX_TAPS];
    int calTapCount;
    int calResult;            /* ms, when CAL_DONE */
    double calStartedAt;      /* Player_Now() when the test was asked for */
    JamClock calClock;

    Song scratch;             /* a song being built before it replaces the editor's */
} App;

/* The editor carries 25 songs of undo history, so this lives in static
 * storage rather than on main's stack. */
static App sApp;

/* --- small helpers --------------------------------------------------------- */

static bool Ctrl(void) { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }
static bool Shift(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }
static bool Alt(void) { return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT); }
/* Navigation keys auto-repeat when held; everything else fires once. */
static bool Nav(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static bool EndsWithNoCase(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

static void StripTrailingSlashes(char *s) {
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = '\0';
}

/* "/home/you/Music/x" -> "~/Music/x", so paths fit on a status line. */
static void Tilde(const char *path, char *out, size_t size) {
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(path, home, hl) == 0 && (path[hl] == '/' || path[hl] == '\0')) snprintf(out, size, "~%s", path + hl);
    else snprintf(out, size, "%s", path);
}

static const char *GameNameForSlug(const char *slug) {
    static char buf[40];
    /* The launcher's theme opens as a preset called Ghost Arcade; the assign
     * list names its slot Ghost Arcade (launcher). */
    if (strcmp(slug, "ghostarcade") == 0) return "Ghost Arcade";
    for (int i = 0; i < GAME_COUNT; i++) if (strcmp(kGameSlugs[i], slug) == 0) return kGameNames[i];
    snprintf(buf, sizeof(buf), "%s", slug);
    if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 'a' + 'A');
    return buf;
}

/* Next to the executable (dev builds), then the XDG install location, then
 * the working directory -- in that order, so an installed launch never
 * depends on which folder the shell happened to be in. */
static void ResolveAssetsDir(char *buf, size_t bufSize) {
    const char *appDir = GetApplicationDirectory();
    snprintf(buf, bufSize, "%sassets", appDir);
    if (DirectoryExists(buf)) return;

    const char *xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        snprintf(buf, bufSize, "%s/" APP_SLUG "/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/" APP_SLUG "/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;

    if (DirectoryExists("assets")) snprintf(buf, bufSize, "assets");
}

static void HandleTextInput(char *buf, int bufSize) {
    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c < 127) {
            int len = (int)strlen(buf);
            if (len < bufSize - 1) {
                buf[len] = (char)c;
                buf[len + 1] = '\0';
            }
        }
        c = GetCharPressed();
    }
    if (Nav(KEY_BACKSPACE)) {
        int len = (int)strlen(buf);
        if (len > 0) buf[len - 1] = '\0';
    }
}

static void Status(App *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void Status(App *a, const char *fmt, ...) {
    char buf[EDITOR_STATUS_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Editor_SetStatus(&a->ed, buf);
}

/* --- library, saving, exporting ------------------------------------------- */

static void RefreshMenuStats(App *a) {
    static char names[MAX_SONGS + 1][SONG_FILE_LEN];
    int songs = Song_LibraryList(names, MAX_SONGS + 1);
    int games = 0;
    char path[1024];
    /* Only count assignments whose file still exists: a missing one means
     * the game falls back to its own theme. */
    for (int i = 0; i < GAME_COUNT; i++) if (Song_Assigned(kGameSlugs[i], path, sizeof(path))) games++;
    if (songs > MAX_SONGS)
        snprintf(a->statLine, sizeof(a->statLine), "Over %d songs in your library, %d with a song of yours", MAX_SONGS, games);
    else
        snprintf(a->statLine, sizeof(a->statLine), "%d song%s in your library, %d with a song of yours",
                 songs, songs == 1 ? "" : "s", games);
}

/* Writes the editor's song to its library file. False (with a status line)
 * if it has no file name yet or the write failed. */
static bool SaveEditor(App *a) {
    char path[1024];
    if (a->ed.fileName[0] == '\0') return false;
    if (!Song_LibraryPath(a->ed.fileName, path, sizeof(path), true) || !Song_Save(&a->ed.song, path)) {
        Status(a, "Could not save %s", a->ed.fileName);
        Sfx_Jingle(JINGLE_WARNING);
        return false;
    }
    a->ed.dirty = false;
    Status(a, "Saved %s", a->ed.fileName);
    Sfx_Jingle(JINGLE_CONFIRM);
    return true;
}

/* The first of <base>.wav, <base>-chip.wav, <base>-chip-2.wav, ... that does
 * not exist yet. An export must never replace a file Chipforge did not write:
 * an imported song is named after its recording and the import browser opens
 * in ~/Music, so <base>.wav is often the very recording the song came from. */
static bool FreeExportPath(const char *dir, const char *base, char *out, size_t size) {
    snprintf(out, size, "%s/%s.wav", dir, base);
    if (!FileExists(out)) return true;
    snprintf(out, size, "%s/%s-chip.wav", dir, base);
    if (!FileExists(out)) return true;
    for (int n = 2; n < 1000; n++) {
        snprintf(out, size, "%s/%s-chip-%d.wav", dir, base, n);
        if (!FileExists(out)) return true;
    }
    return false;
}

static void DrawEditor(App *a);

static void ExportWav(App *a) {
    char dir[1024], base[SONG_FILE_LEN], path[1200], shown[EDITOR_STATUS_LEN];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/Music", home ? home : "");
    if (!home || !DirectoryExists(dir)) {
        if (!Song_LibraryDir(dir, sizeof(dir), true)) { Status(a, "No folder to export to"); return; }
    }
    if (a->ed.fileName[0]) snprintf(base, sizeof(base), "%s", a->ed.fileName);
    else Song_FileNameFor(a->ed.song.name[0] ? a->ed.song.name : "untitled", base, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot && strcasecmp(dot, ".gsong") == 0) *dot = '\0';
    if (!FreeExportPath(dir, base, path, sizeof(path))) {
        Status(a, "No free name left for %s.wav", base);
        Sfx_Jingle(JINGLE_WARNING);
        return;
    }

    /* Rendering blocks the window: a long song at a slow tempo takes many
     * seconds. Say so on screen first rather than leave the last frame up
     * with no word of what is happening. */
    Status(a, "Exporting %.56s...", GetFileName(path));
    DrawEditor(a);
    Tilde(path, shown, sizeof(shown));
    if (Synth_RenderWavFile(&a->ed.song, path, 44100, 0.0f)) {
        Status(a, "Exported to %s", shown);
        Sfx_Jingle(JINGLE_CONFIRM);
    } else {
        Status(a, "Could not write %s", shown);
        Sfx_Jingle(JINGLE_WARNING);
    }
}

static void EnterMenu(App *a) {
    Player_Stop();
    RefreshMenuStats(a);
    a->state = APP_MENU;
}

/* Puts a song in the editor; fileName "" means it has never been saved. */
static void LoadIntoEditor(App *a, const Song *song, const char *fileName, bool dirty) {
    Player_Stop();
    a->pick.kind = PICK_NONE; /* its snapshot belongs to the song being replaced */
    a->jam.active = false;    /* and so does a recording's pattern */
    Player_SetClick(false);
    Editor_SetSong(&a->ed, song, fileName);
    a->ed.dirty = dirty;
    a->confirm = CONFIRM_NONE;
    a->playLoop = false;
    /* In-key typing starts on for a song that says its key (a theme copy, a
     * recipe, anything saved after Chords) and off for one that does not (an
     * import, an older file), where silent black keys would be a surprise;
     * F4 changes it either way. */
    a->scaleKeys = a->ed.song.keyRoot >= 0;
    a->state = APP_EDITOR;
}

/* Opens a .gsong from anywhere. A file inside the library keeps its name so
 * Ctrl+S writes back to it; anything else is treated as never saved. */
static bool OpenGsong(App *a, const char *path) {
    if (!Song_Load(&a->scratch, path)) return false;
    char lib[1024], dir[1024];
    const char *fileName = "";
    if (Song_LibraryDir(lib, sizeof(lib), false)) {
        StripTrailingSlashes(lib);
        snprintf(dir, sizeof(dir), "%s", GetDirectoryPath(path));
        StripTrailingSlashes(dir);
        /* "chipforge my-tune.gsong" from inside the library ("./"), "./x", a
         * symlinked folder or a doubled slash all name the same place, so
         * compare the folders as the kernel resolves them, not as typed. */
        char realDir[PATH_MAX], realLib[PATH_MAX];
        const char *cmpDir = dir, *cmpLib = lib;
        if (realpath(dir, realDir) && realpath(lib, realLib)) { cmpDir = realDir; cmpLib = realLib; }
        /* Keep the name only if saving would write back to this same file:
         * Song_LibraryPath canonicalises names, and one it would rewrite (too
         * long, wrong case suffix, odd characters) must be treated as never
         * saved, or Ctrl+S would quietly write somewhere else. */
        const char *base = GetFileName(path);
        char canon[1024];
        if (strcmp(cmpDir, cmpLib) == 0 && strlen(base) < SONG_FILE_LEN && Song_LibraryPath(base, canon, sizeof(canon), false) &&
            strcmp(GetFileName(canon), base) == 0)
            fileName = base;
    }
    LoadIntoEditor(a, &a->scratch, fileName, false);
    Status(a, "Opened %s", a->scratch.name[0] ? a->scratch.name : GetFileName(path));
    return true;
}

/* --- the name prompt ------------------------------------------------------- */

static void BeginName(App *a, NameReason reason, AppState returnTo) {
    /* A name starts from the current one; a chord line from nothing. */
    snprintf(a->nameBuf, sizeof(a->nameBuf), "%s", reason == NAME_CHORD_LINE ? "" : a->ed.song.name);
    a->nameError[0] = '\0';
    a->nameReason = reason;
    a->nameReturn = returnTo;
    /* The key that opened the prompt may have queued a character. */
    while (GetCharPressed() != 0) { }
    a->state = APP_NAME;
}

static void RefreshAssign(App *a);
static void DoAssign(App *a, int row);
static void FinishChordLine(App *a);

static bool LibraryFileExists(const char *fileName) {
    char path[1024];
    return Song_LibraryPath(fileName, path, sizeof(path), false) && FileExists(path);
}

static void FinishName(App *a) {
    /* First, so a chord line never reaches the saving below (an empty one
     * gets the parser's explanation rather than silence). */
    if (a->nameReason == NAME_CHORD_LINE) { FinishChordLine(a); return; }
    if (a->nameBuf[0] == '\0') return; /* a song needs a name before it can be a file */
    /* Titles map to file names lossily ("My Tune" and "my tune!" share one),
     * so a new name must not land on another song's file. */
    char newFile[SONG_FILE_LEN], oldFile[SONG_FILE_LEN];
    Song_FileNameFor(a->nameBuf, newFile, sizeof(newFile));
    snprintf(oldFile, sizeof(oldFile), "%s", a->ed.fileName);
    if (strcmp(newFile, oldFile) != 0 && LibraryFileExists(newFile)) {
        snprintf(a->nameError, sizeof(a->nameError), "%s is another song's file. Pick another name", newFile);
        Sfx_Jingle(JINGLE_WARNING);
        return; /* stay in the prompt */
    }
    snprintf(a->ed.song.name, sizeof(a->ed.song.name), "%s", a->nameBuf);
    snprintf(a->ed.fileName, sizeof(a->ed.fileName), "%s", newFile);
    bool ok = SaveEditor(a);
    /* A rename is a move: drop the old file and point any game that played
     * it at the new one, so the library does not fork into two copies. */
    if (ok && a->nameReason == NAME_RENAME && oldFile[0] && strcmp(oldFile, newFile) != 0) {
        char oldPath[1024], assigned[SONG_FILE_LEN];
        for (int i = 0; i < GAME_COUNT; i++)
            if (Song_AssignedName(kGameSlugs[i], assigned, sizeof(assigned)) && strcmp(assigned, oldFile) == 0)
                Song_Assign(kGameSlugs[i], newFile);
        if (Song_LibraryPath(oldFile, oldPath, sizeof(oldPath), false)) remove(oldPath);
    }
    if (!ok) snprintf(a->ed.fileName, sizeof(a->ed.fileName), "%s", oldFile);
    switch (a->nameReason) {
        case NAME_SAVE:
        case NAME_RENAME:
            a->state = APP_EDITOR;
            break;
        case NAME_ASSIGN:
            if (ok) DoAssign(a, a->assignSel);
            RefreshAssign(a);
            a->state = APP_ASSIGN;
            break;
        case NAME_LEAVE:
            /* Back in the editor after a failed save, the close request that
             * started this is over, as it is when Esc cancels the prompt. */
            if (ok) EnterMenu(a); else { a->quitAfterLeave = false; a->state = APP_EDITOR; }
            break;
        case NAME_CHORD_LINE:
            break; /* handled above */
    }
}

/* --- assign ---------------------------------------------------------------- */

static void RefreshAssign(App *a) {
    for (int i = 0; i < GAME_COUNT; i++) {
        if (!Song_AssignedName(kGameSlugs[i], a->assignRight[i], sizeof(a->assignRight[i]))) snprintf(a->assignRight[i], sizeof(a->assignRight[i]), "own theme");
        a->assignRightRows[i] = a->assignRight[i];
        a->assignHot[i] = a->ed.fileName[0] && strcmp(a->assignRight[i], a->ed.fileName) == 0;
    }
}

static void DoAssign(App *a, int row) {
    if (Song_Assign(kGameSlugs[row], a->ed.fileName)) {
        Status(a, "%s now plays %s", kGameNames[row], a->ed.fileName);
        Sfx_Jingle(JINGLE_CONFIRM);
    } else {
        Status(a, "Could not write music.ini");
        Sfx_Jingle(JINGLE_WARNING);
    }
}

/* The assign screen shows the status line in place of its note while it is
 * fresh, so what Enter or Delete did there, or why it failed, is seen. A
 * message the editor had before is not about this screen, so it goes. */
static void EnterAssign(App *a) {
    RefreshAssign(a);
    Editor_SetStatus(&a->ed, "");
    a->state = APP_ASSIGN;
}

/* --- song browser ---------------------------------------------------------- */

static void RefreshSongList(App *a) {
    int n = Song_LibraryList(a->songNames, MAX_SONGS + 1);
    a->songCount = n > MAX_SONGS ? MAX_SONGS : n;
    if (n > MAX_SONGS) snprintf(a->songNote, sizeof(a->songNote), "Showing %d songs only; the library folder is very full", MAX_SONGS);
    else a->songNote[0] = '\0';
    /* music.ini read once per game, not once per game per song: a big
     * library would otherwise read it tens of thousands of times. */
    char assigned[GAME_COUNT][SONG_FILE_LEN];
    for (int g = 0; g < GAME_COUNT; g++)
        if (!Song_AssignedName(kGameSlugs[g], assigned[g], sizeof(assigned[g]))) assigned[g][0] = '\0';
    for (int i = 0; i < a->songCount; i++) {
        a->songRows[i] = a->songNames[i];
        a->songRight[i][0] = '\0';
        /* The games that play this file, joined with commas. */
        for (int g = 0; g < GAME_COUNT; g++) {
            if (!assigned[g][0] || strcmp(assigned[g], a->songNames[i]) != 0) continue;
            size_t len = strlen(a->songRight[i]);
            snprintf(a->songRight[i] + len, sizeof(a->songRight[i]) - len, "%s%s", len ? ", " : "", kGameNames[g]);
        }
        a->songRightRows[i] = a->songRight[i];
    }
    if (a->songSel >= a->songCount) a->songSel = a->songCount - 1;
    if (a->songSel < 0) a->songSel = 0;
}

static void DeleteSelectedSong(App *a) {
    if (a->songCount == 0) return;
    const char *name = a->songNames[a->songSel];
    char path[1024];
    if (Song_LibraryPath(name, path, sizeof(path), false) && remove(path) == 0) {
        /* A game pointing at a deleted file would fall back to its own theme
         * anyway; clearing the line keeps the assign screen honest. */
        char assigned[SONG_FILE_LEN];
        for (int g = 0; g < GAME_COUNT; g++) {
            if (Song_AssignedName(kGameSlugs[g], assigned, sizeof(assigned)) && strcmp(assigned, name) == 0) Song_Assign(kGameSlugs[g], NULL);
        }
        if (strcmp(a->ed.fileName, name) == 0) a->ed.fileName[0] = '\0'; /* the editor's copy is now unsaved */
        Sfx_Jingle(JINGLE_BACK);
    } else {
        Sfx_Jingle(JINGLE_WARNING);
    }
    RefreshSongList(a);
}

/* --- audio browser --------------------------------------------------------- */

static void RefreshAudioRows(App *a) {
    for (int i = 0; i < a->browser.count; i++) {
        a->audioRows[i] = a->browser.names[i];
        a->audioRight[i] = a->browser.isDir[i] ? "folder" : NULL;
    }
    Tilde(a->browser.dir, a->browserTitle, sizeof(a->browserTitle));
}

static void OpenAudioBrowser(App *a) {
    int n = 0;
    const char *const *exts = Browser_AudioExts(&n);
    char start[BROWSER_PATH_LEN];
    Browser_StartDir(start, sizeof(start));
    Browser_Open(&a->browser, start, exts, n);
    RefreshAudioRows(a);
    a->state = APP_BROWSE_AUDIO;
}

/* --- import ---------------------------------------------------------------- */

static int TempoFromIdx(int idx) { return idx <= 0 ? 0 : 60 + (idx - 1) * 2; }
#define TEMPO_IDX_MAX 71 /* 200 bpm */

static void BeginImport(App *a, const char *path, AppState returnTo) {
    Player_Stop();
    a->confirm = CONFIRM_NONE; /* a drop may arrive over the song browser's "Delete song?" box */
    snprintf(a->importPath, sizeof(a->importPath), "%s", path);
    TranscribeOptions o;
    Transcribe_DefaultOptions(&o);
    a->tempoIdx = 0;
    a->drums = !o.noDrums;
    a->chords = !o.noArp;
    a->importSel = 3; /* Enter imports with the defaults; Up reaches the options */
    a->importPhase = IMPORT_OPTIONS;
    a->importReturn = returnTo;
    a->importSongFile = false;
    a->state = APP_IMPORT;
}

/* A .gsong that would not open, from a screen with no status line to say so
 * (the command line lands on the menu or the how-to card; a drop can land on
 * any screen): the import screen's failure page says it whatever the screen,
 * and Esc goes back there. */
static void ShowOpenFailure(App *a, const char *path) {
    snprintf(a->importPath, sizeof(a->importPath), "%s", path);
    snprintf(a->importMessage, sizeof(a->importMessage), "It is not a Chipforge song, or it could not be read.");
    a->importPhase = IMPORT_FAILED;
    a->importSongFile = true;
    a->confirm = CONFIRM_NONE; /* a "Delete song?" box under it is answered "keep it" */
    /* The Timing test cannot pick up where it was, so it ends; the import
     * screen keeps the way back it already had. */
    if (a->state == APP_CALIBRATE) { Player_Stop(); a->importReturn = APP_SETTINGS; }
    else if (a->state != APP_IMPORT) a->importReturn = a->state;
    a->state = APP_IMPORT;
}

/* Decodes the file and transcribes it. Synchronous: the caller has already
 * presented a "Listening..." frame so the window is not blank meanwhile. */
static void RunImport(App *a) {
    TranscribeOptions o;
    Transcribe_DefaultOptions(&o);
    o.bpm = TempoFromIdx(a->tempoIdx);
    o.noDrums = !a->drums;
    o.noArp = !a->chords;

    Wave w = LoadWave(a->importPath);
    if (!IsWaveValid(w) || w.frameCount == 0 || w.channels == 0) {
        if (w.data) UnloadWave(w);
        snprintf(a->importMessage, sizeof(a->importMessage), "It could not be decoded. WAV, OGG and MP3 work.");
        a->importPhase = IMPORT_FAILED;
        return;
    }
    int frames = (int)w.frameCount, channels = (int)w.channels, rate = (int)w.sampleRate;
    float *samples = LoadWaveSamples(w);
    float *mono = (float *)malloc((size_t)frames * sizeof(float));
    if (!samples || !mono) {
        if (samples) UnloadWaveSamples(samples);
        free(mono);
        UnloadWave(w);
        snprintf(a->importMessage, sizeof(a->importMessage), "Out of memory while decoding it.");
        a->importPhase = IMPORT_FAILED;
        return;
    }
    /* raylib hands back interleaved channels; the transcriber wants one. */
    for (int f = 0; f < frames; f++) {
        float sum = 0.0f;
        for (int c = 0; c < channels; c++) sum += samples[(size_t)f * (size_t)channels + (size_t)c];
        mono[f] = sum / (float)channels;
    }
    UnloadWaveSamples(samples);
    UnloadWave(w);

    bool ok = Transcribe_Run(mono, frames, rate, &o, &a->importSong, &a->report);
    free(mono);
    if (!ok) {
        snprintf(a->importMessage, sizeof(a->importMessage), "There is too little audio to work with: it needs at least a second.");
        a->importPhase = IMPORT_FAILED;
        return;
    }
    char title[SONG_NAME_LEN];
    Browser_TitleFromPath(a->importPath, title, sizeof(title));
    snprintf(a->importSong.name, sizeof(a->importSong.name), "%s", title);
    a->importPhase = IMPORT_REPORT;
    Sfx_Jingle(JINGLE_LEVEL_UP);
}

/* --- presets --------------------------------------------------------------- */

static void RefreshPresets(App *a) {
    a->presetCount = Music_ThemeCount();
    if (a->presetCount > MAX_PRESETS) a->presetCount = MAX_PRESETS;
    for (int i = 0; i < a->presetCount; i++) {
        snprintf(a->presetNames[i], sizeof(a->presetNames[i]), "%s", GameNameForSlug(Music_ThemeAt(i)->slug));
        a->presetRows[i] = a->presetNames[i];
    }
    if (a->presetSel >= a->presetCount) a->presetSel = 0;
    snprintf(a->presetNote, sizeof(a->presetNote), "Space auditions the highlighted theme, Enter opens a copy in the editor");
}

static void ThemeToScratch(App *a, int i) {
    Music_ThemeToSong(Music_ThemeAt(i), &a->scratch);
    snprintf(a->scratch.name, sizeof(a->scratch.name), "%s", a->presetNames[i]);
}

/* --- make a song (the recipe screen) -------------------------------------- */

/* The recipe's own rows come first (RecipeField order), then what to do with
 * the song. */
enum { RECIPE_ROW_ROLL = RF_COUNT, RECIPE_ROW_OPEN, RECIPE_ROW_GIVE, RECIPE_ROW_BLANK, RECIPE_ROWS };

/* A fresh seed per roll or take. Two 16-bit draws: raylib's generator is
 * seeded at start-up, and a single draw over the whole int range overflows
 * its (max - min + 1). */
static uint32_t FreshSeed(void) {
    return ((uint32_t)GetRandomValue(0, 0xFFFF) << 16) | (uint32_t)GetRandomValue(0, 0xFFFF);
}

/* Opens the recipe screen on a->recipe: the song is composed into the editor
 * and loops while the rows change it, exactly as Game themes auditions. The
 * editor's song is clean whenever this runs: both ways in (the menu and Game
 * themes) are only reached after the song was saved or discarded, and Game
 * themes checks again. */
static void EnterRecipe(App *a, const char *name, AppState returnTo) {
    Player_Stop();
    a->pick.kind = PICK_NONE;
    a->jam.active = false;
    Player_SetClick(false);
    Recipe_Compose(&a->recipe, &a->scratch);
    snprintf(a->scratch.name, sizeof(a->scratch.name), "%s", name);
    Editor_SetSong(&a->ed, &a->scratch, "");
    a->ed.dirty = false;
    a->confirm = CONFIRM_NONE;
    a->playLoop = false;
    a->scaleKeys = true; /* a recipe song always carries its key */
    a->recipeRow = 0;
    a->recipeReturn = returnTo;
    Player_SetIntensity(Recipe_Intensity(&a->recipe));
    Player_Play(0, 0, -1);
    a->state = APP_RECIPE;
}

/* Every row change is heard on the next step: the new song replaces the
 * editor's in place (the player keeps a pointer to it), keeping the song's
 * name, and playback carries on from where it is. The whole song is
 * replaced, since Length changes how many patterns and order entries there
 * are; Pick_ReplaceSong moves the order off patterns that are going away
 * before it rewrites them. No undo entry: nothing is an edit until the song
 * reaches the editor. */
static void RecomposeLive(App *a) {
    Recipe_Compose(&a->recipe, &a->scratch);
    snprintf(a->scratch.name, sizeof(a->scratch.name), "%s", a->ed.song.name);
    Pick_ReplaceSong(&a->ed, &a->scratch);
}

/* --- files from the command line or dropped on the window ----------------- */

static void EndRecording(App *a);

static void OpenAnyPath(App *a, const char *path) {
    /* The editor's song is about to be replaced (or, for an import, may be
     * once the report is accepted), so unsaved work blocks this. */
    if (a->state == APP_NAME || a->importPhase == IMPORT_LISTENING) return;
    /* A take in progress is kept like any other way of stopping it, and then
     * counts as unsaved work below. */
    EndRecording(a);
    /* Guard on the song, not the screen: it is still unsaved behind the
     * assign screen or the how-to card. (The song browser's "Delete song?"
     * box is not unsaved work; the import screen simply replaces it.) */
    if (a->ed.dirty) {
        Status(a, "Save or discard your song first");
        Sfx_Jingle(JINGLE_WARNING);
        /* The how-to card cannot show a status line; the song in question
         * is in the editor it was opened from, which can. */
        if (a->state == APP_HOWTO && a->howtoReturn == APP_EDITOR) a->state = APP_EDITOR;
        return;
    }
    /* A preview is not the song: put it back before the song is replaced,
     * or before the import screen, whose Esc returns to the editor. */
    Pick_Cancel(&a->pick, &a->ed);
    if (EndsWithNoCase(path, ".gsong")) {
        if (!OpenGsong(a, path)) {
            if (a->state == APP_EDITOR) Status(a, "Could not read %s", GetFileName(path));
            else ShowOpenFailure(a, path);
            Sfx_Jingle(JINGLE_WARNING);
        }
        return;
    }
    AppState back = (a->state == APP_BROWSE_AUDIO || a->state == APP_EDITOR) ? a->state : APP_MENU;
    BeginImport(a, path, back);
}

static void HandleDrop(App *a) {
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    if (files.count > 0) OpenAnyPath(a, files.paths[0]);
    UnloadDroppedFiles(files);
}

/* --- the editor's keys ----------------------------------------------------- */

/* FastTracker's two piano rows: semitone offsets from the current octave's C. */
static const struct { int key; int offset; } kPiano[] = {
    {KEY_Z, 0}, {KEY_S, 1}, {KEY_X, 2}, {KEY_D, 3}, {KEY_C, 4}, {KEY_V, 5}, {KEY_G, 6},
    {KEY_B, 7}, {KEY_H, 8}, {KEY_N, 9}, {KEY_J, 10}, {KEY_M, 11},
    {KEY_Q, 12}, {KEY_TWO, 13}, {KEY_W, 14}, {KEY_THREE, 15}, {KEY_E, 16}, {KEY_R, 17}, {KEY_FIVE, 18},
    {KEY_T, 19}, {KEY_SIX, 20}, {KEY_Y, 21}, {KEY_SEVEN, 22}, {KEY_U, 23},
    {KEY_I, 24}, {KEY_NINE, 25}, {KEY_O, 26}, {KEY_ZERO, 27}, {KEY_P, 28},
};
_Static_assert(sizeof(kPiano) / sizeof(kPiano[0]) == PIANO_KEYS, "PIANO_KEYS sizes the recording's key memory");

/* The note piano key i plays now, or NOTE_NONE for a silent one. In key, the
 * white-key positions play the scale and the black ones are silent. The key
 * is read on every press rather than kept: Ctrl+Z can bring back a snapshot
 * from before F4 stored it, and Compose_SongKey then guesses that same key
 * again. */
static int PianoNote(const App *a, int i) {
    const Editor *ed = &a->ed;
    if (a->scaleKeys) return KeyMode_NoteForKey(Compose_SongKey(&ed->song, NULL), ed->octave, kPiano[i].offset);
    return Editor_NoteForKey(ed, kPiano[i].offset);
}

/* The drum pads the lower piano row doubles as, pressed this frame. */
static int PadsPressed(void) {
    int bits = 0;
    if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_K)) bits |= DRUM_KICK;
    if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_S)) bits |= DRUM_SNARE;
    if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_H)) bits |= DRUM_HAT;
    return bits;
}

/* Adding or removing a pattern renumbers the ones after it, so a running
 * pattern loop would now repeat a different pattern from the one on screen.
 * Point it back at the edited pattern. */
static void RepointLoop(App *a, int patternCountBefore) {
    if (a->playLoop && Player_IsPlaying() && a->ed.song.patternCount != patternCountBefore)
        Player_Play(a->ed.orderPos, Player_Step(), a->ed.pattern);
}

static void Apply(App *a, EditorAction act, int arg) {
    int before = a->ed.song.patternCount;
    if (!Editor_Apply(&a->ed, act, arg)) return;
    a->ed.dirty = true;
    RepointLoop(a, before);
}

/* --- the picker (Fill, Chords, Vary, Grow) ---------------------------------- */

typedef enum { PICK_OP_MOVE, PICK_OP_REROLL, PICK_OP_KEY, PICK_OP_TOGGLE, PICK_OP_KEEP, PICK_OP_KEEP_ALL, PICK_OP_CANCEL } PickOp;

/* Loops the edited pattern, as Shift+Space does; from the step playing now
 * when something already plays, so switching to the loop does not jump. */
static void LoopPattern(App *a) {
    int step = Player_IsPlaying() ? Player_Step() : 0;
    a->playLoop = true;
    Player_Play(a->ed.orderPos, step, a->ed.pattern);
}

/* Plays the choice on trial from where it is best heard: Fill and Chords
 * loop their pattern; Vary plays along the order from the source's last bar
 * into the new pattern, and Grow the new arrangement from the top, since
 * what they make is the way one pattern leads into the next. */
static void AuditionPick(App *a) {
    int pos, step;
    if (Pick_Audition(&a->pick, &a->ed, &pos, &step)) {
        a->playLoop = false;
        Player_Play(pos, step, -1);
    } else {
        LoopPattern(a);
    }
}

/* Whether what is playing reaches the choice on trial by itself: Vary while
 * the new pattern or the bar leading into it plays, Grow while the playhead
 * is inside the (possibly shorter) new order. Fill and Chords loop their
 * pattern, so always. */
static bool HearingPick(const App *a) {
    const Pick *pk = &a->pick;
    int pos = Player_OrderPos();
    if (pk->kind == PICK_VARY) return pos == pk->orderPos + 1 || (pos == pk->orderPos && Player_Step() >= SONG_STEPS - 16);
    if (pk->kind == PICK_GROW) return pos < a->ed.song.orderCount;
    return true;
}

/* Every picker call goes through here, as every edit goes through Apply:
 * the picker writes the song behind Editor_Apply's back, so a running
 * pattern loop is re-pointed here when the pattern count changes (Vary,
 * Grow and putting either back all change it). A new Vary or Grow choice
 * whose part the playhead has already gone past is played again from the
 * lead-in, so every choice is heard. */
static void PickWrap(App *a, PickOp op, int arg) {
    Pick *pk = &a->pick;
    Editor *ed = &a->ed;
    int before = ed->song.patternCount;
    switch (op) {
        case PICK_OP_MOVE: Pick_Move(pk, ed, arg); break;
        case PICK_OP_REROLL: Pick_Reroll(pk, ed); break; /* says so itself when a choice has one take */
        case PICK_OP_KEY: Pick_KeyStep(pk, ed, arg); break;
        case PICK_OP_TOGGLE: Pick_Toggle(pk, ed); break;
        case PICK_OP_KEEP: Pick_Keep(pk, ed, false); break;
        case PICK_OP_KEEP_ALL: Pick_Keep(pk, ed, true); break;
        case PICK_OP_CANCEL: Pick_Cancel(pk, ed); break;
    }
    RepointLoop(a, before);
    if ((op == PICK_OP_MOVE || op == PICK_OP_REROLL) && Player_IsPlaying() && !HearingPick(a)) AuditionPick(a);
}

/* Opens a picker on the cursor's pattern. You choose by ear, so playback
 * goes where the choice is heard: Fill and Chords loop the pattern unless it
 * already loops (nothing playing, or the order carrying playback through
 * other patterns, would leave the choices unheard and the grid, which
 * follows the order, showing another pattern); Vary and Grow always start
 * from their lead-in, as their song has just changed shape under the
 * playhead. */
static void BeginPick(App *a, PickKind kind) {
    int before = a->ed.song.patternCount;
    a->pick.seed = FreshSeed();
    if (!Pick_Begin(&a->pick, &a->ed, kind)) {
        Sfx_Jingle(JINGLE_WARNING);
        return;
    }
    if (kind == PICK_VARY || kind == PICK_GROW) AuditionPick(a);
    else if (!Player_IsPlaying() || !a->playLoop || Player_Pattern() != a->ed.pattern) LoopPattern(a);
    else RepointLoop(a, before);
}

/* F4: in-key typing on or off. Turning it on needs a key, so a song without
 * one has its key guessed from its notes and stored (metadata, not an edit:
 * no undo entry); with no notes to guess from it stays off and says why. */
static void ToggleScaleKeys(App *a) {
    if (a->scaleKeys) {
        a->scaleKeys = false;
        Status(a, "All notes");
        return;
    }
    if (!KeyMode_EnsureKey(&a->ed)) {
        Status(a, "No notes to guess a key from: type a few, then F4");
        Sfx_Jingle(JINGLE_WARNING);
        return;
    }
    a->scaleKeys = true;
    ComposeKey k = Compose_SongKey(&a->ed.song, NULL);
    char key[32];
    Song_KeyName(k.root, k.scale, key, sizeof(key));
    Status(a, "In key, %s: Z..M play the scale", key);
}

/* Applies a typed chord line through the open Chords picker, or keeps the
 * prompt open with the reason it was refused where its hints were. */
static void FinishChordLine(App *a) {
    if (a->pick.kind != PICK_CHORDS) { a->state = APP_EDITOR; return; }
    ComposeChords c;
    char err[64];
    if (!Compose_ParseChordLine(a->nameBuf, a->pick.key, &c, err, sizeof(err))) {
        snprintf(a->nameError, sizeof(a->nameError), "%s", err);
        Sfx_Jingle(JINGLE_WARNING);
        return;
    }
    int before = a->ed.song.patternCount;
    Pick_SetChords(&a->pick, &a->ed, &c);
    RepointLoop(a, before);
    a->nameError[0] = '\0';
    a->state = APP_EDITOR;
}

/* The chord prompt's hint: numbers and names, with the example spelt in the
 * key being tried so the names shown are ones the parser accepts. */
static void ChordLineHint(const App *a, char *out, size_t size) {
    static const int kExample[4] = {0, 5, 3, 4}; /* 1 6 4 5 */
    char names[48] = "", key[32];
    for (int b = 0; b < 4; b++) {
        char name[16];
        Compose_ChordName(a->pick.key, kExample[b], name, sizeof(name));
        size_t len = strlen(names);
        snprintf(names + len, sizeof(names) - len, "%s%s", b ? " " : "", name);
    }
    Song_KeyName(a->pick.key.root, a->pick.key.scale, key, sizeof(key));
    snprintf(out, size, "Like 1 6 4 5 or %s in %s    Enter tries it    Esc back", names, key);
}

/* The picker is modal: only its own keys and Space do anything, so a stray
 * note cannot land in a pattern being previewed (Keep would fold it into the
 * same undo entry, and Esc would throw it away). Returns true when the state
 * changed, like EditorInput. */
static bool PickInput(App *a) {
    Pick *pk = &a->pick;
    bool ctrl = Ctrl(), shift = Shift(), alt = Alt();
    if (IsKeyPressed(KEY_ESCAPE)) { PickWrap(a, PICK_OP_CANCEL, 0); return false; }
    if (!alt && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
        PickWrap(a, shift ? PICK_OP_KEEP_ALL : PICK_OP_KEEP, 0);
        return false;
    }
    if (Nav(KEY_LEFT)) PickWrap(a, PICK_OP_MOVE, -1);
    if (Nav(KEY_RIGHT)) PickWrap(a, PICK_OP_MOVE, 1);
    if (pk->kind != PICK_CHORDS && !ctrl && IsKeyPressed(KEY_R)) PickWrap(a, PICK_OP_REROLL, 0);
    if (pk->kind == PICK_CHORDS) {
        if (Nav(KEY_UP)) PickWrap(a, PICK_OP_KEY, 1);
        if (Nav(KEY_DOWN)) PickWrap(a, PICK_OP_KEY, -1);
        if (!ctrl && IsKeyPressed(KEY_F)) PickWrap(a, PICK_OP_TOGGLE, 0);
        if (!ctrl && IsKeyPressed(KEY_SLASH)) { BeginName(a, NAME_CHORD_LINE, APP_EDITOR); return true; }
    }
    /* Space stops, and starts again from where the choice is heard: the
     * pattern's loop for Fill and Chords (so playback never wanders off
     * along the order), the lead-in for Vary and Grow. */
    if (IsKeyPressed(KEY_SPACE)) {
        if (Player_IsPlaying()) Player_Stop();
        else AuditionPick(a);
    }
    return false;
}

/* --- playing along (F10) ---------------------------------------------------- */

static const char *TrackWord(int track) {
    static const char *const kWords[SONG_TRACKS] = {"bass", "arp", "lead", "drums"};
    return (track >= 0 && track < SONG_TRACKS) ? kWords[track] : "track";
}

/* F10: loops the cursor's pattern from its last bar (the count-in, with a
 * click on every beat) and records along with it. Every press is heard at
 * once through the preview voice as well as written into the take. */
static void BeginRecording(App *a) {
    Editor *ed = &a->ed;
    if (ed->pattern < 0 || ed->pattern >= ed->song.patternCount) return;
    Jam_Begin(&a->jam, ed, a->catchGrid);
    JamClock_Reset(&a->jamClock);
    for (int i = 0; i < PIANO_KEYS; i++) { a->recKeyNote[i] = NOTE_NONE; a->recKeyEvent[i] = -1; }
    a->playLoop = true;
    a->recStartedAt = Player_Now();
    Player_SetClick(a->clickOn);
    Player_Play(ed->orderPos, JAM_START_STEP, ed->pattern);
    Status(a, "Recording the %s: play along after the count-in", TrackWord(ed->track));
}

/* F10, Esc, Space, or anything that takes the song away: the pass being
 * played is kept (its notes, as one more undo entry) and the loop plays on
 * without the click, so you hear what you recorded. Nothing when not
 * recording. */
static void EndRecording(App *a) {
    if (!a->jam.active) return;
    Jam_End(&a->jam, &a->ed);
    Player_SetClick(false);
    int kept = a->jam.kept;
    if (kept > 0) Status(a, "Stopped: %d pass%s kept. Ctrl+Z takes back one at a time", kept, kept == 1 ? "" : "es");
    else Status(a, "Stopped: nothing was recorded");
}

/* The position you are hearing, from the audio thread's last reading less the
 * Timing setting, moves the session on (and writes a pass as the loop comes
 * round). Once a frame, before any key is read, so a press is timed at this
 * frame's position. */
static void RecordTick(App *a) {
    PlayerClock pc = Player_Clock();
    double now = Player_Now();
    if (pc.current) JamClock_Feed(&a->jamClock, pc.gen, pc.seconds, pc.at, now);
    double heard = JamClock_Seconds(&a->jamClock, now, (double)a->ini.timingMs / 1000.0);
    Jam_Tick(&a->jam, &a->ed, Jam_StepAt(&a->ed.song, a->jam.startStep, heard));
}

static void SetCatchGrid(App *a, int grid) {
    static const char *const kWhat[5] = {"", "sixteenth", "eighth", "", "beat"};
    a->catchGrid = grid;
    a->jam.grid = grid;
    Status(a, "Catch %s: notes land on the nearest %s", Jam_GridName(grid), kWhat[grid == 1 || grid == 4 ? grid : 2]);
}

/* Recording is modal like the picker: the piano (or the pads), Tab, the
 * octave, F4, F12, Ctrl+M, Backspace and the ways out. Edits that would move
 * the beat under your fingers (tempo, swing) refuse with a reason; everything
 * else is quiet, so a stray key cannot land outside the take. */
static bool RecordInput(App *a) {
    Editor *ed = &a->ed;
    RecordTick(a);
    if (!Player_IsPlaying()) { EndRecording(a); return false; } /* the loop stopped under us: keep what was played */
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_F10)) { EndRecording(a); return false; }
    if (IsKeyPressed(KEY_SPACE)) { EndRecording(a); Player_Stop(); return false; }
    /* Without a sound device the loop never plays and the count-in would
     * wait for ever: say why instead. */
    if (!a->jamClock.anchored && Player_Now() - a->recStartedAt > CAL_NO_SOUND) {
        EndRecording(a);
        Player_Stop();
        Status(a, "No sound is playing, so there is nothing to play along to");
        Sfx_Jingle(JINGLE_WARNING);
        return false;
    }

    /* Releases first, for every key, whatever the track is now: a note held
     * across a Tab ends on the track it was played on. */
    for (int i = 0; i < PIANO_KEYS; i++) {
        if (a->recKeyNote[i] == NOTE_NONE || !IsKeyReleased(kPiano[i].key)) continue;
        Jam_NoteOffId(&a->jam, a->recKeyEvent[i]);
        a->recKeyNote[i] = NOTE_NONE;
        a->recKeyEvent[i] = -1;
    }

    bool ctrl = Ctrl(), shift = Shift();
    if (ctrl) {
        if (IsKeyPressed(KEY_M)) {
            a->clickOn = !a->clickOn;
            Player_SetClick(a->clickOn);
            Status(a, a->clickOn ? "Click on" : "Click off: the song keeps the time");
        }
        if (IsKeyPressed(KEY_Z)) Status(a, "Stop recording to undo (Backspace drops this pass)");
        return false; /* with Ctrl held the piano stays quiet */
    }
    if (IsKeyPressed(KEY_F12)) SetCatchGrid(a, Jam_NextGrid(a->catchGrid));
    if (IsKeyPressed(KEY_F4)) ToggleScaleKeys(a);
    if (Nav(KEY_BACKSPACE)) Status(a, Jam_DropPass(&a->jam) > 0 ? "Pass dropped" : "Nothing played in this pass yet");
    if (IsKeyPressed(KEY_TAB)) {
        Apply(a, shift ? ED_LEFT : ED_RIGHT, 0);
        Jam_SetTrack(&a->jam, ed->track);
        Status(a, "Recording the %s", TrackWord(ed->track));
    }
    if (IsKeyPressed(KEY_EQUAL)) Apply(a, ED_OCTAVE_UP, 0);
    if (IsKeyPressed(KEY_MINUS)) Apply(a, ED_OCTAVE_DOWN, 0);
    if (Nav(KEY_LEFT_BRACKET) || Nav(KEY_RIGHT_BRACKET)) Status(a, "Stop recording to change the tempo");
    if (Nav(KEY_COMMA) || Nav(KEY_PERIOD)) Status(a, "Stop recording to change the swing");

    if (ed->track == TRACK_DRUMS) {
        int bits = PadsPressed();
        for (int bit = DRUM_KICK; bit <= DRUM_HAT; bit <<= 1) {
            if (!(bits & bit)) continue;
            if (Jam_NoteOn(&a->jam, bit) < 0) Status(a, "This pass is full: Backspace drops it");
            Player_Preview(TRACK_DRUMS, bit);
        }
        return false;
    }
    for (int i = 0; i < PIANO_KEYS; i++) {
        if (!IsKeyPressed(kPiano[i].key)) continue;
        int note = PianoNote(a, i);
        if (note == NOTE_NONE) continue;
        int ev = Jam_NoteOn(&a->jam, note);
        if (ev < 0) Status(a, "This pass is full: Backspace drops it");
        a->recKeyNote[i] = note;
        a->recKeyEvent[i] = ev;
        Player_Preview(ed->track, note);
    }
    return false;
}

/* The key-hint card while recording: the piano rows in their current
 * meaning, or the pads, then the recording's own keys. 13 glyphs a line. */
static const char *const *RecordHintCard(const App *a) {
    static const char *const kPianoCard[] = {
        "Z..M notes", "Q..P upper", "Hold for long", "=/- octave", "Tab track", "F12 catch",
        "Ctrl+M click", "Bksp drop", "F10 stop", "Space stop", NULL,
    };
    static const char *const kInKeyCard[] = {
        "Z..M in key", "Q..P up", "Hold for long", "=/- octave", "Tab track", "F12 catch",
        "Ctrl+M click", "Bksp drop", "F10 stop", "Space stop", NULL,
    };
    static const char *const kPadCard[] = {
        "Z/K kick", "X/S snare", "C/H hat", "Tab track", "F12 catch",
        "Ctrl+M click", "Bksp drop", "F10 stop", "Space stop", NULL,
    };
    if (a->ed.track == TRACK_DRUMS) return kPadCard;
    return a->scaleKeys ? kInKeyCard : kPianoCard;
}

/* Settings > Timing, Enter: the click test. It plays through the player
 * (the song is stopped; Settings is only reached from the menu) so the clicks
 * take the very path the music does, and its clock is read the way a
 * recording reads it: the offset of your taps is exactly what a recording
 * has to take back. */
static void BeginCalibration(App *a) {
    a->calPhase = CALIBRATE_RUNNING;
    a->calTapCount = 0;
    a->calResult = -1;
    JamClock_Reset(&a->calClock);
    a->calStartedAt = Player_Now();
    Player_ClickTest(CAL_BPM, CAL_BEATS);
    a->state = APP_CALIBRATE;
}

/* One frame of the tracker screen as it stands: the picker band while one is
 * open, the recording band while playing along, else the status strip. */
static void DrawEditor(App *a) {
    RenderPlayback pb = {
        .playing = Player_IsPlaying(), .looping = a->playLoop && Player_IsPlaying(),
        .orderPos = Player_OrderPos(), .step = Player_Step(), .pattern = Player_Pattern(),
    };
    RenderConfirm unsaved = {"Unsaved changes", "Enter saves    D discards    Esc stays"};
    RenderExtras x = {.pickLabel = NULL, .scaleKeys = a->scaleKeys};
    char pickLabel[PICK_TEXT_LEN], pickInfo[PICK_TEXT_LEN], pickDetail[PICK_TEXT_LEN];
    char recLabel[96], recDetail[160];
    static SongPattern livePass; /* static: drawn from, every frame, while recording */
    if (a->jam.active) {
        const Jam *j = &a->jam;
        snprintf(recLabel, sizeof(recLabel), "Recording the %s, pass %d", TrackWord(a->ed.track), j->pass);
        snprintf(recDetail, sizeof(recDetail), "F12 catch %s    Ctrl+M click %s    Backspace drops this pass    Tab track    F10 stops",
                 Jam_GridName(j->grid), a->clickOn ? "on" : "off");
        x.recPass = j->pass;
        x.countIn = Jam_CountIn(j);
        x.catchLabel = Jam_GridName(j->grid);
        x.livePass = Jam_LivePass(j, &livePass) ? &livePass : NULL;
        x.livePattern = j->pattern;
        x.pickLabel = recLabel;
        x.pickDetail = recDetail;
        x.hintCard = RecordHintCard(a);
    } else if (a->pick.kind != PICK_NONE) {
        Pick_Label(&a->pick, pickLabel, sizeof(pickLabel));
        Pick_Info(&a->pick, pickInfo, sizeof(pickInfo));
        Pick_Detail(&a->pick, pickDetail, sizeof(pickDetail));
        x.pickLabel = pickLabel;
        x.pickInfo = pickInfo;
        x.pickDetail = pickDetail;
        x.hintCard = Pick_Keys(&a->pick);
        x.chordKey = &a->pick.key; /* the grid names chords in the key being tried */
    }
    Render_Editor(&a->ed, &pb, a->confirm == CONFIRM_UNSAVED ? &unsaved : NULL, &x);
}

/* Returns true when the state changed, so the caller draws the new state
 * next frame instead of letting the same key press act twice. */
static bool EditorInput(App *a) {
    Editor *ed = &a->ed;
    bool ctrl = Ctrl(), shift = Shift(), alt = Alt();

    if (a->confirm == CONFIRM_UNSAVED) {
        if (IsKeyPressed(KEY_ESCAPE)) { a->confirm = CONFIRM_NONE; a->quitAfterLeave = false; } /* stay, and stay open */
        else if (IsKeyPressed(KEY_D)) { a->confirm = CONFIRM_NONE; ed->dirty = false; EnterMenu(a); return true; }
        else if (!alt && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
            a->confirm = CONFIRM_NONE;
            if (ed->fileName[0]) {
                /* A failed save leaves the user in the editor, so the close
                 * request behind this box must not fire on the next ordinary
                 * trip to the menu. */
                if (SaveEditor(a)) EnterMenu(a); else a->quitAfterLeave = false;
            } else {
                BeginName(a, NAME_LEAVE, APP_EDITOR);
            }
            return true;
        }
        return false; /* nothing else while asking */
    }

    /* Before Escape below: with a picker open, Esc puts the pattern back
     * instead of stopping or asking to leave; while recording, Esc ends the
     * take before a second one stops the loop. */
    if (a->pick.kind != PICK_NONE) return PickInput(a);
    if (a->jam.active) return RecordInput(a);

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (Player_IsPlaying()) { Player_Stop(); return false; }
        if (ed->dirty) { a->confirm = CONFIRM_UNSAVED; return false; }
        EnterMenu(a);
        return true;
    }
    if (IsKeyPressed(KEY_F1)) { a->howtoReturn = APP_EDITOR; a->state = APP_HOWTO; return true; }
    if (IsKeyPressed(KEY_F2)) { BeginName(a, NAME_RENAME, APP_EDITOR); return true; }
    if (IsKeyPressed(KEY_F9)) { BeginPick(a, PICK_GROW); return false; }
    if (IsKeyPressed(KEY_F10)) { BeginRecording(a); return false; }
    /* F3 brightens the mood, Shift+F3 darkens it; each is one undo entry and
     * names the new mood on the status line, since F3 sits next to F2 rename. */
    if (IsKeyPressed(KEY_F3)) KeyMode_ChangeMood(ed, shift ? -1 : 1);
    if (IsKeyPressed(KEY_F4)) ToggleScaleKeys(a);

    if (ctrl) {
        if (IsKeyPressed(KEY_F)) { BeginPick(a, PICK_FILL); return false; }
        if (IsKeyPressed(KEY_R)) { BeginPick(a, PICK_VARY); return false; }
        if (IsKeyPressed(KEY_K) && !shift) { BeginPick(a, PICK_CHORDS); return false; }
        /* Fit to key: every stray note to the nearest in-key one, one undo
         * entry; Ctrl+K alone is Chords. */
        if (IsKeyPressed(KEY_K) && shift) KeyMode_FitToKey(ed);
        if (IsKeyPressed(KEY_S)) {
            if (ed->fileName[0]) SaveEditor(a);
            else BeginName(a, NAME_SAVE, APP_EDITOR);
            return true;
        }
        if (IsKeyPressed(KEY_G)) { EnterAssign(a); return true; }
        /* ExportWav draws its own frame, so this one ends here: the next
         * reads the keys afresh. */
        if (IsKeyPressed(KEY_E)) { ExportWav(a); return true; }
        if (IsKeyPressed(KEY_Z)) Apply(a, ED_UNDO, 0);
        if (IsKeyPressed(KEY_N)) Apply(a, ED_PATTERN_NEW, 0);
        if (IsKeyPressed(KEY_D)) Apply(a, ED_PATTERN_DUPLICATE, 0);
        if (IsKeyPressed(KEY_DELETE)) Apply(a, ED_PATTERN_DELETE, 0);
        if (IsKeyPressed(KEY_BACKSPACE)) Apply(a, ED_PATTERN_CLEAR_TRACK, 0);
        /* Ctrl+Shift+Up/Down picks the pattern this order slot plays, so the
         * octave transpose takes Alt instead of Shift. */
        if (Nav(KEY_UP)) {
            if (shift) Apply(a, ED_ORDER_SET_UP, 0); else Apply(a, ED_TRANSPOSE_UP, alt ? 12 : 1);
        }
        if (Nav(KEY_DOWN)) {
            if (shift) Apply(a, ED_ORDER_SET_DOWN, 0); else Apply(a, ED_TRANSPOSE_DOWN, alt ? 12 : 1);
        }
        if (Nav(KEY_LEFT)) Apply(a, ED_ORDER_PREV, 0);
        if (Nav(KEY_RIGHT)) Apply(a, ED_ORDER_NEXT, 0);
        if (Nav(KEY_PAGE_UP)) Apply(a, ED_PATTERN_PREV, 0);
        if (Nav(KEY_PAGE_DOWN)) Apply(a, ED_PATTERN_NEXT, 0);
        if (IsKeyPressed(KEY_LEFT_BRACKET)) Apply(a, ED_EDIT_STEP_DOWN, 0);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) Apply(a, ED_EDIT_STEP_UP, 0);
        return false; /* with Ctrl held the piano and the plain keys stay quiet */
    }

    if (Nav(KEY_UP)) Apply(a, ED_UP, 0);
    if (Nav(KEY_DOWN)) Apply(a, ED_DOWN, 0);
    if (Nav(KEY_LEFT)) Apply(a, ED_LEFT, 0);
    if (Nav(KEY_RIGHT)) Apply(a, ED_RIGHT, 0);
    if (Nav(KEY_PAGE_UP)) Apply(a, ED_PAGE_UP, 0);
    if (Nav(KEY_PAGE_DOWN)) Apply(a, ED_PAGE_DOWN, 0);
    if (IsKeyPressed(KEY_HOME)) Apply(a, ED_HOME, 0);
    if (IsKeyPressed(KEY_END)) Apply(a, ED_END, 0);
    if (IsKeyPressed(KEY_TAB)) Apply(a, shift ? ED_LEFT : ED_RIGHT, 0);

    if (IsKeyPressed(KEY_SPACE)) {
        if (Player_IsPlaying()) {
            Player_Stop();
        } else {
            a->playLoop = shift;
            Player_Play(ed->orderPos, 0, shift ? ed->pattern : -1);
        }
    }
    if (!alt && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
        a->playLoop = false;
        Player_Play(ed->orderPos, ed->step, -1);
    }

    if (IsKeyPressed(KEY_DELETE)) Apply(a, shift ? ED_ORDER_REMOVE : ED_CLEAR, 0);
    if (Nav(KEY_BACKSPACE)) Apply(a, ED_CLEAR_BACK, 0);
    if (IsKeyPressed(KEY_INSERT)) Apply(a, ED_ORDER_INSERT, 0);
    if (IsKeyPressed(KEY_APOSTROPHE)) Apply(a, ED_HOLD, 0);
    if (IsKeyPressed(KEY_EQUAL)) Apply(a, ED_OCTAVE_UP, 0);
    if (IsKeyPressed(KEY_MINUS)) Apply(a, ED_OCTAVE_DOWN, 0);
    if (Nav(KEY_LEFT_BRACKET)) Apply(a, ED_BPM_DOWN, shift ? 10 : 1);
    if (Nav(KEY_RIGHT_BRACKET)) Apply(a, ED_BPM_UP, shift ? 10 : 1);
    if (Nav(KEY_COMMA)) Apply(a, ED_SWING_DOWN, 0);
    if (Nav(KEY_PERIOD)) Apply(a, ED_SWING_UP, 0);
    if (IsKeyPressed(KEY_F5)) Apply(a, ED_WAVE_PREV, 0);
    if (IsKeyPressed(KEY_F6)) Apply(a, ED_WAVE_NEXT, 0);
    if (Nav(KEY_F7)) Apply(a, ED_VOLUME_DOWN, 0);
    if (Nav(KEY_F8)) Apply(a, ED_VOLUME_UP, 0);

    if (ed->track == TRACK_DRUMS) {
        /* The lower piano row doubles as drum pads. */
        int bits = PadsPressed();
        for (int bit = DRUM_KICK; bit <= DRUM_HAT; bit <<= 1) {
            if (!(bits & bit)) continue;
            Apply(a, ED_NOTE, bit);
            Player_Preview(TRACK_DRUMS, bit);
        }
    } else {
        for (int i = 0; i < PIANO_KEYS; i++) {
            if (!IsKeyPressed(kPiano[i].key)) continue;
            int note = PianoNote(a, i);
            if (note == NOTE_NONE) continue;
            Apply(a, ED_NOTE, note);
            Player_Preview(ed->track, note);
        }
    }
    return false;
}

/* --- main ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    App *a = &sApp;
    Editor_Init(&a->ed);

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Chipforge");
    SetExitKey(KEY_NULL); /* Escape is ours */
    SetTargetFPS(60);

    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    snprintf(a->iconPath, sizeof(a->iconPath), "%s/icons/chipforge.png", assetsDir);

    UiTheme theme = Ui_DefaultTheme((Color){40, 220, 190, 255});
    Ui_Init(assetsDir, &theme, WINDOW_WIDTH, WINDOW_HEIGHT);
    Win_Init(WINDOW_WIDTH, WINDOW_HEIGHT);
    Sfx_Init(assetsDir, NULL, 0); /* no sound files; the device and the jingles */
    Player_Init(&a->ed.song);
    Player_SetVolume(Prefs_Get()->musicVolume);
    AppIni_Load(&a->ini);
    a->catchGrid = 2;  /* 1/8: forgiving enough for a first try at playing along */
    a->clickOn = true;

    a->state = HowTo_Seen(APP_SLUG) ? APP_MENU : APP_HOWTO;
    a->howtoReturn = APP_MENU;
    RefreshMenuStats(a);
    if (argc > 1) OpenAnyPath(a, argv[1]); /* a .gsong opens, a recording imports */

    static const char *const kMenuItems[] = {
        "New song", "Open song", "Import a recording", "Game themes", "Settings", "How to play", "Updates", "Quit",
    };
    enum { MENU_NEW, MENU_OPEN, MENU_IMPORT, MENU_THEMES, MENU_SETTINGS, MENU_HOWTO, MENU_UPDATES, MENU_QUIT, MENU_COUNT };
    static const char *const kSlugHowTo = APP_SLUG;

    /* raylib only moves key state forward inside EndDrawing. A state change
     * below `continue`s to the next iteration without drawing, so the key
     * that caused it would still read as pressed there (Enter on "How to
     * play" bounced between the menu and the card forever; Escape from any
     * screen fell through the menu and quit; Enter on a folder opened the
     * first entry inside it). Whenever the last iteration presented no frame,
     * poll once so that press is retired before anything reads input. */
    unsigned long framesSeen = Win_FrameCount();
    bool firstPass = true;

    for (;;) {
        /* Before the close check: each poll replaces raylib's close flag, so
         * a request this poll picks up must be read before the frame below
         * polls again, or it is lost. */
        if (!firstPass && Win_FrameCount() == framesSeen) PollInputEvents();
        firstPass = false;
        framesSeen = Win_FrameCount();
        /* A close request (window button, compositor) with unsaved work asks
         * first, like every other way out; the answer then quits. */
        if (WindowShouldClose()) {
            /* A pending "Delete song?" is not unsaved work: closing answers it
             * with "keep it", and then the song alone decides. */
            if (a->confirm == CONFIRM_DELETE_SONG) a->confirm = CONFIRM_NONE;
            /* A take in progress is kept (it is what was played), then the
             * song is asked about like any other unsaved work. */
            EndRecording(a);
            /* A preview is not the song: put it back before anything asks
             * about saving it (or quits without asking). */
            Pick_Cancel(&a->pick, &a->ed);
            if (!a->ed.dirty) break;
            if (!a->quitAfterLeave) {
                a->quitAfterLeave = true;
                Player_Stop();
                a->state = APP_EDITOR;
                a->confirm = CONFIRM_UNSAVED;
            }
        }
        if (a->quitAfterLeave && a->state == APP_MENU) break; /* saved or discarded */
        Win_Update();
        float dt = GetFrameTime();
        Editor_Tick(&a->ed, dt);
        HandleDrop(a);
        /* Recording only lives in the editor; every way out ends it there,
         * and this keeps a take from outliving a screen change it missed. */
        if (a->jam.active && a->state != APP_EDITOR) EndRecording(a);
        /* The recipe screen lowers the player's intensity to audition what a
         * game plays (and keeps it under a failure page for a dropped song,
         * whose Esc goes back to it); everywhere else the song plays in full.
         * Re-asserting it every frame covers every way out, including a file
         * dropped onto the window, which leaves without passing through the
         * screen's keys. */
        bool auditioning = a->state == APP_RECIPE || (a->state == APP_IMPORT && a->importSongFile && a->importReturn == APP_RECIPE);
        Player_SetIntensity(auditioning ? Recipe_Intensity(&a->recipe) : 1.0f);

        bool upPressed = Nav(KEY_UP), downPressed = Nav(KEY_DOWN);
        bool leftPressed = Nav(KEY_LEFT), rightPressed = Nav(KEY_RIGHT);
        /* Alt+Enter is winscale's fullscreen toggle, not a confirm. */
        bool confirmPressed = !Alt() && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
        bool escPressed = IsKeyPressed(KEY_ESCAPE);

        if (a->state == APP_MENU) {
            if (upPressed) a->menuIndex = (a->menuIndex + MENU_COUNT - 1) % MENU_COUNT;
            if (downPressed) a->menuIndex = (a->menuIndex + 1) % MENU_COUNT;
            if (escPressed) break;
            if (confirmPressed) {
                switch (a->menuIndex) {
                    case MENU_NEW:
                        /* A song to start from, already playing; a blank one
                         * is a row on that screen. */
                        Recipe_Random(&a->recipe, FreshSeed());
                        EnterRecipe(a, "New song", APP_MENU);
                        continue;
                    case MENU_OPEN:
                        RefreshSongList(a);
                        a->confirm = CONFIRM_NONE;
                        a->state = APP_BROWSE_SONGS;
                        continue;
                    case MENU_IMPORT:
                        OpenAudioBrowser(a);
                        continue;
                    case MENU_THEMES:
                        RefreshPresets(a);
                        a->state = APP_PRESETS;
                        continue;
                    case MENU_SETTINGS:
                        a->settingsRow = 0;
                        a->state = APP_SETTINGS;
                        continue;
                    case MENU_HOWTO:
                        a->howtoReturn = APP_MENU;
                        a->state = APP_HOWTO;
                        continue;
                    case MENU_UPDATES:
                        a->updatesScroll = 0;
                        a->state = APP_UPDATES;
                        continue;
                    default:
                        goto quit;
                }
            }
            UiMenu menu = {
                .title = "CHIPFORGE", .tagline = "Make the music your games play",
                .items = kMenuItems, .itemCount = MENU_COUNT, .selected = a->menuIndex,
                .statLine = a->statLine, .username = NULL, .hints = "Up/Down choose    Enter select",
                .iconPath = a->iconPath, .versionLabel = "v" CHIPFORGE_VERSION, .backdrop = NULL,
            };
            Ui_DrawMenu(&menu, dt);
            continue;
        }

        if (a->state == APP_HOWTO) {
            if (escPressed || confirmPressed) {
                HowTo_MarkSeen(kSlugHowTo);
                a->state = a->howtoReturn;
                if (a->state == APP_MENU) RefreshMenuStats(a);
                continue;
            }
            const HowTo *h = HowTo_ForSlug(kSlugHowTo);
            Ui_DrawHowTo(h ? h : &kFallbackHowTo);
            continue;
        }

        if (a->state == APP_UPDATES) {
            if (escPressed) { a->state = APP_MENU; continue; }
            if (upPressed) a->updatesScroll -= 48;
            if (downPressed) a->updatesScroll += 48;
            Ui_DrawUpdates(kChangelog, (int)(sizeof(kChangelog) / sizeof(kChangelog[0])), &a->updatesScroll);
            continue;
        }

        if (a->state == APP_SETTINGS) {
            /* Chipforge's own Timing row, then the arcade-wide rows, then
             * Done: Ui_DrawSettingsPlus slots the shared rows in before the
             * last row it is given, so they sit at 1..PREFS_ROWS here. */
            enum { SETTINGS_TIMING = 0, SETTINGS_SHARED = 1, SETTINGS_DONE = 1 + PREFS_ROWS, SETTINGS_TOTAL };
            if (upPressed) a->settingsRow = (a->settingsRow + SETTINGS_TOTAL - 1) % SETTINGS_TOTAL;
            if (downPressed) a->settingsRow = (a->settingsRow + 1) % SETTINGS_TOTAL;
            if (escPressed) { a->state = APP_MENU; continue; }
            if (a->settingsRow == SETTINGS_TIMING) {
                int dir = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
                if (dir != 0) {
                    int ms = AppIni_ClampTiming(a->ini.timingMs + dir * APPINI_TIMING_STEP);
                    if (ms != a->ini.timingMs) {
                        a->ini.timingMs = ms;
                        AppIni_Save(&a->ini);
                        Sfx_Jingle(JINGLE_COIN);
                    }
                }
                if (confirmPressed) { BeginCalibration(a); continue; }
            } else if (a->settingsRow < SETTINGS_DONE) {
                if (Prefs_HandleRow(a->settingsRow - SETTINGS_SHARED, leftPressed, rightPressed, confirmPressed)) {
                    Player_SetVolume(Prefs_Get()->musicVolume);
                    Sfx_Jingle(JINGLE_COIN);
                }
            } else if (confirmPressed) {
                a->state = APP_MENU;
                continue;
            }
            char timing[24];
            snprintf(timing, sizeof(timing), "%d ms", a->ini.timingMs);
            UiSettingsRow rows[2] = {
                {UI_ROW_CHOICE, "Timing (Enter tests it)", timing, false, 0},
                {UI_ROW_DONE, "Done", NULL, false, 0},
            };
            Ui_DrawSettingsPlus(rows, 2, a->settingsRow, false);
            continue;
        }

        if (a->state == APP_CALIBRATE) {
            const double beat = 60.0 / CAL_BPM;
            PlayerClock pc = Player_Clock();
            double now = Player_Now();
            if (pc.current) JamClock_Feed(&a->calClock, pc.gen, pc.seconds, pc.at, now);
            bool started = a->calClock.anchored;
            /* Seconds since the first click as the audio thread rendered it;
             * how much later you heard and tapped it is what is measured. */
            double t = JamClock_Seconds(&a->calClock, now, 0.0);
            if (escPressed) { Player_Stop(); a->state = APP_SETTINGS; continue; }
            if (a->calPhase == CALIBRATE_RUNNING) {
                if (IsKeyPressed(KEY_SPACE) && started && a->calTapCount < CAL_MAX_TAPS) a->calTaps[a->calTapCount++] = t;
                if (!started && now - a->calStartedAt > CAL_NO_SOUND) {
                    Player_Stop();
                    a->calPhase = CALIBRATE_NO_SOUND;
                } else if (started && t > (CAL_BEATS - 1) * beat + 0.6 * beat) {
                    /* Past the last click by more than any tap could be late. */
                    Player_Stop();
                    a->calResult = Jam_LatencyFromTaps(a->calTaps, a->calTapCount, beat, CAL_WARMUP, CAL_BEATS);
                    a->calPhase = a->calResult >= 0 ? CALIBRATE_DONE : CALIBRATE_FAILED;
                    Sfx_Jingle(a->calResult >= 0 ? JINGLE_LEVEL_UP : JINGLE_WARNING);
                }
            } else {
                if (IsKeyPressed(KEY_SPACE)) { BeginCalibration(a); continue; }
                if (confirmPressed && a->calPhase == CALIBRATE_DONE) {
                    a->ini.timingMs = AppIni_ClampTiming(a->calResult);
                    AppIni_Save(&a->ini);
                    Sfx_Jingle(JINGLE_CONFIRM);
                    a->state = APP_SETTINGS;
                    continue;
                }
            }
            RenderCalibrate rc = {
                .phase = a->calPhase, .beats = CAL_BEATS, .warmup = CAL_WARMUP, .beatSeconds = beat,
                .now = started ? t : -1.0, .taps = a->calTaps, .tapCount = a->calTapCount,
                .resultMs = a->calResult, .currentMs = a->ini.timingMs,
            };
            Render_Calibrate(&rc);
            continue;
        }

        if (a->state == APP_NAME) {
            if (escPressed) { a->quitAfterLeave = false; a->nameError[0] = '\0'; a->state = a->nameReturn; continue; }
            char before[SONG_NAME_LEN];
            snprintf(before, sizeof(before), "%s", a->nameBuf);
            HandleTextInput(a->nameBuf, sizeof(a->nameBuf));
            if (strcmp(before, a->nameBuf) != 0) a->nameError[0] = '\0'; /* typing clears the complaint */
            if (confirmPressed) { FinishName(a); continue; }
            const char *title = a->nameReason == NAME_RENAME ? "Rename the song" : "Name your song";
            char hint[160];
            snprintf(hint, sizeof(hint), "Type a name    Enter saves    Esc cancels");
            if (a->nameReason == NAME_CHORD_LINE) {
                title = "Chord line";
                ChordLineHint(a, hint, sizeof(hint));
            }
            Render_Prompt(title, a->nameBuf, a->nameError[0] ? a->nameError : hint);
            continue;
        }

        if (a->state == APP_EDITOR) {
            if (EditorInput(a)) continue;
            DrawEditor(a);
            continue;
        }

        if (a->state == APP_BROWSE_SONGS) {
            if (a->confirm == CONFIRM_DELETE_SONG) {
                if (IsKeyPressed(KEY_Y)) { a->confirm = CONFIRM_NONE; DeleteSelectedSong(a); }
                else if (escPressed || IsKeyPressed(KEY_N)) a->confirm = CONFIRM_NONE;
            } else {
                if (escPressed) { EnterMenu(a); continue; }
                if (a->songCount > 0) {
                    if (upPressed) a->songSel = (a->songSel + a->songCount - 1) % a->songCount;
                    if (downPressed) a->songSel = (a->songSel + 1) % a->songCount;
                    if (IsKeyPressed(KEY_DELETE)) {
                        snprintf(a->confirmTitle, sizeof(a->confirmTitle), "Delete %.60s?", a->songNames[a->songSel]);
                        a->confirm = CONFIRM_DELETE_SONG;
                    }
                    if (confirmPressed) {
                        char path[1024];
                        if (Song_LibraryPath(a->songNames[a->songSel], path, sizeof(path), false) && Song_Load(&a->scratch, path)) {
                            LoadIntoEditor(a, &a->scratch, a->songNames[a->songSel], false);
                            Status(a, "Opened %s", a->songNames[a->songSel]);
                        } else {
                            Sfx_Jingle(JINGLE_WARNING);
                        }
                        continue;
                    }
                }
            }
            RenderConfirm del = {a->confirmTitle, "Y deletes it for good    Esc keeps it"};
            RenderList list = {
                .title = "Open a song", .note = a->songNote[0] ? a->songNote : NULL, .rows = a->songRows, .right = a->songRightRows, .highlight = NULL,
                .count = a->songCount, .selected = a->songSel, .empty = "Nothing saved yet. New song or Game themes gets you started.",
                .hints = "Up/Down choose    Enter open    Delete remove    Esc back",
                .confirm = a->confirm == CONFIRM_DELETE_SONG ? &del : NULL,
            };
            Render_List(&list);
            continue;
        }

        if (a->state == APP_BROWSE_AUDIO) {
            int n = 0;
            const char *const *exts = Browser_AudioExts(&n);
            Browser *b = &a->browser;
            if (escPressed) { EnterMenu(a); continue; }
            if (upPressed) Browser_MoveSelection(b, -1);
            if (downPressed) Browser_MoveSelection(b, 1);
            if (Nav(KEY_PAGE_UP)) Browser_MoveSelection(b, -10);
            if (Nav(KEY_PAGE_DOWN)) Browser_MoveSelection(b, 10);
            if (IsKeyPressed(KEY_BACKSPACE)) { Browser_Up(b, exts, n); RefreshAudioRows(a); }
            if (confirmPressed && b->count > 0) {
                char path[BROWSER_PATH_LEN];
                if (Browser_Enter(b, exts, n, path, sizeof(path))) {
                    BeginImport(a, path, APP_BROWSE_AUDIO);
                    continue;
                }
                RefreshAudioRows(a); /* a folder was opened */
            }
            RenderList list = {
                .title = a->browserTitle, .note = b->truncated ? "Showing the first entries only; the folder is very full" : NULL,
                .rows = a->audioRows, .right = a->audioRight, .highlight = NULL,
                .count = b->count, .selected = b->selected, .empty = "No recordings here (WAV, OGG or MP3). Backspace goes up.",
                .hints = "Up/Down choose    Enter open    Backspace up a folder    Esc back", .confirm = NULL,
            };
            Render_List(&list);
            continue;
        }

        if (a->state == APP_IMPORT) {
            const char *name = GetFileName(a->importPath);
            RenderImport im = {
                .phase = a->importPhase, .fileName = name, .tempoBpm = TempoFromIdx(a->tempoIdx),
                .drums = a->drums, .chords = a->chords, .selected = a->importSel,
                .report = &a->report, .message = a->importMessage, .song = a->importSongFile,
            };
            if (a->importPhase == IMPORT_OPTIONS) {
                if (escPressed) {
                    a->state = a->importReturn;
                    if (a->state == APP_MENU) RefreshMenuStats(a);
                    continue;
                }
                if (upPressed) a->importSel = (a->importSel + 3) % 4;
                if (downPressed) a->importSel = (a->importSel + 1) % 4;
                int dir = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
                if (a->importSel == 0 && dir != 0) {
                    a->tempoIdx += dir;
                    if (a->tempoIdx < 0) a->tempoIdx = TEMPO_IDX_MAX;
                    if (a->tempoIdx > TEMPO_IDX_MAX) a->tempoIdx = 0;
                }
                if (a->importSel == 1 && (dir != 0 || confirmPressed)) a->drums = !a->drums;
                if (a->importSel == 2 && (dir != 0 || confirmPressed)) a->chords = !a->chords;
                if (a->importSel == 3 && confirmPressed) {
                    /* Show the listening frame first: decoding and transcribing
                     * block for a second or two and the window must not go blank. */
                    im.phase = IMPORT_LISTENING;
                    a->importPhase = IMPORT_LISTENING;
                    Render_Import(&im);
                    RunImport(a);
                    continue;
                }
                im.selected = a->importSel;
                im.tempoBpm = TempoFromIdx(a->tempoIdx);
                im.drums = a->drums;
                im.chords = a->chords;
            } else if (a->importPhase == IMPORT_REPORT) {
                if (escPressed) { a->importPhase = IMPORT_OPTIONS; continue; }
                if (confirmPressed) {
                    LoadIntoEditor(a, &a->importSong, "", true);
                    Status(a, "Imported %.40s. Tidy it up, then Ctrl+S saves", name);
                    continue;
                }
            } else if (a->importPhase == IMPORT_FAILED) {
                if (escPressed || confirmPressed) {
                    /* A song that would not open has no options to go back to. */
                    if (a->importSongFile) {
                        a->state = a->importReturn;
                        if (a->state == APP_MENU) RefreshMenuStats(a);
                    } else {
                        a->importPhase = IMPORT_OPTIONS;
                    }
                    continue;
                }
            }
            Render_Import(&im);
            continue;
        }

        if (a->state == APP_PRESETS) {
            if (escPressed) { EnterMenu(a); continue; }
            if (a->presetCount > 0) {
                if (upPressed) a->presetSel = (a->presetSel + a->presetCount - 1) % a->presetCount;
                if (downPressed) a->presetSel = (a->presetSel + 1) % a->presetCount;
                if (IsKeyPressed(KEY_SPACE)) {
                    /* The player streams ed.song, so auditioning means putting the
                     * theme there. Leaving the editor saved or discarded the song,
                     * so this only refuses if something slipped through. */
                    if (Player_IsPlaying()) {
                        Player_Stop();
                    } else if (a->ed.dirty) {
                        snprintf(a->presetNote, sizeof(a->presetNote), "Save or discard your song first");
                        Sfx_Jingle(JINGLE_WARNING);
                    } else {
                        ThemeToScratch(a, a->presetSel);
                        Editor_SetSong(&a->ed, &a->scratch, "");
                        a->playLoop = false;
                        Player_Play(0, 0, -1);
                    }
                }
                if (confirmPressed) {
                    ThemeToScratch(a, a->presetSel);
                    LoadIntoEditor(a, &a->scratch, "", true);
                    Status(a, "The %s theme, as a copy. Ctrl+S saves it under your own name", a->presetNames[a->presetSel]);
                    continue;
                }
                if (IsKeyPressed(KEY_R)) {
                    /* Make one like it: the recipe screen starting from the
                     * very recipe this theme is made of, so every row moves
                     * away from a song the user already likes. */
                    if (a->ed.dirty) {
                        snprintf(a->presetNote, sizeof(a->presetNote), "Save or discard your song first");
                        Sfx_Jingle(JINGLE_WARNING);
                    } else {
                        char name[SONG_NAME_LEN];
                        snprintf(name, sizeof(name), "%.32s remix", a->presetNames[a->presetSel]);
                        Recipe_FromTheme(&a->recipe, Music_ThemeAt(a->presetSel));
                        EnterRecipe(a, name, APP_PRESETS);
                        continue;
                    }
                }
            }
            RenderList list = {
                .title = "Game themes", .note = a->presetNote, .rows = a->presetRows, .right = NULL, .highlight = NULL,
                .count = a->presetCount, .selected = a->presetSel, .empty = "No themes built in",
                .hints = "Up/Down choose    Space audition    Enter open a copy    R make one like it    Esc back", .confirm = NULL,
            };
            Render_List(&list);
            continue;
        }

        if (a->state == APP_RECIPE) {
            Recipe *r = &a->recipe;
            if (escPressed) {
                Player_SetIntensity(1.0f);
                if (a->recipeReturn == APP_PRESETS) {
                    Player_Stop();
                    RefreshPresets(a);
                    a->state = APP_PRESETS;
                } else {
                    EnterMenu(a);
                }
                continue;
            }
            if (upPressed) a->recipeRow = (a->recipeRow + RECIPE_ROWS - 1) % RECIPE_ROWS;
            if (downPressed) a->recipeRow = (a->recipeRow + 1) % RECIPE_ROWS;
            int row = a->recipeRow;
            int dir = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
            if (row < RF_COUNT && (dir != 0 || confirmPressed)) {
                /* Enter moves a row on like Right does, except on Melody,
                 * where it rolls a new tune rather than the next number. */
                if (row == RF_MELODY && dir == 0) Recipe_RollMelody(r, FreshSeed());
                else Recipe_Step(r, (RecipeField)row, dir != 0 ? dir : 1);
                if (row == RF_HEAR) Player_SetIntensity(Recipe_Intensity(r)); /* how it is heard, not what it is */
                else RecomposeLive(a);
            }
            if (IsKeyPressed(KEY_R) || (confirmPressed && row == RECIPE_ROW_ROLL)) {
                Recipe_Roll(r, FreshSeed());
                RecomposeLive(a);
            }
            if (confirmPressed && row == RECIPE_ROW_OPEN) {
                /* The song keeps playing into the editor; a made song is work
                 * worth keeping, so leaving the editor asks. */
                Player_SetIntensity(1.0f);
                a->ed.dirty = true;
                a->state = APP_EDITOR;
                Status(a, "Made from your recipe. Ctrl+S saves, Ctrl+G gives it to a game");
                continue;
            }
            if (confirmPressed && row == RECIPE_ROW_GIVE) {
                /* The assign screen names and saves an unsaved song before it
                 * assigns it, then Esc lands in the editor with the song. */
                Player_SetIntensity(1.0f);
                a->ed.dirty = true;
                EnterAssign(a);
                continue;
            }
            if (confirmPressed && row == RECIPE_ROW_BLANK) {
                Player_SetIntensity(1.0f);
                Song_Init(&a->scratch, "New song");
                LoadIntoEditor(a, &a->scratch, "", false);
                Status(a, "Z to M enter notes, Space plays, Ctrl+S saves");
                continue;
            }
            char values[RF_COUNT][32];
            UiSettingsRow rows[RECIPE_ROWS];
            for (int f = 0; f < RF_COUNT; f++) {
                Recipe_FieldValue(r, (RecipeField)f, values[f], sizeof(values[f]));
                rows[f] = (UiSettingsRow){UI_ROW_CHOICE, Recipe_FieldLabel((RecipeField)f), values[f], false, 0};
            }
            rows[RECIPE_ROW_ROLL] = (UiSettingsRow){UI_ROW_DONE, "Roll everything", NULL, false, 0};
            rows[RECIPE_ROW_OPEN] = (UiSettingsRow){UI_ROW_DONE, "Open in the editor", NULL, false, 0};
            rows[RECIPE_ROW_GIVE] = (UiSettingsRow){UI_ROW_DONE, "Save and give to a game", NULL, false, 0};
            rows[RECIPE_ROW_BLANK] = (UiSettingsRow){UI_ROW_DONE, "Blank song instead", NULL, false, 0};
            char key[64], note[128];
            Recipe_Note(r, key, sizeof(key));
            snprintf(note, sizeof(note), "%s. Left/Right changes it while it plays", key);
            Ui_DrawSettingsTitled("Make a song", note, "Up/Down choose    Left/Right change    R roll    Enter pick    Esc back",
                                  rows, RECIPE_ROWS, a->recipeRow, false);
            continue;
        }

        if (a->state == APP_ASSIGN) {
            if (escPressed) { a->state = APP_EDITOR; continue; }
            if (upPressed) a->assignSel = (a->assignSel + GAME_COUNT - 1) % GAME_COUNT;
            if (downPressed) a->assignSel = (a->assignSel + 1) % GAME_COUNT;
            if (IsKeyPressed(KEY_DELETE)) {
                if (Song_Assign(kGameSlugs[a->assignSel], NULL)) {
                    Status(a, "%s plays its own theme again", kGameNames[a->assignSel]);
                    Sfx_Jingle(JINGLE_BACK);
                } else {
                    Status(a, "Could not write music.ini");
                    Sfx_Jingle(JINGLE_WARNING);
                }
                RefreshAssign(a);
            }
            if (confirmPressed) {
                if (a->ed.fileName[0] == '\0') {
                    BeginName(a, NAME_ASSIGN, APP_ASSIGN); /* a game needs a file to point at */
                    continue;
                }
                if (!a->ed.dirty || SaveEditor(a)) DoAssign(a, a->assignSel);
                RefreshAssign(a);
            }
            /* What Enter, Delete or a refused drop just did takes the note's
             * place for as long as the editor would show it. */
            bool news = Render_StatusAlpha(&a->ed) > 0.0f;
            RenderList list = {
                .title = "Assign this song to a game",
                .note = news ? a->ed.status : "Games bring the lead and drums in as the action heats up.", .noteLit = news,
                .rows = kGameNames, .right = a->assignRightRows, .highlight = a->assignHot,
                .count = GAME_COUNT, .selected = a->assignSel, .empty = NULL,
                .hints = "Up/Down choose    Enter assign this song    Delete back to its own theme    Esc back", .confirm = NULL,
            };
            Render_List(&list);
            continue;
        }
    }

quit:
    Player_Shutdown();
    Sfx_Shutdown();
    Ui_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
