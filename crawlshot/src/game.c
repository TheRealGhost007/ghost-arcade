#include "game.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */

void Game_WaveLayout(int wave, int *mainLen, int *singles) {
    if (wave < 1) wave = 1;
    int s = wave - 1;
    if (s > 7) s = 7;
    if (mainLen) *mainLen = WORM_TOTAL - s;
    if (singles) *singles = s;
}

float Game_WormInterval(int wave) {
    float t = 0.105f - 0.007f * (float)(wave - 1);
    return t < 0.048f ? 0.048f : t;
}

int Game_SpiderPoints(float distance) {
    if (distance <= 4.0f) return 900;
    if (distance <= 8.0f) return 600;
    return 300;
}

int Game_MushroomsInGarden(const Game *g) {
    int n = 0;
    for (int y = PLAYER_TOP; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g->mush[y][x]) n++;
    return n;
}

int Game_WormCount(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_WORMS; i++) if (g->worms[i].active) n++;
    return n;
}

int Game_SegmentCount(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_WORMS; i++) if (g->worms[i].active) n += g->worms[i].len;
    return n;
}

static float Rand01(Game *g) { return (float)Rng_Range(&g->rng, 10000) / 10000.0f; }
static float RandRange(Game *g, float lo, float hi) { return lo + (hi - lo) * Rand01(g); }

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

const char *Game_PowerName(PowerType t) {
    static const char *const kNames[PU_COUNT] = {"PIERCE", "BLAST", "FREEZE"};
    return (t >= 0 && t < PU_COUNT) ? kNames[t] : "";
}

static void DropSpore(Game *g, float x, float y) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (g->pickups[i].active) continue;
        g->pickups[i] = (Pickup){x, y, (PowerType)Rng_Range(&g->rng, PU_COUNT), true};
        return;
    }
}

static void ClearPowers(Game *g) {
    memset(g->pickups, 0, sizeof(g->pickups));
    g->pierceTimer = g->freezeTimer = 0.0f;
    g->blastCharges = 0;
}

/* ------------------------------------------------------------------ setup */

static Worm *FreeWorm(Game *g) {
    for (int i = 0; i < MAX_WORMS; i++) if (!g->worms[i].active) return &g->worms[i];
    return NULL;
}

/* A caterpillar entering from the top-right corner heading left, its tail
 * still off the edge of the field. */
static void SpawnWorm(Game *g, int len, float delay, int fromRight) {
    Worm *w = FreeWorm(g);
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = true;
    w->len = len;
    w->dir = fromRight ? -1 : 1;
    w->vdir = 1;
    w->enterDelay = delay;
    for (int i = 0; i < len; i++) {
        w->seg[i] = (Cell){fromRight ? COLS - 1 + i : -i, 0};
    }
}

static void SpawnWaveWorms(Game *g) {
    for (int i = 0; i < MAX_WORMS; i++) g->worms[i].active = false;
    int mainLen, singles;
    Game_WaveLayout(g->wave, &mainLen, &singles);
    SpawnWorm(g, mainLen, 0.0f, 1);
    for (int i = 0; i < singles; i++) SpawnWorm(g, 1, 1.6f + 1.4f * (float)i, i % 2 == 0 ? 0 : 1);
    g->wormTimer = 0.0f;
}

static void PlantStartingMushrooms(Game *g) {
    memset(g->mush, 0, sizeof(g->mush));
    memset(g->poison, 0, sizeof(g->poison));
    int planted = 0;
    for (int attempt = 0; attempt < 400 && planted < 34; attempt++) {
        int x = (int)Rng_Range(&g->rng, COLS);
        int y = 1 + (int)Rng_Range(&g->rng, PLAYER_TOP - 2); /* never the top row, never the garden */
        if (g->mush[y][x]) continue;
        g->mush[y][x] = MUSH_HP;
        planted++;
    }
    /* A few in the garden too, so the flea (which comes when the garden is
     * bare) doesn't turn up before you have had a chance to lose any. */
    planted = 0;
    for (int attempt = 0; attempt < 200 && planted < 8; attempt++) {
        int x = (int)Rng_Range(&g->rng, COLS);
        int y = PLAYER_TOP + (int)Rng_Range(&g->rng, ROWS - PLAYER_TOP - 2); /* not the last two rows: you start there */
        if (g->mush[y][x]) continue;
        g->mush[y][x] = MUSH_HP;
        planted++;
    }
}

