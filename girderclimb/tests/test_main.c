/* Headless correctness tests for Girderclimb's rules (game.c). No raylib. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/game.h"

static int gChecks = 0, gFailures = 0;
#define CHECK(cond, msg) do { gChecks++; if (!(cond)) { gFailures++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static Game sG;
static Game *G(void) { return &sG; }
static void Run(int n) { for (int i = 0; i < n; i++) Game_Step(G()); }
static void Input(int mx, int my, bool grapple, bool jump) { Game_SetInput(G(), mx, my, grapple, jump); }
static float Dist(float x0, float y0, float x1, float y1) { return sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)); }

static void Fresh(uint64_t seed, int level) {
    Game_Init(G(), seed, 0);
    if (level != 1) Game_StartLevel(G(), level);
    G()->state = LS_PLAY;
    G()->fireSpeed = 0.0f; /* keep the fire out of the way unless a test wants it */
    G()->fireY = 1e6f;
}

/* Puts the game in a small controlled world: no ledges, one ring at (280, 400). */
static void Sandbox(void) {
    Fresh(1, 1);
    Game *g = G();
    g->ledgeCount = 0;
    g->anchorCount = 1;
    g->anchors[0] = (Anchor){280.0f, 400.0f, false};
    g->orbCount = 0;
    g->p = (Player){280.0f, 480.0f, 0.0f, 0.0f, P_AIR, -1, -1, ROPE_MIN, 1};
    g->groundY = 5000.0f;
}

static void test_free_fall(void) {
    Sandbox();
    G()->p.y = 100.0f; G()->p.vx = 0.0f;
    Run(120);
    CHECK(fabsf(G()->p.y - (100.0f + 0.5f * GRAVITY)) < 6.0f, "fall: y follows 1/2 g t^2");
    CHECK(fabsf(G()->p.vy - GRAVITY) < 1.0f, "fall: vy = g t");
    Sandbox();
    G()->p.vx = 100.0f; G()->p.x = 100.0f; G()->p.y = 100.0f;
    Run(60);
    CHECK(G()->p.x > 100.0f + 40.0f && G()->p.x < 100.0f + 50.0f, "fall: a little air drag on sideways speed, none downward");
}

static void test_grapple_targeting(void) {
    Sandbox();
    Game *g = G();
    g->anchorCount = 4;
    g->anchors[0] = (Anchor){280.0f, 400.0f, false};   /* 80 above */
    g->anchors[1] = (Anchor){400.0f, 420.0f, false};   /* up and right */
    g->anchors[2] = (Anchor){150.0f, 430.0f, false};   /* up and left */
    g->anchors[3] = (Anchor){280.0f, 600.0f, false};   /* below */
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 0) == 0, "target: the nearest ring above");
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 1) == 0 || Game_PickAnchor(g, 280.0f, 480.0f, 1) == 1, "target: leaning right can favour the right");
    g->anchors[0].y = 470.0f; /* same distance-ish; make 1 clearly nearer to the right */
    g->anchors[0] = (Anchor){280.0f, 300.0f, false};   /* 180 above */
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 1) == 1, "target: holding right takes the ring to the right");
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, -1) == 2, "target: holding left takes the one on the left");
    CHECK(Game_PickAnchor(g, 280.0f, 350.0f, 0) != 3, "target: rings below you are ignored");
    g->p.lastAnchor = 1;
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 1) != 1, "target: not the ring you just left");
    g->p.lastAnchor = -1;
    CHECK(Game_PickAnchor(g, 20.0f, 2000.0f, 0) == -1, "target: nothing in range, nothing chosen");
    CHECK(Game_PickAnchor(g, 280.0f, 400.0f + 200.0f, 0) >= -1, "target: (range edge)");
    float far = GRAPPLE_RANGE + 5.0f;
    g->anchors[0] = (Anchor){280.0f, 480.0f - far, false};
    g->anchorCount = 1;
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 0) == -1, "target: just out of range");
    g->anchors[0].y = 480.0f - (GRAPPLE_RANGE - 5.0f);
    CHECK(Game_PickAnchor(g, 280.0f, 480.0f, 0) == 0, "target: just inside");

    /* Firing and releasing through the buttons. */
    Sandbox();
    Input(0, 0, true, false);
    Run(1);
    CHECK(G()->p.mode == P_ROPE && G()->p.anchor == 0 && G()->justAttach, "fire: attaches to the ring");
    CHECK(fabsf(G()->p.ropeLen - Dist(280.0f, 480.0f, 280.0f, 400.0f)) < 1.0f, "fire: rope as long as the gap");
    Input(0, 0, true, false);
    Run(3);
    CHECK(G()->p.mode == P_ROPE, "fire: holding the button doesn't release");
    Input(0, 0, false, false);
    Run(1);
    Input(0, 0, true, false);
    Run(1);
    CHECK(G()->p.mode == P_AIR && G()->justRelease && G()->p.lastAnchor == 0, "fire: pressing again lets go");
    Sandbox();
    G()->anchors[0].y = -5000.0f;
    Input(0, 0, true, false);
    Run(1);
    CHECK(G()->p.mode == P_AIR && G()->justFire && !G()->justAttach, "fire: nothing to hit, nothing happens");
}

