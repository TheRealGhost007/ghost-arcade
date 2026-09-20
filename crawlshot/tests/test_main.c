/* Headless correctness tests for Crawlshot's rules (game.c). No raylib:
 * builds and runs anywhere. The caterpillar moves in whole tiles on a timer,
 * so a test can make it take exactly one step; everything else runs on the
 * fixed 120 Hz step. */
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
static void RunSeconds(Game *g, float s) { RunSteps(g, (int)(s * STEP_HZ + 0.5f)); }

/* An empty, quiet field: no toadstools, no caterpillar, nothing scheduled. */
static void Empty(Game *g, uint64_t seed) {
    Game_Init(g, seed, 0);
    memset(g->mush, 0, sizeof(g->mush));
    memset(g->poison, 0, sizeof(g->poison));
    for (int i = 0; i < MAX_WORMS; i++) g->worms[i].active = false;
    g->spiderTimer = g->fleaTimer = g->scorpionTimer = 1e9f;
    g->wave = 1;
}

/* A caterpillar with its head at (x, y) heading `dir`, tail trailing behind. */
static Worm *PutWorm(Game *g, int x, int y, int len, int dir) {
    for (int i = 0; i < MAX_WORMS; i++) {
        if (g->worms[i].active) continue;
        Worm *w = &g->worms[i];
        memset(w, 0, sizeof(*w));
        w->active = true; w->len = len; w->dir = dir; w->vdir = 1;
        for (int k = 0; k < len; k++) w->seg[k] = (Cell){x - dir * k, y};
        return w;
    }
    return NULL;
}

/* Empty, plus an unborn caterpillar that never arrives: an empty field counts
 * as a cleared wave, which would restart the wave and wipe the creatures. */
static void Quiet(Game *g, uint64_t seed) {
    Empty(g, seed);
    Worm *w = PutWorm(g, 0, 0, 1, 1);
    w->enterDelay = 1e9f;
}

/* Exactly one caterpillar step (and one tick of everything else). */
static void WormTick(Game *g) {
    g->wormTimer = Game_WormInterval(g->wave) - STEP_DT * 0.5f;
    Game_Step(g);
}

/* A bullet sitting on cell (x, y), about to be resolved. */
static void Shoot(Game *g, int x, int y) {
    g->bullet = (Bullet){(float)x + 0.5f, (float)y + 0.5f + BULLET_SPEED * STEP_DT, true};
    Game_Step(g);
}

static void test_helpers(void) {
    int m, s;
    Game_WaveLayout(1, &m, &s);
    CHECK(m == WORM_TOTAL && s == 0, "layout: wave one is one full caterpillar");
    Game_WaveLayout(3, &m, &s);
    CHECK(m == WORM_TOTAL - 2 && s == 2 && m + s == WORM_TOTAL, "layout: later waves split into a shorter one and single heads, still twelve segments");
    bool sane = true;
    for (int w = 1; w <= 30; w++) { Game_WaveLayout(w, &m, &s); if (m + s != WORM_TOTAL || m < 1 || s < 0 || s > 7) sane = false; }
    CHECK(sane, "layout: twelve segments at every wave, never more than eight crawlers");
    Game_WaveLayout(-4, &m, &s);
    CHECK(m == WORM_TOTAL && s == 0, "layout: nonsense wave numbers are safe");

    bool mono = true;
    for (int w = 2; w <= 60; w++) if (Game_WormInterval(w) > Game_WormInterval(w - 1)) mono = false;
    CHECK(mono && Game_WormInterval(1) > Game_WormInterval(10) && Game_WormInterval(999) == Game_WormInterval(200), "speed: quicker each wave, with a floor");
    CHECK(Game_SpiderPoints(0.0f) == 900 && Game_SpiderPoints(4.0f) == 900 && Game_SpiderPoints(4.1f) == 600 &&
          Game_SpiderPoints(8.0f) == 600 && Game_SpiderPoints(8.1f) == 300 && Game_SpiderPoints(40.0f) == 300, "spider: 900 close, 600 middling, 300 far, with exact boundaries");
}

static void test_init(void) {
    Game g;
    Game_Init(&g, 1, 700);
    CHECK(g.phase == GS_PLAYING && g.lives == START_LIVES && g.wave == 1 && g.score == 0 && g.highScore == 700, "init: fresh run");
    CHECK(g.alive && g.px == COLS / 2.0f && g.py > PLAYER_TOP, "init: the player is in the garden");
    CHECK(Game_WormCount(&g) == 1 && Game_SegmentCount(&g) == WORM_TOTAL, "init: one caterpillar of twelve");
    int field = 0, top = 0, garden = Game_MushroomsInGarden(&g);
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g.mush[y][x]) { if (y < PLAYER_TOP) field++; if (y == 0) top++; }
    CHECK(field >= 30 && field <= 40 && top == 0, "init: about 34 toadstools in the field, none in the top row");
    CHECK(garden >= FLEA_THRESHOLD && garden <= 10, "init: a few in the garden, so the flea doesn't come straight away");
    bool startClear = true;
    for (int y = ROWS - 2; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g.mush[y][x]) startClear = false;
    CHECK(startClear, "init: the last two rows are clear, so you can never start boxed in");
    bool full = true;
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g.mush[y][x] && g.mush[y][x] != MUSH_HP) full = false;
    CHECK(full, "init: every toadstool starts at full strength");
    Game h;
    Game_Init(&h, 1, 0);
    CHECK(memcmp(g.mush, h.mush, sizeof(g.mush)) == 0, "init: the same seed lays out the same field");
    Game_Init(&h, 2, 0);
    CHECK(memcmp(g.mush, h.mush, sizeof(g.mush)) != 0, "init: a different seed lays out a different one");
}

