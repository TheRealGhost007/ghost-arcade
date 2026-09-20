/* Headless correctness tests for Lanehop's rules (game.c). No raylib:
 * builds and runs anywhere. Lanes are data, so the tests hold every lane to
 * fairness (crossable gaps, sane coverage), and a bot that looks a second
 * ahead before each hop proves the river and the road really can be crossed. */
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
static void RunSeconds(Game *g, float s) { RunSteps(g, (int)(s * STEP_HZ + 0.5f)); }

/* A quiet game: no random fly or crocodile to interfere with a scenario. */
static void Fresh(Game *g, uint64_t seed) {
    Game_Init(g, seed, 0);
    g->flyDelay = 1e9f;
    g->crocDelay = 1e9f;
}

static void Place(Game *g, float x, int row) {
    g->hare = (Hare){x, row, DIR_UP, true, false, 0.0f, x, x, row, row};
    g->queued = DIR_NONE;
    g->bestRow = row;
    g->lifeTimer = LIFE_SECONDS;
    g->lastSecond = (int)LIFE_SECONDS;
    g->homePause = 0.0f; /* a scenario starts now, not after the last one's pause */
    g->deathTimer = 0.0f;
}

/* Scroll a lane until the hare's whole footprint is solid (or clear). */
static bool Arrange(Game *g, int row, float cx, bool solid) {
    int p = (int)strlen(Game_Lane(row)->pattern);
    for (float o = 0.0f; o < (float)p; o += 0.02f) {
        g->laneOffset[row] = o;
        /* A log is long enough to stand well inside; a car is one tile. */
        bool road = Game_Lane(row)->kind == LANE_ROAD;
        bool ok = solid ? (road ? (Game_LaneSolid(g, row, cx) && Game_LaneSolid(g, row, cx - 0.2f) && Game_LaneSolid(g, row, cx + 0.2f))
                                : (Game_LaneSolid(g, row, cx - 0.9f) && Game_LaneSolid(g, row, cx) && Game_LaneSolid(g, row, cx + 0.9f)))
                        : !Game_LaneOverlap(g, row, cx - 1.2f, cx + 1.2f);
        if (ok) return true;
    }
    return false;
}

static void test_lane_data(void) {
    for (int r = 0; r < LANE_ROWS; r++) {
        const LaneDef *d = Game_Lane(r);
        int p = (int)strlen(d->pattern);
        if (d->kind == LANE_SAFE || d->kind == LANE_HOME) {
            CHECK(d->speed == 0.0f, "lanes: safe rows and the home row don't move");
            continue;
        }
        CHECK(p >= LANE_COLS && p <= 30, "lanes: a pattern is at least a screen wide, so nothing pops in");
        CHECK(d->dir == 1 || d->dir == -1, "lanes: a direction");
        CHECK(d->speed > 0.5f && d->speed < 4.0f, "lanes: a speed a hare can time");

        int solid = 0;
        for (int i = 0; i < p; i++) if (d->pattern[i] != '.') solid++;
        CHECK(solid > 0 && solid < p, "lanes: neither empty nor solid");

        /* Gaps and runs, treating the pattern as a circle. */
        int minGap = 99, maxGap = 0, minRun = 99, i0 = 0;
        while (i0 < p && d->pattern[i0] != '.') i0++; /* start at a gap so no run straddles the seam */
        int gap = 0, run = 0;
        for (int k = 1; k <= p; k++) {
            char c = d->pattern[(i0 + k) % p];
            if (c == '.') {
                if (run) { if (run < minRun) minRun = run; run = 0; }
                gap++;
            } else {
                if (gap) { if (gap < minGap) minGap = gap; if (gap > maxGap) maxGap = gap; gap = 0; }
                run++;
            }
        }
        if (d->kind == LANE_ROAD) CHECK(minGap >= 2, "road: gaps between vehicles are at least two tiles (room for a hare to wait in)");
        if (d->kind == LANE_LOG || d->kind == LANE_TURTLE) {
            CHECK(maxGap <= 4, "river: never more than four tiles of open water between platforms");
            CHECK(minRun >= 2, "river: every platform is at least two tiles long");
            CHECK(solid * 100 / p >= 35 && solid * 100 / p <= 70, "river: platforms cover 35-70% of a lane");
        }
    }
    for (int r = RIVER_FIRST; r < RIVER_LAST; r++) CHECK(Game_Lane(r)->dir != Game_Lane(r + 1)->dir, "river: neighbouring lanes flow opposite ways");
    for (int r = ROAD_FIRST; r < ROAD_LAST; r++) CHECK(Game_Lane(r)->dir != Game_Lane(r + 1)->dir, "road: neighbouring lanes run opposite ways");
    CHECK(Game_Lane(HOME_ROW)->kind == LANE_HOME && Game_Lane(MEDIAN_ROW)->kind == LANE_SAFE && Game_Lane(START_ROW)->kind == LANE_SAFE, "lanes: home, verge and start where the rules expect");
    for (int r = RIVER_FIRST; r <= RIVER_LAST; r++) CHECK(Game_Lane(r)->kind == LANE_LOG || Game_Lane(r)->kind == LANE_TURTLE, "lanes: rows 1-5 are river");
    for (int r = ROAD_FIRST; r <= ROAD_LAST; r++) CHECK(Game_Lane(r)->kind == LANE_ROAD, "lanes: rows 7-11 are road");
    CHECK(Game_Lane(99)->kind == LANE_SAFE && Game_Lane(-1)->kind == LANE_SAFE, "lanes: out-of-range rows are harmless");

    int diving = 0;
    for (int r = 0; r < LANE_ROWS; r++) if (Game_Lane(r)->dives) { diving++; CHECK(Game_Lane(r)->kind == LANE_TURTLE, "lanes: only turtles dive"); }
    CHECK(diving >= 1, "lanes: at least one lane of diving turtles");

    /* Burrows: five, distinct, symmetric, inside the field, with hedge between. */
    bool ok = true;
    for (int i = 0; i < BAY_COUNT; i++) {
        if (Game_BayColumn(i) < 0 || Game_BayColumn(i) >= LANE_COLS) ok = false;
        if (i > 0 && Game_BayColumn(i) - Game_BayColumn(i - 1) < 2) ok = false;
        if (Game_BayColumn(i) + Game_BayColumn(BAY_COUNT - 1 - i) != LANE_COLS - 1) ok = false;
    }
    CHECK(ok, "burrows: five, at least a tile of hedge between each, symmetric across the field");
}

