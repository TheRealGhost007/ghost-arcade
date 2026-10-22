#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "raylib.h"
#include "winscale.h"
#include "game.h"
#include "journey.h"
#include "render.h"
#include "screens.h"
#include "input.h"
#include "persist.h"
#include "teams.h"
#include "netui.h"
#include "crews.h"
#include "sounds.h"
#include "sfx.h"
#include "music.h"
#include "prefs.h"
#include "ghostlink.h"
#include "runlog.h"
#include "achievements.h"
#include "daily.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Pond Kings: the screens, the players' devices, and the game run at a
 * fixed 60 ticks a second. Everything that decides a game is in game.c;
 * this file feeds it button presses, shows what happened and keeps the
 * records (the Journey, the boards, the launcher's run log and trophies). */

#define SLUG "pondkings"
#define CATALOG_NAME "Pond Kings" /* the name in Ghost Launcher (per-game player names) */

static const char *const kSfxFiles[SFX_COUNT] = {
    "select.wav", "confirm.wav", "back.wav", "pass.wav", "shot.wav", "onetimer.wav", "post.wav", "net.wav", "goal.wav", "cheer.wav",
    "bank.wav", "bump.wav", "dig.wav", "check.wav", "poke.wav", "stop.wav", "drop.wav", "whistle.wav", "shovel.wav", "false.wav",
};

typedef enum {
    APP_TITLE, APP_MENU, APP_JOIN, APP_SETUP, APP_SO_SETUP, APP_DAILY, APP_HUB, APP_SIGN, APP_GAME, APP_PAUSE,
    APP_RESULTS, APP_JOURNEY_RESULT, APP_JOURNEY_END, APP_SO_RESULT, APP_DAILY_RESULT, APP_HOWTO, APP_SETTINGS, APP_KEYS, APP_NET
} AppState;

typedef enum { PLAY_QUICK, PLAY_JOURNEY, PLAY_SHOOTOUT, PLAY_DAILY, PLAY_ONLINE } PlayKind;

#define REPLAY_LEN 180 /* three seconds of states for the arena's big screen */

typedef struct {
    AppState state;
    PlayKind play;
    int menuSel, setupSel, soSel, dailySel, hubSel, signStep, signTheirs, signSel, settingsSel, keysRow, keysCol, pauseSel, resultSel, howtoPage;
    bool hubConfirm, keysWaiting;
    JoinSlot slots[MAX_PLAYERS];
    Settings settings;
    Journey journey;
    bool haveJourney;
    JourneyResult jr;
    DailyInfo daily;
    RunScore runScore;
    char username[GHOSTLINK_NAME_LEN];
    double acc;
    bool quit;
    int stopCool, shovelCool, overTicks;
    int rivals;                 /* side 1's colours and names */
    const char *rivalName;      /* a friend's crew, by name */
    CrewKid rivalKids[TEAM_SIZE];
    bool recorded;              /* this game's result is in the books */
} App;

static App sApp;
static Game sGame;
static Game sHistory[REPLAY_LEN], sReplay[REPLAY_LEN];
static int sHistHead = 0, sHistCount = 0, sReplayCount = 0;

/* ------------------------------------------------------------ helpers */

static void ResolveAssetsDir(char *buf, size_t size) {
    snprintf(buf, size, "%sassets", GetApplicationDirectory());
    if (DirectoryExists(buf)) return;
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) snprintf(buf, size, "%s/pondkings/assets", xdg);
    else {
        const char *home = getenv("HOME");
        snprintf(buf, size, "%s/.local/share/pondkings/assets", home ? home : "");
    }
    if (DirectoryExists(buf)) return;
    if (DirectoryExists("assets")) snprintf(buf, size, "assets");
}

static void ApplySound(const Settings *s) {
    Sfx_SetEnabled(s->sound);
    Sfx_SetVolume(s->sfxVolume);
    Music_SetEnabled(s->sound);
    Music_SetVolume(Prefs_Get()->musicVolume);
}

static void ApplyKeys(const Settings *s) {
    Input_SetKeys(0, s->keys[0]);
    Input_SetKeys(1, s->keys[1]);
}

static int JoinedCount(const App *a) {
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) n += a->slots[p].joined ? 1 : 0;
    return n;
}
static int SlotOfDevice(const App *a, int dev) {
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->slots[p].joined && a->slots[p].device == dev) return p;
    return -1;
}
static int FreeSlot(const App *a) {
    for (int p = 0; p < MAX_PLAYERS; p++) if (!a->slots[p].joined) return p;
    return -1;
}
static bool KeyboardAlone(const App *a) {
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->slots[p].joined && !Input_IsPad(a->slots[p].device)) n++;
    return n <= 1;
}

static uint64_t Seed(void) {
    static uint64_t counter = 0;
    uint64_t t = (uint64_t)time(NULL);
    return (t * 2654435761u) ^ (uint64_t)(uintptr_t)&counter ^ (++counter * 0x9E3779B97F4A7C15ull);
}

static void Today(int *y, int *m, int *d) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    *y = tm.tm_year + 1900;
    *m = tm.tm_mon + 1;
    *d = tm.tm_mday;
}

