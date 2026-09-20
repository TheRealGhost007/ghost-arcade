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
#include "gamepad.h" /* after raylib.h: routes IsKeyDown/IsKeyPressed through the controller too */
#include "keynames.h"
#include "ghostlink.h"

/* Catalog name in Ghost Launcher (profile lookups) and score-file slug. */
#define GHOST_GAME_NAME "Coilrush"
#define GHOST_GAME_SLUG "coilrush"

enum { SFX_SELECT, SFX_EAT, SFX_BONUS_SPAWN, SFX_BONUS_EAT, SFX_LEVEL_UP, SFX_GAME_OVER, SFX_COUNT };
static const char *const kSfxFiles[SFX_COUNT] = {
    "select.wav", "eat.wav", "bonus_spawn.wav", "bonus_eat.wav", "levelup.wav", "gameover.wav",
};

typedef struct {
    int up, up2, down, down2, left, left2, right, right2, pause, restart, quit;
} KeyMap;

static void BuildKeyMap(const Settings *s, KeyMap *out) {
    out->up = Keys_CodeFromName(s->up);
    out->up2 = Keys_CodeFromName(s->up2);
    out->down = Keys_CodeFromName(s->down);
    out->down2 = Keys_CodeFromName(s->down2);
    out->left = Keys_CodeFromName(s->left);
    out->left2 = Keys_CodeFromName(s->left2);
    out->right = Keys_CodeFromName(s->right);
    out->right2 = Keys_CodeFromName(s->right2);
    out->pause = Keys_CodeFromName(s->pause);
    out->restart = Keys_CodeFromName(s->restart);
    out->quit = Keys_CodeFromName(s->quit);
}

/* Rows: 11 rebindable actions, then Volume, Sound, Done. */
#define SETTINGS_REBIND_ROWS 11
#define SETTINGS_ROW_VOLUME 11
#define SETTINGS_ROW_AUDIO 12
#define SETTINGS_ROW_BACK 13
#define SETTINGS_ROW_COUNT 14

#define MENU_ITEM_COUNT 7
#define MENU_ITEM_START 0
#define MENU_ITEM_MODE 1
#define MENU_ITEM_HOWTO 2
#define MENU_ITEM_SCORES 3
#define MENU_ITEM_SETTINGS 4
#define MENU_ITEM_UPDATES 5
#define MENU_ITEM_QUIT 6

static const char *const kModeTitle[MODE_COUNT] = {"Classic", "Wrap", "Maze"};
static const char *const kModeBlurb[MODE_COUNT] = {
    "The edges are deadly", "The edges loop around", "Walls, stages, no mercy",
};

typedef enum {
    APP_MENU,
    APP_PLAYING,
    APP_SCORES,
    APP_SETTINGS,
    APP_UPDATES,
    APP_HOWTO,
} AppState;

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
        snprintf(buf, bufSize, "%s/coilrush/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/coilrush/assets", home ? home : "");
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
        case 0: return s->up;
        case 1: return s->up2;
        case 2: return s->down;
        case 3: return s->down2;
        case 4: return s->left;
        case 5: return s->left2;
        case 6: return s->right;
        case 7: return s->right2;
        case 8: return s->pause;
        case 9: return s->restart;
        case 10: return s->quit;
        default: return NULL;
    }
}

static void TodayString(char *out, size_t outSize) {
    time_t now = time(NULL);
    struct tm tmNow;
    if (localtime_r(&now, &tmNow) == NULL || strftime(out, outSize, "%Y-%m-%d", &tmNow) == 0) {
        snprintf(out, outSize, "unknown");
    }
}

/* Everything that must happen exactly once when a run ends, however it
 * ends (death, board full, restart mid-run, back to menu, window closed):
 * fold it into the local bests and submit it to the shared score table.
 * Returns the rank reached on the table (0 = none). */
