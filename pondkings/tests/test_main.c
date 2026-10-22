/* Headless tests for Pond Kings' rules core (game.c, ai.c). No raylib, so
 * they run anywhere `make test` runs, CI included. Scenario tests park the
 * skaters they don't need by stunning them for a long time: a stunned skater
 * neither moves on purpose, nor picks the puck up, nor blocks it. */
#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "../src/ai.h"
#include "../src/journey.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static const PadInput kNone[MAX_PLAYERS] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

/* A deterministic stream of button mashing for scripted players. */
static uint32_t sLcg = 12345;
static uint32_t Lcg(void) { sLcg = sLcg * 1664525u + 1013904223u; return sLcg >> 8; }
static void RandomInputs(PadInput in[MAX_PLAYERS]) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        in[p].dir = (uint8_t)(Lcg() % (DIR_COUNT + 1));
        in[p].buttons = (uint8_t)(Lcg() % 8 == 0 ? (Lcg() % 8) : 0);
    }
}

static void Setup(GameSetup *s, uint64_t seed, int humans0, int humans1) {
    Game_DefaultSetup(s, seed);
    for (int p = 0; p < MAX_PLAYERS; p++) s->slotTeam[p] = -1;
    int slot = 0;
    for (int k = 0; k < humans0 && slot < MAX_PLAYERS; k++) s->slotTeam[slot++] = 0;
    for (int k = 0; k < humans1 && slot < MAX_PLAYERS; k++) s->slotTeam[slot++] = 1;
}

static void Steps(Game *g, int n) { for (int k = 0; k < n; k++) Game_Step(g, kNone); }

/* Straight into play: everyone parked except the ones named, puck where wanted. */
static void ScenarioAt(Game *g, uint64_t seed, int venue) {
    GameSetup s;
    Setup(&s, seed, 0, 0);
    s.venue = (uint8_t)venue;
    Game_Init(g, &s);
    g->phase = PH_PLAY;
    g->phaseTicks = 0;
    for (int i = 0; i < SKATERS; i++) {
        g->sk[i].stun = 30000;
        g->sk[i].vel = V2_Make(0, 0);
    }
    /* park them in the corners, well away from the action */
    for (int i = 0; i < SKATERS; i++) g->sk[i].pos = V2_Make(FX_INT(i % 2 ? 250 : -250), FX_INT(i < 3 ? -150 : 150));
}

static void Scenario(Game *g, uint64_t seed) { ScenarioAt(g, seed, VENUE_POND); }

static void Wake(Game *g, int i, V2 pos, V2 face) {
    g->sk[i].stun = 0;
    g->sk[i].pos = pos;
    g->sk[i].face = face;
    g->sk[i].vel = V2_Make(0, 0);
}

/* ---------------------------------------------------------------- maths */

static void test_fixed(void) {
    CHECK(Fx_Mul(FX_INT(3), FX_RATIO(1, 2)) == FX_RATIO(3, 2), "fx: 3 x 0.5");
    CHECK(Fx_Mul(FX_INT(-3), FX_RATIO(1, 2)) == FX_RATIO(-3, 2), "fx: negatives multiply the same way");
    CHECK(Fx_Div(FX_INT(3), FX_INT(4)) == FX_RATIO(3, 4), "fx: 3 / 4");
    CHECK(Fx_Div(FX_INT(1), 0) == 0, "fx: dividing by zero gives zero, not a crash");
    CHECK(Fx_Sqrt(FX_INT(9)) == FX_INT(3) && Fx_Abs(Fx_Sqrt(FX_INT(2)) - 92682) <= 1, "fx: square roots");
    CHECK(Fx_ISqrt64(UINT64_C(0xFFFFFFFFFFFFFFFF)) == 0xFFFFFFFFu, "fx: the biggest square root");
    bool unit = true;
    for (int d = 1; d <= DIR_COUNT; d++) {
        fx len = V2_Len(Fx_DirVector((uint8_t)d));
        if (len < FX_ONE - 2 || len > FX_ONE + 2) unit = false;
    }
    CHECK(unit, "fx: all 32 stick directions are unit length");
    CHECK(V2_IsZero(Fx_DirVector(0)) && V2_IsZero(Fx_DirVector(DIR_COUNT + 1)), "fx: no direction, or a bad one, is zero");
    V2 n = V2_Norm(V2_Make(FX_INT(300), FX_INT(-400)));
    CHECK(n.x == FX_RATIO(3, 5) && n.y == FX_RATIO(-4, 5), "fx: normalising a 3-4-5 triangle is exact");
    CHECK(V2_Len(V2_Make(FX_INT(800), FX_INT(600))) == FX_INT(1000), "fx: lengths across the whole pond don't overflow");
    CHECK(V2_IsZero(V2_Norm(V2_Make(3, -2))), "fx: a vector too short to have a direction has none");
}

/* --------------------------------------------------------- determinism */