static void PrepareDaily(App *a) {
    DailyInfo *d = &a->daily;
    memset(d, 0, sizeof(*d));
    int y, m, day;
    Today(&y, &m, &day);
    snprintf(d->date, sizeof(d->date), "%04u-%02u-%02u", (unsigned)y % 10000u, (unsigned)m % 100u, (unsigned)day % 100u);
    if (Daily_Active()) {
        d->seed = Daily_Seed();
        snprintf(d->mode, sizeof(d->mode), "%s", Daily_Mode());
    } else {
        d->seed = Daily_SeedForDate(y, m, day);
        Daily_ModeForDate(y, m, day, d->mode, sizeof(d->mode));
    }
    d->rival = (int)(d->seed % RIVAL_COUNT);
    d->twist = 1 + (int)((d->seed / 7) % (TWIST_COUNT - 1));
    GhostScore rows[GHOSTLINK_TOP_N];
    int n = GhostLink_LoadScores(SLUG, d->mode, rows, GHOSTLINK_TOP_N);
    d->best = n > 0 ? rows[0].score : 0;
}

/* A finished run goes in the books: the launcher's run log, trophies, and
 * the local board (which ghost-sync shares if the player said yes). */
static void RecordRun(App *a, const char *mode, long score, int level, bool forTrophies) {
    RunScore *rs = &a->runScore;
    memset(rs, 0, sizeof(*rs));
    rs->score = score;
    RunLog_Append(SLUG, mode, score, level);
    if (Ach_CheckRun(SLUG, forTrophies ? score : 0, forTrophies ? level : 0) > 0) {
        char t[48];
        if (Ach_PopToast(t, sizeof(t))) snprintf(rs->toast, sizeof(rs->toast), "%s", t);
        while (Ach_PopToast(t, sizeof(t))) {}
        Sfx_Jingle(JINGLE_LEVEL_UP);
    }
    if (score <= 0) return;
    char date[GHOSTLINK_DATE_LEN];
    int y, m, d;
    Today(&y, &m, &d);
    snprintf(date, sizeof(date), "%04u-%02u-%02u", (unsigned)y % 10000u, (unsigned)m % 100u, (unsigned)d % 100u);
    rs->rank = GhostLink_SubmitScore(SLUG, mode, a->username, score, date);
    rs->newBest = rs->rank == 1;
    GhostLink_TriggerSync();
}

/* ------------------------------------------------------------ starting games */

static void ClearRivalNames(App *a) {
    a->rivalName = NULL;
    memset(a->rivalKids, 0, sizeof(a->rivalKids));
}

static void BeginGame(App *a, const GameSetup *s, PlayKind kind) {
    Game_Init(&sGame, s);
    Render_NewGame();
    a->play = kind;
    a->acc = 0.0;
    a->overTicks = 0;
    a->recorded = false;
    sHistCount = sHistHead = sReplayCount = 0;
    a->state = APP_GAME;
    Sfx_Play(SFX_WHISTLE);
}

static void StartQuick(App *a) {
    GameSetup s;
    Game_DefaultSetup(&s, Seed());
    s.venue = (uint8_t)a->settings.venue;
    s.periodSeconds = a->settings.periodMinutes * 60;
    s.teamLevel[0] = s.teamLevel[1] = (uint8_t)a->settings.difficulty;
    for (int p = 0; p < MAX_PLAYERS; p++) s.slotTeam[p] = a->slots[p].joined ? (int8_t)a->slots[p].team : -1;
    const TeamStyle *r = Team_Rival(a->settings.rivals);
    for (int k = 0; k < TEAM_SIZE; k++) s.trait[1][k] = r->traits[k];
    s.goalieStyle[1] = r->goalieStyle;
    a->rivals = a->settings.rivals;
    ClearRivalNames(a);
    BeginGame(a, &s, PLAY_QUICK);
}

static void StartJourneyGame(App *a) {
    GameSetup s;
    int8_t slotTeam[MAX_PLAYERS];
    for (int p = 0; p < MAX_PLAYERS; p++) slotTeam[p] = a->slots[p].joined ? 0 : -1;
    Journey_Setup(&a->journey, &s, slotTeam, Seed(), a->settings.periodMinutes);
    int opp = Journey_Opponent(&a->journey);
    ClearRivalNames(a);
    if (opp == JOURNEY_FRIEND) {
        a->rivals = 4; /* a friend's crew wears the Toque Squad's colours */
        a->rivalName = a->journey.friendCrew.name;
        for (int k = 0; k < TEAM_SIZE; k++) a->rivalKids[k] = a->journey.friendCrew.kids[k];
    } else {
        a->rivals = opp;
    }
    BeginGame(a, &s, PLAY_JOURNEY);
}

static void StartShootout(App *a) {
    GameSetup s;
    Game_DefaultSetup(&s, Seed());
    s.mode = MODE_SHOOTOUT;
    s.venue = (uint8_t)a->settings.soVenue;
    for (int p = 0; p < MAX_PLAYERS; p++) s.slotTeam[p] = p == 0 ? 0 : -1;
    a->rivals = (int)(Seed() % RIVAL_COUNT);
    s.goalieStyle[1] = Team_Rival(a->rivals)->goalieStyle;
    ClearRivalNames(a);
    BeginGame(a, &s, PLAY_SHOOTOUT);
}

static void StartDaily(App *a) {
    GameSetup s;
    Game_DefaultSetup(&s, a->daily.seed);
    s.venue = VENUE_POND;
    s.twist = (uint8_t)a->daily.twist;
    s.periodSeconds = 60;
    s.periods = 2;
    for (int p = 0; p < MAX_PLAYERS; p++) s.slotTeam[p] = p == 0 ? 0 : -1;
    const TeamStyle *r = Team_Rival(a->daily.rival);
    for (int k = 0; k < TEAM_SIZE; k++) s.trait[1][k] = r->traits[k];
    a->rivals = a->daily.rival;
    ClearRivalNames(a);
    BeginGame(a, &s, PLAY_DAILY);
}