static void ResetPlayer(Game *g) {
    g->px = COLS / 2.0f;
    g->py = ROWS - 1.5f;
    g->bullet.active = false;
    g->pierceCx = g->pierceCy = -1;
    g->alive = true;
    g->deathTimer = 0.0f;
    g->restoring = false;
}

void Game_StartWave(Game *g, int wave) {
    if (wave < 1) wave = 1;
    g->wave = wave;
    SpawnWaveWorms(g);
    g->spider.active = false;
    g->flea.active = false;
    g->scorpion.active = false;
    g->spiderTimer = RandRange(g, 6.0f, 10.0f);
    g->fleaTimer = 3.0f;
    g->scorpionTimer = wave >= 2 ? RandRange(g, 10.0f, 18.0f) : 1e9f;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->nextExtraAt = EXTRA_LIFE_EVERY;
    PlantStartingMushrooms(g);
    ResetPlayer(g);
    Game_StartWave(g, 1);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, highScore);
}

void Game_SetInput(Game *g, float moveX, float moveY, bool fire) {
    if (moveX < -1.0f) moveX = -1.0f;
    if (moveX > 1.0f) moveX = 1.0f;
    if (moveY < -1.0f) moveY = -1.0f;
    if (moveY > 1.0f) moveY = 1.0f;
    g->moveX = moveX;
    g->moveY = moveY;
    g->fireHeld = fire;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

/* ------------------------------------------------------------- caterpillar */

/* Does the caterpillar treat this cell as a wall? Off the sides isn't a
 * cell at all: the tail of a worm still coming on lives out there. */
static bool WormBlocked(const Game *g, int x, int y) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return false;
    return g->mush[y][x] != 0;
}

static void StepWorm(Game *g, Worm *w) {
    if (w->enterDelay > 0.0f) return;

    Cell head = w->seg[0];
    Cell next = head;

    if (w->diving && head.y >= ROWS - 1) {
        /* The dive is over: it carries on along the bottom row, and its next
         * turn will take it back up into the garden. */
        w->diving = false;
        w->vdir = -1;
    }
    if (w->diving) {
        /* Poisoned: straight down, ignoring everything, until the bottom. */
        next = (Cell){head.x, head.y + 1};
    } else {
        int nx = head.x + w->dir;
        bool offEdge = (w->dir < 0 && nx < 0) || (w->dir > 0 && nx >= COLS);
        bool blocked = !offEdge && WormBlocked(g, nx, head.y);
        if (!offEdge && !blocked) {
            next = (Cell){nx, head.y};
        } else {
            /* Turn: one row down (or up, in the garden), and the other way. */
            if (blocked && g->poison[head.y][nx]) w->diving = true;
            w->dir = -w->dir;
            if (w->diving) {
                next = (Cell){head.x, head.y + 1 > ROWS - 1 ? ROWS - 1 : head.y + 1};
            } else {
                if (w->vdir > 0 && head.y >= ROWS - 1) w->vdir = -1;
                else if (w->vdir < 0 && head.y <= PLAYER_TOP) w->vdir = 1;
                next = (Cell){head.x, head.y + w->vdir};
            }
        }
    }

    for (int i = w->len - 1; i >= 1; i--) w->seg[i] = w->seg[i - 1];
    w->seg[0] = next;
}

static void StepWorms(Game *g) {
    for (int i = 0; i < MAX_WORMS; i++) {
        Worm *w = &g->worms[i];
        if (!w->active) continue;
        if (w->enterDelay > 0.0f) {
            w->enterDelay -= Game_WormInterval(g->wave); /* this runs once per step, so it counts seconds */
            if (w->enterDelay < 0.0f) w->enterDelay = 0.0f;
            continue;
        }
        StepWorm(g, w);
    }
}

