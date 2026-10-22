/* Pictures of Pond Kings' real screens, drawn by the real render.c,
 * screens.c and netui.c from a HIDDEN window (it is never shown on the
 * desktop), for checking the look without playing. `make shots` writes them
 * to build/shots (tools/shots_venues.c does the rinks and the arena). The
 * game moments come from real games between computer players, run tick by
 * tick through game.c; the Journey from journey.c; the online screens from
 * netui.c's preview states, with no network. */
#include "raylib.h"
#include "game.h"
#include "journey.h"
#include "render.h"
#include "screens.h"
#include "input.h"
#include "netui.h"
#include "prefs.h"
#include <stdio.h>
#include <string.h>

static RenderTexture2D sTarget;
static const char *sOut = ".";
static int sCount = 0;

static void Save(const char *name) {
    Image img = LoadImageFromTexture(sTarget.texture);
    ImageFlipVertical(&img);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", sOut, name);
    if (ExportImage(img, path)) sCount++;
    else printf("could not write %s\n", path);
    UnloadImage(img);
}

static void Begin(void) {
    BeginDrawing(); /* raylib wants a frame around texture drawing */
    BeginTextureMode(sTarget);
    ClearBackground((Color){10, 14, 38, 255});
}

static void End(void) {
    EndTextureMode();
    EndDrawing();
}

static const PadInput kNone[MAX_PLAYERS] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

static void Step(Game *g, int n) {
    for (int k = 0; k < n; k++) {
        Game_Step(g, kNone);
        Render_OnStep(g);
        Render_Prepare(g, 1.0f / 60.0f);
    }
}

/* Draws the game as P1 would see it: the skater nearest the puck wears P1's
 * ring and tag for the picture only (the computer is really steering). */
static void Shoot(Game *g, const RenderInfo *ri, const char *name) {
    Render_Prepare(g, 0.0f);
    int tag = -1;
    if (g->phase != PH_FACEOFF) {
        int64_t best = 0;
        for (int i = 0; i < TEAM_SIZE; i++) {
            if (g->sk[i].player >= 0) { tag = -1; break; }
            int64_t d = V2_Len2(V2_Sub(g->sk[i].pos, g->puck.pos));
            if (tag < 0 || d < best) { tag = i; best = d; }
        }
    }
    if (tag >= 0) g->sk[tag].player = 0;
    Begin();
    Render_Game(g, ri);
    End();
    if (tag >= 0) g->sk[tag].player = -1;
    Save(name);
}

static void PlayOut(Game *g) {
    int guard = 0;
    while (g->phase != PH_OVER && guard++ < 60 * 60 * 20) Step(g, 1);
}