/* One-player modes: whoever picked it plays, as P1. */
static void SoloSlot(App *a) {
    memset(a->slots, 0, sizeof(a->slots));
    a->slots[0] = (JoinSlot){true, Input_MenuDevice(), 0};
}

/* ------------------------------------------------------------ sounds */

static void StepSounds(App *a, const Game *g) {
    uint32_t ev = g->events;
    if (ev & EV_PASS) Sfx_PlayVaried(SFX_PASS, 0.06f);
    if (ev & EV_SHOT) Sfx_PlayVaried((ev & (EV_ONE_TIMER | EV_SLAPSHOT)) ? SFX_ONE_TIMER : SFX_SHOT, 0.05f);
    if (ev & EV_POST) Sfx_PlayVaried(SFX_POST, 0.03f);
    if (ev & (EV_NET | EV_BLOCK | EV_SAVE)) Sfx_PlayVaried(SFX_NET, 0.08f);
    if (ev & EV_BOARDS) Sfx_PlayPitched(SFX_CHECK, 1.25f);
    if (ev & EV_GOAL) { Sfx_Play(SFX_GOAL); Sfx_Play(SFX_CHEER); }
    if (ev & EV_BANK) Sfx_PlayVaried(SFX_BANK, 0.05f);
    if (ev & EV_BANK_SOFT) Sfx_PlayVaried(SFX_BUMP, 0.08f);
    if (ev & EV_DIG_OUT) Sfx_Play(SFX_DIG);
    if (ev & EV_CHECK) Sfx_PlayVaried(SFX_CHECK, 0.05f);
    if (ev & EV_POKE) Sfx_PlayVaried(SFX_POKE, 0.08f);
    if (ev & EV_DROP) Sfx_Play(SFX_DROP);
    if (ev & EV_FALSE_START) Sfx_Play(SFX_FALSE);
    if (ev & (EV_PERIOD_END | EV_GAME_OVER | EV_FREEZE)) Sfx_Play(SFX_WHISTLE);
    if (ev & EV_SO_MISS) Sfx_Play(SFX_FALSE);
    if (a->stopCool > 0) a->stopCool--;
    if ((ev & EV_STOP) && a->stopCool == 0) { Sfx_PlayVaried(SFX_STOP, 0.1f); a->stopCool = 14; }
    if (g->phase == PH_BREAK && g->phaseTicks < 220) {
        if (a->shovelCool > 0) a->shovelCool--;
        else { Sfx_PlayVaried(SFX_SHOVEL, 0.1f); a->shovelCool = 55; }
    }
}

static void OnTick(const Game *g, void *ctx) {
    Render_OnStep(g);
    StepSounds((App *)ctx, g);
}

static const Game *ShownGame(const App *a) { return a->play == PLAY_ONLINE ? NetUi_Game() : &sGame; }

static void MusicFor(const App *a) {
    if (a->state == APP_PAUSE && a->play != PLAY_ONLINE) { Music_SetMode(MUSIC_PAUSED); return; }
    if (a->state != APP_GAME && a->state != APP_PAUSE) { Music_SetMode(MUSIC_MENU); return; }
    const Game *g = ShownGame(a);
    if (!g) { Music_SetMode(MUSIC_MENU); return; }
    Music_SetMode(MUSIC_PLAY);
    float intensity = 0.45f;
    if (abs(g->score[0] - g->score[1]) <= 1) intensity += 0.2f;
    if (Game_SuddenDeath(g) || (g->period >= g->setup.periods && Game_ClockSeconds(g) <= 30 && g->setup.mode == MODE_MATCH)) intensity += 0.3f;
    if (g->setup.venue == VENUE_ARENA) intensity += 0.1f;
    if (g->phase == PH_BREAK) intensity = 0.3f;
    Music_SetIntensity(intensity);
}

/* ------------------------------------------------------------ menus */

static void Move(int *sel, int n, const MenuPress *m) {
    if (m->up) { *sel = (*sel + n - 1) % n; Sfx_Play(SFX_SELECT); }
    if (m->down) { *sel = (*sel + 1) % n; Sfx_Play(SFX_SELECT); }
}

static void OpenJoin(App *a, PlayKind kind) {
    a->play = kind;
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->slots[p].joined && !Input_DeviceReady(a->slots[p].device)) a->slots[p].joined = false;
    a->state = APP_JOIN;
}

static void OpenHub(App *a) {
    a->haveJourney = Journey_Load(&a->journey);
    a->hubSel = 0;
    a->hubConfirm = false;
    a->state = APP_HUB;
}

static void UpdateTitle(App *a) {
    MenuPress m = Input_MenuAny();
    if (m.confirm || m.start || m.join) { Sfx_Play(SFX_CONFIRM); a->state = APP_MENU; }
}

static void UpdateMenu(App *a) {
    MenuPress m = Input_MenuAny();
    Move(&a->menuSel, MENU_ITEMS, &m);
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_TITLE; return; }
    if (!m.confirm) return;
    Sfx_Play(SFX_CONFIRM);
    switch (a->menuSel) {
        case MI_JOURNEY: OpenJoin(a, PLAY_JOURNEY); break;
        case MI_QUICK: OpenJoin(a, PLAY_QUICK); break;
        case MI_SHOOTOUT: SoloSlot(a); a->soSel = SO_SETUP_ROWS - 1; a->state = APP_SO_SETUP; break;
        case MI_DAILY: SoloSlot(a); PrepareDaily(a); a->dailySel = 0; a->state = APP_DAILY; break;
        case MI_ONLINE: SoloSlot(a); NetUi_OpenOnline(&a->settings, a->slots); a->state = APP_NET; break;
        case MI_CREWS: {
            Journey j;
            bool have = Journey_Load(&j);
            NetUi_OpenCrews(&a->settings, have ? &j : NULL);
            a->state = APP_NET;
            break;
        }
        case MI_HOWTO: a->howtoPage = 0; a->state = APP_HOWTO; break;
        case MI_SETTINGS: a->settingsSel = 0; a->state = APP_SETTINGS; break;
        default: a->quit = true; break;
    }
}

