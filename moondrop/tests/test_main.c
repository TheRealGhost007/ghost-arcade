/* Headless correctness tests for Moondrop's rules (game.c). No raylib. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* A flat moon at y=400 with one pad (x 300..360, x3) and one narrow (x5), ship hovering. */
static void Flat(Game *g) {
    Game_Init(g, 1, 0);
    for (int i = 0; i < TERR_N; i++) g->terrain[i] = 400.0f;
    g->padCount = 2;
    g->pads[0] = (Pad){25, 6, 3};  /* x 300..360 */
    g->pads[1] = (Pad){50, 3, 5};  /* x 600..624 */
    g->x = 330.0f; g->y = 200.0f; g->vx = 0.0f; g->vy = 0.0f; g->angle = 0.0f;
    g->fuel = FUEL_START;
}

static void test_freefall(void) {
    Game g;
    Flat(&g);
    g.x = 100.0f; g.y = 20.0f; g.vx = 10.0f;
    RunSteps(&g, 120);
    float t = 1.0f;
    CHECK(fabsf(g.y - (20.0f + 0.5f * GRAVITY * t * t)) < 0.01f, "freefall: y follows 1/2 g t^2 exactly");
    CHECK(fabsf(g.vy - GRAVITY * t) < 0.01f, "freefall: vy = g t");
    CHECK(fabsf(g.x - 110.0f) < 0.01f && g.vx == 10.0f, "freefall: no sideways force, no drift");
    CHECK(g.fuel == FUEL_START, "freefall: coasting burns nothing");

    /* Thrust straight up: net acceleration is THRUST - GRAVITY. */
    Flat(&g);
    g.y = 300.0f;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 60);
    float a = THRUST - GRAVITY;
    CHECK(fabsf(g.vy + a * 0.5f) < 0.02f && fabsf(g.y - (300.0f - 0.5f * a * 0.25f)) < 0.05f, "thrust: straight up gives net THRUST - GRAVITY");

    /* Thrust pointing right accelerates sideways, and up-and-right at 45 degrees. */
    Flat(&g);
    g.angle = 0.7853982f;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 60);
    CHECK(fabsf(g.vx - THRUST * 0.7071f * 0.5f) < 0.05f, "thrust: tilted right pushes right");
    CHECK(fabsf(g.vy - (GRAVITY - THRUST * 0.7071f) * 0.5f) < 0.05f, "thrust: and reduces the fall by cos(angle)");
}

static void test_rotation_and_fuel(void) {
    Game g;
    Flat(&g);
    Game_SetInput(&g, 1.0f, false);
    RunSteps(&g, 120);
    CHECK(fabsf(g.angle - ROT_RATE) < 0.01f, "rotate: clockwise at ROT_RATE");
    Game_SetInput(&g, -1.0f, false);
    RunSteps(&g, 240);
    CHECK(fabsf(g.angle + ROT_RATE) < 0.01f, "rotate: and back the other way");
    Flat(&g);
    g.y = 100.0f;
    Game_SetInput(&g, 1.0f, false);
    RunSteps(&g, 1000);
    CHECK(g.angle > -3.15f && g.angle < 3.15f, "rotate: the angle stays normalised");
    Flat(&g);
    Game_SetInput(&g, 5.0f, false);
    CHECK(g.rotInput == 1.0f, "input: clamped");

    Flat(&g);
    g.y = 100.0f;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 120);
    CHECK(fabsf(g.fuel - (FUEL_START - FUEL_BURN)) < 0.05f && g.thrusting, "fuel: a second of thrust burns FUEL_BURN");
    Game_SetInput(&g, 0.0f, false);
    RunSteps(&g, 10);
    CHECK(!g.thrusting && fabsf(g.fuel - (FUEL_START - FUEL_BURN)) < 0.05f, "fuel: nothing burns when the engine is off");

    Flat(&g);
    g.y = 100.0f; g.fuel = 0.05f;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 10);
    CHECK(g.fuel == 0.0f, "fuel: never below zero");
    float vy = g.vy;
    RunSteps(&g, 60);
    CHECK(!g.thrusting && g.vy > vy + GRAVITY * 0.4f, "fuel: with none left the engine does nothing");

    Flat(&g);
    g.y = 100.0f; g.fuel = FUEL_LOW + 0.5f;
    Game_SetInput(&g, 0.0f, true);
    Game_ConsumeFrameFlags(&g);
    bool warned = false;
    for (int i = 0; i < 60; i++) { Game_Step(&g); if (g.justLowFuel) warned = true; Game_ConsumeFrameFlags(&g); }
    CHECK(warned, "fuel: a warning when it drops through the low mark");
    g.fuel = FUEL_LOW - 50.0f;
    warned = false;
    for (int i = 0; i < 20; i++) { Game_Step(&g); if (g.justLowFuel) warned = true; }
    CHECK(!warned, "fuel: and only once");
}

