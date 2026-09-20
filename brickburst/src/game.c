#include "game.h"
#include <math.h>
#include <string.h>

#define DEG2RAD (3.14159265358979f / 180.0f)

bool Level_Parse(const char *text, uint8_t out[BRICK_ROWS][BRICK_COLS]) {
    memset(out, 0, (size_t)BRICK_ROWS * BRICK_COLS);

    int row = 0, col = 0, breakable = 0;
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '\r') continue;
        if (c == '\n') {
            row++;
            col = 0;
            continue;
        }
        if (row >= BRICK_ROWS || col >= BRICK_COLS) {
            /* Anything beyond the grid must at least be blank. */
            if (c != '.' && c != ' ') goto invalid;
            col++;
            continue;
        }

        uint8_t type = BRICK_NONE;
        switch (c) {
            case '.': case ' ': type = BRICK_NONE; break;
            case '1': type = BRICK_ONE; break;
            case '2': type = BRICK_TWO; break;
            case '3': type = BRICK_THREE; break;
            case '#': type = BRICK_STEEL; break;
            case '*': type = BRICK_BOMB; break;
            default: goto invalid;
        }
        out[row][col] = type;
        if (type != BRICK_NONE && type != BRICK_STEEL) breakable++;
        col++;
    }
    if (breakable > 0) return true;

invalid:
    memset(out, 0, (size_t)BRICK_ROWS * BRICK_COLS);
    return false;
}

static uint8_t HitPointsFor(uint8_t type) {
    switch (type) {
        case BRICK_TWO: return 2;
        case BRICK_THREE: return 3;
        case BRICK_NONE: return 0;
        default: return 1;
    }
}

float Game_EffectiveSpeed(const Game *g) {
    return g->slowTimer > 0.0f ? g->speed * SLOW_FACTOR : g->speed;
}

int Game_BallsInPlay(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_BALLS; i++) if (g->balls[i].active) n++;
    return n;
}

static void ParkBallOnPaddle(Game *g) {
    memset(g->balls, 0, sizeof(g->balls));
    Ball *b = &g->balls[0];
    b->active = true;
    b->stuck = true;
    b->stuckOffset = 0.0f;
    b->x = g->paddleX;
    b->y = PADDLE_Y - BALL_RADIUS;
}

static void ClearPickupsAndTimers(Game *g) {
    memset(g->powerups, 0, sizeof(g->powerups));
    g->wideTimer = g->slowTimer = g->fireTimer = 0.0f;
    g->laserTimer = g->stickyTimer = g->shotCooldown = 0.0f;
    memset(g->shots, 0, sizeof(g->shots));
    g->shield = false;
    g->combo = 0;
    g->paddleW = PADDLE_W_NORMAL;
}

void Game_StartLevel(Game *g, int level) {
    if (level < 1) level = 1;
    g->level = level;

    int count = Levels_Count();
    if (count <= 0 || !Level_Parse(Levels_Text((level - 1) % count), g->brick)) {
        /* Never leave the player facing an empty board. */
        memset(g->brick, 0, sizeof(g->brick));
        for (int c = 0; c < BRICK_COLS; c++) g->brick[2][c] = BRICK_ONE;
    }

    g->bricksLeft = 0;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            g->hp[r][c] = HitPointsFor(g->brick[r][c]);
            if (g->brick[r][c] != BRICK_NONE && g->brick[r][c] != BRICK_STEEL) g->bricksLeft++;
        }
    }

    g->speed = BALL_SPEED_BASE + BALL_SPEED_PER_LEVEL * (float)(level - 1);
    if (g->speed > BALL_SPEED_CAP * 0.8f) g->speed = BALL_SPEED_CAP * 0.8f;

    g->paddleX = FIELD_W / 2.0f;
    ClearPickupsAndTimers(g);
    ParkBallOnPaddle(g);
    g->stepAccumulator = 0.0f;
}

void Game_Init(Game *g, uint64_t seed, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->justPowerup = -1;
    Game_StartLevel(g, 1);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    bool combo = g->comboEnabled;
    Game_Init(g, seed, highScore);
    g->comboEnabled = combo;
}

void Game_SetComboScoring(Game *g, bool on) { g->comboEnabled = on; }