static int FinalizeRun(const Game *g, SaveData *save, const char *username, bool *finalized) {
    if (*finalized) return 0;
    *finalized = true;
    if (Tune_Modified()) return 0; /* a tuned run is a playtest, not a score */
    RunLog_Append(GHOST_GAME_SLUG, Game_ModeKey(g->mode), (long)g->score, (int)g->level);
    if (Ach_CheckRun(GHOST_GAME_SLUG, (long)g->score, (int)g->level) > 0) Sfx_Jingle(JINGLE_LEVEL_UP);

    bool changed = false;
    if (g->score > save->highScore[g->mode]) {
        save->highScore[g->mode] = g->score;
        changed = true;
    }
    if (g->length > save->bestLength[g->mode]) {
        save->bestLength[g->mode] = g->length;
        changed = true;
    }
    if (changed) SaveData_Save(save);

    char date[GHOSTLINK_DATE_LEN];
    TodayString(date, sizeof(date));
    int rank = GhostLink_SubmitScore(GHOST_GAME_SLUG, Game_ModeKey(g->mode), username, g->score, date);

    /* Push the new row to the online board (and refresh the global cache)
     * in the background; the game itself never waits on the network. */
    if (g->score > 0) GhostLink_TriggerSync();
    return rank;
}

static int LoadScoreView(GameMode mode, bool global, GhostScore *out) {
    return global ? GhostLink_LoadGlobalScores(GHOST_GAME_SLUG, Game_ModeKey(mode), out, GHOSTLINK_TOP_N)
                  : GhostLink_LoadScores(GHOST_GAME_SLUG, Game_ModeKey(mode), out, GHOSTLINK_TOP_N);
}

static void DrawMenu(int selected, GameMode mode, const SaveData *save, const char *username, const char *iconPath, float dt) {
    char modeLabel[48], statLine[96];
    snprintf(modeLabel, sizeof(modeLabel), "< %s >", kModeTitle[mode]);
    snprintf(statLine, sizeof(statLine), "%s best  %ld      longest  %d", kModeTitle[mode], save->highScore[mode], save->bestLength[mode]);
    const char *items[MENU_ITEM_COUNT] = {"Start game", modeLabel, "How to play", "Scores", "Settings", "Updates", "Quit"};
    const char *blurbs[MENU_ITEM_COUNT] = {NULL, kModeBlurb[mode], NULL, NULL, NULL, NULL, NULL};
    UiMenu menu = {
        .title = "COILRUSH", .tagline = "Eat. Grow. Don't bite yourself.",
        .items = items, .blurbs = blurbs, .itemCount = MENU_ITEM_COUNT, .selected = selected,
        .statLine = statLine, .username = username, .hints = "Up/Down choose    Left/Right mode    Enter select",
        .iconPath = iconPath, .versionLabel = "v" COILRUSH_VERSION, .backdrop = Render_MenuBackdrop,
    };
    Ui_DrawMenu(&menu, dt);
}