static void test_classifier(void) {
    Game g;
    Flat(&g);
    int pad = -9;
    /* On the wide pad: both feet at 322 and 338. */
    CHECK(Game_Classify(&g, 0, 0, 0, 322, 338, &pad) == LAND_OK && pad == 0, "classify: gentle, level, on a pad");
    CHECK(Game_Classify(&g, MAX_LAND_VX, MAX_LAND_VY, 0, 322, 338, &pad) == LAND_OK, "classify: exactly at the limits is a landing");
    CHECK(Game_Classify(&g, MAX_LAND_VX + 0.01f, 0, 0, 322, 338, &pad) == LAND_TOO_FAST_SIDEWAYS, "classify: a hair too much sideways");
    CHECK(Game_Classify(&g, -MAX_LAND_VX - 0.01f, 0, 0, 322, 338, &pad) == LAND_TOO_FAST_SIDEWAYS, "classify: either direction");
    CHECK(Game_Classify(&g, 0, MAX_LAND_VY + 0.01f, 0, 322, 338, &pad) == LAND_TOO_FAST_DOWN, "classify: a hair too fast down");
    CHECK(Game_Classify(&g, 0, 0, MAX_LAND_TILT - 0.001f, 322, 338, &pad) == LAND_OK, "classify: just inside the tilt limit");
    CHECK(Game_Classify(&g, 0, 0, MAX_LAND_TILT + 0.001f, 322, 338, &pad) == LAND_TILTED, "classify: just past it");
    CHECK(Game_Classify(&g, 0, 0, -MAX_LAND_TILT - 0.001f, 322, 338, &pad) == LAND_TILTED, "classify: leaning the other way");
    /* Pad edges: x 300..360. */
    CHECK(Game_Classify(&g, 0, 0, 0, 300, 316, &pad) == LAND_OK, "classify: left foot exactly on the pad's left edge");
    CHECK(Game_Classify(&g, 0, 0, 0, 344, 360, &pad) == LAND_OK, "classify: right foot exactly on the right edge");
    CHECK(Game_Classify(&g, 0, 0, 0, 299.9f, 316, &pad) == LAND_OFF_PAD && pad == -1, "classify: a foot off the left edge is off the pad");
    CHECK(Game_Classify(&g, 0, 0, 0, 344, 360.1f, &pad) == LAND_OFF_PAD, "classify: or off the right");
    CHECK(Game_Classify(&g, 0, 0, 0, 290, 306, &pad) == LAND_OFF_PAD, "classify: straddling the edge");
    CHECK(Game_Classify(&g, 0, 0, 0, 350, 605, &pad) == LAND_OFF_PAD, "classify: one foot on each of two pads is nothing");
    CHECK(Game_Classify(&g, 0, 0, 0, 604, 620, &pad) == LAND_OK && pad == 1, "classify: the narrow pad fits the feet");
    /* Order: a bad pad beats everything, tilt beats speed. */
    CHECK(Game_Classify(&g, 99, 99, 1.0f, 100, 116, &pad) == LAND_OFF_PAD, "classify: off the pad, whatever else");
    CHECK(Game_Classify(&g, 99, 99, 1.0f, 322, 338, &pad) == LAND_TILTED, "classify: tilt is reported before speed");
    CHECK(Game_Classify(&g, 99, 99, 0.0f, 322, 338, &pad) == LAND_TOO_FAST_DOWN, "classify: down speed before sideways");
    CHECK(Game_Classify(&g, 0, 0, 0, 322, 338, NULL) == LAND_OK, "classify: the pad out-parameter is optional");

    CHECK(Game_LandingScore(5, 0.0f, 50.0f) == 250, "score: pad multiplier x 50");
    CHECK(Game_LandingScore(1, 500.0f, 50.0f) == 100, "score: plus a tenth of the fuel");
    CHECK(Game_LandingScore(2, 0.0f, 9.9f) == 150, "score: plus 50 for a feather-light touch");
    CHECK(Game_LandingScore(2, 0.0f, 10.0f) == 100, "score: not at 10");
}

