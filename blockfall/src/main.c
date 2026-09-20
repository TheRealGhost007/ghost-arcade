#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "winscale.h"
#include "tuning.h"
#include "tuningui.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "game.h"
#include "render.h"
#include "persist.h"
#include "repeat.h"
#include "version.h"
#include "changelog.h"
#include "ui.h"
#include "sfx.h"
#include "music.h"
#include "prefs.h"
#include "runlog.h"
#include "achievements.h"
#include "gamepad.h" /* after raylib.h: routes IsKeyDown/IsKeyPressed through the controller too */
#include "keynames.h"
#include "ghostlink.h"

/* Catalog name in Ghost Launcher (profile lookups), score-file slug, and the
 * single mode Blockfall has on the shared score tables. */
#define GHOST_GAME_NAME "Blockfall"
#define GHOST_GAME_SLUG "blockfall"

enum { SFX_MOVE, SFX_ROTATE, SFX_HARD_DROP, SFX_CLEAR, SFX_QUAD, SFX_LEVEL_UP, SFX_GAME_OVER, SFX_COUNT };
static const char *const kSfxFiles[SFX_COUNT] = {
    "move.wav", "rotate.wav", "harddrop.wav", "clear.wav", "quad.wav", "levelup.wav", "gameover.wav",
};

typedef struct {
    int left, left2, right, right2, softDrop, softDrop2, rotate, rotate2, hardDrop, hold, pause, restart, quit;
} KeyMap;

static void BuildKeyMap(const Settings *s, KeyMap *out) {
    out->left = Keys_CodeFromName(s->left);
    out->left2 = Keys_CodeFromName(s->left2);
    out->right = Keys_CodeFromName(s->right);
    out->right2 = Keys_CodeFromName(s->right2);
    out->softDrop = Keys_CodeFromName(s->softDrop);
    out->softDrop2 = Keys_CodeFromName(s->softDrop2);
    out->rotate = Keys_CodeFromName(s->rotate);
    out->rotate2 = Keys_CodeFromName(s->rotate2);
    out->hardDrop = Keys_CodeFromName(s->hardDrop);
    out->hold = Keys_CodeFromName(s->hold);
    out->pause = Keys_CodeFromName(s->pause);
    out->restart = Keys_CodeFromName(s->restart);
    out->quit = Keys_CodeFromName(s->quit);
}

/* Rows: 12 rebindable actions, then Volume, Sound, Done. */
#define SETTINGS_REBIND_ROWS 13
#define SETTINGS_ROW_VOLUME 13
#define SETTINGS_ROW_AUDIO 14
#define SETTINGS_ROW_BACK 15
#define SETTINGS_ROW_COUNT 16

#define MENU_ITEM_COUNT 6
#define MENU_ITEM_START 0
#define MENU_ITEM_HOWTO 1
#define MENU_ITEM_SCORES 2
#define MENU_ITEM_SETTINGS 3
#define MENU_ITEM_UPDATES 4
#define MENU_ITEM_QUIT 5

typedef enum {
    APP_MENU,
    APP_PLAYING,
    APP_SETUP,
    APP_SCORES,
    APP_SETTINGS,
    APP_UPDATES,
    APP_HOWTO,
} AppState;


/* ------------------------------------------------------------ play setup */

typedef struct { const char *name; int w, h; } BoardPreset;
static const BoardPreset kPresets[] = {
    {"Classic", 10, 20}, {"Wide", 14, 20}, {"Tall", 8, 26}, {"Huge", 18, 30}, {"Tiny", 6, 14}, {"Custom", 0, 0},
};
#define PRESET_COUNT ((int)(sizeof(kPresets) / sizeof(kPresets[0])))
#define PRESET_CUSTOM (PRESET_COUNT - 1)

static const char *const kModeNotes[MODE_COUNT] = {
    "Marathon: endless, the pace climbs every ten lines",
    "Sprint: clear 40 lines as fast as you can",
    "Ultra: two minutes, score as much as you can",
    "Zen: no game over, no rush, just stack",
    "Power: marathon with bombs, lasers, freezes and more",
};

static void SetupSize(const Settings *s, int *w, int *h) {
    if (s->setupPreset >= 0 && s->setupPreset < PRESET_CUSTOM) { *w = kPresets[s->setupPreset].w; *h = kPresets[s->setupPreset].h; }
    else { *w = s->setupWidth; *h = s->setupHeight; }
}