static void test_determinism(void) {
    GameSetup s;
    Setup(&s, 777, 2, 1);
    static Game a, b;
    Game_Init(&a, &s);
    Game_Init(&b, &s);
    CHECK(Game_Hash(&a) == Game_Hash(&b), "same: two new games are the same game");
    sLcg = 99;
    bool same = true;
    PadInput in[MAX_PLAYERS];
    for (int t = 0; t < 20000; t++) {
        RandomInputs(in);
        Game_Step(&a, in);
        Game_Step(&b, in);
        if (t % 500 == 0 && Game_Hash(&a) != Game_Hash(&b)) same = false;
    }
    CHECK(same && Game_Hash(&a) == Game_Hash(&b), "same: the same presses give the same game, tick for tick");

    static Game c;
    s.seed = 778;
    Game_Init(&c, &s);
    sLcg = 99;
    for (int t = 0; t < 20000; t++) { RandomInputs(in); Game_Step(&c, in); }
    CHECK(Game_Hash(&c) != Game_Hash(&a), "same: another seed is another game");
}

/* What rollback will do: save, play on, load, replay the same presses. */
static void test_rollback(void) {
    GameSetup s;
    Setup(&s, 4242, 1, 1);
    static Game g, saved;
    Game_Init(&g, &s);
    sLcg = 7;
    PadInput in[MAX_PLAYERS];
    for (int t = 0; t < 3000; t++) { RandomInputs(in); Game_Step(&g, in); }
    saved = g; /* a save is a copy */
    uint32_t seedAt = sLcg;
    for (int t = 0; t < 900; t++) { RandomInputs(in); Game_Step(&g, in); }
    uint64_t ahead = Game_Hash(&g);
    g = saved; /* a load is a copy back */
    sLcg = seedAt;
    for (int t = 0; t < 900; t++) { RandomInputs(in); Game_Step(&g, in); }
    CHECK(Game_Hash(&g) == ahead, "rollback: restore and replay arrives at the same game");

    /* different presses in the middle make a different game: the hash notices */
    g = saved;
    sLcg = seedAt;
    bool differsAtOnce = false;
    for (int t = 0; t < 900; t++) {
        RandomInputs(in);
        if (t >= 10 && t < 70) in[0].dir = (uint8_t)(1 + (in[0].dir + 16) % DIR_COUNT);
        Game_Step(&g, in);
        if (t == 10) { static Game ref; ref = saved; differsAtOnce = Game_Hash(&g) != Game_Hash(&ref); }
    }
    CHECK(differsAtOnce && Game_Hash(&g) != ahead, "rollback: changed presses show in the hash");

    static Game copy;
    memcpy(&copy, &saved, sizeof(copy));
    CHECK(Game_Hash(&copy) == Game_Hash(&saved), "rollback: a byte copy is the same game");
}

/* -------------------------------------------------------- containment */

static void test_containment(void) {
    GameSetup s;
    Setup(&s, 31337, 2, 2);
    s.periodSeconds = 60;
    static Game g;
    Game_Init(&g, &s);
    sLcg = 3;
    bool puckOk = true, skatersOk = true, finite = true;
    PadInput in[MAX_PLAYERS];
    for (int t = 0; t < 60000; t++) {
        RandomInputs(in);
        Game_Step(&g, in);
        const Puck *p = &g.puck;
        bool inNet = Fx_Abs(p->pos.x) >= GOAL_X - PUCK_R && Fx_Abs(p->pos.x) <= GOAL_X + NET_DEPTH && Fx_Abs(p->pos.y) < NET_HALF_W;
        if (p->buried == 0 && !Game_OnIce(&g, p->pos, 0) && !inNet) puckOk = false;
        for (int i = 0; i < SKATERS; i++) {
            if (!Game_OnIce(&g, g.sk[i].pos, SKATER_R - FX_INT(1))) skatersOk = false;
            if (V2_Len2(g.sk[i].vel) > (int64_t)FX_INT(8) * FX_INT(8)) finite = false;
        }
        if (V2_Len2(p->vel) > (int64_t)FX_INT(16) * FX_INT(16)) finite = false;
        if (g.phase == PH_OVER) {
            Game_Init(&g, &s);
        }
    }
    CHECK(puckOk, "containment: the puck never leaves the ice except into a snowbank or a net");
    CHECK(skatersOk, "containment: no skater ever leaves the ice");
    CHECK(finite, "containment: speeds stay sane");
}

/* ---------------------------------------------------------- the rules */

static void test_goal(void) {
    static Game g;
    Scenario(&g, 1);
    /* team 0's skater in the slot, facing the right net, puck on the blade */
    Wake(&g, 0, V2_Make(FX_INT(250), 0), V2_Make(FX_ONE, 0));
    g.puck.carrier = 0;
    g.puck.lastTouch = 0;
    g.sk[0].player = 0; /* a player, so the computer doesn't shoot first */
    g.slotSkater[0] = 0;
    Steps(&g, 1);
    CHECK(g.puck.carrier == 0, "goal: carrying");
    PadInput in[MAX_PLAYERS] = {{1, BTN_SHOOT}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, in);
    CHECK(g.events & EV_SHOT, "goal: the shot is away");
    bool scored = false;
    for (int t = 0; t < 120 && !scored; t++) { Game_Step(&g, kNone); if (g.events & EV_GOAL) scored = true; }
    CHECK(scored && g.score[0] == 1 && g.score[1] == 0, "goal: a shot from the slot at an empty net goes in");
    CHECK(g.lastScorer == 0 && g.sk[0].goals == 1 && g.lastGoalTeam == 0, "goal: credited to the shooter");
    CHECK(g.phase == PH_GOAL, "goal: the celebration");
    Steps(&g, 200);
    CHECK(g.phase == PH_FACEOFF || g.phase == PH_PLAY, "goal: then a faceoff");
}