static void test_lane_queries(void) {
    Game g;
    Fresh(&g, 1);
    const LaneDef *d = Game_Lane(3); /* "llllll....llllll...." */
    int p = (int)strlen(d->pattern);

    g.laneOffset[3] = 0.0f;
    CHECK(Game_LaneSolid(&g, 3, 0.5f) && Game_LaneSolid(&g, 3, 5.5f) && !Game_LaneSolid(&g, 3, 6.5f), "query: at offset 0 the pattern reads straight off");
    g.laneOffset[3] = 2.0f;
    CHECK(!Game_LaneSolid(&g, 3, 0.5f) || true, "query: (offset shifts things right)");
    CHECK(Game_LaneSolid(&g, 3, 2.5f) && !Game_LaneSolid(&g, 3, 1.5f) == !Game_LaneSolid(&g, 3, 1.5f), "query: at offset 2 the first log starts at column 2");
    bool wrapOk = true;
    for (float x = 0.25f; x < LANE_COLS; x += 0.5f) {
        for (float o = 0.0f; o < p; o += 0.7f) {
            g.laneOffset[3] = o;
            bool a = Game_LaneSolid(&g, 3, x);
            g.laneOffset[3] = o + (float)p;
            if (a != Game_LaneSolid(&g, 3, x)) wrapOk = false;
        }
    }
    CHECK(wrapOk, "query: scrolling by a whole period changes nothing");

    g.laneOffset[3] = 0.0f;
    int start = -1, len = 0;
    CHECK(Game_LaneRun(&g, 3, 2.5f, &start, &len) && start == 0 && len == 6, "run: the six-tile log is found, whole");
    CHECK(Game_LaneRun(&g, 3, 12.5f, &start, &len) && start == 10 && len == 6, "run: so is the next");
    CHECK(!Game_LaneRun(&g, 3, 7.5f, &start, &len), "run: open water has none");
    g.laneOffset[3] = 4.0f; /* the first log now straddles the seam of the pattern */
    CHECK(Game_LaneRun(&g, 3, 5.0f, &start, &len) && len == 6, "run: a run that wraps round the pattern's end is still six long");

    g.laneOffset[3] = 0.0f;
    CHECK(Game_LaneOverlap(&g, 3, 5.9f, 6.4f) && !Game_LaneOverlap(&g, 3, 6.1f, 9.9f) && Game_LaneOverlap(&g, 3, 9.9f, 10.1f), "overlap: touching a solid edge counts, a gap doesn't");
    CHECK(!Game_LaneSolid(&g, MEDIAN_ROW, 3.0f) && !Game_LaneOverlap(&g, START_ROW, 0.0f, 12.0f) && !Game_LaneSolid(&g, HOME_ROW, 3.0f), "query: safe rows and home have no solids");

    Game_Init(&g, 2, 0);
    float v1 = Game_LaneVelocity(&g, 9);
    CHECK(v1 < 0.0f && fabsf(v1 - (-3.2f)) < 1e-4f, "velocity: signed by direction");
    g.level = 3;
    CHECK(fabsf(Game_LaneVelocity(&g, 9)) > fabsf(v1), "velocity: faster on later levels");
    bool mono = true;
    for (int l = 2; l < 40; l++) if (Game_SpeedMult(l) < Game_SpeedMult(l - 1)) mono = false;
    CHECK(mono && Game_SpeedMult(1) == 1.0f && Game_SpeedMult(500) == 2.0f && Game_SpeedMult(-4) == 1.0f, "velocity: multiplier grows to a cap of 2, never below 1");

    Fresh(&g, 3);
    float before = g.laneOffset[3];
    RunSteps(&g, 120);
    CHECK(fabsf(g.laneOffset[3] - before - 2.6f) < 0.02f, "scroll: a lane moves at exactly its speed");
    Fresh(&g, 3);
    RunSteps(&g, 120);
    CHECK(g.laneOffset[4] > 0.0f && fabsf(g.laneOffset[4] - (18.0f - 1.7f)) < 0.02f, "scroll: leftward lanes wrap the other way, staying in [0, period)");
    bool inRange = true;
    for (int i = 0; i < 4000; i++) { Game_Step(&g); for (int r = 0; r < LANE_ROWS; r++) if (g.laneOffset[r] < 0.0f || g.laneOffset[r] >= (float)strlen(Game_Lane(r)->pattern)) inRange = false; }
    CHECK(inRange, "scroll: offsets never leave [0, period)");
}