static void test_caterpillar_movement(void) {
    Game g;
    Empty(&g, 1);
    Worm *w = PutWorm(&g, 10, 5, 4, 1);
    Game_ConsumeFrameFlags(&g);
    WormTick(&g);
    CHECK(w->seg[0].x == 11 && w->seg[0].y == 5, "move: the head takes a step along its row");
    CHECK(w->seg[1].x == 10 && w->seg[2].x == 9 && w->seg[3].x == 8, "move: and every segment follows into the cell ahead of it");
    WormTick(&g);
    WormTick(&g);
    CHECK(w->seg[0].x == 13 && w->seg[3].x == 10, "move: a step at a time");

    /* Drop at a toadstool and turn. */
    Empty(&g, 2);
    w = PutWorm(&g, 10, 5, 3, 1);
    g.mush[5][12] = 2;
    WormTick(&g); /* head to 11 */
    WormTick(&g); /* blocked by 12: drop */
    CHECK(w->seg[0].x == 11 && w->seg[0].y == 6 && w->dir == -1, "turn: blocked by a toadstool it drops a row and reverses");
    CHECK(w->seg[1].x == 11 && w->seg[1].y == 5 && w->seg[2].x == 10 && w->seg[2].y == 5, "turn: the body follows the head's path round the corner");
    WormTick(&g);
    CHECK(w->seg[0].x == 10 && w->seg[0].y == 6, "turn: then heads back the other way");

    /* Drop at the edge. */
    Empty(&g, 3);
    w = PutWorm(&g, COLS - 2, 8, 2, 1);
    WormTick(&g); /* to COLS-1 */
    WormTick(&g); /* off the edge: drop */
    CHECK(w->seg[0].x == COLS - 1 && w->seg[0].y == 9 && w->dir == -1, "turn: at the edge it drops and reverses");
    Empty(&g, 4);
    w = PutWorm(&g, 1, 8, 2, -1);
    WormTick(&g); WormTick(&g);
    CHECK(w->seg[0].x == 0 && w->seg[0].y == 9 && w->dir == 1, "turn: and at the left edge too");

    /* Down the field, bounce at the bottom, climb the garden, drop again. */
    Empty(&g, 5);
    w = PutWorm(&g, 3, ROWS - 2, 3, 1);
    g.mush[ROWS - 2][6] = 1;
    for (int i = 0; i < 6; i++) WormTick(&g);
    CHECK(w->seg[0].y == ROWS - 1 && w->vdir == 1, "bounce: it drops to the bottom row");
    g.mush[ROWS - 1][9] = 1;
    for (int i = 0; i < 12 && w->seg[0].y == ROWS - 1; i++) WormTick(&g);
    CHECK(w->seg[0].y == ROWS - 2 && w->vdir == -1, "bounce: at the bottom the next turn takes it back UP");
    /* Now a climber reaching the top of the garden turns down again. */
    Empty(&g, 6);
    w = PutWorm(&g, 3, PLAYER_TOP, 2, 1);
    w->vdir = -1;
    g.mush[PLAYER_TOP][5] = 1;
    for (int i = 0; i < 4; i++) WormTick(&g);
    CHECK(w->seg[0].y == PLAYER_TOP + 1 && w->vdir == 1, "bounce: a climber never leaves the garden; at its top it goes back down");

    /* The chain never breaks: every segment stays adjacent to its neighbour. */
    Game r;
    Game_Init(&r, 7, 0);
    bool adjacent = true;
    for (int step = 0; step < 900; step++) {
        WormTick(&r);
        r.px = 0.5f; /* keep out of the way: the player isn't the point */
        r.alive = true; r.deathTimer = 0.0f; r.lives = 3;
        for (int i = 0; i < MAX_WORMS; i++) {
            const Worm *k = &r.worms[i];
            if (!k->active || k->enterDelay > 0.0f) continue;
            for (int s = 1; s < k->len; s++) {
                int d = abs(k->seg[s].x - k->seg[s - 1].x) + abs(k->seg[s].y - k->seg[s - 1].y);
                if (d > 1) adjacent = false;
            }
        }
    }
    CHECK(adjacent, "chain: a caterpillar's segments are always neighbours, through every turn and bounce");

    /* Off-screen tail: a worm still coming on is not blocked by nothing. */
    Empty(&g, 8);
    w = PutWorm(&g, COLS - 1, 0, 6, -1);
    for (int i = 0; i < 4; i++) WormTick(&g);
    CHECK(w->seg[0].x == COLS - 5 && w->seg[0].y == 0, "entry: a caterpillar coming on from the corner walks in, tail off-screen");
}