static void test_net_back(void) {
    static Game g;
    Scenario(&g, 2);
    /* behind the right net, flying at its back wall */
    g.puck.pos = V2_Make(GOAL_X + NET_DEPTH + FX_INT(30), 0);
    g.puck.vel = V2_Make(FX_INT(-6), 0);
    bool net = false, goal = false;
    for (int t = 0; t < 40; t++) { Game_Step(&g, kNone); if (g.events & EV_NET) net = true; if (g.events & EV_GOAL) goal = true; }
    CHECK(net && !goal && g.score[0] == 0, "net: in through the back is no goal, it bounces off");
    CHECK(g.puck.pos.x > GOAL_X + NET_DEPTH, "net: and stays behind the net");

    Scenario(&g, 3);
    /* along the goal line from the side, into the post */
    g.puck.pos = V2_Make(GOAL_X - FX_INT(2), FX_INT(-80));
    g.puck.vel = V2_Make(0, FX_INT(6));
    bool post = false;
    goal = false;
    for (int t = 0; t < 40; t++) { Game_Step(&g, kNone); if (g.events & EV_POST) post = true; if (g.events & EV_GOAL) goal = true; }
    CHECK(post || net, "net: a puck sliding along the goal line rings off the frame");
    CHECK(!goal, "net: without crossing the line it isn't a goal");

    Scenario(&g, 4);
    /* a slow puck trickling across the line still counts */
    g.puck.pos = V2_Make(-GOAL_X + FX_INT(30), FX_INT(5));
    g.puck.vel = V2_Make(FX_RATIO(-15, 10), 0);
    g.puck.lastTouch = 4;
    goal = false;
    for (int t = 0; t < 80 && !goal; t++) { Game_Step(&g, kNone); if (g.events & EV_GOAL) goal = true; }
    CHECK(goal && g.score[1] == 1 && g.lastScorer == 4, "net: a trickler over the line is a goal (team 1 scores in the left net)");
}

static void test_snowbank(void) {
    static Game g;
    Scenario(&g, 5);
    g.puck.pos = V2_Make(0, POND_HW - FX_INT(40));
    g.puck.vel = V2_Make(0, FX_INT(9));
    bool buried = false;
    for (int t = 0; t < 20 && !buried; t++) { Game_Step(&g, kNone); if (g.events & EV_BANK) buried = true; }
    CHECK(buried && g.puck.buried > 0 && V2_IsZero(g.puck.vel), "bank: a hard puck buries itself in the snow");
    bool out = false;
    for (int t = 0; t < 100 && !out; t++) { Game_Step(&g, kNone); if (g.events & EV_DIG_OUT) out = true; }
    CHECK(out && g.puck.buried == 0 && Game_OnIce(&g, g.puck.pos, FX_INT(10)), "bank: then it's dug out, back on the ice");

    Scenario(&g, 6);
    g.puck.pos = V2_Make(0, POND_HW - FX_INT(20));
    g.puck.vel = V2_Make(0, FX_INT(2));
    bool soft = false;
    buried = false;
    for (int t = 0; t < 30; t++) { Game_Step(&g, kNone); if (g.events & EV_BANK_SOFT) soft = true; if (g.events & EV_BANK) buried = true; }
    CHECK(soft && !buried && g.puck.vel.y <= 0, "bank: a slow puck thuds off it instead");
}

