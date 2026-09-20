#include "game.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */

float Game_TowerHeight(int level) { return 1700.0f + 260.0f * (float)(level < 1 ? 1 : (level > 12 ? 12 : level)); }
float Game_FireSpeed(int level) { return 18.0f + 5.0f * (float)(level < 1 ? 1 : (level > 12 ? 12 : level)); }
float Game_OrbX(const Orb *o, float time) { return o->cx + o->amp * sinf(o->freq * time + o->phase); }

static float Rand01(Rng *r) { return (float)Rng_Range(r, 10000) / 10000.0f; }
static float RandRange(Rng *r, float lo, float hi) { return lo + (hi - lo) * Rand01(r); }
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void AddScore(Game *g, long points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    while (g->score >= g->nextExtra) {
        g->nextExtra += EXTRA_LIFE_EVERY;
        if (g->lives < MAX_LIVES) g->lives++;
        g->justExtraLife = true;
    }
}

int Game_PickAnchor(const Game *g, float x, float y, int moveX) {
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < g->anchorCount; i++) {
        if (i == g->p.lastAnchor) continue;
        float dx = g->anchors[i].x - x, dy = g->anchors[i].y - y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > GRAPPLE_RANGE || dy > -28.0f || d < 14.0f) continue; /* only rings clearly above you */
        float score = d - ((moveX != 0 && (float)moveX * dx > 0.0f) ? 35.0f : 0.0f);
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

/* ------------------------------------------------------------------ tower */

void Game_BuildTower(Game *g, int level) {
    if (level < 1) level = 1;
    g->level = level;
    Rng r;
    Rng_Seed(&r, g->seed * 0x9E3779B97F4A7C15ull + (uint64_t)level * 0xD1B54A32D192ED03ull + 7u);

    float ht = Game_TowerHeight(level);
    g->groundY = ht - 40.0f;
    g->towerTop = 90.0f;
    g->ledgeCount = 0;
    g->anchorCount = 0;
    g->orbCount = 0;

    /* Rings, built from the goal down: one directly above the goal ledge, then
     * evenly spaced (never more than ~125 apart) with a sideways wander, down to
     * one a little above the floor. So there is always a ring within reach of
     * wherever you are on the way up. */
    float topY = g->towerTop - 55.0f, bottomY = g->groundY - 150.0f;
    int steps = (int)ceilf((bottomY - topY) / 115.0f);
    if (steps < 8) steps = 8;
    if (steps + 1 > MAX_ANCHORS) steps = MAX_ANCHORS - 1;
    float x = 280.0f;
    float rings[MAX_ANCHORS][2];
    for (int k = 0; k <= steps; k++) {
        float y = topY + (bottomY - topY) * (float)k / (float)steps + (k > 0 && k < steps ? RandRange(&r, -8.0f, 8.0f) : 0.0f);
        rings[k][0] = x;
        rings[k][1] = y;
        x = Clampf(x + RandRange(&r, -140.0f, 140.0f), 50.0f, (float)WORLD_W - 50.0f);
    }
    for (int k = steps; k >= 0; k--) g->anchors[g->anchorCount++] = (Anchor){rings[k][0], rings[k][1], false};

    /* Ledges: the floor, a rest stop every ~700 up (each under a ring so you can
     * step off it and fire at once), and the goal. */
    g->ledges[g->ledgeCount++] = (Ledge){0.0f, g->groundY, (float)WORLD_W, true, false, true};
    int checkpoints = (int)((g->groundY - g->towerTop) / 700.0f);
    for (int k = 1; k <= checkpoints && g->ledgeCount < MAX_LEDGES - 1; k++) {
        float y = g->groundY - 700.0f * (float)k;
        if (y < g->towerTop + 200.0f) break;
        int near = 0;
        for (int i = 0; i < g->anchorCount; i++) if (g->anchors[i].y < y - 40.0f && g->anchors[i].y > g->anchors[near].y - 1e9f) near = i;
        float best = 1e9f;
        for (int i = 0; i < g->anchorCount; i++) { float d = y - g->anchors[i].y; if (d > 40.0f && d < best) { best = d; near = i; } }
        g->ledges[g->ledgeCount++] = (Ledge){Clampf(g->anchors[near].x - 80.0f, 0.0f, (float)WORLD_W - 160.0f), y, 160.0f, true, false, false};
    }
    g->ledges[g->ledgeCount++] = (Ledge){180.0f, g->towerTop, 200.0f, false, true, false};

    /* Ghost orbs drift across the shaft from tower 2 on. */
    int orbs = level >= 2 ? (level * 2 > MAX_ORBS ? MAX_ORBS : level * 2) : 0;
    for (int i = 0; i < orbs; i++) {
        Orb o;
        o.cy = RandRange(&r, g->towerTop + 260.0f, g->groundY - 320.0f);
        o.cx = RandRange(&r, 120.0f, (float)WORLD_W - 120.0f);
        o.amp = RandRange(&r, 60.0f, 110.0f);
        o.freq = RandRange(&r, 0.5f, 1.0f);
        o.phase = RandRange(&r, 0.0f, 6.28f);
        o.x = o.cx;
        g->orbs[g->orbCount++] = o;
    }
}

