/* Headless correctness tests for Rockdrift's rules (game.c). No raylib:
 * builds and runs anywhere. Physics is a fixed 120 Hz step, so scenarios
 * are exact and repeatable; the statistical ones (hyperspace odds, the small
 * saucer's aim) use fixed seeds and tolerances wide enough to be stable. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/game.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static void RunSteps(Game *g, int n) { for (int i = 0; i < n; i++) Game_Step(g); }

/* An empty field with the ship parked mid-screen and nothing scheduled. */
static void Empty(Game *g, uint64_t seed) {
    Game_Init(g, seed, 0);
    memset(g->rocks, 0, sizeof(g->rocks));
    g->rockCount = 0;
    g->saucerTimer = 1e9f;
    g->ship.invuln = 0.0f;
    g->waveTimer = 1e9f; /* don't advance to the next wave in the middle of a scenario */
}

static Rock *PutRock(Game *g, float x, float y, float vx, float vy, int size) {
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (g->rocks[i].active) continue;
        g->rocks[i] = (Rock){x, y, vx, vy, 0.0f, 0.0f, size, 1234u + (uint32_t)i, true};
        g->rockCount++;
        return &g->rocks[i];
    }
    return NULL;
}

static int CountActive(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_ROCKS; i++) n += g->rocks[i].active ? 1 : 0;
    return n;
}

static void test_helpers(void) {
    CHECK(fabsf(Game_WrapDelta(10.0f, 30.0f, FIELD_W) - 20.0f) < 1e-4f, "wrap: plain difference");
    CHECK(fabsf(Game_WrapDelta(FIELD_W - 5.0f, 5.0f, FIELD_W) - 10.0f) < 1e-4f, "wrap: the short way across the seam is positive");
    CHECK(fabsf(Game_WrapDelta(5.0f, FIELD_W - 5.0f, FIELD_W) + 10.0f) < 1e-4f, "wrap: ...and negative the other way");
    bool antisym = true;
    for (float a = 0; a < FIELD_W; a += 37.0f) for (float b = 0; b < FIELD_W; b += 53.0f) {
        float d = Game_WrapDelta(a, b, FIELD_W);
        if (fabsf(d + Game_WrapDelta(b, a, FIELD_W)) > 1e-3f && fabsf(fabsf(d) - FIELD_W / 2.0f) > 1e-3f) antisym = false;
        if (d < -FIELD_W / 2.0f - 1e-3f || d > FIELD_W / 2.0f + 1e-3f) antisym = false;
    }
    CHECK(antisym, "wrap: delta is antisymmetric and never longer than half the field");
    CHECK(Game_WrapDist(1.0f, 1.0f, FIELD_W - 1.0f, FIELD_H - 1.0f) < 3.0f, "wrap: distance across a corner is tiny");

    float a[ROCK_VERTS], b[ROCK_VERTS], c[ROCK_VERTS];
    Game_RockShape(77u, a); Game_RockShape(77u, b); Game_RockShape(78u, c);
    CHECK(memcmp(a, b, sizeof(a)) == 0, "shape: the same seed gives the same outline");
    CHECK(memcmp(a, c, sizeof(a)) != 0, "shape: a different seed gives a different one");
    bool inRange = true;
    for (uint32_t s = 0; s < 500; s++) {
        float r[ROCK_VERTS];
        Game_RockShape(s, r);
        for (int i = 0; i < ROCK_VERTS; i++) if (r[i] < 0.72f - 1e-4f || r[i] > 1.16f + 1e-4f) inRange = false;
    }
    CHECK(inRange, "shape: every radius stays within 0.72..1.16 of nominal (so hit circles are honest)");

    CHECK(Game_RockRadius(ROCK_LARGE) > Game_RockRadius(ROCK_MEDIUM) && Game_RockRadius(ROCK_MEDIUM) > Game_RockRadius(ROCK_SMALL), "rocks: bigger sizes are bigger");
    CHECK(Game_RockPoints(ROCK_LARGE) == 20 && Game_RockPoints(ROCK_MEDIUM) == 50 && Game_RockPoints(ROCK_SMALL) == 100, "rocks: smaller is worth more");

    bool monotonic = true;
    for (int n = 2; n <= 40; n++) if (Game_BeatInterval(n) < Game_BeatInterval(n - 1)) monotonic = false;
    CHECK(monotonic && Game_BeatInterval(1) < Game_BeatInterval(20) && Game_BeatInterval(0) == Game_BeatInterval(1), "beat: fewer rocks, faster beat; degenerate counts safe");
    CHECK(Game_SaucerAimError(0) > Game_SaucerAimError(15000) && Game_SaucerAimError(15000) > Game_SaucerAimError(30000), "aim: the error shrinks as the score climbs");
    CHECK(Game_SaucerAimError(1000000) > 0.05f && Game_SaucerAimError(-5) == Game_SaucerAimError(0), "aim: never perfect, negatives guarded");
}