static void Drop(Game *g, float x, float vx, float vy, float angle) {
    g->x = x; g->y = 380.0f; g->vx = vx; g->vy = vy; g->angle = angle;
    g->state = SHIP_FLYING;
    Game_SetInput(g, 0.0f, false);
}

static void test_touchdown(void) {
    Game g;
    Flat(&g);
    Drop(&g, 330.0f, 0.0f, 10.0f, 0.0f);
    long before = g.score;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 90);
    CHECK(g.state == SHIP_LANDED && g.justLand && g.lastResult == LAND_OK, "land: a gentle drop onto the pad lands");
    CHECK(g.vx == 0.0f && g.vy == 0.0f, "land: and it stops");
    float lx, ly, rx, ry;
    Game_ShipPoint(&g, -FOOT_X, FOOT_Y, &lx, &ly);
    Game_ShipPoint(&g, FOOT_X, FOOT_Y, &rx, &ry);
    CHECK(fabsf(ly - 400.0f) < 0.01f && fabsf(ry - 400.0f) < 0.01f, "land: feet sit exactly on the pad");
    CHECK(g.lastPad == 0 && g.score - before == g.lastAward && g.lastAward == Game_LandingScore(3, FUEL_START, 10.0f), "land: scored on the pad's multiplier");
    CHECK(fabsf(g.fuel - (FUEL_START + LAND_REFUEL)) < 0.01f, "land: refuelled a little");
    CHECK(g.highScore == g.score, "land: best updated");
    g.fuel = FUEL_MAX;
    Flat(&g);
    g.fuel = FUEL_MAX - 10.0f;
    Drop(&g, 330.0f, 0.0f, 10.0f, 0.0f);
    RunSteps(&g, 90);
    CHECK(g.fuel == FUEL_MAX, "land: fuel is capped");

    /* The landing card, then the next level (fuel carried). */
    Flat(&g);
    Drop(&g, 330.0f, 0.0f, 10.0f, 0.0f);
    RunSteps(&g, 90);
    float fuel = g.fuel;
    long score = g.score;
    RunSteps(&g, (int)(LAND_HOLD * STEP_HZ) - 30);
    CHECK(g.state == SHIP_LANDED && g.level == 1, "land: the card stays up for a moment");
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 20);
    CHECK(g.level == 2 && g.justNewLevel && g.state == SHIP_FLYING, "land: then the next level begins");
    CHECK(g.fuel == fuel && g.score == score, "land: fuel and score carry over");

    /* Crashes. */
    struct { float x, vx, vy, ang; LandResult want; const char *what; } cases[] = {
        {330, 0, 60, 0, LAND_TOO_FAST_DOWN, "too fast down"},
        {312, 40, 10, 0, LAND_TOO_FAST_SIDEWAYS, "too fast sideways"},
        {330, 0, 10, 0.3f, LAND_TILTED, "tilted"},
        {150, 0, 10, 0, LAND_OFF_PAD, "off the pad"},
    };
    for (int i = 0; i < 4; i++) {
        Flat(&g);
        Drop(&g, cases[i].x, cases[i].vx, cases[i].vy, cases[i].ang);
        g.fuel = 600.0f;
        long s0 = g.score;
        Game_ConsumeFrameFlags(&g);
        RunSteps(&g, 90);
        CHECK(g.state == SHIP_CRASHED && g.justCrash && g.lastResult == cases[i].want, cases[i].what);
        CHECK(g.score == s0 && fabsf(g.fuel - (600.0f - CRASH_FUEL_PENALTY)) < 0.01f, "crash: costs fuel, scores nothing");
    }

    /* Hull strike: a steeply rolled ship digs its shoulder in. */
    Flat(&g);
    Drop(&g, 330.0f, 0.0f, 10.0f, 1.4f);
    RunSteps(&g, 90);
    CHECK(g.state == SHIP_CRASHED && g.lastResult != LAND_OK, "crash: on its side is a crash");

    /* After a crash the ship comes back on the same moon, if there is fuel. */
    Flat(&g);
    Drop(&g, 150.0f, 0.0f, 10.0f, 0.0f);
    g.fuel = 700.0f;
    RunSteps(&g, 90);
    float terr0 = g.terrain[10];
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, (int)(CRASH_HOLD * STEP_HZ) + 5);
    CHECK(g.state == SHIP_FLYING && g.justRespawn && g.level == 1 && g.terrain[10] == terr0 && g.y < 100.0f, "crash: respawn at the top of the same level");
    CHECK(g.phase == GS_PLAYING, "crash: with fuel the run goes on");

    /* No fuel, no game. */
    Flat(&g);
    Drop(&g, 150.0f, 0.0f, 10.0f, 0.0f);
    g.fuel = 100.0f; g.score = 640;
    RunSteps(&g, 90);
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, (int)(CRASH_HOLD * STEP_HZ) + 5);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && g.fuel == 0.0f && g.highScore == 640, "game over: the crash that empties the tank ends the run");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "game over: pause can't undo it");
    Game_Restart(&g, 9);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.level == 1 && g.fuel == FUEL_START && g.highScore == 640, "restart: fresh run, best kept");
}