/* Removes segment `idx` of worm `wi`: it becomes a toadstool, and whatever was
 * behind it becomes a new caterpillar with a head of its own. */
static void HitSegment(Game *g, int wi, int idx) {
    Worm *w = &g->worms[wi];
    Cell where = w->seg[idx];
    bool wasHead = (idx == 0);

    int points = wasHead ? 100 : 10;
    NoteKill(g, (float)where.x + 0.5f, (float)where.y + 0.5f, wasHead ? KILL_HEAD : KILL_BODY, points);
    AddScore(g, points);

    if (where.x >= 0 && where.x < COLS && where.y >= 0 && where.y < ROWS) {
        g->mush[where.y][where.x] = MUSH_HP;
        g->poison[where.y][where.x] = 0;
    }

    int behind = w->len - idx - 1;
    if (behind > 0) {
        Worm *n = FreeWorm(g);
        if (n) {
            *n = (Worm){true, behind, {{0, 0}}, w->dir, w->vdir, false, 0.0f};
            for (int i = 0; i < behind; i++) n->seg[i] = w->seg[idx + 1 + i];
        }
    }
    if (idx == 0) {
        w->active = false;
    } else {
        w->len = idx;
    }
    if (behind > 0 || idx > 0) g->justSplit = true;
}

/* -------------------------------------------------------------- creatures */

static void MaybeSpawnCreatures(Game *g) {
    if (!g->spider.active && (g->spiderTimer -= STEP_DT) <= 0.0f) {
        bool fromLeft = Rng_Range(&g->rng, 2) == 0;
        g->spider = (Spider){true, fromLeft ? -1.0f : COLS + 1.0f, RandRange(g, (float)PLAYER_TOP, (float)ROWS - 1.5f),
                             (fromLeft ? 1.0f : -1.0f) * RandRange(g, 3.2f, 4.6f), (Rand01(g) < 0.5f ? -1.0f : 1.0f) * 3.4f,
                             RandRange(g, 0.4f, 1.0f)};
        g->justSpiderAppeared = true;
        g->spiderTimer = RandRange(g, 7.0f, 13.0f);
    }

    if (!g->flea.active) {
        g->fleaTimer -= STEP_DT;
        if (g->fleaTimer <= 0.0f && Game_MushroomsInGarden(g) < FLEA_THRESHOLD) {
            g->flea = (Flea){true, (int)Rng_Range(&g->rng, COLS), 0.0f, 0};
            g->justFleaAppeared = true;
            g->fleaTimer = 4.0f;
        } else if (g->fleaTimer <= 0.0f) {
            g->fleaTimer = 1.0f; /* enough toadstools: check again shortly */
        }
    }

    if (!g->scorpion.active && (g->scorpionTimer -= STEP_DT) <= 0.0f) {
        int dir = Rng_Range(&g->rng, 2) ? 1 : -1;
        g->scorpion = (Scorpion){true, dir > 0 ? -1.5f : COLS + 0.5f, 3 + (int)Rng_Range(&g->rng, 16), dir};
        g->justScorpionAppeared = true;
        g->scorpionTimer = RandRange(g, 14.0f, 24.0f);
    }
}

static void StepSpider(Game *g) {
    Spider *s = &g->spider;
    if (!s->active) return;
    s->x += s->vx * STEP_DT;
    s->y += s->vy * STEP_DT;
    if (s->y < (float)PLAYER_TOP - 2.0f) { s->y = (float)PLAYER_TOP - 2.0f; s->vy = fabsf(s->vy); }
    if (s->y > (float)ROWS - 1.2f) { s->y = (float)ROWS - 1.2f; s->vy = -fabsf(s->vy); }
    if ((s->turnTimer -= STEP_DT) <= 0.0f) {
        s->vy = -s->vy;
        s->turnTimer = RandRange(g, 0.4f, 1.1f);
    }
    /* It eats what it walks over. */
    int cx = (int)floorf(s->x), cy = (int)floorf(s->y);
    for (int dx = 0; dx <= 1; dx++) {
        int x = cx + dx;
        if (x >= 0 && x < COLS && cy >= 0 && cy < ROWS) { g->mush[cy][x] = 0; g->poison[cy][x] = 0; }
    }
    if (s->x < -3.0f || s->x > COLS + 3.0f) s->active = false;
}