static void test_rope_physics(void) {
    Sandbox();
    Game *g = G();
    g->p.x = 280.0f + 80.0f; g->p.y = 400.0f; /* level with the ring, off to the side */
    g->p.mode = P_ROPE; g->p.anchor = 0; g->p.ropeLen = 80.0f;
    float maxDist = 0.0f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 120 * 6; i++) {
        Run(1);
        float d = Dist(g->p.x, g->p.y, 280.0f, 400.0f);
        if (d > maxDist) maxDist = d;
        if (g->p.y < minY) minY = g->p.y;
        if (g->p.y > maxY) maxY = g->p.y;
    }
    CHECK(maxDist <= 80.0f + 0.05f, "rope: you never get further than its length");
    CHECK(maxY > 400.0f + 60.0f, "rope: it swings down under the ring");
    CHECK(minY > 400.0f - 3.0f, "rope: with no help it never swings higher than where it started");

    /* Pumping adds energy. */
    Sandbox();
    g = G();
    g->p.x = 280.0f + 20.0f; g->p.y = 400.0f + 78.0f;
    g->p.mode = P_ROPE; g->p.anchor = 0; g->p.ropeLen = 80.0f;
    for (int i = 0; i < 120 * 6; i++) {
        Input(g->p.vx >= 0.0f ? 1 : -1, 0, false, false);
        Run(1);
    }
    CHECK(g->p.y < 400.0f + 40.0f, "rope: pumping with the swing builds it up");

    /* Reeling. */
    Sandbox();
    g = G();
    g->p.x = 280.0f; g->p.y = 600.0f; g->p.mode = P_ROPE; g->p.anchor = 0; g->p.ropeLen = 200.0f;
    Input(0, -1, false, false);
    Run(60);
    CHECK(fabsf(g->p.ropeLen - (200.0f - REEL_SPEED * 0.5f)) < 1.0f, "reel: in at REEL_SPEED");
    Run(200);
    CHECK(g->p.ropeLen == ROPE_MIN && g->p.y < 400.0f + ROPE_MIN + 8.0f, "reel: stops at the minimum, you are pulled up to the ring");
    Input(0, 1, false, false);
    Run(300);
    CHECK(g->p.ropeLen == ROPE_MAX, "reel: out to the maximum and no further");

    /* Release keeps your speed. */
    Sandbox();
    g = G();
    g->p.x = 280.0f + 80.0f; g->p.y = 400.0f; g->p.mode = P_ROPE; g->p.anchor = 0; g->p.ropeLen = 80.0f;
    Run(70);
    float vx = g->p.vx, vy = g->p.vy;
    Input(0, 0, true, false);
    Run(1);
    CHECK(g->p.mode == P_AIR && fabsf(g->p.vx - vx) < 20.0f && fabsf(g->p.vy - vy) < 20.0f, "release: you fly off with the speed you had");

    /* Ring points, once. */
    Sandbox();
    g = G();
    g->p.x = 280.0f; g->p.y = 428.0f; g->p.mode = P_ROPE; g->p.anchor = 0; g->p.ropeLen = 30.0f;
    long s0 = g->score;
    Run(2);
    CHECK(g->anchors[0].touched && g->score - s0 >= RING_POINTS && g->justRing, "ring: close to it counts, for points");
    s0 = g->score;
    Run(120);
    CHECK(g->score - s0 < RING_POINTS, "ring: only once");
}

