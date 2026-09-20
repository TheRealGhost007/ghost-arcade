#include "game.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* ---------------------------------------------------------------- helpers */

float Game_PadX0(const Pad *p) { return (float)(p->start * TERR_STEP); }
float Game_PadX1(const Pad *p) { return (float)((p->start + p->points - 1) * TERR_STEP); }

float Game_TerrainY(const Game *g, float x) {
    x = fmodf(x, (float)FIELD_W);
    if (x < 0.0f) x += (float)FIELD_W;
    int i = (int)(x / (float)TERR_STEP);
    if (i > TERR_N - 2) i = TERR_N - 2;
    float t = (x - (float)(i * TERR_STEP)) / (float)TERR_STEP;
    return g->terrain[i] + (g->terrain[i + 1] - g->terrain[i]) * t;
}

void Game_ShipPoint(const Game *g, float lx, float ly, float *wx, float *wy) {
    float s = sinf(g->angle), c = cosf(g->angle);
    *wx = g->x + lx * c - ly * s;
    *wy = g->y + lx * s + ly * c;
}

float Game_Altitude(const Game *g) {
    float best = 1e9f;
    for (int k = 0; k < 2; k++) {
        float wx, wy;
        Game_ShipPoint(g, k ? FOOT_X : -FOOT_X, FOOT_Y, &wx, &wy);
        float a = Game_TerrainY(g, wx) - wy;
        if (a < best) best = a;
    }
    return best;
}

int Game_PadUnder(const Game *g, float xa, float xb) {
    for (int i = 0; i < g->padCount; i++) {
        float x0 = Game_PadX0(&g->pads[i]), x1 = Game_PadX1(&g->pads[i]);
        if (xa >= x0 && xa <= x1 && xb >= x0 && xb <= x1) return i;
    }
    return -1;
}

LandResult Game_Classify(const Game *g, float vx, float vy, float angle, float footLx, float footRx, int *padOut) {
    int pad = Game_PadUnder(g, footLx, footRx);
    if (padOut) *padOut = pad;
    if (pad < 0) return LAND_OFF_PAD;
    if (fabsf(angle) >= MAX_LAND_TILT) return LAND_TILTED;
    if (vy > MAX_LAND_VY) return LAND_TOO_FAST_DOWN;
    if (fabsf(vx) > MAX_LAND_VX) return LAND_TOO_FAST_SIDEWAYS;
    return LAND_OK;
}

long Game_LandingScore(int mult, float fuel, float vy) {
    long s = (long)mult * 50 + (long)(fuel / 10.0f);
    if (vy < 10.0f) s += 50; /* a feather-light touchdown */
    return s;
}

static const float *sTuneGravity = NULL, *sTuneThrust = NULL, *sTuneBurn = NULL;
void Game_SetTuning(const float *gravity, const float *thrust, const float *fuelBurn) {
    sTuneGravity = gravity; sTuneThrust = thrust; sTuneBurn = fuelBurn;
}
static float Gravity(void) { return sTuneGravity ? *sTuneGravity : GRAVITY; }
static float Thrust(void) { return sTuneThrust ? *sTuneThrust : THRUST; }
static float Burn(void) { return sTuneBurn ? *sTuneBurn : FUEL_BURN; }

static float Rand01(Rng *r) { return (float)Rng_Range(r, 10000) / 10000.0f; }

/* ---------------------------------------------------------------- terrain */

/* Pad widths (in terrain points) per multiplier; they get tighter with level. */
static void PadSpec(int level, int *count, int mults[MAX_PADS], int widths[MAX_PADS]) {
    if (level <= 3) {
        *count = 4;
        int m[4] = {1, 2, 3, 5}, w[4] = {6, 5, 4, 3};
        for (int i = 0; i < 4; i++) { mults[i] = m[i]; widths[i] = w[i]; }
    } else if (level <= 6) {
        *count = 3;
        int m[3] = {2, 3, 5}, w[3] = {5, 4, 3};
        for (int i = 0; i < 3; i++) { mults[i] = m[i]; widths[i] = w[i]; }
    } else {
        *count = 3;
        int m[3] = {2, 3, 5}, w[3] = {4, 3, 3};
        for (int i = 0; i < 3; i++) { mults[i] = m[i]; widths[i] = w[i]; }
    }
}