static void test_pass_and_one_timer(void) {
    static Game g;
    Scenario(&g, 7);
    Wake(&g, 0, V2_Make(FX_INT(0), FX_INT(60)), V2_Make(FX_ONE, 0));
    Wake(&g, 1, V2_Make(FX_INT(200), FX_INT(-40)), V2_Make(FX_ONE, 0));
    g.puck.carrier = 0;
    g.puck.lastTouch = 0;
    g.sk[0].player = 0; g.slotSkater[0] = 0;
    g.sk[1].player = 1; g.slotSkater[1] = 1;
    Steps(&g, 1);
    /* aim up and right at the teammate */
    PadInput in[MAX_PLAYERS] = {{28, BTN_PASS}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, in);
    CHECK((g.events & EV_PASS) && g.puck.passTo == 1, "pass: to the teammate the stick points at");
    bool got = false;
    for (int t = 0; t < 80 && !got; t++) { Game_Step(&g, kNone); if (g.puck.carrier == 1) got = true; }
    CHECK(got && g.puck.lastTouch == 1 && g.puck.prevTouch == 0, "pass: received, and the passer is in line for the assist");

    /* the same pass, but the receiver presses Shoot as it arrives: a one-timer */
    Scenario(&g, 8);
    Wake(&g, 0, V2_Make(FX_INT(100), FX_INT(80)), V2_Make(FX_ONE, 0));
    Wake(&g, 1, V2_Make(FX_INT(230), FX_INT(-20)), V2_Make(FX_ONE, 0));
    g.puck.carrier = 0; g.puck.lastTouch = 0;
    g.sk[0].player = 0; g.slotSkater[0] = 0;
    g.sk[1].player = 1; g.slotSkater[1] = 1;
    Steps(&g, 1);
    Game_Step(&g, in);
    bool one = false, goal = false;
    for (int t = 0; t < 90; t++) {
        PadInput hold[MAX_PLAYERS] = {{0, 0}, {0, (uint8_t)((t % 6) < 3 ? BTN_SHOOT : 0)}, {0, 0}, {0, 0}};
        Game_Step(&g, hold);
        if (g.events & EV_ONE_TIMER) one = true;
        if (g.events & EV_GOAL) goal = true;
    }
    CHECK(one, "one-timer: Shoot pressed as the pass arrives fires it straight on");
    CHECK(goal && g.lastScorer == 1 && g.lastAssist == 0, "one-timer: from the slot it scores, with the assist");
}

static void test_check(void) {
    static Game g;
    Scenario(&g, 9);
    /* team 1's carrier skating left; team 0's big skater charging into them */
    Wake(&g, 3, V2_Make(0, 0), V2_Make(-FX_ONE, 0));
    g.puck.carrier = 3; g.puck.lastTouch = 3;
    Wake(&g, 2, V2_Make(FX_INT(-30), 0), V2_Make(FX_ONE, 0));
    g.sk[2].build = BUILD_BIG;
    g.sk[2].vel = V2_Make(FX_INT(3), 0);
    g.sk[2].player = 0; g.slotSkater[0] = 2;
    int knocked = 0, tries = 0;
    for (uint64_t seed = 0; seed < 40; seed++) {
        Game t = g;
        Rng_Seed(&t.rng, seed + 100);
        PadInput in[MAX_PLAYERS] = {{1, BTN_CHECK}, {0, 0}, {0, 0}, {0, 0}};
        Game_Step(&t, in);
        tries++;
        if ((t.events & EV_CHECK) && t.puck.carrier == -1 && t.sk[3].stun > 0) knocked++;
    }
    CHECK(knocked > tries / 3 && knocked < tries, "check: a big skater's charge usually knocks the carrier off the puck, not always");

    /* nobody in reach: a player switches to the teammate nearest the puck */
    Scenario(&g, 10);
    Wake(&g, 0, V2_Make(FX_INT(-300), FX_INT(-100)), V2_Make(FX_ONE, 0));
    Wake(&g, 1, V2_Make(FX_INT(50), 0), V2_Make(FX_ONE, 0));
    Wake(&g, 2, V2_Make(FX_INT(-200), FX_INT(100)), V2_Make(FX_ONE, 0));
    g.puck.pos = V2_Make(FX_INT(80), 0);
    g.sk[0].player = 0; g.slotSkater[0] = 0;
    g.sk[2].player = 1; g.slotSkater[1] = 2; /* another player on the same team */
    PadInput in[MAX_PLAYERS] = {{0, BTN_CHECK}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, in);
    CHECK((g.events & EV_SWITCH) && g.slotSkater[0] == 1 && g.sk[1].player == 0 && g.sk[0].player == -1,
          "switch: Check with nobody to hit takes over the teammate nearest the puck");
    CHECK(g.slotSkater[1] == 2 && g.sk[2].player == 1, "switch: never the skater another player is steering");
}

static void test_faceoff(void) {
    GameSetup s;
    Setup(&s, 11, 1, 0);
    static Game g;
    Game_Init(&g, &s);
    CHECK(g.phase == PH_FACEOFF && g.sk[g.centre[0]].player == 0, "faceoff: the player takes the faceoff");
    CHECK(V2_IsZero(g.puck.pos) && g.puck.carrier == -1, "faceoff: puck at centre ice");
    /* jump the drop */
    PadInput go[MAX_PLAYERS] = {{0, BTN_PASS}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, go);
    CHECK((g.events & EV_FALSE_START) && g.falseStart[0] > 0, "faceoff: going before the drop is a false start");
    while (!Game_PuckDropped(&g)) Game_Step(&g, kNone);
    Game_Step(&g, go);
    CHECK(g.phase == PH_FACEOFF, "faceoff: and the false starter can't win straight after the drop");
    /* the computer centre takes it */
    for (int t = 0; t < 80 && g.phase == PH_FACEOFF; t++) Game_Step(&g, kNone);
    CHECK(g.phase == PH_PLAY && g.puck.carrier == g.centre[1], "faceoff: the computer wins it when the player was penalised");

    /* a clean, quick press after the drop wins */
    Game_Init(&g, &s);
    while (!Game_PuckDropped(&g)) Game_Step(&g, kNone);
    Game_Step(&g, kNone);
    Game_Step(&g, go);
    CHECK(g.phase == PH_PLAY && g.puck.carrier == g.centre[0] && (g.events & EV_FACEOFF_WON), "faceoff: first to press after the drop wins it");
}