static void test_ledges_and_walking(void) {
    Fresh(3, 1);
    Game *g = G();
    CHECK(g->p.mode == P_STAND && fabsf(g->p.y + PLAYER_R - g->groundY) < 0.01f, "stand: you start on the floor");
    g->p.x = 280.0f;
    Input(1, 0, false, false);
    float x0 = g->p.x;
    Run(120);
    CHECK(fabsf((g->p.x - x0) - WALK_SPEED) < 1.0f, "walk: WALK_SPEED a second");
    Input(0, 0, false, true);
    Run(1);
    CHECK(g->p.mode == P_AIR && g->justJump, "jump: leaves the floor");
    float minY = g->p.y;
    for (int i = 0; i < 200 && g->p.mode == P_AIR; i++) { Run(1); if (g->p.y < minY) minY = g->p.y; }
    CHECK(fabsf((g->groundY - PLAYER_R - minY) - JUMP_SPEED * JUMP_SPEED / (2.0f * GRAVITY)) < 3.0f, "jump: the height v^2 / 2g");
    CHECK(g->p.mode == P_STAND && g->justLand, "jump: and back on the floor");
    Input(0, 0, false, false);
    Run(1);
    Input(0, 0, false, true);
    Run(1);
    Input(0, 0, false, true);
    Run(30);
    CHECK(g->p.mode == P_AIR || g->p.mode == P_STAND, "jump: holding the key doesn't re-jump mid-air");

    /* Walking off the end of a ledge is a fall. */
    Fresh(3, 1);
    g = G();
    g->ledgeCount = 2;
    g->ledges[1] = (Ledge){100.0f, g->groundY - 200.0f, 80.0f, false, false, false};
    g->p.x = 170.0f; g->p.y = g->ledges[1].y - PLAYER_R; g->p.mode = P_STAND;
    Input(1, 0, false, false);
    Run(60);
    CHECK(g->p.mode == P_AIR && g->p.x > 180.0f, "ledge: walk off the end and you fall");
    Input(0, 0, false, false);
    Run(200);
    CHECK(g->p.mode == P_STAND && g->p.y > g->ledges[1].y, "ledge: onto whatever is below");

    /* One-way: you can rise through a ledge from below, then land on it. */
    Fresh(3, 1);
    g = G();
    g->ledgeCount = 2;
    g->ledges[1] = (Ledge){200.0f, g->groundY - 100.0f, 160.0f, false, false, false};
    g->p.x = 280.0f; g->p.y = g->groundY - 60.0f; g->p.vy = -520.0f; g->p.mode = P_AIR;
    Run(20);
    CHECK(g->p.y < g->ledges[1].y - 10.0f, "ledge: you can go up through it");
    Run(120);
    CHECK(g->p.mode == P_STAND && fabsf(g->p.y + PLAYER_R - g->ledges[1].y) < 0.01f, "ledge: and land on top when you come down");
}

static void test_walls(void) {
    Sandbox();
    G()->p.x = 40.0f; G()->p.y = 200.0f; G()->p.vx = -300.0f;
    Run(30);
    CHECK(G()->p.x >= PLAYER_R, "wall: you stay inside the shaft");
    Sandbox();
    G()->p.x = 30.0f; G()->p.y = 200.0f; G()->p.vx = -300.0f;
    float vx = 0.0f;
    for (int i = 0; i < 20; i++) { Run(1); if (G()->p.vx > 0.0f) { vx = G()->p.vx; break; } }
    CHECK(vx > 0.0f && vx < 300.0f * WALL_BOUNCE + 5.0f, "wall: a soft bounce off the side");
    Sandbox();
    G()->p.x = 540.0f; G()->p.vx = 300.0f;
    Run(10);
    CHECK(G()->p.x <= WORLD_W - PLAYER_R + 0.01f, "wall: the other side too");
}