int Game_ComboMultiplier(const Game *g) {
    if (!g->comboEnabled) return 1;
    int m = 1 + g->combo / COMBO_STEP;
    return m > COMBO_MAX ? COMBO_MAX : m;
}

void Game_SetInput(Game *g, float moveDir, bool launch) {
    if (moveDir < -1.0f) moveDir = -1.0f;
    if (moveDir > 1.0f) moveDir = 1.0f;
    g->moveDir = moveDir;
    g->launchHeld = launch;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

static void SetBallVelocity(Ball *b, float speed, float angleFromUpDeg) {
    float a = angleFromUpDeg * DEG2RAD;
    b->vx = speed * sinf(a);
    b->vy = -speed * cosf(a);
}

/* Rescales to `speed` and guarantees a minimum vertical component, keeping
 * the signs the bounce just produced. */
static void NormalizeVelocity(Ball *b, float speed) {
    float len = sqrtf(b->vx * b->vx + b->vy * b->vy);
    if (len < 0.0001f) {
        b->vx = 0.0f;
        b->vy = -speed;
        return;
    }
    b->vx *= speed / len;
    b->vy *= speed / len;

    float minVy = speed * MIN_VERTICAL_SHARE;
    if (fabsf(b->vy) < minVy) {
        float signY = b->vy < 0.0f ? -1.0f : 1.0f;
        float signX = b->vx < 0.0f ? -1.0f : 1.0f;
        b->vy = signY * minVy;
        b->vx = signX * sqrtf(speed * speed - minVy * minVy);
    }
}

static void SpawnPowerup(Game *g, float x, float y) {
    if ((int)Rng_Range(&g->rng, 100) >= POWERUP_CHANCE_PCT) return;

    /* Weighted so the run-changing ones stay special. */
    static const int kWeights[PU_COUNT] = {24, 20, 14, 12, 6, 10, 8, 6};
    int roll = (int)Rng_Range(&g->rng, 100);
    PowerupType type = PU_MULTI;
    for (int t = 0; t < PU_COUNT; t++) {
        if (roll < kWeights[t]) { type = (PowerupType)t; break; }
        roll -= kWeights[t];
    }

    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g->powerups[i].active) continue;
        g->powerups[i] = (Powerup){x, y, type, true};
        return;
    }
}

static void BreakBrick(Game *g, int col, int row) {
    uint8_t type = g->brick[row][col];
    if (type == BRICK_NONE || type == BRICK_STEEL) return;

    g->brick[row][col] = BRICK_NONE;
    g->hp[row][col] = 0;
    g->bricksLeft--;
    if (g->brokenCount < MAX_BROKEN_PER_STEP) {
        g->broken[g->brokenCount++] = (BrokenBrick){(uint8_t)col, (uint8_t)row, type};
    }

    static const long kPoints[] = {0, 10, 20, 30, 0, 15};
    g->score += kPoints[type] * Game_ComboMultiplier(g);
    if (g->comboEnabled) g->combo++;
    if (g->score > g->highScore) g->highScore = g->score;

    SpawnPowerup(g, (float)(col * BRICK_W) + BRICK_W / 2.0f, (float)(BRICK_TOP + row * BRICK_H) + BRICK_H / 2.0f);

    if (type == BRICK_BOMB) {
        g->justExploded = true;
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                int r = row + dr, c = col + dc;
                if ((dr || dc) && r >= 0 && r < BRICK_ROWS && c >= 0 && c < BRICK_COLS) BreakBrick(g, c, r);
            }
        }
    }
}

static void DamageBrick(Game *g, int col, int row, bool fire) {
    g->justBrickHit = true;
    if (g->brick[row][col] == BRICK_STEEL) return;
    if (fire || g->hp[row][col] <= 1) BreakBrick(g, col, row);
    else g->hp[row][col]--;
}

/* Finds the brick the ball overlaps most and bounces off it (or, with the
 * fire powerup, burns through it). At most one brick per step. */