static GameConfig SetupConfig(const Settings *s) {
    GameConfig cfg;
    cfg.mode = (GameMode)(s->setupMode % MODE_COUNT);
    SetupSize(s, &cfg.width, &cfg.height);
    /* The classic marathon keeps the classic scoring, so its old scores still mean what they did. */
    cfg.modernScoring = !(cfg.mode == MODE_MARATHON && cfg.width == 10 && cfg.height == 20);
    return cfg;
}

/* The score-table mode for a run: "marathon" for the classic board, "sprint-14x20" for others. */
static void ModeKey(GameMode mode, int w, int h, char *out, size_t size) {
    static const char *const kNames[MODE_COUNT] = {"marathon", "sprint", "ultra", "zen", "power"};
    if (w == 10 && h == 20) snprintf(out, size, "%s", kNames[mode]);
    else snprintf(out, size, "%s-%dx%d", kNames[mode], w, h);
}

/* Best local score for a mode and size, for the HUD's Best. */
static long LocalBest(GameConfig cfg, const SaveData *save) {
    char key[GHOSTLINK_MODE_LEN];
    ModeKey(cfg.mode, cfg.width, cfg.height, key, sizeof(key));
    GhostScore top;
    if (GhostLink_LoadScores(GHOST_GAME_SLUG, key, &top, 1) > 0) return top.score;
    return (cfg.mode == MODE_MARATHON && cfg.width == 10 && cfg.height == 20) ? save->highScore : 0;
}