static void test_fire_and_death(void) {
    Fresh(5, 1);
    Game *g = G();
    g->fireSpeed = Game_FireSpeed(1);
    g->fireY = g->groundY + 60.0f;
    float y0 = g->fireY;
    Run(120);
    CHECK(y0 - g->fireY > Game_FireSpeed(1) - 1.0f, "fire: it climbs");
    float y1 = g->fireY;
    Run(120);
    CHECK((y1 - g->fireY) > (y0 - y1) - 0.01f && g->time > 1.9f, "fire: and gets quicker the longer you take");
    CHECK(Game_FireSpeed(6) > Game_FireSpeed(1) && Game_FireSpeed(999) == Game_FireSpeed(12), "fire: quicker on later towers, with a cap");
    Run(120 * 8);
    CHECK(g->state == LS_DYING && g->death == DEATH_FIRE && g->justDeath, "fire: it burns you when it arrives");

    /* A life lost puts you back on your last checkpoint, and the fire below you again. */
    Fresh(5, 1);
    g = G();
    g->checkpoint = 1;
    g->p.x = g->ledges[1].x + 40.0f; g->p.y = g->ledges[1].y - PLAYER_R; g->p.mode = P_STAND;
    g->fireY = g->p.y - 5.0f;
    Run(2);
    CHECK(g->state == LS_DYING, "death: dying");
    for (int i = 0; i < 40; i++) Game_Update(g, 0.05f);
    CHECK(g->lives == START_LIVES - 1 && g->state == LS_PLAY && g->p.mode == P_STAND, "death: a life lost and you are back");
    CHECK(fabsf(g->p.y + PLAYER_R - g->ledges[1].y) < 0.01f && g->fireY > g->ledges[1].y + 200.0f, "death: on the checkpoint ledge, with the fire well below");
    g->lives = 1;
    g->state = LS_DYING; g->stateTimer = 0.05f;
    Game_ConsumeFrameFlags(g);
    for (int i = 0; i < 4; i++) Game_Update(g, 0.05f);
    CHECK(g->phase == GS_GAMEOVER && g->justGameOver, "death: the last life ends the run");
    Game_TogglePause(g);
    CHECK(g->phase == GS_GAMEOVER, "death: pause can't undo it");
    Game_Restart(g, 9, 0);
    CHECK(g->phase == GS_PLAYING && g->level == 1 && g->lives == START_LIVES, "restart: fresh run");

    /* Checkpoints: landing on a ledge sets the respawn point, once, for points. */
    Fresh(5, 1);
    g = G();
    long s0 = g->score;
    g->p.x = g->ledges[1].x + 60.0f; g->p.y = g->ledges[1].y - 40.0f; g->p.vy = 50.0f; g->p.mode = P_AIR;
    Run(30);
    CHECK(g->checkpoint == 1 && g->justCheckpoint && g->score - s0 >= CHECKPOINT_POINTS, "checkpoint: touching a ledge sets it, for points");
}

static void test_orbs(void) {
    Fresh(7, 3);
    Game *g = G();
    CHECK(g->orbCount > 0, "orbs: later towers have them");
    Game_BuildTower(g, 1);
    CHECK(g->orbCount == 0, "orbs: the first tower doesn't");
    Fresh(7, 3);
    g = G();
    g->orbs[0].cy = g->p.y - 200.0f;
    g->p.mode = P_AIR; g->p.x = g->orbs[0].x; g->p.y = g->orbs[0].cy; g->p.vy = 0.0f;
    g->time = 1.0f;
    g->p.x = Game_OrbX(&g->orbs[0], g->time);
    Run(1);
    CHECK(g->state == LS_DYING && g->death == DEATH_ORB, "orbs: touching one is fatal");
    Fresh(7, 3);
    g = G();
    g->orbsEnabled = false;
    g->p.mode = P_AIR; g->p.y = g->orbs[0].cy; g->p.x = Game_OrbX(&g->orbs[0], 0.01f); g->p.vy = 0;
    Run(2);
    CHECK(g->state == LS_PLAY, "orbs: switched off, they do nothing");
    Orb o = {300.0f, 500.0f, 80.0f, 0.5f, 0.0f, 300.0f};
    CHECK(fabsf(Game_OrbX(&o, 0.0f) - 300.0f) < 0.001f && fabsf(Game_OrbX(&o, 3.14159f / 0.5f / 2.0f) - 380.0f) < 0.1f, "orbs: sweep sideways by their amplitude");
}