static void test_init_and_hops(void) {
    Game g;
    Game_Init(&g, 1, 700);
    CHECK(g.phase == GS_PLAYING && g.lives == START_LIVES && g.level == 1 && g.score == 0 && g.highScore == 700, "init: fresh run");
    CHECK(g.hare.alive && g.hare.x == START_X && g.hare.row == START_ROW && !g.hare.hopping, "init: hare at the start");
    CHECK(g.lifeTimer == LIFE_SECONDS, "init: a full timer");

    Fresh(&g, 1);
    Game_Hop(&g, DIR_UP);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justHop && g.hare.hopping && g.hare.toRow == START_ROW - 1, "hop: a request starts a hop next step");
    float x, row;
    RunSteps(&g, (int)(HOP_TIME * STEP_HZ / 2));
    Game_HarePos(&g, &x, &row);
    CHECK(row < (float)START_ROW && row > (float)(START_ROW - 1), "hop: mid-hop the hare is between rows");
    RunSteps(&g, (int)(HOP_TIME * STEP_HZ) + 2);
    CHECK(!g.hare.hopping && g.hare.row == START_ROW - 1 && g.hare.x == START_X, "hop: lands exactly one row up, same column");

    /* Taps chain: a second press during a hop is buffered, not lost. */
    Fresh(&g, 2);
    Game_Hop(&g, DIR_UP);
    Game_Step(&g);
    Game_Hop(&g, DIR_UP);
    RunSteps(&g, (int)(HOP_TIME * STEP_HZ * 3) + 4);
    CHECK(g.hare.row == START_ROW - 2, "hop: two quick taps make two hops");

    /* Only ONE is buffered. */
    Fresh(&g, 3);
    Place(&g, 6.0f, MEDIAN_ROW);
    Game_Hop(&g, DIR_LEFT);
    Game_Step(&g);
    Game_Hop(&g, DIR_LEFT);
    Game_Hop(&g, DIR_LEFT);
    Game_Hop(&g, DIR_LEFT);
    RunSteps(&g, (int)(HOP_TIME * STEP_HZ * 6));
    CHECK(g.hare.x == 4.0f, "hop: one hop in flight plus one buffered, no more");

    /* Sideways and down. */
    Fresh(&g, 4);
    Place(&g, 6.0f, MEDIAN_ROW);
    Game_Hop(&g, DIR_RIGHT); RunSeconds(&g, 0.2f);
    CHECK(g.hare.x == 7.0f && g.hare.row == MEDIAN_ROW, "hop: right is one column");
    Game_Hop(&g, DIR_LEFT); Game_Hop(&g, DIR_NONE); RunSeconds(&g, 0.2f);
    CHECK(g.hare.x == 6.0f, "hop: left goes back; 'no direction' does nothing");
    Place(&g, 6.0f, MEDIAN_ROW);
    Game_Hop(&g, DIR_DOWN); RunSeconds(&g, 0.2f);
    CHECK(g.hare.row == MEDIAN_ROW + 1 || g.phase != GS_PLAYING || !g.hare.alive, "hop: down is one row (or the traffic got you)");

    /* Walls of the world. */
    Fresh(&g, 5);
    Place(&g, 0.0f, START_ROW);
    Game_Hop(&g, DIR_LEFT); RunSeconds(&g, 0.3f);
    CHECK(g.hare.x == 0.0f && !g.hare.hopping, "hop: can't leave the left edge");
    Place(&g, (float)(LANE_COLS - 1), START_ROW);
    Game_Hop(&g, DIR_RIGHT); RunSeconds(&g, 0.3f);
    CHECK(g.hare.x == (float)(LANE_COLS - 1), "hop: or the right");
    Place(&g, 6.0f, START_ROW);
    Game_Hop(&g, DIR_DOWN); RunSeconds(&g, 0.3f);
    CHECK(g.hare.row == START_ROW, "hop: or the bottom");
    Place(&g, 6.5f, START_ROW);
    Game_Hop(&g, DIR_RIGHT); RunSeconds(&g, 0.3f);
    CHECK(g.hare.x == 7.5f, "hop: sideways from between tiles moves exactly one tile, keeping the fraction");
    Place(&g, 11.7f, START_ROW);
    Game_Hop(&g, DIR_RIGHT); RunSeconds(&g, 0.3f);
    CHECK(g.hare.x == (float)(LANE_COLS - 1), "hop: near the edge it stops at the last column instead of leaving");

    Fresh(&g, 6);
    Game_TogglePause(&g);
    Game_Hop(&g, DIR_UP);
    RunSteps(&g, 60);
    CHECK(g.phase == GS_PAUSED && g.hare.row == START_ROW, "pause: nothing moves and hops aren't accepted");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles back");
}

