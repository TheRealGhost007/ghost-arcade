#include "game.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */

static bool InBounds(int x, int y) { return x >= 0 && x < COLS && y >= 0 && y < ROWS; }

static int CrawlerAt(const Game *g, int x, int y) {
    for (int i = 0; i < g->crawlerCount; i++) if (g->crawlers[i].alive && g->crawlers[i].x == x && g->crawlers[i].y == y) return i;
    return -1;
}

static bool PlayerAt(const Game *g, int x, int y) { return g->playerAlive && g->px == x && g->py == y; }

/* Empty for a rock to fall or roll into: nothing solid, and nobody standing there. */
static bool ClearFor(const Game *g, int x, int y) {
    return InBounds(x, y) && g->cell[y][x] == C_EMPTY && !PlayerAt(g, x, y) && CrawlerAt(g, x, y) < 0;
}

static bool Rounded(uint8_t c) { return c == C_BOULDER || c == C_GEM || c == C_BRICK; }

float Game_TickSeconds(const Game *g) {
    float t = TICK_SECONDS * powf(0.9f, (float)g->round);
    return t < 0.06f ? 0.06f : t;
}

bool Game_IsCleared(const Game *g) { return g->state == ST_CLEAR; }

static void AddScore(Game *g, long points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    while (g->score >= g->nextExtra) {
        g->nextExtra += EXTRA_LIFE_EVERY;
        if (g->lives < MAX_LIVES) g->lives++;
        g->justExtraLife = true;
    }
}

/* ------------------------------------------------------------- explosions */

static void KillPlayer(Game *g, DeathCause cause);

/* Stamps a 3x3 blast: everything but steel becomes empty (or gems), any
 * crawler caught in it blows up in turn, and so does the player. */
static void Explode(Game *g, int cx, int cy, int gemKind) {
    if (g->blastCount < MAX_BLASTS) g->blasts[g->blastCount++] = (Blast){cx, cy, gemKind};
    g->justExplode = true;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = cx + dx, y = cy + dy;
            if (!InBounds(x, y) || g->cell[y][x] == C_STEEL || g->cell[y][x] == C_EXIT) continue;
            g->cell[y][x] = gemKind ? C_GEM : C_EMPTY;
            g->falling[y][x] = 0;
            g->moved[y][x] = 1;
        }
    }
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = cx + dx, y = cy + dy;
            if (!InBounds(x, y)) continue;
            int c = CrawlerAt(g, x, y);
            if (c >= 0) {
                g->crawlers[c].alive = false;
                Explode(g, x, y, g->crawlers[c].kind);
            }
            if (PlayerAt(g, x, y)) KillPlayer(g, DEATH_BLAST);
        }
    }
}

static void KillPlayer(Game *g, DeathCause cause) {
    if (!g->playerAlive) return;
    g->playerAlive = false;
    g->death = cause;
    g->state = ST_DYING;
    g->stateTimer = DYING_SECONDS;
    g->justDeath = true;
    /* Whatever killed you, you go up in a blast too. */
    int px = g->px, py = g->py;
    Explode(g, px, py, 0);
}

/* ------------------------------------------------------------------ setup */

static void LoadCave(Game *g, const Cave *cave) {
    g->cave = cave;
    memset(g->cell, 0, sizeof(g->cell));
    memset(g->falling, 0, sizeof(g->falling));
    memset(g->moved, 0, sizeof(g->moved));
    g->crawlerCount = 0;
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            switch (cave->map[y][x]) {
                case '#': g->cell[y][x] = C_STEEL; break;
                case 'B': g->cell[y][x] = C_BRICK; break;
                case ':': g->cell[y][x] = C_DIRT; break;
                case 'o': g->cell[y][x] = C_BOULDER; break;
                case '*': g->cell[y][x] = C_GEM; break;
                case 'X': g->cell[y][x] = C_EXIT; break;
                case 'P': g->px = x; g->py = y; break;
                case 'f': case 'g':
                    if (g->crawlerCount < MAX_CRAWLERS) g->crawlers[g->crawlerCount++] = (Crawler){x, y, 0, cave->map[y][x] == 'g', true};
                    break;
                default: break;
            }
        }
    }
    g->quota = cave->quota;
    g->gems = 0;
    g->exitOpen = cave->quota <= 0;
    g->timeLimit = g->timeLeft = (float)cave->timeLimit;
    g->playerAlive = true;
    g->death = DEATH_NONE;
    g->facing = 1;
    g->tickCount = 0;
    g->heldDx = g->heldDy = g->pendingDx = g->pendingDy = 0;
    g->tickAccumulator = 0.0f;
    g->blastCount = 0;
}