static void test_init_and_waves(void) {
    Game g;
    Game_Init(&g, 1, 700);
    CHECK(g.phase == GS_PLAYING && g.lives == START_LIVES && g.wave == 1 && g.score == 0 && g.highScore == 700, "init: fresh run");
    CHECK(g.ship.alive && g.ship.x == FIELD_W / 2.0f && g.ship.y == FIELD_H / 2.0f, "init: ship mid-field");
    CHECK(g.rockCount == 4 && CountActive(&g) == 4, "init: wave one is four large rocks");
    bool clear = true, large = true;
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (!g.rocks[i].active) continue;
        if (Game_WrapDist(g.rocks[i].x, g.rocks[i].y, g.ship.x, g.ship.y) < SPAWN_CLEAR_RADIUS - 0.5f) clear = false;
        if (g.rocks[i].size != ROCK_LARGE) large = false;
    }
    CHECK(clear && large, "init: rocks start large and well away from the ship");

    int prev = 0;
    bool grows = true, capped = true, safe = true;
    for (int w = 1; w <= 20; w++) {
        Game h;
        Game_Init(&h, (uint64_t)w, 0);
        Game_StartWave(&h, w);
        if (h.rockCount < prev) grows = false;
        if (h.rockCount > ROCK_START_MAX) capped = false;
        prev = h.rockCount;
        for (int i = 0; i < MAX_ROCKS; i++) if (h.rocks[i].active && Game_WrapDist(h.rocks[i].x, h.rocks[i].y, h.ship.x, h.ship.y) < SPAWN_CLEAR_RADIUS - 0.5f) safe = false;
    }
    CHECK(grows && capped && safe && prev == ROCK_START_MAX, "waves: more rocks each wave, capped at 11, never spawned on top of the ship");
    CHECK(4 * ROCK_START_MAX <= MAX_ROCKS, "waves: the rock array is big enough for every piece of a full wave");
}

static void test_ship_physics(void) {
    Game g;
    Empty(&g, 1);
    Game_SetInput(&g, 1.0f, false, false, false);
    RunSteps(&g, 120);
    CHECK(fabsf(g.ship.angle - SHIP_ROT_SPEED) < 0.02f || fabsf(g.ship.angle - (SHIP_ROT_SPEED - 2.0f * 3.14159265f)) < 0.02f, "ship: turns at SHIP_ROT_SPEED radians a second");
    Empty(&g, 1);
    Game_SetInput(&g, -1.0f, false, false, false);
    RunSteps(&g, 60);
    CHECK(g.ship.angle < 0.0f, "ship: left turns the other way");

    Empty(&g, 1);
    Game_SetInput(&g, 0.0f, true, false, false); /* facing up */
    RunSteps(&g, 30);
    CHECK(g.ship.vy < -50.0f && fabsf(g.ship.vx) < 0.001f, "thrust: accelerates the way the nose points");
    CHECK(g.ship.thrusting, "thrust: flag set for the renderer");
    float vy = g.ship.vy;
    Game_SetInput(&g, 0.0f, false, false, false);
    RunSteps(&g, 60);
    CHECK(g.ship.vy == vy && !g.ship.thrusting, "inertia: with the engine off it keeps drifting at the same speed");

    Empty(&g, 1);
    g.ship.angle = 3.14159265f / 2.0f; /* pointing right */
    Game_SetInput(&g, 0.0f, true, false, false);
    RunSteps(&g, 20);
    CHECK(g.ship.vx > 20.0f && fabsf(g.ship.vy) < 0.01f, "thrust: heading is measured clockwise from up");

    Empty(&g, 1);
    Game_SetInput(&g, 0.0f, true, false, false);
    RunSteps(&g, 120 * 10);
    float speed = sqrtf(g.ship.vx * g.ship.vx + g.ship.vy * g.ship.vy);
    CHECK(speed <= SHIP_MAX_SPEED + 0.01f && speed > SHIP_MAX_SPEED - 1.0f, "thrust: speed tops out at SHIP_MAX_SPEED");

    Empty(&g, 1);
    g.ship.x = 2.0f; g.ship.vx = -200.0f;
    RunSteps(&g, 10);
    CHECK(g.ship.x > FIELD_W - 20.0f && g.ship.x < FIELD_W, "wrap: leaving the left edge comes in on the right");
    Empty(&g, 1);
    g.ship.y = FIELD_H - 2.0f; g.ship.vy = 200.0f;
    RunSteps(&g, 10);
    CHECK(g.ship.y >= 0.0f && g.ship.y < 20.0f, "wrap: leaving the bottom comes in at the top");
}

