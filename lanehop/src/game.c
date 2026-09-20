#include "game.h"
#include <math.h>
#include <string.h>

/* The lanes. Patterns are pictures: one character a tile, '.' empty. The
 * letters only choose how a solid tile is drawn (cars 'a' and 'c', a bike
 * 'b', a truck 't', logs 'l', turtles 'T'); for the rules any non-'.' is
 * solid. Adjacent water lanes run opposite ways, and the tests hold every
 * lane to fairness: gaps between vehicles are at least two tiles (room for
 * a hare), gaps between logs at most four (a hop and a wait). */
static const LaneDef kLanes[LANE_ROWS] = {
    /* 0  */ {LANE_HOME,   0, 0.0f, ".", false},
    /* 1  */ {LANE_LOG,    +1, 2.0f, "lll....lll....lll....", false},
    /* 2  */ {LANE_TURTLE, -1, 1.3f, "TTT..TTT..TTT..", false},
    /* 3  */ {LANE_LOG,    +1, 2.6f, "llllll....llllll....", false},
    /* 4  */ {LANE_LOG,    -1, 1.7f, "lll...lll...lll...", false},
    /* 5  */ {LANE_TURTLE, +1, 1.5f, "TT..TT..TT..TT..", true},
    /* 6  */ {LANE_SAFE,   0, 0.0f, ".", false},
    /* 7  */ {LANE_ROAD,   -1, 2.4f, "a...a.....a....", false},
    /* 8  */ {LANE_ROAD,   +1, 1.5f, "ttt......ttt......", false},
    /* 9  */ {LANE_ROAD,   -1, 3.2f, "b........b........", false},
    /* 10 */ {LANE_ROAD,   +1, 1.8f, "aa....aa.....aa....", false},
    /* 11 */ {LANE_ROAD,   -1, 1.2f, "c...c...c...c...", false},
    /* 12 */ {LANE_SAFE,   0, 0.0f, ".", false},
};

static const int kBayColumns[BAY_COUNT] = {1, 3, 6, 9, 11};

const LaneDef *Game_Lane(int row) {
    if (row < 0 || row >= LANE_ROWS) return &kLanes[MEDIAN_ROW];
    return &kLanes[row];
}

float Game_SpeedMult(int level) {
    float m = 1.0f + 0.10f * (float)(level - 1);
    if (m < 1.0f) m = 1.0f;
    return m > 2.0f ? 2.0f : m;
}

float Game_LaneVelocity(const Game *g, int row) {
    const LaneDef *d = Game_Lane(row);
    return (float)d->dir * d->speed * Game_SpeedMult(g->level);
}

int Game_BayColumn(int i) {
    if (i < 0 || i >= BAY_COUNT) return 0;
    return kBayColumns[i];
}

static int Period(const LaneDef *d) { return (int)strlen(d->pattern); }

static bool CellSolid(const LaneDef *d, int idx) {
    int p = Period(d);
    idx = ((idx % p) + p) % p;
    return d->pattern[idx] != '.';
}

/* Pattern index under field column c (an integer tile), given the lane's
 * scroll: the pattern slides right by `offset` tiles. */
static int IndexAt(const Game *g, int row, float x) {
    return (int)floorf(x - g->laneOffset[row]);
}

bool Game_LaneSolid(const Game *g, int row, float x) {
    const LaneDef *d = Game_Lane(row);
    if (d->kind != LANE_ROAD && d->kind != LANE_LOG && d->kind != LANE_TURTLE) return false;
    return CellSolid(d, IndexAt(g, row, x));
}

bool Game_LaneOverlap(const Game *g, int row, float x0, float x1) {
    const LaneDef *d = Game_Lane(row);
    if (d->kind != LANE_ROAD && d->kind != LANE_LOG && d->kind != LANE_TURTLE) return false;
    int a = IndexAt(g, row, x0), b = IndexAt(g, row, x1);
    for (int i = a; i <= b; i++) if (CellSolid(d, i)) return true;
    return false;
}

bool Game_LaneRun(const Game *g, int row, float x, int *startIdx, int *len) {
    const LaneDef *d = Game_Lane(row);
    if (!Game_LaneSolid(g, row, x)) return false;
    int p = Period(d);
    int j = ((IndexAt(g, row, x) % p) + p) % p;
    int start = j, guard = 0;
    while (CellSolid(d, start - 1) && guard++ < p) start--;
    if (guard >= p) return false; /* a lane with no gaps has no runs; the tests forbid it */
    start = ((start % p) + p) % p;
    int n = 0;
    while (CellSolid(d, start + n) && n < p) n++;
    if (startIdx) *startIdx = start;
    if (len) *len = n;
    return true;
}