static void test_join_leave(void) {
    GameSetup s;
    Setup(&s, 12, 1, 0);
    static Game g;
    Game_Init(&g, &s);
    CHECK(Game_JoinPlayer(&g, 1, 0) && g.slotSkater[1] >= 0 && Game_TeamOf(g.slotSkater[1]) == 0, "join: a second player drops in on the crew");
    CHECK(Game_JoinPlayer(&g, 2, 0) && !Game_JoinPlayer(&g, 3, 0), "join: three players fill the crew, a fourth can't join it");
    CHECK(Game_JoinPlayer(&g, 3, 1) && Game_TeamOf(g.slotSkater[3]) == 1, "join: but can join the other team");
    int had = g.slotSkater[1];
    CHECK(Game_LeavePlayer(&g, 1) && g.sk[had].player == -1 && g.slotSkater[1] == -1, "leave: the computer takes the skater back");
    CHECK(!Game_LeavePlayer(&g, 1), "leave: twice is refused");
}


/* ------------------------------------------------------------ the rinks */

static void test_rink_and_goalies(void) {
    static Game g;
    ScenarioAt(&g, 21, VENUE_RINK);
    CHECK(g.ice.boards && g.ice.goalies && g.ice.slapshots && !g.ice.snow, "rink: boards, goalies, slapshots, no snow");
    CHECK(g.gk[0].present && g.gk[1].present, "rink: a goalie in each net");
    /* a hard puck into the boards bounces back instead of burying */
    g.gk[0].present = g.gk[1].present = 0;
    g.puck.pos = V2_Make(0, g.ice.hw - FX_INT(40));
    g.puck.vel = V2_Make(0, FX_INT(9));
    bool boards = false, buried = false;
    for (int t = 0; t < 30; t++) { Game_Step(&g, kNone); if (g.events & EV_BOARDS) boards = true; if (g.events & EV_BANK) buried = true; }
    CHECK(boards && !buried && g.puck.vel.y < 0, "rink: the boards bounce a hard puck back");

    /* a shot straight at the goalie is saved */
    ScenarioAt(&g, 22, VENUE_RINK);
    for (int t = 0; t < 30; t++) Game_Step(&g, kNone); /* let the goalies settle */
    Wake(&g, 0, V2_Make(FX_INT(200), 0), V2_Make(FX_ONE, 0));
    g.puck.carrier = 0; g.puck.lastTouch = 0;
    g.sk[0].player = 0; g.slotSkater[0] = 0;
    Steps(&g, 2);
    /* tap Shoot (a rink wind-up let go at once: a wrist shot), stick straight ahead */
    PadInput tap[MAX_PLAYERS] = {{1, BTN_SHOOT}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, tap);
    CHECK(g.sk[0].charge > 0 && !(g.events & EV_WINDUP), "slapshot: pressing Shoot on a rink starts winding up, quietly until it's a slapshot");
    PadInput aim[MAX_PLAYERS] = {{1, 0}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&g, aim);
    CHECK((g.events & EV_SHOT) && !(g.events & EV_SLAPSHOT), "slapshot: let go at once, it's a wrist shot");
    bool saved = false, goal = false;
    for (int t = 0; t < 80; t++) { Game_Step(&g, kNone); if (g.events & EV_SAVE) saved = true; if (g.events & EV_GOAL) goal = true; }
    CHECK(saved && !goal && g.gk[1].saves >= 1, "goalie: a shot straight at him is saved");

    /* a slow puck at the goalie is held: whistle, then a faceoff */
    ScenarioAt(&g, 23, VENUE_RINK);
    for (int t = 0; t < 30; t++) Game_Step(&g, kNone);
    g.puck.pos = V2_Add(g.gk[1].pos, V2_Make(FX_INT(-40), 0));
    g.puck.vel = V2_Make(FX_INT(2), 0);
    bool froze = false;
    for (int t = 0; t < 40 && !froze; t++) { Game_Step(&g, kNone); if (g.events & EV_FREEZE) froze = true; }
    CHECK(froze && g.phase == PH_STOPPAGE && g.puck.heldBy == 1, "goalie: a slow puck is held, play stops");
    for (int t = 0; t < 80 && g.phase == PH_STOPPAGE; t++) Game_Step(&g, kNone);
    CHECK(g.phase == PH_FACEOFF, "goalie: then a faceoff");
}