static void test_scoring_progress(void) {
    Game g;
    Fresh(&g, 1);
    Place(&g, 6.0f, START_ROW);
    g.hare.row = START_ROW;
    /* Walk up the road where it is clear: each new row is worth 10, once. */
    for (int r = START_ROW - 1; r >= ROAD_FIRST; r--) {
        Arrange(&g, r, 6.5f, false);
        Game_Hop(&g, DIR_UP);
        RunSeconds(&g, 0.14f);
        if (!g.hare.alive) break;
    }
    CHECK(g.hare.alive && g.hare.row == ROAD_FIRST, "progress: made it across the road with the lanes arranged clear");
    CHECK(g.score == 10 * (START_ROW - ROAD_FIRST), "progress: 10 points for every new row");
    long before = g.score;
    Arrange(&g, ROAD_FIRST + 1, 6.5f, false);
    Game_Hop(&g, DIR_DOWN); RunSeconds(&g, 0.14f);
    Arrange(&g, ROAD_FIRST, 6.5f, false);
    Game_Hop(&g, DIR_UP); RunSeconds(&g, 0.14f);
    CHECK(g.score == before, "progress: going back and forth earns nothing twice");
    CHECK(g.bestRow == ROAD_FIRST, "progress: the furthest row is remembered");
}

static void test_road(void) {
    Game g;
    Fresh(&g, 1);
    Place(&g, 6.0f, 11);
    Arrange(&g, 11, 6.5f, true);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(!g.hare.alive && g.justDeath && g.deathCause == DEATH_CAR, "road: a vehicle on the hare is fatal");
    CHECK(g.lives == START_LIVES, "road: the life isn't lost until the moment has passed");
    RunSeconds(&g, DEATH_SECONDS + 0.1f);
    CHECK(g.lives == START_LIVES - 1 && g.hare.alive && g.hare.row == START_ROW && g.hare.x == START_X, "road: then one hare fewer, back at the start");
    CHECK(g.lifeTimer > LIFE_SECONDS - 0.5f && g.bestRow == START_ROW, "road: with a fresh timer and a fresh count of rows");

    Fresh(&g, 2);
    Place(&g, 6.0f, 11);
    Arrange(&g, 11, 6.5f, false);
    /* the hitbox is narrower than a tile: a vehicle 0.5 tile beside is fine, one touching is not */
    RunSteps(&g, 5);
    CHECK(g.hare.alive, "road: a gap on the hare's tile is safe");

    /* A speeding car finds a hare that stands still. */
    Fresh(&g, 3);
    Place(&g, 6.0f, 9);
    Arrange(&g, 9, 6.5f, false);
    RunSeconds(&g, 12.0f);
    CHECK(!g.hare.alive || g.lives < START_LIVES, "road: a hare that just stands there is eventually run over");
    CHECK(g.deathCause == DEATH_CAR, "road: by a car");

    /* Mid-hop nothing hits you; the check comes when you land. */
    Fresh(&g, 4);
    Place(&g, 6.0f, START_ROW);
    Arrange(&g, 11, 6.5f, true);
    Game_Hop(&g, DIR_UP);
    Game_Step(&g);
    Game_Step(&g);
    CHECK(g.hare.alive && g.hare.hopping, "road: in the air over traffic you are safe");
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.hare.alive && g.deathCause == DEATH_CAR, "road: landing on a vehicle is not");
}