static void UpdateJoin(App *a) {
    bool teams = a->play == PLAY_QUICK;
    bool escUsed = false;
    for (int d = 0; d < DEV_COUNT; d++) {
        if (!Input_DeviceReady(d)) continue;
        MenuPress m = Input_Menu(d);
        int slot = SlotOfDevice(a, d);
        bool keyboard = !Input_IsPad(d);
        if (slot >= 0) {
            if (teams && (m.left || m.right)) { a->slots[slot].team = 1 - a->slots[slot].team; Sfx_Play(SFX_SELECT); }
            if (m.back && !(keyboard && escUsed)) {
                a->slots[slot].joined = false;
                Sfx_Play(SFX_BACK);
                if (keyboard) escUsed = true;
            }
        } else if (m.join) {
            int free = FreeSlot(a);
            if (free >= 0) { a->slots[free] = (JoinSlot){true, d, 0}; Sfx_Play(SFX_CONFIRM); }
        }
    }
    MenuPress any = Input_MenuAny();
    if (any.back && !escUsed && JoinedCount(a) == 0) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
    if (any.start && JoinedCount(a) > 0) {
        Sfx_Play(SFX_CONFIRM);
        if (a->play == PLAY_JOURNEY) OpenHub(a);
        else { a->setupSel = SETUP_ROWS - 1; a->state = APP_SETUP; }
    }
}

static void UpdateSetup(App *a) {
    MenuPress m = Input_MenuAny();
    Settings *s = &a->settings;
    Move(&a->setupSel, SETUP_ROWS, &m);
    int step = m.left ? -1 : m.right ? 1 : 0;
    if (step) {
        if (a->setupSel == 0) s->venue = (s->venue + VENUE_COUNT + step) % VENUE_COUNT;
        if (a->setupSel == 1) s->rivals = (s->rivals + RIVAL_COUNT + step) % RIVAL_COUNT;
        if (a->setupSel == 2) s->periodMinutes = 1 + (s->periodMinutes - 1 + 3 + step) % 3;
        if (a->setupSel == 3) s->difficulty = (s->difficulty + DIFF_COUNT + step) % DIFF_COUNT;
        if (a->setupSel < 4) { Settings_Save(s); Sfx_Play(SFX_SELECT); }
    }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_JOIN; return; }
    if ((m.confirm && a->setupSel == 4) || m.start) StartQuick(a);
}

static void UpdateShootoutSetup(App *a) {
    MenuPress m = Input_MenuAny();
    Move(&a->soSel, SO_SETUP_ROWS, &m);
    if ((m.left || m.right) && a->soSel == 0) {
        a->settings.soVenue = a->settings.soVenue == VENUE_RINK ? VENUE_ARENA : VENUE_RINK;
        Settings_Save(&a->settings);
        Sfx_Play(SFX_SELECT);
    }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
    if ((m.confirm && a->soSel == 1) || m.start) { SoloSlot(a); StartShootout(a); }
}

static void UpdateDaily(App *a) {
    MenuPress m = Input_MenuAny();
    if (m.left || m.right) { a->dailySel = 1 - a->dailySel; Sfx_Play(SFX_SELECT); }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
    if (m.confirm || m.start) {
        if (a->dailySel == 1 && !m.start) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
        SoloSlot(a);
        StartDaily(a);
    }
}

static void NewJourney(App *a) {
    CrewCard friends[8];
    int n = NetUi_FriendCrews(friends, 8);
    const CrewCard *pick = n > 0 ? &friends[(int)(Seed() % (uint64_t)n)] : NULL;
    Journey_New(&a->journey, Seed(), pick);
    a->haveJourney = true;
    Journey_Save(&a->journey);
}

static void UpdateHub(App *a) {
    MenuPress m = Input_MenuAny();
    Journey *j = &a->journey;
    bool running = a->haveJourney && j->active;
    if (!running) {
        Move(&a->hubSel, 2, &m);
        if (m.back || (m.confirm && a->hubSel == 1)) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
        if (m.confirm || m.start) { Sfx_Play(SFX_CONFIRM); NewJourney(a); a->hubSel = 0; }
        return;
    }
    if (m.left) { a->hubSel = (a->hubSel + (a->hubConfirm ? 1 : HUB_ITEMS - 1)) % (a->hubConfirm ? 2 : HUB_ITEMS); Sfx_Play(SFX_SELECT); }
    if (m.right) { a->hubSel = (a->hubSel + 1) % (a->hubConfirm ? 2 : HUB_ITEMS); Sfx_Play(SFX_SELECT); }
    if (m.back) {
        Sfx_Play(SFX_BACK);
        if (a->hubConfirm) { a->hubConfirm = false; a->hubSel = 1; }
        else a->state = APP_MENU;
        return;
    }
    if (!m.confirm && !m.start) return;
    Sfx_Play(SFX_CONFIRM);
    if (a->hubConfirm) {
        if (a->hubSel == 0) { Journey_Delete(); NewJourney(a); }
        a->hubConfirm = false;
        a->hubSel = 0;
        return;
    }
    if (a->hubSel == 0 || m.start) StartJourneyGame(a);
    else if (a->hubSel == 1) { a->hubConfirm = true; a->hubSel = 1; }
    else a->state = APP_MENU;
}

