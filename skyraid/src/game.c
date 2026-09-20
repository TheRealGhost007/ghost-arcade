#include "game.h"
#include <math.h>
#include <string.h>

float Game_MarchInterval(int aliveCount, int wave) {
    if (aliveCount < 1) aliveCount = 1;
    int full = INV_ROWS * INV_COLS;
    float base = 0.045f + 0.62f * ((float)(aliveCount - 1) / (float)(full - 1));
    float waveFactor = 1.0f - 0.05f * (float)(wave - 1);
    if (waveFactor < 0.6f) waveFactor = 0.6f;
    return base * waveFactor;
}

int Game_RowPoints(int row) {
    if (row <= 0) return 30;
    if (row <= 2) return 20;
    return 10;
}

int Game_UfoPoints(int shotsFired) {
    static const int kTable[15] = {100, 50, 50, 100, 150, 100, 100, 50, 300, 100, 100, 100, 50, 150, 100};
    if (shotsFired < 0) shotsFired = 0;
    return kTable[shotsFired % 15];
}

void Game_InvaderRect(const Game *g, int row, int col, float *x, float *y) {
    *x = g->formX + (float)(col * INV_CELL_W);
    *y = g->formY + (float)(row * INV_CELL_H);
}

float Game_BunkerX(int index) {
    return (float)(BUNKER_FIRST_X + index * BUNKER_SPACING);
}

bool Game_BunkerShape(int row, int col) {
    if (row < 0 || row >= BUNKER_ROWS || col < 0 || col >= BUNKER_COLS) return false;
    if (row < 4 && (col < 4 - row || col > BUNKER_COLS - 5 + row)) return false; /* chamfered shoulders */
    if (row >= 10 && col >= 7 && col <= 14) return false;                        /* the arch */
    if (row >= 8 && row < 10 && col >= 8 && col <= 13) return false;
    return true;
}

static const float *sTuneFire = NULL, *sTuneSpeed = NULL;
void Game_SetTuning(const float *fireDelayMult, const float *playerSpeed) { sTuneFire = fireDelayMult; sTuneSpeed = playerSpeed; }

static float NextFireDelay(Game *g) {
    float base = 1.0f - 0.06f * (float)g->wave;
    if (base < 0.35f) base = 0.35f;
    return base * (sTuneFire ? *sTuneFire : 1.0f) * (0.6f + (float)Rng_Range(&g->rng, 80) / 100.0f);
}

static float NextUfoDelay(Game *g) {
    return 18.0f + (float)Rng_Range(&g->rng, 10);
}

void Game_StartWave(Game *g, int wave) {
    if (wave < 1) wave = 1;
    g->wave = wave;

    for (int r = 0; r < INV_ROWS; r++) for (int c = 0; c < INV_COLS; c++) g->alive[r][c] = true;
    g->aliveCount = INV_ROWS * INV_COLS;

    int lower = wave - 1;
    if (lower > WAVE_LOWER_MAX) lower = WAVE_LOWER_MAX;
    float formationW = (float)((INV_COLS - 1) * INV_CELL_W + INV_W);
    g->formX = ((float)FIELD_W - formationW) / 2.0f;
    g->formY = (float)(FORMATION_START_Y + lower * WAVE_LOWER_STEP);
    g->marchDir = 1;
    g->marchTimer = 0.0f;
    g->marchFrame = 0;
    g->marchNote = 0;

    for (int b = 0; b < BUNKER_COUNT; b++) {
        for (int r = 0; r < BUNKER_ROWS; r++) {
            for (int c = 0; c < BUNKER_COLS; c++) g->bunker[b][r][c] = Game_BunkerShape(r, c) ? 1 : 0;
        }
    }

    g->playerShot.active = false;
    memset(g->extraShots, 0, sizeof(g->extraShots));
    memset(g->capsules, 0, sizeof(g->capsules));
    g->fireCooldown = 0.0f;
    memset(g->enemyShots, 0, sizeof(g->enemyShots));
    g->enemyFireTimer = NextFireDelay(g) + 0.8f; /* a breath before the first volley */
    g->ufoActive = false;
    g->ufoTimer = NextUfoDelay(g);
    g->respawnTimer = 0.0f;
    g->stepAccumulator = 0.0f;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->playerX = FIELD_W / 2.0f;
    Game_StartWave(g, 1);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, highScore);
}