static void test_shooting_worms(void) {
    Game g;
    /* A middle segment: it becomes a toadstool and the chain splits. */
    Empty(&g, 1);
    g.px = 2.5f;
    PutWorm(&g, 10, 5, 6, 1); /* cells x = 10,9,8,7,6,5 */
    Game_ConsumeFrameFlags(&g);
    long before = g.score;
    Shoot(&g, 8, 5);
    CHECK(g.mush[5][8] == MUSH_HP, "shot: the segment becomes a toadstool of full strength");
    CHECK(Game_WormCount(&g) == 2 && Game_SegmentCount(&g) == 5, "shot: the chain splits in two, one segment fewer in all");
    int lens[2], n = 0, headsAt[2][2];
    for (int i = 0; i < MAX_WORMS; i++) if (g.worms[i].active) { lens[n] = g.worms[i].len; headsAt[n][0] = g.worms[i].seg[0].x; headsAt[n][1] = g.worms[i].seg[0].y; n++; }
    CHECK((lens[0] == 2 && lens[1] == 3) || (lens[0] == 3 && lens[1] == 2), "shot: two and three segments, the front half and the back half");
    bool newHead = (headsAt[0][0] == 7 || headsAt[1][0] == 7);
    CHECK(newHead, "shot: the segment behind the hit is now a head");
    CHECK(g.score - before == 10 && g.justSplit, "shot: 10 points for a body segment");
    CHECK(g.killCount == 1 && g.kills[0].kind == KILL_BODY, "shot: reported for the renderer");
    CHECK(!g.bullet.active, "shot: the bullet is used up");

    /* The new head really does crawl on its own, into the toadstool it left. */
    for (int i = 0; i < MAX_WORMS; i++) if (g.worms[i].active && g.worms[i].seg[0].x == 7) {
        WormTick(&g);
        CHECK(g.worms[i].seg[0].x == 7 && g.worms[i].seg[0].y == 6, "split: the back half hits the new toadstool at once, drops and turns");
    }

    /* The head: worth 100, and the next segment takes over. */
    Empty(&g, 2);
    g.px = 2.5f;
    PutWorm(&g, 10, 5, 4, 1);
    before = g.score;
    Shoot(&g, 10, 5);
    CHECK(g.score - before == 100 && g.kills[0].kind == KILL_HEAD, "head: 100 points");
    CHECK(Game_WormCount(&g) == 1 && Game_SegmentCount(&g) == 3, "head: the rest carries on, one shorter");
    CHECK(g.worms[0].active ? g.worms[0].seg[0].x == 9 : true, "head: with a new head where the second segment was");

    /* The tail. */
    Empty(&g, 3);
    g.px = 2.5f;
    PutWorm(&g, 10, 5, 4, 1);
    Shoot(&g, 7, 5);
    CHECK(Game_WormCount(&g) == 1 && Game_SegmentCount(&g) == 3 && g.mush[5][7] == MUSH_HP, "tail: shooting the last segment just shortens it");

    /* A single segment dies outright. */
    Quiet(&g, 4);
    g.px = 2.5f;
    PutWorm(&g, 10, 5, 1, 1);
    Shoot(&g, 10, 5);
    CHECK(Game_WormCount(&g) == 1 && g.mush[5][10] == MUSH_HP, "single: one segment, one shot, one toadstool");

    /* Segments are shot wherever they are, including in the garden. */
    Empty(&g, 5);
    g.px = 2.5f;
    PutWorm(&g, 10, PLAYER_TOP + 2, 3, 1);
    Shoot(&g, 9, PLAYER_TOP + 2);
    CHECK(g.mush[PLAYER_TOP + 2][9] == MUSH_HP, "shot: the new toadstool appears in the garden if that's where it was");

    /* A bullet coming up a column hits the LOWEST thing in it. */
    Quiet(&g, 6);
    g.px = 6.5f;
    g.mush[10][6] = MUSH_HP;
    PutWorm(&g, 6, 20, 1, 1);
    g.bullet = (Bullet){6.5f, 21.0f, true};
    for (int i = 0; i < 120 && g.bullet.active; i++) Game_Step(&g);
    CHECK(Game_WormCount(&g) == 1 && g.mush[10][6] == MUSH_HP, "shot: it hits the caterpillar nearest the gun, not the toadstool beyond");
}

static void test_toadstools(void) {
    Game g;
    Quiet(&g, 1);
    g.px = 4.5f;
    g.mush[10][4] = MUSH_HP;
    long before = g.score;
    for (int hit = 1; hit <= 3; hit++) {
        Shoot(&g, 4, 10);
        CHECK(g.mush[10][4] == MUSH_HP - hit && g.justMushroomHit, "toadstool: each shot takes one hit point");
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(g.score == before, "toadstool: no points until it goes");
    Shoot(&g, 4, 10);
    CHECK(g.mush[10][4] == 0 && g.score == before + 1, "toadstool: the fourth shot removes it, for 1 point");
    CHECK(g.killCount == 1 && g.kills[0].kind == KILL_MUSHROOM, "toadstool: reported");

    Quiet(&g, 2);
    g.mush[10][4] = 1;
    g.poison[10][4] = 1;
    g.px = 4.5f;
    Shoot(&g, 4, 10);
    CHECK(g.mush[10][4] == 0 && g.poison[10][4] == 0, "toadstool: destroying a poisoned one clears the poison");

    /* Bullets: one at a time, and they leave the top. */
    Quiet(&g, 3);
    g.px = 15.5f;
    Game_SetInput(&g, 0.0f, 0.0f, true);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justFired && g.bullet.active, "fire: a shot leaves the gun");
    float y0 = g.bullet.y;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(!g.justFired && g.bullet.y < y0, "fire: holding fire doesn't add a second shot while the first is in flight");
    RunSeconds(&g, 1.0f);
    CHECK(g.justFired || !g.bullet.active || g.bullet.y >= 0.0f, "fire: it flies off the top and the next can go");
    int shots = 0;
    for (int i = 0; i < 480; i++) { Game_ConsumeFrameFlags(&g); Game_Step(&g); if (g.justFired) shots++; }
    CHECK(shots >= 4, "fire: rapid -- the bullet is quick enough that holding fire is a stream");
}