static void CollideBricks(Game *g, Ball *b) {
    int c0 = (int)floorf((b->x - BALL_RADIUS) / BRICK_W), c1 = (int)floorf((b->x + BALL_RADIUS) / BRICK_W);
    int r0 = (int)floorf((b->y - BALL_RADIUS - BRICK_TOP) / BRICK_H), r1 = (int)floorf((b->y + BALL_RADIUS - BRICK_TOP) / BRICK_H);

    int bestCol = -1, bestRow = -1;
    float bestDist = 1e9f, bestNx = 0.0f, bestNy = 0.0f;

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (r < 0 || r >= BRICK_ROWS || c < 0 || c >= BRICK_COLS) continue;
            if (g->brick[r][c] == BRICK_NONE) continue;

            float left = (float)(c * BRICK_W), top = (float)(BRICK_TOP + r * BRICK_H);
            float nearX = b->x < left ? left : (b->x > left + BRICK_W ? left + BRICK_W : b->x);
            float nearY = b->y < top ? top : (b->y > top + BRICK_H ? top + BRICK_H : b->y);
            float dx = b->x - nearX, dy = b->y - nearY;
            float dist2 = dx * dx + dy * dy;
            if (dist2 > BALL_RADIUS * BALL_RADIUS) continue;
            if (dist2 < bestDist) {
                bestDist = dist2;
                bestCol = c;
                bestRow = r;
                bestNx = dx;
                bestNy = dy;
            }
        }
    }
    if (bestCol < 0) return;

    bool fire = g->fireTimer > 0.0f;
    bool steel = g->brick[bestRow][bestCol] == BRICK_STEEL;
    if (!fire || steel) {
        float left = (float)(bestCol * BRICK_W), top = (float)(BRICK_TOP + bestRow * BRICK_H);
        if (bestNx == 0.0f && bestNy == 0.0f) {
            /* Centre is inside the brick: leave by the shallowest side. */
            float toLeft = b->x - left, toRight = left + BRICK_W - b->x;
            float toTop = b->y - top, toBottom = top + BRICK_H - b->y;
            float m = toLeft;
            bestNx = -1.0f; bestNy = 0.0f;
            if (toRight < m) { m = toRight; bestNx = 1.0f; bestNy = 0.0f; }
            if (toTop < m) { m = toTop; bestNx = 0.0f; bestNy = -1.0f; }
            if (toBottom < m) { bestNx = 0.0f; bestNy = 1.0f; }
        }
        if (fabsf(bestNx) > fabsf(bestNy)) {
            b->vx = bestNx > 0.0f ? fabsf(b->vx) : -fabsf(b->vx);
            b->x = bestNx > 0.0f ? left + BRICK_W + BALL_RADIUS : left - BALL_RADIUS;
        } else {
            b->vy = bestNy > 0.0f ? fabsf(b->vy) : -fabsf(b->vy);
            b->y = bestNy > 0.0f ? top + BRICK_H + BALL_RADIUS : top - BALL_RADIUS;
        }
        NormalizeVelocity(b, Game_EffectiveSpeed(g));
    }

    DamageBrick(g, bestCol, bestRow, fire);
}