void Game_SetInput(Game *g, float moveDir, bool fire) {
    if (moveDir < -1.0f) moveDir = -1.0f;
    if (moveDir > 1.0f) moveDir = 1.0f;
    g->moveDir = moveDir;
    g->fireHeld = fire;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

static void AddScore(Game *g, int points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    if (!g->extraLifeGiven && g->score >= EXTRA_LIFE_SCORE) {
        g->extraLifeGiven = true;
        if (g->lives < MAX_LIVES) g->lives++;
        g->justExtraLife = true;
    }
}

const char *Game_PowerName(PowerType t) {
    static const char *const kNames[PU_COUNT] = {"RAPID", "TWIN", "SHIELD", "NOVA"};
    return (t >= 0 && t < PU_COUNT) ? kNames[t] : "";
}

static Shot *ShotSlot(Game *g, int i) { return i == 0 ? &g->playerShot : &g->extraShots[i - 1]; }

int Game_ActiveShots(const Game *g) {
    int n = g->playerShot.active ? 1 : 0;
    for (int i = 0; i < MAX_EXTRA_SHOTS; i++) if (g->extraShots[i].active) n++;
    return n;
}

static void SpawnCapsule(Game *g, float x, float y, PowerType type) {
    for (int i = 0; i < MAX_CAPSULES; i++) {
        if (g->capsules[i].active) continue;
        g->capsules[i] = (Capsule){x, y, type, true};
        return;
    }
}

static void NoteKill(Game *g, float x, float y, int row, int points) {
    if (g->killCount >= MAX_KILLS_PER_FRAME) return;
    g->kills[g->killCount++] = (Kill){x, y, row, points};
}

static void EndGame(Game *g, bool invaded) {
    g->phase = GS_GAMEOVER;
    g->invaded = invaded;
    g->justGameOver = true;
    if (g->score > g->highScore) g->highScore = g->score;
}

/* Blasts a ragged hole around bunker cell (cr, cc). */
static void ErodeBunker(Game *g, int b, int cr, int cc) {
    for (int dr = -3; dr <= 3; dr++) {
        for (int dc = -3; dc <= 3; dc++) {
            int r = cr + dr, c = cc + dc;
            if (r < 0 || r >= BUNKER_ROWS || c < 0 || c >= BUNKER_COLS) continue;
            int d2 = dr * dr + dc * dc;
            if (d2 <= 4 || (d2 <= 9 && Rng_Range(&g->rng, 3) == 0)) g->bunker[b][r][c] = 0;
        }
    }
    g->justBunkerHit = true;
}

/* True (and the bunker eroded) if the point lies in a solid bunker cell. */
static bool HitBunkerAt(Game *g, float x, float y) {
    if (y < BUNKER_Y || y >= BUNKER_Y + BUNKER_ROWS * BUNKER_CELL) return false;
    for (int b = 0; b < BUNKER_COUNT; b++) {
        float bx = Game_BunkerX(b);
        if (x < bx || x >= bx + BUNKER_COLS * BUNKER_CELL) continue;
        int c = (int)((x - bx) / BUNKER_CELL), r = (int)((y - BUNKER_Y) / BUNKER_CELL);
        if (!g->bunker[b][r][c]) return false;
        ErodeBunker(g, b, r, c);
        return true;
    }
    return false;
}

/* Invaders don't shoot their way through bunkers, they just walk over them. */
static void TrampleBunkers(Game *g) {
    for (int row = 0; row < INV_ROWS; row++) {
        for (int col = 0; col < INV_COLS; col++) {
            if (!g->alive[row][col]) continue;
            float ix, iy;
            Game_InvaderRect(g, row, col, &ix, &iy);
            if (iy + INV_H <= BUNKER_Y || iy >= BUNKER_Y + BUNKER_ROWS * BUNKER_CELL) continue;
            for (int b = 0; b < BUNKER_COUNT; b++) {
                float bx = Game_BunkerX(b);
                for (int r = 0; r < BUNKER_ROWS; r++) {
                    float cy = (float)(BUNKER_Y + r * BUNKER_CELL);
                    if (cy + BUNKER_CELL <= iy || cy >= iy + INV_H) continue;
                    for (int c = 0; c < BUNKER_COLS; c++) {
                        float cx = bx + (float)(c * BUNKER_CELL);
                        if (cx + BUNKER_CELL > ix && cx < ix + INV_W) g->bunker[b][r][c] = 0;
                    }
                }
            }
        }
    }
}

static void March(Game *g) {
    /* Bounds of what is still alive: an emptied edge column lets the
     * formation travel further before it turns. */
    int minCol = INV_COLS, maxCol = -1, maxRow = -1;
    for (int r = 0; r < INV_ROWS; r++) {
        for (int c = 0; c < INV_COLS; c++) {
            if (!g->alive[r][c]) continue;
            if (c < minCol) minCol = c;
            if (c > maxCol) maxCol = c;
            if (r > maxRow) maxRow = r;
        }
    }
    if (maxCol < 0) return;

    float left = g->formX + (float)(minCol * INV_CELL_W) + (float)(g->marchDir * MARCH_DX);
    float right = g->formX + (float)(maxCol * INV_CELL_W + INV_W) + (float)(g->marchDir * MARCH_DX);
    if (left < FORMATION_MARGIN || right > FIELD_W - FORMATION_MARGIN) {
        g->formY += MARCH_DROP;
        g->marchDir = -g->marchDir;
    } else {
        g->formX += (float)(g->marchDir * MARCH_DX);
    }

    g->marchFrame ^= 1;
    g->marchNote = (g->marchNote + 1) % 4;
    g->justMarched = true;
    TrampleBunkers(g);

    float lowest = g->formY + (float)(maxRow * INV_CELL_H + INV_H);
    if (lowest >= PLAYER_Y) EndGame(g, true);
}

static void EnemyFire(Game *g) {
    int slot = -1;
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) if (!g->enemyShots[i].active) { slot = i; break; }
    if (slot < 0) return;

    /* Pick a random column that still has someone in it; only the lowest
     * invader in a column has a clear shot. */
    int cols[INV_COLS], n = 0;
    for (int c = 0; c < INV_COLS; c++) {
        for (int r = 0; r < INV_ROWS; r++) if (g->alive[r][c]) { cols[n++] = c; break; }
    }
    if (n == 0) return;
    int col = cols[Rng_Range(&g->rng, (uint32_t)n)];
    int row = INV_ROWS - 1;
    while (row >= 0 && !g->alive[row][col]) row--;

    float ix, iy;
    Game_InvaderRect(g, row, col, &ix, &iy);
    g->enemyShots[slot] = (Shot){ix + INV_W / 2.0f, iy + INV_H, true};
}

