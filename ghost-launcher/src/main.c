#include "raylib.h"
#include "winscale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "manifest.h"
#include "render.h"
#include "adminlock.h"
#include "launch.h"
#include "stats.h"
#include "runstats.h"
#include "../sync/syncdata.h"
#include "build_commit.h" /* generated: BUILD_SOURCE_DIR, where this build's git checkout lives */
#include "profile.h"
#include "scores.h"
#include "input.h"
#include "prefswatch.h"
#include "prefs.h"
#include "kit.h"
#include "nav.h"
#include "screens.h"
#include "keymap.h"
#include "textfield.h"
#include "settings.h"
#include "launcherini.h"
#include "achievements.h"
#include "hall.h"
#include "lineup.h"
#include "hallmusic.h"
#include "migrate.h"
#include "music.h"
#include "kitdraw.h"
#include "gamepage.h"
#include "gameform.h"
#include "machines.h"
#include "desk.h"
#include "attract.h"
#include "jukebox.h"
#include "selfupdate.h"
#include <sys/stat.h>

/* Games whose seed changes what you play, so a shared daily seed means
 * something (desk.c keeps the list). */
static bool DailySupported(const GameEntry *e) {
    char slug[64];
    return Scores_SlugFromExec(e->exec, slug, sizeof(slug)) && Desk_IsDaily(slug);
}

typedef enum {
    UI_LIST,
    UI_ADD_GAME,
    UI_EDIT_GAME,
    UI_STATS,
    UI_SETTINGS, /* tabs across the top: Player, Sound, Screen, Online, Controls */
    UI_TROPHIES,
    UI_ONLINE,
    UI_UPDATE,
    UI_MENU, /* the Arcade menu, over the hall */
    UI_GAME, /* one machine's page: I, pad X, or a click on the card */
    UI_DAILY, /* the Daily challenge desk: the menu, Shift+D, the chalkboard, a page's Daily challenge */
    UI_COUNT
} UIState;

/* What an "are you sure?" box is asking. */
enum { DLG_REMOVE = 1, DLG_LEAVE, DLG_DISCARD };

/* Everything the launcher knows between frames. One static instance, so the
 * screenshot harness (tools/shots) can see which screen is up. */
typedef struct {
    UIState ui; /* always Nav_Top(&nav) */
    Nav nav;
    bool quit;
    bool adminUnlocked;
    char assetsDir[512];
    Manifest manifest;
    PlayStats stats;
    Profiles profiles;
    /* The lit machine, as a catalog index: playing, Edit, Remove, names and
     * trophies all act on it. The hall moves along places in `lineup`, the
     * player's own row, and Lineup maps between the two. While the row has
     * machines, selected is always one of them. */
    int selected;
    RowPrefs rowPrefs; /* launcher.ini's favourites, hiding, order, last played */
    Lineup lineup;     /* the row as the hall shows it, sorted only when asked */
    Lineup mine;       /* your order with every machine, for Settings > Machines */
    HallRow row;      /* where the row is drawn: a click is tested against that */
    HallInput hallIn; /* the wheel's part notches */
    int trophyGame;

    UpdateState updateState;
    float updateReloadTimer;
    char sourceDir[300]; /* this build's checkout, with the home folder as ~ */
    bool staleKicked;    /* update.txt was about another build: GitHub asked again once */

    /* Update now (install.sh --update in the background, launch.c runs it). */
    UpdatesMode updView;
    SelfUpdateLog updLog;
    SelfUpdateFail updFail;
    double updStarted, updStepAt, updCancelAt; /* updCancelAt < 0: not asked to stop */
    bool updHardSent;
    float updPoll, updElsewherePoll;
    bool updElsewhere;    /* an update that is not ours is running (update.lock is held) */
    bool updFollow;       /* ...and it is a launcher's: update.log is its output, followed here */
    bool updSeen;         /* how it ended (done or failed) has been on the Updates screen */
    char updWhy[96];      /* why Update now cannot run, "" when it can */
    bool restart;         /* leave, then start the installed launcher */
    SyncConfig syncCfg;
    bool onlineChoice, onlineFirstTime;
    RunSummary runSums[MANIFEST_MAX_GAMES];

    /* Check now on the Updates screen: under way until update.txt says
     * GitHub answered (its checked time moves), or it gives up. */
    bool checking, checkNoAnswer;
    double checkStarted;
    long long checkBaseline;

    /* Sync scores now: under way until ghost-sync writes the online boards
     * (scores/global changes), or it gives up. */
    bool syncing, syncNoAnswer;
    double syncStarted;
    long long syncBaseline, lastSync; /* Settings_LastSync stamps */
    float syncPoll;

    char errorMsg[128];
    float errorMsgTimer;

    /* Why the last press on the hall did nothing (Daily on a machine that
     * has none): on the card for a moment, gone when the row moves on. */
    char note[96];
    float noteTimer;

    /* "Saved" / "Couldn't save", top centre for a moment after a save. */
    char toast[48];
    bool toastBad;
    float toastTimer;

    GameForm form; /* admin's Add / Edit form: all five fields at once */

    /* The lit machine's page: its tab and board, and what it shows, read
     * again whenever the hall's card is (cardLoads counts those). */
    GamePage page;
    GamePageData pageData;
    int cardLoads, pageLoads;
    double pageClickedAt; /* when a click opened the page (GamePage_ClickSettling) */

    Settings settings;
    LauncherIni ini; /* launcher.ini: the launcher's own settings */
    int hallSounds;  /* 0-10: the coin and the clicks */
    int buttonHints; /* ButtonHints: what the hints name */

    GameCard card;
    int cardFor;
    float cardAge;
    bool installed[MANIFEST_MAX_GAMES];

    /* The coin drop between Enter and the game starting. coinTimer < 0 means
     * no coin in flight; while one is, the row is locked. */
    float coinTimer;
    bool coinDinged;
    bool coinDaily; /* the coin in flight is for a daily challenge */

    Sound coinSound, moveSound;
    bool soundsLoaded;

    /* Hall music: music.c plays it; these are what the launcher asks of it. */
    bool hallMusic;          /* Settings > Sound > Hall music */
    HallSong hallSong;       /* what it plays: the theme, or the player's song */
    MusicMode musicMode;     /* asked for this frame */
    int musicVolume;         /* the hall's share of the shared volume, last set */

    /* Quiet hall: behind a game the launcher started, a few frames a second
     * and nothing moving. The animations' clock stops while it rests. */
    bool quiet;
    int fps;
    double restedFor;        /* seconds the clock has stood still */

    /* The Daily challenge desk, and the hall's chalkboard that counts from it:
     * read again whenever a run may have been left behind (a game closed,
     * focus came back, the boards came down, an install, a new day). */
    Desk desk;
    bool deskStale;
    KitScroll deskScroll;
    double deskClickedAt; /* when a click opened it: a double-click's second half lands on a row */

    /* Attract mode: the hall tours its machines when nobody is there. */
    Attract attract;
    int attractSetting;

    /* The hall's song as notes, for the jukebox's screen: what music.c
     * plays, and when it started it. */
    Song tune;
    double tuneSince;

    InputState input;
    PrefsWatch prefsWatch;

    /* The buttons: each screen keeps its focus while you are elsewhere, so
     * Back to the menu lands on the item you left from. */
    KitState kit[UI_COUNT];
    KitDialog dialog;
    KitState dialogKit;
    KitScroll statsScroll, trophyScroll;
    int cursor;         /* the mouse cursor last asked for */
    bool pointerHidden; /* keys or the pad took over: the pointer waits for the mouse */
} App;

static App sApp;

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

static void SourceDirForDisplay(char *out, size_t size) {
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(BUILD_SOURCE_DIR, home, hl) == 0 && BUILD_SOURCE_DIR[hl] == '/') snprintf(out, size, "~%s", BUILD_SOURCE_DIR + hl);
    else snprintf(out, size, "%s", BUILD_SOURCE_DIR[0] ? BUILD_SOURCE_DIR : "the folder you cloned ghost-arcade into");
}

/* Gathers what the instruction card shows for one game. Reads two small
 * text files, so it is called on selection changes and a slow timer, never
 * per frame. Chipforge's card counts the songs instead. */
