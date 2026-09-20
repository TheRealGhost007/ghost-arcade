#include "game.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define TAU_F (2.0f * PI_F)

/* ---------------------------------------------------------------- helpers */

float Game_WrapDelta(float a, float b, float size) {
    float d = fmodf(b - a, size);
    if (d > size / 2.0f) d -= size;
    if (d < -size / 2.0f) d += size;
    return d;
}

float Game_WrapDist(float x1, float y1, float x2, float y2) {
    float dx = Game_WrapDelta(x1, x2, FIELD_W), dy = Game_WrapDelta(y1, y2, FIELD_H);
    return sqrtf(dx * dx + dy * dy);
}

static float WrapPos(float v, float size) {
    v = fmodf(v, size);
    return v < 0.0f ? v + size : v;
}

void Game_RockShape(uint32_t shape, float out[ROCK_VERTS]) {
    Rng r;
    Rng_Seed(&r, (uint64_t)shape * 2654435761u + 12345u);
    for (int i = 0; i < ROCK_VERTS; i++) out[i] = 0.72f + (float)Rng_Range(&r, 45) / 100.0f; /* 0.72 .. 1.16 */
}

float Game_RockRadius(int size) {
    switch (size) {
        case ROCK_LARGE: return 40.0f;
        case ROCK_MEDIUM: return 22.0f;
        default: return 11.0f;
    }
}

int Game_RockPoints(int size) {
    switch (size) {
        case ROCK_LARGE: return 20;
        case ROCK_MEDIUM: return 50;
        default: return 100;
    }
}

float Game_BeatInterval(int rocksLeft) {
    if (rocksLeft < 1) rocksLeft = 1;
    if (rocksLeft > 20) rocksLeft = 20;
    return 0.30f + 0.70f * (float)rocksLeft / 20.0f;
}

float Game_SaucerAimError(long score) {
    if (score < 0) score = 0;
    float k = (float)score / 30000.0f;
    if (k > 1.0f) k = 1.0f;
    return 0.55f - 0.47f * k;
}

static float Rand01(Game *g) { return (float)Rng_Range(&g->rng, 10000) / 10000.0f; }
static float RandRange(Game *g, float lo, float hi) { return lo + (hi - lo) * Rand01(g); }

/* ------------------------------------------------------------------ setup */

static void CountRocks(Game *g) {
    g->rockCount = 0;
    for (int i = 0; i < MAX_ROCKS; i++) if (g->rocks[i].active) g->rockCount++;
}

static Rock *FreeRockSlot(Game *g) {
    for (int i = 0; i < MAX_ROCKS; i++) if (!g->rocks[i].active) return &g->rocks[i];
    return NULL;
}

static float RockSpeedScale(int wave) {
    float s = 1.0f + 0.05f * (float)(wave - 1);
    return s > 1.9f ? 1.9f : s;
}

void Game_StartWave(Game *g, int wave) {
    if (wave < 1) wave = 1;
    g->wave = wave;
    memset(g->rocks, 0, sizeof(g->rocks));

    int count = 3 + wave;
    if (count > ROCK_START_MAX) count = ROCK_START_MAX;

    float cx = g->ship.alive ? g->ship.x : FIELD_W / 2.0f;
    float cy = g->ship.alive ? g->ship.y : FIELD_H / 2.0f;
    for (int i = 0; i < count; i++) {
        Rock *r = &g->rocks[i];
        float x = 0.0f, y = 0.0f;
        for (int attempt = 0; attempt < 60; attempt++) {
            x = RandRange(g, 0.0f, FIELD_W);
            y = RandRange(g, 0.0f, FIELD_H);
            if (Game_WrapDist(x, y, cx, cy) >= SPAWN_CLEAR_RADIUS) break;
        }
        float speed = RandRange(g, 28.0f, 66.0f) * RockSpeedScale(wave);
        float dir = RandRange(g, 0.0f, TAU_F);
        *r = (Rock){x, y, sinf(dir) * speed, -cosf(dir) * speed, RandRange(g, 0.0f, TAU_F), RandRange(g, -1.2f, 1.2f),
                    ROCK_LARGE, (uint32_t)Rng_Next(&g->rng), true};
    }
    CountRocks(g);

    memset(g->bullets, 0, sizeof(g->bullets));
    memset(g->enemyBullets, 0, sizeof(g->enemyBullets));
    g->saucer.active = false;
    g->saucerTimer = RandRange(g, 9.0f, 14.0f);
    g->waveTimer = 0.0f;
    g->beatTimer = 0.6f;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->nextExtraAt = EXTRA_LIFE_EVERY;
    g->ship = (Ship){FIELD_W / 2.0f, FIELD_H / 2.0f, 0.0f, 0.0f, 0.0f, true, false, 0.0f, INVULN_SECONDS * 0.5f, 0.0f};
    Game_StartWave(g, 1);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, highScore);
}

