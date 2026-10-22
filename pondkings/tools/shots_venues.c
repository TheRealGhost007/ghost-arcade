/* Pictures of Pond Kings' town rink and arena, drawn by the real render.c
 * from a HIDDEN window (it is never shown on the desktop): goalies,
 * slapshots, the resurfacing breaks, the big screen's replay, the shootout,
 * the twists and the menus' backdrops. Every game moment comes from a real
 * game run tick by tick through game.c, between computer players unless a
 * picture needs a player's hands on the stick.
 *
 *   (`make shots` builds and runs this with the menus' tool; by hand:)
 *   gcc -std=c17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -I../ghost-common \
 *       $(pkg-config --cflags raylib) tools/shots_venues.c src/render.c src/game.c src/ai.c \
 *       src/teams.c src/journey.c ../ghost-common/rng.c ../ghost-common/safefile.c \
 *       -o build/shots-venues $(pkg-config --libs raylib) -lm
 *   ./build/shots-venues build/shots-venues-png */
#include "raylib.h"
#include "game.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

static RenderTexture2D sTarget;
static const char *sOut = ".";
static int sCount = 0, sMissing = 0;

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

static void Missing(const char *name, const char *why) {
    printf("no picture %s: %s\n", name, why);
    sMissing++;
}

static const PadInput kNone[MAX_PLAYERS] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

static void StepWith(Game *g, const PadInput in[MAX_PLAYERS]) {
    Game_Step(g, in);
    Render_OnStep(g);
    Render_Prepare(g, 1.0f / 60.0f);
}

static void Step(Game *g, int n) {
    for (int k = 0; k < n; k++) StepWith(g, kNone);
}

/* Steps until done(g) holds after a tick, or the guard runs out. */
static bool Until(Game *g, bool (*done)(const Game *), int guard) {
    for (int k = 0; k < guard; k++) {
        StepWith(g, kNone);
        if (done(g)) return true;
    }
    return false;
}

/* The picture as P1 would see it: with tagNearest, the crew's skater nearest
 * the puck wears P1's ring and tag (the computer is really steering). */