static void DrawSettings(Settings *s, int selected, bool awaitingKey) {
    static const char *kLabels[SETTINGS_REBIND_ROWS] = {
        "Up", "Up, second key", "Down", "Down, second key", "Left", "Left, second key",
        "Right", "Right, second key", "Pause", "Restart", "Back to menu",
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

    SaveData saveData;
    SaveData_Load(&saveData);

    KeyMap keys;
    BuildKeyMap(&settings, &keys);

    char username[GHOSTLINK_NAME_LEN];
    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));

    /* Refresh the global board while the player is still on the menu. */
    GhostLink_TriggerSync();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Coilrush");
    SetExitKey(KEY_NULL); /* we handle Escape ourselves */
    SetTargetFPS(60);     /* safety cap alongside vsync; keeps CPU/GPU low and stable */

    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    char iconPath[600];
    snprintf(iconPath, sizeof(iconPath), "%s/icons/coilrush.png", assetsDir);
    UiTheme theme = Ui_DefaultTheme((Color){96, 220, 130, 255});
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

    GameMode mode = settings.mode;
    Game game;
    Game_Init(&game, MakeSeed(0), mode, saveData.highScore[mode]);
    bool runFinalized = true; /* nothing to record until a run actually starts */
    int lastRank = 0;

    int restartNonce = 1;
    bool showFps = false;

    AppState appState = HowTo_Seen(GHOST_GAME_SLUG) ? APP_MENU : APP_HOWTO; /* first launch shows the how-to card */
    int menuIndex = 0;
    int settingsRow = 0;
    bool awaitingKey = false;
    int updatesScroll = 0;

    GameMode scoresMode = mode;
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

            bool cycleBack = (menuIndex == MENU_ITEM_MODE && leftPressed);
            bool cycleFwd = (menuIndex == MENU_ITEM_MODE && (rightPressed || confirmPressed));
            if (cycleBack || cycleFwd) {
                mode = (GameMode)((mode + (cycleFwd ? 1 : MODE_COUNT - 1)) % MODE_COUNT);
                settings.mode = mode;
                Settings_Save(&settings);
                Sfx_Play(SFX_SELECT);
            } else if (confirmPressed) {
                if (menuIndex == MENU_ITEM_START) {
                    /* Re-read the profile each run so a name change made in
                     * Ghost Launcher applies without restarting the game. */
                    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));
                    restartNonce++;
                    Game_Init(&game, MakeSeed(restartNonce), mode, saveData.highScore[mode]);
                    runFinalized = false;
                    lastRank = 0;
                    appState = APP_PLAYING;
                    Sfx_Jingle(JINGLE_READY);
                } else if (menuIndex == MENU_ITEM_SCORES) {
                    scoresMode = mode;
                    scoreCount = LoadScoreView(scoresMode, scoresGlobal, scoreEntries);
                    scoresReloadTimer = 0.0f;
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

            DrawMenu(menuIndex, mode, &saveData, username, iconPath, dt);
            continue;
        }

        if (appState == APP_SCORES) {
            if (IsKeyPressed(KEY_T)) scoresPeriod = (scoresPeriod + 1) % UI_PERIOD_COUNT;
            if (escPressed) appState = APP_MENU;
            bool viewChanged = false;
            if (leftPressed || rightPressed) {
                scoresMode = (GameMode)((scoresMode + (rightPressed ? 1 : MODE_COUNT - 1)) % MODE_COUNT);
                viewChanged = true;
            }
            if (upPressed || downPressed) {
                scoresGlobal = !scoresGlobal;
                viewChanged = true;
            }
            if (viewChanged) Sfx_Play(SFX_SELECT);

            /* Touch the file when the view changes, plus a slow poll so a
             * background sync that just finished shows up -- never per frame. */
            scoresReloadTimer += dt;
            if (viewChanged || scoresReloadTimer >= 2.0f) {
                scoreCount = LoadScoreView(scoresMode, scoresGlobal, scoreEntries);
                scoresReloadTimer = 0.0f;
            }

            char scoreLabel[48];
            snprintf(scoreLabel, sizeof(scoreLabel), "< %s >", kModeTitle[scoresMode]);
            Ui_DrawScoresV2(scoreLabel, scoresGlobal, (UiScorePeriod)scoresPeriod, scoreEntries, scoreCount, username,
                          "Left/Right mode    Up/Down this machine or world    T period    Esc back");
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
            FinalizeRun(&game, &saveData, username, &runFinalized);
            appState = APP_MENU;
            DrawMenu(menuIndex, mode, &saveData, username, iconPath, dt);
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
            FinalizeRun(&game, &saveData, username, &runFinalized);
            restartNonce++;
            Game_Restart(&game, MakeSeed(restartNonce));
            runFinalized = false;
            lastRank = 0;
        } else if (pausePressed && game.phase != GS_GAMEOVER) {
            Game_TogglePause(&game);
        }

        /* Turns are queued in press order, so "up then left" inside a single
         * step still plays out as two turns on consecutive steps. */
        if (IsKeyPressed(keys.up) || IsKeyPressed(keys.up2)) Game_QueueTurn(&game, DIR_UP);
        if (IsKeyPressed(keys.down) || IsKeyPressed(keys.down2)) Game_QueueTurn(&game, DIR_DOWN);
        if (IsKeyPressed(keys.left) || IsKeyPressed(keys.left2)) Game_QueueTurn(&game, DIR_LEFT);
        if (IsKeyPressed(keys.right) || IsKeyPressed(keys.right2)) Game_QueueTurn(&game, DIR_RIGHT);

        Game_Update(&game, dt * Tune_Speed());

        if (game.justAteBonus) Sfx_Play(SFX_BONUS_EAT);
        else if (game.justLeveledUp) Sfx_Play(SFX_LEVEL_UP);
        else if (game.justAte) Sfx_Play(SFX_EAT);
        if (game.justBonusSpawned || game.justRelicSpawned) Sfx_Play(SFX_BONUS_SPAWN);
        if (game.justRelic) Sfx_Play(SFX_BONUS_EAT);

        if (game.justDied) {
            Sfx_Play(game.won ? SFX_LEVEL_UP : SFX_GAME_OVER);
            lastRank = FinalizeRun(&game, &saveData, username, &runFinalized);
            if (lastRank == 1) Sfx_Jingle(JINGLE_NEW_BEST);
        }

        Game_ConsumeFrameFlags(&game);

        FrameInfo info = {username, lastRank, showFps};
        Render_Frame(&game, &info);
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