static void test_slapshot(void) {
    static Game a, b;
    fx speeds[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        Game *g = k ? &b : &a;
        ScenarioAt(g, 24, VENUE_RINK);
        g->gk[0].present = g->gk[1].present = 0;
        Wake(g, 0, V2_Make(0, 0), V2_Make(FX_ONE, 0));
        g->puck.carrier = 0; g->puck.lastTouch = 0;
        g->sk[0].player = 0; g->slotSkater[0] = 0;
        Steps(g, 2);
        PadInput hold[MAX_PLAYERS] = {{1, BTN_SHOOT}, {0, 0}, {0, 0}, {0, 0}};
        int ticks = k ? 40 : 1, windups = 0;
        for (int t = 0; t < ticks; t++) { Game_Step(g, hold); if (g->events & EV_WINDUP) windups++; }
        CHECK(k == 0 || g->sk[0].charge >= 30, "slapshot: holding Shoot winds it up");
        CHECK(windups == (k ? 1 : 0), "slapshot: the wind-up is announced once, when it becomes a slapshot");
        PadInput let[MAX_PLAYERS] = {{1, 0}, {0, 0}, {0, 0}, {0, 0}};
        Game_Step(g, let);
        CHECK(g->events & EV_SHOT, "slapshot: letting go shoots");
        if (k) CHECK(g->events & EV_SLAPSHOT, "slapshot: a long wind-up is a slapshot");
        speeds[k] = V2_Len(g->puck.vel);
    }
    CHECK(speeds[1] > Fx_Mul(speeds[0], FX_RATIO(14, 10)), "slapshot: much harder than a wrist shot");

    /* a pond has no slapshots: Shoot shoots at once */
    static Game p;
    Scenario(&p, 25);
    Wake(&p, 0, V2_Make(0, 0), V2_Make(FX_ONE, 0));
    p.puck.carrier = 0; p.puck.lastTouch = 0;
    p.sk[0].player = 0; p.slotSkater[0] = 0;
    Steps(&p, 2);
    PadInput tap[MAX_PLAYERS] = {{1, BTN_SHOOT}, {0, 0}, {0, 0}, {0, 0}};
    Game_Step(&p, tap);
    CHECK((p.events & EV_SHOT) && !(p.events & EV_WINDUP), "pond: wrist shots only, straight away");
}

static void test_traits(void) {
    /* a Cannon's wrist shot is harder than the same skater's without it */
    fx speed[2];
    for (int k = 0; k < 2; k++) {
        static Game g;
        Scenario(&g, 26);
        Wake(&g, 0, V2_Make(0, 0), V2_Make(FX_ONE, 0));
        g.sk[0].trait = k ? TRAIT_CANNON : TRAIT_NONE;
        g.puck.carrier = 0; g.puck.lastTouch = 0;
        g.sk[0].player = 0; g.slotSkater[0] = 0;
        Steps(&g, 1);
        PadInput tap[MAX_PLAYERS] = {{1, BTN_SHOOT}, {0, 0}, {0, 0}, {0, 0}};
        Game_Step(&g, tap);
        speed[k] = V2_Len(g.puck.vel);
    }
    CHECK(speed[1] > speed[0], "traits: a Cannon shoots harder");
    CHECK(strcmp(Game_TraitName(TRAIT_SNIPER), "Sniper") == 0 && Game_TraitWords(TRAIT_WALL)[0] != '\0', "traits: names and words");
}

static void test_twists(void) {
    static Game g;
    GameSetup s;
    Setup(&s, 27, 0, 0);
    s.twist = TWIST_WIND;
    Game_Init(&g, &s);
    CHECK(!V2_IsZero(g.wind), "twist: wind blows");
    g.phase = PH_PLAY;
    for (int i = 0; i < SKATERS; i++) { g.sk[i].stun = 30000; g.sk[i].pos = V2_Make(FX_INT(i % 2 ? 250 : -250), FX_INT(i < 3 ? -150 : 150)); }
    g.puck.carrier = -1;
    g.puck.pos = V2_Make(0, 0);
    g.puck.vel = V2_Make(0, 0);
    Steps(&g, 40);
    CHECK(!V2_Within(g.puck.pos, V2_Make(0, 0), FX_INT(1)), "twist: the wind moves a puck lying still");

    Setup(&s, 28, 0, 0);
    s.twist = TWIST_TINY_NETS;
    Game_Init(&g, &s);
    Ice normal;
    Game_IceFor(VENUE_POND, TWIST_NONE, &normal);
    CHECK(g.ice.netHalfW < normal.netHalfW, "twist: tiny nets are narrower");

    Setup(&s, 29, 0, 0);
    s.twist = TWIST_BLIZZARD;
    Game_Init(&g, &s);
    static Game calm;
    Setup(&s, 29, 0, 0);
    Game_Init(&calm, &s);
    CHECK(g.snowPerTick > calm.snowPerTick, "twist: a blizzard snows faster");
    CHECK(strcmp(Game_TwistName(TWIST_GLASS_ICE), "Glass ice") == 0, "twist: names");
}

