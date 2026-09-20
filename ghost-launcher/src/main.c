#include "raylib.h"
#include "winscale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "manifest.h"
#include "render.h"
#include "adminlock.h"
#include "launch.h"
#include "stats.h"
#include "runstats.h"
#include "../sync/syncdata.h"
#include "profile.h"
#include "scores.h"
#include "gamepad.h" /* after raylib.h: routes IsKeyDown/IsKeyPressed through the controller too */
#include <sys/stat.h>

/* Games whose seed changes what you play, so a shared daily seed means something. */
static bool DailySupported(const GameEntry *e) {
    static const char *const kDaily[] = {"brickburst", "skyraid", "rockdrift", "lanehop", "crawlshot", "moondrop", "girderclimb", NULL};
    char slug[64];
    if (!Scores_SlugFromExec(e->exec, slug, sizeof(slug))) return false;
    for (int i = 0; kDaily[i]; i++) if (strcmp(slug, kDaily[i]) == 0) return true;
    return false;
}

typedef enum {
    UI_LIST,
    UI_ADD_GAME,
    UI_EDIT_GAME,
    UI_STATS,
    UI_PROFILE,
    UI_TROPHIES,
    UI_ONLINE,
} UIState;

/* Order matters: the XDG install location is checked before a bare
 * CWD-relative "assets" match, since the latter can accidentally match an
 * unrelated directory's assets folder (e.g. another installed app's
 * project dir) if the shell happens to be sitting in one when this binary
 * is launched from PATH. */
static void ResolveAssetsDir(char *buf, size_t bufSize) {
    const char *appDir = GetApplicationDirectory();
    snprintf(buf, bufSize, "%sassets", appDir);
    if (DirectoryExists(buf)) return;

    const char *xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        snprintf(buf, bufSize, "%s/ghost-launcher/assets", xdgData);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, bufSize, "%s/.local/share/ghost-launcher/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;

    if (DirectoryExists("assets")) {
        snprintf(buf, bufSize, "assets");
    }
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
    if (IsKeyPressed(KEY_BACKSPACE)) {
        int len = (int)strlen(buf);
        if (len > 0) buf[len - 1] = '\0';
    }
}

/* Gathers what the instruction card shows for one game. Reads two small
 * text files, so it is called on selection changes and a slow timer, never
 * per frame. */
static void RefreshCard(GameCard *card, const GameEntry *e, const PlayStats *stats, const Profiles *profiles) {
    memset(card, 0, sizeof(*card));
    char slug[SCORES_SLUG_LEN];
    if (Scores_SlugFromExec(e->exec, slug, sizeof(slug))) {
        card->localBest = Scores_LoadBest(slug, false);
        card->worldBest = Scores_LoadBest(slug, true);
    }
    card->playSeconds = Stats_GetGameTime(stats, e->name);
    card->playerName = Profiles_GetUsername(profiles, e->name);
    card->nameIsOverride = Profiles_GetOwnUsername(profiles, e->name)[0] != '\0';
}

static bool IsInstalled(const GameEntry *e) {
    char expanded[512];
    Manifest_ExpandPath(e->exec, expanded, sizeof(expanded));
    struct stat st;
    return stat(expanded, &st) == 0 && (st.st_mode & S_IXUSR);
}