static void RefreshCard(GameCard *card, const GameEntry *e, const PlayStats *stats, const Profiles *profiles, const HallSong *song) {
    memset(card, 0, sizeof(*card));
    char slug[SCORES_SLUG_LEN];
    card->tool = Manifest_IsTool(e);
    if (card->tool) {
        card->songs = HallMusic_LibraryCount();
        snprintf(card->hallSong, sizeof(card->hallSong), "%s", HallMusic_SongWords(song));
    } else if (Scores_SlugFromExec(e->exec, slug, sizeof(slug))) {
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

static void ShowError(App *a, const char *msg) {
    snprintf(a->errorMsg, sizeof(a->errorMsg), "%s", msg);
    a->errorMsgTimer = 4.0f;
}

/* Every save says how it went. */
static void Toast(App *a, bool ok) {
    snprintf(a->toast, sizeof(a->toast), "%s", ok ? "Saved" : "Couldn't save");
    a->toastBad = !ok;
    a->toastTimer = 1.5f;
}

/* ------------------------------------------------------------------ row */

static bool RowShown(const App *a) { return a->lineup.count > 0; }

/* The lit machine's place in the row (0 for an empty row). */
static int Place(const App *a) {
    int p = Lineup_PlaceOf(&a->lineup, a->selected);
    return p < 0 ? 0 : p;
}

/* launcher.ini is read again and the row put in order: at start-up, when
 * the player changes the order, a favourite or what is hidden, and when the
 * catalog changes; never while browsing, so machines do not shuffle under
 * you. `keep` stays lit if it is still on the row, else whatever now stands
 * at `place`; the row stands there at once instead of sliding. */
static void Reorder(App *a, int keep, int place) {
    Row_Read(&a->rowPrefs, &a->ini, &a->manifest, &a->stats);
    Lineup_Build(&a->lineup, &a->rowPrefs, a->rowPrefs.order, false);
    Lineup_Build(&a->mine, &a->rowPrefs, ORDER_YOURS, true);
    int index = Lineup_Keep(&a->lineup, keep, place);
    if (index != keep) a->note[0] = '\0'; /* a note about the machine that was lit */
    a->selected = index >= 0 ? index : 0;
    Hall_RowSnap(&a->row, Place(a));
    a->cardFor = -1;
    a->deskStale = true; /* the desk lists the daily games on the row, in its order */
    /* Cabinets are known by their place: a press held across a new order
     * must not land on whichever machine stands there now. */
    Kit_Enter(&a->kit[UI_LIST], a->kit[UI_LIST].focus);
}

/* A row save that failed reads the file back, so what shows is what is kept. */
static bool RowSaved(App *a, bool ok) {
    if (!ok) LauncherIni_Load(&a->ini);
    Reorder(a, a->selected, Place(a));
    return ok;
}

/* ------------------------------------------------------------- screens */

/* fresh: the screen was not open below, so it starts on its first button;
 * one gone back to keeps the button it was left from. */
static void ShowTop(App *a, bool fresh) {
    a->ui = (UIState)Nav_Top(&a->nav);
    KitState *k = &a->kit[a->ui];
    Kit_Enter(k, fresh ? KIT_NONE : k->focus);
}

static void Go(App *a, UIState screen) {
    bool fresh = !Nav_Contains(&a->nav, screen);
    if (Nav_Push(&a->nav, screen)) ShowTop(a, fresh);
}

static void GoBack(App *a) {
    if (Nav_Back(&a->nav)) ShowTop(a, false);
}

static void GoHome(App *a) {
    Nav_Home(&a->nav);
    ShowTop(a, false);
}

/* Back, one level: Esc, pad B, the mouse's back button or [< Back]. */
static bool BackPressed(const Input *in, const KitEvent *ev) {
    return Keymap_Pressed(KA_BACK, in) || (ev->type == KIT_EV_PRESS && ev->id == WID_BACK);
}

static void OpenDialog(App *a, int kind, int arg, const char *title, const char *body0, const char *body1,
                       const char *safe, const char *act) {
    Kit_DialogOpen(&a->dialog, &a->dialogKit, kind, arg, title, body0, body1, safe, act);
}

/* ------------------------------------------------------------ start-up */

/* Hall sounds at 10 are the volumes the coin and the click always had. */
static void SetHallSounds(App *a, int level) {
    a->hallSounds = level;
    if (!a->soundsLoaded) return;
    SetSoundVolume(a->coinSound, 0.7f * (float)level / 10.0f);
    SetSoundVolume(a->moveSound, 0.5f * (float)level / 10.0f);
}

static void PlayHall(const App *a, Sound s) {
    if (a->soundsLoaded && a->hallSounds > 0) PlaySound(s);
}

/* Two sounds, both optional: no audio device just means a silent coin. */
static void LoadSounds(App *a, const char *assetsDir) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    char soundPath[600];
    snprintf(soundPath, sizeof(soundPath), "%s/sounds/coin.wav", assetsDir);
    if (FileExists(soundPath)) a->coinSound = LoadSound(soundPath);
    snprintf(soundPath, sizeof(soundPath), "%s/sounds/move.wav", assetsDir);
    if (FileExists(soundPath)) a->moveSound = LoadSound(soundPath);
    a->soundsLoaded = true;
    SetHallSounds(a, a->hallSounds);
}

/* update.txt, believed only when it is about this build (an older
 * launcher wrote it, and the code has been updated since): otherwise
 * unknown, and GitHub is asked again once. */
static void StartCheck(App *a, double now);
static void LoadUpdate(App *a) {
    Update_Load(&a->updateState);
    if (SelfUpdate_Believe(&a->updateState, BUILD_COMMIT) && !a->staleKicked && a->syncCfg.checkUpdates) {
        a->staleKicked = true;
        StartCheck(a, GetTime()); /* as Check now: Updates says it is asking, and does not ask twice */
    }
}

static void PollElsewhere(App *a, double now);

static void App_Start(App *a) {
    for (int i = 0; i < UI_COUNT; i++) Kit_Init(&a->kit[i]);
    Kit_Init(&a->dialogKit);
    Settings_Init(&a->settings);
    a->lastSync = Settings_LastSync();
    a->cursor = MOUSE_CURSOR_DEFAULT;
    Nav_Init(&a->nav, UI_LIST);
    a->ui = UI_LIST;
    Hall_RowInit(&a->row);
    Reorder(a, -1, 0);
    int start = Lineup_Start(&a->lineup, &a->rowPrefs); /* where you left off, unless Start on says the first */
    a->selected = start >= 0 ? start : 0;
    Hall_RowSnap(&a->row, Place(a));
    SyncConfig_Load(&a->syncCfg); /* before update.txt: a stale one asks again only with checks on */
    LoadUpdate(a);
    SourceDirForDisplay(a->sourceDir, sizeof(a->sourceDir));
    a->updCancelAt = -1.0;
    PollElsewhere(a, GetTime()); /* one a launcher before this one left running is followed */
    a->onlineChoice = a->syncCfg.enabled;
    a->onlineFirstTime = !a->syncCfg.decided;
    a->form.editing = -1;
    a->pageData.index = -1;
    a->pageClickedAt = -1.0e9;
    a->deskClickedAt = -1.0e9;
    a->card.playerName = "";
    a->cardFor = -1;
    a->coinTimer = -1.0f;
    a->deskStale = true;
    if (a->onlineFirstTime) { /* sharing scores is opt-in: ask before anything is uploaded */
        Go(a, UI_ONLINE);
        a->kit[UI_ONLINE].focus = WID_ONLINE_NO;
    }
}

/* ---------------------------------------------------------- every frame */

/* Check now is answered when ghost-sync rewrites update.txt; GitHub that
 * does not answer (or a missing ghost-sync) gives up after 20 s. */
static void TickCheck(App *a, double now) {
    if (!a->checking) return;
    if (a->updateState.checkedAt != a->checkBaseline) a->checking = false;
    else if (now - a->checkStarted > 20.0) {
        a->checking = false;
        a->checkNoAnswer = true;
    }
}

/* Sync scores now is answered when ghost-sync writes the online boards;
 * one that sends nothing back (sharing refused, no network) gives up. */
static void TickSync(App *a, float dt, double now) {
    a->syncPoll += dt;
    if (a->syncPoll < (a->syncing ? 1.0f : 5.0f)) return;
    a->syncPoll = 0.0f;
    long long stamp = Settings_LastSync();
    if (stamp != a->lastSync) {
        a->cardFor = -1; /* new world boards: the card's world best */
        a->deskStale = true; /* ...and today's daily boards */
    }
    a->lastSync = stamp;
    if (!a->syncing) return;
    if (stamp != a->syncBaseline) a->syncing = false;
    else if (now - a->syncStarted > 30.0) {
        a->syncing = false;
        a->syncNoAnswer = true;
    }
}

static bool Updating(const App *a) { return a->updView == UPV_RUNNING || a->updElsewhere; }

/* Why Update now cannot start now, into updWhy. */
static void UpdateWhyNot(App *a) {
    SelfUpdateWhy w = {
        .gamesRunning = Launch_RunningCount(),
        .installRunning = Launch_InstallStatus(NULL) == INSTALL_RUNNING,
        .updateRunning = a->updView == UPV_RUNNING && !a->updFollow,
        .updateElsewhere = a->updElsewhere,
        .sourceDir = BUILD_SOURCE_DIR,
    };
    SelfUpdate_WhyNot(&w, a->updWhy, sizeof(a->updWhy));
}

/* The log since the last look; the step it is on starts its clock. */
static void ReadUpdateLog(App *a, double now) {
    char log[700];
    Launch_UpdateLogPath(log, sizeof(log));
    SelfUpdateStep before = a->updLog.step;
    SelfUpdate_LogPoll(&a->updLog, log);
    if (a->updLog.step != before) a->updStepAt = now;
}

/* It worked: update.txt says so for the new build (or is cleared for it),
 * so the notice goes, and GitHub is asked when the new build is not known
 * to be its newest. */
static void UpdateFileAfterSuccess(App *a) {
    UpdateState u;
    Update_Load(&u);
    bool ask = SelfUpdate_AfterSuccess(&u, a->updLog.version);
    Update_Save(&u);
    a->staleKicked = true; /* asked here, if at all */
    if (ask && a->syncCfg.checkUpdates) Launch_CheckUpdateNow();
    LoadUpdate(a);
}

/* How an update ended, ours or one followed from update.log. Seen at once
 * when Updates is up; else the hall says so until Updates is opened. */
static void FinishUpdate(App *a, SelfUpdateFail fail) {
    a->updFail = fail;
    a->updView = fail == SU_FAIL_NONE ? UPV_DONE : UPV_FAILED;
    a->updCancelAt = -1.0;
    a->updFollow = false;
    a->updSeen = a->ui == UI_UPDATE;
    a->cardFor = -1;     /* the games may be new */
    a->deskStale = true;
    Render_ForgetMissingIcons();
    if (a->updView == UPV_DONE) UpdateFileAfterSuccess(a);
    UpdateWhyNot(a);
    if (a->ui == UI_UPDATE) Kit_Enter(&a->kit[UI_UPDATE], a->updView == UPV_DONE ? WID_UPDATE_RESTART : WID_UPDATE_RETRY);
}

/* An update that is not ours: noticed (a launcher's is followed through
 * update.log from where it is), and ended (a followed one is judged from
 * its log; after a terminal's, GitHub is asked about the build now there). */
static void PollElsewhere(App *a, double now) {
    UpdateElsewhere e;
    bool running = Launch_UpdateElsewhere(&e);
    if (running && !a->updElsewhere) {
        a->updFollow = e.launcherLog && a->updView != UPV_RUNNING;
        if (a->updFollow) {
            SelfUpdate_LogReset(&a->updLog);
            a->updView = UPV_RUNNING;
            long long ago = e.startedAt > 0 ? (long long)time(NULL) - e.startedAt : 0;
            a->updStarted = now - (double)(ago > 0 ? ago : 0);
            a->updCancelAt = -1.0;
            a->updPoll = 0.0f;
            ReadUpdateLog(a, now);
        }
    } else if (!running && a->updElsewhere) {
        if (a->updFollow) {
            ReadUpdateLog(a, now);
            FinishUpdate(a, SelfUpdate_ClassifyLog(&a->updLog));
        } else if (a->syncCfg.checkUpdates) {
            Launch_CheckUpdateNow();
        }
        a->updFollow = false;
    }
    a->updElsewhere = running;
}

/* Update now, every frame: the log while it runs, how it ended once it has. */
static void TickSelfUpdate(App *a, float dt, double now) {
    a->updElsewherePoll += dt;
    if (a->updElsewherePoll >= (a->updElsewhere ? 1.0f : 5.0f)) {
        a->updElsewherePoll = 0.0f;
        PollElsewhere(a, now);
    }
    if (a->ui == UI_UPDATE) UpdateWhyNot(a);
    if (a->updView != UPV_RUNNING) return;
    a->updPoll += dt;
    if (a->updFollow) { /* another launcher's: its log is all there is */
        if (a->updPoll >= 0.25f) {
            a->updPoll = 0.0f;
            ReadUpdateLog(a, now);
        }
        return;
    }
    int code = 0;
    bool cancelled = false;
    SelfUpdateRun run = Launch_UpdateStatus(&code, &cancelled);
    if (a->updPoll >= 0.25f || run != SU_RUN_RUNNING) {
        a->updPoll = 0.0f;
        ReadUpdateLog(a, now);
    }
    if (run == SU_RUN_RUNNING) {
        /* Asked to stop and still going after a while: no more asking. */
        if (a->updCancelAt >= 0.0 && !a->updHardSent && now - a->updCancelAt > 5.0) {
            Launch_UpdateCancel(true);
            a->updHardSent = true;
        }
        return;
    }
    SelfUpdateFail fail = SelfUpdate_Classify(&a->updLog, code, cancelled);
    Launch_UpdateAcknowledge();
    FinishUpdate(a, fail);
}

/* Games and installs finishing, the error and toast timers, update.txt.
 * Returns true if a game the launcher started has exited. */
static bool TickBackground(App *a, float dt, double now) {
    bool gameExited = false;
    InstallStatus installBefore = Launch_InstallStatus(NULL);
    if (Launch_ReapFinished(now, &a->stats)) {
        Stats_Save(&a->stats);
        a->cardFor = -1; /* a game just closed: its scores and playtime changed */
        gameExited = true;
    }
    if (Launch_InstallStatus(NULL) != installBefore) {
        a->cardFor = -1;             /* an install just finished */
        Render_ForgetMissingIcons(); /* its icon probably exists now */
    }

    if (a->errorMsgTimer > 0.0f) {
        a->errorMsgTimer -= dt;
        if (a->errorMsgTimer <= 0.0f) a->errorMsg[0] = '\0';
    }
    if (a->toastTimer > 0.0f) a->toastTimer -= dt;
    if (a->noteTimer > 0.0f) {
        a->noteTimer -= dt;
        if (a->noteTimer <= 0.0f) a->note[0] = '\0';
    }

    /* ghost-sync writes update.txt in the background a few seconds after
     * start-up; re-read it whatever screen is up, and often while a Check
     * now waits for its answer. */
    a->updateReloadTimer += dt;
    if (a->updateReloadTimer > (a->checking ? 1.0f : 5.0f)) {
        a->updateReloadTimer = 0.0f;
        LoadUpdate(a);
    }
    TickSelfUpdate(a, dt, now);
    TickCheck(a, now);
    TickSync(a, dt, now);
    return gameExited;
}

/* ----------------------------------------------------------------- hall */

static void StartCoin(App *a, bool daily) {
    a->coinTimer = 0.0f;
    a->coinDinged = false;
    a->coinDaily = daily;
    /* A button held down on the lit machine (or any chip) while Enter drops
     * the coin must not act when it is let go after the game is up. */
    Kit_Enter(&a->kit[UI_LIST], a->kit[UI_LIST].focus);
}

/* The coin is in flight: nothing on the row reacts until the game starts. */
static void TickCoin(App *a, float dt, double now) {
    a->coinTimer += dt;
    if (!a->coinDinged && a->coinTimer >= COIN_DING_AT * COIN_ANIM_SECONDS) {
        a->coinDinged = true;
        PlayHall(a, a->coinSound);
    }
    if (a->coinTimer < COIN_ANIM_SECONDS) return;
    a->coinTimer = -1.0f;
    if (Updating(a)) { /* an update began elsewhere while it dropped */
        ShowError(a, UPDATING_WHY);
        return;
    }
    const GameEntry *e = &a->manifest.games[a->selected];
    char expanded[512], launchErr[96] = "";
    Manifest_ExpandPath(e->exec, expanded, sizeof(expanded));
    bool wasDaily = a->coinDaily;
    a->coinDaily = false;
    if (!Launch_GameArg(expanded, e->name, now, wasDaily ? "--daily" : NULL, launchErr, sizeof(launchErr))) {
        ShowError(a, launchErr);
        return;
    }
    /* For Recently played and Start on; the row itself keeps its order until
     * the player asks for another, and a failed write is not worth a note
     * while a game starts. */
    if (a->selected < a->rowPrefs.count && !Row_SavePlayed(&a->ini, a->rowPrefs.m[a->selected].slug, (long long)time(NULL)))
        LauncherIni_Load(&a->ini);
}

static void ShowNote(App *a, const char *text);

/* Why Daily cannot start on this machine ("" when it can): the machine's
 * own reason first, then an update under way. */
static void DailyWhy(const App *a, const GameEntry *e, bool installed, bool hasDaily, char *out, size_t size) {
    Hall_DailyReason(e->name, installed, hasDaily, out, size);
    if (!out[0] && Updating(a)) snprintf(out, size, "%s", UPDATING_WHY);
}

/* Enter on the row: play, install, or say why neither can happen. */
static void PressPlay(App *a, double now) {
    a->coinDaily = false;
    if (Updating(a)) {
        ShowNote(a, UPDATING_WHY);
        return;
    }
    const GameEntry *e = &a->manifest.games[a->selected];
    char expanded[512];
    char launchErr[96] = "";
    bool ok;
    if (!IsInstalled(e) && e->install[0] != '\0') {
        /* Enter on a machine that isn't installed installs it. */
        Launch_InstallAcknowledge();
        Manifest_ExpandPath(e->install, expanded, sizeof(expanded));
        ok = Launch_InstallGame(expanded, e->name, launchErr, sizeof(launchErr));
        a->cardFor = -1;
    } else if (IsInstalled(e)) {
        /* Insert coin: the launch itself happens when the animation finishes. */
        StartCoin(a, false);
        ok = true;
    } else {
        Manifest_ExpandPath(e->exec, expanded, sizeof(expanded));
        ok = Launch_Game(expanded, e->name, now, launchErr, sizeof(launchErr)); /* reports why it can't */
    }
    if (!ok) ShowError(a, launchErr);
}

/* Saves the catalog and says how it went. A save that failed puts back
 * what is on disk, so the row never shows a change that was not kept. The
 * catalog's indexes may have moved, so the row is built again: `keep` (an
 * index in the new catalog) stays lit, or whatever stands at `place`. */
static bool SaveCatalog(App *a, bool changed, int keep, int place) {
    bool ok = changed && Manifest_Save(&a->manifest);
    if (!ok) {
        Manifest onDisk;
        if (Manifest_Load(&onDisk, a->assetsDir)) a->manifest = onDisk;
    }
    Toast(a, ok);
    a->note[0] = '\0'; /* it was about the catalog as it was */
    Reorder(a, keep < a->manifest.count ? keep : -1, place);
    return ok;
}

/* Delete asks first; this is the answer "Remove". The index is the one the
 * question was about; the machine after it in the row comes to the middle. */
static void RemoveGame(App *a, int index) {
    if (index < 0 || index >= a->manifest.count) return;
    int place = Lineup_PlaceOf(&a->lineup, index), keep = a->selected;
    keep = keep == index ? -1 : (keep > index ? keep - 1 : keep); /* the catalog closes up after it */
    SaveCatalog(a, Manifest_RemoveGame(&a->manifest, index), keep, place < 0 ? Place(a) : place);
}

/* Delete on the hall, Remove on a page or in Settings > Machines. */
static void AskRemove(App *a, int index) {
    if (index < 0 || index >= a->manifest.count) return;
    char title[64];
    snprintf(title, sizeof(title), "Remove %.22s?", a->manifest.games[index].name);
    OpenDialog(a, DLG_REMOVE, index, title, "It leaves the row. Its scores and playtime stay on disk.", NULL, "Keep", "Remove");
}

/* Leaving only asks while something the launcher started is still going. */
static void Leave(App *a) {
    int games = Launch_RunningCount();
    const char *installing = "";
    bool install = Launch_InstallStatus(&installing) == INSTALL_RUNNING;
    bool updating = a->updView == UPV_RUNNING && !a->updFollow; /* ours, not one followed */
    if (games == 0 && !install && !updating) {
        a->quit = true;
        return;
    }
    char l0[80], l1[80];
    if (updating && games > 0) {
        snprintf(l0, sizeof(l0), "A game is still running, and Ghost Arcade is updating.");
        snprintf(l1, sizeof(l1), "The update keeps going in the background.");
    } else if (updating) {
        snprintf(l0, sizeof(l0), "Ghost Arcade is updating. It keeps going if you leave;");
        snprintf(l1, sizeof(l1), "open the launcher again to see how it went.");
    } else if (games > 0 && install) {
        snprintf(l0, sizeof(l0), "A game is still running, and %.20s is installing.", installing);
        snprintf(l1, sizeof(l1), "Time played from now on is not counted.");
    } else if (games > 0) {
        snprintf(l0, sizeof(l0), "A game you started is still running.");
        snprintf(l1, sizeof(l1), "Time played from now on is not counted.");
    } else {
        snprintf(l0, sizeof(l0), "%.24s is still installing. It carries on,", installing);
        snprintf(l1, sizeof(l1), "but the launcher will not see how it ends.");
    }
    OpenDialog(a, DLG_LEAVE, 0, "Leave the arcade?", l0, l1, "Stay", "Leave");
}

/* Admin's form, all five fields at once (gameform.h); typing starts in
 * the name, its cursor at the end. */
static void OpenEditFormFor(App *a, int index) {
    if (index < 0 || index >= a->manifest.count) return;
    GameForm_OpenEdit(&a->form, &a->manifest.games[index], index);
    a->form.field.steadySince = GetTime();
    Go(a, UI_EDIT_GAME);
}

static void OpenEditForm(App *a) { OpenEditFormFor(a, a->selected); }

static void OpenAddForm(App *a) {
    GameForm_OpenAdd(&a->form);
    a->form.field.steadySince = GetTime();
    Go(a, UI_ADD_GAME);
}

/* The catalog index being installed, or -1. */
static int InstallingIndex(const App *a) {
    const char *name = "";
    if (Launch_InstallStatus(&name) != INSTALL_RUNNING) return -1;
    for (int i = 0; i < a->manifest.count; i++) if (strcmp(a->manifest.games[i].name, name) == 0) return i;
    return -1;
}

/* What the Settings page shows now. */
static SettingsView ViewSettings(const App *a) {
    const ArcadePrefs *p = Prefs_Get();
    SettingsView v = {
        .musicVolume = p->musicVolume,
        .fullscreen = p->fullscreen,
        .reducedFlashing = p->reducedFlashing,
        .screenShake = p->screenShake,
        .hallSounds = a->hallSounds,
        .buttonHints = a->buttonHints,
        .attract = a->attractSetting,
        .soundReady = a->soundsLoaded,
        .hallMusic = a->hallMusic,
        .hallSong = HallMusic_SongWords(&a->hallSong),
        .startOnLast = a->rowPrefs.startOnLast,
        .rowOrder = a->rowPrefs.order,
        .row = &a->rowPrefs,
        .mine = &a->mine,
        .installed = a->installed,
        .installing = InstallingIndex(a),
        .sharing = a->syncCfg.enabled,
        .checkUpdates = a->syncCfg.checkUpdates,
        .checking = a->checking,
        .checkNoAnswer = a->checkNoAnswer,
        .updateStatus = (int)a->updateState.status,
        .checkedAt = a->updateState.checkedAt,
        .syncing = a->syncing,
        .syncNoAnswer = a->syncNoAnswer,
        .lastSync = a->lastSync / 1000000000LL,
        .now = (long long)time(NULL),
        .serverUrl = a->syncCfg.url,
        .manifest = &a->manifest,
        .profiles = &a->profiles,
        .admin = a->adminUnlocked,
    };
    return v;
}

static void CheckHallSong(App *a);

/* Settings on a tab (or the one it was left on, for a negative tab).
 * online.conf may have been edited by hand since, and the boards synced. */
static void OpenSettings(App *a, int tab) {
    Prefs_Load(); /* it shows the shared four: as the games left them */
    SyncConfig_Load(&a->syncCfg);
    CheckHallSong(a); /* Sound says what the hall plays */
    if (!Nav_Contains(&a->nav, UI_SETTINGS)) { /* a fresh visit starts at the top */
        a->settings.names = (KitScroll){0, 0.0f};
        a->settings.machines = (KitScroll){0, 0.0f};
    }
    a->lastSync = Settings_LastSync();
    Go(a, UI_SETTINGS);
    if (tab < 0) return;
    SettingsView v = ViewSettings(a);
    Settings_OpenTab(&a->settings, &a->kit[UI_SETTINGS], &v, tab);
}

static int NameEditId(int index) { return index == SET_ARCADE_NAME ? WID_SET_NAME_EDIT : WID_SET_GAME_EDIT + index; }

static void BeginNameEdit(App *a, int index, bool quick, bool fromPad) {
    Settings_BeginEdit(&a->settings, &a->profiles, &a->manifest, index, quick, fromPad);
    a->settings.field.steadySince = GetTime();
    KitState *k = &a->kit[UI_SETTINGS];
    k->focus = NameEditId(a->settings.fieldFor);
    k->keyDown = KIT_NONE;
    Input_DropHolds(&a->input);
}

/* U = the arcade-wide name (works with an empty catalog too); Shift+U =
 * the name for just the selected game. Typing starts at once, and Enter or
 * Esc comes straight back here, as the old name screen did. */
static void OpenNameEdit(App *a, bool forGame) {
    OpenSettings(a, ST_PLAYER);
    BeginNameEdit(a, forGame && RowShown(a) ? a->selected : SET_ARCADE_NAME, true, false);
}

/* Each game's runs.log is read once, on the way in. */
static void OpenStats(App *a) {
    time_t statsNow = time(NULL);
    for (int i = 0; i < a->manifest.count; i++) {
        char slug[64];
        if (Scores_SlugFromExec(a->manifest.games[i].exec, slug, sizeof(slug))) RunStats_Load(slug, statsNow, &a->runSums[i]);
        else memset(&a->runSums[i], 0, sizeof(a->runSums[i]));
    }
    a->statsScroll = (KitScroll){0, 0.0f};
    Go(a, UI_STATS);
}

static void OpenTrophies(App *a) {
    a->trophyGame = a->selected;
    a->trophyScroll = (KitScroll){0, 0.0f};
    Go(a, UI_TROPHIES);
}

/* online.conf may have been edited by hand since: Check now follows it. */
static bool UpdateBehind(const App *a);

/* The keys start on what can be done: Update now when an update waits and
 * can run, Restart once one is done, else Check now. A failure is shown
 * once on this screen (even when it ended while the player was elsewhere);
 * a visit after that starts afresh. */
static void OpenUpdates(App *a) {
    SyncConfig_Load(&a->syncCfg);
    a->checkNoAnswer = false;
    PollElsewhere(a, GetTime());
    bool fresh = a->ui != UI_UPDATE;
    if (fresh && a->updView == UPV_FAILED && a->updSeen) a->updView = UPV_IDLE;
    if (a->updView == UPV_FAILED || a->updView == UPV_DONE) a->updSeen = true;
    UpdateWhyNot(a);
    Go(a, UI_UPDATE);
    if (!fresh) return;
    int focus = WID_CHECK_NOW;
    if (a->updView == UPV_RUNNING) focus = WID_UPDATE_CANCEL;
    else if (a->updView == UPV_DONE) focus = WID_UPDATE_RESTART;
    else if (UpdateBehind(a) && !a->updWhy[0]) focus = WID_UPDATE_NOW;
    Kit_Enter(&a->kit[UI_UPDATE], focus);
}

/* The desk and the chalkboard read the files again only when something may
 * have changed them, or the day turned. */
static void RefreshDesk(App *a) {
    time_t now = time(NULL);
    if (!a->deskStale && Desk_Today(now) == a->desk.today) return;
    DeskSource src = {&a->manifest, &a->lineup, a->installed, &a->profiles, now};
    Desk_Load(&a->desk, &src);
    a->deskStale = false;
    Kit_ScrollClamp(&a->deskScroll, a->desk.count, DESK_VISIBLE); /* the row may have lost a machine under an open desk */
}

/* The desk opens on this machine's row when it has one that can be played,
 * else on today's featured game. Going back to a desk already open below
 * keeps the row it was left on. */
static void OpenDesk(App *a, int index) {
    a->deskStale = true;
    RefreshDesk(a);
    bool fresh = !Nav_Contains(&a->nav, UI_DAILY);
    Go(a, UI_DAILY);
    /* A page's Daily challenge sits where the desk's rows are: the second
     * click of a double-click must not play some other machine's daily. */
    if (a->input.lastDevice == DEV_MOUSE) a->deskClickedAt = GetTime();
    if (!fresh) return;
    a->deskScroll = (KitScroll){0, 0.0f};
    int row = Desk_StartRow(&a->desk, index);
    if (row < 0) return;
    Kit_ScrollShow(&a->deskScroll, row, a->desk.count, DESK_VISIBLE);
    a->kit[UI_DAILY].focus = WID_DESK_ROW + row;
}

/* What an Arcade menu item opens. */
static void OpenMenuItem(App *a, int item) {
    switch (item) {
        case AM_HALL: GoHome(a); break;
        case AM_DAILY: OpenDesk(a, a->selected); break;
        case AM_SETTINGS: OpenSettings(a, -1); break;
        case AM_STATS: OpenStats(a); break;
        case AM_TROPHIES: OpenTrophies(a); break;
        case AM_UPDATES: OpenUpdates(a); break;
        case AM_LEAVE: Leave(a); break;
        default: break;
    }
}

/* What Tab U O L H V (and Shift+U, and pad View) open. */
static bool OpenShortcut(App *a, KeyAction key) {
    switch (key) {
        case KA_OPEN_SETTINGS: OpenSettings(a, -1); return true;
        case KA_OPEN_NAME: OpenNameEdit(a, false); return true;
        case KA_OPEN_GAME_NAME: OpenNameEdit(a, true); return true;
        case KA_OPEN_ONLINE: OpenSettings(a, ST_ONLINE); return true;
        case KA_OPEN_STATS: OpenStats(a); return true;
        case KA_OPEN_TROPHIES: OpenTrophies(a); return true;
        case KA_OPEN_UPDATES: OpenUpdates(a); return true;
        case KA_OPEN_DAILY: OpenDesk(a, a->selected); return true;
        default: return false;
    }
}

/* The letters, on the hall and in the Arcade menu alike. */
static bool LetterShortcut(App *a, const Input *in) { return OpenShortcut(a, Screens_Shortcut(in)); }

static void RefreshCardIfStale(App *a, float dt) {
    a->cardAge += dt;
    if (a->cardFor != a->selected || a->cardAge > 3.0f) {
        /* A handful of stat() calls and two small file reads, a few times a
         * minute -- never per frame. Settings > Machines shows every
         * machine's status, hidden ones too. */
        for (int i = 0; i < a->manifest.count; i++) {
            bool now = IsInstalled(&a->manifest.games[i]);
            if (now != a->installed[i]) a->deskStale = true; /* the desk's rows can be played, or not */
            a->installed[i] = now;
        }
        if (RowShown(a)) {
            const GameEntry *e = &a->manifest.games[a->selected];
            RefreshCard(&a->card, e, &a->stats, &a->profiles, &a->hallSong);
            a->card.installed = a->installed[a->selected];
            a->card.canInstall = !a->card.installed && e->install[0] != '\0';
            a->card.dailyGame = DailySupported(e);
            a->card.supportsDaily = a->card.installed && a->card.dailyGame;
        }
        a->cardFor = a->selected;
        a->cardAge = 0.0f;
        a->cardLoads++; /* the machine's page follows the card */
    }
    if (!RowShown(a)) return;
    const char *installingName = "";
    InstallStatus st = Launch_InstallStatus(&installingName);
    a->card.install = (strcmp(installingName, a->manifest.games[a->selected].name) == 0) ? st : INSTALL_IDLE;
    if (a->card.install == INSTALL_DONE) a->card.install = INSTALL_IDLE; /* success needs no banner: the machine lights up */
}

static void ShowNote(App *a, const char *text) {
    snprintf(a->note, sizeof(a->note), "%s", text);
    a->noteTimer = 3.0f;
}

/* A machine comes to the middle. A note about the one left goes with it. */
static void Select(App *a, int index) {
    if (index < 0 || index >= a->manifest.count || index == a->selected) return;
    a->selected = index;
    a->note[0] = '\0';
    a->noteTimer = 0.0f;
    if (a->manifest.count > 1) PlayHall(a, a->moveSound);
}

/* F, or the card's Favourite: the star goes on (or off) and the row is put
 * in order again, the same machine still lit. */
static void ToggleFavourite(App *a) {
    if (a->selected >= a->rowPrefs.count) return;
    const RowMachine *m = &a->rowPrefs.m[a->selected];
    if (!m->slug[0]) { /* the card's button is faint for the same reason */
        ShowNote(a, "Its program name cannot be kept as a favourite.");
        return;
    }
    bool on = !m->favourite;
    char name[32], line[96];
    snprintf(name, sizeof(name), "%.22s", a->manifest.games[a->selected].name);
    bool ok = RowSaved(a, Row_SaveFavourite(&a->ini, m->slug, on));
    Toast(a, ok);
    if (!ok) return;
    snprintf(line, sizeof(line), on ? "%s is a favourite now: favourites come first." : "%s is no longer a favourite.", name);
    ShowNote(a, line);
}

/* T: the next order. The lit machine stays lit wherever it lands. */
static void NextOrder(App *a) {
    RowOrder order = Row_NextOrder(a->rowPrefs.order);
    bool ok = RowSaved(a, Row_SaveOrder(&a->ini, order));
    Toast(a, ok);
    if (!ok) return;
    char line[64];
    snprintf(line, sizeof(line), "Row order: %s", Row_OrderName(order));
    ShowNote(a, line);
}

/* Daily on a machine with none, or not installed, says why instead of
 * doing nothing. */
static void PressDaily(App *a) {
    const GameEntry *e = &a->manifest.games[a->selected];
    char why[96];
    DailyWhy(a, e, IsInstalled(e), DailySupported(e), why, sizeof(why));
    if (why[0]) ShowNote(a, why);
    else StartCoin(a, true);
}

/* ---------------------------------------------------------------- desk */

/* A row of the desk: back to the hall, that machine lit, and its daily's
 * coin drops there, as D on the hall does. */
static void DeskPlay(App *a, int index) {
    if (index < 0 || index >= a->manifest.count || Lineup_PlaceOf(&a->lineup, index) < 0) return;
    GoHome(a);
    Select(a, index);
    Hall_RowSnap(&a->row, Place(a));
    PressDaily(a);
}

/* Shift+D, the key that opened it, closes it again. */
static void UpdateDesk(App *a, const Input *in, const KitEvent *ev, bool wheelFree, float dt) {
    RefreshCardIfStale(a, dt); /* an install that finishes lights its row */
    if (BackPressed(in, ev) || Keymap_Pressed(KA_OPEN_DAILY, in)) {
        GoBack(a);
        return;
    }
    int row = ev->type == KIT_EV_PRESS ? Desk_RowOf(ev->id) : -1;
    if (row >= 0 && row < a->desk.count) {
        DeskPlay(a, a->desk.rows[row].index);
        return;
    }
    Desk_Navigate(&a->desk, &a->deskScroll, &a->kit[UI_DAILY], in, wheelFree);
}

/* ------------------------------------------------------- machine page */

static HallPlay PlayState(const App *a);
static InputDevice HintDevice(const App *a);

/* The page's machine is always the lit one: its numbers, boards, how-to
 * and (for Chipforge) songs, read from the files. */
static void LoadPage(App *a) {
    const GameEntry *e = RowShown(a) ? &a->manifest.games[a->selected] : NULL;
    const char *slug = a->selected < a->rowPrefs.count ? a->rowPrefs.m[a->selected].slug : "";
    GamePageSource src = {
        .game = e,
        .index = a->selected,
        .stats = &a->stats,
        .profiles = &a->profiles,
        .played = Row_Played(&a->ini, slug), /* launcher.ini as TickCoin last wrote it */
        .manifest = &a->manifest,
        .hallSong = HallMusic_SongWords(&a->hallSong),
        .now = time(NULL),
        .hallFile = a->hallSong.custom ? a->hallSong.file : "",
    };
    GamePage_Load(&a->pageData, &src);
    a->pageLoads = a->cardLoads;
    /* Songs deleted from the library while the list was scrolled down. */
    Kit_ScrollClamp(&a->page.songs, GamePage_SongCount(&a->pageData), PAGE_SONGS_VISIBLE);
}

/* A machine comes onto the page: its best board first. */
static void PageArrives(App *a) {
    a->cardFor = -1;
    RefreshCardIfStale(a, 0.0f);
    LoadPage(a);
    GamePage_StartBoard(&a->page, &a->pageData.boards, a->pageData.best.found ? a->pageData.best.mode : NULL);
}

/* I, pad X, This game or a click on the card. With no machine on the row
 * they open Trophies, as I and pad X always did. */
static void OpenGamePage(App *a) {
    if (!RowShown(a)) {
        OpenTrophies(a);
        return;
    }
    PageArrives(a);
    Go(a, UI_GAME);
    if (a->input.lastDevice == DEV_MOUSE) a->pageClickedAt = GetTime();
}

typedef struct {
    GamePageView v;
    char daily[96];
    char today[24];
} PageViewNow;

static void ViewPage(const App *a, PageViewNow *b) {
    const GameEntry *e = &a->manifest.games[a->selected];
    const RowMachine *m = a->selected < a->rowPrefs.count ? &a->rowPrefs.m[a->selected] : NULL;
    DailyWhy(a, e, a->card.installed, a->card.dailyGame, b->daily, sizeof(b->daily));
    GamePage_Today(time(NULL), b->today, sizeof(b->today));
    b->v = (GamePageView){
        .name = e->name,
        .tool = a->pageData.tool,
        .play = PlayState(a),
        .dailyReason = b->daily,
        .favourite = m && m->favourite,
        .canFavourite = m && m->slug[0],
        .admin = a->adminUnlocked,
        .place = Place(a),
        .count = a->lineup.count,
        .boards = &a->pageData.boards,
        .today = b->today,
        .device = HintDevice(a),
        .data = &a->pageData,
        .hallMusic = a->hallMusic,
    };
}

/* Play goes back to the row and drops the coin there, so starting a game
 * feels as it always did. Install starts here and the page stays. */
static void PagePlay(App *a, double now) {
    if (PlayState(a) != HP_PLAY) {
        PressPlay(a, now);
        return;
    }
    GoHome(a);
    Hall_RowSnap(&a->row, Place(a));
    StartCoin(a, false);
}

/* The page's Daily challenge (and D, pad Y) opens the desk on this
 * machine, one Enter from its coin; D on the hall still drops it at once. */
static void PageDaily(App *a) {
    const GameEntry *e = &a->manifest.games[a->selected];
    char why[96];
    DailyWhy(a, e, IsInstalled(e), DailySupported(e), why, sizeof(why));
    if (why[0]) ShowNote(a, why);
    else OpenDesk(a, a->selected);
}

/* Chipforge's Songs: the hall gets the song (music.ini, as Chipforge's own
 * assign screen writes it) and the music crossfades to it. */
static void PickHallSong(App *a, int entry) {
    const GamePageData *d = &a->pageData;
    const char *file = entry > 0 && entry <= d->libraryCount ? d->library[entry - 1].file : NULL;
    bool ok = Song_Assign(HALL_MUSIC_SLUG, file);
    Toast(a, ok);
    char title[SONG_NAME_LEN], line[96];
    snprintf(title, sizeof(title), "%s", GamePage_SongTitle(d, entry));
    CheckHallSong(a);
    a->cardFor = -1; /* the page marks the hall's song again */
    if (!ok) return;
    if (file && !a->hallSong.custom) snprintf(line, sizeof(line), "%.40s does not load: the hall keeps its own tune.", title);
    else if (!file) snprintf(line, sizeof(line), "%s", a->hallMusic ? "The hall plays its own tune again." : "The hall's own tune again. Hall music is off.");
    else if (!a->hallMusic) snprintf(line, sizeof(line), "The hall's song is %.40s. Hall music is off.", title);
    else snprintf(line, sizeof(line), "The hall plays %.40s now.", title);
    ShowNote(a, line);
}

/* [<] and [>]: the machine before or after on the row, which the hall then
 * comes back to. Its buttons are another machine's: a press half-made on the
 * last one (Enter or the mouse held while PgDn turns) must not start this
 * one, and the keys stay on their button only where it can be pressed. */
static void FlipPage(App *a, int dir) {
    int n = a->lineup.count;
    if (n < 2) return;
    Select(a, Lineup_IndexAt(&a->lineup, (Place(a) + dir + n) % n));
    PageArrives(a);
    PageViewNow b;
    ViewPage(a, &b);
    KitList l;
    GamePage_Layout(&a->page, &b.v, &l);
    Kit_Enter(&a->kit[UI_GAME], GamePage_KeepFocus(&l, a->kit[UI_GAME].focus));
}

static void UpdateGamePage(App *a, const Input *in, const KitEvent *ev, const KitList *l, bool wheelFree, float dt, double now) {
    RefreshCardIfStale(a, dt);
    if (!RowShown(a)) { /* removed or hidden from under it */
        GoHome(a);
        return;
    }
    if (a->pageData.index != a->selected || a->pageLoads != a->cardLoads) LoadPage(a); /* a sync, a game that closed */
    PageViewNow b;
    ViewPage(a, &b);
    GamePageAction act = GamePage_Read(&a->page, &b.v, ev, in);
    switch (act.verb) {
        case GA_BACK: GoBack(a); break;
        case GA_PLAY: PagePlay(a, now); break;
        case GA_DAILY: PageDaily(a); break;
        case GA_DESK: OpenDesk(a, a->selected); break;
        case GA_NAME: OpenNameEdit(a, true); break;
        case GA_FAVOURITE: ToggleFavourite(a); break;
        case GA_EDIT: OpenEditForm(a); break;
        case GA_REMOVE: AskRemove(a, a->selected); break;
        case GA_FLIP: FlipPage(a, act.value); break;
        case GA_SONG: PickHallSong(a, act.value); break;
        case GA_NONE: GamePage_SongKeys(&a->page, &b.v, &a->kit[UI_GAME], l, in, wheelFree); break; /* Up/Down on Chipforge's songs */
        default: break;
    }
}

/* What hall.c read from the keys, the pad, the wheel and the clicks: first
 * the move, then the one action (Right and Enter together still play the
 * next machine, as they always did). */
static void DoHall(App *a, const HallIntent *h, double now) {
    Select(a, Lineup_IndexAt(&a->lineup, h->moveTo)); /* hall.c moves along places */
    bool any = RowShown(a);
    switch (h->verb) {
        case HV_PLAY: if (any) PressPlay(a, now); break;
        case HV_DAILY: if (any) PressDaily(a); break;
        case HV_THIS_GAME: OpenGamePage(a); break;
        case HV_FAVOURITE: if (any) ToggleFavourite(a); break;
        case HV_ORDER: NextOrder(a); break;
        case HV_DESK: OpenDesk(a, a->selected); break;
        case HV_MENU: Go(a, UI_MENU); break;
        case HV_OPEN: OpenShortcut(a, h->open); break;
        case HV_LEAVE: Leave(a); break;
        case HV_UPDATES: OpenUpdates(a); break;
        case HV_PLAYER: OpenSettings(a, ST_PLAYER); break;
        case HV_ADD: if (a->adminUnlocked) OpenAddForm(a); break;
        case HV_EDIT: if (a->adminUnlocked && any) OpenEditForm(a); break;
        case HV_REMOVE: if (a->adminUnlocked && any) AskRemove(a, a->selected); break;
        default: break;
    }
}

/* While the coin drops nothing on the row reacts: keys, pad, wheel and
 * clicks wait for the game (Layout gives the clicks only the X). */
static void UpdateList(App *a, const Input *in, const KitEvent *ev, float dt, double now) {
    if (a->coinTimer >= 0.0f) {
        TickCoin(a, dt, now);
        return;
    }
    HallIntent h = Hall_Read(&a->hallIn, in, ev, Place(a), a->lineup.count, a->adminUnlocked);
    DoHall(a, &h, now);
    RefreshCardIfStale(a, dt);
}

/* The hall stays live behind the menu (its card keeps refreshing). */
static void UpdateMenu(App *a, const Input *in, const KitEvent *ev, float dt) {
    RefreshCardIfStale(a, dt);
    if (ev->type == KIT_EV_PRESS) {
        int item = Screens_MenuItemFor(ev->id);
        if (item >= 0) { OpenMenuItem(a, item); return; }
        if (ev->id == WID_MENU_VEIL) { GoBack(a); return; } /* a click beside the panel */
    }
    if (Keymap_Pressed(KA_MENU_CLOSE, in)) {
        GoBack(a);
        return;
    }
    LetterShortcut(a, in);
}

/* ---------------------------------------------------- secondary screens */

static int TrophyCount(const App *a) {
    if (a->trophyGame < 0 || a->trophyGame >= a->manifest.count) return 0;
    char slug[64];
    return Scores_SlugFromExec(a->manifest.games[a->trophyGame].exec, slug, sizeof(slug)) ? Ach_CountForGame(slug) : 0;
}

/* Left/Right (or the stepper's arrows) change the game; Up/Down scroll.
 * wheelFree: no control under the pointer took the wheel. */
static void UpdateTrophies(App *a, const Input *in, const KitEvent *ev, bool wheelFree) {
    if (BackPressed(in, ev) || Keymap_Pressed(KA_OPEN_TROPHIES, in)) {
        GoBack(a);
        return;
    }
    if (a->manifest.count == 0) return;
    if (ev->type == KIT_EV_CHANGE && ev->id == WID_TROPHY_GAME) {
        a->trophyGame = ev->value;
        a->trophyScroll = (KitScroll){0, 0.0f};
    }
    Kit_Scroll(&a->trophyScroll, TrophyCount(a), TROPHY_VISIBLE, in, wheelFree);
}

/* Turning sharing on syncs at once, as Sync scores now does. */
static void StartSync(App *a, double now) {
    if (a->syncing || !a->syncCfg.enabled) return;
    Launch_SyncDetached();
    a->syncing = true;
    a->syncNoAnswer = false;
    a->syncStarted = now;
    a->syncBaseline = Settings_LastSync();
    a->syncPoll = 0.0f;
}

/* Why Restart the launcher must wait, "" when it need not. */
static const char *RestartWhy(void) {
    if (Launch_RunningCount() > 0) return "Close your games first.";
    if (Launch_InstallStatus(NULL) == INSTALL_RUNNING) return "Wait for the install to finish first.";
    return "";
}

static void StartCheck(App *a, double now) {
    if (a->checking || !a->syncCfg.checkUpdates) return;
    Launch_CheckUpdateNow();
    a->checking = true;
    a->checkNoAnswer = false;
    a->checkStarted = now;
    a->checkBaseline = a->updateState.checkedAt;
    a->updateReloadTimer = 0.0f;
}

/* V closes it again. The pad's View button is the old Back/Select, which read as R. */
/* Update now (and Try again): install.sh --update in the background, if
 * nothing stands in the way; the screen shows its steps from here on. */
static void StartSelfUpdate(App *a, double now) {
    PollElsewhere(a, now); /* not only the last 5 s poll: one may have just begun */
    UpdateWhyNot(a);
    if (a->updWhy[0]) return; /* the button is faint and says why */
    Launch_UpdateAcknowledge();
    SelfUpdate_LogReset(&a->updLog);
    char err[160] = "";
    if (!Launch_UpdateStart(BUILD_SOURCE_DIR, err, sizeof(err))) {
        a->updView = UPV_FAILED;
        a->updFail = SU_FAIL_START;
        a->updSeen = true; /* on screen now */
        SelfUpdate_LogFeed(&a->updLog, err, strlen(err));
        SelfUpdate_LogFeed(&a->updLog, "\n", 1);
        Kit_Enter(&a->kit[UI_UPDATE], WID_UPDATE_BACK);
        return;
    }
    a->updView = UPV_RUNNING;
    a->updStarted = a->updStepAt = now;
    a->updCancelAt = -1.0;
    a->updHardSent = false;
    a->updPoll = 0.0f;
    UpdateWhyNot(a);
    Kit_Enter(&a->kit[UI_UPDATE], WID_UPDATE_CANCEL);
}

/* Stop: only before Installing begins (the log is read once more first). */
static void CancelSelfUpdate(App *a, double now) {
    if (a->updView != UPV_RUNNING || a->updFollow || a->updCancelAt >= 0.0) return;
    ReadUpdateLog(a, now);
    if (!SelfUpdate_CanCancel(a->updLog.step)) return;
    if (Launch_UpdateCancel(false)) a->updCancelAt = now;
}

/* V closes it again. The pad's View button is the old Back/Select, which read as R. */
static void UpdateUpdates(App *a, const Input *in, const KitEvent *ev, double now) {
    if (BackPressed(in, ev) || Keymap_Pressed(KA_OPEN_UPDATES, in)) {
        GoBack(a);
        return;
    }
    int pressed = ev->type == KIT_EV_PRESS ? ev->id : KIT_NONE;
    switch (a->updView) {
        case UPV_RUNNING:
            if (pressed == WID_UPDATE_CANCEL) CancelSelfUpdate(a, now);
            break;
        case UPV_DONE:
            /* main() saves, then starts the new one; not while a game or an
             * install the launcher started still runs (the new one would
             * not know them). */
            if (pressed == WID_UPDATE_RESTART && !RestartWhy()[0]) a->restart = a->quit = true;
            else if (pressed == WID_UPDATE_LATER) GoBack(a);
            break;
        case UPV_FAILED:
            if (pressed == WID_UPDATE_RETRY || Keymap_Pressed(KA_UPDATE_NOW, in)) StartSelfUpdate(a, now);
            else if (pressed == WID_UPDATE_BACK) GoBack(a);
            break;
        default:
            if (Keymap_Pressed(KA_CHECK_NOW, in) || pressed == WID_CHECK_NOW) StartCheck(a, now);
            if (UpdateBehind(a) && (Keymap_Pressed(KA_UPDATE_NOW, in) || pressed == WID_UPDATE_NOW)) StartSelfUpdate(a, now);
            break;
    }
}

static void SaveOnline(App *a, bool share) {
    Toast(a, SyncConfig_SaveEnabled(share));
    SyncConfig_Load(&a->syncCfg);
    a->onlineChoice = share;
    a->onlineFirstTime = false;
    GoBack(a);
}

/* The two answers are buttons: Left/Right move between them, Enter or a
 * click saves the one pressed. Y, N, A and D still pick, as before. */
static void UpdateOnline(App *a, const Input *in, const KitEvent *ev) {
    KitState *k = &a->kit[UI_ONLINE];
    if (Keymap_Pressed(KA_ONLINE_FLIP, in)) k->focus = k->focus == WID_ONLINE_YES ? WID_ONLINE_NO : WID_ONLINE_YES;
    if (Keymap_Pressed(KA_ONLINE_YES, in)) k->focus = WID_ONLINE_YES;
    if (Keymap_Pressed(KA_ONLINE_NO, in)) k->focus = WID_ONLINE_NO;
    a->onlineChoice = k->focus == WID_ONLINE_YES;
    if (ev->type == KIT_EV_PRESS && (ev->id == WID_ONLINE_YES || ev->id == WID_ONLINE_NO)) {
        SaveOnline(a, ev->id == WID_ONLINE_YES);
        return;
    }
    if (BackPressed(in, ev) || (Keymap_Pressed(KA_OPEN_ONLINE, in) && !a->onlineFirstTime)) {
        if (a->onlineFirstTime) SaveOnline(a, false); /* no answer is a no */
        else GoBack(a);
    }
}

/* Save on the form, once it is valid. A rename (or a new program) can
 * carry the launcher's own records over: playtime and the game's player
 * name are kept by name, the row's star and place by program. Editing
 * leaves the hall's lit machine as it was; a new machine lights up. */
static void SaveForm(App *a) {
    char v[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    GameForm_Values(&a->form, v);
    CarryOver carry;
    GameForm_WhatStays(&a->form, &a->manifest, &a->stats, &a->profiles, &a->ini, &carry);
    bool move = a->form.carry && Machines_CanCarry(&carry);
    char oldName[MANIFEST_FIELD_LEN] = "";
    bool ok = true;
    int keep = a->selected, shown = a->form.editing;
    if (a->ui == UI_ADD_GAME) {
        ok = Manifest_AddGame(&a->manifest, v[0], v[1], v[2], v[3], v[4]); /* false: the arcade is full */
        if (ok) keep = shown = a->manifest.count - 1;
    } else if (a->form.editing >= 0 && a->form.editing < a->manifest.count) {
        GameEntry *e = &a->manifest.games[a->form.editing];
        snprintf(oldName, sizeof(oldName), "%s", e->name);
        char *dst[MANIFEST_FIELD_COUNT] = {e->name, e->exec, e->icon, e->desc, e->install};
        for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) snprintf(dst[i], MANIFEST_FIELD_LEN, "%.*s", MANIFEST_FIELD_LEN - 1, v[i]);
    }
    bool saved = SaveCatalog(a, ok, keep, Place(a));
    if (saved && move) {
        if (!Machines_Carry(&carry, oldName, v[0], &a->stats, &a->profiles, &a->ini)) Toast(a, false);
        Reorder(a, a->selected, Place(a));
    }
    GoBack(a);
    if (saved && a->ui == UI_SETTINGS) Settings_ShowMachine(&a->settings, Lineup_PlaceOf(&a->mine, shown), a->mine.count);
}

/* The clipboard is asked for only on Ctrl+V: on Wayland it can be slow, or
 * come back empty (then nothing is pasted). */
static const char *Clipboard(const Input *in) {
    const char *clip = TextField_WantsPaste(in) ? GetClipboardText() : NULL;
    return clip ? clip : "";
}

/* The cursor holds still while it moves, and blinks once it rests. */
static void KeepCursorSteady(TextField *f, int cursorBefore, int lengthBefore) {
    if (f->cursor != cursorBefore || TextField_Length(f) != lengthBefore) f->steadySince = GetTime();
}

/* Layout and drawing both need what a save would leave behind. */
static void FormCarry(const App *a, CarryOver *c) {
    GameForm_WhatStays(&a->form, &a->manifest, &a->stats, &a->profiles, &a->ini, c);
}

/* The form moves its own focus and types into its fields (gameform.h);
 * leaving with changes asks first, as it always has. */
static void UpdateForm(App *a, const Input *in) {
    KitList l;
    CarryOver c;
    FormCarry(a, &c);
    GameForm_Layout(&a->form, &c, &l);
    TextField *f = &a->form.field;
    int active = a->form.active, cursor = f->cursor, length = TextField_Length(f);
    GameFormResult r = GameForm_Update(&a->form, &a->kit[a->ui], &l, in, Clipboard(in), &a->manifest);
    if (a->form.active != active) {
        f->steadySince = GetTime();
        Input_DropHolds(&a->input); /* a held Backspace stays with the field it began in */
    } else {
        KeepCursorSteady(f, cursor, length);
    }
    switch (r) {
        case GF_QUIT: a->quit = true; break;
        case GF_SAVE: SaveForm(a); break;
        case GF_CANCEL:
            if (GameForm_Changed(&a->form))
                OpenDialog(a, DLG_DISCARD, 0, "Throw away your changes?", "The game list stays as it was.", NULL, "Keep editing", "Throw away");
            else GoBack(a);
            break;
        default: break;
    }
}

static void UpdateStats(App *a, const Input *in, const KitEvent *ev, bool wheelFree) {
    if (BackPressed(in, ev) || Keymap_Pressed(KA_OPEN_STATS, in)) {
        GoBack(a);
        return;
    }
    Kit_Scroll(&a->statsScroll, a->manifest.count, STATS_VISIBLE, in, wheelFree);
}

/* ------------------------------------------------------------- settings */

/* The page is rebuilt for each use (it follows the files it shows); one
 * copy, not on the stack: it is large. */
static SettingsPage sPage;

static const SettingsPage *SettingsPageNow(const App *a) {
    SettingsView v = ViewSettings(a);
    Settings_Layout(&a->settings, &v, &sPage);
    return &sPage;
}

static void SharingChanged(App *a, bool on, double now) {
    bool ok = Settings_SaveOnline("enabled", on);
    Toast(a, ok);
    SyncConfig_Load(&a->syncCfg);
    a->onlineChoice = a->syncCfg.enabled;
    if (a->syncCfg.decided) a->onlineFirstTime = false;
    if (ok && on) StartSync(a, now);
}

/* A failed save reads the file back, so the blocks show what is kept. */
static void HallSoundsChanged(App *a, int level) {
    bool ok = Settings_SaveHallSounds(&a->ini, level);
    if (!ok) LauncherIni_Load(&a->ini);
    SetHallSounds(a, Settings_HallSounds(&a->ini));
    Toast(a, ok);
}

/* As HallSoundsChanged: what the file kept is what the hints follow. */
static void ButtonHintsChanged(App *a, int hints) {
    bool ok = Settings_SaveButtonHints(&a->ini, hints);
    if (!ok) LauncherIni_Load(&a->ini);
    a->buttonHints = Settings_ButtonHints(&a->ini);
    Toast(a, ok);
}

/* As HallSoundsChanged: what the file kept is what the hall waits for. */
static void AttractChanged(App *a, int setting) {
    bool ok = Attract_SaveSetting(&a->ini, setting);
    if (!ok) LauncherIni_Load(&a->ini);
    a->attractSetting = Attract_Setting(&a->ini);
    Toast(a, ok);
}

static void HallMusicChanged(App *a, bool on) {
    bool ok = HallMusic_Save(&a->ini, on);
    if (!ok) LauncherIni_Load(&a->ini);
    a->hallMusic = HallMusic_Enabled(&a->ini);
    Toast(a, ok);
}

/* Start on matters at the next start: the row stays as it is. */
static void StartOnChanged(App *a, bool last) {
    bool ok = Row_SaveStartOn(&a->ini, last);
    if (!ok) LauncherIni_Load(&a->ini);
    a->rowPrefs.startOnLast = Row_StartOnLast(&a->ini);
    Toast(a, ok);
}

/* Settings > Machines: the row changes, and the list keeps the machine the
 * keys are on in sight (its buttons keep their ids, whatever its place). */
static void MachineChanged(App *a, int index, bool ok) {
    Toast(a, RowSaved(a, ok));
    Settings_ShowMachine(&a->settings, Lineup_PlaceOf(&a->mine, index), a->mine.count);
}

static void ApplyMachine(App *a, const SettingsAction *act) {
    int i = act->which;
    if (i < 0 || i >= a->rowPrefs.count) return;
    const RowMachine *m = &a->rowPrefs.m[i];
    switch (act->type) {
        case SA_MACH_FAVOURITE: MachineChanged(a, i, Row_SaveFavourite(&a->ini, m->slug, !m->favourite)); break;
        case SA_MACH_UP: MachineChanged(a, i, Row_Move(&a->ini, &a->rowPrefs, &a->mine, i, -1)); break;
        case SA_MACH_DOWN: MachineChanged(a, i, Row_Move(&a->ini, &a->rowPrefs, &a->mine, i, 1)); break;
        case SA_MACH_HIDE: MachineChanged(a, i, Row_CanHide(&a->rowPrefs, i) && Row_SaveHidden(&a->ini, m->slug, !m->hidden)); break;
        default: break;
    }
}

/* Clear goes once the name has gone, so the keys stay on the row, on its
 * Set (the same button as Edit); otherwise they would jump to the top. */
static void ClearName(App *a, int game) {
    if (game < 0 || game >= a->manifest.count) return;
    Toast(a, Settings_SaveName(&a->profiles, a->manifest.games[game].name, ""));
    a->cardFor = -1;
    a->kit[UI_SETTINGS].focus = NameEditId(game);
}

/* A click on the name itself puts the cursor where it landed. */
static void StartTyping(App *a, const SettingsAction *act, const Input *in) {
    BeginNameEdit(a, act->which, false, in->lastDevice == DEV_PAD);
    if (act->value == 1) Settings_ClickIntoName(&a->settings, in->mouseX);
}

static void ApplySettings(App *a, const SettingsAction *act, const Input *in, double now) {
    KitState *k = &a->kit[UI_SETTINGS];
    SettingsView v = ViewSettings(a);
    switch (act->type) {
        case SA_TAB: Settings_OpenTab(&a->settings, k, &v, act->value); break;
        case SA_SHARED: Toast(a, Settings_SaveShared((SharedPref)act->which, act->value)); break; /* Win_Update applies fullscreen */
        case SA_HALL_SOUNDS: HallSoundsChanged(a, act->value); break;
        case SA_HALL_MUSIC: HallMusicChanged(a, act->value != 0); break;
        case SA_START_ON: StartOnChanged(a, act->value != 0); break;
        case SA_ROW_ORDER: Toast(a, RowSaved(a, Row_SaveOrder(&a->ini, (RowOrder)act->value))); break;
        case SA_MACH_FAVOURITE:
        case SA_MACH_UP:
        case SA_MACH_DOWN:
        case SA_MACH_HIDE: ApplyMachine(a, act); break;
        case SA_MACH_ADD: if (a->adminUnlocked) OpenAddForm(a); break;
        case SA_MACH_EDIT: if (a->adminUnlocked) OpenEditFormFor(a, act->which); break;
        case SA_MACH_REMOVE: if (a->adminUnlocked) AskRemove(a, act->which); break;
        case SA_BUTTON_HINTS: ButtonHintsChanged(a, act->value); break;
        case SA_ATTRACT: AttractChanged(a, act->value); break;
        case SA_PLAY_COIN: PlayHall(a, a->coinSound); break;
        case SA_SHARING: SharingChanged(a, act->value != 0, now); break;
        case SA_CHECKS:
            Toast(a, Settings_SaveOnline("check_updates", act->value != 0));
            SyncConfig_Load(&a->syncCfg);
            break;
        case SA_CHECK_NOW: StartCheck(a, now); break;
        case SA_UPDATE_DETAILS: OpenUpdates(a); break;
        case SA_SYNC_NOW: StartSync(a, now); break;
        case SA_EDIT_NAME: StartTyping(a, act, in); break;
        case SA_CLEAR_NAME: ClearName(a, act->which); break;
        default: break;
    }
}

/* Inside Settings U and O go to their tabs, and Shift+U to this game's name. */
static bool SettingsLetter(App *a, const Input *in) {
    KitState *k = &a->kit[UI_SETTINGS];
    SettingsView v = ViewSettings(a);
    KeyAction key = Screens_Shortcut(in);
    if (key != KA_OPEN_NAME && key != KA_OPEN_GAME_NAME && key != KA_OPEN_ONLINE) return false;
    k->focus = KIT_NONE;
    Settings_OpenTab(&a->settings, k, &v, key == KA_OPEN_ONLINE ? ST_ONLINE : ST_PLAYER);
    if (key == KA_OPEN_GAME_NAME && RowShown(a)) {
        Settings_ShowName(&a->settings, a->selected, a->manifest.count);
        k->focus = WID_SET_GAME_EDIT + a->selected;
    }
    return true;
}

static void UpdateSettings(App *a, const Input *in, const KitEvent *ev, bool wheelFree, float dt, double now) {
    RefreshCardIfStale(a, dt); /* Machines shows which are installed */
    if (BackPressed(in, ev)) {
        GoBack(a);
        return;
    }
    if (SettingsLetter(a, in)) return;
    SettingsAction act = Settings_Event(&a->settings, ev);
    if (act.type != SA_NONE) {
        ApplySettings(a, &act, in, now);
        return;
    }
    KitState *k = &a->kit[UI_SETTINGS];
    SettingsView v = ViewSettings(a);
    const SettingsPage *page = SettingsPageNow(a);
    if (Settings_Navigate(&a->settings, k, page, &v, in)) return;
    if (wheelFree) Settings_Wheel(&a->settings, k, page, &v, in);
}

/* The name is kept (or put back); from U or Shift+U, Enter and Esc go
 * straight back to where they came from. A click elsewhere keeps what was
 * typed and stays. */
static void FinishTyping(App *a, const SettingsAction *act) {
    Settings *s = &a->settings;
    bool leave = s->quick && !(act->type == SA_SAVE_NAME && act->value == 1);
    int focus = NameEditId(s->fieldFor);
    if (act->type == SA_SAVE_NAME && (act->value == 0 || TextField_Changed(&s->field))) {
        Toast(a, Settings_SaveName(&a->profiles, Settings_NameKey(s, &a->manifest), s->field.text));
        a->cardFor = -1;
    }
    Settings_EndEdit(s, act->type == SA_SAVE_NAME);
    Input_DropHolds(&a->input);
    Kit_Enter(&a->kit[UI_SETTINGS], focus);
    if (leave) GoBack(a);
}

/* While a name is typed only the field has the keys: letters type, and the
 * screen's own buttons wait. */
static void UpdateTyping(App *a, const Input *in) {
    TextField *f = &a->settings.field;
    int cursor = f->cursor, length = TextField_Length(f);
    SettingsAction act = Settings_Type(&a->settings, SettingsPageNow(a), in, Clipboard(in));
    KeepCursorSteady(f, cursor, length);
    if (act.type == SA_QUIT) a->quit = true;
    else if (act.type == SA_SAVE_NAME || act.type == SA_CANCEL_NAME) FinishTyping(a, &act);
}

static bool Typing(const App *a) { return a->ui == UI_SETTINGS && a->settings.field.editing; }

/* --------------------------------------------------------------- layout */

static bool UpdateBehind(const App *a) { return a->updateState.status == UPDATE_BEHIND; }

/* What hints name now: Settings > Screen > Button hints, or (Auto) the
 * keyboard or the pad, whichever was used last. */
static InputDevice HintDevice(const App *a) { return Settings_HintDevice(a->buttonHints, a->input.hintDevice); }

/* What Play does on the selected machine (the card is refreshed with it). */
static HallPlay PlayState(const App *a) {
    if (!RowShown(a)) return HP_NONE;
    if (a->card.install == INSTALL_RUNNING) return HP_INSTALLING;
    if (Updating(a)) return HP_UPDATING;
    if (a->card.installed) return HP_PLAY;
    return a->card.canInstall ? HP_INSTALL : HP_CANNOT;
}

/* The machine lit in the middle: attract mode's tour, else yours. */
static int ShownPlace(const App *a) {
    if (!RowShown(a)) return 0;
    int p = Attract_ShownPlace(&a->attract, Place(a));
    return p < a->lineup.count ? p : Place(a);
}

/* Attract mode: how far down the card has slid; off the screen by the end. */
static float CardSlide(const App *a) { return Attract_CardSlide(&a->attract, (float)(KIT_SCREEN_H - HALL_CARD_Y + 8)); }

/* One source of geometry: Update hit-tests it, Draw draws it. While a coin
 * drops only the X takes clicks, but the header and the strip stay in
 * sight (forDrawing) instead of blinking out for a second. */
static void HallLayout(const App *a, KitList *l, bool forDrawing) {
    char daily[96] = "", chalk[48];
    Desk_BoardLine(&a->desk, chalk, sizeof(chalk));
    const RowMachine *lit = RowShown(a) && a->selected < a->rowPrefs.count ? &a->rowPrefs.m[a->selected] : NULL;
    if (lit) DailyWhy(a, &a->manifest.games[a->selected], a->card.installed, a->card.dailyGame, daily, sizeof(daily));
    HallView v = {
        .coinInFlight = a->coinTimer >= 0.0f && !forDrawing,
        .updateBehind = UpdateBehind(a),
        .arcadeName = Profiles_GetOwnUsername(&a->profiles, PROFILE_DEFAULT_KEY),
        .admin = a->adminUnlocked,
        .manifest = &a->manifest,
        .lineup = &a->lineup,
        .place = ShownPlace(a),
        .scroll = a->row.scroll,
        .device = HintDevice(a),
        .play = PlayState(a),
        .dailyReason = daily,
        .tool = lit && lit->tool,
        .favourite = lit && lit->favourite,
        .canFavourite = lit && lit->slug[0],
        .allHidden = !RowShown(a) && a->manifest.count > 0,
        .deskLine = chalk,
        .cardSlide = CardSlide(a),
    };
    Screens_Hall(l, &v);
}

static void Layout(const App *a, UIState ui, KitList *l, bool forDrawing) {
    switch (ui) {
        case UI_LIST: HallLayout(a, l, forDrawing); break;
        case UI_MENU: Screens_Menu(l, UpdateBehind(a), HintDevice(a)); break;
        case UI_ONLINE: Screens_Online(l); break;
        case UI_TROPHIES: {
            int n = a->manifest.count, g = a->trophyGame;
            Screens_Trophies(l, (g >= 0 && g < n) ? a->manifest.games[g].name : "", g, n);
            break;
        }
        case UI_UPDATE: {
            UpdatesLayout v = {
                .mode = a->updView,
                .checking = a->checking,
                .checksOff = !a->syncCfg.checkUpdates,
                .offerUpdate = UpdateBehind(a),
                .whyNot = a->updWhy,
                .canCancel = SelfUpdate_CanCancel(a->updLog.step),
                .elsewhere = a->updFollow,
                .restartWhy = RestartWhy(),
                .cancelling = a->updCancelAt >= 0.0,
            };
            Screens_Updates(l, &v);
            break;
        }
        case UI_SETTINGS: *l = SettingsPageNow(a)->list; break;
        case UI_GAME: {
            PageViewNow b;
            ViewPage(a, &b);
            GamePage_Layout(&a->page, &b.v, l);
            break;
        }
        case UI_DAILY:
            Desk_Layout(&a->desk, &a->deskScroll, l);
            if (Updating(a))
                for (int i = 0; i < l->count; i++)
                    if (Desk_RowOf(l->items[i].id) >= 0) Kit_Disable(&l->items[i], UPDATING_WHY);
            break;
        case UI_ADD_GAME:
        case UI_EDIT_GAME: {
            CarryOver c;
            FormCarry(a, &c);
            GameForm_Layout(&a->form, &c, l);
            break;
        }
        default: Screens_Plain(l); break;
    }
}

/* ------------------------------------------------------- update + draw */

/* Remove, answered: from a page, back to the hall; in Settings > Machines
 * the keys go to the machine now in its place (its Remove has gone). */
static void RemoveAnswered(App *a, int index) {
    int pos = Lineup_PlaceOf(&a->mine, index);
    UIState from = a->ui;
    RemoveGame(a, index);
    if (from == UI_GAME) {
        GoHome(a);
        return;
    }
    if (from != UI_SETTINGS || a->mine.count == 0) return;
    if (pos < 0) pos = 0;
    if (pos >= a->mine.count) pos = a->mine.count - 1;
    a->kit[UI_SETTINGS].focus = WID_SET_MACH_EDIT + Lineup_IndexAt(&a->mine, pos);
    Settings_ShowMachine(&a->settings, pos, a->mine.count);
}

/* The box is modal: nothing behind it reacts, except the X. */
static void UpdateDialog(App *a, const Input *in) {
    KitList l;
    Screens_Dialog(&l, &a->dialog);
    KitEvent ev = Kit_Update(&a->dialogKit, &l, in);
    if (ev.type == KIT_EV_PRESS && ev.id == WID_CLOSE) {
        a->quit = true;
        return;
    }
    int kind = a->dialog.kind, arg = a->dialog.arg;
    if (Kit_DialogAnswer(&a->dialog, &ev, in) != KIT_DLG_ACT) return;
    if (kind == DLG_REMOVE) RemoveAnswered(a, arg);
    else if (kind == DLG_LEAVE) a->quit = true;
    else if (kind == DLG_DISCARD) GoBack(a);
}

static void Update(App *a, const Input *in, float dt, double now) {
    if (a->dialog.open) {
        UpdateDialog(a, in);
        return;
    }
    if (Typing(a)) {
        UpdateTyping(a, in);
        return;
    }
    if (a->ui == UI_ADD_GAME || a->ui == UI_EDIT_GAME) { /* the form runs its own buttons */
        UpdateForm(a, in);
        return;
    }
    KitList l;
    Layout(a, a->ui, &l, false);
    Input settled;
    const Input *kin = in;
    bool pageSettling = a->ui == UI_GAME && GamePage_ClickSettling(a->pageClickedAt, now); /* a double-click's second half */
    bool deskSettling = a->ui == UI_DAILY && GamePage_ClickSettling(a->deskClickedAt, now);
    bool hallSettling = a->ui == UI_LIST && Attract_Settling(&a->attract);          /* the move that woke the hall, and its click */
    if (pageSettling || deskSettling || hallSettling) {
        settled = *in;
        settled.mousePressed[MB_LEFT] = false;
        /* A wheel flick is one gesture: the notches right behind the one
         * that woke the hall belong to the waking too. */
        if (hallSettling) settled.wheel = 0.0f;
        kin = &settled;
    }
    KitEvent ev = Kit_Update(&a->kit[a->ui], &l, kin);
    if (ev.type == KIT_EV_PRESS && ev.id == WID_CLOSE) {
        a->quit = true; /* the X quits at once, whatever is going on */
        return;
    }
    bool wheelFree = !Kit_OwnsWheel(&a->kit[a->ui], &l);
    switch (a->ui) {
        case UI_LIST: UpdateList(a, kin, &ev, dt, now); break;
        case UI_MENU: UpdateMenu(a, in, &ev, dt); break;
        case UI_TROPHIES: UpdateTrophies(a, in, &ev, wheelFree); break;
        case UI_UPDATE: UpdateUpdates(a, in, &ev, now); break;
        case UI_ONLINE: UpdateOnline(a, in, &ev); break;
        case UI_STATS: UpdateStats(a, in, &ev, wheelFree); break;
        case UI_SETTINGS: UpdateSettings(a, in, &ev, wheelFree, dt, now); break;
        case UI_GAME: UpdateGamePage(a, in, &ev, &l, wheelFree, dt, now); break;
        case UI_DAILY: UpdateDesk(a, in, &ev, wheelFree, dt); break;
        default: break;
    }
}

/* The card's one line. A faint button under the pointer says why it is
 * faint there, as the Settings help line does. */
/* An update that ended while Updates was not up, until Updates shows it. */
static HallUpdateNews UpdateNews(const App *a) {
    if (a->updSeen) return HUN_NONE;
    if (a->updView == UPV_FAILED) return HUN_FAILED;
    if (a->updView == UPV_DONE) return HUN_DONE;
    return HUN_NONE;
}

static void HallStatusNow(const App *a, const KitView *kv, HallStatus *out) {
    const char *why = kv->state ? Kit_Reason(kv->state, kv->list, kv->device) : "";
    char log[600], shown[600];
    Launch_InstallLogPath(log, sizeof(log));
    Hall_Tilde(log, getenv("HOME"), shown, sizeof(shown));
    HallStatusIn in = {
        .installing = a->card.install == INSTALL_RUNNING,
        .installFailed = a->card.install == INSTALL_FAILED,
        .updating = Updating(a) && a->card.installed,
        .updateNews = UpdateNews(a),
        .error = a->errorMsg,
        .note = why[0] ? why : a->note,
        .installed = a->card.installed,
        .canInstall = a->card.canInstall,
        .tool = a->card.tool,
        .device = HintDevice(a),
        .installLog = shown,
    };
    Hall_Status(&in, out);
}

/* Chipforge's screen: the hall's song since music.c began it (real time, as
 * the synth keeps it, even while the hall rests). */
static JukeView JukeNow(const App *a) {
    return (JukeView){&a->tune, GetTime() - a->tuneSince, a->hallMusic, HallMusic_SongWords(&a->hallSong)};
}

static void DrawHallScreen(App *a, float dt, const KitView *kv) {
    const Manifest *m = &a->manifest;
    HallStatus status;
    HallStatusNow(a, kv, &status);
    JukeView juke = JukeNow(a);
    int shown = ShownPlace(a), lit = Lineup_IndexAt(&a->lineup, shown);
    HallScene s = {
        .m = m,
        .lineup = &a->lineup,
        .selected = a->selected,
        .place = shown,
        .lit = lit >= 0 ? lit : a->selected,
        .cardOut = a->attract.cardOut,
        .cardSlide = CardSlide(a),
        .attract = a->attract.on,
        .desk = &a->desk,
        .juke = &juke,
        .order = a->rowPrefs.order,
        .allHidden = !RowShown(a) && m->count > 0,
        .installed = a->installed,
        .prefs = &a->rowPrefs,
        .card = RowShown(a) ? &a->card : NULL,
        .arcadeName = Profiles_GetOwnUsername(&a->profiles, PROFILE_DEFAULT_KEY),
        .admin = a->adminUnlocked,
        .updateBehind = UpdateBehind(a),
        .row = &a->row,
        .dt = dt,
        .coinT = a->coinTimer >= 0.0f ? a->coinTimer / COIN_ANIM_SECONDS : -1.0f,
        .status = &status,
    };
    Render_List(&s, kv);
}

/* Under the page's buttons: an install under way or failed, an error, or
 * why the last press did nothing; nothing otherwise (the page has no
 * "Press Enter" prompt: its buttons say it). */
static void PageStatus(const App *a, HallStatus *out) {
    memset(out, 0, sizeof(*out));
    bool installing = a->card.install == INSTALL_RUNNING, failed = a->card.install == INSTALL_FAILED;
    HallUpdateNews news = UpdateNews(a);
    if (!installing && !failed && !a->errorMsg[0] && !a->note[0] && news == HUN_NONE) return;
    char log[600], shown[600];
    Launch_InstallLogPath(log, sizeof(log));
    Hall_Tilde(log, getenv("HOME"), shown, sizeof(shown));
    HallStatusIn in = {.installing = installing, .installFailed = failed, .updateNews = news, .error = a->errorMsg, .note = a->note,
                       .installed = true, .device = HintDevice(a), .installLog = shown};
    Hall_Status(&in, out);
}

/* The sentence for the button the keys are on, or the pointer: a faint
 * one says why it is faint. */
static void PageHelp(const App *a, const KitView *kv, const GamePageView *v, char *out, size_t size) {
    out[0] = '\0';
    if (!kv->state) return;
    if (kv->device == DEV_MOUSE) {
        const char *why = Kit_Reason(kv->state, kv->list, kv->device);
        if (why[0]) snprintf(out, size, "%s", why);
        else GamePage_Help(v, &a->pageData, kv->state->hover, out, size);
        return;
    }
    const KitItem *f = Kit_Get(kv->list, kv->state->focus);
    if (f && (f->flags & KIT_DISABLED)) snprintf(out, size, "%s", f->reason);
    else GamePage_Help(v, &a->pageData, kv->state->focus, out, size);
}

static void DrawGamePage(const App *a, const KitView *kv) {
    PageViewNow b;
    ViewPage(a, &b);
    HallStatus status;
    PageStatus(a, &status);
    char help[160], hint[200];
    PageHelp(a, kv, &b.v, help, sizeof(help));
    GamePage_Hint(&a->page, &b.v, kv->hints, hint, sizeof(hint));
    PageScene ps = {
        .game = &a->manifest.games[a->selected],
        .data = &a->pageData,
        .page = &a->page,
        .view = &b.v,
        .installed = a->card.installed,
        .installing = a->card.install == INSTALL_RUNNING,
        .dailyGame = a->card.dailyGame,
        .status = &status,
        .help = help,
        .hint = hint,
        .syncedAt = a->lastSync / 1000000000LL,
        .now = (long long)time(NULL),
        .sharing = a->syncCfg.enabled,
    };
    JukeView juke = JukeNow(a);
    if (a->pageData.tool) ps.juke = &juke;
    Render_GamePage(&ps, kv);
}

static void DrawDesk(const App *a, const KitView *kv) {
    char help[160] = "", hint[160];
    if (kv->state) {
        const char *why = kv->device == DEV_MOUSE ? Kit_Reason(kv->state, kv->list, kv->device) : "";
        int row = Desk_RowOf(kv->device == DEV_MOUSE ? kv->state->hover : kv->state->focus);
        if (why[0]) snprintf(help, sizeof(help), "%s", why);
        else if (row >= 0 && Updating(a)) snprintf(help, sizeof(help), "%s", UPDATING_WHY);
        else Desk_Help(&a->desk, row, help, sizeof(help));
    }
    Desk_Hint(kv->hints, hint, sizeof(hint));
    DeskScene ds = {&a->desk, a->deskScroll.top, help, hint, a->lastSync / 1000000000LL, (long long)time(NULL), a->syncCfg.enabled};
    Render_Desk(&ds, kv);
}

static void DrawForm(const App *a, const KitView *kv) {
    CarryOver c;
    FormCarry(a, &c);
    char stays[128], scores[128], moves[128];
    bool editing = a->form.editing >= 0 && a->form.editing < a->manifest.count;
    Machines_CarryWords(&c, editing ? a->manifest.games[a->form.editing].name : "", stays, scores, moves, sizeof(stays));
    FormScene fs = {a->ui == UI_ADD_GAME ? "Add a machine" : "Edit a machine", &a->form, stays, scores, moves};
    Render_GameForm(&fs, kv);
}

static void DrawUpdates(const App *a, const KitView *kv) {
    char words[2][128], log[600], shown[600], command[400];
    SelfUpdate_FailWords(a->updFail, &a->updLog, words[0], words[1], sizeof(words[0]));
    Launch_UpdateLogPath(log, sizeof(log));
    Hall_Tilde(log, getenv("HOME"), shown, sizeof(shown));
    snprintf(command, sizeof(command), "cd %.300s && ./install.sh --update", a->sourceDir);
    double now = GetTime();
    UpdateView v = {
        .checking = a->checking,
        .noAnswer = a->checkNoAnswer,
        .checksOff = !a->syncCfg.checkUpdates,
        .mode = a->updView,
        .whyNot = a->updWhy,
        .step = a->updLog.step,
        .elapsed = now - a->updStarted,
        .stepElapsed = now - a->updStepAt,
        .cancelling = a->updCancelAt >= 0.0,
        .version = a->updLog.version,
        .words = {words[0], words[1]},
        .cancelled = a->updFail == SU_FAIL_CANCELLED,
        .terminalHelps = SelfUpdate_TerminalHelps(a->updFail),
        .log = &a->updLog,
        .logPath = shown,
        .command = command,
    };
    Render_Update(&a->updateState, &v, kv);
}

static void DrawScreen(App *a, float dt, const KitView *kv) {
    const Manifest *m = &a->manifest;
    switch (a->ui) {
        case UI_LIST: DrawHallScreen(a, dt, kv); break;
        case UI_MENU: {
            KitList hall;
            Layout(a, UI_LIST, &hall, true);
            KitView behind = {&hall, NULL, kv->device, kv->hints}; /* dimmed, at rest */
            DrawHallScreen(a, dt, &behind);
            Render_ArcadeMenu(kv);
            break;
        }
        case UI_TROPHIES: Render_Achievements(m, a->trophyGame, a->trophyScroll.top, kv); break;
        case UI_UPDATE: DrawUpdates(a, kv); break;
        case UI_ONLINE: Render_OnlineAsk(a->onlineFirstTime, a->syncCfg.enabled, kv); break;
        case UI_STATS: Render_Stats(&a->stats, m, a->runSums, a->statsScroll.top, kv); break;
        case UI_SETTINGS: Render_Settings(&sPage, &a->settings, kv); break;
        case UI_ADD_GAME:
        case UI_EDIT_GAME: DrawForm(a, kv); break;
        case UI_GAME: DrawGamePage(a, kv); break;
        case UI_DAILY: DrawDesk(a, kv); break;
        default: break;
    }
}

/* The hand over anything that can be pressed. */
static void PointAt(App *a, bool hand) {
    int want = hand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT;
    if (want == a->cursor) return;
    SetMouseCursor(want);
    a->cursor = want;
}

/* The pointer gets out of the way once keys or the pad take over, and is
 * back the moment the mouse moves (or the window loses focus). */
static void ShowPointer(App *a, const Input *in) {
    bool keys = in->focused && in->any && in->lastDevice != DEV_MOUSE;
    bool mouse = !in->focused || (in->any && in->lastDevice == DEV_MOUSE);
    if (keys && !a->pointerHidden) {
        HideCursor();
        a->pointerHidden = true;
    } else if (mouse && a->pointerHidden) {
        ShowCursor();
        a->pointerHidden = false;
    }
}

/* The buttons' state: a name being typed has its own (only its buttons
 * answer the mouse); a box over the screen leaves it at rest. */
static const KitState *ScreenKit(const App *a) {
    if (a->dialog.open) return NULL;
    return Typing(a) ? &a->settings.editKit : &a->kit[a->ui];
}

/* Exactly one frame, of whatever is up after this frame's input: the
 * screen, then the dialog over it, then the "Saved" note over both. */
static void Draw(App *a, float dt, InputDevice device) {
    /* The row moves before it is laid out: the next click is tested against
     * exactly what this frame shows. */
    if (a->ui == UI_LIST || a->ui == UI_MENU) Hall_RowStep(&a->row, ShownPlace(a), dt);
    Win_BeginFrame();
    KitList l;
    Layout(a, a->ui, &l, true);
    KitView kv = {&l, ScreenKit(a), device, HintDevice(a)};
    DrawScreen(a, dt, &kv);
    bool hand = kv.state && Kit_WantsHand(kv.state, &l);
    if (a->dialog.open) {
        KitList dl;
        Screens_Dialog(&dl, &a->dialog);
        KitView dv = {&dl, &a->dialogKit, Kit_DialogDevice(&a->dialogKit, &dl, device), HintDevice(a)};
        Render_Dialog(&a->dialog, &dv);
        hand = Kit_WantsHand(&a->dialogKit, &dl);
    }
    Render_Toast(a->toast, a->toastBad, a->toastTimer / 0.3f, a->ui == UI_LIST && !a->dialog.open);
    Win_EndFrame();
    PointAt(a, hand);
}

/* What the hall plays may have changed in Chipforge: the song is read again
 * only when its choice or its file did, and the music crossfades to it. */
static void CheckHallSong(App *a) {
    if (HallMusic_Stamp() == a->hallSong.stamp) return;
    HallMusic_Song(&a->hallSong);
    Music_SetTheme(HALL_MUSIC_SLUG);
    HallMusic_Tune(&a->tune); /* the jukebox's notes, from the top as the music starts again */
    a->tuneSince = GetTime();
    a->cardFor = -1; /* Chipforge's card names the song */
}

/* Every frame: music.c picks up a theme switch it had to put off here. */
static void TickMusic(App *a, const Input *in) {
    HallMusicState st = {a->hallMusic, in->focused, a->coinTimer >= 0.0f, Launch_RunningCount() > 0};
    a->musicMode = HallMusic_Mode(st);
    a->musicVolume = HallMusic_Volume(Prefs_Get()->musicVolume);
    Music_SetVolume(a->musicVolume);
    Music_SetMode(a->musicMode);
}

/* Behind a game the launcher started, the hall rests: SetTargetFPS only on
 * a change, and the animations' clock held still while it rests. */
static void TickQuiet(App *a, const Input *in, float dt) {
    a->quiet = Hall_Quiet(Launch_RunningCount() > 0, in->focused);
    if (a->quiet) a->restedFor += dt;
    int fps = Hall_TargetFps(a->quiet);
    if (fps != a->fps) {
        SetTargetFPS(fps);
        a->fps = fps;
    }
    KitDraw_SetClock(GetTime() - a->restedFor);
}

/* Someone is there while anything is held down, though nothing new is pressed. */
static bool AnyHeld(const Input *in) {
    if (in->shift || in->ctrl || in->alt) return true;
    for (int k = 0; k < IK_COUNT; k++) if (in->keyDown[k]) return true;
    for (int b = 0; b < PB_COUNT; b++) if (in->padDown[b]) return true;
    for (int b = 0; b < MB_COUNT; b++) if (in->mouseDown[b]) return true;
    return false;
}

/* Attract mode, every frame. True when this frame's input only woke the
 * hall: then it does nothing else. A press half made when the tour began
 * or ended never lands afterwards either. */
static bool TickAttract(App *a, const Input *in, float dt) {
    static bool shown[MANIFEST_MAX_GAMES];
    for (int p = 0; p < a->lineup.count; p++) shown[p] = a->installed[a->lineup.index[p]];
    AttractIn ai = {
        .setting = a->attractSetting,
        .onHall = a->ui == UI_LIST && !a->dialog.open && a->coinTimer < 0.0f,
        .gameRunning = Launch_RunningCount() > 0,
        .input = in->any || in->modPressed, /* Shift alone wakes it, as it wakes a screen */
        .held = AnyHeld(in),
        .place = Place(a),
        .count = a->lineup.count,
        .shown = shown,
    };
    AttractEvent ev = Attract_Step(&a->attract, &ai, dt);
    if (ev == ATTRACT_BEGAN || ev == ATTRACT_WOKE) {
        Kit_Enter(&a->kit[UI_LIST], a->kit[UI_LIST].focus);
        Input_DropHolds(&a->input);
        a->hallIn.wheel = 0.0f; /* no part notch from before carries over */
    }
    return ev == ATTRACT_WOKE;
}

/* One pass of the main loop: read input once, update, draw one frame. */
static void Frame(App *a) {
    float dt = GetFrameTime();
    double now = GetTime();
    InputRaw raw;
    InputRL_Read(&raw);
    Input in;
    Input_Step(&a->input, &raw, dt, &in);

    bool gameExited = TickBackground(a, dt, now);
    bool woke = TickAttract(a, &in, dt);
    /* Before Win_Update: on F11 / Alt+Enter it writes prefs.ini from the
     * copy in memory, which a game may have changed on disk since. Focus
     * back, or a game closed, is also when the player may have given the
     * hall another song in Chipforge, or played a daily. */
    if (PrefsWatch_Update(&a->prefsWatch, in.focused, gameExited, !woke && Keymap_Pressed(KA_FULLSCREEN, &in))) {
        CheckHallSong(a);
        a->deskStale = true;
    }
    if (!woke) Win_Update(); /* an F11 that woke the hall does nothing else either */

    UIState before = a->ui;
    bool dialogBefore = a->dialog.open, typingBefore = Typing(a);
    if (!woke && !a->attract.on) Update(a, &in, dt, now); /* touring, nothing but waking reacts */
    RefreshDesk(a);
    /* A hold belongs to the screen (or box, or text field) it began on; a box
     * that closes leaves the screen under it as it was, minus any half-made
     * click. */
    if (a->ui != before || a->dialog.open != dialogBefore || Typing(a) != typingBefore) Input_DropHolds(&a->input);
    if (dialogBefore && !a->dialog.open && a->ui == before) Kit_Enter(&a->kit[a->ui], a->kit[a->ui].focus);
    if (a->quit) return;
    TickQuiet(a, &in, dt);
    TickMusic(a, &in);
    Draw(a, a->quiet ? 0.0f : dt, in.lastDevice);
    ShowPointer(a, &in);
}

int main(void) {
    App *a = &sApp;
    char *assetsDir = a->assetsDir;
    ResolveAssetsDir(assetsDir, sizeof(a->assetsDir));

    Manifest_Load(&a->manifest, assetsDir);
    Stats_Load(&a->stats);
    Profiles_Load(&a->profiles);
    a->adminUnlocked = AdminLock_IsUnlocked();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ghost Launcher");
    Win_Init(WINDOW_WIDTH, WINDOW_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(HALL_FPS);
    a->fps = HALL_FPS;

    Render_Init(assetsDir);
    LauncherIni_Load(&a->ini);
    a->hallSounds = Settings_HallSounds(&a->ini);
    a->buttonHints = Settings_ButtonHints(&a->ini);
    a->hallMusic = HallMusic_Enabled(&a->ini);
    a->attractSetting = Attract_Setting(&a->ini);
    Migrate_GemdelvePlaytime(&a->ini, &a->stats, &a->manifest);
    LoadSounds(a, assetsDir);
    Music_Init(HALL_MUSIC_SLUG); /* after the audio device opens; silent without one */
    HallMusic_Song(&a->hallSong);
    HallMusic_Tune(&a->tune);
    a->tuneSince = GetTime();

    /* Refresh the online boards in the background while the player browses. */
    Launch_SyncDetached();

    double launcherStartTime = GetTime();
    App_Start(a);
    Input_Init(&a->input);
    PrefsWatch_Init(&a->prefsWatch, IsWindowFocused());

    /* raylib only moves key state forward inside EndDrawing, so a pass that
     * changed screens without presenting a frame would let the same key or
     * click act again on the next screen. Frame() always draws; should a
     * later change break that, poll here so the press is retired anyway
     * (the same guard as Chipforge's). */
    unsigned long framesSeen = Win_FrameCount();
    bool firstPass = true;
    while (!a->quit) {
        if (!firstPass && Win_FrameCount() == framesSeen) PollInputEvents();
        firstPass = false;
        framesSeen = Win_FrameCount();
        if (WindowShouldClose()) break;
        Frame(a);
    }

    double closeTime = GetTime();
    Launch_CreditStillRunning(closeTime, &a->stats);
    Stats_AddLauncherTime(&a->stats, closeTime - launcherStartTime);
    Stats_Save(&a->stats);

    Music_Shutdown(); /* its stream before the device goes */
    if (a->soundsLoaded) {
        UnloadSound(a->coinSound);
        UnloadSound(a->moveSound);
    }
    if (IsAudioDeviceReady()) CloseAudioDevice();
    Render_Shutdown();
    Win_Shutdown();
    CloseWindow();
    /* Restart the launcher (Update now, done): everything the quit path
     * saves is saved; now the newly installed launcher takes this process. */
    if (a->restart && !Launch_RestartLauncher(BUILD_BIN_DIR "/ghost-launcher")) {
        fprintf(stderr, "ghost-launcher: could not start the new launcher; start it again yourself.\n");
        return 1;
    }
    return 0;
}