static void UpdateSign(App *a) {
    MenuPress m = Input_MenuAny();
    Move(&a->signSel, TEAM_SIZE + 1, &m);
    if (!m.confirm) return;
    Sfx_Play(SFX_CONFIRM);
    if (a->signStep == 0) {
        if (a->signSel == TEAM_SIZE) { Journey_Pass(&a->journey); Journey_Save(&a->journey); OpenHub(a); return; }
        a->signTheirs = a->signSel;
        a->signStep = 1;
        a->signSel = 0;
        return;
    }
    if (a->signSel == TEAM_SIZE) { a->signStep = 0; a->signSel = a->signTheirs; return; }
    Journey_Sign(&a->journey, a->signTheirs, a->signSel);
    Journey_Save(&a->journey);
    OpenHub(a);
}

static void UpdateSettings(App *a) {
    MenuPress m = Input_MenuAny();
    Settings *s = &a->settings;
    Move(&a->settingsSel, SETTINGS_ROWS, &m);
    int step = m.left ? -1 : m.right ? 1 : 0;
    bool toggle = step != 0 || m.confirm;
    bool changed = false;
    switch (a->settingsSel) {
        case 0:
            if (step) { s->sfxVolume += step * 10; s->sfxVolume = s->sfxVolume < 0 ? 0 : s->sfxVolume > 100 ? 100 : s->sfxVolume; changed = true; }
            break;
        case 1:
            if (step) { Prefs_HandleRow(0, step < 0, step > 0, false); changed = true; }
            break;
        case 2:
            if (toggle) { s->sound = !s->sound; changed = true; }
            break;
        case 3:
            if (toggle) { Prefs_Get()->fullscreen = !Prefs_Get()->fullscreen; Prefs_Save(); changed = true; }
            break;
        case 4:
            if (m.confirm) { Sfx_Play(SFX_CONFIRM); a->keysRow = 0; a->keysCol = 0; a->keysWaiting = false; a->state = APP_KEYS; return; }
            break;
        default:
            if (m.confirm) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
            break;
    }
    if (changed) { Settings_Save(s); ApplySound(s); Sfx_Play(SFX_SELECT); }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_MENU; }
}

static void UpdateKeys(App *a) {
    Settings *s = &a->settings;
    if (a->keysWaiting) {
        if (IsKeyPressed(KEY_ESCAPE)) { a->keysWaiting = false; Sfx_Play(SFX_BACK); return; }
        int k = Input_KeyPressedNow();
        if (k <= 0) return;
        if (Input_KeyReserved(k)) { Sfx_Play(SFX_FALSE); return; }
        /* a key used elsewhere swaps with the one it replaces */
        int old = s->keys[a->keysCol][a->keysRow];
        for (int h = 0; h < 2; h++)
            for (int act = 0; act < KA_COUNT; act++) if (s->keys[h][act] == k) s->keys[h][act] = old;
        s->keys[a->keysCol][a->keysRow] = k;
        a->keysWaiting = false;
        ApplyKeys(s);
        Settings_Save(s);
        Sfx_Play(SFX_CONFIRM);
        return;
    }
    MenuPress m = Input_MenuAny();
    if (m.up) { a->keysRow = (a->keysRow + KEYS_ROWS - 1) % KEYS_ROWS; Sfx_Play(SFX_SELECT); }
    if (m.down) { a->keysRow = (a->keysRow + 1) % KEYS_ROWS; Sfx_Play(SFX_SELECT); }
    if ((m.left || m.right) && a->keysRow < KA_COUNT) { a->keysCol = 1 - a->keysCol; Sfx_Play(SFX_SELECT); }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_SETTINGS; return; }
    if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        if (a->keysRow == KA_COUNT) {
            Input_DefaultKeys(0, s->keys[0]);
            Input_DefaultKeys(1, s->keys[1]);
            ApplyKeys(s);
            Settings_Save(s);
            Sfx_Play(SFX_CONFIRM);
        } else {
            a->keysWaiting = true;
            (void)GetKeyPressed(); /* forget the Enter that got us here */
            Sfx_Play(SFX_SELECT);
        }
    }
}

/* ------------------------------------------------------------ playing */

static void DropInOut(App *a) {
    if (a->play == PLAY_SHOOTOUT || a->play == PLAY_DAILY || a->play == PLAY_ONLINE) return;
    for (int d = DEV_PAD0; d < DEV_COUNT; d++) {
        if (!Input_DeviceReady(d)) continue;
        int slot = SlotOfDevice(a, d);
        if (slot < 0) {
            if (!Input_Menu(d).join) continue;
            int free = FreeSlot(a);
            if (free < 0) continue;
            int team = Game_JoinPlayer(&sGame, free, 0) ? 0 : (a->play == PLAY_QUICK && Game_JoinPlayer(&sGame, free, 1)) ? 1 : -1;
            if (team < 0) continue;
            a->slots[free] = (JoinSlot){true, d, team};
            Sfx_Play(SFX_CONFIRM);
        } else if (Input_HeldLeave(d)) {
            Game_LeavePlayer(&sGame, slot);
            a->slots[slot].joined = false;
            Sfx_Play(SFX_BACK);
        }
    }
}

static void RememberState(const Game *g) {
    sHistory[sHistHead] = *g;
    sHistHead = (sHistHead + 1) % REPLAY_LEN;
    if (sHistCount < REPLAY_LEN) sHistCount++;
}

static void TakeReplay(void) {
    sReplayCount = sHistCount;
    int start = (sHistHead - sHistCount + REPLAY_LEN) % REPLAY_LEN;
    for (int k = 0; k < sHistCount; k++) sReplay[k] = sHistory[(start + k) % REPLAY_LEN];
}