static void StepFlea(Game *g) {
    Flea *f = &g->flea;
    if (!f->active) return;
    float speed = f->hits > 0 ? 17.0f : 10.0f;
    int rowBefore = (int)floorf(f->y);
    f->y += speed * STEP_DT;
    int rowNow = (int)floorf(f->y);
    if (rowNow != rowBefore && rowNow >= 1 && rowNow < ROWS - 1 && Rng_Range(&g->rng, 3) == 0 && !g->mush[rowNow][f->col]) {
        g->mush[rowNow][f->col] = MUSH_HP;
    }
    if (f->y >= (float)ROWS) f->active = false;
}

static void StepScorpion(Game *g) {
    Scorpion *s = &g->scorpion;
    if (!s->active) return;
    s->x += (float)s->dir * 4.2f * STEP_DT;
    int cx = (int)floorf(s->x);
    for (int dx = 0; dx <= 1; dx++) {
        int x = cx + dx;
        if (x >= 0 && x < COLS && g->mush[s->row][x]) g->poison[s->row][x] = 1;
    }
    if (s->x < -3.0f || s->x > COLS + 3.0f) s->active = false;
}

/* ----------------------------------------------------------------- player */

static bool PlayerBlockedAt(const Game *g, float cx, float cy) {
    const float r = 0.36f;
    int x0 = (int)floorf(cx - r), x1 = (int)floorf(cx + r);
    int y0 = (int)floorf(cy - r), y1 = (int)floorf(cy + r);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return true;
        if (g->mush[y][x]) return true;
    }
    return false;
}

static void StepPlayer(Game *g) {
    float mx = g->moveX, my = g->moveY;
    float len = sqrtf(mx * mx + my * my);
    if (len > 1.0f) { mx /= len; my /= len; }
    float nx = g->px + mx * PLAYER_SPEED * STEP_DT;
    float ny = g->py + my * PLAYER_SPEED * STEP_DT;

    /* The garden is the bottom six rows and nothing else. */
    if (ny < (float)PLAYER_TOP + 0.5f) ny = (float)PLAYER_TOP + 0.5f;
    if (ny > (float)ROWS - 0.5f) ny = (float)ROWS - 0.5f;
    if (nx < 0.5f) nx = 0.5f;
    if (nx > (float)COLS - 0.5f) nx = (float)COLS - 0.5f;

    /* Axis by axis, so you slide along a toadstool instead of sticking to it. */
    if (!PlayerBlockedAt(g, nx, g->py)) g->px = nx;
    if (!PlayerBlockedAt(g, g->px, ny)) g->py = ny;

    if (g->fireHeld && !g->bullet.active) {
        g->bullet = (Bullet){g->px, g->py - 0.6f, true};
        g->pierceCx = g->pierceCy = -1;
        g->justFired = true;
    }
}

static void KillPlayer(Game *g) {
    ClearPowers(g);
    if (!g->alive) return;
    g->alive = false;
    g->bullet.active = false;
    g->deathTimer = DEATH_SECONDS;
    g->justPlayerDeath = true;
    g->lives--;
}

static bool Touches(float ax, float ay, float bx, float by, float rx, float ry) {
    return fabsf(ax - bx) < rx && fabsf(ay - by) < ry;
}

static void CheckPlayerHit(Game *g) {
    if (!g->alive) return;
    for (int i = 0; i < MAX_WORMS; i++) {
        const Worm *w = &g->worms[i];
        if (!w->active || w->enterDelay > 0.0f) continue;
        for (int k = 0; k < w->len; k++) {
            if (Touches(g->px, g->py, (float)w->seg[k].x + 0.5f, (float)w->seg[k].y + 0.5f, 0.78f, 0.78f)) { KillPlayer(g); return; }
        }
    }
    if (g->spider.active && Touches(g->px, g->py, g->spider.x + 0.5f, g->spider.y, 1.0f, 0.75f)) { KillPlayer(g); return; }
    if (g->flea.active && Touches(g->px, g->py, (float)g->flea.col + 0.5f, g->flea.y, 0.7f, 0.75f)) { KillPlayer(g); return; }
    /* The scorpion only poisons; it is harmless to you. */
}