static void StepBall(Game *g, Ball *b) {
    if (b->stuck) {
        b->x = g->paddleX + b->stuckOffset;
        b->y = PADDLE_Y - BALL_RADIUS;
        if (g->launchHeld) {
            b->stuck = false;
            /* Straight up is dull and a little dangerous; lean with where it
             * sat on the paddle, or gently right if it sat dead centre. */
            float lean = b->stuckOffset / (g->paddleW / 2.0f) * 35.0f;
            if (fabsf(lean) < 8.0f) lean = 12.0f;
            SetBallVelocity(b, Game_EffectiveSpeed(g), lean);
            g->justLaunched = true;
        }
        return;
    }

    NormalizeVelocity(b, Game_EffectiveSpeed(g)); /* picks up slow starting/ending */
    b->x += b->vx * STEP_DT;
    b->y += b->vy * STEP_DT;

    if (b->x < BALL_RADIUS) { b->x = BALL_RADIUS; b->vx = fabsf(b->vx); g->justWallHit = true; }
    if (b->x > FIELD_W - BALL_RADIUS) { b->x = FIELD_W - BALL_RADIUS; b->vx = -fabsf(b->vx); g->justWallHit = true; }
    if (b->y < BALL_RADIUS) { b->y = BALL_RADIUS; b->vy = fabsf(b->vy); g->justWallHit = true; }

    if (g->shield && b->vy > 0.0f && b->y + BALL_RADIUS >= SHIELD_Y) {
        b->y = SHIELD_Y - BALL_RADIUS;
        b->vy = -fabsf(b->vy);
        g->shield = false;
        g->justShieldHit = true;
    }

    if (b->y - BALL_RADIUS > FIELD_H) {
        b->active = false;
        return;
    }

    /* Paddle: only on the way down, and only if the ball is still above the
     * paddle's underside -- one that slipped past the edge is gone. */
    float half = g->paddleW / 2.0f;
    if (b->vy > 0.0f && b->y + BALL_RADIUS >= PADDLE_Y && b->y < PADDLE_Y + PADDLE_H &&
        b->x >= g->paddleX - half - BALL_RADIUS && b->x <= g->paddleX + half + BALL_RADIUS) {
        float offset = (b->x - g->paddleX) / half;
        if (offset < -1.0f) offset = -1.0f;
        if (offset > 1.0f) offset = 1.0f;

        g->speed += BALL_SPEED_PER_HIT;
        if (g->speed > BALL_SPEED_CAP) g->speed = BALL_SPEED_CAP;
        g->combo = 0;
        b->y = PADDLE_Y - BALL_RADIUS;
        g->justPaddleHit = true;
        if (g->stickyTimer > 0.0f) {
            /* Caught: it rides the paddle where it landed until you launch it. */
            b->stuck = true;
            b->stuckOffset = b->x - g->paddleX;
            b->vx = b->vy = 0.0f;
            g->justCatch = true;
            return;
        }
        SetBallVelocity(b, Game_EffectiveSpeed(g), offset * MAX_PADDLE_ANGLE_DEG);
        return;
    }

    CollideBricks(g, b);
}