static void test_shooting(void) {
    Game g;
    Empty(&g, 1);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE); /* keep the wave alive */
    Game_SetInput(&g, 0.0f, false, true, false);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justFired && g.bullets[0].active, "fire: a bullet leaves the nose");
    CHECK(g.bullets[0].vy < -BULLET_SPEED * 0.99f && fabsf(g.bullets[0].vx) < 0.01f, "fire: fast, straight the way the ship faces");
    CHECK(g.bullets[0].y < g.ship.y, "fire: starting just ahead of the ship");

    RunSteps(&g, 120);
    int active = 0;
    for (int i = 0; i < MAX_BULLETS; i++) active += g.bullets[i].active ? 1 : 0;
    CHECK(active <= MAX_BULLETS, "fire: never more than four bullets");
    Game_SetInput(&g, 0.0f, false, false, false);
    RunSteps(&g, (int)(BULLET_LIFE * STEP_HZ) + 4);
    active = 0;
    for (int i = 0; i < MAX_BULLETS; i++) active += g.bullets[i].active ? 1 : 0;
    CHECK(active == 0, "fire: bullets expire after BULLET_LIFE, they don't fly forever");

    Empty(&g, 2);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    Game_SetInput(&g, 0.0f, false, true, false);
    int maxSeen = 0, shots = 0;
    for (int i = 0; i < 120 * 3; i++) {
        Game_ConsumeFrameFlags(&g);
        Game_Step(&g);
        if (g.justFired) shots++;
        int n = 0;
        for (int k = 0; k < MAX_BULLETS; k++) n += g.bullets[k].active ? 1 : 0;
        if (n > maxSeen) maxSeen = n;
    }
    CHECK(maxSeen == MAX_BULLETS, "fire: holding fire fills the four-bullet allowance");
    CHECK(shots >= 4 && shots <= (int)(3.0f / FIRE_COOLDOWN) + 2, "fire: and the cooldown paces the shots");

    Empty(&g, 3);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.ship.x = 5.0f; g.ship.y = 240.0f; g.ship.angle = -3.14159265f / 2.0f; /* pointing left, at the edge */
    Game_SetInput(&g, 0.0f, false, true, false);
    Game_Step(&g);
    Game_SetInput(&g, 0.0f, false, false, false);
    RunSteps(&g, 20);
    CHECK(g.bullets[0].active && g.bullets[0].x > FIELD_W / 2.0f, "fire: bullets wrap round the field too");
}