void Game_BuildLevel(Game *g, int level, uint64_t seed) {
    if (level < 1) level = 1;
    g->level = level;
    Rng r;
    Rng_Seed(&r, seed * 0x9E3779B97F4A7C15ull + (uint64_t)level * 0xD1B54A32D192ED03ull + 12345u);

    float h[TERR_N];
    h[0] = h[TERR_N - 1] = 300.0f + (Rand01(&r) - 0.5f) * 80.0f;
    float amp = 118.0f + 6.0f * (float)(level > 12 ? 12 : level);
    for (int step = TERR_N - 1; step > 1; step /= 2) {
        for (int i = step / 2; i < TERR_N - 1; i += step) {
            h[i] = (h[i - step / 2] + h[i + step / 2]) * 0.5f + (Rand01(&r) - 0.5f) * 2.0f * amp;
        }
        amp *= 0.6f;
    }
    for (int i = 0; i < TERR_N; i++) {
        if (h[i] < 210.0f) h[i] = 210.0f;
        if (h[i] > 446.0f) h[i] = 446.0f;
    }

    /* Pads: one per slot, in a shuffled order so the x5 isn't always in the same place. */
    int count, mults[MAX_PADS], widths[MAX_PADS];
    PadSpec(level, &count, mults, widths);
    for (int i = count - 1; i > 0; i--) {
        int j = (int)Rng_Range(&r, (uint32_t)(i + 1));
        int tm = mults[i], tw = widths[i];
        mults[i] = mults[j]; widths[i] = widths[j];
        mults[j] = tm; widths[j] = tw;
    }
    int slotW = (TERR_N - 5) / count; /* keep clear of both edges */
    g->padCount = count;
    for (int i = 0; i < count; i++) {
        int slack = slotW - widths[i];
        int start = 2 + i * slotW + (slack > 0 ? (int)Rng_Range(&r, (uint32_t)slack) : 0);
        g->pads[i] = (Pad){start, widths[i], mults[i]};
        float y = h[start + widths[i] / 2];
        if (y < 260.0f) y = 260.0f;
        if (y > 430.0f) y = 430.0f;
        for (int k = 0; k < widths[i]; k++) h[start + k] = y;
    }
    memcpy(g->terrain, h, sizeof(h));

    /* Weather and fuel pods come after the terrain draws, so the ground is
     * the same as it ever was for a given seed. */
    float strength = level >= WIND_FIRST_LEVEL ? 2.5f + 1.0f * (float)(level - WIND_FIRST_LEVEL) : 0.0f;
    if (strength > WIND_MAX) strength = WIND_MAX;
    g->windBase = (Rand01(&r) < 0.5f ? -1.0f : 1.0f) * strength;
    g->windPhase = Rand01(&r) * 6.2832f;
    g->podCount = level <= 2 ? 2 : 3;
    for (int i = 0; i < MAX_PODS; i++) g->pods[i] = (Pod){0};
    for (int i = 0; i < g->podCount; i++) {
        float px = 60.0f + ((float)i + 0.15f + 0.7f * Rand01(&r)) * ((float)(FIELD_W - 120) / (float)g->podCount);
        float ground = 0.0f;
        for (float dx = -POD_RADIUS; dx <= POD_RADIUS; dx += 6.0f) {
            float gy = 0.0f;
            /* highest ground near the pod */
            float t = px + dx;
            while (t < 0.0f) t += (float)FIELD_W;
            while (t >= (float)FIELD_W) t -= (float)FIELD_W;
            int k = (int)(t / TERR_STEP);
            gy = g->terrain[k];
            if (ground == 0.0f || gy < ground) ground = gy;
        }
        float lo = 110.0f, hi = ground - 60.0f;
        if (hi < lo) hi = lo;
        g->pods[i] = (Pod){px, lo + (hi - lo) * Rand01(&r), false};
    }

    g->baseSeed = seed;
    g->state = SHIP_FLYING;
    g->stateTimer = 0.0f;
    Game_ResetShip(g);
}

float Game_Wind(const Game *g) {
    if (g->windBase == 0.0f) return 0.0f;
    /* a steady push with slow gusts on top */
    return g->windBase * (0.65f + 0.35f * sinf(g->flightTime * 0.55f + g->windPhase));
}

void Game_ResetShip(Game *g) {
    g->x = 120.0f + Rand01(&g->rng) * 528.0f;
    g->y = 64.0f;
    g->vx = (Rand01(&g->rng) < 0.5f ? -1.0f : 1.0f) * (14.0f + Rand01(&g->rng) * 16.0f);
    g->vy = 0.0f;
    g->angle = 0.0f;
    g->thrusting = false;
    g->flightTime = 0.0f;
    g->state = SHIP_FLYING;
    g->stateTimer = 0.0f;
}

/* ------------------------------------------------------------------ setup */

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->fuel = FUEL_START;
    g->lastPad = -1;
    Game_BuildLevel(g, 1, seed);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, highScore);
}