static void KillPlayer(Game *g) {
    if (g->shield) {
        g->shield = false;
        g->justShieldHit = true;
        return;
    }
    g->justPlayerHit = true;
    g->lives--;
    g->playerShot.active = false;
    memset(g->extraShots, 0, sizeof(g->extraShots));
    memset(g->capsules, 0, sizeof(g->capsules));
    g->rapidTimer = g->twinTimer = 0.0f;
    memset(g->enemyShots, 0, sizeof(g->enemyShots));
    if (g->lives <= 0) {
        g->lives = 0;
        EndGame(g, false);
        return;
    }
    g->respawnTimer = RESPAWN_SECONDS;
}

static void StepPlayerShot(Game *g, Shot *s) {
    if (!s->active) return;
    s->y -= PLAYER_SHOT_SPEED * STEP_DT;
    if (s->y + SHOT_H < 0.0f) { s->active = false; return; }

    if (HitBunkerAt(g, s->x, s->y)) { s->active = false; return; }

    if (g->ufoActive && s->y <= UFO_Y + UFO_H && s->y + SHOT_H >= UFO_Y &&
        s->x >= g->ufoX && s->x <= g->ufoX + UFO_W) {
        int points = Game_UfoPoints(g->shotsFired);
        NoteKill(g, g->ufoX + UFO_W / 2.0f, UFO_Y + UFO_H / 2.0f, -1, points);
        AddScore(g, points);
        SpawnCapsule(g, g->ufoX + UFO_W / 2.0f, UFO_Y + UFO_H, (PowerType)Rng_Range(&g->rng, PU_COUNT));
        g->ufoActive = false;
        g->ufoTimer = NextUfoDelay(g);
        s->active = false;
        return;
    }

    /* Which formation cell is the shot's tip in? */
    float relX = s->x - g->formX, relY = s->y - g->formY;
    if (relX < 0.0f || relY < 0.0f) return;
    int col = (int)(relX / INV_CELL_W), row = (int)(relY / INV_CELL_H);
    if (col >= INV_COLS || row >= INV_ROWS || !g->alive[row][col]) return;
    if (relX - (float)(col * INV_CELL_W) > INV_W || relY - (float)(row * INV_CELL_H) > INV_H) return; /* the gap between sprites */

    g->alive[row][col] = false;
    g->aliveCount--;
    s->active = false;
    float ix, iy;
    Game_InvaderRect(g, row, col, &ix, &iy);
    int points = Game_RowPoints(row);
    NoteKill(g, ix + INV_W / 2.0f, iy + INV_H / 2.0f, row, points);
    AddScore(g, points);
    if (Rng_Range(&g->rng, CAPSULE_DROP_ODDS) == 0)
        SpawnCapsule(g, ix + INV_W / 2.0f, iy + INV_H, (PowerType)Rng_Range(&g->rng, PU_COUNT));
}