void Game_SetInput(Game *g, float turn, bool thrust, bool fire, bool hyperspace) {
    if (turn < -1.0f) turn = -1.0f;
    if (turn > 1.0f) turn = 1.0f;
    g->turn = turn;
    g->thrustHeld = thrust;
    g->fireHeld = fire;
    g->hyperHeld = hyperspace;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

/* ---------------------------------------------------------------- scoring */

static void NoteKill(Game *g, float x, float y, int kind, int points) {
    if (g->killCount < MAX_KILLS_PER_FRAME) g->kills[g->killCount++] = (Kill){x, y, kind, points};
}

static void AddScore(Game *g, int points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    while (g->score >= g->nextExtraAt) {
        g->nextExtraAt += EXTRA_LIFE_EVERY;
        if (g->lives < MAX_LIVES) g->lives++;
        g->justExtraLife = true;
    }
}

/* ------------------------------------------------------------------ rocks */

/* Removes rock i and, unless it was the smallest, leaves two smaller ones
 * flying off at angles either side of where it was heading. */
static void BreakRock(Game *g, int i) {
    Rock parent = g->rocks[i];
    g->rocks[i].active = false;

    int points = Game_RockPoints(parent.size);
    NoteKill(g, parent.x, parent.y, parent.size == ROCK_LARGE ? KILL_ROCK_LARGE : (parent.size == ROCK_MEDIUM ? KILL_ROCK_MEDIUM : KILL_ROCK_SMALL), points);
    AddScore(g, points);

    if (parent.size <= ROCK_SMALL) return;
    for (int k = 0; k < 2; k++) {
        Rock *child = FreeRockSlot(g);
        if (!child) break;
        float spread = RandRange(g, 0.35f, 0.8f) * (k == 0 ? 1.0f : -1.0f);
        float c = cosf(spread), s = sinf(spread);
        float vx = parent.vx * c - parent.vy * s, vy = parent.vx * s + parent.vy * c;
        float speed = sqrtf(vx * vx + vy * vy);
        float want = speed * 1.25f + 12.0f;          /* smaller rocks are quicker */
        if (want > 150.0f) want = 150.0f;
        if (speed < 1.0f) { vx = 0.0f; vy = -1.0f; speed = 1.0f; }
        *child = (Rock){parent.x, parent.y, vx / speed * want, vy / speed * want,
                        RandRange(g, 0.0f, TAU_F), RandRange(g, -2.0f, 2.0f),
                        parent.size - 1, (uint32_t)Rng_Next(&g->rng), true};
    }
}

static bool AnyRockWithin(const Game *g, float x, float y, float radius) {
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (!g->rocks[i].active) continue;
        if (Game_WrapDist(x, y, g->rocks[i].x, g->rocks[i].y) < radius + Game_RockRadius(g->rocks[i].size)) return true;
    }
    return false;
}

/* ------------------------------------------------------------------- ship */

static void KillShip(Game *g) {
    if (!g->ship.alive) return;
    g->ship.alive = false;
    g->ship.thrusting = false;
    g->justShipDeath = true;
    NoteKill(g, g->ship.x, g->ship.y, KILL_SHIP, 0);
    g->lives--;
    if (g->lives <= 0) {
        g->lives = 0;
        g->phase = GS_GAMEOVER;
        g->justGameOver = true;
        if (g->score > g->highScore) g->highScore = g->score;
        return;
    }
    g->ship.respawnTimer = RESPAWN_SECONDS;
}

static void TryFire(Game *g) {
    if (!g->fireHeld || g->ship.fireCooldown > 0.0f) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g->bullets[i].active) continue;
        float sa = sinf(g->ship.angle), ca = cosf(g->ship.angle);
        g->bullets[i] = (Bullet){WrapPos(g->ship.x + sa * 12.0f, FIELD_W), WrapPos(g->ship.y - ca * 12.0f, FIELD_H),
                                 sa * BULLET_SPEED, -ca * BULLET_SPEED, BULLET_LIFE, true};
        g->ship.fireCooldown = FIRE_COOLDOWN;
        g->justFired = true;
        return;
    }
}