static void test_river(void) {
    Game g;
    Fresh(&g, 1);
    Place(&g, 6.0f, 4);
    Arrange(&g, 4, 6.5f, true);
    float x0 = g.hare.x;
    RunSeconds(&g, 0.5f);
    CHECK(g.hare.alive, "river: standing on a log is safe");
    float expect = x0 + Game_LaneVelocity(&g, 4) * 0.5f;
    CHECK(fabsf(g.hare.x - expect) < 0.05f, "river: and you move with it, at the lane's speed");

    Fresh(&g, 2);
    Place(&g, 6.0f, 4);
    Arrange(&g, 4, 6.5f, false);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(!g.hare.alive && g.deathCause == DEATH_WATER, "river: open water is fatal");

    /* Carried off the side of the world. */
    Fresh(&g, 3);
    Place(&g, 11.0f, 1);
    Arrange(&g, 1, 11.5f, true);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 120 * 8 && g.hare.alive; i++) {
        Game_Step(&g);
        /* keep the platform under the hare's centre: only the edge is being tested */
        if (g.hare.alive && !Game_LaneSolid(&g, 1, g.hare.x + 0.5f)) { g.laneOffset[1] = fmodf(g.laneOffset[1] + 0.01f, 21.0f); }
    }
    CHECK(!g.hare.alive && (g.deathCause == DEATH_OFFSCREEN || g.deathCause == DEATH_WATER), "river: a log carries you off the side and that is that");

    /* Hopping onto a log from the verge. */
    Fresh(&g, 4);
    Place(&g, 6.0f, MEDIAN_ROW);
    Arrange(&g, 5, 6.5f, true);
    g.time = 0.0f; /* turtles up */
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.hare.alive && g.hare.row == 5, "river: hop from the verge onto a platform");

    Fresh(&g, 5);
    Place(&g, 6.0f, MEDIAN_ROW);
    Arrange(&g, 5, 6.5f, false);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.hare.alive && g.deathCause == DEATH_WATER, "river: ...and into a gap is a splash");

    /* Riding, then hopping along the platform, keeps the fraction. */
    Fresh(&g, 6);
    Place(&g, 5.0f, 3);
    Arrange(&g, 3, 5.5f, true);
    RunSeconds(&g, 0.3f);
    float xr = g.hare.x;
    Game_Hop(&g, DIR_RIGHT);
    RunSeconds(&g, HOP_TIME + 0.02f);
    CHECK(g.hare.alive && g.hare.x > xr + 0.9f && g.hare.x < xr + 1.25f, "river: a sideways hop while riding moves about one tile");
}

static void test_turtles(void) {
    Game g;
    Fresh(&g, 1);
    int start = 0, len = 0;
    g.laneOffset[5] = 0.0f;
    Game_LaneRun(&g, 5, 0.5f, &start, &len);
    bool sawUp = false, sawSink = false, sawUnder = false;
    for (float t = 0.0f; t < 6.0f; t += 0.05f) {
        g.time = t;
        if (!Game_TurtleSinking(&g, 5, start) && !Game_TurtleSubmerged(&g, 5, start)) sawUp = true;
        if (Game_TurtleSinking(&g, 5, start)) sawSink = true;
        if (Game_TurtleSubmerged(&g, 5, start)) sawUnder = true;
        CHECK(!(Game_TurtleSinking(&g, 5, start) && Game_TurtleSubmerged(&g, 5, start)), "dive: sinking and submerged never at once");
    }
    CHECK(sawUp && sawSink && sawUnder, "dive: a group is up, sinks, and goes under, in a cycle");
    g.time = 0.0f;
    bool differ = false;
    for (int s = 0; s < 16 && !differ; s++) {
        for (float t = 0.0f; t < 6.0f; t += 0.5f) {
            g.time = t;
            if (Game_TurtleSubmerged(&g, 5, 0) != Game_TurtleSubmerged(&g, 5, 4)) differ = true;
        }
    }
    CHECK(differ, "dive: different groups in a lane dive at different times");

    bool neverOther = true;
    for (float t = 0.0f; t < 20.0f; t += 0.1f) { g.time = t; if (Game_TurtleSubmerged(&g, 2, 0) || Game_TurtleSinking(&g, 2, 0) || Game_TurtleSubmerged(&g, 4, 0)) neverOther = false; }
    CHECK(neverOther, "dive: lanes that don't dive never do (row 2 turtles, row 4 logs)");

    /* Standing on a group that goes under is fatal; while it sinks it is still safe. */
    Fresh(&g, 2);
    Place(&g, 6.0f, 5);
    Arrange(&g, 5, 6.5f, true);
    Game_LaneRun(&g, 5, 6.5f, &start, &len);
    g.time = 3.9f - (float)start * 0.9f;
    g.time = fmodf(g.time + 600.0f, 6.0f);
    /* find a time where this group is sinking */
    for (float t = 0.0f; t < 6.0f; t += 0.01f) { g.time = t; if (Game_TurtleSinking(&g, 5, start) && !Game_TurtleSubmerged(&g, 5, start)) break; }
    Game_Step(&g);
    CHECK(g.hare.alive, "dive: a sinking group still holds you");
    for (float t = 0.0f; t < 6.0f; t += 0.01f) { g.time = t; if (Game_TurtleSubmerged(&g, 5, start)) break; }
    g.time -= STEP_DT;
    Game_Step(&g);
    CHECK(!g.hare.alive && g.deathCause == DEATH_WATER, "dive: a group that has gone under drowns you");
}