static void test_world(void) {
    Game g;
    Flat(&g);
    g.x = FIELD_W - 1.0f; g.y = 100.0f; g.vx = 30.0f;
    RunSteps(&g, 60);
    CHECK(g.x >= 0.0f && g.x < 30.0f, "wrap: flying off the right comes back on the left");
    g.x = 1.0f; g.vx = -30.0f;
    RunSteps(&g, 60);
    CHECK(g.x > FIELD_W - 40.0f && g.x < FIELD_W, "wrap: and the other way");

    Flat(&g);
    g.y = 30.0f;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 240);
    CHECK(g.y >= 14.0f && g.state == SHIP_FLYING, "sky: there's a ceiling, and hitting it is harmless");

    Flat(&g);
    g.terrain[10] = 300.0f; g.terrain[11] = 340.0f;
    CHECK(fabsf(Game_TerrainY(&g, 10 * TERR_STEP) - 300.0f) < 0.001f, "terrain: exact at a point");
    CHECK(fabsf(Game_TerrainY(&g, 10 * TERR_STEP + TERR_STEP / 2.0f) - 320.0f) < 0.001f, "terrain: interpolated between points");
    CHECK(Game_TerrainY(&g, -5.0f) == Game_TerrainY(&g, FIELD_W - 5.0f), "terrain: wraps");
    CHECK(Game_TerrainY(&g, 3.0f * FIELD_W + 100.0f) == Game_TerrainY(&g, 100.0f), "terrain: wraps many times over");
    g.x = 330.0f; g.y = 380.0f; g.angle = 0.0f;
    CHECK(fabsf(Game_Altitude(&g) - 10.0f) < 0.01f, "altitude: from the lowest foot");
    g.angle = 0.5f;
    CHECK(Game_Altitude(&g) < 10.0f, "altitude: a tilted ship's low foot is nearer");

    Game h;
    Game_Init(&h, 1, 0);
    h.phase = GS_PAUSED;
    float y = h.y;
    Game_Update(&h, 1.0f);
    CHECK(h.y == y, "pause: nothing moves");
    Game_TogglePause(&h);
    Game_Update(&h, 100.0f);
    CHECK(h.y < 200.0f, "update: a long hitch is clamped");
}