static void test_shootout(void) {
    GameSetup s;
    Setup(&s, 30, 1, 0);
    s.mode = MODE_SHOOTOUT;
    s.venue = VENUE_POND; /* asks for the pond: a shootout needs a goalie, so it moves to the rink */
    static Game g;
    Game_Init(&g, &s);
    CHECK(g.setup.venue == VENUE_RINK && g.gk[1].present && !g.gk[0].present, "shootout: a rink, one goalie");
    int benched = 0;
    for (int i = 0; i < SKATERS; i++) benched += g.sk[i].benched;
    CHECK(benched == SKATERS - 1 && g.sk[0].player == 0 && g.puck.carrier == 0, "shootout: just you and the puck");
    CHECK(g.phase == PH_SO_READY, "shootout: a moment to get ready");
    /* stand still until the clock runs out: a miss, three times, and it's over */
    int misses = 0, guard = 0;
    while (g.phase != PH_OVER && guard++ < 60 * 60) { Game_Step(&g, kNone); if (g.events & EV_SO_MISS) misses++; }
    CHECK(g.phase == PH_OVER && misses == 3 && g.soMisses == 3 && g.score[0] == 0, "shootout: three misses and you're out");
    CHECK(g.soAttempts == 3, "shootout: three tries counted");

    /* a computer shooter scores some and misses some: goals and the learning goalie */
    Setup(&s, 31, 0, 0);
    s.mode = MODE_SHOOTOUT;
    s.venue = VENUE_RINK;
    Game_Init(&g, &s);
    guard = 0;
    int goals = 0;
    while (g.phase != PH_OVER && guard++ < 60 * 60 * 10) { Game_Step(&g, kNone); if (g.events & EV_GOAL) goals++; }
    CHECK(g.phase == PH_OVER && goals == g.score[0] && g.soAttempts == g.score[0] + 3, "shootout: every try is a goal or a miss");
    CHECK(g.soBestStreak <= g.score[0], "shootout: a streak never exceeds the goals");

    /* a puck the goalie smothers is a miss too, not a free try */
    Setup(&s, 32, 1, 0);
    s.mode = MODE_SHOOTOUT;
    s.venue = VENUE_RINK;
    Game_Init(&g, &s);
    guard = 0;
    while (g.phase != PH_PLAY && guard++ < 600) Game_Step(&g, kNone);
    g.sk[0].pos = V2_Make(-FX_INT(200), 0);
    g.sk[0].noPickup = 1000;
    g.puck.carrier = -1;
    g.puck.flight = FLIGHT_NONE;
    g.puck.pos = V2_Add(g.gk[1].pos, V2_Make(-FX_INT(20), 0));
    g.puck.vel = V2_Make(FX_INT(2), 0);
    int missesBefore = g.soMisses;
    bool froze = false, counted = false;
    for (int t = 0; t < 120 && !froze; t++) {
        Game_Step(&g, kNone);
        if (g.events & EV_FREEZE) { froze = true; counted = (g.events & EV_SO_MISS) != 0; }
    }
    CHECK(froze && counted && g.soMisses == missesBefore + 1 && g.soStreak == 0 && g.phase == PH_SO_RESULT,
          "shootout: a puck the goalie holds is a miss");
}

static void test_venue_games(void) {
    for (int venue = VENUE_RINK; venue <= VENUE_ARENA; venue++) {
        int ended = 0, goals = 0, saves = 0, games = 0;
        for (uint64_t seed = 1; seed <= 12; seed++) {
            GameSetup s;
            Setup(&s, seed * 104729 + (uint64_t)venue, 0, 0);
            s.venue = (uint8_t)venue;
            s.periodSeconds = 90;
            s.teamLevel[0] = s.teamLevel[1] = (uint8_t)(seed % DIFF_COUNT);
            s.goalieStyle[0] = (uint8_t)(seed % GOALIE_STYLE_COUNT);
            s.goalieStyle[1] = (uint8_t)((seed + 1) % GOALIE_STYLE_COUNT);
            static Game g;
            Game_Init(&g, &s);
            int t = 0;
            while (g.phase != PH_OVER && t < 90 * 60 * 2 * 8) { Game_Step(&g, kNone); t++; }
            games++;
            ended += g.phase == PH_OVER;
            goals += g.score[0] + g.score[1];
            saves += g.gk[0].saves + g.gk[1].saves;
        }
        printf("  %s: %d games, %.1f goals and %.1f saves a game\n", Game_VenueName(venue), games, (double)goals / games, (double)saves / games);
        CHECK(ended == games, "venues: every rink and arena game finishes");
        CHECK(goals >= games && goals <= games * 14, "venues: goals happen past the goalies, not too many");
        CHECK(saves >= games * 2, "venues: the goalies make saves");
    }
}

static void test_rink_rollback(void) {
    GameSetup s;
    Setup(&s, 4343, 2, 1);
    s.venue = VENUE_ARENA;
    static Game g, saved;
    Game_Init(&g, &s);
    sLcg = 17;
    PadInput in[MAX_PLAYERS];
    for (int t = 0; t < 2500; t++) { RandomInputs(in); Game_Step(&g, in); }
    saved = g;
    uint32_t at = sLcg;
    for (int t = 0; t < 700; t++) { RandomInputs(in); Game_Step(&g, in); }
    uint64_t ahead = Game_Hash(&g);
    g = saved;
    sLcg = at;
    for (int t = 0; t < 700; t++) { RandomInputs(in); Game_Step(&g, in); }
    CHECK(Game_Hash(&g) == ahead, "rollback: an arena game with goalies and slapshots restores and replays exactly");
}