static void StepShip(Game *g) {
    Ship *s = &g->ship;

    if (!s->alive) {
        if (g->phase != GS_PLAYING) return;
        if (s->respawnTimer > 0.0f) s->respawnTimer -= STEP_DT;
        if (s->respawnTimer <= 0.0f) {
            /* Only come back when the middle of the field is safe. */
            bool clear = !AnyRockWithin(g, FIELD_W / 2.0f, FIELD_H / 2.0f, RESPAWN_CLEAR_RADIUS);
            if (g->saucer.active && Game_WrapDist(g->saucer.x, g->saucer.y, FIELD_W / 2.0f, FIELD_H / 2.0f) < RESPAWN_CLEAR_RADIUS) clear = false;
            for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
                if (g->enemyBullets[i].active && Game_WrapDist(g->enemyBullets[i].x, g->enemyBullets[i].y, FIELD_W / 2.0f, FIELD_H / 2.0f) < RESPAWN_CLEAR_RADIUS) clear = false;
            }
            if (clear) {
                *s = (Ship){FIELD_W / 2.0f, FIELD_H / 2.0f, 0.0f, 0.0f, 0.0f, true, false, 0.0f, INVULN_SECONDS, 0.0f};
            }
        }
        return;
    }

    s->angle += g->turn * SHIP_ROT_SPEED * STEP_DT;
    s->angle = fmodf(s->angle, TAU_F);
    s->thrusting = g->thrustHeld;
    if (s->thrusting) {
        s->vx += sinf(s->angle) * SHIP_THRUST * STEP_DT;
        s->vy += -cosf(s->angle) * SHIP_THRUST * STEP_DT;
        float speed = sqrtf(s->vx * s->vx + s->vy * s->vy);
        if (speed > SHIP_MAX_SPEED) {
            s->vx *= SHIP_MAX_SPEED / speed;
            s->vy *= SHIP_MAX_SPEED / speed;
        }
    }
    s->x = WrapPos(s->x + s->vx * STEP_DT, FIELD_W);
    s->y = WrapPos(s->y + s->vy * STEP_DT, FIELD_H);

    if (s->invuln > 0.0f) s->invuln -= STEP_DT;
    if (s->fireCooldown > 0.0f) s->fireCooldown -= STEP_DT;
    TryFire(g);

    /* Hyperspace: a random spot, and a one-in-six chance of coming out in
     * pieces. It does NOT check that the spot is free. */
    if (g->hyperHeld && !g->hyperPrev) {
        if (Rng_Range(&g->rng, 6) == 0) {
            g->justHyperDeath = true;
            KillShip(g);
        } else {
            s->x = RandRange(g, 0.0f, FIELD_W);
            s->y = RandRange(g, 0.0f, FIELD_H);
            g->justHyperspace = true;
        }
    }
}

/* ----------------------------------------------------------------- saucer */

static void StepSaucer(Game *g) {
    Saucer *u = &g->saucer;

    if (!u->active) {
        if (g->rockCount > 0 && g->waveTimer <= 0.0f && (g->saucerTimer -= STEP_DT) <= 0.0f) {
            int smallPct = (int)(g->score / 400);
            if (smallPct > 100) smallPct = 100;
            bool small = (int)Rng_Range(&g->rng, 100) < smallPct;
            int dir = Rng_Range(&g->rng, 2) ? 1 : -1;
            *u = (Saucer){dir > 0 ? -20.0f : FIELD_W + 20.0f, RandRange(g, 50.0f, FIELD_H - 50.0f),
                          (float)dir * (small ? 130.0f : 90.0f), 0.0f, true, small,
                          RandRange(g, 0.8f, 1.6f), small ? 1.0f : 1.4f};
            g->justSaucerAppeared = true;
        }
        return;
    }

    u->x += u->vx * STEP_DT;
    u->y = WrapPos(u->y + u->vy * STEP_DT, FIELD_H);
    if (u->x < -40.0f || u->x > FIELD_W + 40.0f) {
        u->active = false;
        g->saucerTimer = RandRange(g, 12.0f, 20.0f);
        return;
    }

    if ((u->turnTimer -= STEP_DT) <= 0.0f) {
        static const float kDrift[3] = {-45.0f, 0.0f, 45.0f};
        u->vy = kDrift[Rng_Range(&g->rng, 3)];
        u->turnTimer = RandRange(g, 0.8f, 1.6f);
    }

    if ((u->fireTimer -= STEP_DT) <= 0.0f) {
        u->fireTimer = u->small ? RandRange(g, 0.9f, 1.4f) : RandRange(g, 1.2f, 2.0f);
        float angle;
        if (u->small && g->ship.alive) {
            /* Bearing to the ship the SHORT way round the wrapped field, plus
             * an error that shrinks as the score climbs. */
            float dx = Game_WrapDelta(u->x, g->ship.x, FIELD_W), dy = Game_WrapDelta(u->y, g->ship.y, FIELD_H);
            angle = atan2f(dx, -dy) + RandRange(g, -1.0f, 1.0f) * Game_SaucerAimError(g->score);
        } else {
            angle = RandRange(g, 0.0f, TAU_F);
        }
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (g->enemyBullets[i].active) continue;
            g->enemyBullets[i] = (Bullet){u->x, u->y, sinf(angle) * ENEMY_BULLET_SPEED, -cosf(angle) * ENEMY_BULLET_SPEED, ENEMY_BULLET_LIFE, true};
            g->justSaucerFired = true;
            break;
        }
    }
}