static bool CloseButtonClicked(void) {
    Vector2 mp = GetMousePosition();
    bool over = mp.x >= CLOSE_BTN_X && mp.x <= CLOSE_BTN_X + CLOSE_BTN_SIZE &&
                mp.y >= CLOSE_BTN_Y && mp.y <= CLOSE_BTN_Y + CLOSE_BTN_SIZE;
    return over && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

int main(void) {
    char assetsDir[512];
    ResolveAssetsDir(assetsDir, sizeof(assetsDir));

    Manifest manifest;
    Manifest_Load(&manifest, assetsDir);

    PlayStats stats;
    Stats_Load(&stats);

    Profiles profiles;
    Profiles_Load(&profiles);

    bool adminUnlocked = AdminLock_IsUnlocked();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ghost Launcher");
    Win_Init(WINDOW_WIDTH, WINDOW_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Render_Init(assetsDir);

    /* Two sounds, both optional: no audio device just means a silent coin. */
    InitAudioDevice();
    Sound coinSound = {0}, moveSound = {0};
    bool soundsLoaded = false;
    if (IsAudioDeviceReady()) {
        char soundPath[600];
        snprintf(soundPath, sizeof(soundPath), "%s/sounds/coin.wav", assetsDir);
        if (FileExists(soundPath)) coinSound = LoadSound(soundPath);
        snprintf(soundPath, sizeof(soundPath), "%s/sounds/move.wav", assetsDir);
        if (FileExists(soundPath)) moveSound = LoadSound(soundPath);
        soundsLoaded = true;
        SetSoundVolume(coinSound, 0.7f);
        SetSoundVolume(moveSound, 0.5f);
    }

    /* Refresh the online boards in the background while the player browses. */
    Launch_SyncDetached();

    double launcherStartTime = GetTime();

    UIState uiState = UI_LIST;
    int selected = 0;
    int trophyGame = 0;
    SyncConfig syncCfg;
    SyncConfig_Load(&syncCfg);
    bool onlineChoice = syncCfg.enabled, onlineFirstTime = !syncCfg.decided;
    if (onlineFirstTime) uiState = UI_ONLINE; /* sharing scores is opt-in: ask before anything is uploaded */
    static RunSummary runSums[MANIFEST_MAX_GAMES];
    bool coinDaily = false; /* the coin in flight is for a daily challenge */

    char errorMsg[128] = "";
    float errorMsgTimer = 0.0f;

    /* Shared by both the Add and Edit game forms. */
    int formStep = 0;
    char formBuffers[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    memset(formBuffers, 0, sizeof(formBuffers));
    char formError[128] = "";
    int editingIndex = -1;

    char profileBuffer[PROFILE_USERNAME_LEN] = "";
    bool profileForGame = false; /* false = editing the arcade-wide name */

    GameCard card;
    memset(&card, 0, sizeof(card));
    card.playerName = "";
    int cardFor = -1;
    float cardAge = 0.0f;
    static bool installed[MANIFEST_MAX_GAMES];

    /* The coin drop between Enter and the game starting. coinTimer < 0 means
     * no coin in flight; while one is, the row is locked. */
    float coinTimer = -1.0f;
    bool coinDinged = false;

    bool shouldQuit = false;
    while (!WindowShouldClose() && !shouldQuit) {
        Win_Update();
        float dt = GetFrameTime();
        Pad_Update();
        double now = GetTime();

        InstallStatus installBefore = Launch_InstallStatus(NULL);
        if (Launch_ReapFinished(now, &stats)) {
            Stats_Save(&stats);
            cardFor = -1; /* a game just closed: its scores and playtime changed */
        }
        if (Launch_InstallStatus(NULL) != installBefore) {
            cardFor = -1;                 /* an install just finished */
            Render_ForgetMissingIcons();  /* its icon probably exists now */
        }

        if (errorMsgTimer > 0.0f) {
            errorMsgTimer -= dt;
            if (errorMsgTimer <= 0.0f) errorMsg[0] = '\0';
        }

        if (CloseButtonClicked()) {
            shouldQuit = true;
            continue;
        }

        if (uiState == UI_LIST && coinTimer >= 0.0f) {
            coinTimer += dt;
            if (!coinDinged && coinTimer >= COIN_DING_AT * COIN_ANIM_SECONDS) {
                coinDinged = true;
                if (soundsLoaded) PlaySound(coinSound);
            }
            if (coinTimer >= COIN_ANIM_SECONDS) {
                coinTimer = -1.0f;
                const GameEntry *e = &manifest.games[selected];
                char expanded[512], launchErr[96] = "";
                Manifest_ExpandPath(e->exec, expanded, sizeof(expanded));
                bool wasDaily = coinDaily;
                coinDaily = false;
                if (!Launch_GameArg(expanded, e->name, now, wasDaily ? "--daily" : NULL, launchErr, sizeof(launchErr))) {
                    snprintf(errorMsg, sizeof(errorMsg), "%s", launchErr);
                    errorMsgTimer = 4.0f;
                }
            }
            Render_List(&manifest, selected, installed, &card,
                        Profiles_GetOwnUsername(&profiles, PROFILE_DEFAULT_KEY), adminUnlocked, errorMsg, dt,
                        coinTimer >= 0.0f ? coinTimer / COIN_ANIM_SECONDS : -1.0f);
            continue;
        }

        if (uiState == UI_LIST) {
            /* The row runs left-to-right; Up/Down still work so nobody has
             * to relearn the old list's keys. */
            bool upPressed = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
            bool downPressed = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
            bool confirmPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
            bool addPressed = adminUnlocked && IsKeyPressed(KEY_A);
            bool editPressed = adminUnlocked && IsKeyPressed(KEY_E);
            bool removePressed = adminUnlocked && IsKeyPressed(KEY_DELETE);
            bool statsPressed = IsKeyPressed(KEY_L);
            bool trophiesPressed = IsKeyPressed(KEY_H);
            bool dailyPressed = IsKeyPressed(KEY_D);
            if (IsKeyPressed(KEY_O)) { onlineChoice = syncCfg.enabled; uiState = UI_ONLINE; }
            bool profilePressed = IsKeyPressed(KEY_U);
            bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

            if (manifest.count > 0) {
                if (upPressed) selected = (selected + manifest.count - 1) % manifest.count;
                if (downPressed) selected = (selected + 1) % manifest.count;
                if ((upPressed || downPressed) && manifest.count > 1 && soundsLoaded) PlaySound(moveSound);

                if (dailyPressed && IsInstalled(&manifest.games[selected]) && DailySupported(&manifest.games[selected])) {
                    coinTimer = 0.0f;
                    coinDinged = false;
                    coinDaily = true;
                }

                if (confirmPressed) {
                    coinDaily = false;
                    const GameEntry *e = &manifest.games[selected];
                    char expanded[512];
                    char launchErr[96] = "";
                    bool ok;
                    if (!IsInstalled(e) && e->install[0] != '\0') {
                        /* Enter on a machine that isn't installed installs it. */
                        Launch_InstallAcknowledge();
                        Manifest_ExpandPath(e->install, expanded, sizeof(expanded));
                        ok = Launch_InstallGame(expanded, e->name, launchErr, sizeof(launchErr));
                        cardFor = -1;
                    } else if (IsInstalled(e)) {
                        /* Insert coin: the launch itself happens when the
                         * animation finishes (see the top of this state). */
                        coinTimer = 0.0f;
                        coinDinged = false;
                        ok = true;
                    } else {
                        Manifest_ExpandPath(e->exec, expanded, sizeof(expanded));
                        ok = Launch_Game(expanded, e->name, now, launchErr, sizeof(launchErr)); /* reports why it can't */
                    }
                    if (!ok) {
                        snprintf(errorMsg, sizeof(errorMsg), "%s", launchErr);
                        errorMsgTimer = 4.0f;
                    }
                }

                if (removePressed) {
                    Manifest_RemoveGame(&manifest, selected);
                    Manifest_Save(&manifest);
                    cardFor = -1;
                    if (selected >= manifest.count) selected = manifest.count > 0 ? manifest.count - 1 : 0;
                }

                if (editPressed) {
                    const GameEntry *e = &manifest.games[selected];
                    snprintf(formBuffers[0], MANIFEST_FIELD_LEN, "%s", e->name);
                    snprintf(formBuffers[1], MANIFEST_FIELD_LEN, "%s", e->exec);
                    snprintf(formBuffers[2], MANIFEST_FIELD_LEN, "%s", e->icon);
                    snprintf(formBuffers[3], MANIFEST_FIELD_LEN, "%s", e->desc);
                    snprintf(formBuffers[4], MANIFEST_FIELD_LEN, "%s", e->install);
                    editingIndex = selected;
                    formStep = 0;
                    formError[0] = '\0';
                    uiState = UI_EDIT_GAME;
                    while (GetCharPressed() != 0) { }
                }

            }

            /* U = the arcade-wide name (works with an empty catalog too);
             * Shift+U = an override for just the selected game. */
            if (profilePressed) {
                profileForGame = shiftHeld && manifest.count > 0;
                const char *key = profileForGame ? manifest.games[selected].name : PROFILE_DEFAULT_KEY;
                snprintf(profileBuffer, sizeof(profileBuffer), "%s", Profiles_GetOwnUsername(&profiles, key));
                uiState = UI_PROFILE;
                while (GetCharPressed() != 0) { }
            }

            if (addPressed) {
                uiState = UI_ADD_GAME;
                formStep = 0;
                memset(formBuffers, 0, sizeof(formBuffers));
                formError[0] = '\0';
                while (GetCharPressed() != 0) { }
            }

            if (statsPressed) {
                uiState = UI_STATS;
                time_t statsNow = time(NULL);
                for (int i = 0; i < manifest.count; i++) {
                    char slug[64];
                    if (Scores_SlugFromExec(manifest.games[i].exec, slug, sizeof(slug))) RunStats_Load(slug, statsNow, &runSums[i]);
                    else memset(&runSums[i], 0, sizeof(runSums[i]));
                }
                while (GetCharPressed() != 0) { }
            }
            if (trophiesPressed) {
                uiState = UI_TROPHIES;
                trophyGame = selected;
                while (GetCharPressed() != 0) { }
            }

            cardAge += dt;
            if (manifest.count > 0 && (cardFor != selected || cardAge > 3.0f)) {
                /* A handful of stat() calls and two small file reads, a few
                 * times a minute -- never per frame. */
                for (int i = 0; i < manifest.count; i++) installed[i] = IsInstalled(&manifest.games[i]);
                const GameEntry *e = &manifest.games[selected];
                RefreshCard(&card, e, &stats, &profiles);
                card.installed = installed[selected];
                card.canInstall = !card.installed && e->install[0] != '\0';
                card.supportsDaily = card.installed && DailySupported(e);
                cardFor = selected;
                cardAge = 0.0f;
            }
            if (manifest.count > 0) {
                const char *installingName = "";
                InstallStatus st = Launch_InstallStatus(&installingName);
                card.install = (strcmp(installingName, manifest.games[selected].name) == 0) ? st : INSTALL_IDLE;
                if (card.install == INSTALL_DONE) card.install = INSTALL_IDLE; /* success needs no banner: the machine lights up */
            }

            Render_List(&manifest, selected, installed, manifest.count > 0 ? &card : NULL,
                        Profiles_GetOwnUsername(&profiles, PROFILE_DEFAULT_KEY), adminUnlocked, errorMsg, dt, -1.0f);
            continue;
        }

        if (uiState == UI_TROPHIES) {
            if (IsKeyPressed(KEY_ESCAPE)) uiState = UI_LIST;
            if (manifest.count > 0) {
                if (IsKeyPressed(KEY_LEFT)) trophyGame = (trophyGame + manifest.count - 1) % manifest.count;
                if (IsKeyPressed(KEY_RIGHT)) trophyGame = (trophyGame + 1) % manifest.count;
            }
            Render_Achievements(&manifest, trophyGame);
            continue;
        }

        if (uiState == UI_ONLINE) {
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_D)) onlineChoice = !onlineChoice;
            if (IsKeyPressed(KEY_Y)) onlineChoice = true;
            if (IsKeyPressed(KEY_N)) onlineChoice = false;
            bool save = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (onlineFirstTime) { onlineChoice = false; save = true; } /* no answer is a no */
                else { onlineChoice = syncCfg.enabled; uiState = UI_LIST; }
            }
            if (save) {
                SyncConfig_SaveEnabled(onlineChoice);
                SyncConfig_Load(&syncCfg);
                onlineFirstTime = false;
                uiState = UI_LIST;
            }
            Render_OnlineAsk(onlineChoice, onlineFirstTime);
            continue;
        }

        if (uiState == UI_STATS) {
            if (IsKeyPressed(KEY_ESCAPE)) uiState = UI_LIST;
            Render_Stats(&stats, &manifest, runSums);
            continue;
        }

        if (uiState == UI_PROFILE) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                uiState = UI_LIST;
                continue;
            }
            HandleTextInput(profileBuffer, sizeof(profileBuffer));
            if (IsKeyPressed(KEY_ENTER)) {
                const char *key = profileForGame ? manifest.games[selected].name : PROFILE_DEFAULT_KEY;
                Profiles_SetUsername(&profiles, key, profileBuffer);
                Profiles_Save(&profiles);
                cardFor = -1;
                uiState = UI_LIST;
            }
            Render_Profile(profileForGame ? manifest.games[selected].name : NULL, profileBuffer);
            continue;
        }

        /* UI_ADD_GAME or UI_EDIT_GAME: shared form, one catalog field per step. */
        if (IsKeyPressed(KEY_ESCAPE)) {
            uiState = UI_LIST;
            continue;
        }

        if (formStep < MANIFEST_FIELD_COUNT) {
            HandleTextInput(formBuffers[formStep], MANIFEST_FIELD_LEN);
            if (IsKeyPressed(KEY_ENTER)) {
                bool required = (formStep == 0 || formStep == 1);
                if (required && formBuffers[formStep][0] == '\0') {
                    snprintf(formError, sizeof(formError), "%s",
                             formStep == 0 ? "Give the game a name first" : "Enter the path to its executable");
                } else {
                    formError[0] = '\0';
                    formStep++;
                }
            }
        } else {
            if (IsKeyPressed(KEY_ENTER)) {
                if (uiState == UI_ADD_GAME) {
                    Manifest_AddGame(&manifest, formBuffers[0], formBuffers[1], formBuffers[2], formBuffers[3], formBuffers[4]);
                    selected = manifest.count - 1;
                } else if (editingIndex >= 0 && editingIndex < manifest.count) {
                    GameEntry *e = &manifest.games[editingIndex];
                    snprintf(e->name, MANIFEST_FIELD_LEN, "%s", formBuffers[0]);
                    snprintf(e->exec, MANIFEST_FIELD_LEN, "%s", formBuffers[1]);
                    snprintf(e->icon, MANIFEST_FIELD_LEN, "%s", formBuffers[2]);
                    snprintf(e->desc, MANIFEST_FIELD_LEN, "%s", formBuffers[3]);
                    snprintf(e->install, MANIFEST_FIELD_LEN, "%s", formBuffers[4]);
                }
                Manifest_Save(&manifest);
                cardFor = -1;
                uiState = UI_LIST;
            }
        }

        Render_GameForm(uiState == UI_ADD_GAME ? "Add a game" : "Edit this game",
                         formStep, (const char (*)[MANIFEST_FIELD_LEN])formBuffers, formError);
    }

    double closeTime = GetTime();
    Launch_CreditStillRunning(closeTime, &stats);
    Stats_AddLauncherTime(&stats, closeTime - launcherStartTime);
    Stats_Save(&stats);

    if (soundsLoaded) {
        UnloadSound(coinSound);
        UnloadSound(moveSound);
    }
    if (IsAudioDeviceReady()) CloseAudioDevice();
    Render_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