/* The game just ended: the books, then the right results screen. */
/* How this machine's players play, for the rival crews: their habits from
 * a finished game added to the running total. */
static void NoteStyle(const Game *g, uint8_t slotMask) {
    PlayStyle ps;
    Crews_LoadStyle(&ps);
    bool any = false;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (((slotMask >> p) & 1) && g->setup.slotTeam[p] >= 0) {
            Crews_AddGame(&ps, &g->habits[p]);
            any = true;
        }
    if (any) Crews_SaveStyle(&ps);
}

static uint8_t JoinedMask(const App *a) {
    uint8_t m = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (a->slots[p].joined) m |= (uint8_t)(1u << p);
    return m;
}

static void FinishGame(App *a) {
    const Game *g = &sGame;
    a->resultSel = 0;
    switch (a->play) {
        case PLAY_JOURNEY: {
            Journey_Result(&a->journey, g, &a->jr);
            Journey_Save(&a->journey);
            NoteStyle(g, JoinedMask(a));
            RunLog_Append(SLUG, "journey-game", a->jr.points, Journey_Level(&a->journey));
            a->state = APP_JOURNEY_RESULT;
            break;
        }
        case PLAY_SHOOTOUT:
            RecordRun(a, "shootout", g->score[0], 0, false);
            a->state = APP_SO_RESULT;
            break;
        case PLAY_DAILY:
            NoteStyle(g, JoinedMask(a));
            RecordRun(a, a->daily.mode, Journey_GamePoints(g, 0), 0, false);
            a->state = APP_DAILY_RESULT;
            break;
        default: {
            /* a game played counts for the first and the Regular trophies, and for the launcher's stats */
            NoteStyle(g, JoinedMask(a));
            RunLog_Append(SLUG, "quick", g->score[0], 0);
            Ach_CheckRun(SLUG, 0, 0);
            char t[48];
            while (Ach_PopToast(t, sizeof(t))) {}
            a->state = APP_RESULTS;
            break;
        }
    }
}

static void UpdateGame(App *a, float dt) {
    bool pause = IsKeyPressed(KEY_ESCAPE);
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->slots[p].joined && Input_IsPad(a->slots[p].device) && Input_Menu(a->slots[p].device).start) pause = true;
    const Game *shown = ShownGame(a);
    if (pause && shown && shown->phase != PH_OVER) { Sfx_Play(SFX_BACK); a->pauseSel = 0; a->state = APP_PAUSE; return; }
    DropInOut(a);

    PadInput in[MAX_PLAYERS];
    memset(in, 0, sizeof(in));
    bool alone = KeyboardAlone(a);
    if (a->play == PLAY_ONLINE) {
        int n = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!a->slots[p].joined) continue;
            int slot = NetUi_SlotFor(n++);
            if (slot >= 0 && slot < MAX_PLAYERS) in[slot] = Input_Read(a->slots[p].device, alone);
        }
        if (!NetUi_StepGame(in, dt, OnTick, a)) { a->state = APP_NET; return; }
        const Game *g = NetUi_Game();
        /* the result shown is the one both machines agree on */
        if (g && g->phase == PH_OVER && NetUi_Settled()) {
            if (!a->recorded) {
                a->recorded = true;
                int slot = NetUi_SlotFor(0);
                int team = slot >= 0 ? g->setup.slotTeam[slot] : 0;
                RunLog_Append(SLUG, "online", g->score[team < 0 ? 0 : team], 0);
                Ach_CheckRun(SLUG, 0, 0);
                char t[48];
                while (Ach_PopToast(t, sizeof(t))) {}
                if (slot >= 0) NoteStyle(g, (uint8_t)(1u << slot));
            }
            if (++a->overTicks > 90) { a->resultSel = 0; a->state = APP_RESULTS; }
        }
        return;
    }
    for (int p = 0; p < MAX_PLAYERS; p++) if (a->slots[p].joined) in[p] = Input_Read(a->slots[p].device, alone);

    a->acc += dt > 0.25f ? 0.25f : dt;
    const double tick = 1.0 / TICK_HZ;
    while (a->acc >= tick) {
        Game_Step(&sGame, in);
        if (sGame.setup.venue == VENUE_ARENA) {
            if (sGame.events & EV_GOAL) TakeReplay();
            else if (sGame.phase == PH_PLAY || sGame.phase == PH_FACEOFF) RememberState(&sGame);
        }
        OnTick(&sGame, a);
        a->acc -= tick;
        if (sGame.phase == PH_OVER) a->overTicks++;
    }
    if (sGame.phase == PH_OVER && a->overTicks > 45 && !a->recorded) {
        a->recorded = true;
        FinishGame(a);
    }
}

static void UpdatePause(App *a) {
    MenuPress m = Input_MenuAny();
    bool online = a->play == PLAY_ONLINE;
    if (online) {
        /* the game can't stop for one player: keep it running underneath */
        PadInput none[MAX_PLAYERS];
        memset(none, 0, sizeof(none));
        if (!NetUi_StepGame(none, GetFrameTime(), OnTick, a)) { a->state = APP_NET; return; }
    }
    DropInOut(a);
    Move(&a->pauseSel, PAUSE_ITEMS, &m);
    if (online && a->pauseSel == 1) a->pauseSel = m.up ? 0 : 2;
    if (m.back || m.start) { Sfx_Play(SFX_CONFIRM); a->state = APP_GAME; return; }
    if (!m.confirm) return;
    Sfx_Play(SFX_CONFIRM);
    if (a->pauseSel == 0) { a->state = APP_GAME; return; }
    if (online) { NetUi_Leave(); a->state = APP_MENU; return; }
    if (a->pauseSel == 1) {
        switch (a->play) {
            case PLAY_JOURNEY: StartJourneyGame(a); break; /* a restart isn't a loss */
            case PLAY_SHOOTOUT: StartShootout(a); break;
            case PLAY_DAILY: StartDaily(a); break;
            default: StartQuick(a); break;
        }
        return;
    }
    a->state = a->play == PLAY_JOURNEY ? APP_HUB : APP_MENU;
    if (a->play == PLAY_JOURNEY) OpenHub(a);
}

