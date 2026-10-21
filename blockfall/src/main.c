/* Blockfall: the window, the loop and the screen stack. Every screen is a
 * file of its own (screens.h); this file reads the devices once a frame,
 * hands that input to the screen on top, and draws exactly one frame per
 * pass. A screen that opens another (App_Open, App_Back, App_Home in app.h)
 * has the new one entered, updated with a quiet input (what is held, no
 * presses) and drawn in that same pass, so the press that changed screens
 * is used up and never acts twice (Esc on Play setup, Space starting a run,
 * Alt+Enter on a menu). */
#include "winscale.h"
#include "tuning.h"
#include "tuningui.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "screens.h"
#include "comfort.h"
#include "look.h"
#include "menukeys.h"
#include "ui.h"
#include "sfx.h"
#include "music.h"
#include "prefs.h"
#include "howto.h"
#include "keynames.h"
#include "ghostlink.h"
#include "uiinput.h"
#include "uikit.h"
#include "daily.h"

/* Catalog name in Ghost Launcher (profile lookups). */
#define GHOST_GAME_NAME "Blockfall"

static const char *const kSfxFiles[SFX_COUNT] = {
    "move.wav", "rotate.wav", "harddrop.wav", "clear.wav", "quad.wav", "levelup.wav", "gameover.wav",
    /* 3.1.0: the sound pass (soundplan.h names them in this order) */
    "lock.wav", "hold.wav", "bomb.wav", "laser.wav", "freeze.wav", "thaw.wav", "slow.wav", "windup.wav", "sweep.wav", "b2b.wav",
};

typedef struct {
    void (*enter)(App *a, bool back);
    void (*update)(App *a, const Input *in, float dt);
    void (*draw)(App *a, float dt);
} Screen;

static const Screen kScreens[SCR_COUNT] = {
    [SCR_MENU] = {ScrMenu_Enter, ScrMenu_Update, ScrMenu_Draw},
    [SCR_SETUP] = {ScrSetup_Enter, ScrSetup_Update, ScrSetup_Draw},
    [SCR_PLAY] = {ScrPlay_Enter, ScrPlay_Update, ScrPlay_Draw},
    [SCR_SCORES] = {ScrScores_Enter, ScrScores_Update, ScrScores_Draw},
    [SCR_SETTINGS] = {ScrSettings_Enter, ScrSettings_Update, ScrSettings_Draw},
    [SCR_HOWTO] = {ScrHowTo_Enter, ScrHowTo_Update, ScrHowTo_Draw},
    [SCR_UPDATES] = {ScrUpdates_Enter, ScrUpdates_Update, ScrUpdates_Draw},
    [SCR_STATS] = {ScrStats_Enter, ScrStats_Update, ScrStats_Draw},
    [SCR_PRACTICE] = {ScrPractice_Enter, ScrPractice_Update, ScrPractice_Draw},
    [SCR_DAILY] = {ScrDaily_Enter, ScrDaily_Update, ScrDaily_Draw},
};

/* Everything the screens share. Static: the Game inside is big, and the
 * screenshot harness (tools/shots/main_wrap.c) reads it. */
static App sApp;

/* Tries, in order: next to the running executable (dev builds / portable
 * runs), the XDG data install location used by `make install`, then finally
 * the current working directory (`make run` from the project root). The
 * XDG check is deliberately checked before the CWD-relative one -- a bare
 * "assets" match against whatever directory the shell happened to be in is
 * unreliable (it can accidentally match an unrelated project's assets
 * folder) and installed launches shouldn't depend on it. No path is baked
 * in at compile time, so the same binary works run-in-place or installed. */