/* ------------------------------------------------------------------ shots */

/* What a shot does when it lands: stop, or (piercing) carry on, and (blast
 * charges) take the eight cells around it too. */
static void BulletImpact(Game *g, Bullet *b, int cx, int cy, bool pierce) {
    if (g->blastCharges > 0) {
        g->blastCharges--;
        g->justBlast = true;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= COLS || y < 0 || y >= ROWS || (dx == 0 && dy == 0)) continue;
                for (int i = 0; i < MAX_WORMS; i++) {
                    Worm *w = &g->worms[i];
                    if (!w->active || w->enterDelay > 0.0f) continue;
                    for (int k = 0; k < w->len; k++) {
                        if (w->seg[k].x == x && w->seg[k].y == y) { HitSegment(g, i, k); break; }
                    }
                }
                if (g->mush[y][x]) {
                    g->mush[y][x] = 0;
                    g->poison[y][x] = 0;
                    NoteKill(g, (float)x + 0.5f, (float)y + 0.5f, KILL_MUSHROOM, 1);
                    AddScore(g, 1);
                }
            }
        }
    }
    if (pierce) { g->pierceCx = cx; g->pierceCy = cy; }
    else b->active = false;
}

static void StepBullet(Game *g) {
    Bullet *b = &g->bullet;
    if (!b->active) return;
    b->y -= BULLET_SPEED * STEP_DT;
    if (b->y < 0.0f) { b->active = false; return; }

    int cx = (int)floorf(b->x), cy = (int)floorf(b->y);

    if (g->spider.active && Touches(b->x, b->y, g->spider.x + 0.5f, g->spider.y, 1.0f, 0.8f)) {
        float dist = sqrtf((g->px - g->spider.x) * (g->px - g->spider.x) + (g->py - g->spider.y) * (g->py - g->spider.y));
        int points = Game_SpiderPoints(dist);
        NoteKill(g, g->spider.x + 0.5f, g->spider.y, KILL_SPIDER, points);
        AddScore(g, points);
        DropSpore(g, g->spider.x + 0.5f, g->spider.y);
        g->spider.active = false;
        b->active = false;
        return;
    }
    if (g->flea.active && Touches(b->x, b->y, (float)g->flea.col + 0.5f, g->flea.y, 0.7f, 0.8f)) {
        b->active = false;
        if (++g->flea.hits >= 2) {
            NoteKill(g, (float)g->flea.col + 0.5f, g->flea.y, KILL_FLEA, 200);
            AddScore(g, 200);
            DropSpore(g, (float)g->flea.col + 0.5f, g->flea.y);
            g->flea.active = false;
        }
        return;
    }
    if (g->scorpion.active && Touches(b->x, b->y, g->scorpion.x + 0.5f, (float)g->scorpion.row + 0.5f, 1.2f, 0.7f)) {
        NoteKill(g, g->scorpion.x + 0.5f, (float)g->scorpion.row + 0.5f, KILL_SCORPION, 1000);
        AddScore(g, 1000);
        DropSpore(g, g->scorpion.x + 0.5f, (float)g->scorpion.row + 0.5f);
        g->scorpion.active = false;
        b->active = false;
        return;
    }
    if (cx == g->pierceCx && cy == g->pierceCy) return; /* still inside the cell it just went through */

    bool pierce = g->pierceTimer > 0.0f;
    for (int i = 0; i < MAX_WORMS; i++) {
        Worm *w = &g->worms[i];
        if (!w->active || w->enterDelay > 0.0f) continue;
        for (int k = 0; k < w->len; k++) {
            if (w->seg[k].x == cx && w->seg[k].y == cy) {
                HitSegment(g, i, k);
                BulletImpact(g, b, cx, cy, pierce);
                return;
            }
        }
    }
    if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS && g->mush[cy][cx]) {
        g->justMushroomHit = true;
        if (--g->mush[cy][cx] == 0) {
            g->poison[cy][cx] = 0;
            NoteKill(g, (float)cx + 0.5f, (float)cy + 0.5f, KILL_MUSHROOM, 1);
            AddScore(g, 1);
            if (Rng_Range(&g->rng, PICKUP_DROP_ODDS) == 0) DropSpore(g, (float)cx + 0.5f, (float)cy + 0.5f);
        }
        BulletImpact(g, b, cx, cy, pierce);
    }
}