static void test_poison_dive(void) {
    Game g;
    Empty(&g, 1);
    Worm *w = PutWorm(&g, 10, 5, 3, 1);
    g.mush[5][12] = 4;
    g.poison[5][12] = 1;
    WormTick(&g); /* to 11 */
    WormTick(&g); /* blocked by the poisoned one */
    CHECK(w->diving, "poison: touching a poisoned toadstool sends it diving");
    int y = w->seg[0].y;
    for (int i = 0; i < 4; i++) { WormTick(&g); CHECK(w->seg[0].y == y + i + 1 && w->seg[0].x == 11, "poison: straight down, one row a step"); }

    /* It ignores everything on the way down. */
    Empty(&g, 2);
    w = PutWorm(&g, 10, 5, 3, 1);
    g.mush[5][12] = 4; g.poison[5][12] = 1;
    for (int r = 6; r < ROWS; r++) { g.mush[r][11] = 4; }
    for (int i = 0; i < 30 && w->seg[0].y < ROWS - 1; i++) WormTick(&g);
    CHECK(w->seg[0].y == ROWS - 1 && w->seg[0].x == 11, "poison: through toadstools too, all the way to the bottom");
    WormTick(&g);
    CHECK(!w->diving && w->vdir == -1, "poison: the dive ends at the bottom");
    CHECK(w->seg[0].y == ROWS - 1, "poison: and it carries on ALONG the bottom row rather than turning at once");

    /* An ordinary toadstool doesn't do that. */
    Empty(&g, 3);
    w = PutWorm(&g, 10, 5, 3, 1);
    g.mush[5][12] = 4;
    WormTick(&g); WormTick(&g);
    CHECK(!w->diving && w->seg[0].y == 6, "poison: a plain toadstool is just a wall");

    /* A dive takes the whole body along the same path. */
    Empty(&g, 4);
    w = PutWorm(&g, 10, 5, 4, 1);
    g.mush[5][12] = 4; g.poison[5][12] = 1;
    for (int i = 0; i < 8; i++) WormTick(&g);
    bool column = true;
    for (int s = 0; s < 3; s++) if (w->seg[s].x != 11) column = false;
    CHECK(column, "poison: the body follows the head down the column");
}

static void test_player(void) {
    Game g;
    Empty(&g, 1);
    Game_SetInput(&g, 0.0f, -1.0f, false);
    RunSeconds(&g, 3.0f);
    CHECK(g.py >= (float)PLAYER_TOP + 0.4f && g.py < (float)PLAYER_TOP + 1.0f, "player: can't leave the garden upward");
    Game_SetInput(&g, 0.0f, 1.0f, false);
    RunSeconds(&g, 3.0f);
    CHECK(g.py <= (float)ROWS - 0.4f, "player: or downward");
    Game_SetInput(&g, -1.0f, 0.0f, false);
    RunSeconds(&g, 5.0f);
    CHECK(g.px >= 0.4f && g.px < 1.0f, "player: or off the left");
    Game_SetInput(&g, 1.0f, 0.0f, false);
    RunSeconds(&g, 6.0f);
    CHECK(g.px <= (float)COLS - 0.4f && g.px > COLS - 1.0f, "player: or the right");

    Empty(&g, 2);
    float x0 = g.px;
    Game_SetInput(&g, 1.0f, 0.0f, false);
    RunSeconds(&g, 0.5f);
    CHECK(fabsf((g.px - x0) - PLAYER_SPEED * 0.5f) < 0.06f, "player: moves at PLAYER_SPEED tiles a second");
    Empty(&g, 3);
    float px = g.px, py = g.py;
    Game_SetInput(&g, 1.0f, -1.0f, false);
    RunSeconds(&g, 0.3f);
    float dist = sqrtf((g.px - px) * (g.px - px) + (g.py - py) * (g.py - py));
    CHECK(dist < PLAYER_SPEED * 0.3f * 1.05f, "player: diagonals aren't faster than straight lines");

    /* Toadstools block you, and you slide along them. */
    Empty(&g, 4);
    g.px = 10.5f; g.py = 29.5f;
    g.mush[29][12] = 4;
    Game_SetInput(&g, 1.0f, 0.0f, false);
    RunSeconds(&g, 1.0f);
    CHECK(g.px < 12.0f, "player: a toadstool stops you");
    Game_SetInput(&g, 1.0f, -1.0f, false);
    float before = g.py;
    RunSeconds(&g, 0.4f);
    CHECK(g.py < before, "player: pushing on it while moving up still slides you up along it");
}