/* The lowest surviving row goes up in one go. */
static void Nova(Game *g) {
    int row = -1;
    for (int r = INV_ROWS - 1; r >= 0 && row < 0; r--)
        for (int c = 0; c < INV_COLS; c++) if (g->alive[r][c]) { row = r; break; }
    if (row < 0) return;
    for (int c = 0; c < INV_COLS; c++) {
        if (!g->alive[row][c]) continue;
        g->alive[row][c] = false;
        g->aliveCount--;
        float ix, iy;
        Game_InvaderRect(g, row, c, &ix, &iy);
        NoteKill(g, ix + INV_W / 2.0f, iy + INV_H / 2.0f, row, Game_RowPoints(row));
        AddScore(g, Game_RowPoints(row));
    }
    g->justNova = true;
}

static void Collect(Game *g, PowerType t) {
    g->lastPower = t;
    g->justPowerUp = true;
    switch (t) {
        case PU_RAPID: g->rapidTimer = POWER_SECONDS; break;
        case PU_TWIN: g->twinTimer = POWER_SECONDS; break;
        case PU_SHIELD: g->shield = true; break;
        case PU_NOVA: Nova(g); break;
        default: break;
    }
}

static void StepCapsules(Game *g) {
    for (int i = 0; i < MAX_CAPSULES; i++) {
        Capsule *k = &g->capsules[i];
        if (!k->active) continue;
        k->y += CAPSULE_SPEED * STEP_DT;
        if (k->y - CAPSULE_H / 2.0f > FIELD_H) { k->active = false; continue; }
        if (k->y + CAPSULE_H / 2.0f >= PLAYER_Y && k->y - CAPSULE_H / 2.0f <= PLAYER_Y + PLAYER_H &&
            fabsf(k->x - g->playerX) <= (PLAYER_W + CAPSULE_W) / 2.0f) {
            k->active = false;
            Collect(g, k->type);
        }
    }
}

static void Fire(Game *g) {
    bool rapid = g->rapidTimer > 0.0f, twin = g->twinTimer > 0.0f;
    if (g->fireCooldown > 0.0f) return;
    int active = Game_ActiveShots(g);
    int need = twin ? 2 : 1;
    if (!rapid && active > 0) return; /* one volley at a time: missing costs you the wait */
    int free = MAX_EXTRA_SHOTS + 1 - active;
    if (free < need) return;
    int placed = 0;
    for (int i = 0; i < MAX_EXTRA_SHOTS + 1 && placed < need; i++) {
        Shot *s = ShotSlot(g, i);
        if (s->active) continue;
        float dx = twin ? (placed == 0 ? -7.0f : 7.0f) : 0.0f;
        *s = (Shot){g->playerX + dx, (float)PLAYER_Y - SHOT_H, true};
        placed++;
    }
    g->shotsFired++;
    g->justFired = true;
    g->fireCooldown = rapid ? RAPID_COOLDOWN : 0.0f;
}