static void PlaceOnLedge(Game *g, int li) {
    const Ledge *l = &g->ledges[li];
    float px = l->x + l->w * 0.5f;
    if (li == 0 && g->anchorCount > 0) px = g->anchors[0].x; /* start right under the first ring */
    g->p = (Player){px, l->y - PLAYER_R, 0.0f, 0.0f, P_STAND, -1, -1, ROPE_MIN, 1};
    g->fireY = l->y + 330.0f;
    g->bestY = g->p.y;
}

void Game_StartLevel(Game *g, int level) {
    Game_BuildTower(g, level);
    g->fireSpeed = Game_FireSpeed(level);
    g->checkpoint = 0;
    g->time = 0.0f;
    g->climbAcc = 0.0f;
    g->death = DEATH_NONE;
    PlaceOnLedge(g, 0);
    g->moveX = g->moveY = 0;
    g->grappleQueued = g->jumpQueued = g->grappleHeld = g->jumpHeld = false;
    g->state = LS_INTRO;
    g->stateTimer = INTRO_SECONDS;
    g->justNewLevel = true;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->seed = seed;
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->nextExtra = EXTRA_LIFE_EVERY;
    g->orbsEnabled = true;
    Game_StartLevel(g, 1);
}

void Game_Restart(Game *g, uint64_t seed, long highScore) {
    long best = g->highScore > g->score ? g->highScore : g->score;
    if (highScore > best) best = highScore;
    bool orbs = g->orbsEnabled;
    Game_Init(g, seed, best);
    g->orbsEnabled = orbs;
}