static void ResolveAssetsDir(char *buf, size_t bufSize) {
    const char *appDir = GetApplicationDirectory();
    snprintf(buf, bufSize, "%sassets", appDir);
    if (DirectoryExists(buf)) return;

    const char *xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        snprintf(buf, bufSize, "%s/blockfall/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/blockfall/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;

    if (DirectoryExists("assets")) {
        snprintf(buf, bufSize, "assets");
    }
}

/* The screen on top changed during this pass: enter it and let it see the
 * frame quietly, as many times as screens keep changing (a bounded few). */
static void SettleScreens(App *a, const Input *in, float dt) {
    for (int guard = 0; a->changed && guard < 4 && !a->quit; guard++) {
        a->changed = false;
        Input_DropHolds(&a->inputState);
        ScreenId top = App_Top(a);
        kScreens[top].enter(a, a->wentBack);
        Input quiet;
        Input_Quiet(in, &quiet);
        kScreens[top].update(a, &quiet, dt);
    }
}

int main(int argc, char **argv) {
    App *a = &sApp;
    /* The shared daily code (3.6.0): today's date for the Daily challenge,
     * and --daily (Ghost Launcher's D key) opens the game on it. */
    Daily_Init(argc, argv);
    bool openDaily = Daily_Active();
    Settings_Load(&a->settings);
    Setup_Sanitize(&a->settings); /* a hand-edited config can hold anything */
    Comfort_SetReducedMotion(a->settings.reducedMotion);
    Comfort_SetPieceSnap(a->settings.pieceSnap);
    Look_SetPatterns(a->settings.blockPatterns);
    SaveData_Load(&a->save);

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Blockfall");
    SetExitKey(KEY_NULL); /* we handle Escape ourselves */
    SetTargetFPS(60);     /* safety cap alongside vsync; keeps CPU/GPU low and stable */

    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    snprintf(a->iconPath, sizeof(a->iconPath), "%s/icons/blockfall.png", assetsDir);
    UiTheme theme = Ui_DefaultTheme((Color){160, 136, 255, 255});
    Ui_Init(assetsDir, &theme, WINDOW_WIDTH, WINDOW_HEIGHT);
    Win_Init(WINDOW_WIDTH, WINDOW_HEIGHT);
    Tune_Init(RECORD_SLUG);
    Tune_UiInit();
    Sfx_Init(assetsDir, kSfxFiles, SFX_COUNT);
    Sfx_SetEnabled(a->settings.audioEnabled);
    Music_SetEnabled(a->settings.audioEnabled);
    Sfx_SetVolume(a->settings.volumePercent);
    Music_Init(RECORD_SLUG);
    Music_SetEnabled(a->settings.audioEnabled);
    Music_SetVolume(Prefs_Get()->musicVolume);

    Kit_SetScreen(WINDOW_WIDTH, WINDOW_HEIGHT);
    Input_Init(&a->inputState, WINDOW_WIDTH, WINDOW_HEIGHT);
    MenuKeys_Install(&a->inputState);
    Actions_Keys(&a->settings, Keys_CodeFromName, &a->keys);
    GhostLink_GetUsername(GHOST_GAME_NAME, a->username, sizeof(a->username));
    ScrPlay_Init(a);

    /* The menu is the bottom of the stack; the first launch shows How to
     * play's Basics over it, and so does the next launch after Settings >
     * Game > Show How to play. */
    Nav_Init(&a->nav, SCR_MENU);
    if (openDaily) Nav_Push(&a->nav, SCR_DAILY); /* --daily: the Daily screen over the menu (How to play still comes first on a first launch) */
    if (!HowTo_Seen(RECORD_SLUG) || a->settings.showHowTo) {
        a->howto.firstLaunch = true;
        Nav_Push(&a->nav, SCR_HOWTO);
        if (a->settings.showHowTo) {
            a->settings.showHowTo = false; /* once */
            Settings_Save(&a->settings);
        }
    }
    a->slideT = SLIDE_SECONDS; /* the first screen is just there */
    kScreens[App_Top(a)].enter(a, false);

    /* Refresh the online board while the player is still on the menu. */
    GhostLink_TriggerSync();

    /* raylib moves key state on only inside EndDrawing. Every pass below
     * draws a frame, but if one ever did not (a bug), the press it saw would
     * still read as pressed next pass: poll once so it is retired. */
    unsigned long framesSeen = Win_FrameCount();
    bool firstPass = true;

    for (;;) {
        if (!firstPass && Win_FrameCount() == framesSeen) PollInputEvents();
        firstPass = false;
        framesSeen = Win_FrameCount();
        if (WindowShouldClose()) break;

        Win_Update();
        if (Tune_UiUpdate()) { Win_BeginFrame(); Win_EndFrame(); continue; } /* F2 panel open: the game holds still */
        /* A stalled frame (a window drag, a hiccup) can not eat Ultra's clock
         * or run the lock delay out in one go. */
        float dt = fminf(GetFrameTime(), 0.1f);

        InputRaw raw;
        InputRL_Read(&raw);
        Input_Step(&a->inputState, &raw, dt, &a->input);
        Music_SetMode((MusicMode)ScrPlay_MusicMode(a));

        ScreenId top = App_Top(a);
        kScreens[top].update(a, &a->input, dt);
        SettleScreens(a, &a->input, dt);
        if (a->quit) break;
        kScreens[App_Top(a)].draw(a, dt);
        App_Tick(a, dt);
    }

    /* A run still going when the game closes counts for what it scored. */
    ScrPlay_Shutdown(a);
    Settings_Save(&a->settings);

    Ui_Shutdown();
    Music_Shutdown();
    Sfx_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