static void test_terrain(void) {
    bool ok = true, flat = true, edges = true, range = true, distinct = true, gaps = true, wrap = true;
    for (uint64_t seed = 1; seed <= 60; seed++) {
        for (int level = 1; level <= 14; level++) {
            Game g;
            Game_Init(&g, seed, 0);
            Game_BuildLevel(&g, level, seed);
            int want = level <= 3 ? 4 : 3;
            if (g.padCount != want) ok = false;
            int mask = 0;
            for (int i = 0; i < g.padCount; i++) {
                const Pad *p = &g.pads[i];
                float y = g.terrain[p->start];
                for (int k = 0; k < p->points; k++) if (g.terrain[p->start + k] != y) flat = false;
                if (p->start < 2 || p->start + p->points > TERR_N - 3) edges = false;
                if (p->points < 3) ok = false;
                if (mask & (1 << p->mult)) distinct = false;
                mask |= 1 << p->mult;
                if (p->mult == 5 && p->points > 3) ok = false;
                for (int j = 0; j < i; j++) {
                    const Pad *q = &g.pads[j];
                    if (p->start < q->start + q->points + 1 && q->start < p->start + p->points + 1) gaps = false;
                }
            }
            if (!(mask & (1 << 5))) ok = false; /* the x5 is always there */
            for (int i = 0; i < TERR_N; i++) if (g.terrain[i] < 200.0f || g.terrain[i] > 450.0f) range = false;
            if (g.terrain[0] != g.terrain[TERR_N - 1]) wrap = false;
        }
    }
    CHECK(ok, "terrain: 4 pads early and 3 later, every one at least 3 points wide, always an x5 no wider than 3");
    CHECK(flat, "terrain: every pad is perfectly flat");
    CHECK(edges, "terrain: pads keep clear of the seam");
    CHECK(distinct, "terrain: no two pads share a multiplier");
    CHECK(gaps, "terrain: pads never touch each other");
    CHECK(range, "terrain: heights stay between the sky and the floor");
    CHECK(wrap, "terrain: the two ends meet");

    Game a, b, c;
    Game_Init(&a, 77, 0); Game_Init(&b, 77, 0); Game_Init(&c, 78, 0);
    CHECK(memcmp(a.terrain, b.terrain, sizeof(a.terrain)) == 0 && memcmp(a.pads, b.pads, sizeof(a.pads)) == 0, "terrain: same seed, same moon");
    CHECK(memcmp(a.terrain, c.terrain, sizeof(a.terrain)) != 0, "terrain: different seed, different moon");
    Game_BuildLevel(&b, 2, 77);
    CHECK(memcmp(a.terrain, b.terrain, sizeof(a.terrain)) != 0, "terrain: each level is its own moon");
    Game_BuildLevel(&a, 2, 77);
    CHECK(memcmp(a.terrain, b.terrain, sizeof(a.terrain)) == 0, "terrain: but a level is always the same for a given seed");

    /* Rougher later. */
    double r1 = 0, r9 = 0;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        Game g;
        Game_Init(&g, seed, 0);
        for (int i = 1; i < TERR_N; i++) r1 += fabsf(g.terrain[i] - g.terrain[i - 1]);
        Game_BuildLevel(&g, 9, seed);
        for (int i = 1; i < TERR_N; i++) r9 += fabsf(g.terrain[i] - g.terrain[i - 1]);
    }
    CHECK(r9 > r1, "terrain: later levels are rougher");

    /* The ship never starts inside the ground. */
    bool clear = true;
    for (uint64_t seed = 1; seed <= 50; seed++) {
        Game g;
        Game_Init(&g, seed, 0);
        if (Game_Altitude(&g) < 100.0f) clear = false;
    }
    CHECK(clear, "start: the ship begins well above the surface");
}

/* A landing bot: steer its horizontal speed toward a pad, keep the descent
 * rate proportional to altitude, and go upright for the last stretch. If this
 * lands reliably, the numbers in game.h leave room for a human. */