static void test_towers(void) {
    bool ok = true, chain = true, ledges = true, goal = true, world = true;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        for (int level = 1; level <= 12; level++) {
            Game g;
            Game_Init(&g, seed, 0);
            Game_BuildTower(&g, level);
            if (g.anchorCount < 8 || g.anchorCount > MAX_ANCHORS) ok = false;
            if (fabsf(g.groundY - (Game_TowerHeight(level) - 40.0f)) > 0.01f) ok = false;
            if (Dist(g.anchors[0].x, g.anchors[0].y, g.anchors[0].x, g.groundY - PLAYER_R) > GRAPPLE_RANGE - 20.0f) chain = false;
            for (int i = 1; i < g.anchorCount; i++) {
                if (g.anchors[i].y >= g.anchors[i - 1].y) chain = false;
                if (Dist(g.anchors[i].x, g.anchors[i].y, g.anchors[i - 1].x, g.anchors[i - 1].y) > GRAPPLE_RANGE - 45.0f) chain = false;
            }
            for (int i = 0; i < g.anchorCount; i++) if (g.anchors[i].x < 30 || g.anchors[i].x > WORLD_W - 30 || g.anchors[i].y < 0) world = false;
            for (int i = 1; i < g.ledgeCount; i++) if (g.ledges[i].y >= g.ledges[i - 1].y) ledges = false;
            if (!g.ledges[g.ledgeCount - 1].goal || g.ledges[g.ledgeCount - 1].y != g.towerTop) goal = false;
            const Anchor *top = &g.anchors[g.anchorCount - 1];
            if (top->y >= g.towerTop || top->x < g.ledges[g.ledgeCount - 1].x || top->x > g.ledges[g.ledgeCount - 1].x + g.ledges[g.ledgeCount - 1].w) goal = false;
            for (int i = 0; i < g.ledgeCount; i++) if (g.ledges[i].x < 0 || g.ledges[i].x + g.ledges[i].w > WORLD_W + 0.01f) world = false;
        }
    }
    CHECK(ok, "tower: anchors and ground as specified");
    CHECK(chain, "tower: every ring is within easy reach of the one before, and the first of the floor");
    CHECK(ledges && world, "tower: ledges in order and everything inside the shaft");
    CHECK(goal, "tower: a goal ledge with a ring above it");
    Game a, b;
    Game_Init(&a, 11, 0); Game_Init(&b, 11, 0);
    CHECK(memcmp(a.anchors, b.anchors, sizeof(a.anchors)) == 0, "tower: the same seed builds the same tower");
    Game_Init(&b, 12, 0);
    CHECK(memcmp(a.anchors, b.anchors, sizeof(a.anchors)) != 0, "tower: a different seed builds a different one");
    Game_BuildTower(&a, 2);
    CHECK(a.towerTop < a.groundY && Game_TowerHeight(3) > Game_TowerHeight(2), "tower: later ones are taller");
}

/* A bot that climbs by reeling: fire, reel in, let go near the ring, fire again. */
static void BotStep(Game *g) {
    bool want = false;
    int my = 0, mx = 0;
    Player *p = &g->p;
    if (p->mode == P_ROPE) {
        my = -1;
        const Anchor *a = &g->anchors[p->anchor];
        float letGo = p->anchor == g->anchorCount - 1 ? 33.0f : 60.0f; /* the last ring: all the way up */
        if (Dist(p->x, p->y, a->x, a->y) < letGo) want = true; /* let go while still rising */
    } else {
        int next = Game_PickAnchor(g, p->x, p->y, 0);
        want = next >= 0;
        if (p->mode == P_STAND && next < 0) mx = 0;
    }
    Game_SetInput(g, mx, my, want && !g->grappleHeld, false);
}

static int Climb(uint64_t seed, int level, bool orbs, float *secondsOut) {
    Game g;
    Game_Init(&g, seed, 0);
    Game_StartLevel(&g, level);
    g.orbsEnabled = orbs;
    g.state = LS_PLAY;
    for (int i = 0; i < 120 * 200; i++) {
        BotStep(&g);
        Game_Step(&g);
        if (g.state == LS_CLEAR) { if (secondsOut) *secondsOut = g.time; return 1; }
        if (g.state == LS_DYING) return 0;
    }
    return 0;
}