static void UpdateResults(App *a) {
    MenuPress m = Input_MenuAny();
    if (m.left || m.right) { a->resultSel = 1 - a->resultSel; Sfx_Play(SFX_SELECT); }
    if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_MENU; return; }
    if (!m.confirm && !m.start) return;
    Sfx_Play(SFX_CONFIRM);
    if (a->play == PLAY_ONLINE) { NetUi_Leave(); a->state = APP_MENU; return; }
    if (a->resultSel == 1) { a->state = APP_MENU; return; }
    if (a->state == APP_SO_RESULT) StartShootout(a);
    else if (a->state == APP_DAILY_RESULT) { PrepareDaily(a); StartDaily(a); }
    else StartQuick(a);
}

static void UpdateJourneyResult(App *a) {
    MenuPress m = Input_MenuAny();
    if (!m.confirm && !m.start && !m.back) return;
    Sfx_Play(SFX_CONFIRM);
    Journey *j = &a->journey;
    if (j->finished) {
        RecordRun(a, "journey", j->points, Journey_Level(j), true);
        Journey_Save(j);
        a->state = APP_JOURNEY_END;
        return;
    }
    if (j->canSign) { a->signStep = 0; a->signSel = 0; a->state = APP_SIGN; return; }
    OpenHub(a);
}

/* ------------------------------------------------------------ drawing */

static void FillInfo(const App *a, RenderInfo *ri) {
    memset(ri, 0, sizeof(*ri));
    ri->rivals = a->play == PLAY_ONLINE ? NetUi_Rivals() : a->rivals;
    ri->pingMs = a->play == PLAY_ONLINE ? NetUi_PingMs() : -1;
    ri->waiting = a->play == PLAY_ONLINE && NetUi_Waiting();
    bool alone = KeyboardAlone(a);
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!a->slots[p].joined) continue;
        int slot = a->play == PLAY_ONLINE ? NetUi_SlotFor(n++) : p;
        if (slot < 0 || slot >= MAX_PLAYERS) continue;
        ri->passKey[slot] = Input_ButtonName(a->slots[p].device, BTN_PASS, alone);
        ri->shootKey[slot] = Input_ButtonName(a->slots[p].device, BTN_SHOOT, alone);
    }
    if (a->play == PLAY_JOURNEY)
        for (int k = 0; k < TEAM_SIZE; k++) ri->crewNames[k] = a->journey.crew[k].name;
    if (a->rivalName && a->play == PLAY_JOURNEY) {
        ri->rivalName = a->rivalName;
        for (int k = 0; k < TEAM_SIZE; k++) ri->rivalNames[k] = a->rivalKids[k].name;
    }
    static char banner[64];
    if (a->play == PLAY_DAILY) { snprintf(banner, sizeof(banner), "Daily pond: %s", Game_TwistName(a->daily.twist)); ri->banner = banner; }
    else if (a->play == PLAY_JOURNEY) { snprintf(banner, sizeof(banner), "%s, %s", Journey_StageName(a->journey.stage), Journey_GameName(&a->journey)); ri->banner = banner; }
}

static void DrawGame(const App *a) {
    RenderInfo ri;
    FillInfo(a, &ri);
    const Game *g = ShownGame(a);
    if (!g) return;
    /* the arena's big screen: after the first second of a goal, the goal again */
    if (a->play != PLAY_ONLINE && g->phase == PH_GOAL && g->setup.venue == VENUE_ARENA && sReplayCount > 20 && g->phaseTicks >= 60) {
        float t = (float)(g->phaseTicks - 60) / 260.0f;
        if (t < 1.0f) {
            int frame = (int)(t * (float)(sReplayCount - 1));
            ri.replay = true;
            ri.replayProgress = t;
            Render_Game(&sReplay[frame], &ri);
            return;
        }
    }
    Render_Game(g, &ri);
}