static void StepEnemyShots(Game *g) {
    float speed = 210.0f + 10.0f * (float)g->wave;
    if (speed > 330.0f) speed = 330.0f;

    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
        Shot *s = &g->enemyShots[i];
        if (!s->active) continue;
        s->y += speed * STEP_DT;
        if (s->y > FIELD_H) { s->active = false; continue; }
        if (HitBunkerAt(g, s->x, s->y + SHOT_H)) { s->active = false; continue; }

        if (s->y + SHOT_H >= PLAYER_Y && s->y <= PLAYER_Y + PLAYER_H &&
            s->x >= g->playerX - PLAYER_W / 2.0f && s->x <= g->playerX + PLAYER_W / 2.0f) {
            s->active = false;
            KillPlayer(g);
            return;
        }
    }
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    /* The classic beat after losing a ship: everything holds still. */
    if (g->respawnTimer > 0.0f) {
        g->respawnTimer -= STEP_DT;
        if (g->respawnTimer <= 0.0f) {
            g->respawnTimer = 0.0f;
            g->playerX = FIELD_W / 2.0f;
        }
        return;
    }

    g->playerX += g->moveDir * (sTuneSpeed ? *sTuneSpeed : PLAYER_SPEED) * STEP_DT;
    if (g->playerX < PLAYER_W / 2.0f) g->playerX = PLAYER_W / 2.0f;
    if (g->playerX > FIELD_W - PLAYER_W / 2.0f) g->playerX = FIELD_W - PLAYER_W / 2.0f;

    if (g->fireCooldown > 0.0f) g->fireCooldown -= STEP_DT;
    if (g->rapidTimer > 0.0f) g->rapidTimer -= STEP_DT;
    if (g->twinTimer > 0.0f) g->twinTimer -= STEP_DT;
    if (g->fireHeld) Fire(g);

    for (int i = 0; i < MAX_EXTRA_SHOTS + 1; i++) StepPlayerShot(g, ShotSlot(g, i));
    StepCapsules(g);

    if (g->aliveCount <= 0) {
        g->justWaveClear = true;
        Game_StartWave(g, g->wave + 1);
        return;
    }

    g->marchTimer += STEP_DT;
    float interval = Game_MarchInterval(g->aliveCount, g->wave);
    if (g->marchTimer >= interval) {
        g->marchTimer -= interval;
        March(g);
        if (g->phase != GS_PLAYING) return;
    }

    g->enemyFireTimer -= STEP_DT;
    if (g->enemyFireTimer <= 0.0f) {
        EnemyFire(g);
        g->enemyFireTimer = NextFireDelay(g);
    }
    StepEnemyShots(g);
    if (g->phase != GS_PLAYING || g->respawnTimer > 0.0f) return;

    if (g->ufoActive) {
        g->ufoX += (float)g->ufoDir * UFO_SPEED * STEP_DT;
        if (g->ufoX < -(float)UFO_W || g->ufoX > (float)FIELD_W) {
            g->ufoActive = false;
            g->ufoTimer = NextUfoDelay(g);
        }
    } else if (g->aliveCount >= 8 && (g->ufoTimer -= STEP_DT) <= 0.0f) {
        g->ufoActive = true;
        g->ufoDir = Rng_Range(&g->rng, 2) ? 1 : -1;
        g->ufoX = g->ufoDir > 0 ? -(float)UFO_W : (float)FIELD_W;
        g->justUfoAppeared = true;
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f; /* a long hitch shouldn't fast-forward an invasion */

    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justFired = g->justMarched = g->justPlayerHit = g->justBunkerHit = false;
    g->justUfoAppeared = g->justWaveClear = g->justGameOver = g->justExtraLife = false;
    g->justPowerUp = g->justShieldHit = g->justNova = false;
    g->killCount = 0;
}