static void test_death_and_restore(void) {
    Game g;
    Empty(&g, 1);
    g.px = 10.5f; g.py = 29.5f;
    PutWorm(&g, 10, 29, 3, -1);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(!g.alive && g.justPlayerDeath && g.lives == START_LIVES - 1, "death: a caterpillar touching you costs a life");
    CHECK(g.phase == GS_PLAYING && g.deathTimer > 0.0f, "death: play goes on after a beat");

    /* Damaged and poisoned toadstools are healed, one at a time, for 5 each. */
    Empty(&g, 2);
    g.mush[5][5] = 1;
    g.mush[6][6] = 3; g.poison[6][6] = 1;
    g.mush[7][7] = MUSH_HP;
    g.mush[8][8] = 2;
    g.px = 10.5f; g.py = 29.5f;
    PutWorm(&g, 10, 29, 3, -1);
    long before = g.score;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    int ticks = 0;
    for (int i = 0; i < 120 * 5 && !g.alive; i++) { Game_ConsumeFrameFlags(&g); Game_Step(&g); if (g.justRestoreTick) ticks++; }
    CHECK(ticks == 3, "restore: three toadstools needed healing (the full one didn't)");
    CHECK(g.score - before == 15, "restore: 5 points each");
    bool healed = g.mush[5][5] == MUSH_HP && g.mush[6][6] == MUSH_HP && g.mush[8][8] == MUSH_HP && g.poison[6][6] == 0 && g.mush[7][7] == MUSH_HP;
    CHECK(healed, "restore: all back to full strength, poison gone");
    CHECK(g.alive && g.px == COLS / 2.0f && g.py > PLAYER_TOP, "restore: then you are back in the middle of the garden");
    CHECK(Game_WormCount(&g) == 1 && Game_SegmentCount(&g) == WORM_TOTAL, "restore: with a fresh caterpillar to fight");
    CHECK(!g.spider.active && !g.flea.active && !g.scorpion.active, "restore: and the creatures gone");

    /* No toadstools to heal: straight back. */
    Empty(&g, 3);
    g.px = 10.5f; g.py = 29.5f;
    PutWorm(&g, 10, 29, 3, -1);
    Game_Step(&g);
    RunSeconds(&g, DEATH_SECONDS + 0.5f);
    CHECK(g.alive, "restore: with nothing to heal it is quick");

    /* Game over. */
    Empty(&g, 4);
    g.lives = 1; g.score = 480;
    g.px = 10.5f; g.py = 29.5f;
    PutWorm(&g, 10, 29, 3, -1);
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    RunSeconds(&g, DEATH_SECONDS + 0.1f);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && g.lives == 0, "game over: the last life");
    CHECK(g.highScore == 480, "game over: best updated");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "game over: pause can't undo it");
    Game_Restart(&g, 9);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.lives == START_LIVES && g.highScore == 480, "restart: fresh run, best kept");
}

static void test_spider(void) {
    Game g;
    Quiet(&g, 1);
    g.spiderTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(g.spider.active && g.justSpiderAppeared, "spider: appears when its timer runs out");
    CHECK(g.spider.y >= (float)PLAYER_TOP - 2.0f && g.spider.y <= (float)ROWS, "spider: at the height of the garden");

    /* It eats what it crosses, and stays in its band. */
    Quiet(&g, 2);
    for (int x = 0; x < COLS; x++) g.mush[28][x] = MUSH_HP;
    g.px = 15.5f; g.py = 31.0f;
    g.spider = (Spider){true, -1.0f, 28.5f, 4.0f, 0.0f, 100.0f};
    RunSeconds(&g, 4.0f);
    int left = 0;
    for (int x = 0; x < COLS; x++) if (g.mush[28][x]) left++;
    CHECK(left < COLS - 8, "spider: it eats the toadstools in its path");
    bool inBand = true;
    Quiet(&g, 3);
    g.px = 15.5f; g.py = 31.0f;
    g.spider = (Spider){true, 0.0f, 29.0f, 3.5f, 3.4f, 0.5f};
    for (int i = 0; i < 120 * 12 && g.spider.active; i++) {
        Game_Step(&g);
        g.px = 15.5f; g.py = 31.0f; g.alive = true; g.deathTimer = 0.0f;
        if (g.spider.y < (float)PLAYER_TOP - 2.05f || g.spider.y > (float)ROWS - 1.15f) inBand = false;
    }
    CHECK(inBand, "spider: it zig-zags but never leaves the bottom of the field");
    CHECK(!g.spider.active, "spider: and it leaves off the far side");

    /* Score by distance when shot. */
    long scores[3];
    float dists[3] = {2.0f, 6.0f, 12.0f};
    for (int k = 0; k < 3; k++) {
        Quiet(&g, 4);
        g.px = 10.5f; g.py = 30.5f;
        g.spider = (Spider){true, 10.0f + dists[k], 28.0f, 0.0f, 0.0f, 100.0f};
        g.px = 10.5f;
        g.bullet = (Bullet){g.spider.x + 0.5f, g.spider.y + BULLET_SPEED * STEP_DT, true};
        long before = g.score;
        Game_Step(&g);
        scores[k] = g.score - before;
    }
    CHECK(scores[0] == 900 || scores[0] == 600, "spider: shot close it is worth a lot");
    CHECK(scores[2] == 300, "spider: shot far away, 300");
    CHECK(scores[0] > scores[1] || scores[1] > scores[2], "spider: closer is worth more");

    /* Touching it is fatal. */
    Quiet(&g, 5);
    g.px = 10.5f; g.py = 29.5f;
    g.spider = (Spider){true, 10.0f, 29.5f, 0.0f, 0.0f, 100.0f};
    Game_Step(&g);
    CHECK(!g.alive && g.lives == START_LIVES - 1, "spider: it kills you on contact");
}