void Game_StartLevel(Game *g, int level) {
    if (level < 1) level = 1;
    g->level = level;
    g->round = (level - 1) / (g->setCount > 0 ? g->setCount : 1);
    LoadCave(g, &g->set[(level - 1) % g->setCount]);
    g->state = ST_INTRO;
    g->stateTimer = INTRO_SECONDS;
    g->justNewLevel = true;
}

void Game_Init(Game *g, const Cave *set, int setCount, long highScore) {
    memset(g, 0, sizeof(*g));
    g->set = set;
    g->setCount = setCount;
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->nextExtra = EXTRA_LIFE_EVERY;
    Game_StartLevel(g, 1);
}

void Game_Restart(Game *g, long highScore) {
    long best = g->highScore > g->score ? g->highScore : g->score;
    if (highScore > best) best = highScore;
    Game_Init(g, g->set, g->setCount, best);
}

void Game_SetInput(Game *g, int dx, int dy) {
    if (dx != 0) dy = 0; /* one axis at a time: horizontal wins */
    g->heldDx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
    g->heldDy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
    if (g->heldDx || g->heldDy) { g->pendingDx = g->heldDx; g->pendingDy = g->heldDy; }
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

/* ----------------------------------------------------------------- player */

static void MovePlayer(Game *g, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    int nx = g->px + dx, ny = g->py + dy;
    if (!InBounds(nx, ny)) return;
    if (dx != 0) g->facing = dx;

    switch (g->cell[ny][nx]) {
        case C_EMPTY:
            break;
        case C_DIRT:
            g->cell[ny][nx] = C_EMPTY;
            g->justDig = true;
            break;
        case C_GEM: {
            g->cell[ny][nx] = C_EMPTY;
            g->falling[ny][nx] = 0;
            g->gems++;
            AddScore(g, g->exitOpen ? GEM_POINTS_OPEN : GEM_POINTS);
            g->justGem = true;
            if (!g->exitOpen && g->gems >= g->quota) { g->exitOpen = true; g->justExitOpen = true; }
            break;
        }
        case C_BOULDER:
            /* Boulders can be pushed sideways onto empty ground, never up or down. */
            if (dy != 0 || !ClearFor(g, nx + dx, ny)) return;
            g->cell[ny][nx + dx] = C_BOULDER;
            g->cell[ny][nx] = C_EMPTY;
            g->falling[ny][nx] = 0;
            g->falling[ny][nx + dx] = 0;
            g->justPush = true;
            break;
        case C_EXIT:
            if (!g->exitOpen) return;
            g->px = nx; g->py = ny;
            g->state = ST_CLEAR;
            g->stateTimer = CLEAR_SECONDS;
            g->lastClearBonus = CLEAR_BONUS + (long)ceilf(g->timeLeft) * TIME_BONUS_PER_SECOND;
            AddScore(g, g->lastClearBonus);
            g->justClear = true;
            return;
        default:
            return; /* steel and brick */
    }
    g->px = nx;
    g->py = ny;
    g->justStep = true;
    if (CrawlerAt(g, nx, ny) >= 0) KillPlayer(g, DEATH_CRAWLER);
}

/* --------------------------------------------------------------- the rocks */

static void SettleRocks(Game *g) {
    memset(g->moved, 0, sizeof(g->moved));
    for (int y = 0; y < ROWS - 1; y++) {
        for (int x = 0; x < COLS; x++) {
            uint8_t c = g->cell[y][x];
            if ((c != C_BOULDER && c != C_GEM) || g->moved[y][x]) continue;

            if (ClearFor(g, x, y + 1)) {
                g->cell[y + 1][x] = c;
                g->cell[y][x] = C_EMPTY;
                g->falling[y + 1][x] = 1;
                g->falling[y][x] = 0;
                g->moved[y + 1][x] = 1;
                continue;
            }
            if (g->falling[y][x]) {
                /* Already falling when it reaches someone: that's a crush. */
                if (PlayerAt(g, x, y + 1)) { KillPlayer(g, DEATH_CRUSHED); continue; }
                int cr = CrawlerAt(g, x, y + 1);
                if (cr >= 0) {
                    g->crawlers[cr].alive = false;
                    Explode(g, x, y + 1, g->crawlers[cr].kind);
                    continue;
                }
            }
            if (Rounded(g->cell[y + 1][x])) {
                int side = 0;
                if (ClearFor(g, x - 1, y) && ClearFor(g, x - 1, y + 1)) side = -1;
                else if (ClearFor(g, x + 1, y) && ClearFor(g, x + 1, y + 1)) side = 1;
                if (side) {
                    g->cell[y][x + side] = c;
                    g->cell[y][x] = C_EMPTY;
                    g->falling[y][x + side] = 1;
                    g->falling[y][x] = 0;
                    g->moved[y][x + side] = 1;
                    continue;
                }
            }
            if (g->falling[y][x]) { g->falling[y][x] = 0; g->justRockLand = true; }
        }
    }
}

/* ---------------------------------------------------------------- crawlers */

static bool CrawlerCanEnter(const Game *g, int x, int y) {
    return InBounds(x, y) && g->cell[y][x] == C_EMPTY && CrawlerAt(g, x, y) < 0;
}

static void StepCrawlers(Game *g) {
    static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
    for (int i = 0; i < g->crawlerCount; i++) {
        Crawler *c = &g->crawlers[i];
        if (!c->alive) continue;
        int left = (c->dir + 3) & 3;
        int lx = c->x + kDx[left], ly = c->y + kDy[left];
        int fx = c->x + kDx[c->dir], fy = c->y + kDy[c->dir];
        if (CrawlerCanEnter(g, lx, ly) || PlayerAt(g, lx, ly)) { c->dir = left; c->x = lx; c->y = ly; }
        else if (CrawlerCanEnter(g, fx, fy) || PlayerAt(g, fx, fy)) { c->x = fx; c->y = fy; }
        else c->dir = (c->dir + 1) & 3;
        if (PlayerAt(g, c->x, c->y)) KillPlayer(g, DEATH_CRAWLER);
    }
}

/* ------------------------------------------------------------------- tick */

void Game_Tick(Game *g) {
    if (g->phase != GS_PLAYING || g->state != ST_PLAY) return;
    g->tickCount++;

    float before = g->timeLeft;
    g->timeLeft -= TICK_SECONDS;
    if (before > 20.0f && g->timeLeft <= 20.0f) g->justTimeWarning = true;
    if (g->timeLeft <= 0.0f) { g->timeLeft = 0.0f; KillPlayer(g, DEATH_TIME); return; }

    int dx = g->heldDx, dy = g->heldDy;
    if (dx == 0 && dy == 0) { dx = g->pendingDx; dy = g->pendingDy; }
    g->pendingDx = g->pendingDy = 0;
    MovePlayer(g, dx, dy);
    if (g->state != ST_PLAY) return;

    SettleRocks(g);
    if (g->state != ST_PLAY) return;
    StepCrawlers(g);
}

void Game_TickWithMove(Game *g, char move) {
    int dx = 0, dy = 0;
    if (move == 'L') dx = -1;
    else if (move == 'R') dx = 1;
    else if (move == 'U') dy = -1;
    else if (move == 'D') dy = 1;
    g->heldDx = dx; g->heldDy = dy;
    g->pendingDx = g->pendingDy = 0;
    Game_Tick(g);
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.1f) dt = 0.1f;

    switch (g->state) {
        case ST_INTRO:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) g->state = ST_PLAY;
            return;
        case ST_DYING:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) {
                g->lives--;
                if (g->lives <= 0) {
                    g->lives = 0;
                    g->phase = GS_GAMEOVER;
                    g->justGameOver = true;
                } else {
                    LoadCave(g, g->cave);
                    g->state = ST_INTRO;
                    g->stateTimer = INTRO_SECONDS;
                }
            }
            return;
        case ST_CLEAR:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) Game_StartLevel(g, g->level + 1);
            return;
        case ST_PLAY:
            break;
    }

    g->tickAccumulator += dt;
    float tick = Game_TickSeconds(g);
    while (g->tickAccumulator >= tick && g->state == ST_PLAY && g->phase == GS_PLAYING) {
        g->tickAccumulator -= tick;
        Game_Tick(g);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justGem = g->justDig = g->justPush = g->justRockLand = g->justExplode = g->justExitOpen = g->justStep = false;
    g->justDeath = g->justClear = g->justGameOver = g->justExtraLife = g->justTimeWarning = g->justNewLevel = false;
    g->blastCount = 0;
}