static void ApplyPowerup(Game *g, PowerupType type) {
    g->justPowerup = (int)type;
    g->score += 25;
    if (g->score > g->highScore) g->highScore = g->score;

    switch (type) {
        case PU_WIDE:
            g->wideTimer = WIDE_SECONDS;
            g->paddleW = PADDLE_W_WIDE;
            break;
        case PU_SLOW: g->slowTimer = SLOW_SECONDS; break;
        case PU_FIRE: g->fireTimer = FIRE_SECONDS; break;
        case PU_LIFE:
            if (g->lives < MAX_LIVES) g->lives++;
            break;
        case PU_LASER: g->laserTimer = LASER_SECONDS; break;
        case PU_STICKY: g->stickyTimer = STICKY_SECONDS; break;
        case PU_SHIELD: g->shield = true; break;
        case PU_MULTI: {
            /* Snapshot first: the new balls must not split again this step. */
            bool wasMoving[MAX_BALLS];
            for (int i = 0; i < MAX_BALLS; i++) wasMoving[i] = g->balls[i].active && !g->balls[i].stuck;
            for (int i = 0; i < MAX_BALLS; i++) {
                if (!wasMoving[i]) continue;
                for (int k = 0; k < 2; k++) {
                    int slot = -1;
                    for (int j = 0; j < MAX_BALLS; j++) if (!g->balls[j].active) { slot = j; break; }
                    if (slot < 0) break;
                    float a = (k == 0 ? 24.0f : -24.0f) * DEG2RAD;
                    Ball nb = g->balls[i];
                    nb.vx = g->balls[i].vx * cosf(a) - g->balls[i].vy * sinf(a);
                    nb.vy = g->balls[i].vx * sinf(a) + g->balls[i].vy * cosf(a);
                    NormalizeVelocity(&nb, Game_EffectiveSpeed(g));
                    g->balls[slot] = nb;
                }
            }
            break;
        }
        default: break;
    }
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    if (g->wideTimer > 0.0f && (g->wideTimer -= STEP_DT) <= 0.0f) {
        g->wideTimer = 0.0f;
        g->paddleW = PADDLE_W_NORMAL;
    }
    if (g->slowTimer > 0.0f && (g->slowTimer -= STEP_DT) <= 0.0f) g->slowTimer = 0.0f;
    if (g->fireTimer > 0.0f && (g->fireTimer -= STEP_DT) <= 0.0f) g->fireTimer = 0.0f;
    if (g->laserTimer > 0.0f && (g->laserTimer -= STEP_DT) <= 0.0f) g->laserTimer = 0.0f;
    if (g->stickyTimer > 0.0f && (g->stickyTimer -= STEP_DT) <= 0.0f) g->stickyTimer = 0.0f;
    if (g->shotCooldown > 0.0f) g->shotCooldown -= STEP_DT;

    g->paddleX += g->moveDir * PADDLE_SPEED * STEP_DT;
    float half = g->paddleW / 2.0f;
    if (g->paddleX < half) g->paddleX = half;
    if (g->paddleX > FIELD_W - half) g->paddleX = FIELD_W - half;

    /* Lasers: two bolts from the paddle's edges while you hold launch (and nothing is waiting to be launched). */
    bool waiting = false;
    for (int i = 0; i < MAX_BALLS; i++) if (g->balls[i].active && g->balls[i].stuck) waiting = true;
    if (g->laserTimer > 0.0f && g->launchHeld && !waiting && g->shotCooldown <= 0.0f) {
        int placed = 0;
        for (int i = 0; i < MAX_SHOTS && placed < 2; i++) {
            if (g->shots[i].active) continue;
            g->shots[i] = (Shot){g->paddleX + (placed == 0 ? -half + 6.0f : half - 6.0f), PADDLE_Y - 4.0f, true};
            placed++;
        }
        if (placed) { g->shotCooldown = SHOT_INTERVAL; g->justShot = true; }
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &g->shots[i];
        if (!s->active) continue;
        s->y -= SHOT_SPEED * STEP_DT;
        if (s->y < 0.0f) { s->active = false; continue; }
        int col = (int)(s->x / BRICK_W), row = (int)((s->y - BRICK_TOP) / BRICK_H);
        if (s->y >= BRICK_TOP && col >= 0 && col < BRICK_COLS && row >= 0 && row < BRICK_ROWS && g->brick[row][col] != BRICK_NONE) {
            DamageBrick(g, col, row, false);
            s->active = false;
        }
    }

    for (int i = 0; i < MAX_BALLS; i++) {
        if (g->balls[i].active) StepBall(g, &g->balls[i]);
    }

    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *p = &g->powerups[i];
        if (!p->active) continue;
        p->y += POWERUP_FALL_SPEED * STEP_DT;
        if (p->y - POWERUP_H / 2 > FIELD_H) {
            p->active = false;
        } else if (p->y + POWERUP_H / 2 >= PADDLE_Y && p->y - POWERUP_H / 2 <= PADDLE_Y + PADDLE_H &&
                   p->x + POWERUP_W / 2 >= g->paddleX - half && p->x - POWERUP_W / 2 <= g->paddleX + half) {
            p->active = false;
            ApplyPowerup(g, p->type);
        }
    }

    if (g->bricksLeft <= 0) {
        g->justLevelClear = true;
        g->score += 100L * g->level;
        if (g->score > g->highScore) g->highScore = g->score;
        Game_StartLevel(g, g->level + 1);
        return;
    }

    if (Game_BallsInPlay(g) == 0) {
        g->lives--;
        g->justLifeLost = true;
        if (g->lives <= 0) {
            g->lives = 0;
            if (g->score > g->highScore) g->highScore = g->score;
            g->phase = GS_GAMEOVER;
            g->justGameOver = true;
            return;
        }
        ClearPickupsAndTimers(g);
        ParkBallOnPaddle(g);
    }
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f; /* a long hitch shouldn't fast-forward the ball past the paddle */

    g->stepAccumulator += dt;
    while (g->stepAccumulator >= STEP_DT && g->phase == GS_PLAYING) {
        g->stepAccumulator -= STEP_DT;
        Game_Step(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justPaddleHit = g->justWallHit = g->justBrickHit = g->justExploded = false;
    g->justLaunched = g->justLifeLost = g->justLevelClear = g->justGameOver = false;
    g->justShot = g->justShieldHit = g->justCatch = false;
    g->justPowerup = -1;
    g->brokenCount = 0;
}
