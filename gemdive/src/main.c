#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "winscale.h"
#include "tuning.h"
#include "tuningui.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "cave.h"
#include "game.h"
#include "render.h"
#include "persist.h"
#include "version.h"
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
 * single mode Gemdive has on the shared score tables. */
#define GHOST_GAME_NAME "Gemdive"
#define GHOST_GAME_SLUG "gemdive"
#define GHOST_GAME_MODE "arcade"

typedef enum { APP_MENU, APP_PLAYING, APP_SCORES, APP_SETTINGS, APP_UPDATES, APP_HOWTO } AppState;

enum {
    SFX_SELECT, SFX_DIG, SFX_GEM, SFX_PUSH, SFX_THUD, SFX_BOOM, SFX_EXIT_OPEN, SFX_DEATH, SFX_CLEAR,
    SFX_EXTRA_LIFE, SFX_TICK, SFX_GAME_OVER, SFX_COUNT
};
static const char *const kSfxFiles[SFX_COUNT] = {
    "select.wav", "dig.wav", "gem.wav", "push.wav", "thud.wav", "boom.wav", "exitopen.wav", "death.wav", "clear.wav",
    "extralife.wav", "tick.wav", "gameover.wav",
};

static const char *const kNotes_1_0_0[] = {
    "Initial release: dig through dirt, collect enough gems to open the exit, and get out before time runs out",
    "Boulders and gems fall, roll off anything round, and crush you if they were already falling when they reach you",
    "Push a boulder sideways onto empty ground; never up or down",
    "Crawlers hug the wall with their left hand and kill on touch; a boulder dropped on one blows a 3x3 hole, and some crawlers leave gems behind",
    "Twenty caves, each with a recorded solution that the test suite replays, so every one can be finished",
    "Lose a life and the cave starts again; an extra life every 3,000 points; the set speeds up each time round",
    "Optional CRT scanlines; shares its score table and your name with Ghost Launcher",
    NULL,
};
static const UiChangelogEntry kChangelog[] = {
    {"1.0.0", "2026-09-20", kNotes_1_0_0},
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

#define SETTINGS_REBIND_ROWS 11
#define SETTINGS_ROW_SCANLINES 11
#define SETTINGS_ROW_VOLUME 12
#define SETTINGS_ROW_AUDIO 13
#define SETTINGS_ROW_COUNT 15

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

static void BuildSettingsRows(Settings *s, UiSettingsRow rows[SETTINGS_ROW_COUNT]) {
    static const char *kLabels[SETTINGS_REBIND_ROWS] = {
        "Up", "Up, second key", "Down", "Down, second key", "Left", "Left, second key",
        "Right", "Right, second key", "Pause", "Restart", "Back to menu",
    };
    for (int i = 0; i < SETTINGS_REBIND_ROWS; i++) {
        rows[i] = (UiSettingsRow){UI_ROW_KEY, kLabels[i], SettingsRowField(s, i), false, 0};
    }
    rows[SETTINGS_ROW_SCANLINES] = (UiSettingsRow){UI_ROW_TOGGLE, "CRT scanlines", NULL, s->scanlines, 0};
    rows[SETTINGS_ROW_VOLUME] = (UiSettingsRow){UI_ROW_VOLUME, "Effects volume", NULL, false, s->volumePercent};
    rows[SETTINGS_ROW_AUDIO] = (UiSettingsRow){UI_ROW_TOGGLE, "Sound", NULL, s->audioEnabled, 0};
    rows[SETTINGS_ROW_COUNT - 1] = (UiSettingsRow){UI_ROW_DONE, "Done", NULL, false, 0};
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
        snprintf(buf, bufSize, "%s/gemdive/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/gemdive/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;

    if (DirectoryExists("assets")) snprintf(buf, bufSize, "assets");
}

/* Everything that must happen exactly once when a run ends, however it ends. */
static int FinalizeRun(const Game *g, SaveData *save, const char *username, bool *finalized) {
    if (*finalized) return 0;
    *finalized = true;
    if (Tune_Modified()) return 0; /* a tuned run is a playtest, not a score */
    RunLog_Append(GHOST_GAME_SLUG, GHOST_GAME_MODE, (long)g->score, (int)g->level);
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
    int rank = GhostLink_SubmitScore(GHOST_GAME_SLUG, GHOST_GAME_MODE, username, g->score, date);
    GhostLink_TriggerSync();
    return rank;
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
    GhostLink_TriggerSync(); /* refresh the online board while the player is on the menu */

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Gemdive");
    SetExitKey(KEY_NULL); /* we handle Escape ourselves */
    SetTargetFPS(60);

    char assetsDir[512], iconPath[600];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));
    snprintf(iconPath, sizeof(iconPath), "%s/icons/gemdive.png", assetsDir);

    UiTheme theme = Ui_DefaultTheme((Color){255, 208, 80, 255});
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

    static Cave caves[40];
    int caveCount = 0;
    for (int i = 1; i <= 40; i++) {
        char path[640];
        snprintf(path, sizeof(path), "%s/caves/%02d.cave", assetsDir, i);
        if (!FileExists(path)) break;
        if (Cave_LoadFile(path, &caves[caveCount], NULL)) caveCount++;
    }
    if (caveCount == 0) { fprintf(stderr, "gemdive: no caves found in %s/caves\n", assetsDir); CloseWindow(); return 1; }

    Game game;
    Game_Init(&game, caves, caveCount, saveData.highScore);
    bool runFinalized = true; /* nothing to record until a run actually starts */
    int lastRank = 0;
    bool showFps = false;

    AppState appState = HowTo_Seen(GHOST_GAME_SLUG) ? APP_MENU : APP_HOWTO; /* first launch shows the how-to card */
    int menuIndex = 0, settingsRow = 0, updatesScroll = 0;
    bool awaitingKey = false;

    bool scoresGlobal = false;
    int scoresPeriod = 0;
    float scoresReloadTimer = 0.0f;
    static GhostScore scoreEntries[GHOSTLINK_TOP_N];
    int scoreCount = 0;

    static const char *const kMenuItems[] = {"Start game", "How to play", "Scores", "Settings", "Updates", "Quit"};
    enum { MENU_START, MENU_HOWTO, MENU_SCORES, MENU_SETTINGS, MENU_UPDATES, MENU_QUIT, MENU_COUNT };

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
            if (upPressed) menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
            if (downPressed) menuIndex = (menuIndex + 1) % MENU_COUNT;
            if (upPressed || downPressed) Sfx_Play(SFX_SELECT);
            if (escPressed) break;

            if (confirmPressed) {
                if (menuIndex == MENU_START) {
                    /* Re-read the profile each run so a name change made in
                     * Ghost Launcher applies without restarting the game. */
                    GhostLink_GetUsername(GHOST_GAME_NAME, username, sizeof(username));
                    Game_Init(&game, caves, caveCount, saveData.highScore);
                    Ui_ClearParticles();
                    runFinalized = false;
                    lastRank = 0;
                    appState = APP_PLAYING;
                    Sfx_Jingle(JINGLE_READY);
                    continue;
                } else if (menuIndex == MENU_SCORES) {
                    scoresReloadTimer = 99.0f; /* load on the first frame */
                    appState = APP_SCORES;
                } else if (menuIndex == MENU_SETTINGS) {
                    appState = APP_SETTINGS;
                    settingsRow = 0;
                    awaitingKey = false;
                } else if (menuIndex == MENU_HOWTO) {
                    appState = APP_HOWTO;
                } else if (menuIndex == MENU_UPDATES) {
                    appState = APP_UPDATES;
                    updatesScroll = 0;
                } else {
                    break;
                }
            }

            char statLine[96];
            snprintf(statLine, sizeof(statLine), "Best  %ld      furthest  level %d", saveData.highScore, saveData.bestLevel);
            UiMenu menu = {
                .title = "GEMDIVE", .tagline = "Dive deeper. Do not get crushed.",
                .items = kMenuItems, .itemCount = MENU_COUNT, .selected = menuIndex,
                .statLine = statLine, .username = username, .hints = "Up/Down choose    Enter select",
                .iconPath = iconPath, .versionLabel = "v" GEMDIVE_VERSION, .backdrop = Render_MenuBackdrop,
            };
            Ui_DrawMenu(&menu, dt);
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
                    ? GhostLink_LoadGlobalScores(GHOST_GAME_SLUG, GHOST_GAME_MODE, scoreEntries, GHOSTLINK_TOP_N)
                    : GhostLink_LoadScores(GHOST_GAME_SLUG, GHOST_GAME_MODE, scoreEntries, GHOSTLINK_TOP_N);
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
            Ui_DrawUpdates(kChangelog, (int)(sizeof(kChangelog) / sizeof(kChangelog[0])), &updatesScroll);
            continue;
        }

        if (appState == APP_SETTINGS) {
            if (awaitingKey) {
                int captured = GetKeyPressed();
                if (captured == KEY_ESCAPE) {
                    awaitingKey = false;
                } else if (captured != 0) {
                    const char *name = Keys_NameFromCode(captured);
                    char *field = SettingsRowField(&settings, settingsRow);
                    if (name && field) {
                        snprintf(field, KEYNAME_LEN, "%s", name);
                        Settings_Save(&settings);
                        BuildKeyMap(&settings, &keys);
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
                    } else if (settingsRow == SETTINGS_ROW_SCANLINES) {
                        settings.scanlines = !settings.scanlines;
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

            UiSettingsRow rows[SETTINGS_ROW_COUNT];
            BuildSettingsRows(&settings, rows);
            Ui_DrawSettingsPlus(rows, SETTINGS_ROW_COUNT, settingsRow, awaitingKey);
            continue;
        }

        /* appState == APP_PLAYING */
        if (IsKeyPressed(keys.quit)) {
            /* Escape during a game returns to the menu; an abandoned run
             * still counts for what it scored. */
            FinalizeRun(&game, &saveData, username, &runFinalized);
            appState = APP_MENU;
            continue;
        }
        if (IsKeyPressed(KEY_F3)) showFps = !showFps;
        if (IsKeyPressed(KEY_M)) {
            settings.audioEnabled = !settings.audioEnabled;
            Sfx_SetEnabled(settings.audioEnabled);
            Music_SetEnabled(settings.audioEnabled);
        }
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
            settings.volumePercent = settings.volumePercent >= 10 ? settings.volumePercent - 10 : 0;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
            settings.volumePercent = settings.volumePercent <= 90 ? settings.volumePercent + 10 : 100;
            Sfx_SetVolume(settings.volumePercent);
            Music_SetVolume(Prefs_Get()->musicVolume);
        }

        if (IsKeyPressed(keys.restart)) {
            FinalizeRun(&game, &saveData, username, &runFinalized);
            Game_Restart(&game, saveData.highScore);
            Ui_ClearParticles();
            runFinalized = false;
            lastRank = 0;
        } else if (IsKeyPressed(keys.pause) && game.phase != GS_GAMEOVER) {
            Game_TogglePause(&game);
        }

        {
            int dx = 0, dy = 0;
            if (IsKeyDown(keys.left) || IsKeyDown(keys.left2)) dx -= 1;
            if (IsKeyDown(keys.right) || IsKeyDown(keys.right2)) dx += 1;
            if (IsKeyDown(keys.up) || IsKeyDown(keys.up2)) dy -= 1;
            if (IsKeyDown(keys.down) || IsKeyDown(keys.down2)) dy += 1;
            Game_SetInput(&game, dx, dy);
        }

        Game_Update(&game, dt * Tune_Speed());

        if (game.justDig) Sfx_Play(SFX_DIG);
        if (game.justGem) Sfx_Play(SFX_GEM);
        if (game.justPush) Sfx_Play(SFX_PUSH);
        if (game.justRockLand) Sfx_Play(SFX_THUD);
        if (game.justExplode) Sfx_Play(SFX_BOOM);
        if (game.justExitOpen) Sfx_Play(SFX_EXIT_OPEN);
        if (game.justDeath) Sfx_Play(SFX_DEATH);
        if (game.justClear) Sfx_Play(SFX_CLEAR);
        if (game.justExtraLife) Sfx_Play(SFX_EXTRA_LIFE);
        if (game.justTimeWarning) Sfx_Play(SFX_TICK);
        if (game.justGameOver) {
            Sfx_Play(SFX_GAME_OVER);
            lastRank = FinalizeRun(&game, &saveData, username, &runFinalized);
            if (lastRank == 1) Sfx_Jingle(JINGLE_NEW_BEST);
        }

        /* Render first: it starts its bursts and pop-ups off the same
         * one-frame events the sounds above just used. */
        FrameInfo info = {username, lastRank, settings.scanlines, showFps};
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