static void StepPickups(Game *g) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g->pickups[i];
        if (!p->active) continue;
        p->y += PICKUP_FALL * STEP_DT;
        if (p->y > (float)ROWS) { p->active = false; continue; }
        if (Touches(g->px, g->py, p->x, p->y, 0.9f, 0.9f)) {
            p->active = false;
            g->lastPower = p->type;
            g->justPowerUp = true;
            if (p->type == PU_PIERCE) g->pierceTimer = PIERCE_SECONDS;
            else if (p->type == PU_BLAST) g->blastCharges = BLAST_CHARGES;
            else g->freezeTimer = FREEZE_SECONDS;
        }
    }
}

/* ------------------------------------------------------------------- step */

/* After a death the field is tidied up: every damaged or poisoned toadstool
 * is healed, one at a time, for a small bonus. */
static bool RestoreOne(Game *g) {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (g->mush[y][x] && (g->mush[y][x] < MUSH_HP || g->poison[y][x])) {
                g->mush[y][x] = MUSH_HP;
                g->poison[y][x] = 0;
                AddScore(g, 5);
                return true;
            }
        }
    }
    return false;
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    if (!g->alive) {
        if (g->deathTimer > 0.0f) {
            g->deathTimer -= STEP_DT;
            if (g->deathTimer <= 0.0f) {
                g->deathTimer = 0.0f;
                if (g->lives <= 0) {
                    g->lives = 0;
                    g->phase = GS_GAMEOVER;
                    g->justGameOver = true;
                    if (g->score > g->highScore) g->highScore = g->score;
                    return;
                }
                g->restoring = true;
                g->restoreTimer = RESTORE_INTERVAL;
                g->spider.active = g->flea.active = g->scorpion.active = false;
            }
            return;
        }
        if (g->restoring) {
            g->restoreTimer -= STEP_DT;
            if (g->restoreTimer <= 0.0f) {
                g->restoreTimer += RESTORE_INTERVAL;
                if (RestoreOne(g)) g->justRestoreTick = true;
                else {
                    /* tidy: a fresh caterpillar for the wave, and you are back */
                    SpawnWaveWorms(g);
                    g->spiderTimer = RandRange(g, 5.0f, 9.0f);
                    ResetPlayer(g);
                }
            }
        }
        return;
    }

    StepPlayer(g);
    if (g->pierceTimer > 0.0f) g->pierceTimer -= STEP_DT;
    StepBullet(g);
    StepPickups(g);
    MaybeSpawnCreatures(g);
    StepSpider(g);
    StepFlea(g);
    StepScorpion(g);

    if (g->freezeTimer > 0.0f) g->freezeTimer -= STEP_DT;
    else g->wormTimer += STEP_DT;
    float interval = Game_WormInterval(g->wave);
    while (g->wormTimer >= interval) {
        g->wormTimer -= interval;
        StepWorms(g);
    }

    CheckPlayerHit(g);
    if (!g->alive) return;

    if (Game_WormCount(g) == 0) {
        g->justWaveClear = true;
        Game_StartWave(g, g->wave + 1);
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f; /* a long hitch shouldn't let the caterpillar walk over you */

    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justFired = g->justMushroomHit = g->justPlayerDeath = false;
    g->justSpiderAppeared = g->justFleaAppeared = g->justScorpionAppeared = false;
    g->justRestoreTick = g->justWaveClear = g->justExtraLife = g->justGameOver = g->justSplit = false;
    g->justPowerUp = g->justBlast = false;
    g->killCount = 0;
}