static void Draw(const App *a) {
    switch (a->state) {
        case APP_TITLE: Screen_Title(); break;
        case APP_MENU: Screen_Menu(a->menuSel); break;
        case APP_JOIN: {
            bool freeDev[DEV_COUNT];
            for (int d = 0; d < DEV_COUNT; d++) freeDev[d] = Input_DeviceReady(d) && SlotOfDevice(a, d) < 0 && FreeSlot(a) >= 0;
            Screen_Join(a->slots, a->settings.rivals, freeDev, DEV_COUNT, a->play == PLAY_QUICK, a->play == PLAY_JOURNEY ? "THE CREW" : NULL);
            break;
        }
        case APP_SETUP: Screen_Setup(a->setupSel, &a->settings, a->slots); break;
        case APP_SO_SETUP: Screen_ShootoutSetup(a->soSel, &a->settings); break;
        case APP_DAILY: Screen_Daily(&a->daily, a->dailySel); break;
        case APP_HUB: Screen_JourneyHub(&a->journey, a->haveJourney, a->hubSel, a->hubConfirm); break;
        case APP_SIGN: Screen_Sign(&a->journey, a->signStep, a->signTheirs, a->signSel); break;
        case APP_HOWTO: Screen_HowTo(a->howtoPage); break;
        case APP_SETTINGS: Screen_Settings(a->settingsSel, &a->settings); break;
        case APP_KEYS: Screen_Keys(a->keysRow, a->keysCol, &a->settings, a->keysWaiting); break;
        case APP_NET: NetUi_Draw(); break;
        case APP_GAME: DrawGame(a); break;
        case APP_PAUSE: DrawGame(a); Screen_Pause(a->pauseSel, a->play == PLAY_ONLINE); break;
        case APP_RESULTS: {
            DrawGame(a);
            RenderInfo ri;
            FillInfo(a, &ri);
            Screen_Results(ShownGame(a), &ri, a->play == PLAY_ONLINE ? 1 : a->resultSel);
            break;
        }
        case APP_JOURNEY_RESULT: {
            DrawGame(a);
            RenderInfo ri;
            FillInfo(a, &ri);
            Screen_JourneyResult(&a->journey, &a->jr, &sGame, &ri);
            break;
        }
        case APP_JOURNEY_END: Screen_JourneyEnd(&a->journey, &a->runScore); break;
        case APP_SO_RESULT: DrawGame(a); Screen_ShootoutResult(&sGame, &a->runScore, a->resultSel); break;
        case APP_DAILY_RESULT: DrawGame(a); Screen_DailyResult(&sGame, &a->daily, &a->runScore, a->resultSel); break;
    }
}

int main(int argc, char **argv) {
    App *a = &sApp;
    memset(a, 0, sizeof(*a));
    Daily_Init(argc, argv);
    Settings_Load(&a->settings);
    ApplyKeys(&a->settings);
    Prefs_Load();
    GhostLink_GetUsername(CATALOG_NAME, a->username, sizeof(a->username));
    GhostLink_TriggerSync();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(SCREEN_W, SCREEN_H, "Pond Kings");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    Win_Init(SCREEN_W, SCREEN_H);

    char assets[512];
    ResolveAssetsDir(assets, sizeof(assets));
    char icon[640];
    snprintf(icon, sizeof(icon), "%s/icons/pondkings.png", assets);
    if (FileExists(icon)) {
        Image img = LoadImage(icon);
        if (img.data) { SetWindowIcon(img); UnloadImage(img); }
    }
    Sfx_Init(assets, kSfxFiles, SFX_COUNT);
    Music_Init(SLUG);
    ApplySound(&a->settings);
    Render_Init();
    NetUi_Init(a->username);

    a->state = APP_TITLE;
    if (Daily_Active()) { /* started from the launcher's daily desk */
        SoloSlot(a);
        PrepareDaily(a);
        a->state = APP_DAILY;
    }
    while (!a->quit && !WindowShouldClose()) {
        Win_Update();
        Input_Update();
        float dt = GetFrameTime();
        switch (a->state) {
            case APP_TITLE: UpdateTitle(a); break;
            case APP_MENU: UpdateMenu(a); break;
            case APP_JOIN: UpdateJoin(a); break;
            case APP_SETUP: UpdateSetup(a); break;
            case APP_SO_SETUP: UpdateShootoutSetup(a); break;
            case APP_DAILY: UpdateDaily(a); break;
            case APP_HUB: UpdateHub(a); break;
            case APP_SIGN: UpdateSign(a); break;
            case APP_GAME: UpdateGame(a, dt); break;
            case APP_PAUSE: UpdatePause(a); break;
            case APP_RESULTS:
            case APP_SO_RESULT:
            case APP_DAILY_RESULT: UpdateResults(a); break;
            case APP_JOURNEY_RESULT: UpdateJourneyResult(a); break;
            case APP_JOURNEY_END: {
                MenuPress m = Input_MenuAny();
                if (m.confirm || m.start || m.back) { Sfx_Play(SFX_CONFIRM); a->state = APP_MENU; }
                break;
            }
            case APP_HOWTO: {
                MenuPress m = Input_MenuAny();
                if (m.left) a->howtoPage = (a->howtoPage + HOWTO_PAGES - 1) % HOWTO_PAGES;
                if (m.right || m.confirm) a->howtoPage = (a->howtoPage + 1) % HOWTO_PAGES;
                if (m.back) { Sfx_Play(SFX_BACK); a->state = APP_MENU; }
                break;
            }
            case APP_SETTINGS: UpdateSettings(a); break;
            case APP_KEYS: UpdateKeys(a); break;
            case APP_NET: {
                NetUiStep r = NetUi_Update(&a->settings);
                if (r == NETUI_BACK) a->state = APP_MENU;
                else if (r == NETUI_PLAY) {
                    a->play = PLAY_ONLINE;
                    a->overTicks = 0;
                    a->recorded = false;
                    memset(a->slots, 0, sizeof(a->slots));
                    a->slots[0] = (JoinSlot){true, NetUi_LocalDevice(), 0};
                    Render_NewGame();
                    a->state = APP_GAME;
                }
                break;
            }
        }
        MusicFor(a);
        bool inGame = a->state == APP_GAME || a->state == APP_PAUSE || a->state == APP_RESULTS || a->state == APP_JOURNEY_RESULT ||
                      a->state == APP_SO_RESULT || a->state == APP_DAILY_RESULT;
        Render_Prepare(inGame ? ShownGame(a) : NULL, dt);
        Win_BeginFrame();
        ClearBackground((Color){10, 14, 38, 255});
        Draw(a);
        Win_EndFrame();
    }

    NetUi_Shutdown();
    Render_Shutdown();
    Music_Shutdown();
    Sfx_Shutdown();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