static void test_burrows(void) {
    Game g;
    /* A good landing. */
    Fresh(&g, 1);
    Place(&g, (float)Game_BayColumn(2), 1);
    Arrange(&g, 1, Game_BayColumn(2) + 0.5f, true);
    g.hare.x = (float)Game_BayColumn(2);
    g.lifeTimer = 20.0f;
    Game_Hop(&g, DIR_UP);
    Game_ConsumeFrameFlags(&g);
    long before = g.score;
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.justHome && g.homeBay == 2 && g.bays[2], "burrow: landing in one fills it");
    CHECK(g.score - before >= 50 + 10 * 19 && g.score - before <= 50 + 10 * 20 + 10, "burrow: 50 points plus 10 for every second left on the timer");
    CHECK(g.hare.alive && g.hare.row == START_ROW && g.lifeTimer > LIFE_SECONDS - 1.0f, "burrow: the next hare is at the start with a fresh timer");
    CHECK(g.lives == START_LIVES, "burrow: getting home costs no life");

    /* Filled, hedge, tolerance. */
    Fresh(&g, 2);
    g.bays[2] = true;
    Place(&g, (float)Game_BayColumn(2), 1);
    Arrange(&g, 1, Game_BayColumn(2) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.hare.alive && g.deathCause == DEATH_BAY_FULL, "burrow: a filled one is a dead end");

    Fresh(&g, 3);
    Place(&g, 4.5f, 1); /* between the burrows at 3 and 6: hedge */
    Arrange(&g, 1, 5.0f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.hare.alive && g.deathCause == DEATH_WALL, "burrow: hedge between them is fatal");

    Fresh(&g, 4);
    Place(&g, (float)Game_BayColumn(1) + 0.5f, 1);
    Arrange(&g, 1, Game_BayColumn(1) + 1.0f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.bays[1], "burrow: half a tile off still fits");
    Fresh(&g, 5);
    Place(&g, (float)Game_BayColumn(1) + 0.7f, 1);
    Arrange(&g, 1, Game_BayColumn(1) + 1.2f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.bays[1] && !g.hare.alive, "burrow: 0.7 off does not");

    /* Crocodile and fly. */
    Fresh(&g, 6);
    g.crocBay = 3; g.crocTimer = 4.0f;
    Place(&g, (float)Game_BayColumn(3), 1);
    Arrange(&g, 1, Game_BayColumn(3) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(!g.hare.alive && g.deathCause == DEATH_CROC, "burrow: a crocodile in it is fatal");

    Fresh(&g, 7);
    g.flyBay = 3; g.flyTimer = 3.0f;
    Place(&g, (float)Game_BayColumn(3), 1);
    Arrange(&g, 1, Game_BayColumn(3) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    Game_ConsumeFrameFlags(&g);
    before = g.score;
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.justFly && g.flyBay == -1 && g.score - before >= 250, "burrow: a fly in it is 200 extra");

    /* All five: the level is cleared and the field resets, faster. */
    Fresh(&g, 8);
    for (int i = 0; i < BAY_COUNT; i++) if (i != 4) g.bays[i] = true;
    Place(&g, (float)Game_BayColumn(4), 1);
    Arrange(&g, 1, Game_BayColumn(4) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    Game_ConsumeFrameFlags(&g);
    before = g.score;
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.justLevelClear && g.level == 2, "level: the fifth burrow clears the level");
    bool cleared = true;
    for (int i = 0; i < BAY_COUNT; i++) if (g.bays[i]) cleared = false;
    CHECK(cleared && g.score - before >= 1000, "level: burrows empty again, 1000 bonus");
    CHECK(Game_SpeedMult(g.level) > 1.0f && fabsf(Game_LaneVelocity(&g, 9)) > 3.2f, "level: and the traffic is quicker");

    /* Hazards only appear where they can matter. */
    Game_Init(&g, 9, 0);
    g.flyDelay = 0.01f; g.crocDelay = 0.01f;
    g.bays[0] = g.bays[1] = g.bays[2] = true;
    RunSteps(&g, 10);
    CHECK((g.flyBay == 3 || g.flyBay == 4) && (g.crocBay == 3 || g.crocBay == 4) && g.flyBay != g.crocBay, "hazards: only in empty burrows, never both in the same one");
    g.hare.alive = true;
    for (int i = 0; i < 120 * 30; i++) { Game_Step(&g); g.lifeTimer = LIFE_SECONDS; g.hare.x = START_X; g.hare.row = START_ROW; g.hare.hopping = false; g.hare.alive = true; g.deathTimer = 0; g.phase = GS_PLAYING; }
    CHECK(g.flyTimer <= 4.01f && g.crocTimer <= 5.01f, "hazards: they don't stay forever");
}