static void test_rock_splitting(void) {
    Game g;
    Empty(&g, 5);
    PutRock(&g, 500.0f, 100.0f, 30.0f, -20.0f, ROCK_LARGE);
    g.bullets[0] = (Bullet){500.0f, 100.0f, 0.0f, 0.0f, 1.0f, true};
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(CountActive(&g) == 2 && g.rockCount == 2, "split: a large rock becomes two");
    bool medium = true, atParent = true;
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active) {
        if (g.rocks[i].size != ROCK_MEDIUM) medium = false;
        if (Game_WrapDist(g.rocks[i].x, g.rocks[i].y, 500.0f, 100.0f) > 12.0f) atParent = false;
    }
    CHECK(medium && atParent, "split: ...both medium, where the parent was");
    CHECK(g.score == 20 && !g.bullets[0].active, "split: 20 points, bullet used up");
    CHECK(g.killCount == 1 && g.kills[0].kind == KILL_ROCK_LARGE && g.kills[0].points == 20, "split: the kill is reported for the renderer");

    bool diverge = false, quicker = true;
    Rock *pieces[2]; int n = 0;
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active && n < 2) pieces[n++] = &g.rocks[i];
    if (fabsf(pieces[0]->vx - pieces[1]->vx) + fabsf(pieces[0]->vy - pieces[1]->vy) > 20.0f) diverge = true;
    float parentSpeed = sqrtf(30.0f * 30.0f + 20.0f * 20.0f);
    for (int k = 0; k < 2; k++) if (sqrtf(pieces[k]->vx * pieces[k]->vx + pieces[k]->vy * pieces[k]->vy) <= parentSpeed) quicker = false;
    CHECK(diverge, "split: the pieces fly apart");
    CHECK(quicker, "split: smaller pieces are quicker than what they came from");

    /* Medium -> two small; small -> nothing. */
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active) { g.rocks[i].x = 300.0f + i; g.rocks[i].y = 300.0f; g.rocks[i].vx = g.rocks[i].vy = 0.0f; }
    int firstIdx = -1;
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active) { firstIdx = i; break; }
    g.bullets[0] = (Bullet){g.rocks[firstIdx].x, g.rocks[firstIdx].y, 0.0f, 0.0f, 1.0f, true};
    long before = g.score;
    Game_Step(&g);
    CHECK(CountActive(&g) == 3 && g.score == before + 50, "split: a medium rock becomes two small ones, worth 50");
    int smallIdx = -1;
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active && g.rocks[i].size == ROCK_SMALL) { smallIdx = i; break; }
    g.rocks[smallIdx].vx = g.rocks[smallIdx].vy = 0.0f;
    g.bullets[0] = (Bullet){g.rocks[smallIdx].x, g.rocks[smallIdx].y, 0.0f, 0.0f, 1.0f, true};
    before = g.score;
    Game_Step(&g);
    CHECK(CountActive(&g) == 2 && g.score == before + 100, "split: a small rock just vanishes, worth 100");

    /* A whole wave, shot to bits, never overflows the array. */
    Game h;
    Game_Init(&h, 9, 0);
    Game_StartWave(&h, 30);
    int peak = 0;
    for (int round = 0; round < 6; round++) {
        for (int i = 0; i < MAX_ROCKS; i++) {
            if (!h.rocks[i].active) continue;
            h.bullets[0] = (Bullet){h.rocks[i].x, h.rocks[i].y, 0.0f, 0.0f, 1.0f, true};
            h.ship.invuln = 100.0f;
            h.waveTimer = 1e9f;
            Game_Step(&h);
            if (CountActive(&h) > peak) peak = CountActive(&h);
            break;
        }
    }
    CHECK(peak <= MAX_ROCKS && CountActive(&h) == h.rockCount, "split: the rock count stays exact and within the array");
}

static void test_ship_death_and_respawn(void) {
    Game g;
    Empty(&g, 1);
    PutRock(&g, g.ship.x + 5.0f, g.ship.y, 0.0f, 0.0f, ROCK_LARGE);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justShipDeath && !g.ship.alive && g.lives == START_LIVES - 1, "death: hitting a rock costs a ship");
    CHECK(g.killCount >= 1 && g.kills[g.killCount - 1].kind == KILL_SHIP, "death: reported for the renderer");
    CHECK(g.phase == GS_PLAYING && g.ship.respawnTimer > 0.0f, "death: play goes on, respawn pending");

    /* It doesn't come back into a crowded middle. */
    g.rockCount = 0;
    for (int i = 0; i < MAX_ROCKS; i++) g.rocks[i].active = false;
    PutRock(&g, FIELD_W / 2.0f + 60.0f, FIELD_H / 2.0f, 0.0f, 0.0f, ROCK_LARGE); /* inside the clear radius */
    g.waveTimer = 1e9f;
    RunSteps(&g, (int)(RESPAWN_SECONDS * STEP_HZ) + 60);
    CHECK(!g.ship.alive, "respawn: waits while a rock is near the middle");
    g.rocks[0].active = false; for (int i = 0; i < MAX_ROCKS; i++) g.rocks[i].active = false; g.rockCount = 0;
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    RunSteps(&g, 5);
    CHECK(g.ship.alive && g.ship.x == FIELD_W / 2.0f && g.ship.vx == 0.0f, "respawn: back in the middle, still, once it is clear");
    CHECK(g.ship.invuln > 1.0f, "respawn: with a moment of protection");

    /* ...which really protects it. */
    PutRock(&g, g.ship.x, g.ship.y, 0.0f, 0.0f, ROCK_MEDIUM);
    RunSteps(&g, 10);
    CHECK(g.ship.alive, "respawn: protection: a rock right on top of it does nothing yet");
    for (int i = 0; i < MAX_ROCKS; i++) if (g.rocks[i].active && g.rocks[i].size == ROCK_MEDIUM) g.rocks[i].active = false;
    g.rockCount = CountActive(&g);
    g.ship.invuln = 0.0f;
    PutRock(&g, g.ship.x, g.ship.y, 0.0f, 0.0f, ROCK_MEDIUM);
    RunSteps(&g, 5);
    CHECK(!g.ship.alive, "respawn: ...and stops when the time is up");

    /* Last ship. */
    Game o;
    Empty(&o, 2);
    o.lives = 1;
    o.score = 640;
    PutRock(&o, o.ship.x, o.ship.y, 0.0f, 0.0f, ROCK_SMALL);
    Game_ConsumeFrameFlags(&o);
    Game_Step(&o);
    CHECK(o.phase == GS_GAMEOVER && o.justGameOver && o.lives == 0, "game over: the last ship goes");
    CHECK(o.highScore >= 640, "game over: high score updated");
    Game_TogglePause(&o);
    CHECK(o.phase == GS_GAMEOVER, "game over: pause can't undo it");
    Game_Restart(&o, 3);
    CHECK(o.phase == GS_PLAYING && o.score == 0 && o.lives == START_LIVES && o.highScore >= 640, "restart: fresh run, best kept");
}