void Game_SetInput(Game *g, int moveX, int moveY, bool grapple, bool jump) {
    g->moveX = moveX < 0 ? -1 : (moveX > 0 ? 1 : 0);
    g->moveY = moveY < 0 ? -1 : (moveY > 0 ? 1 : 0);
    if (grapple && !g->grappleHeld) g->grappleQueued = true;
    if (jump && !g->jumpHeld) g->jumpQueued = true;
    g->grappleHeld = grapple;
    g->jumpHeld = jump;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

/* ------------------------------------------------------------------- step */

static void Kill(Game *g, DeathCause c) {
    if (g->state != LS_PLAY) return;
    g->death = c;
    g->state = LS_DYING;
    g->stateTimer = DYING_SECONDS;
    g->justDeath = true;
}

static void Land(Game *g, int li) {
    Ledge *l = &g->ledges[li];
    g->p.mode = P_STAND;
    g->p.vx = g->p.vy = 0.0f;
    g->p.y = l->y - PLAYER_R;
    g->p.anchor = -1;
    g->p.lastAnchor = -1;
    g->justLand = true;
    if (l->checkpoint && li > g->checkpoint) {
        g->checkpoint = li;
        if (!l->reached) { l->reached = true; AddScore(g, CHECKPOINT_POINTS); g->justCheckpoint = true; }
    }
    if (l->goal) {
        g->state = LS_CLEAR;
        g->stateTimer = CLEAR_SECONDS;
        long bonus = 1000;
        if (g->time < 120.0f) bonus += (long)((120.0f - g->time) * 10.0f);
        g->lastBonus = bonus;
        AddScore(g, bonus);
        g->justClear = true;
    }
}

/* Lands on a ledge whose top we crossed this step while falling. */
static bool TryLand(Game *g, float prevY) {
    /* On the rope you have to be really coming down: the step you attach from a ledge does not count. */
    if (g->p.vy <= (g->p.mode == P_ROPE ? 40.0f : 0.0f)) return false;
    for (int i = 0; i < g->ledgeCount; i++) {
        const Ledge *l = &g->ledges[i];
        if (g->p.x < l->x - PLAYER_R * 0.5f || g->p.x > l->x + l->w + PLAYER_R * 0.5f) continue;
        if (prevY + PLAYER_R <= l->y + 1.0f && g->p.y + PLAYER_R >= l->y) { Land(g, i); return true; }
    }
    return false;
}

static void Walls(Game *g) {
    if (g->p.x < PLAYER_R) { g->p.x = PLAYER_R; if (g->p.vx < 0.0f) { g->p.vx = -g->p.vx * WALL_BOUNCE; g->justWall = true; } }
    if (g->p.x > (float)WORLD_W - PLAYER_R) { g->p.x = (float)WORLD_W - PLAYER_R; if (g->p.vx > 0.0f) { g->p.vx = -g->p.vx * WALL_BOUNCE; g->justWall = true; } }
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING || g->state != LS_PLAY) return;
    g->stepCount++;
    g->time += STEP_DT;
    Player *p = &g->p;

    /* The fire climbs, and climbs faster the longer you take. */
    g->fireY -= (g->fireSpeed + 0.3f * g->time) * STEP_DT;

    /* Buttons */
    if (g->grappleQueued) {
        if (p->mode == P_ROPE) {
            p->mode = P_AIR;
            p->lastAnchor = p->anchor;
            p->anchor = -1;
            g->justRelease = true;
        } else {
            int i = Game_PickAnchor(g, p->x, p->y, g->moveX);
            g->justFire = true;
            if (i >= 0) {
                float dx = g->anchors[i].x - p->x, dy = g->anchors[i].y - p->y;
                p->mode = P_ROPE;
                p->anchor = i;
                p->ropeLen = Clampf(sqrtf(dx * dx + dy * dy), ROPE_MIN, ROPE_MAX);
                g->justAttach = true;
            }
        }
    }
    if (g->jumpQueued && p->mode == P_STAND) {
        p->mode = P_AIR;
        p->vy = -JUMP_SPEED;
        p->vx = (float)g->moveX * WALK_SPEED;
        g->justJump = true;
    }
    g->grappleQueued = g->jumpQueued = false;

    float prevY = p->y;
    if (p->mode == P_STAND) {
        p->x += (float)g->moveX * WALK_SPEED * STEP_DT;
        if (g->moveX) p->facing = g->moveX;
        p->x = Clampf(p->x, PLAYER_R, (float)WORLD_W - PLAYER_R);
        /* Walk off the end of a ledge and you fall. */
        bool supported = false;
        for (int i = 0; i < g->ledgeCount; i++) {
            const Ledge *l = &g->ledges[i];
            if (fabsf(p->y + PLAYER_R - l->y) < 2.0f && p->x >= l->x - PLAYER_R * 0.5f && p->x <= l->x + l->w + PLAYER_R * 0.5f) supported = true;
        }
        if (!supported) p->mode = P_AIR;
    } else {
        float ax = 0.0f;
        if (p->mode == P_ROPE) {
            ax = (float)g->moveX * PUMP_ACCEL;
            if (g->moveY < 0) p->ropeLen -= REEL_SPEED * STEP_DT;
            else if (g->moveY > 0) p->ropeLen += REEL_SPEED * STEP_DT;
            p->ropeLen = Clampf(p->ropeLen, ROPE_MIN, ROPE_MAX);
            if (g->moveX) p->facing = g->moveX;
        }
        p->vx += ax * STEP_DT;
        p->vy += GRAVITY * STEP_DT;
        p->vx *= 1.0f - (p->mode == P_ROPE ? 0.05f : AIR_DRAG) * STEP_DT;
        p->x += p->vx * STEP_DT;
        p->y += p->vy * STEP_DT;

        if (p->mode == P_ROPE) {
            const Anchor *a = &g->anchors[p->anchor];
            float dx = p->x - a->x, dy = p->y - a->y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > p->ropeLen && d > 0.0001f) {
                float nx = dx / d, ny = dy / d;
                p->x = a->x + nx * p->ropeLen;
                p->y = a->y + ny * p->ropeLen;
                float radial = p->vx * nx + p->vy * ny;
                if (radial > 0.0f) { p->vx -= radial * nx; p->vy -= radial * ny; }
            }
            Anchor *am = &g->anchors[p->anchor];
            float rx = p->x - am->x, ry = p->y - am->y;
            if (!am->touched && rx * rx + ry * ry < ANCHOR_REACH * ANCHOR_REACH) { am->touched = true; AddScore(g, RING_POINTS); g->justRing = true; }
        }
        Walls(g);
        if (TryLand(g, prevY)) { /* landed: Land() set everything */ }
    }

    /* Points for height. */
    if (p->y < g->bestY) {
        g->climbAcc += g->bestY - p->y;
        g->bestY = p->y;
        long whole = (long)(g->climbAcc / 10.0f);
        if (whole > 0) { AddScore(g, whole); g->climbAcc -= (float)whole * 10.0f; }
    }
    g->heightMeters = (int)((g->groundY - g->p.y) / 10.0f);
    if (g->heightMeters < 0) g->heightMeters = 0;

    if (g->state != LS_PLAY) return;
    if (p->y + PLAYER_R > g->fireY) { Kill(g, DEATH_FIRE); return; }
    if (g->orbsEnabled) {
        for (int i = 0; i < g->orbCount; i++) {
            g->orbs[i].x = Game_OrbX(&g->orbs[i], g->time);
            float dx = p->x - g->orbs[i].x, dy = p->y - g->orbs[i].cy;
            if (dx * dx + dy * dy < (PLAYER_R + ORB_RADIUS) * (PLAYER_R + ORB_RADIUS)) { Kill(g, DEATH_ORB); return; }
        }
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f;
    switch (g->state) {
        case LS_INTRO:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) g->state = LS_PLAY;
            return;
        case LS_DYING:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) {
                g->lives--;
                if (g->lives <= 0) { g->lives = 0; g->phase = GS_GAMEOVER; g->justGameOver = true; }
                else {
                    /* Back to the last ledge you reached, with the fire pushed down a little. */
                    for (int i = 0; i < g->anchorCount; i++) g->anchors[i].touched = g->anchors[i].touched;
                    PlaceOnLedge(g, g->checkpoint);
                    g->time = 0.0f;
                    g->death = DEATH_NONE;
                    g->state = LS_PLAY;
                }
            }
            return;
        case LS_CLEAR:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) Game_StartLevel(g, g->level + 1);
            return;
        case LS_PLAY: break;
    }
    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->state == LS_PLAY && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justFire = g->justAttach = g->justRelease = g->justJump = g->justLand = g->justRing = g->justCheckpoint = false;
    g->justDeath = g->justClear = g->justGameOver = g->justNewLevel = g->justExtraLife = g->justWall = false;
}