void Game_SetInput(Game *g, float rot, bool thrust) {
    if (rot < -1.0f) rot = -1.0f;
    if (rot > 1.0f) rot = 1.0f;
    g->rotInput = rot;
    g->thrustHeld = thrust;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

/* ------------------------------------------------------------------- step */

static void Touchdown(Game *g, LandResult res, int pad) {
    g->touchVx = g->vx;
    g->touchVy = g->vy;
    g->touchAngle = g->angle;
    g->lastResult = res;
    g->lastPad = pad;
    g->thrusting = false;
    if (res == LAND_OK) {
        /* Sit the lower foot exactly on the pad and stop. */
        float lx, ly, rx, ry;
        Game_ShipPoint(g, -FOOT_X, FOOT_Y, &lx, &ly);
        Game_ShipPoint(g, FOOT_X, FOOT_Y, &rx, &ry);
        float lowest = ly > ry ? ly : ry;
        g->y += g->terrain[g->pads[pad].start] - lowest;
        g->lastAward = Game_LandingScore(g->pads[pad].mult, g->fuel, g->vy);
        g->score += g->lastAward;
        if (g->score > g->highScore) g->highScore = g->score;
        g->fuel += LAND_REFUEL;
        if (g->fuel > FUEL_MAX) g->fuel = FUEL_MAX;
        g->vx = g->vy = 0.0f;
        g->state = SHIP_LANDED;
        g->stateTimer = LAND_HOLD;
        g->justLand = true;
    } else {
        g->lastAward = 0;
        g->fuel -= CRASH_FUEL_PENALTY;
        if (g->fuel < 0.0f) g->fuel = 0.0f;
        g->vx = g->vy = 0.0f;
        g->state = SHIP_CRASHED;
        g->stateTimer = CRASH_HOLD;
        g->justCrash = true;
    }
}

static void CheckContact(Game *g) {
    /* Anything but a foot meeting the ground is a crash. */
    static const float kBody[3][2] = {{0.0f, -13.0f}, {-9.0f, -3.0f}, {9.0f, -3.0f}};
    for (int i = 0; i < 3; i++) {
        float wx, wy;
        Game_ShipPoint(g, kBody[i][0], kBody[i][1], &wx, &wy);
        if (wy >= Game_TerrainY(g, wx)) { Touchdown(g, LAND_HIT_BODY, -1); return; }
    }
    float lx, ly, rx, ry;
    Game_ShipPoint(g, -FOOT_X, FOOT_Y, &lx, &ly);
    Game_ShipPoint(g, FOOT_X, FOOT_Y, &rx, &ry);
    bool leftDown = ly >= Game_TerrainY(g, lx), rightDown = ry >= Game_TerrainY(g, rx);
    if (!leftDown && !rightDown) return;
    int pad;
    LandResult res = Game_Classify(g, g->vx, g->vy, g->angle, lx, rx, &pad);
    Touchdown(g, res, pad);
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    if (g->state != SHIP_FLYING) {
        g->stateTimer -= STEP_DT;
        if (g->stateTimer > 0.0f) return;
        if (g->state == SHIP_LANDED) {
            Game_BuildLevel(g, g->level + 1, g->baseSeed);
            g->justNewLevel = true;
        } else if (g->fuel <= 0.0f) {
            g->phase = GS_GAMEOVER;
            g->justGameOver = true;
            if (g->score > g->highScore) g->highScore = g->score;
        } else {
            Game_ResetShip(g);
            g->justRespawn = true;
        }
        return;
    }

    g->angle += g->rotInput * ROT_RATE * STEP_DT;
    if (g->angle > PI_F) g->angle -= 2.0f * PI_F;
    if (g->angle < -PI_F) g->angle += 2.0f * PI_F;

    g->flightTime += STEP_DT;
    float ax = Game_Wind(g), ay = Gravity();
    g->thrusting = g->thrustHeld && g->fuel > 0.0f;
    if (g->thrusting) {
        ax += Thrust() * sinf(g->angle);
        ay -= Thrust() * cosf(g->angle);
        float before = g->fuel;
        g->fuel -= Burn() * STEP_DT;
        if (g->fuel < 0.0f) g->fuel = 0.0f;
        if (before > FUEL_LOW && g->fuel <= FUEL_LOW) g->justLowFuel = true;
    }
    g->x += g->vx * STEP_DT + 0.5f * ax * STEP_DT * STEP_DT;
    g->y += g->vy * STEP_DT + 0.5f * ay * STEP_DT * STEP_DT;
    g->vx += ax * STEP_DT;
    g->vy += ay * STEP_DT;

    if (g->x < 0.0f) g->x += (float)FIELD_W;
    if (g->x >= (float)FIELD_W) g->x -= (float)FIELD_W;
    if (g->y < 14.0f) { g->y = 14.0f; if (g->vy < 0.0f) g->vy = 0.0f; } /* the top of the sky */

    for (int i = 0; i < g->podCount; i++) {
        Pod *p = &g->pods[i];
        if (p->taken) continue;
        float dx = fabsf(g->x - p->x);
        if (dx > FIELD_W / 2.0f) dx = FIELD_W - dx;
        float dy = g->y - p->y;
        if (dx * dx + dy * dy < POD_RADIUS * POD_RADIUS) {
            p->taken = true;
            g->fuel += POD_FUEL;
            if (g->fuel > FUEL_MAX) g->fuel = FUEL_MAX;
            g->score += POD_POINTS;
            if (g->score > g->highScore) g->highScore = g->score;
            g->justPod = true;
        }
    }

    CheckContact(g);
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f;
    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justLand = g->justCrash = g->justLowFuel = g->justNewLevel = g->justGameOver = g->justRespawn = g->justPod = false;
}