static void test_hyperspace(void) {
    Game g;
    Empty(&g, 1);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    float x0 = g.ship.x, y0 = g.ship.y;
    Game_SetInput(&g, 0.0f, false, false, true);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justHyperspace || g.justHyperDeath, "hyperspace: pressing it does one thing or the other");
    if (g.justHyperspace) CHECK(g.ship.x != x0 || g.ship.y != y0, "hyperspace: and the ship really moves");
    Game_ConsumeFrameFlags(&g);
    if (g.ship.alive) {
        float x1 = g.ship.x, y1 = g.ship.y;
        RunSteps(&g, 30);
        CHECK(g.ship.x == x1 && g.ship.y == y1, "hyperspace: holding the key doesn't jump again (rising edge only)");
    }

    int deaths = 0, jumps = 0, inField = 0, trials = 3000;
    for (int i = 0; i < trials; i++) {
        Game h;
        Empty(&h, 1000u + (uint64_t)i);
        PutRock(&h, 5.0f, 5.0f, 0.0f, 0.0f, ROCK_SMALL);
        h.rocks[0].x = -5000.0f;   /* nowhere near anything */
        h.rocks[0].active = true;
        Game_SetInput(&h, 0.0f, false, false, true);
        Game_Step(&h);
        if (h.justHyperDeath) deaths++;
        if (h.justHyperspace) { jumps++; if (h.ship.x >= 0 && h.ship.x <= FIELD_W && h.ship.y >= 0 && h.ship.y <= FIELD_H) inField++; }
    }
    CHECK(deaths + jumps == trials, "hyperspace: every press is one or the other");
    CHECK(deaths > trials / 6 - 120 && deaths < trials / 6 + 120, "hyperspace: about one press in six ends badly");
    CHECK(inField == jumps, "hyperspace: survivors always land inside the field");
}