static void Pilot(Game *g, int pad) {
    const Pad *p = &g->pads[pad];
    float tx = (Game_PadX0(p) + Game_PadX1(p)) * 0.5f;
    float dx = tx - g->x;
    if (dx > FIELD_W / 2.0f) dx -= FIELD_W;
    if (dx < -FIELD_W / 2.0f) dx += FIELD_W;
    float alt = Game_Altitude(g);
    float wantVx = dx * 0.30f;
    if (wantVx > 45.0f) wantVx = 45.0f;
    if (wantVx < -45.0f) wantVx = -45.0f;
    if (fabsf(dx) < 6.0f) wantVx = 0.0f;
    float wantAngle = (wantVx - g->vx) * 0.045f - Game_Wind(g) / GRAVITY; /* lean into the wind, as a person would */
    if (wantAngle > 0.6f) wantAngle = 0.6f;
    if (wantAngle < -0.6f) wantAngle = -0.6f;
    if (alt < 45.0f) wantAngle = (wantVx - g->vx) * 0.01f - Game_Wind(g) / THRUST * 0.9f;
    float lim = 0.06f;
    if (wantAngle > lim && alt < 45.0f) wantAngle = lim;
    if (wantAngle < -lim && alt < 45.0f) wantAngle = -lim;
    float da = wantAngle - g->angle;
    float rot = da > 0.03f ? 1.0f : (da < -0.03f ? -1.0f : 0.0f);
    float wantVy = 6.0f + alt * 0.28f;
    if (wantVy > 60.0f) wantVy = 60.0f;
    /* Don't descend onto the pad until roughly above it. */
    if (fabsf(dx) > 30.0f && alt < 90.0f) wantVy = -8.0f;
    bool thrust = g->vy > wantVy && fabsf(g->angle) < 0.7f;
    /* In wind, also burn a little to hold position over the pad (the bot's wind feed-forward only helps while thrusting). */
    if (Game_Wind(g) != 0.0f && alt < 90.0f && fabsf(g->vx - wantVx) > 5.0f && fabsf(g->angle) < 0.13f && (g->vx - wantVx) * g->angle < 0.0f && g->vy > -6.0f) thrust = true;
    Game_SetInput(g, rot, thrust);
}

static int BestPad(const Game *g) {
    int best = 0;
    for (int i = 1; i < g->padCount; i++) if (g->pads[i].points > g->pads[best].points) best = i;
    return best;
}

static void test_weather_and_pods(void) {
    Game g;
    Game_Init(&g, 5, 0);
    CHECK(Game_Wind(&g) == 0.0f, "wind: the first moons are calm");
    Game_BuildLevel(&g, WIND_FIRST_LEVEL, 5);
    bool moves = false, capped = true;
    for (int i = 0; i < 40; i++) {
        g.flightTime = (float)i;
        float w = Game_Wind(&g);
        if (fabsf(w) > 0.5f) moves = true;
        if (fabsf(w) > WIND_MAX + 0.001f) capped = false;
    }
    CHECK(moves, "wind: a windy moon pushes");
    Game_BuildLevel(&g, 30, 5);
    for (int i = 0; i < 400; i++) { g.flightTime = (float)i * 0.5f; if (fabsf(Game_Wind(&g)) > WIND_MAX + 0.001f) capped = false; }
    CHECK(capped, "wind: never stronger than the cap");

    /* Wind actually moves an unpowered ship sideways. */
    Game_BuildLevel(&g, 6, 5);
    g.x = 300.0f; g.y = 100.0f; g.vx = 0.0f; g.vy = 0.0f;
    float sign = g.windBase > 0 ? 1.0f : -1.0f;
    Game_SetInput(&g, 0.0f, false);
    for (int i = 0; i < 60; i++) Game_Step(&g);
    CHECK(g.vx * sign > 0.5f, "wind: an unpowered ship drifts downwind");

    /* Pods sit in open sky and give fuel + points once. */
    bool clear = true;
    for (int lvl = 1; lvl <= 15; lvl++) {
        Game_BuildLevel(&g, lvl, 9);
        if (g.podCount < 2) clear = false;
        for (int i = 0; i < g.podCount; i++) {
            if (g.pods[i].y < 100.0f || g.pods[i].y >= Game_TerrainY(&g, g.pods[i].x) - 40.0f + 1.0f) { if (g.pods[i].y > 111.0f) clear = false; }
        }
    }
    CHECK(clear, "pods: always at least two, always above the ground");
    Game_BuildLevel(&g, 1, 9);
    g.fuel = 500.0f;
    g.x = g.pods[0].x; g.y = g.pods[0].y; g.vx = g.vy = 0.0f;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justPod && g.pods[0].taken && g.fuel > 500.0f + POD_FUEL - 5.0f && g.score == POD_POINTS, "pods: flying through one refuels and scores");
    Game_ConsumeFrameFlags(&g);
    g.x = g.pods[0].x; g.y = g.pods[0].y; g.vx = g.vy = 0.0f;
    Game_Step(&g);
    CHECK(!g.justPod && g.score == POD_POINTS, "pods: each pays out once");
    /* The ground is unchanged by the new randomness: same seed, same terrain. */
    Game a, b;
    Game_Init(&a, 3, 0); Game_Init(&b, 3, 0);
    CHECK(memcmp(a.terrain, b.terrain, sizeof(a.terrain)) == 0 && a.pods[1].x == b.pods[1].x, "pods: deterministic per seed");
}