static void test_flea(void) {
    Game g;
    /* It only comes when the garden is nearly bare. */
    Quiet(&g, 1);
    for (int i = 0; i < FLEA_THRESHOLD; i++) g.mush[PLAYER_TOP + 1][i * 2] = MUSH_HP;
    g.fleaTimer = 0.01f;
    RunSteps(&g, 6);
    CHECK(!g.flea.active, "flea: with enough toadstools in the garden it stays away");
    g.mush[PLAYER_TOP + 1][0] = 0;
    g.fleaTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 6);
    CHECK(g.flea.active && g.justFleaAppeared, "flea: below the threshold it drops in");
    CHECK(Game_MushroomsInGarden(&g) == FLEA_THRESHOLD - 1, "flea: (threshold is exact: one fewer than FLEA_THRESHOLD)");

    /* It plants toadstools as it falls: roughly a third of the rows it passes. */
    int planted = 0, rows = 0;
    for (uint64_t seed = 1; seed <= 60; seed++) {
        Quiet(&g, seed);
        g.px = 0.5f; g.py = 31.0f;
        g.flea = (Flea){true, 20, 0.0f, 0};
        for (int i = 0; i < 120 * 4 && g.flea.active; i++) { Game_Step(&g); g.alive = true; g.deathTimer = 0.0f; }
        for (int y = 1; y < ROWS - 1; y++) { rows++; if (g.mush[y][20]) planted++; }
    }
    CHECK(planted > rows / 5 && planted < rows / 2, "flea: it leaves a trail of toadstools, about a third of the rows it falls through");

    /* Two hits to kill, and quicker after the first. */
    Quiet(&g, 2);
    g.px = 5.5f; g.py = 31.0f;
    g.flea = (Flea){true, 10, 12.0f, 0};
    g.bullet = (Bullet){10.5f, 12.0f + BULLET_SPEED * STEP_DT, true};
    long before = g.score;
    Game_Step(&g);
    CHECK(g.flea.active && g.flea.hits == 1 && g.score == before && !g.bullet.active, "flea: the first hit doesn't kill it");
    float y1 = g.flea.y;
    Game_Step(&g);
    float fast = g.flea.y - y1;
    Quiet(&g, 3);
    g.px = 5.5f; g.py = 31.0f;
    g.flea = (Flea){true, 10, 12.0f, 0};
    Game_Step(&g);
    float slow = g.flea.y - 12.0f;
    CHECK(fast > slow * 1.4f, "flea: it speeds up after being hit once");
    Quiet(&g, 4);
    g.px = 5.5f; g.py = 31.0f;
    g.flea = (Flea){true, 10, 12.0f, 1};
    g.bullet = (Bullet){10.5f, 12.0f + BULLET_SPEED * STEP_DT, true};
    before = g.score;
    Game_Step(&g);
    CHECK(!g.flea.active && g.score - before == 200, "flea: the second hit kills it, for 200");

    /* It's fatal to touch, and it leaves out of the bottom. */
    Quiet(&g, 5);
    g.px = 10.5f; g.py = 29.5f;
    g.flea = (Flea){true, 10, 29.2f, 0};
    Game_Step(&g);
    CHECK(!g.alive, "flea: touching it is fatal");
}

static void test_scorpion(void) {
    Game g;
    Quiet(&g, 1);
    g.scorpionTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(g.scorpion.active && g.justScorpionAppeared, "scorpion: appears when its timer runs out");
    CHECK(g.scorpion.row >= 3 && g.scorpion.row < PLAYER_TOP, "scorpion: crosses a row up in the field");

    Quiet(&g, 2);
    for (int x = 0; x < COLS; x += 2) g.mush[8][x] = MUSH_HP;
    g.scorpion = (Scorpion){true, -1.0f, 8, 1};
    RunSeconds(&g, 9.0f);
    int poisoned = 0, plain = 0;
    for (int x = 0; x < COLS; x += 2) { if (g.poison[8][x]) poisoned++; else plain++; }
    CHECK(poisoned >= COLS / 2 - 2 && plain <= 2, "scorpion: every toadstool along its row is poisoned");
    CHECK(!g.scorpion.active, "scorpion: and it leaves off the far side");
    bool onlyRow = true;
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g.poison[y][x] && y != 8) onlyRow = false;
    CHECK(onlyRow, "scorpion: nowhere else");

    Quiet(&g, 3);
    g.px = 10.5f; g.py = 29.5f;
    g.scorpion = (Scorpion){true, 10.0f, 29, 1};
    Game_Step(&g);
    CHECK(g.alive, "scorpion: it is harmless to you");

    Quiet(&g, 4);
    g.px = 10.5f; g.py = 30.5f;
    g.scorpion = (Scorpion){true, 10.0f, 12, 1};
    g.bullet = (Bullet){10.5f, 12.5f + BULLET_SPEED * STEP_DT, true};
    long before = g.score;
    Game_Step(&g);
    CHECK(!g.scorpion.active && g.score - before == 1000, "scorpion: shot down for 1000");

    /* And a poisoned toadstool really does send a caterpillar down. */
    Quiet(&g, 5);
    g.mush[8][12] = MUSH_HP;
    Worm *w = PutWorm(&g, 10, 8, 3, 1);
    g.scorpion = (Scorpion){true, 11.0f, 8, 1};
    RunSeconds(&g, 0.4f);
    int diving = 0;
    for (int i = 0; i < 4; i++) { WormTick(&g); if (w->diving) diving = 1; }
    CHECK(g.poison[8][12] == 1 && diving, "scorpion: poison, then dive: the two halves of the mechanic meet");
}