static uint64_t MakeSeed(int nonce) {
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

/* Points a settings-row index (0-11) at the Settings struct field it edits,
 * for the in-game rebind UI. */
static char *SettingsRowField(Settings *s, int row) {
    switch (row) {
        case 0: return s->left;
        case 1: return s->left2;
        case 2: return s->right;
        case 3: return s->right2;
        case 4: return s->softDrop;
        case 5: return s->softDrop2;
        case 6: return s->rotate;
        case 7: return s->rotate2;
        case 8: return s->hardDrop;
        case 9: return s->hold;
        case 10: return s->pause;
        case 11: return s->restart;
        case 12: return s->quit;
        default: return NULL;
    }
}

/* Records a finished (or abandoned) run on the score table shared with
 * Ghost Launcher and kicks off the background online sync. Guarded so each
 * run is submitted exactly once however it ends. */
static int SubmitRun(const Game *g, const char *username, bool *submitted) {
    if (*submitted) return 0;
    *submitted = true;
    if (Tune_Modified()) return 0; /* a tuned run is a playtest, not a score */
    {
        char logMode[GHOSTLINK_MODE_LEN];
        ModeKey(g->cfg.mode, g->cfg.width, g->cfg.height, logMode, sizeof(logMode));
        RunLog_Append(GHOST_GAME_SLUG, logMode, g->score, g->level);
    }
    if (g->cfg.mode != MODE_ZEN && Ach_CheckRun(GHOST_GAME_SLUG, g->score, g->level) > 0) Sfx_Jingle(JINGLE_LEVEL_UP);
    if (g->score <= 0 || g->cfg.mode == MODE_ZEN) return 0;

    char date[GHOSTLINK_DATE_LEN];
    time_t now = time(NULL);
    struct tm tmNow;
    if (localtime_r(&now, &tmNow) == NULL || strftime(date, sizeof(date), "%Y-%m-%d", &tmNow) == 0) {
        snprintf(date, sizeof(date), "unknown");
    }
    char modeKey[GHOSTLINK_MODE_LEN];
    ModeKey(g->cfg.mode, g->cfg.width, g->cfg.height, modeKey, sizeof(modeKey));
    int rank = GhostLink_SubmitScore(GHOST_GAME_SLUG, modeKey, username, g->score, date);
    GhostLink_TriggerSync();
    return rank;
}

static const char *const kMenuItems[MENU_ITEM_COUNT] = {"Start game", "How to play", "Scores", "Settings", "Updates", "Quit"};

static void DrawMenu(int selected, const SaveData *save, const char *username, const char *iconPath, float dt) {
    char statLine[96];
    snprintf(statLine, sizeof(statLine), "Best  %ld      level  %d      lines  %d", save->highScore, save->highLevel, save->highLines);
    UiMenu menu = {
        .title = "BLOCKFALL", .tagline = "Stack them. Clear them. Keep up.",
        .items = kMenuItems, .itemCount = MENU_ITEM_COUNT, .selected = selected,
        .statLine = statLine, .username = username, .hints = "Up/Down choose    Enter select",
        .iconPath = iconPath, .versionLabel = "v" BLOCKFALL_VERSION, .backdrop = Render_MenuBackdrop,
    };
    Ui_DrawMenu(&menu, dt);
}

static void DrawSettings(Settings *s, int selected, bool awaitingKey) {
    static const char *kLabels[SETTINGS_REBIND_ROWS] = {
        "Move left", "Move left, second key", "Move right", "Move right, second key",
        "Soft drop", "Soft drop, second key", "Rotate", "Rotate, second key",
        "Hard drop", "Hold piece", "Pause", "Restart", "Back to menu",
    };
    UiSettingsRow rows[SETTINGS_ROW_COUNT];
    for (int i = 0; i < SETTINGS_REBIND_ROWS; i++) rows[i] = (UiSettingsRow){UI_ROW_KEY, kLabels[i], SettingsRowField(s, i), false, 0};
    rows[SETTINGS_ROW_VOLUME] = (UiSettingsRow){UI_ROW_VOLUME, "Effects volume", NULL, false, s->volumePercent};
    rows[SETTINGS_ROW_AUDIO] = (UiSettingsRow){UI_ROW_TOGGLE, "Sound", NULL, s->audioEnabled, 0};
    rows[SETTINGS_ROW_BACK] = (UiSettingsRow){UI_ROW_DONE, "Done", NULL, false, 0};
    Ui_DrawSettingsPlus(rows, SETTINGS_ROW_COUNT, selected, awaitingKey);
}

int main(void) {
    Settings settings;
    Settings_Load(&settings);
    /* A hand-edited config can hold anything: keep the play setup inside its tables. */
    if (settings.setupMode < 0 || settings.setupMode >= MODE_COUNT) settings.setupMode = 0;
    if (settings.setupPreset < 0 || settings.setupPreset >= PRESET_COUNT) settings.setupPreset = 0;

    SaveData saveData;
    SaveData_Load(&saveData);

    KeyMap keys;
    BuildKeyMap(&settings, &keys);

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Blockfall");
    SetExitKey(KEY_NULL); /* we handle Escape ourselves */
    SetTargetFPS(60);     /* safety cap alongside vsync; keeps CPU/GPU low and stable */

    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    char iconPath[600];
    snprintf(iconPath, sizeof(iconPath), "%s/icons/blockfall.png", assetsDir);
    UiTheme theme = Ui_DefaultTheme((Color){160, 136, 255, 255});
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
    Game_Init(&game, MakeSeed(0), saveData.highScore, saveData.highLevel, saveData.highLines);

    char username[GHOSTLINK_NAME_LEN];
    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));
    bool runSubmitted = true; /* nothing to record until a run actually starts */

    RepeatButton leftBtn = {0}, rightBtn = {0}, softDropBtn = {0};
    int restartNonce = 1;
    bool showFps = false;

    AppState appState = HowTo_Seen(GHOST_GAME_SLUG) ? APP_MENU : APP_HOWTO; /* first launch shows the how-to card */
    int menuIndex = 0;
    int settingsRow = 0;
    int setupRow = 0;
    int scoresMode = 0; /* Marathon, Sprint, Ultra, Power */
    bool awaitingKey = false;
    int updatesScroll = 0;

    bool scoresGlobal = false;
    int scoresPeriod = 0;
    float scoresReloadTimer = 0.0f;
    static GhostScore scoreEntries[GHOSTLINK_TOP_N];
    int scoreCount = 0;

    /* Refresh the online board while the player is still on the menu. */
    GhostLink_TriggerSync();

    while (!WindowShouldClose()) {
        Win_Update();
        if (Tune_UiUpdate()) { Win_BeginFrame(); Win_EndFrame(); continue; } /* F2 panel open: the game holds still */
        float dt = GetFrameTime();
        Pad_Update();
        Music_SetMode(appState != APP_PLAYING ? MUSIC_MENU
                      : (game.phase == GS_PAUSED ? MUSIC_PAUSED : (game.phase == GS_GAMEOVER ? MUSIC_SILENT : MUSIC_PLAY)));
        if (appState == APP_PLAYING) Music_SetIntensity(fminf(1.0f, 0.40f + 0.06f * (float)game.level));

        bool upPressed = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
        bool downPressed = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
        bool leftPressed = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
        bool rightPressed = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
        bool confirmPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
        bool escPressed = IsKeyPressed(KEY_ESCAPE);

        if (appState == APP_MENU) {
            if (upPressed) menuIndex = (menuIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
            if (downPressed) menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
            if (escPressed) break; /* quit from the top-level menu */

            if (confirmPressed) {
                if (menuIndex == MENU_ITEM_START) {
                    /* Re-read the profile each run so a name change made in
                     * Ghost Launcher applies without restarting the game. */
                    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));
                    appState = APP_SETUP;
                    setupRow = 0;
                    Sfx_Play(SFX_MOVE);
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

        if (appState == APP_SETUP) {
            bool custom = settings.setupPreset == PRESET_CUSTOM;
            int rowCount = custom ? 5 : 3;
            int startRow = rowCount - 1;
            if (escPressed) { appState = APP_MENU; continue; }
            if (upPressed) setupRow = (setupRow + rowCount - 1) % rowCount;
            if (downPressed) setupRow = (setupRow + 1) % rowCount;
            int dir = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
            bool changed = false;
            if (setupRow == 0 && dir) { settings.setupMode = (settings.setupMode + dir + MODE_COUNT) % MODE_COUNT; changed = true; }
            else if (setupRow == 1 && dir) { settings.setupPreset = (settings.setupPreset + dir + PRESET_COUNT) % PRESET_COUNT; changed = true; }
            else if (custom && setupRow == 2 && dir) { settings.setupWidth += dir; if (settings.setupWidth < BOARD_MIN_W) settings.setupWidth = BOARD_MIN_W; if (settings.setupWidth > BOARD_MAX_W) settings.setupWidth = BOARD_MAX_W; changed = true; }
            else if (custom && setupRow == 3 && dir) { settings.setupHeight += dir; if (settings.setupHeight < BOARD_MIN_H) settings.setupHeight = BOARD_MIN_H; if (settings.setupHeight > BOARD_MAX_H) settings.setupHeight = BOARD_MAX_H; changed = true; }
            if (changed) { Settings_Save(&settings); Sfx_Play(SFX_MOVE); }
            if (upPressed || downPressed) Sfx_Play(SFX_MOVE);
            if (setupRow >= rowCount) setupRow = startRow;

            if (confirmPressed && (setupRow == startRow || setupRow < 2)) {
                GameConfig cfg = SetupConfig(&settings);
                restartNonce++;
                Game_InitConfig(&game, cfg, MakeSeed(restartNonce), LocalBest(cfg, &saveData), saveData.highLevel, saveData.highLines);
                Ui_ClearParticles();
                runSubmitted = false;
                appState = APP_PLAYING;
                Sfx_Jingle(JINGLE_READY);
                continue;
            }

            char sizeText[48], modeText[32];
            int w, h;
            SetupSize(&settings, &w, &h);
            snprintf(modeText, sizeof(modeText), "%s", Game_ModeName((GameMode)(settings.setupMode % MODE_COUNT)));
            if (custom) snprintf(sizeText, sizeof(sizeText), "Custom");
            else snprintf(sizeText, sizeof(sizeText), "%s  %dx%d", kPresets[settings.setupPreset].name, w, h);
            char wText[16], hText[16];
            snprintf(wText, sizeof(wText), "%d", settings.setupWidth);
            snprintf(hText, sizeof(hText), "%d", settings.setupHeight);
            UiSettingsRow rows[5];
            int n = 0;
            rows[n++] = (UiSettingsRow){UI_ROW_CHOICE, "Mode", modeText, false, 0};
            rows[n++] = (UiSettingsRow){UI_ROW_CHOICE, "Board", sizeText, false, 0};
            if (custom) {
                rows[n++] = (UiSettingsRow){UI_ROW_CHOICE, "Width", wText, false, 0};
                rows[n++] = (UiSettingsRow){UI_ROW_CHOICE, "Height", hText, false, 0};
            }
            rows[n++] = (UiSettingsRow){UI_ROW_DONE, "Start", NULL, false, 0};
            Ui_DrawSettingsTitled("Play setup", kModeNotes[settings.setupMode % MODE_COUNT],
                                  "Up/Down move    Left/Right change    Enter play    Esc back", rows, n, setupRow, false);
            continue;
        }

        if (appState == APP_SCORES) {
            if (IsKeyPressed(KEY_T)) scoresPeriod = (scoresPeriod + 1) % UI_PERIOD_COUNT;
            if (escPressed) appState = APP_MENU;
            static const GameMode kScoreModes[4] = {MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_POWER};
            int modeDir = (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0);
            if (modeDir) scoresMode = (scoresMode + modeDir + 4) % 4;
            bool viewChanged = upPressed || downPressed || modeDir;
            if (upPressed || downPressed) scoresGlobal = !scoresGlobal;
            if (viewChanged) Sfx_Play(SFX_MOVE);
            int sw, sh;
            SetupSize(&settings, &sw, &sh);
            char scoreKey[GHOSTLINK_MODE_LEN];
            ModeKey(kScoreModes[scoresMode], sw, sh, scoreKey, sizeof(scoreKey));
            /* Touch the file when the view changes, plus a slow poll so a
             * background sync that just finished shows up -- never per frame. */
            scoresReloadTimer += dt;
            if (viewChanged || scoresReloadTimer >= 2.0f) {
                scoreCount = scoresGlobal
                    ? GhostLink_LoadGlobalScores(GHOST_GAME_SLUG, scoreKey, scoreEntries, GHOSTLINK_TOP_N)
                    : GhostLink_LoadScores(GHOST_GAME_SLUG, scoreKey, scoreEntries, GHOSTLINK_TOP_N);
                scoresReloadTimer = 0.0f;
            }
            char scoreLabel[40];
            snprintf(scoreLabel, sizeof(scoreLabel), "< %s  %dx%d >", Game_ModeName(kScoreModes[scoresMode]), sw, sh);
            Ui_DrawScoresV2(scoreLabel, scoresGlobal, (UiScorePeriod)scoresPeriod, scoreEntries, scoreCount, username,
                          "Left/Right mode    Up/Down world    T period    Esc back");
            continue;
        }

        if (appState == APP_HOWTO) {
            if (escPressed || confirmPressed) { HowTo_MarkSeen(GHOST_GAME_SLUG); appState = APP_MENU; }
            Ui_DrawHowTo(HowTo_ForSlug(GHOST_GAME_SLUG));
            continue;
        }

        if (appState == APP_UPDATES) {
            if (escPressed) appState = APP_MENU;
            if (upPressed) updatesScroll -= 60;
            if (downPressed) updatesScroll += 60;

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
                        Sfx_Play(SFX_MOVE);
                    }
                } else if (settingsRow == SETTINGS_ROW_VOLUME) {
                    if (leftPressed || rightPressed) {
                        settings.volumePercent += rightPressed ? 10 : -10;
                        if (settings.volumePercent < 0) settings.volumePercent = 0;
                        if (settings.volumePercent > 100) settings.volumePercent = 100;
                        Sfx_SetVolume(settings.volumePercent);
                        Music_SetVolume(Prefs_Get()->musicVolume);
                        Settings_Save(&settings);
                    }
                } else if (confirmPressed) {
                    if (settingsRow < SETTINGS_REBIND_ROWS) {
                        awaitingKey = true;
                    } else if (settingsRow == SETTINGS_ROW_AUDIO) {
                        settings.audioEnabled = !settings.audioEnabled;
                        Sfx_SetEnabled(settings.audioEnabled);
                        Music_SetEnabled(settings.audioEnabled);
                        Settings_Save(&settings);
                    } else { /* Back to Menu */
                        appState = APP_MENU;
                    }
                }
            }

            DrawSettings(&settings, settingsRow, awaitingKey);
            continue;
        }

        /* appState == APP_PLAYING */
        bool leftHeld = IsKeyDown(keys.left) || IsKeyDown(keys.left2);
        bool rightHeld = IsKeyDown(keys.right) || IsKeyDown(keys.right2);
        if (leftHeld && rightHeld) {
            leftHeld = false;
            rightHeld = false;
        }
        bool downHeld = IsKeyDown(keys.softDrop) || IsKeyDown(keys.softDrop2);

        bool doLeft = RepeatButton_Update(&leftBtn, leftHeld, dt, 0.17f, 0.035f);
        bool doRight = RepeatButton_Update(&rightBtn, rightHeld, dt, 0.17f, 0.035f);
        bool doSoftDrop = RepeatButton_Update(&softDropBtn, downHeld, dt, 0.0f, 0.03f);

        bool rotatePressed = IsKeyPressed(keys.rotate) || IsKeyPressed(keys.rotate2);
        bool hardDropPressed = IsKeyPressed(keys.hardDrop);
        bool pausePressed = IsKeyPressed(keys.pause);
        bool restartPressed = IsKeyPressed(keys.restart);
        bool quitPressed = IsKeyPressed(keys.quit);

        bool mutePressed = IsKeyPressed(KEY_M);
        bool volDownPressed = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
        bool volUpPressed = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
        bool fpsTogglePressed = IsKeyPressed(KEY_F3);

        if (quitPressed) {
            /* Escape during a game returns to the main menu rather than
             * exiting outright; Escape from the menu itself quits. An
             * abandoned run still counts for what it scored. */
            SubmitRun(&game, username, &runSubmitted);
            appState = APP_MENU;
            DrawMenu(menuIndex, &saveData, username, iconPath, dt);
            continue;
        }
        if (fpsTogglePressed) showFps = !showFps;

        if (mutePressed) {
            settings.audioEnabled = !settings.audioEnabled;
            Sfx_SetEnabled(settings.audioEnabled);
            Music_SetEnabled(settings.audioEnabled);
        }
        if (volDownPressed) {
            settings.volumePercent -= 10;
            if (settings.volumePercent < 0) settings.volumePercent = 0;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }
        if (volUpPressed) {
            settings.volumePercent += 10;
            if (settings.volumePercent > 100) settings.volumePercent = 100;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }

        if (restartPressed) {
            SubmitRun(&game, username, &runSubmitted);
            restartNonce++;
            Game_Restart(&game, MakeSeed(restartNonce));
            runSubmitted = false;
        } else if (pausePressed && game.phase != GS_GAMEOVER) {
            Game_TogglePause(&game);
        }

        if (game.phase == GS_PLAYING) {
            if (doLeft) Game_MoveLeft(&game);
            if (doRight) Game_MoveRight(&game);
            if (doSoftDrop) Game_SoftDrop(&game);
            if (rotatePressed) Game_Rotate(&game, 1);
            if (hardDropPressed) Game_HardDrop(&game);
            if (IsKeyPressed(keys.hold) && Game_Hold(&game)) Sfx_Play(SFX_ROTATE);

            Game_Update(&game, dt * Tune_Speed());
        }

        if (game.justMoved) Sfx_Play(SFX_MOVE);
        if (game.justRotated) Sfx_Play(SFX_ROTATE);
        if (game.justHardDropped) Sfx_Play(SFX_HARD_DROP);
        if (game.justLocked && game.lastClearCount > 0) {
            Sfx_Play(game.lastClearCount >= 4 ? SFX_QUAD : SFX_CLEAR);
        }
        if (game.lastWasLevelUp) Sfx_Play(SFX_LEVEL_UP);

        static bool prevGameOver = false;
        bool nowGameOver = (game.phase == GS_GAMEOVER);
        if (nowGameOver && !prevGameOver) {
            Sfx_Play(SFX_GAME_OVER);
            /* The menu's Best is the classic marathon's: other modes and sizes have their own tables. */
            if (game.cfg.mode == MODE_MARATHON && game.cfg.width == BOARD_W && game.cfg.height == BOARD_H) {
                if (game.score > saveData.highScore) saveData.highScore = game.score;
                if (game.level > saveData.highLevel) saveData.highLevel = game.level;
                if (game.linesCleared > saveData.highLines) saveData.highLines = game.linesCleared;
                SaveData_Save(&saveData);
            }
            if (SubmitRun(&game, username, &runSubmitted) == 1) Sfx_Jingle(JINGLE_NEW_BEST);
        }
        prevGameOver = nowGameOver;

        /* Render first: it starts its flashes and particles off the same
         * one-frame events the sounds above just used. */
        Render_Frame(&game, &settings, showFps);

        Game_ConsumeFrameFlags(&game);
    }

    SubmitRun(&game, username, &runSubmitted);
    if (game.score > saveData.highScore) saveData.highScore = game.score;
    if (game.level > saveData.highLevel) saveData.highLevel = game.level;
    if (game.linesCleared > saveData.highLines) saveData.highLines = game.linesCleared;
    SaveData_Save(&saveData);
    Settings_Save(&settings);

    Ui_Shutdown();
    Music_Shutdown();
    Sfx_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