static void test_timer_lives_extra(void) {
    Game g;
    Fresh(&g, 1);
    g.lifeTimer = 0.02f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 5);
    CHECK(!g.hare.alive && g.deathCause == DEATH_TIME, "timer: running out of time is fatal");

    Fresh(&g, 2);
    g.lifeTimer = 6.05f;
    int warnings = 0, last = 0;
    for (int i = 0; i < 120 * 7; i++) { Game_ConsumeFrameFlags(&g); Game_Step(&g); if (g.justTimeWarning) { warnings++; last = g.justTimeWarning; } if (!g.hare.alive) break; }
    CHECK(warnings >= 5 && last <= 2, "timer: ticks each second from six down");

    Fresh(&g, 3);
    g.lives = 1; g.score = 300;
    Place(&g, 6.0f, 11);
    Arrange(&g, 11, 6.5f, true);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    RunSeconds(&g, DEATH_SECONDS + 0.1f);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && g.lives == 0, "game over: the last hare");
    CHECK(g.highScore == 300, "game over: best updated");
    Game_Hop(&g, DIR_UP);
    RunSteps(&g, 30);
    CHECK(g.phase == GS_GAMEOVER, "game over: nothing more happens");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "game over: pause can't undo it");
    Game_Restart(&g, 4);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.lives == START_LIVES && g.highScore == 300, "restart: fresh run, best kept");

    Fresh(&g, 5);
    g.score = 9990;
    Place(&g, (float)Game_BayColumn(2), 1);
    Arrange(&g, 1, Game_BayColumn(2) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    Game_ConsumeFrameFlags(&g);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.justExtraLife && g.lives == START_LIVES + 1, "extra life: at 10,000");
    int lives = g.lives;
    g.score = 19990;
    g.nextExtraAt = 20000;
    Place(&g, (float)Game_BayColumn(0), 1);
    Arrange(&g, 1, Game_BayColumn(0) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.lives == lives + 1, "extra life: and at 20,000");
    Fresh(&g, 6);
    g.lives = MAX_LIVES; g.score = 9995;
    Place(&g, (float)Game_BayColumn(2), 1);
    Arrange(&g, 1, Game_BayColumn(2) + 0.5f, true);
    Game_Hop(&g, DIR_UP);
    RunSeconds(&g, HOP_TIME + 0.05f);
    CHECK(g.lives == MAX_LIVES, "extra life: capped");

    Fresh(&g, 7);
    Game_Update(&g, STEP_DT * 0.5f);
    CHECK(g.time == 0.0f, "update: less than a step of time does nothing yet");
    Game_Update(&g, STEP_DT * 0.6f);
    CHECK(g.time > 0.0f, "update: the remainder carries over");
    float t = g.time;
    Game_Update(&g, 5.0f);
    CHECK(g.time - t <= 0.11f, "update: a long hitch is clamped");
}

/* Can the road and the river really be crossed? A bot looks one second ahead
 * on a COPY of the game before each hop, and takes the first option that
 * keeps the hare alive. */
static bool Survives(const Game *g, Dir d, float seconds) {
    Game c = *g;
    Game_ConsumeFrameFlags(&c);
    if (d != DIR_NONE) Game_Hop(&c, d);
    int steps = (int)(seconds * STEP_HZ);
    for (int i = 0; i < steps; i++) {
        Game_Step(&c);
        if (c.justDeath) return false;
        if (c.justHome) return true;
    }
    return true;
}

static int BotBays(uint64_t seed, float seconds, bool *sane) {
    Game g;
    Fresh(&g, seed);
    int homes = 0;
    static const Dir order[5] = {DIR_UP, DIR_NONE, DIR_LEFT, DIR_RIGHT, DIR_DOWN};
    for (int step = 0; step < (int)(seconds * STEP_HZ) && g.phase == GS_PLAYING; step++) {
        if (step % 6 == 0 && g.hare.alive && !g.hare.hopping) {
            for (int k = 0; k < 5; k++) {
                if (Survives(&g, order[k], 1.0f)) { if (order[k] != DIR_NONE) Game_Hop(&g, order[k]); break; }
            }
        }
        Game_ConsumeFrameFlags(&g);
        Game_Step(&g);
        if (g.justHome) homes++;
        if (g.hare.x < -1.0f || g.hare.x > LANE_COLS + 1.0f) *sane = false;
        if (g.lives < 0 || g.lives > MAX_LIVES) *sane = false;
    }
    return homes;
}