static void test_bot(void) {
    int landed = 0, tried = 0, crashes = 0, windLanded = 0, windCrashed = 0;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        Game g;
        Game_Init(&g, seed, 0);
        int levelsDone = 0;
        for (int i = 0; i < 120 * 200 && g.phase == GS_PLAYING && levelsDone < 4; i++) {
            if (g.state == SHIP_FLYING) Pilot(&g, BestPad(&g));
            Game_ConsumeFrameFlags(&g);
            Game_Step(&g);
            if (g.justLand) { landed++; levelsDone++; if (g.level >= WIND_FIRST_LEVEL) windLanded++; }
            if (g.justCrash) { if (g.level < WIND_FIRST_LEVEL) crashes++; else windCrashed++; }
            if (g.justNewLevel) tried++;
        }
        tried++;
    }
    printf("  (bot: %d landings, %d crashes over %d attempts)\n", landed, crashes, tried);
    CHECK(landed >= 40 * 3, "bot: a simple pilot lands on the widest pad, level after level, so the numbers are fair");
    CHECK(crashes * 5 < landed, "bot: and rarely crashes on the calm moons");
    /* This bot is crude about wind (it can't hold station over a pad the way a person can),
     * so the bar here is only "landing is still the usual outcome". */
    CHECK(windLanded > windCrashed, "bot: in wind, landings still outnumber crashes");

    /* Fuel it needs: a level should not cost the whole tank. */
    Game g;
    Game_Init(&g, 3, 0);
    float start = g.fuel;
    for (int i = 0; i < 120 * 60 && g.state != SHIP_LANDED; i++) { if (g.state == SHIP_FLYING) Pilot(&g, BestPad(&g)); Game_Step(&g); }
    CHECK(g.state == SHIP_LANDED && start - g.fuel < 500.0f, "bot: a level costs well under half the tank");

    /* Determinism. */
    Game a, b;
    Game_Init(&a, 5, 0); Game_Init(&b, 5, 0);
    for (int i = 0; i < 120 * 30; i++) {
        if (a.state == SHIP_FLYING) Pilot(&a, BestPad(&a));
        if (b.state == SHIP_FLYING) Pilot(&b, BestPad(&b));
        Game_Step(&a); Game_Step(&b);
    }
    CHECK(a.x == b.x && a.y == b.y && a.score == b.score && a.fuel == b.fuel, "determinism: same seed, same flight");
}

int main(void) {
    test_freefall();
    test_rotation_and_fuel();
    test_classifier();
    test_touchdown();
    test_world();
    test_terrain();
    test_weather_and_pods();
    test_bot();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