/* One dive cycle: up for 3.6 s, sinking for 0.7, under for 1.3, rising for
 * 0.4. Every group has its own phase so the lane never goes all at once. */
#define DIVE_CYCLE 6.0f
static float DivePhase(const Game *g, int runStartIdx) {
    return fmodf(g->time + (float)runStartIdx * 0.9f, DIVE_CYCLE);
}

bool Game_TurtleSinking(const Game *g, int row, int runStartIdx) {
    if (!Game_Lane(row)->dives) return false;
    float p = DivePhase(g, runStartIdx);
    return (p >= 3.6f && p < 4.3f) || p >= 5.6f;
}

bool Game_TurtleSubmerged(const Game *g, int row, int runStartIdx) {
    if (!Game_Lane(row)->dives) return false;
    float p = DivePhase(g, runStartIdx);
    return p >= 4.3f && p < 5.6f;
}

void Game_HarePos(const Game *g, float *x, float *row) {
    const Hare *h = &g->hare;
    if (h->hopping) {
        float t = h->hopT / HOP_TIME;
        if (t > 1.0f) t = 1.0f;
        *x = h->fromX + (h->toX - h->fromX) * t;
        *row = (float)h->fromRow + (float)(h->toRow - h->fromRow) * t;
    } else {
        *x = h->x;
        *row = (float)h->row;
    }
}

/* ------------------------------------------------------------------ setup */

static float RandRange(Game *g, float lo, float hi) {
    return lo + (hi - lo) * ((float)Rng_Range(&g->rng, 10000) / 10000.0f);
}

static void PlaceHare(Game *g) {
    g->hare = (Hare){START_X, START_ROW, DIR_UP, true, false, 0.0f, START_X, START_X, START_ROW, START_ROW};
    g->queued = DIR_NONE;
    g->lifeTimer = LIFE_SECONDS;
    g->bestRow = START_ROW;
    g->lastSecond = (int)LIFE_SECONDS;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->level = 1;
    g->nextExtraAt = EXTRA_LIFE_EVERY;
    g->flyBay = g->crocBay = -1;
    g->flyDelay = RandRange(g, 6.0f, 10.0f);
    g->crocDelay = RandRange(g, 9.0f, 15.0f);
    g->goodieDelay = RandRange(g, 5.0f, 9.0f);
    PlaceHare(g);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, highScore);
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

void Game_Hop(Game *g, Dir d) {
    if (g->phase != GS_PLAYING || d == DIR_NONE) return;
    if (!g->hare.alive || g->deathTimer > 0.0f) return;
    g->queued = d;
}

/* ---------------------------------------------------------------- scoring */

static void AddScore(Game *g, long points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    while (g->score >= g->nextExtraAt) {
        g->nextExtraAt += EXTRA_LIFE_EVERY;
        if (g->lives < MAX_LIVES) g->lives++;
        g->justExtraLife = true;
    }
}

/* ------------------------------------------------------------------ death */

const char *Game_GoodieName(GoodieType t) {
    static const char *const kNames[GOODIE_COUNT] = {"CARROT", "CLOCK", "SHIELD"};
    return (t >= 0 && t < GOODIE_COUNT) ? kNames[t] : "";
}

static void Die(Game *g, DeathCause cause) {
    if (!g->hare.alive) return;
    if (g->shield && cause != DEATH_TIME) {
        /* The charm takes the hit: back to the start line, clock still running. */
        g->shield = false;
        g->justShieldSave = true;
        g->hare = (Hare){START_X, START_ROW, DIR_UP, true, false, 0.0f, START_X, START_X, START_ROW, START_ROW};
        g->queued = DIR_NONE;
        return;
    }
    g->hare.alive = false;
    g->hare.hopping = false;
    g->queued = DIR_NONE;
    g->deathCause = cause;
    g->deathTimer = DEATH_SECONDS;
    g->justDeath = true;
}

static void FinishDeath(Game *g) {
    g->lives--;
    if (g->lives <= 0) {
        g->lives = 0;
        g->phase = GS_GAMEOVER;
        g->justGameOver = true;
        if (g->score > g->highScore) g->highScore = g->score;
        return;
    }
    PlaceHare(g);
}

/* --------------------------------------------------------------- landing */