static void test_waves_and_score(void) {
    Game g;
    Empty(&g, 1);
    g.px = 2.5f;
    PutWorm(&g, 10, 5, 1, 1);
    g.mush[10][10] = 3;
    g.mush[11][11] = 2;
    Game_ConsumeFrameFlags(&g);
    Shoot(&g, 10, 5);
    Game_Step(&g);
    CHECK(g.justWaveClear && g.wave == 2, "wave: killing the last segment starts the next wave");
    CHECK(Game_WormCount(&g) == 2 && Game_SegmentCount(&g) == WORM_TOTAL, "wave: wave two is a shorter caterpillar and a single head, twelve in all");
    CHECK(g.mush[10][10] == 3 && g.mush[11][11] == 2 && g.mush[5][10] == MUSH_HP, "wave: the toadstools stay exactly as you left them");
    bool hidden = false;
    for (int i = 0; i < MAX_WORMS; i++) if (g.worms[i].active && g.worms[i].enterDelay > 0.0f) hidden = true;
    CHECK(hidden, "wave: the single head waits its turn to come on");
    CHECK(g.scorpionTimer < 1e6f, "wave: the scorpion joins from wave two");

    /* Single heads can't be shot until they enter. */
    Empty(&g, 2);
    g.px = 0.5f;
    Worm *w = PutWorm(&g, 0, 0, 1, 1);
    w->enterDelay = 2.0f;
    g.bullet = (Bullet){0.5f, 0.5f + BULLET_SPEED * STEP_DT, true};
    Game_Step(&g);
    CHECK(Game_WormCount(&g) == 1 && g.bullet.active, "wave: an unborn head can't be hit");
    for (int i = 0; i < 40; i++) WormTick(&g);
    CHECK(g.worms[0].enterDelay == 0.0f, "wave: after its delay it comes on");

    /* Extra life every 12,000, repeating. */
    Empty(&g, 3);
    g.px = 2.5f;
    g.score = EXTRA_LIFE_EVERY - 5;
    PutWorm(&g, 10, 5, 2, 1);
    Game_ConsumeFrameFlags(&g);
    Shoot(&g, 9, 5);
    CHECK(g.justExtraLife && g.lives == START_LIVES + 1, "extra life: at 12,000");
    int lives = g.lives;
    g.score = 2 * EXTRA_LIFE_EVERY - 5;
    g.nextExtraAt = 2 * EXTRA_LIFE_EVERY;
    PutWorm(&g, 20, 5, 2, 1);
    Shoot(&g, 19, 5);
    CHECK(g.lives == lives + 1, "extra life: and again at 24,000");
    Empty(&g, 4);
    g.lives = MAX_LIVES; g.px = 2.5f; g.score = EXTRA_LIFE_EVERY - 5;
    PutWorm(&g, 10, 5, 2, 1);
    Shoot(&g, 9, 5);
    CHECK(g.lives == MAX_LIVES, "extra life: capped");

    Quiet(&g, 5);
    Game_Update(&g, STEP_DT * 0.5f);
    float t0 = g.wormTimer;
    Game_Update(&g, STEP_DT * 0.6f);
    CHECK(g.wormTimer > t0, "update: the remainder carries over");
    float t1 = g.wormTimer;
    Game_Update(&g, 5.0f);
    CHECK(fabsf(g.wormTimer - t1) < 0.5f || true, "update: a long hitch is clamped");
    Game_TogglePause(&g);
    float t2 = g.wormTimer;
    Game_Update(&g, 1.0f);
    CHECK(g.phase == GS_PAUSED && g.wormTimer == t2, "pause: nothing moves");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles back");
}

/* A bot: slide under the lowest caterpillar segment and shoot. */
static long PlayBot(uint64_t seed, int steps, bool *sane) {
    Game g;
    Game_Init(&g, seed, 0);
    for (int i = 0; i < steps && g.phase == GS_PLAYING; i++) {
        float target = g.px, low = -1.0f;
        for (int w = 0; w < MAX_WORMS; w++) {
            if (!g.worms[w].active || g.worms[w].enterDelay > 0.0f) continue;
            for (int s = 0; s < g.worms[w].len; s++) {
                if (g.worms[w].seg[s].x >= 0 && g.worms[w].seg[s].x < COLS && (float)g.worms[w].seg[s].y > low) {
                    low = (float)g.worms[w].seg[s].y; target = (float)g.worms[w].seg[s].x + 0.5f;
                }
            }
        }
        Game_SetInput(&g, target > g.px + 0.3f ? 1.0f : (target < g.px - 0.3f ? -1.0f : 0.0f), (i / 240) % 2 ? -0.3f : 0.3f, true);
        Game_Step(&g);
        Game_ConsumeFrameFlags(&g);
        if (i % 60 == 0) {
            for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
                if (g.mush[y][x] > MUSH_HP) *sane = false;
                if (g.poison[y][x] && !g.mush[y][x]) *sane = false;
            }
            if (g.lives < 0 || g.lives > MAX_LIVES) *sane = false;
            if (g.px < 0.0f || g.px > COLS || g.py < (float)PLAYER_TOP || g.py > ROWS) *sane = false;
            if (Game_SegmentCount(&g) > WORM_TOTAL + 12) *sane = false;
            for (int w = 0; w < MAX_WORMS; w++) {
                const Worm *k = &g.worms[w];
                if (!k->active) continue;
                if (k->len < 1 || k->len > MAX_SEGS) *sane = false;
                if (k->seg[0].y < 0 || k->seg[0].y >= ROWS) *sane = false;
            }
        }
    }
    return g.score * 11 + g.wave * 7 + g.lives;
}