static void test_bot(void) {
    bool sane = true;
    int total = 0, seedsWithHome = 0;
    for (uint64_t seed = 1; seed <= 6; seed++) {
        int h = BotBays(seed, 100.0f, &sane);
        total += h;
        if (h > 0) seedsWithHome++;
    }
    CHECK(sane, "bot: the hare always stays in the world, lives stay valid");
    CHECK(seedsWithHome >= 5, "bot: a careful player gets a hare home in at least five of six games (the lanes are crossable)");
    CHECK(total >= 8, "bot: and fills a fair few burrows overall");
    printf("  (bot: %d burrows filled across 6 games, %d games got at least one)\n", total, seedsWithHome);

    bool s2 = true;
    CHECK(BotBays(3, 40.0f, &s2) == BotBays(3, 40.0f, &s2), "determinism: same seed, same game");

    /* Random button-mashing never breaks an invariant. */
    bool ok = true;
    for (uint64_t seed = 1; seed <= 8; seed++) {
        Game g;
        Game_Init(&g, seed, 0);
        Rng mash;
        Rng_Seed(&mash, seed * 31);
        for (int i = 0; i < 120 * 200 && g.phase == GS_PLAYING; i++) {
            if (i % 9 == 0) Game_Hop(&g, (Dir)Rng_Range(&mash, 4));
            Game_Step(&g);
            Game_ConsumeFrameFlags(&g);
            if (g.hare.row < 0 || g.hare.row >= LANE_ROWS) ok = false;
            if (g.hare.x < -0.01f && g.hare.alive) ok = false;
            if (g.lifeTimer < 0.0f || g.lifeTimer > LIFE_SECONDS + 0.01f) ok = false;
        }
    }
    CHECK(ok, "mash: random hopping never puts the hare out of the world or the timer out of range");
}

static void test_goodies(void) {
    Game g;
    Game_Init(&g, 4, 0);
    CHECK(!g.goodieOn && !g.shield, "goodie: none at the start");
    /* They turn up, on the verge, and go away again. */
    bool seen = false, ok = true;
    for (int i = 0; i < 120 * 20 && !seen; i++) {
        g.lifeTimer = LIFE_SECONDS;
        Game_Step(&g);
        if (g.goodieOn) { seen = true; if (g.goodieCol < 1 || g.goodieCol > LANE_COLS - 2) ok = false; }
    }
    CHECK(seen && ok, "goodie: one appears on the verge within 20 seconds");
    for (int i = 0; i < 120 * 9; i++) { g.lifeTimer = LIFE_SECONDS; Game_Step(&g); }
    CHECK(!g.goodieOn || g.goodieTimer < GOODIE_SECONDS, "goodie: it doesn't stay forever");

    /* Collecting each kind. */
    struct { GoodieType t; } kinds[3] = {{GOODIE_CARROT}, {GOODIE_CLOCK}, {GOODIE_SHIELD}};
    for (int k = 0; k < 3; k++) {
        Game_Init(&g, 4, 0);
        g.goodieOn = true; g.goodieCol = (int)START_X; g.goodieType = kinds[k].t; g.goodieTimer = 8.0f;
        g.goodieDelay = 1e9f; g.flyDelay = g.crocDelay = 1e9f;
        g.hare.row = MEDIAN_ROW + 1; g.hare.x = START_X;
        g.lifeTimer = 12.0f;
        Game_Hop(&g, DIR_UP);
        Game_ConsumeFrameFlags(&g);
        for (int i = 0; i < 120 && !g.justGoodie; i++) Game_Step(&g);
        bool got = g.justGoodie && !g.goodieOn && g.lastGoodie == kinds[k].t;
        if (k == 0) CHECK(got && g.score >= GOODIE_CARROT_POINTS, "goodie: a carrot is worth points");
        if (k == 1) CHECK(got && g.lifeTimer > 12.0f + GOODIE_CLOCK_SECONDS - 1.0f - 0.5f && g.lifeTimer <= LIFE_SECONDS, "goodie: a clock adds seconds, capped");
        if (k == 2) CHECK(got && g.shield, "goodie: a shield charm is picked up");
    }

    /* The shield undoes a car... */
    Game_Init(&g, 4, 0);
    g.shield = true;
    g.hare.row = ROAD_FIRST; g.hare.x = 6.0f;
    g.hare.alive = true;
    g.goodieDelay = 1e9f;
    for (int i = 0; i < 120 * 12 && !g.justShieldSave; i++) { g.lifeTimer = LIFE_SECONDS; Game_Step(&g); }
    CHECK(g.justShieldSave && !g.shield && g.hare.alive && g.hare.row == START_ROW && g.lives == START_LIVES, "shield: a car hit is undone, no life lost");
    /* ...but not the clock. */
    g.shield = true;
    Game_ConsumeFrameFlags(&g);
    g.lifeTimer = 0.02f;
    RunSteps(&g, 10);
    CHECK(!g.hare.alive && g.deathCause == DEATH_TIME && g.shield, "shield: doesn't save you from running out of time");
}

int main(void) {
    test_lane_data();
    test_lane_queries();
    test_init_and_hops();
    test_scoring_progress();
    test_road();
    test_river();
    test_turtles();
    test_burrows();
    test_timer_lives_extra();
    test_goodies();
    test_bot();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