static int NearestBay(float x) {
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < BAY_COUNT; i++) {
        float d = fabsf((x + 0.5f) - ((float)kBayColumns[i] + 0.5f));
        if (d < bestD) { bestD = d; best = i; }
    }
    return bestD <= BAY_TOLERANCE ? best : -1;
}

static void ClearBays(Game *g) {
    memset(g->bays, 0, sizeof(g->bays));
    g->flyBay = g->crocBay = -1;
}

static void ReachHome(Game *g, float x) {
    int b = NearestBay(x);
    if (b < 0) { Die(g, DEATH_WALL); return; }
    if (g->bays[b]) { Die(g, DEATH_BAY_FULL); return; }
    if (g->crocBay == b && g->crocTimer > 0.0f) { Die(g, DEATH_CROC); return; }

    g->bays[b] = true;
    AddScore(g, 50 + 10L * (long)g->lifeTimer);
    if (g->flyBay == b && g->flyTimer > 0.0f) {
        AddScore(g, 200);
        g->justFly = true;
        g->flyBay = -1;
    }
    g->justHome = true;
    g->homeBay = b;

    bool all = true;
    for (int i = 0; i < BAY_COUNT; i++) if (!g->bays[i]) all = false;
    if (all) {
        AddScore(g, 1000);
        g->justLevelClear = true;
        g->level++;
        ClearBays(g);
    }
    PlaceHare(g);
    g->homePause = HOME_PAUSE_SECONDS;
}

/* What is fatal where the hare stands NOW (not mid-hop). */
static void CheckGrounded(Game *g) {
    Hare *h = &g->hare;
    if (!h->alive || h->hopping) return;
    float cx = h->x + 0.5f;
    const LaneDef *d = Game_Lane(h->row);

    if (d->kind == LANE_ROAD) {
        if (Game_LaneOverlap(g, h->row, cx - HARE_HALF_WIDTH, cx + HARE_HALF_WIDTH)) Die(g, DEATH_CAR);
    } else if (d->kind == LANE_LOG || d->kind == LANE_TURTLE) {
        int start = 0, len = 0;
        bool onSolid = Game_LaneRun(g, h->row, cx, &start, &len);
        if (onSolid && d->kind == LANE_TURTLE && Game_TurtleSubmerged(g, h->row, start)) onSolid = false;
        if (!onSolid) Die(g, DEATH_WATER);
        else if (cx < 0.0f || cx > (float)LANE_COLS) Die(g, DEATH_OFFSCREEN);
    }
}

static void Land(Game *g) {
    Hare *h = &g->hare;
    h->hopping = false;
    h->x = h->toX;
    h->row = h->toRow;

    if (h->row < g->bestRow) {
        AddScore(g, 10L * (g->bestRow - h->row));
        g->bestRow = h->row;
    }
    if (h->row == HOME_ROW) { ReachHome(g, h->x); return; }
    if (h->row == MEDIAN_ROW && g->goodieOn && fabsf(h->x - (float)g->goodieCol) < 0.6f) {
        g->goodieOn = false;
        g->goodieDelay = RandRange(g, 9.0f, 15.0f);
        g->lastGoodie = g->goodieType;
        g->justGoodie = true;
        if (g->goodieType == GOODIE_CARROT) AddScore(g, GOODIE_CARROT_POINTS);
        else if (g->goodieType == GOODIE_CLOCK) {
            g->lifeTimer += GOODIE_CLOCK_SECONDS;
            if (g->lifeTimer > LIFE_SECONDS) g->lifeTimer = LIFE_SECONDS;
            g->lastSecond = (int)ceilf(g->lifeTimer);
        } else g->shield = true;
    }
    CheckGrounded(g);
}

static void StartHop(Game *g, Dir d) {
    Hare *h = &g->hare;
    float nx = h->x;
    int nr = h->row;
    switch (d) {
        case DIR_UP: nr--; break;
        case DIR_DOWN: nr++; break;
        case DIR_LEFT: nx -= 1.0f; break;
        case DIR_RIGHT: nx += 1.0f; break;
        default: return;
    }
    if (nr < HOME_ROW || nr > START_ROW) return;
    if (nx < 0.0f) nx = 0.0f;
    if (nx > (float)(LANE_COLS - 1)) nx = (float)(LANE_COLS - 1);
    if (nx == h->x && nr == h->row) return; /* already against the wall */

    h->facing = d;
    h->hopping = true;
    h->hopT = 0.0f;
    h->fromX = h->x; h->toX = nx;
    h->fromRow = h->row; h->toRow = nr;
    g->justHop = true;
}