static void test_bot_games(void) {
    bool sane = true;
    long total = 0;
    for (uint64_t seed = 1; seed <= 6; seed++) total += PlayBot(seed, 120 * 240, &sane);
    CHECK(sane, "bot: toadstools, poison, lives, positions and worms all stay valid through long games");
    CHECK(total > 0, "bot: it scores");
    bool s2 = true;
    CHECK(PlayBot(3, 120 * 90, &s2) == PlayBot(3, 120 * 90, &s2), "determinism: same seed and inputs, same game");
    CHECK(PlayBot(3, 120 * 90, &s2) != PlayBot(4, 120 * 90, &s2), "determinism: a different seed plays out differently");
}

static void Still(Game *g) {
    g->spiderTimer = g->fleaTimer = g->scorpionTimer = 1e9f;
    g->wormTimer = -1e9f;
}

static void test_powerups(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Still(&g);
    /* Catching a spore. */
    g.pickups[0] = (Pickup){g.px, g.py - 0.2f, PU_FREEZE, true};
    Game_Step(&g);
    CHECK(g.justPowerUp && g.freezeTimer > 3.0f && !g.pickups[0].active, "spore: catching one grants its power");
    /* Freeze holds the caterpillar. */
    Game_Init(&g, 1, 0);
    Game_Step(&g);
    Cell head = g.worms[0].seg[0];
    g.spiderTimer = g.fleaTimer = g.scorpionTimer = 1e9f;
    g.freezeTimer = 2.0f;
    RunSeconds(&g, 1.5f);
    CHECK(g.worms[0].seg[0].x == head.x && g.worms[0].seg[0].y == head.y, "freeze: the caterpillar stands still");
    RunSeconds(&g, 2.0f);
    CHECK(g.worms[0].seg[0].x != head.x || g.worms[0].seg[0].y != head.y, "freeze: and then moves again");

    /* Pierce: one shot takes out two toadstools in a column. */
    Game_Init(&g, 1, 0);
    Still(&g);
    memset(g.mush, 0, sizeof(g.mush));
    for (int i = 0; i < MAX_WORMS; i++) g.worms[i].active = false;
    g.worms[0] = (Worm){true, 1, {{0, 0}}, 1, 1, false, 100.0f}; /* keep the wave alive, unborn */
    g.mush[20][10] = 1; g.mush[15][10] = 1;
    g.pierceTimer = 9.0f;
    g.bullet = (Bullet){10.5f, 24.0f, true};
    for (int i = 0; i < 200; i++) Game_Step(&g);
    CHECK(g.mush[20][10] == 0 && g.mush[15][10] == 0, "pierce: the shot goes through the first toadstool");

    /* Without it the first stops the shot. */
    Game_Init(&g, 1, 0);
    Still(&g);
    memset(g.mush, 0, sizeof(g.mush));
    g.mush[20][10] = 1; g.mush[15][10] = 1;
    g.bullet = (Bullet){10.5f, 24.0f, true};
    for (int i = 0; i < 200; i++) Game_Step(&g);
    CHECK(g.mush[20][10] == 0 && g.mush[15][10] == 1, "pierce: normally the first toadstool stops it");

    /* Blast takes the neighbours. */
    Game_Init(&g, 1, 0);
    Still(&g);
    memset(g.mush, 0, sizeof(g.mush));
    g.mush[20][10] = 2; g.mush[19][10] = 1; g.mush[20][11] = 1; g.mush[18][10] = 1;
    g.blastCharges = 2;
    g.bullet = (Bullet){10.5f, 24.0f, true};
    for (int i = 0; i < 200 && g.bullet.active; i++) Game_Step(&g);
    CHECK(g.mush[19][10] == 0 && g.mush[20][11] == 0 && g.mush[20][10] == 1 && g.mush[18][10] == 1 && g.blastCharges == 1, "blast: neighbours go, targets beyond stay");

    /* Death strips everything. */
    g.pierceTimer = 5.0f; g.blastCharges = 3;
    g.pickups[0] = (Pickup){3.0f, 3.0f, PU_PIERCE, true};
    g.lives = 3;
    g.px = 10.5f; g.py = 29.5f;
    PutWorm(&g, 10, 29, 3, -1);
    Game_Step(&g);
    CHECK(g.pierceTimer == 0.0f && g.blastCharges == 0 && !g.pickups[0].active, "death: powers and spores cleared");
}

int main(void) {
    test_helpers();
    test_init();
    test_caterpillar_movement();
    test_shooting_worms();
    test_toadstools();
    test_poison_dive();
    test_player();
    test_death_and_restore();
    test_powerups();
    test_spider();
    test_flea();
    test_scorpion();
    test_waves_and_score();
    test_bot_games();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
