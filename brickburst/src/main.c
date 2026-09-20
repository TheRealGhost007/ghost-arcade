#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "winscale.h"
#include "tuning.h"
#include "tuningui.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "game.h"
#include "render.h"
#include "persist.h"
#include "version.h"
#include "changelog.h"
#include "ui.h"
#include "sfx.h"
#include "music.h"
#include "prefs.h"
#include "runlog.h"
#include "achievements.h"
#include "daily.h"
#include "gamepad.h" /* after raylib.h: routes IsKeyDown/IsKeyPressed through the controller too */
#include "keynames.h"
#include "ghostlink.h"

/* Catalog name in Ghost Launcher (profile lookups), score-file slug, and the
 * single mode Brickburst has on the shared score tables. */
#define GHOST_GAME_NAME "Brickburst"
#define GHOST_GAME_SLUG "brickburst"
#define GHOST_GAME_MODE "arcade"
static const char *GhostModeNow(void) { return Daily_Active() ? Daily_Mode() : GHOST_GAME_MODE; }

enum {
    SFX_SELECT, SFX_LAUNCH, SFX_PADDLE, SFX_WALL, SFX_BRICK, SFX_BREAK,
    SFX_EXPLODE, SFX_POWERUP, SFX_LIFE_LOST, SFX_LEVEL_CLEAR, SFX_GAME_OVER, SFX_SHOT, SFX_SHIELD, SFX_CATCH, SFX_COUNT
};
static const char *const kSfxFiles[SFX_COUNT] = {
    "select.wav", "launch.wav", "paddle.wav", "wall.wav", "brick.wav", "break.wav",
    "explode.wav", "powerup.wav", "lifelost.wav", "levelclear.wav", "gameover.wav", "shot.wav", "shield.wav", "catch.wav",
};

typedef struct {
    int left, left2, right, right2, launch, launch2, pause, restart, quit;
} KeyMap;

static void BuildKeyMap(const Settings *s, KeyMap *out) {
    out->left = Keys_CodeFromName(s->left);
    out->left2 = Keys_CodeFromName(s->left2);
    out->right = Keys_CodeFromName(s->right);
    out->right2 = Keys_CodeFromName(s->right2);
    out->launch = Keys_CodeFromName(s->launch);
    out->launch2 = Keys_CodeFromName(s->launch2);
    out->pause = Keys_CodeFromName(s->pause);
    out->restart = Keys_CodeFromName(s->restart);
    out->quit = Keys_CodeFromName(s->quit);
}

/* Rows: 9 rebindable actions, then Mouse, Volume, Sound, Done. */
#define SETTINGS_REBIND_ROWS 9
#define SETTINGS_ROW_MOUSE 9
#define SETTINGS_ROW_VOLUME 10
#define SETTINGS_ROW_AUDIO 11
#define SETTINGS_ROW_BACK 12
#define SETTINGS_ROW_COUNT 13

typedef enum {
    APP_MENU,
    APP_PLAYING,
    APP_SCORES,
    APP_SETTINGS,
    APP_UPDATES,
    APP_HOWTO,
} AppState;

static uint64_t MakeSeed(int nonce) {
    if (Daily_Active()) return Daily_Seed(); /* the same game all day, however often you restart */
    uint64_t t = (uint64_t)time(NULL);
    uint64_t addrEntropy = (uint64_t)(uintptr_t)&t;
    return (t * 2654435761u) ^ addrEntropy ^ (uint64_t)(nonce * 0x9E3779B1u);
}

/* Tries, in order: next to the running executable (dev builds / portable
 * runs), the XDG data install location used by `make install`, then finally
 * the current working directory (`make run` from the project root). The
 * XDG check is deliberately checked before the CWD-relative one -- a bare
 * "assets" match against whatever directory the shell happened to be in is
 * unreliable (it can accidentally match an unrelated project's assets
 * folder) and installed launches shouldn't depend on it. */