/* ----------------------------------------------------------------- events */

static void StepBayHazards(Game *g) {
    /* A fly turns up in an empty burrow for a few seconds... */
    if (g->flyTimer > 0.0f) {
        g->flyTimer -= STEP_DT;
        if (g->flyTimer <= 0.0f) { g->flyTimer = 0.0f; g->flyBay = -1; }
    } else if ((g->flyDelay -= STEP_DT) <= 0.0f) {
        int open[BAY_COUNT], n = 0;
        for (int i = 0; i < BAY_COUNT; i++) if (!g->bays[i] && i != g->crocBay) open[n++] = i;
        if (n > 0) { g->flyBay = open[Rng_Range(&g->rng, (uint32_t)n)]; g->flyTimer = 4.0f; }
        g->flyDelay = RandRange(g, 8.0f, 13.0f);
    }
    /* ...and a crocodile in another, which makes it a trap. */
    if (g->crocTimer > 0.0f) {
        g->crocTimer -= STEP_DT;
        if (g->crocTimer <= 0.0f) { g->crocTimer = 0.0f; g->crocBay = -1; }
    } else if ((g->crocDelay -= STEP_DT) <= 0.0f) {
        int open[BAY_COUNT], n = 0;
        for (int i = 0; i < BAY_COUNT; i++) if (!g->bays[i] && i != g->flyBay) open[n++] = i;
        if (n > 0) { g->crocBay = open[Rng_Range(&g->rng, (uint32_t)n)]; g->crocTimer = 5.0f; }
        g->crocDelay = RandRange(g, 11.0f, 18.0f);
    }
}

static void StepGoodie(Game *g) {
    if (g->goodieOn) {
        if ((g->goodieTimer -= STEP_DT) <= 0.0f) {
            g->goodieOn = false;
            g->goodieDelay = RandRange(g, 9.0f, 15.0f);
        }
    } else if ((g->goodieDelay -= STEP_DT) <= 0.0f) {
        g->goodieOn = true;
        g->goodieCol = 1 + (int)Rng_Range(&g->rng, LANE_COLS - 2);
        g->goodieType = (GoodieType)Rng_Range(&g->rng, GOODIE_COUNT);
        g->goodieTimer = GOODIE_SECONDS;
    }
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    g->time += STEP_DT;
    for (int r = 0; r < LANE_ROWS; r++) {
        const LaneDef *d = Game_Lane(r);
        if (d->speed == 0.0f) continue;
        int p = Period(d);
        float o = g->laneOffset[r] + Game_LaneVelocity(g, r) * STEP_DT;
        o = fmodf(o, (float)p);
        if (o < 0.0f) o += (float)p;
        g->laneOffset[r] = o;
    }
    StepBayHazards(g);
    StepGoodie(g);

    if (g->deathTimer > 0.0f) {
        g->deathTimer -= STEP_DT;
        if (g->deathTimer <= 0.0f) { g->deathTimer = 0.0f; FinishDeath(g); }
        return;
    }
    if (g->homePause > 0.0f) {
        g->homePause -= STEP_DT;
        if (g->homePause < 0.0f) g->homePause = 0.0f;
        return;
    }

    g->lifeTimer -= STEP_DT;
    int secs = (int)ceilf(g->lifeTimer);
    if (secs != g->lastSecond) {
        g->lastSecond = secs;
        if (secs > 0 && secs <= 6) g->justTimeWarning = secs;
    }
    if (g->lifeTimer <= 0.0f) { g->lifeTimer = 0.0f; Die(g, DEATH_TIME); return; }

    Hare *h = &g->hare;
    if (h->hopping) {
        h->hopT += STEP_DT;
        if (h->hopT >= HOP_TIME) Land(g);
    } else {
        /* Riding: on a log or turtle you move with it. */
        const LaneDef *d = Game_Lane(h->row);
        if (d->kind == LANE_LOG || d->kind == LANE_TURTLE) h->x += Game_LaneVelocity(g, h->row) * STEP_DT;
        CheckGrounded(g);
    }

    if (g->hare.alive && !g->hare.hopping && g->queued != DIR_NONE) {
        Dir d = g->queued;
        g->queued = DIR_NONE;
        StartHop(g, d);
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f; /* a long hitch shouldn't run the hare into traffic */

    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justHop = g->justDeath = g->justHome = g->justFly = false;
    g->justLevelClear = g->justGameOver = g->justExtraLife = false;
    g->justGoodie = g->justShieldSave = false;
    g->justTimeWarning = 0;
}