static void Shot(Game *g, const RenderInfo *ri, const char *name, bool tagNearest) {
    Render_Prepare(g, 0.0f);
    int tag = -1;
    if (tagNearest) {
        int64_t best = 0;
        for (int i = 0; i < TEAM_SIZE; i++) {
            if (g->sk[i].player >= 0 || g->sk[i].benched) { tag = -1; break; }
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

static RenderInfo Info(void) {
    RenderInfo ri;
    memset(&ri, 0, sizeof(ri));
    ri.rivals = 0;
    ri.passKey[0] = "SHIFT";
    ri.shootKey[0] = "X";
    ri.pingMs = -1;
    return ri;
}

static void Setup(GameSetup *s, int venue, int mode, int twist, uint64_t seed) {
    Game_DefaultSetup(s, seed);
    s->venue = (uint8_t)venue;
    s->mode = (uint8_t)mode;
    s->twist = (uint8_t)twist;
    s->periodSeconds = 30;
    for (int p = 0; p < MAX_PLAYERS; p++) s->slotTeam[p] = -1;
    s->goalieStyle[0] = GOALIE_BUTTERFLY;
    s->goalieStyle[1] = GOALIE_SCRAMBLER;
}

static void Start(Game *g, const GameSetup *s) {
    Game_Init(g, s);
    Render_NewGame();
}

static bool Playing(const Game *g) { return g->phase == PH_PLAY && g->phaseTicks > 30; }
static bool HardOffBoards(const Game *g) { return (g->events & EV_BOARDS) && V2_Len2(g->puck.vel) > (int64_t)FX_INT(4) * FX_INT(4); }
static bool ReboundSave(const Game *g) { return (g->events & EV_SAVE) && !(g->events & EV_FREEZE); }
static bool Stoppage(const Game *g) { return g->phase == PH_STOPPAGE; }
static bool OnBreak(const Game *g) { return g->phase == PH_BREAK; }
static bool Facing(const Game *g) { return g->phase == PH_FACEOFF; }
static bool Scored(const Game *g) { return (g->events & EV_GOAL) != 0; }
static bool SoResult(const Game *g) { return g->phase == PH_SO_RESULT || g->phase == PH_OVER; }
static bool SoNext(const Game *g) { return g->phase != PH_SO_RESULT; }

/* ------------------------------------------------------------ the scenes */

static void Backdrops(void) {
    static const char *const kNames[VENUE_COUNT] = {"backdrop-pond", "backdrop-rink", "backdrop-arena"};
    for (int v = 0; v < VENUE_COUNT; v++) {
        Begin();
        Render_VenueBackdrop(v);
        End();
        Save(kNames[v]);
    }
}

static void InPlay(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    /* the town rink: a faceoff with P1 at centre, then play */
    Setup(&s, VENUE_RINK, MODE_MATCH, TWIST_NONE, 20261022);
    s.slotTeam[0] = 0;
    Start(&g, &s);
    Step(&g, g.dropAt - 20);
    Shot(&g, &ri, "rink-faceoff", false);
    Game_LeavePlayer(&g, 0);
    Step(&g, 600);
    Shot(&g, &ri, "rink-play", true);
    if (Until(&g, Scored, 60 * 60 * 3)) {
        Step(&g, 40);
        Shot(&g, &ri, "rink-goal", false);
    } else {
        Missing("rink-goal", "nobody scored");
    }
    if (Until(&g, HardOffBoards, 60 * 60 * 3)) {
        Step(&g, 2);
        Shot(&g, &ri, "rink-boards", false);
    } else {
        Missing("rink-boards", "the puck never hit the boards hard");
    }
    /* the arena: a faceoff with P1 at centre, then play */
    Setup(&s, VENUE_ARENA, MODE_MATCH, TWIST_NONE, 31337);
    s.slotTeam[0] = 0;
    Start(&g, &s);
    Step(&g, g.dropAt - 20);
    Shot(&g, &ri, "arena-faceoff", false);
    Game_LeavePlayer(&g, 0);
    Until(&g, Playing, 60 * 30);
    Step(&g, 420);
    Shot(&g, &ri, "arena-play", true);
}

static void Goalies(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    bool saved = false;
    for (uint64_t seed = 1234; seed < 1244 && !saved; seed++) {
        Setup(&s, VENUE_RINK, MODE_MATCH, TWIST_NONE, seed);
        Start(&g, &s);
        for (int tries = 0; tries < 20 && !saved; tries++) {
            if (!Until(&g, ReboundSave, 60 * 60 * 5)) break;
            Step(&g, 3);
            if (g.phase != PH_PLAY) continue; /* smothered straight after: that's the next picture's */
            Shot(&g, &ri, "goalie-save", false);
            saved = true;
        }
    }
    if (!saved) Missing("goalie-save", "no rebound save");
    Setup(&s, VENUE_ARENA, MODE_MATCH, TWIST_NONE, 4321);
    s.goalieStyle[1] = GOALIE_POKER;
    Start(&g, &s);
    if (Until(&g, Stoppage, 60 * 60 * 5)) {
        Step(&g, 12);
        Shot(&g, &ri, "goalie-holds", false);
    } else {
        Missing("goalie-holds", "no goalie held on");
    }
}

/* P1 on the crew; whenever the crew has the puck it's P1's, and from the
 * neutral zone P1 holds Shoot, creeping toward the far net, then lets go. */
static void Slapshot(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    bool wound = false, released = false;
    PadInput in[MAX_PLAYERS];
    memset(in, 0, sizeof(in));
    for (int guard = 0; guard < 60 * 60 * 20 && !released; guard++) {
        if (guard % (60 * 60 * 4) == 0) {
            /* a fresh game every few minutes, in case this one never gives P1 the chance */
            Setup(&s, VENUE_RINK, MODE_MATCH, TWIST_NONE, 77001 + (uint64_t)(guard / (60 * 60 * 4)));
            s.slotTeam[0] = 0;
            Start(&g, &s);
        }
        int me = g.slotSkater[0];
        if (!(g.phase == PH_PLAY && me >= 0 && g.puck.carrier == me && g.sk[me].actCool == 0 && g.sk[me].stun == 0 &&
              g.sk[me].pos.x < FX_INT(40) && g.sk[me].pos.x > FX_INT(-160))) {
            StepWith(&g, kNone);
            continue;
        }
        in[0] = (PadInput){1, BTN_SHOOT};
        for (int held = 0; held < 44 && g.phase == PH_PLAY && g.puck.carrier == me; held++) {
            StepWith(&g, in);
            if (!wound && g.sk[me].charge >= 30) {
                Shot(&g, &ri, "slapshot-windup", false);
                wound = true;
            }
        }
        if (g.phase == PH_PLAY && g.puck.carrier == me && g.sk[me].charge >= 40) {
            in[0] = (PadInput){1, 0};
            StepWith(&g, in);
            if (g.events & EV_SLAPSHOT) {
                Step(&g, 7);
                if (g.puck.flight == FLIGHT_SHOT) {
                    Shot(&g, &ri, "slapshot-release", false);
                    released = true;
                }
            }
        }
    }
    if (!wound) Missing("slapshot-windup", "P1 never wound one up");
    if (!released) Missing("slapshot-release", "P1 never let one go");
}

static void Breaks(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    for (int v = VENUE_RINK; v <= VENUE_ARENA; v++) {
        const char *name = v == VENUE_RINK ? "rink-break" : "arena-break";
        Setup(&s, v, MODE_MATCH, TWIST_NONE, 4242 + (uint64_t)v);
        Start(&g, &s);
        if (!Until(&g, OnBreak, 60 * 60 * 5)) {
            Missing(name, "no break");
            continue;
        }
        Step(&g, 70);
        Shot(&g, &ri, name, false);
        Step(&g, 120);
        Shot(&g, &ri, v == VENUE_RINK ? "rink-break-turn" : "arena-break-turn", false);
        if (v == VENUE_RINK) {
            /* the next period's faceoff, on new ice still glossy */
            Until(&g, Facing, 60 * 10);
            Step(&g, 20);
            Shot(&g, &ri, "rink-new-ice", false);
        }
    }
}

/* the arena's goal, and its replay on the big screen from the states kept before it */
#define HISTORY 200
static Game sHistory[HISTORY];

static void Replay(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    Setup(&s, VENUE_ARENA, MODE_MATCH, TWIST_NONE, 8086);
    Start(&g, &s);
    int at = 0, kept = 0;
    bool scored = false;
    for (int guard = 0; guard < 60 * 60 * 5 && !scored; guard++) {
        StepWith(&g, kNone);
        sHistory[at] = g;
        at = (at + 1) % HISTORY;
        if (kept < HISTORY) kept++;
        scored = (g.events & EV_GOAL) != 0;
    }
    if (!scored || kept < 150) {
        Missing("arena-goal", "nobody scored");
        Missing("arena-replay", "nobody scored");
        return;
    }
    Step(&g, 40);
    Shot(&g, &ri, "arena-goal", false);
    Step(&g, 60);
    /* 40% through a replay of the last 150 ticks before the goal */
    const Game *old = &sHistory[(at - 1 - 90 + 2 * HISTORY) % HISTORY];
    RenderInfo rp = ri;
    rp.replay = true;
    rp.replayProgress = 0.4f;
    Render_Prepare(&g, 0.0f);
    Begin();
    Render_Game(old, &rp);
    End();
    Save("arena-replay");
}

static void Shootout(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    /* P1 shooting at the town rink: the moment before the try */
    Setup(&s, VENUE_RINK, MODE_SHOOTOUT, TWIST_NONE, 5150);
    s.slotTeam[0] = 0;
    Start(&g, &s);
    Step(&g, 20);
    Shot(&g, &ri, "shootout-ready", false);
    /* the computer shooting in the arena, until it has scored, been saved and missed */
    static const char *const kNames[3] = {"shootout-score", "shootout-saved", "shootout-missed"};
    bool have[3] = {false, false, false};
    for (uint64_t seed = 5151; seed < 5171 && !(have[0] && have[1] && have[2]); seed++) {
        Setup(&s, VENUE_ARENA, MODE_SHOOTOUT, TWIST_NONE, seed);
        Start(&g, &s);
        while (g.phase != PH_OVER && !(have[0] && have[1] && have[2])) {
            if (!Until(&g, SoResult, 60 * 30) || g.phase == PH_OVER) break;
            Step(&g, 24);
            int kind = (g.gk[1].hold > 0 || g.gk[1].recover > 0) ? 1 : (g.soStreak > 0 ? 0 : 2);
            if (!have[kind]) {
                Shot(&g, &ri, kNames[kind], false);
                have[kind] = true;
            }
            if (!Until(&g, SoNext, 60 * 30)) break;
        }
    }
    for (int k = 0; k < 3; k++)
        if (!have[k]) Missing(kNames[k], "never happened");
}

static void Twists(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    static const struct { int twist; const char *name, *banner; } kTwists[4] = {
        {TWIST_WIND, "pond-wind", "Daily pond: Wind"},
        {TWIST_GLASS_ICE, "pond-glass-ice", "Daily pond: Glass ice"},
        {TWIST_BLIZZARD, "pond-blizzard", "Daily pond: Blizzard"},
        {TWIST_TINY_NETS, "pond-tiny-nets", "Daily pond: Tiny nets"},
    };
    for (int k = 0; k < 4; k++) {
        Setup(&s, VENUE_POND, MODE_MATCH, kTwists[k].twist, 9000 + (uint64_t)k);
        Start(&g, &s);
        ri.banner = kTwists[k].banner;
        Until(&g, Playing, 60 * 30);
        Step(&g, 500);
        Shot(&g, &ri, kTwists[k].name, true);
    }
}

/* a friend's crew in the arena, online, waiting on the other machine, with the Journey's names */
static void Online(void) {
    static Game g;
    GameSetup s;
    RenderInfo ri = Info();
    static const char *const kCrew[TEAM_SIZE] = {"Zippy", "Mo-Mo", "Tank"};
    static const char *const kTheirs[TEAM_SIZE] = {"Ace", "Biscuit", "Crusher"};
    for (int k = 0; k < TEAM_SIZE; k++) {
        ri.crewNames[k] = kCrew[k];
        ri.rivalNames[k] = kTheirs[k];
    }
    ri.rivalName = "Gus's Gang";
    ri.banner = "The arena playoffs, the final";
    ri.pingMs = 42;
    ri.waiting = true;
    Setup(&s, VENUE_ARENA, MODE_MATCH, TWIST_NONE, 2024);
    Start(&g, &s);
    if (Until(&g, Scored, 60 * 60 * 5)) {
        Step(&g, 40);
        Shot(&g, &ri, "arena-online-names", false);
    } else {
        Missing("arena-online-names", "nobody scored");
    }
}

int main(int argc, char **argv) {
    if (argc > 1) sOut = argv[1];
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(SCREEN_W, SCREEN_H, "pondkings-shots-venues");
    sTarget = LoadRenderTexture(SCREEN_W, SCREEN_H);
    Render_Init();
    for (int k = 0; k < 120; k++) Render_Prepare(NULL, 1.0f / 60.0f); /* let the snow and smoke get going */

    Backdrops();
    InPlay();
    Goalies();
    Slapshot();
    Breaks();
    Replay();
    Shootout();
    Twists();
    Online();

    Render_Shutdown();
    UnloadRenderTexture(sTarget);
    CloseWindow();
    printf("%d pictures in %s\n", sCount, sOut);
    return sMissing == 0 ? 0 : 1;
}