static void test_saucers(void) {
    Game g;
    Empty(&g, 4);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.saucerTimer = 0.01f;
    g.waveTimer = 0.0f; /* a live wave: saucers only visit those */
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(g.saucer.active && g.justSaucerAppeared, "saucer: appears when its timer runs out");
    CHECK(!g.saucer.small, "saucer: only the big one at score zero");
    CHECK(g.saucer.x < 0.0f || g.saucer.x > FIELD_W, "saucer: enters from off the side of the field");

    /* Shot down: 200 for the large one. */
    g.saucer.x = 300.0f; g.saucer.y = 100.0f; g.saucer.vx = 0.0f; g.saucer.vy = 0.0f;
    g.bullets[0] = (Bullet){300.0f, 100.0f, 0.0f, 0.0f, 1.0f, true};
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(!g.saucer.active && g.score == 200 && g.kills[0].kind == KILL_SAUCER_LARGE, "saucer: shot down for 200");

    Game s;
    Empty(&s, 4);
    PutRock(&s, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    s.score = 40000; s.nextExtraAt = 1000000;
    s.saucer = (Saucer){300.0f, 100.0f, 0.0f, 0.0f, true, true, 100.0f, 100.0f};
    s.bullets[0] = (Bullet){300.0f, 100.0f, 0.0f, 0.0f, 1.0f, true};
    Game_Step(&s);
    CHECK(s.score == 41000, "saucer: the small one is worth 1000");

    /* It leaves the far side and schedules another. */
    Game l;
    Empty(&l, 6);
    PutRock(&l, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    l.saucer = (Saucer){-20.0f, 300.0f, 90.0f, 0.0f, true, false, 100.0f, 1e9f};
    RunSteps(&l, (int)((FIELD_W + 100.0f) / 90.0f * STEP_HZ) + 20);
    CHECK(!l.saucer.active && l.saucerTimer > 10.0f, "saucer: leaves the far side and schedules its next visit");

    /* It doesn't turn up when there is nothing to fight over. */
    Game n;
    Empty(&n, 7);
    n.saucerTimer = 0.01f;
    n.waveTimer = 0.0f;
    RunSteps(&n, 20);
    CHECK(!n.saucer.active, "saucer: stays away from an empty field");
}

/* Fires many shots from a small saucer at a parked ship and measures how far
 * each one is from the true bearing. */
static float WorstAimMiss(long score, uint64_t seed, int shots) {
    Game g;
    Empty(&g, seed);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.score = score; g.nextExtraAt = 100000000L;
    g.ship.x = 500.0f; g.ship.y = 300.0f; g.ship.invuln = 1e9f;
    float worst = 0.0f;
    for (int i = 0; i < shots; i++) {
        memset(g.enemyBullets, 0, sizeof(g.enemyBullets));
        g.saucer = (Saucer){120.0f, 90.0f, 0.0f, 0.0f, true, true, 100.0f, 0.001f};
        Game_Step(&g);
        for (int k = 0; k < MAX_ENEMY_BULLETS; k++) if (g.enemyBullets[k].active) {
            float bearing = atan2f(Game_WrapDelta(g.saucer.x, g.ship.x, FIELD_W), -Game_WrapDelta(g.saucer.y, g.ship.y, FIELD_H));
            float shot = atan2f(g.enemyBullets[k].vx, -g.enemyBullets[k].vy);
            float miss = fabsf(Game_WrapDelta(bearing, shot, 6.28318531f));
            if (miss > worst) worst = miss;
        }
        g.saucer.active = false;
        g.saucer.vx = g.saucer.vy = 0.0f;
    }
    return worst;
}

static void test_saucer_aim(void) {
    float loose = WorstAimMiss(0, 11, 300), tight = WorstAimMiss(30000, 11, 300);
    CHECK(loose <= Game_SaucerAimError(0) + 0.06f, "aim: no shot strays further than the error allows (score 0)");
    CHECK(tight <= Game_SaucerAimError(30000) + 0.06f, "aim: ...nor at score 30000");
    CHECK(tight < loose * 0.5f, "aim: a high score really does make it deadlier");
    CHECK(loose > 0.2f, "aim: at score zero it is not a sniper");

    /* Wrapped bearing: the ship is nearer THROUGH the edge. */
    Game g;
    Empty(&g, 12);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.score = 30000; g.nextExtraAt = 100000000L;
    g.ship.x = FIELD_W - 30.0f; g.ship.y = 240.0f; g.ship.invuln = 1e9f;
    g.saucer = (Saucer){30.0f, 240.0f, 0.0f, 0.0f, true, true, 100.0f, 0.001f};
    Game_Step(&g);
    bool viaEdge = false;
    for (int k = 0; k < MAX_ENEMY_BULLETS; k++) if (g.enemyBullets[k].active && g.enemyBullets[k].vx < -100.0f) viaEdge = true;
    CHECK(viaEdge, "aim: it shoots the short way, through the wrapped edge");

    /* A large saucer just fires anywhere. */
    Game r;
    Empty(&r, 13);
    PutRock(&r, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    r.ship.x = 500.0f; r.ship.y = 300.0f; r.ship.invuln = 1e9f;
    int quadrants[4] = {0, 0, 0, 0};
    for (int i = 0; i < 200; i++) {
        memset(r.enemyBullets, 0, sizeof(r.enemyBullets));
        r.saucer = (Saucer){120.0f, 90.0f, 0.0f, 0.0f, true, false, 100.0f, 0.001f};
        Game_Step(&r);
        for (int k = 0; k < MAX_ENEMY_BULLETS; k++) if (r.enemyBullets[k].active) quadrants[(r.enemyBullets[k].vx > 0 ? 1 : 0) + (r.enemyBullets[k].vy > 0 ? 2 : 0)]++;
    }
    CHECK(quadrants[0] > 20 && quadrants[1] > 20 && quadrants[2] > 20 && quadrants[3] > 20, "aim: the large saucer fires every which way");
}

static void test_enemy_bullets(void) {
    Game g;
    Empty(&g, 8);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.enemyBullets[0] = (Bullet){g.ship.x, g.ship.y, 0.0f, 0.0f, 1.0f, true};
    Game_Step(&g);
    CHECK(!g.ship.alive && g.lives == START_LIVES - 1, "saucer bullet: a hit costs a ship");
    Empty(&g, 8);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.enemyBullets[0] = (Bullet){100.0f, 100.0f, 200.0f, 0.0f, 0.05f, true};
    RunSteps(&g, 20);
    CHECK(!g.enemyBullets[0].active, "saucer bullet: expires, doesn't fly forever");
    CHECK(g.ship.alive && CountActive(&g) == 1, "saucer bullet: and leaves rocks alone");
}

static void test_waves_scoring_extra_life(void) {
    Game g;
    Empty(&g, 9);
    g.waveTimer = 0.0f;
    g.saucerTimer = 1e9f;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justWaveClear && g.waveTimer > 0.0f && g.wave == 1, "wave: an empty field clears the wave, with a pause before the next");
    RunSteps(&g, (int)(WAVE_DELAY * STEP_HZ) + 3);
    CHECK(g.wave == 2 && g.rockCount == 5, "wave: then wave two, one rock more");
    CHECK(g.saucerTimer > 5.0f, "wave: and the saucer clock is reset");

    /* A lingering saucer holds the next wave back. */
    Game h;
    Empty(&h, 10);
    h.waveTimer = 0.0f;
    h.saucer = (Saucer){300.0f, 100.0f, 0.0f, 0.0f, true, false, 100.0f, 1e9f};
    Game_ConsumeFrameFlags(&h);
    RunSteps(&h, 10);
    CHECK(!h.justWaveClear && h.wave == 1, "wave: not cleared while a saucer is still in the field");

    /* Extra ships, every 10,000. */
    Game e;
    Empty(&e, 11);
    PutRock(&e, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    e.score = 9990;
    e.bullets[0] = (Bullet){30.0f, 30.0f, 0.0f, 0.0f, 1.0f, true};
    Game_ConsumeFrameFlags(&e);
    Game_Step(&e);
    CHECK(e.justExtraLife && e.lives == START_LIVES + 1, "extra ship: at 10,000");
    int lives = e.lives;
    e.score = 19990; e.nextExtraAt = 20000;
    PutRock(&e, 300.0f, 300.0f, 0.0f, 0.0f, ROCK_MEDIUM);
    for (int i = 0; i < MAX_ROCKS; i++) if (e.rocks[i].active && e.rocks[i].x == 300.0f) e.bullets[0] = (Bullet){300.0f, 300.0f, 0.0f, 0.0f, 1.0f, true};
    Game_Step(&e);
    CHECK(e.lives == lives + 1, "extra ship: and again at 20,000 -- it repeats");
    e.score = 20010;
    int again = e.lives;
    RunSteps(&e, 30);
    CHECK(e.lives == again, "extra ship: but not twice for the same threshold");
    Game c;
    Empty(&c, 12);
    c.lives = MAX_LIVES;
    c.score = 9995; PutRock(&c, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    c.bullets[0] = (Bullet){30.0f, 30.0f, 0.0f, 0.0f, 1.0f, true};
    Game_Step(&c);
    CHECK(c.lives == MAX_LIVES, "extra ship: capped");
}

static void test_beat_and_update(void) {
    Game g;
    Empty(&g, 14);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    g.waveTimer = 0.0f; g.beatTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(g.justBeat && g.beatNote == 1, "beat: a note when the timer runs out");
    CHECK(g.beatTimer > 0.2f, "beat: and the next one is scheduled");
    Empty(&g, 15);
    g.waveTimer = 0.0f; g.beatTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(!g.justBeat, "beat: silent with nothing left to fight");

    Empty(&g, 16);
    PutRock(&g, 30.0f, 30.0f, 0.0f, 0.0f, ROCK_LARGE);
    Game_SetInput(&g, 1.0f, false, false, false);
    float a0 = g.ship.angle;
    Game_Update(&g, STEP_DT * 0.5f);
    CHECK(g.ship.angle == a0, "update: less than one step of time does nothing yet");
    Game_Update(&g, STEP_DT * 0.6f);
    CHECK(g.ship.angle != a0, "update: the remainder carries over");
    float a1 = g.ship.angle;
    Game_Update(&g, 5.0f);
    CHECK(g.ship.angle - a1 < SHIP_ROT_SPEED * 0.1f + 0.05f, "update: a long hitch is clamped");
    Game_TogglePause(&g);
    float a2 = g.ship.angle;
    Game_Update(&g, 0.5f);
    CHECK(g.phase == GS_PAUSED && g.ship.angle == a2, "pause: nothing moves");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles back");
}

/* A bot that turns toward the nearest rock, fires, thrusts now and then and
 * sometimes panics into hyperspace. */
static long PlayBot(uint64_t seed, int steps, bool *sane) {
    Game g;
    Game_Init(&g, seed, 0);
    Rng bot;
    Rng_Seed(&bot, seed * 7919);
    for (int i = 0; i < steps && g.phase == GS_PLAYING; i++) {
        float best = 1e9f, bx = 0, by = 0;
        for (int r = 0; r < MAX_ROCKS; r++) if (g.rocks[r].active) {
            float d = Game_WrapDist(g.ship.x, g.ship.y, g.rocks[r].x, g.rocks[r].y);
            if (d < best) { best = d; bx = Game_WrapDelta(g.ship.x, g.rocks[r].x, FIELD_W); by = Game_WrapDelta(g.ship.y, g.rocks[r].y, FIELD_H); }
        }
        float want = atan2f(bx, -by);
        float diff = Game_WrapDelta(g.ship.angle, want, 6.28318531f);
        float turn = diff > 0.05f ? 1.0f : (diff < -0.05f ? -1.0f : 0.0f);
        bool thrust = (i % 300) < 25;
        bool hyper = best < 40.0f && Rng_Range(&bot, 400) == 0;
        Game_SetInput(&g, turn, thrust, true, hyper);
        Game_Step(&g);
        Game_ConsumeFrameFlags(&g);

        if (i % 60 == 0) {
            int counted = CountActive(&g);
            if (counted != g.rockCount) *sane = false;
            if (counted > MAX_ROCKS) *sane = false;
            if (g.lives < 0 || g.lives > MAX_LIVES) *sane = false;
            if (!(g.ship.x >= 0 && g.ship.x <= FIELD_W && g.ship.y >= 0 && g.ship.y <= FIELD_H)) *sane = false;
            for (int r = 0; r < MAX_ROCKS; r++) {
                const Rock *k = &g.rocks[r];
                if (!k->active) continue;
                if (!(k->x >= 0 && k->x <= FIELD_W && k->y >= 0 && k->y <= FIELD_H)) *sane = false;
                if (k->size < ROCK_SMALL || k->size > ROCK_LARGE) *sane = false;
            }
        }
    }
    return g.score * 13 + g.wave * 5 + g.lives;
}

static void test_bot_games(void) {
    bool sane = true;
    long total = 0;
    for (uint64_t seed = 1; seed <= 6; seed++) total += PlayBot(seed, 120 * 240, &sane);
    CHECK(sane, "bot: rock counts, positions, sizes and lives stay valid through long games");
    CHECK(total > 0, "bot: it scores");
    bool s2 = true;
    CHECK(PlayBot(3, 120 * 90, &s2) == PlayBot(3, 120 * 90, &s2), "determinism: same seed and inputs, same game");
    CHECK(PlayBot(3, 120 * 90, &s2) != PlayBot(4, 120 * 90, &s2), "determinism: a different seed plays out differently");
}

int main(void) {
    test_helpers();
    test_init_and_waves();
    test_ship_physics();
    test_shooting();
    test_rock_splitting();
    test_ship_death_and_respawn();
    test_hyperspace();
    test_saucers();
    test_saucer_aim();
    test_enemy_bullets();
    test_waves_scoring_extra_life();
    test_beat_and_update();
    test_bot_games();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