static void test_bot_climbs(void) {
    int cleared = 0, tried = 0, worstMs = 0;
    for (uint64_t seed = 1; seed <= 30; seed++) {
        for (int level = 1; level <= 6; level++) {
            float secs = 0.0f;
            tried++;
            if (Climb(seed, level, false, &secs)) cleared++;
            if ((int)(secs * 1000.0f) > worstMs) worstMs = (int)(secs * 1000.0f);
        }
    }
    printf("  (bot: %d of %d towers, longest %.1fs)\n", cleared, tried, (double)worstMs / 1000.0);
    CHECK(cleared == tried, "bot: a simple reeling climber clears every tower (orbs off), so each is reachable before the fire");
    int withOrbs = 0, triedOrbs = 0;
    for (uint64_t seed = 1; seed <= 20; seed++) for (int level = 1; level <= 3; level++) { triedOrbs++; withOrbs += Climb(seed, level, true, NULL); }
    CHECK(withOrbs > triedOrbs / 2, "bot: and even blind to the orbs it usually makes it, so they are avoidable");
    /* Doing nothing loses. */
    Fresh(2, 1);
    G()->fireSpeed = Game_FireSpeed(1);
    G()->fireY = G()->groundY + 200.0f;
    Run(120 * 30);
    CHECK(G()->state == LS_DYING, "bot: standing still gets you burned");
}

static void test_flow(void) {
    Game_Init(G(), 1, 40);
    Game *g = G();
    CHECK(g->state == LS_INTRO && g->lives == START_LIVES && g->level == 1 && g->highScore == 40, "flow: opens on an intro");
    Run(10);
    CHECK(g->stepCount == 0, "flow: nothing runs during the intro");
    for (int i = 0; i < 40; i++) Game_Update(g, 0.05f);
    CHECK(g->state == LS_PLAY, "flow: then play");
    long n0 = g->stepCount;
    Game_Update(g, STEP_DT * 0.4f);
    Game_Update(g, STEP_DT * 0.4f);
    CHECK(g->stepCount - n0 <= 1, "flow: fixed steps");
    long n1 = g->stepCount;
    Game_Update(g, 5.0f);
    CHECK(g->stepCount - n1 <= (long)(0.1f * STEP_HZ) + 1, "flow: a long hitch is clamped");

    /* Clearing the tower: bonus, then the next one. */
    Fresh(4, 1);
    g = G();
    g->fireY = 1e6f;
    g->p.x = g->ledges[g->ledgeCount - 1].x + 50.0f; g->p.y = g->towerTop - 30.0f; g->p.vy = 80.0f; g->p.mode = P_AIR;
    g->time = 30.0f;
    long s0 = g->score;
    Run(30);
    CHECK(g->state == LS_CLEAR && g->justClear && g->lastBonus >= 1000 && g->score - s0 >= g->lastBonus, "goal: landing on the top ledge clears the tower, with a bonus");
    for (int i = 0; i < 60; i++) Game_Update(g, 0.05f);
    CHECK(g->level == 2 && g->state == LS_INTRO && g->p.mode == P_STAND && g->orbCount > 0, "goal: on to a taller one with orbs");
    CHECK(g->lives == START_LIVES, "goal: lives carry over");

    /* Points for height and extra lives. */
    Fresh(4, 1);
    g = G();
    g->score = EXTRA_LIFE_EVERY - 30;
    g->p.mode = P_AIR; g->p.y = g->p.y - 400.0f; g->p.vy = -10.0f; g->fireY = 1e6f;
    Run(3);
    CHECK(g->score > EXTRA_LIFE_EVERY - 30 && g->lives == START_LIVES + 1 && g->justExtraLife, "score: height pays, and an extra life at 8,000");

    Fresh(4, 1);
    Game_TogglePause(G());
    long st = G()->stepCount;
    Game_Update(G(), 1.0f);
    Game_Step(G());
    CHECK(G()->stepCount == st && G()->phase == GS_PAUSED, "pause: nothing moves");
}

static void test_determinism(void) {
    static Game a, b;
    Game_Init(&a, 77, 0); Game_Init(&b, 77, 0);
    a.state = b.state = LS_PLAY;
    for (int i = 0; i < 120 * 30; i++) {
        BotStep(&a); BotStep(&b);
        Game_Step(&a); Game_Step(&b);
        if (a.state != LS_PLAY) break;
    }
    CHECK(a.p.x == b.p.x && a.p.y == b.p.y && a.score == b.score && a.fireY == b.fireY, "determinism: same seed and inputs, same climb");
}

int main(void) {
    test_free_fall();
    test_grapple_targeting();
    test_rope_physics();
    test_ledges_and_walking();
    test_walls();
    test_fire_and_death();
    test_orbs();
    test_towers();
    test_bot_climbs();
    test_flow();
    test_determinism();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