int main(int argc, char **argv) {
    if (argc > 1) sOut = argv[1];
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(SCREEN_W, SCREEN_H, "pondkings-shots");
    sTarget = LoadRenderTexture(SCREEN_W, SCREEN_H);
    Prefs_Load();
    Render_Init();
    NetUi_Init("TRG");
    for (int k = 0; k < 120; k++) Render_Prepare(NULL, 1.0f / 60.0f); /* let the snow and smoke get going */

    Settings settings;
    Settings_Default(&settings);
    JoinSlot slots[MAX_PLAYERS];
    memset(slots, 0, sizeof(slots));
    slots[0] = (JoinSlot){true, DEV_KEYS_LEFT, 0};
    slots[1] = (JoinSlot){true, DEV_PAD0, 0};
    slots[2] = (JoinSlot){true, DEV_PAD1, 1};
    bool freeDev[DEV_COUNT] = {false, true, false, false, true, true};

    /* the menus */
    Begin(); Screen_Title(); End(); Save("title");
    Begin(); Screen_Menu(0); End(); Save("menu");
    Begin(); Screen_Menu(MI_ONLINE); End(); Save("menu-online");
    Begin(); Screen_Join(slots, settings.rivals, freeDev, DEV_COUNT, true, NULL); End(); Save("join");
    Begin(); Screen_Join(slots, settings.rivals, freeDev, DEV_COUNT, false, "THE JOURNEY"); End(); Save("join-journey");
    Begin(); Screen_Setup(3, &settings, slots); End(); Save("setup");
    settings.venue = VENUE_ARENA;
    Begin(); Screen_Setup(0, &settings, slots); End(); Save("setup-arena");
    settings.venue = VENUE_POND;
    Begin(); Screen_ShootoutSetup(0, &settings); End(); Save("shootout-setup");
    DailyInfo daily = {"2026-10-22", "daily-20261022", 20261022, 2, TWIST_WIND, 0};
    Begin(); Screen_Daily(&daily, 0); End(); Save("daily");
    for (int p = 0; p < HOWTO_PAGES; p++) {
        char name[32];
        snprintf(name, sizeof(name), "howto-%d", p + 1);
        Begin(); Screen_HowTo(p); End(); Save(name);
    }
    Begin(); Screen_Settings(4, &settings); End(); Save("settings");
    Begin(); Screen_Keys(4, 0, &settings, false); End(); Save("keys");
    Begin(); Screen_Keys(2, 1, &settings, true); End(); Save("keys-waiting");

    /* the Journey */
    static Journey j;
    Journey_New(&j, 4242, NULL);
    Begin(); Screen_JourneyHub(&j, true, 0, false); End(); Save("journey-hub");
    Begin(); Screen_JourneyHub(&j, true, 1, true); End(); Save("journey-new-confirm");
    static Game g;
    GameSetup s;
    int8_t crewSlots[MAX_PLAYERS] = {0, -1, -1, -1};
    Journey_Setup(&j, &s, crewSlots, 20261022, 1);
    Game_Init(&g, &s);
    Render_NewGame();
    PlayOut(&g);
    g.score[0] = g.score[1] + 2; /* the picture is of a win */
    g.winner = 0;
    JourneyResult jr;
    Journey_Result(&j, &g, &jr);
    RenderInfo ri;
    memset(&ri, 0, sizeof(ri));
    ri.rivals = j.lastOpponent >= 0 && j.lastOpponent < RIVAL_COUNT ? j.lastOpponent : 0;
    ri.pingMs = -1;
    for (int k = 0; k < TEAM_SIZE; k++) ri.crewNames[k] = j.crew[k].name;
    Render_Prepare(&g, 0.0f);
    Begin(); Render_Game(&g, &ri); Screen_JourneyResult(&j, &jr, &g, &ri); End(); Save("journey-result");
    if (j.canSign) {
        Begin(); Screen_Sign(&j, 0, 0, 1); End(); Save("journey-sign");
        Begin(); Screen_Sign(&j, 1, 1, 2); End(); Save("journey-sign-room");
    } else {
        printf("no picture journey-sign: the win didn't offer a signing\n");
    }
    Begin(); Screen_JourneyHub(&j, true, 0, false); End(); Save("journey-hub-after-win");
    j.finished = 1;
    j.champion = 1;
    j.active = 0;
    RunScore rs = {8450, 1, true, "Pond Kings"};
    Begin(); Screen_JourneyEnd(&j, &rs); End(); Save("journey-end");

    /* a quick game on the pond */
    memset(&ri, 0, sizeof(ri));
    ri.rivals = 0;
    ri.pingMs = -1;
    ri.passKey[0] = "SHIFT";
    ri.passKey[1] = "A";
    Game_DefaultSetup(&s, 20261022);
    s.slotTeam[0] = 0;
    s.slotTeam[1] = 0;
    s.slotTeam[2] = 1;
    s.periodSeconds = 60;
    Game_Init(&g, &s);
    Render_NewGame();
    Step(&g, g.dropAt - 20);
    Shoot(&g, &ri, "faceoff");

    /* the players stand still, so the computer takes their skaters from here */
    for (int p = 0; p < MAX_PLAYERS; p++) Game_LeavePlayer(&g, p);
    Step(&g, 600);
    Shoot(&g, &ri, "play");

    int guard = 0;
    while (!(g.events & EV_GOAL) && guard++ < 60 * 60 * 5) Step(&g, 1);
    Step(&g, 30);
    Shoot(&g, &ri, "goal");

    guard = 0;
    while (g.phase != PH_BREAK && guard++ < 60 * 60 * 5) Step(&g, 1);
    Step(&g, 30);
    Shoot(&g, &ri, "snow-before-shovel");
    Step(&g, 80);
    Shoot(&g, &ri, "shovel-break");

    Step(&g, 400);
    Render_Prepare(&g, 0.0f);
    Begin(); Render_Game(&g, &ri); Screen_Pause(0, false); End(); Save("pause");
    Begin(); Render_Game(&g, &ri); Screen_Pause(2, true); End(); Save("pause-online");
    ri.pingMs = 48;
    ri.waiting = true;
    Shoot(&g, &ri, "online-waiting");
    ri.pingMs = -1;
    ri.waiting = false;

    PlayOut(&g);
    Render_Prepare(&g, 0.0f);
    Begin(); Render_Game(&g, &ri); Screen_Results(&g, &ri, 0); End(); Save("results");

    /* the shootout and the daily's results */
    GameSetup so;
    Game_DefaultSetup(&so, 77);
    so.mode = MODE_SHOOTOUT;
    so.venue = VENUE_RINK;
    for (int p = 0; p < MAX_PLAYERS; p++) so.slotTeam[p] = p == 0 ? 0 : -1;
    Game_Init(&g, &so);
    Render_NewGame();
    PlayOut(&g);
    RunScore sors = {g.score[0] > 0 ? g.score[0] : 3, 2, false, ""};
    Render_Prepare(&g, 0.0f);
    Begin(); Render_Game(&g, &ri); Screen_ShootoutResult(&g, &sors, 0); End(); Save("shootout-result");

    GameSetup ds;
    Game_DefaultSetup(&ds, daily.seed);
    ds.twist = (uint8_t)daily.twist;
    ds.periodSeconds = 60;
    for (int p = 0; p < MAX_PLAYERS; p++) ds.slotTeam[p] = -1;
    Game_Init(&g, &ds);
    Render_NewGame();
    PlayOut(&g);
    RunScore drs = {1450, 1, true, "First Faceoff"};
    ri.banner = "Daily pond: Wind";
    Render_Prepare(&g, 0.0f);
    Begin(); Render_Game(&g, &ri); Screen_DailyResult(&g, &daily, &drs, 0); End(); Save("daily-result");

    /* online and the rival crews */
    static const char *const kNet[NETUI_PREVIEW_COUNT] = {"online-notice", "online-menu", "online-host", "online-join-code", "online-room-code",
                                                         "online-connecting", "online-not-ready", "crews", "crews-add-code"};
    for (int i = 0; i < NETUI_PREVIEW_COUNT; i++) {
        NetUi_Preview(i, &settings);
        Begin(); NetUi_Draw(); End(); Save(kNet[i]);
    }

    NetUi_Shutdown();
    Render_Shutdown();
    UnloadRenderTexture(sTarget);
    CloseWindow();
    printf("%d pictures in %s\n", sCount, sOut);
    return sCount >= 40 ? 0 : 1; /* 42, or 40 when the Journey's win offered nobody to sign */
}