static void ResolveAssetsDir(char *buf, size_t bufSize) {
    const char *appDir = GetApplicationDirectory();
    snprintf(buf, bufSize, "%sassets", appDir);
    if (DirectoryExists(buf)) return;

    const char *xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        snprintf(buf, bufSize, "%s/brickburst/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/brickburst/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;

    if (DirectoryExists("assets")) {
        snprintf(buf, bufSize, "assets");
    }
}

/* Points a settings-row index at the Settings struct field it edits, for
 * the in-game rebind UI. Order matches kSettingsRows in render.c. */
static char *SettingsRowField(Settings *s, int row) {
    switch (row) {
        case 0: return s->left;
        case 1: return s->left2;
        case 2: return s->right;
        case 3: return s->right2;
        case 4: return s->launch;
        case 5: return s->launch2;
        case 6: return s->pause;
        case 7: return s->restart;
        case 8: return s->quit;
        default: return NULL;
    }
}

/* Everything that must happen exactly once when a run ends, however it
 * ends (game over, restart mid-run, back to menu, window closed): fold it
 * into the local bests, submit it to the shared score table, and kick off
 * the background online sync. Returns the rank reached (0 = none). */
static int FinalizeRun(const Game *g, SaveData *save, const char *username, bool *finalized) {
    if (*finalized) return 0;
    *finalized = true;
    if (Tune_Modified()) return 0; /* a tuned run is a playtest, not a score */
    RunLog_Append(GHOST_GAME_SLUG, GhostModeNow(), (long)g->score, (int)g->level);
    if (Ach_CheckRun(GHOST_GAME_SLUG, (long)g->score, (int)g->level) > 0) Sfx_Jingle(JINGLE_LEVEL_UP);

    bool changed = false;
    if (g->score > save->highScore) { save->highScore = g->score; changed = true; }
    if (g->level > save->bestLevel) { save->bestLevel = g->level; changed = true; }
    if (changed) SaveData_Save(save);
    if (g->score <= 0) return 0;

    char date[GHOSTLINK_DATE_LEN];
    time_t now = time(NULL);
    struct tm tmNow;
    if (localtime_r(&now, &tmNow) == NULL || strftime(date, sizeof(date), "%Y-%m-%d", &tmNow) == 0) {
        snprintf(date, sizeof(date), "unknown");
    }
    int rank = GhostLink_SubmitScore(GHOST_GAME_SLUG, GhostModeNow(), username, g->score, date);
    GhostLink_TriggerSync();
    return rank;
}

static const char *const kMenuItems[] = {"Start game", "How to play", "Scores", "Settings", "Updates", "Quit"};
#define MENU_ITEM_COUNT 6
#define MENU_ITEM_START 0
#define MENU_ITEM_HOWTO 1
#define MENU_ITEM_SCORES 2
#define MENU_ITEM_SETTINGS 3
#define MENU_ITEM_UPDATES 4
#define MENU_ITEM_QUIT 5

static void DrawMenu(int selected, const SaveData *save, const char *username, const char *iconPath, float dt) {
    char statLine[96];
    snprintf(statLine, sizeof(statLine), "Best  %ld      furthest  level %d", save->highScore, save->bestLevel);
    UiMenu menu = {
        .title = "BRICKBURST", .tagline = "Break everything. Drop nothing.",
        .items = kMenuItems, .itemCount = MENU_ITEM_COUNT, .selected = selected,
        .statLine = statLine, .username = username, .hints = "Up/Down choose    Enter select",
        .iconPath = iconPath, .versionLabel = Daily_Active() ? "v" BRICKBURST_VERSION "  DAILY" : "v" BRICKBURST_VERSION, .backdrop = Render_MenuBackdrop,
    };
    Ui_DrawMenu(&menu, dt);
}

static void DrawSettings(Settings *s, int selected, bool awaitingKey) {
    static const char *kLabels[SETTINGS_REBIND_ROWS] = {
        "Left", "Left, second key", "Right", "Right, second key", "Launch", "Launch, second key",
        "Pause", "Restart", "Back to menu",
    };
    UiSettingsRow rows[SETTINGS_ROW_COUNT];
    for (int i = 0; i < SETTINGS_REBIND_ROWS; i++) rows[i] = (UiSettingsRow){UI_ROW_KEY, kLabels[i], SettingsRowField(s, i), false, 0};
    rows[SETTINGS_ROW_MOUSE] = (UiSettingsRow){UI_ROW_TOGGLE, "Mouse moves the paddle", NULL, s->mouseControl, 0};
    rows[SETTINGS_ROW_VOLUME] = (UiSettingsRow){UI_ROW_VOLUME, "Effects volume", NULL, false, s->volumePercent};
    rows[SETTINGS_ROW_AUDIO] = (UiSettingsRow){UI_ROW_TOGGLE, "Sound", NULL, s->audioEnabled, 0};
    rows[SETTINGS_ROW_BACK] = (UiSettingsRow){UI_ROW_DONE, "Done", NULL, false, 0};
    Ui_DrawSettingsPlus(rows, SETTINGS_ROW_COUNT, selected, awaitingKey);
}

int main(int argc, char **argv) {
    Daily_Init(argc, argv);
    Settings settings;
    Settings_Load(&settings);

    SaveData saveData;
    SaveData_Load(&saveData);

    KeyMap keys;
    BuildKeyMap(&settings, &keys);

    char username[GHOSTLINK_NAME_LEN];
    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));

    /* Refresh the online board while the player is still on the menu. */
    GhostLink_TriggerSync();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Brickburst");
    SetExitKey(KEY_NULL); /* we handle Escape ourselves */
    SetTargetFPS(60);     /* safety cap alongside vsync; keeps CPU/GPU low and stable */

    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    char iconPath[600];
    snprintf(iconPath, sizeof(iconPath), "%s/icons/brickburst.png", assetsDir);
    UiTheme theme = Ui_DefaultTheme((Color){255, 150, 60, 255});
    Ui_Init(assetsDir, &theme, WINDOW_WIDTH, WINDOW_HEIGHT);
    Win_Init(WINDOW_WIDTH, WINDOW_HEIGHT);
    Tune_Init(GHOST_GAME_SLUG);
    Tune_UiInit();
    Sfx_Init(assetsDir, kSfxFiles, SFX_COUNT);
    Sfx_SetEnabled(settings.audioEnabled);
    Music_SetEnabled(settings.audioEnabled);
    Sfx_SetVolume(settings.volumePercent);
    Music_Init(GHOST_GAME_SLUG);
    Music_SetEnabled(settings.audioEnabled);
    Music_SetVolume(Prefs_Get()->musicVolume);

    Game game;
    Game_Init(&game, MakeSeed(0), saveData.highScore);
    Game_SetComboScoring(&game, true);
    bool runFinalized = true; /* nothing to record until a run actually starts */
    int lastRank = 0;

    int restartNonce = 1;
    bool showFps = false;
    bool mouseActive = false; /* the mouse drives the paddle until a move key is pressed, and vice versa */

    AppState appState = HowTo_Seen(GHOST_GAME_SLUG) ? APP_MENU : APP_HOWTO; /* first launch shows the how-to card */
    int menuIndex = 0;
    int settingsRow = 0;
    bool awaitingKey = false;
    int updatesScroll = 0;

    bool scoresGlobal = false;
    int scoresPeriod = 0;
    float scoresReloadTimer = 0.0f;
    static GhostScore scoreEntries[GHOSTLINK_TOP_N];
    int scoreCount = 0;

    while (!WindowShouldClose()) {
        Win_Update();
        if (Tune_UiUpdate()) { Win_BeginFrame(); Win_EndFrame(); continue; } /* F2 panel open: the game holds still */
        float dt = GetFrameTime();
        Pad_Update();
        Music_SetMode(appState != APP_PLAYING ? MUSIC_MENU
                      : (game.phase == GS_PAUSED ? MUSIC_PAUSED : (game.phase == GS_GAMEOVER ? MUSIC_SILENT : MUSIC_PLAY)));
        if (appState == APP_PLAYING) Music_SetIntensity(0.65f);

        bool upPressed = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
        bool downPressed = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
        bool leftPressed = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
        bool rightPressed = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
        bool confirmPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
        bool escPressed = IsKeyPressed(KEY_ESCAPE);

        if (appState == APP_MENU) {
            if (upPressed) menuIndex = (menuIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
            if (downPressed) menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
            if (upPressed || downPressed) Sfx_Play(SFX_SELECT);
            if (escPressed) break; /* quit from the top-level menu */

            if (confirmPressed) {
                if (menuIndex == MENU_ITEM_START) {
                    /* Re-read the profile each run so a name change made in
                     * Ghost Launcher applies without restarting the game. */
                    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));
                    restartNonce++;
                    Game_Init(&game, MakeSeed(restartNonce), saveData.highScore);
                    Game_SetComboScoring(&game, true);
                    runFinalized = false;
                    lastRank = 0;
                    appState = APP_PLAYING;
                    Sfx_Jingle(JINGLE_READY);
                } else if (menuIndex == MENU_ITEM_SCORES) {
                    scoresReloadTimer = 99.0f; /* load on the first frame */
                    appState = APP_SCORES;
                } else if (menuIndex == MENU_ITEM_SETTINGS) {
                    appState = APP_SETTINGS;
                    settingsRow = 0;
                    awaitingKey = false;
                } else if (menuIndex == MENU_ITEM_HOWTO) {
                    appState = APP_HOWTO;
                } else if (menuIndex == MENU_ITEM_UPDATES) {
                    appState = APP_UPDATES;
                    updatesScroll = 0;
                } else {
                    break; /* Quit */
                }
            }

            DrawMenu(menuIndex, &saveData, username, iconPath, dt);
            continue;
        }

        if (appState == APP_SCORES) {
            if (IsKeyPressed(KEY_T)) scoresPeriod = (scoresPeriod + 1) % UI_PERIOD_COUNT;
            if (escPressed) appState = APP_MENU;
            bool viewChanged = upPressed || downPressed;
            if (viewChanged) {
                scoresGlobal = !scoresGlobal;
                Sfx_Play(SFX_SELECT);
            }
            /* Touch the file when the view changes, plus a slow poll so a
             * background sync that just finished shows up -- never per frame. */
            scoresReloadTimer += dt;
            if (viewChanged || scoresReloadTimer >= 2.0f) {
                scoreCount = scoresGlobal
                    ? GhostLink_LoadGlobalScores(GHOST_GAME_SLUG, GhostModeNow(), scoreEntries, GHOSTLINK_TOP_N)
                    : GhostLink_LoadScores(GHOST_GAME_SLUG, GhostModeNow(), scoreEntries, GHOSTLINK_TOP_N);
                scoresReloadTimer = 0.0f;
            }
            Ui_DrawScoresV2("Arcade", scoresGlobal, (UiScorePeriod)scoresPeriod, scoreEntries, scoreCount, username,
                          "Up/Down this machine or world    T period    Esc back");
            continue;
        }

        if (appState == APP_HOWTO) {
            if (escPressed || confirmPressed) { HowTo_MarkSeen(GHOST_GAME_SLUG); appState = APP_MENU; }
            Ui_DrawHowTo(HowTo_ForSlug(GHOST_GAME_SLUG));
            continue;
        }

        if (appState == APP_UPDATES) {
            if (escPressed) appState = APP_MENU;
            if (upPressed) updatesScroll -= 48;
            if (downPressed) updatesScroll += 48;
            Ui_DrawUpdates(kChangelog, CHANGELOG_COUNT, &updatesScroll);
            continue;
        }

        if (appState == APP_SETTINGS) {
            if (awaitingKey) {
                int captured = GetKeyPressed();
                if (captured == KEY_ESCAPE) {
                    awaitingKey = false;
                } else if (captured != 0) {
                    const char *name = Keys_NameFromCode(captured);
                    if (name) {
                        char *field = SettingsRowField(&settings, settingsRow);
                        if (field) {
                            snprintf(field, KEYNAME_LEN, "%s", name);
                            Settings_Save(&settings);
                            BuildKeyMap(&settings, &keys);
                        }
                        awaitingKey = false;
                    }
                    /* Unrecognized key: keep waiting for a mappable one. */
                }
            } else {
                if (upPressed) settingsRow = (settingsRow + SETTINGS_ROW_COUNT + PREFS_ROWS - 1) % (SETTINGS_ROW_COUNT + PREFS_ROWS);
                if (downPressed) settingsRow = (settingsRow + 1) % (SETTINGS_ROW_COUNT + PREFS_ROWS);
                if (escPressed) appState = APP_MENU;

                if (settingsRow >= SETTINGS_ROW_COUNT - 1 && settingsRow < SETTINGS_ROW_COUNT - 1 + PREFS_ROWS) {
                    if (Prefs_HandleRow(settingsRow - (SETTINGS_ROW_COUNT - 1), leftPressed, rightPressed, confirmPressed)) {
                        Music_SetVolume(Prefs_Get()->musicVolume);
                        Sfx_Play(SFX_SELECT);
                    }
                } else if (settingsRow == SETTINGS_ROW_VOLUME) {
                    if (leftPressed || rightPressed) {
                        settings.volumePercent += rightPressed ? 10 : -10;
                        if (settings.volumePercent < 0) settings.volumePercent = 0;
                        if (settings.volumePercent > 100) settings.volumePercent = 100;
                        Sfx_SetVolume(settings.volumePercent);
                        Music_SetVolume(Prefs_Get()->musicVolume);
                        Settings_Save(&settings);
                        Sfx_Play(SFX_SELECT);
                    }
                } else if (confirmPressed) {
                    if (settingsRow < SETTINGS_REBIND_ROWS) {
                        awaitingKey = true;
                    } else if (settingsRow == SETTINGS_ROW_MOUSE) {
                        settings.mouseControl = !settings.mouseControl;
                        Settings_Save(&settings);
                    } else if (settingsRow == SETTINGS_ROW_AUDIO) {
                        settings.audioEnabled = !settings.audioEnabled;
                        Sfx_SetEnabled(settings.audioEnabled);
                        Music_SetEnabled(settings.audioEnabled);
                        Settings_Save(&settings);
                    } else {
                        appState = APP_MENU;
                    }
                }
            }

            DrawSettings(&settings, settingsRow, awaitingKey);
            continue;
        }

        /* appState == APP_PLAYING */
        if (IsKeyPressed(keys.quit)) {
            /* Escape during a game returns to the main menu rather than
             * exiting outright. An abandoned run still counts. */
            FinalizeRun(&game, &saveData, username, &runFinalized);
            appState = APP_MENU;
            DrawMenu(menuIndex, &saveData, username, iconPath, dt);
            continue;
        }
        if (IsKeyPressed(KEY_F3)) showFps = !showFps;

        if (IsKeyPressed(KEY_M)) {
            settings.audioEnabled = !settings.audioEnabled;
            Sfx_SetEnabled(settings.audioEnabled);
            Music_SetEnabled(settings.audioEnabled);
        }
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
            settings.volumePercent -= 10;
            if (settings.volumePercent < 0) settings.volumePercent = 0;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
            settings.volumePercent += 10;
            if (settings.volumePercent > 100) settings.volumePercent = 100;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }

        if (IsKeyPressed(keys.restart)) {
            FinalizeRun(&game, &saveData, username, &runFinalized);
            restartNonce++;
            Game_Restart(&game, MakeSeed(restartNonce));
            runFinalized = false;
            lastRank = 0;
        } else if (IsKeyPressed(keys.pause) && game.phase != GS_GAMEOVER) {
            Game_TogglePause(&game);
        }

        /* Paddle input. Keys give a full-speed push. The mouse gives a push
         * proportional to how far the paddle is from the pointer, capped at
         * the same top speed -- so the mouse is smoother, never faster, and
         * the rules stay the same whichever you use. Whichever was touched
         * last is in charge. */
        bool leftHeld = IsKeyDown(keys.left) || IsKeyDown(keys.left2);
        bool rightHeld = IsKeyDown(keys.right) || IsKeyDown(keys.right2);
        Vector2 mouseDelta = GetMouseDelta();
        if (leftHeld || rightHeld) mouseActive = false;
        else if (settings.mouseControl && (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)) mouseActive = true;

        float moveDir = 0.0f;
        if (leftHeld != rightHeld) moveDir = leftHeld ? -1.0f : 1.0f;
        else if (mouseActive && settings.mouseControl) {
            float target = (float)GetMouseX() - (float)FIELD_X;
            moveDir = (target - game.paddleX) / 10.0f;
        }
        bool launch = IsKeyDown(keys.launch) || IsKeyDown(keys.launch2) ||
                      (settings.mouseControl && IsMouseButtonDown(MOUSE_LEFT_BUTTON));
        Game_SetInput(&game, moveDir, launch);

        Game_Update(&game, dt * Tune_Speed());

        if (game.justExploded) Sfx_Play(SFX_EXPLODE);
        else if (game.brokenCount > 0) Sfx_Play(SFX_BREAK);
        else if (game.justBrickHit) Sfx_Play(SFX_BRICK);
        if (game.justPaddleHit) Sfx_Play(SFX_PADDLE);
        if (game.justWallHit) Sfx_Play(SFX_WALL);
        if (game.justLaunched) Sfx_Play(SFX_LAUNCH);
        if (game.justShot) Sfx_PlayVaried(SFX_SHOT, 0.06f);
        if (game.justShieldHit) Sfx_Play(SFX_SHIELD);
        if (game.justCatch) Sfx_Play(SFX_CATCH);
        if (game.justPowerup >= 0) Sfx_Play(SFX_POWERUP);
        if (game.justLevelClear) Sfx_Play(SFX_LEVEL_CLEAR);
        if (game.justGameOver) {
            Sfx_Play(SFX_GAME_OVER);
            lastRank = FinalizeRun(&game, &saveData, username, &runFinalized);
            if (lastRank == 1) Sfx_Jingle(JINGLE_NEW_BEST);
        } else if (game.justLifeLost) {
            Sfx_Play(SFX_LIFE_LOST);
        }

        /* Render first: it starts its particles and shake off the same
         * one-frame events the sounds above just used. */
        FrameInfo info = {username, lastRank, showFps};
        Render_Frame(&game, &info);

        Game_ConsumeFrameFlags(&game);
    }

    FinalizeRun(&game, &saveData, username, &runFinalized);
    Settings_Save(&settings);

    Ui_Shutdown();
    Music_Shutdown();
    Sfx_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