/* ------------------------------------------------------------ whole games */

static int PlayOut(Game *g, int maxTicks) {
    int t = 0;
    while (g->phase != PH_OVER && t < maxTicks) { Game_Step(g, kNone); t++; }
    return t;
}

static void test_periods(void) {
    GameSetup s;
    Setup(&s, 13, 0, 0);
    s.periodSeconds = 30;
    static Game g;
    Game_Init(&g, &s);
    bool sawBreak = false, snowed = false, cleared = false;
    fx snowAtEnd = 0;
    for (int t = 0; t < 30 * 60 * 3 && g.phase != PH_OVER; t++) {
        uint8_t was = g.phase;
        Game_Step(&g, kNone);
        if (g.snow > FX_RATIO(1, 10)) snowed = true;
        if (was == PH_PLAY && g.phase == PH_BREAK) { sawBreak = true; snowAtEnd = g.snow; }
        if (g.events & EV_BREAK_DONE) cleared = g.snow == 0 && g.period >= 2;
    }
    CHECK(snowed && snowAtEnd > FX_RATIO(4, 10), "periods: snow builds up over a period");
    CHECK(sawBreak && cleared, "periods: a shovel break between periods clears it");

    /* level at the end: sudden death, and the next goal wins */
    Setup(&s, 14, 0, 0);
    s.periodSeconds = 30;
    s.periods = 1;
    Game_Init(&g, &s);
    g.score[0] = g.score[1] = 0;
    g.clock = 1;
    g.phase = PH_PLAY;
    g.puck.pos = V2_Make(0, FX_INT(150));
    g.puck.carrier = -1;
    Game_Step(&g, kNone);
    int guard = 0;
    while (g.phase == PH_BREAK && guard++ < 400) Game_Step(&g, kNone);
    CHECK(Game_SuddenDeath(&g) && Game_ClockSeconds(&g) == 0, "periods: level after the last period goes to sudden death");
    PlayOut(&g, 60 * 60 * 10);
    CHECK(g.phase == PH_OVER && g.score[g.winner] == g.score[1 - g.winner] + 1, "periods: and ends on the next goal");
}

static void test_computer_games(void) {
    int games = 0, ended = 0, goals = 0, maxGoals = 0, oneSided = 0;
    int wins[2] = {0, 0};
    long stalls = 0;
    for (uint64_t seed = 1; seed <= 30; seed++) {
        GameSetup s;
        Setup(&s, seed * 7919, 0, 0);
        s.periodSeconds = 90;
        s.teamLevel[0] = s.teamLevel[1] = (uint8_t)(seed % DIFF_COUNT);
        static Game g;
        Game_Init(&g, &s);
        int still = 0;
        V2 last = g.puck.pos;
        int t = 0;
        while (g.phase != PH_OVER && t < 90 * 60 * 2 * 6) {
            Game_Step(&g, kNone);
            t++;
            if (g.phase == PH_PLAY) {
                if (V2_Within(g.puck.pos, last, FX_INT(1))) still++;
                else still = 0;
                if (still == 60 * 20) stalls++;
            }
            last = g.puck.pos;
        }
        games++;
        if (g.phase == PH_OVER) { ended++; wins[g.winner]++; }
        int total = g.score[0] + g.score[1];
        goals += total;
        if (total > maxGoals) maxGoals = total;
        if (g.score[0] == 0 || g.score[1] == 0) oneSided++;
    }
    printf("  computer games: %d played, %d finished, %.1f goals a game (most %d), wins %d-%d, %d shut-outs\n",
           games, ended, (double)goals / games, maxGoals, wins[0], wins[1], oneSided);
    CHECK(ended == games, "computer games: every game finishes");
    CHECK(stalls == 0, "computer games: the puck never sits still for 20 seconds in play");
    CHECK(goals >= games * 2 && goals <= games * 16, "computer games: a believable number of goals");
    CHECK(wins[0] > 0 && wins[1] > 0, "computer games: both ends win some");
}

#include "test_journey.c"
#include "test_net.c"
#include "test_crews.c"

int main(void) {
    test_fixed();
    test_determinism();
    test_rollback();
    test_containment();
    test_goal();
    test_net_back();
    test_snowbank();
    test_pass_and_one_timer();
    test_check();
    test_faceoff();
    test_join_leave();
    test_periods();
    test_computer_games();
    test_rink_and_goalies();
    test_slapshot();
    test_traits();
    test_twists();
    test_shootout();
    test_venue_games();
    test_rink_rollback();
    test_journey_basics();
    test_journey_road();
    test_journey_file();
    test_wire_packets();
    test_session_online();
    test_session_ticks_once();
    test_crews_text();
    test_crews_style();
    test_crews_files();
    test_net_plumbing();
    test_netmsg();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