static void KillSaucer(Game *g) {
    Saucer *u = &g->saucer;
    int points = u->small ? 1000 : 200;
    NoteKill(g, u->x, u->y, u->small ? KILL_SAUCER_SMALL : KILL_SAUCER_LARGE, points);
    AddScore(g, points);
    u->active = false;
    g->saucerTimer = RandRange(g, 12.0f, 20.0f);
}

/* ------------------------------------------------------------- collisions */

static void StepBullets(Game *g) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g->bullets[i];
        if (!b->active) continue;
        b->x = WrapPos(b->x + b->vx * STEP_DT, FIELD_W);
        b->y = WrapPos(b->y + b->vy * STEP_DT, FIELD_H);
        if ((b->life -= STEP_DT) <= 0.0f) { b->active = false; continue; }

        bool hit = false;
        for (int r = 0; r < MAX_ROCKS && !hit; r++) {
            if (!g->rocks[r].active) continue;
            if (Game_WrapDist(b->x, b->y, g->rocks[r].x, g->rocks[r].y) <= Game_RockRadius(g->rocks[r].size) * 0.9f + 2.0f) {
                BreakRock(g, r);
                hit = true;
            }
        }
        if (!hit && g->saucer.active) {
            float radius = g->saucer.small ? 9.0f : 15.0f;
            if (Game_WrapDist(b->x, b->y, g->saucer.x, g->saucer.y) <= radius + 2.0f) {
                KillSaucer(g);
                hit = true;
            }
        }
        if (hit) b->active = false;
    }
    CountRocks(g);

    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        Bullet *b = &g->enemyBullets[i];
        if (!b->active) continue;
        b->x = WrapPos(b->x + b->vx * STEP_DT, FIELD_W);
        b->y = WrapPos(b->y + b->vy * STEP_DT, FIELD_H);
        if ((b->life -= STEP_DT) <= 0.0f) b->active = false;
    }
}

static void StepShipCollisions(Game *g) {
    Ship *s = &g->ship;
    if (!s->alive || s->invuln > 0.0f) return;

    for (int r = 0; r < MAX_ROCKS; r++) {
        if (!g->rocks[r].active) continue;
        if (Game_WrapDist(s->x, s->y, g->rocks[r].x, g->rocks[r].y) <= Game_RockRadius(g->rocks[r].size) * 0.9f + SHIP_RADIUS * 0.8f) {
            BreakRock(g, r); /* the rock goes too: no free kills, but no free lunch either */
            CountRocks(g);
            KillShip(g);
            return;
        }
    }
    if (g->saucer.active && Game_WrapDist(s->x, s->y, g->saucer.x, g->saucer.y) <= (g->saucer.small ? 9.0f : 15.0f) + SHIP_RADIUS * 0.8f) {
        KillSaucer(g);
        KillShip(g);
        return;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (g->enemyBullets[i].active && Game_WrapDist(s->x, s->y, g->enemyBullets[i].x, g->enemyBullets[i].y) <= SHIP_RADIUS * 0.8f + 2.0f) {
            g->enemyBullets[i].active = false;
            KillShip(g);
            return;
        }
    }
}

/* ------------------------------------------------------------------- step */

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    StepShip(g);
    g->hyperPrev = g->hyperHeld;
    if (g->phase != GS_PLAYING) return;

    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &g->rocks[i];
        if (!r->active) continue;
        r->x = WrapPos(r->x + r->vx * STEP_DT, FIELD_W);
        r->y = WrapPos(r->y + r->vy * STEP_DT, FIELD_H);
        r->angle += r->spin * STEP_DT;
    }

    StepBullets(g);
    StepSaucer(g);
    StepShipCollisions(g);
    if (g->phase != GS_PLAYING) return;

    if (g->waveTimer > 0.0f) {
        g->waveTimer -= STEP_DT;
        if (g->waveTimer <= 0.0f) Game_StartWave(g, g->wave + 1);
    } else if (g->rockCount == 0 && !g->saucer.active) {
        g->justWaveClear = true;
        g->waveTimer = WAVE_DELAY;
        memset(g->enemyBullets, 0, sizeof(g->enemyBullets));
    }

    if (g->rockCount > 0 && g->ship.alive && g->waveTimer <= 0.0f && (g->beatTimer -= STEP_DT) <= 0.0f) {
        g->beatTimer += Game_BeatInterval(g->rockCount);
        g->beatNote ^= 1;
        g->justBeat = true;
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f; /* a long hitch shouldn't fast-forward the field */

    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justFired = g->justBeat = g->justHyperspace = g->justHyperDeath = g->justShipDeath = false;
    g->justSaucerAppeared = g->justSaucerFired = g->justWaveClear = g->justExtraLife = g->justGameOver = false;
    g->killCount = 0;
}
