#include "game.h"
#include <string.h>

/* ---------------------------------------------------------------- geometry */

static bool InMap(int x, int y) { return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H; }
static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};

int Game_HostAt(const Game *g, int x, int y) {
    for (int i = 0; i < g->hostCount; i++) if (g->hosts[i].present && g->hosts[i].x == x && g->hosts[i].y == y) return i;
    return -1;
}

int Game_CrateAt(const Game *g, int x, int y) {
    for (int i = 0; i < g->crateCount; i++) if (g->crates[i].present && g->crates[i].x == x && g->crates[i].y == y) return i;
    return -1;
}

static int PriestAt(const Game *g, int x, int y) {
    for (int i = 0; i < g->priestCount; i++) if (g->priests[i].x == x && g->priests[i].y == y) return i;
    return -1;
}

bool Game_PlateHeld(const Game *g, int x, int y) {
    return Game_HostAt(g, x, y) >= 0 || Game_CrateAt(g, x, y) >= 0;
}

/* Gates are open while every plate has something on it. */
bool Game_GateOpen(const Game *g) {
    int plates = 0;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        if (g->tile[y][x] != T_PLATE) continue;
        plates++;
        if (!Game_PlateHeld(g, x, y)) return false;
    }
    return plates > 0;
}

bool Game_TileWalkable(const Game *g, int who, int x, int y) {
    if (!InMap(x, y)) return false;
    Tile t = (Tile)g->tile[y][x];
    if (t == T_GATE) return Game_GateOpen(g);
    if (t == T_FLOOR || t == T_PLATE) return true;
    if (who < 0) return t == T_GRATE || t == T_EXIT;
    HostType type = g->hosts[who].type;
    if (type == HOST_CAT) return t == T_GRATE || t == T_FLAP;
    return false;
}

/* Things that stop a priest's line of sight. */
static bool Opaque(const Game *g, int x, int y) {
    if (!InMap(x, y)) return true;
    Tile t = (Tile)g->tile[y][x];
    if (t == T_WALL || t == T_DOOR || t == T_CRACK) return true;
    if (t == T_GATE && !Game_GateOpen(g)) return true;
    return Game_HostAt(g, x, y) >= 0 || Game_CrateAt(g, x, y) >= 0;
}

bool Game_SeenBy(const Game *g, int x, int y, int *by) {
    for (int i = 0; i < g->priestCount; i++) {
        const Priest *p = &g->priests[i];
        for (int k = 1; k <= SIGHT_RANGE; k++) {
            int sx = p->x + kDx[p->dir] * k, sy = p->y + kDy[p->dir] * k;
            if (!InMap(sx, sy)) break;
            if (sx == x && sy == y) { if (by) *by = i; return true; }
            if (Opaque(g, sx, sy)) break;
        }
    }
    return false;
}

bool Game_IsCleared(const Game *g) { return g->state == ST_CLEAR; }
bool Game_IsCaught(const Game *g) { return g->state == ST_DYING; }

/* ------------------------------------------------------------------ levels */

void Game_LoadLevel(Game *g, int level) {
    if (level < 1) level = 1;
    g->level = level;
    const LevelDef *d = &kLevels[(level - 1) % kLevelCount];
    memset(g->tile, T_WALL, sizeof(g->tile));
    g->hostCount = g->crateCount = g->keyCount = g->priestCount = 0;
    for (int y = 0; y < MAP_H; y++) {
        const char *row = d->rows[y];
        for (int x = 0; x < MAP_W; x++) {
            char c = (row && x < (int)strlen(row)) ? row[x] : '#';
            Tile t = T_FLOOR;
            switch (c) {
                case '#': t = T_WALL; break;
                case ':': t = T_GRATE; break;
                case 'f': t = T_FLAP; break;
                case 'D': t = T_DOOR; break;
                case 'C': t = T_CRACK; break;
                case '_': t = T_PLATE; break;
                case 'G': t = T_GATE; break;
                case 'X': t = T_EXIT; break;
                case '@': g->gx = x; g->gy = y; break;
                case 'c': case 'a': case 's':
                    if (g->hostCount < MAX_HOSTS) g->hosts[g->hostCount++] = (Host){x, y, c == 'c' ? HOST_CAT : (c == 'a' ? HOST_ARMOR : HOST_SERVANT), true, false};
                    break;
                case 'k': if (g->keyCount < MAX_KEYS) g->keys[g->keyCount++] = (Key){x, y, false}; break;
                case 'b': if (g->crateCount < MAX_CRATES) g->crates[g->crateCount++] = (Crate){x, y, true}; break;
                default: break;
            }
            g->tile[y][x] = (uint8_t)t;
        }
    }
    for (int i = 0; i < d->priestCount && i < MAX_PRIESTS; i++) {
        const int *p = d->patrol[i];
        Priest pr = {p[0], p[1], 0, p[0], p[1], p[2], p[3], 0, 0, p[4] > 0 ? p[4] : 2};
        int dx = (p[2] > p[0]) - (p[2] < p[0]), dy = (p[3] > p[1]) - (p[3] < p[1]);
        pr.dir = dx > 0 ? 1 : (dx < 0 ? 3 : (dy > 0 ? 2 : 0));
        g->priests[g->priestCount++] = pr;
    }
    g->possessed = -1;
    g->possessCooldown = 0;
    g->ticks = 0;
    g->par = d->par;
    g->caughtBy = CAUGHT_NONE;
    g->queued = ACT_WAIT;
    g->tickAccumulator = 0.0f;
}

void Game_StartLevel(Game *g, int level, bool intro) {
    Game_LoadLevel(g, level);
    g->state = intro ? ST_INTRO : ST_PLAY;
    g->stateTimer = INTRO_SECONDS;
    g->justNewLevel = true;
}

void Game_Init(Game *g, long highScore) {
    memset(g, 0, sizeof(*g));
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->lives = START_LIVES;
    g->nextExtra = 5000;
    Game_StartLevel(g, 1, true);
}

void Game_Restart(Game *g, long highScore) {
    long best = g->highScore > g->score ? g->highScore : g->score;
    if (highScore > best) best = highScore;
    Game_Init(g, best);
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

void Game_Queue(Game *g, Action a) { if (a != ACT_WAIT) g->queued = a; }

static void AddScore(Game *g, long points) {
    g->score += points;
    if (g->score > g->highScore) g->highScore = g->score;
    while (g->score >= g->nextExtra) {
        g->nextExtra += 5000;
        if (g->lives < 6) g->lives++;
        g->justExtraLife = true;
    }
}

/* ----------------------------------------------------------------- acting */

static void Caught(Game *g, CaughtBy how) {
    if (g->state != ST_PLAY) return;
    g->caughtBy = how;
    g->state = ST_DYING;
    g->stateTimer = DYING_SECONDS;
    g->justCaught = true;
}

static void DoPossess(Game *g) {
    if (g->possessed >= 0) {
        g->possessed = -1;
        g->possessCooldown = 1;
        g->justRelease = true;
        return;
    }
    if (g->possessCooldown > 0) { g->justBlocked = true; return; }
    int best = -1;
    for (int i = 0; i < g->hostCount; i++) {
        if (!g->hosts[i].present) continue;
        int d = (g->hosts[i].x > g->gx ? g->hosts[i].x - g->gx : g->gx - g->hosts[i].x) + (g->hosts[i].y > g->gy ? g->hosts[i].y - g->gy : g->gy - g->hosts[i].y);
        if (d <= 1 && (best < 0 || d == 0)) best = i;
    }
    if (best < 0) { g->justBlocked = true; return; }
    g->possessed = best;
    g->gx = g->hosts[best].x;
    g->gy = g->hosts[best].y;
    g->justPossess = true;
}

/* Is (x, y) free for a crate to be pushed onto? */
static bool CrateCanEnter(const Game *g, int x, int y) {
    if (!InMap(x, y)) return false;
    Tile t = (Tile)g->tile[y][x];
    if (!(t == T_FLOOR || t == T_PLATE || (t == T_GATE && Game_GateOpen(g)))) return false;
    return Game_HostAt(g, x, y) < 0 && Game_CrateAt(g, x, y) < 0 && PriestAt(g, x, y) < 0;
}

static void DoMove(Game *g, int dir) {
    int dx = kDx[dir], dy = kDy[dir];
    int nx = g->gx + dx, ny = g->gy + dy;
    if (!InMap(nx, ny)) { g->justBlocked = true; return; }

    if (g->possessed < 0) {
        if (Game_TileWalkable(g, -1, nx, ny) && Game_HostAt(g, nx, ny) < 0 && Game_CrateAt(g, nx, ny) < 0) {
            g->gx = nx; g->gy = ny; g->justStep = true;
        } else g->justBlocked = true;
        return;
    }

    Host *h = &g->hosts[g->possessed];
    /* Armor is heavy: it only manages a move every other tick. */
    if (h->type == HOST_ARMOR && (g->ticks & 1)) return;

    int c = Game_CrateAt(g, nx, ny);
    if (c >= 0) {
        if (h->type == HOST_ARMOR && CrateCanEnter(g, nx + dx, ny + dy)) {
            g->crates[c].x = nx + dx; g->crates[c].y = ny + dy;
            h->x = nx; h->y = ny;
            g->justPush = true;
        } else { g->justBlocked = true; return; }
    } else if (Game_HostAt(g, nx, ny) >= 0) {
        g->justBlocked = true; return;
    } else if (g->tile[ny][nx] == T_CRACK && h->type == HOST_ARMOR) {
        g->tile[ny][nx] = T_FLOOR;
        g->justSmash = true;
        return;
    } else if (g->tile[ny][nx] == T_DOOR && h->type == HOST_SERVANT && h->hasKey) {
        g->tile[ny][nx] = T_FLOOR;
        h->hasKey = false;
        h->x = nx; h->y = ny;
        g->justDoor = true;
    } else if (Game_TileWalkable(g, g->possessed, nx, ny)) {
        h->x = nx; h->y = ny;
        g->justStep = true;
    } else { g->justBlocked = true; return; }

    if (h->type == HOST_SERVANT && !h->hasKey) {
        for (int k = 0; k < g->keyCount; k++) if (!g->keys[k].taken && g->keys[k].x == h->x && g->keys[k].y == h->y) {
            g->keys[k].taken = true; h->hasKey = true; g->justKey = true;
        }
    }
    g->gx = h->x; g->gy = h->y;
}

/* ---------------------------------------------------------------- priests */

static bool PriestCanEnter(const Game *g, int x, int y) {
    if (!InMap(x, y)) return false;
    Tile t = (Tile)g->tile[y][x];
    if (!(t == T_FLOOR || t == T_PLATE || (t == T_GATE && Game_GateOpen(g)))) return false;
    return Game_CrateAt(g, x, y) < 0 && !(Game_HostAt(g, x, y) >= 0 && (g->possessed < 0 || Game_HostAt(g, x, y) != g->possessed));
}

static void StepPriests(Game *g) {
    for (int i = 0; i < g->priestCount; i++) {
        Priest *p = &g->priests[i];
        if (p->stride > 1 && (g->ticks % p->stride) != 0) continue;
        if (p->wait > 0) { p->wait--; continue; }
        int tx = p->toward ? p->ax : p->bx, ty = p->toward ? p->ay : p->by;
        if (p->x == tx && p->y == ty) { p->toward = !p->toward; p->wait = 1; g->justBell = true; continue; }
        int dx = (tx > p->x) - (tx < p->x), dy = (ty > p->y) - (ty < p->y);
        if (dx != 0) dy = 0;
        p->dir = dx > 0 ? 1 : (dx < 0 ? 3 : (dy > 0 ? 2 : 0));
        if (PriestCanEnter(g, p->x + dx, p->y + dy)) { p->x += dx; p->y += dy; }
        if (p->x == tx && p->y == ty) { p->toward = !p->toward; p->wait = 1; g->justBell = true; }
    }
}

/* ------------------------------------------------------------------- tick */

void Game_Tick(Game *g, Action a) {
    if (g->phase != GS_PLAYING || g->state != ST_PLAY) return;
    g->ticks++;
    if (g->possessCooldown > 0 && a != ACT_POSSESS) g->possessCooldown--;

    switch (a) {
        case ACT_UP: DoMove(g, 0); break;
        case ACT_RIGHT: DoMove(g, 1); break;
        case ACT_DOWN: DoMove(g, 2); break;
        case ACT_LEFT: DoMove(g, 3); break;
        case ACT_POSSESS: DoPossess(g); break;
        default: break;
    }

    bool gateWas = Game_GateOpen(g);
    StepPriests(g);
    bool gateNow = Game_GateOpen(g);
    if (gateNow != gateWas) g->justGate = true;

    /* Caught: walking into a priest, or a priest into you; or, if you are only a ghost, being seen. */
    int by = -1;
    if (PriestAt(g, g->gx, g->gy) >= 0) { Caught(g, g->possessed >= 0 ? CAUGHT_EXORCISED : CAUGHT_TOUCHED); return; }
    if (g->possessed < 0 && Game_SeenBy(g, g->gx, g->gy, &by)) { Caught(g, CAUGHT_SEEN); return; }

    if (g->possessed < 0 && g->tile[g->gy][g->gx] == T_EXIT) {
        g->state = ST_CLEAR;
        g->stateTimer = CLEAR_SECONDS;
        long bonus = SCORE_CLEAR;
        if (g->par > 0) {
            if (g->ticks <= g->par) bonus += SCORE_PAR_BONUS;
            if (g->ticks < g->par + 30) bonus += (long)(g->par + 30 - g->ticks) * SCORE_PER_SPARE_TICK;
        }
        g->lastBonus = bonus;
        AddScore(g, bonus);
        g->justClear = true;
    }
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
                if (g->lives <= 0) { g->lives = 0; g->phase = GS_GAMEOVER; g->justGameOver = true; }
                else Game_StartLevel(g, g->level, false), g->justNewLevel = false;
            }
            return;
        case ST_CLEAR:
            g->stateTimer -= dt;
            if (g->stateTimer <= 0.0f) Game_StartLevel(g, g->level + 1, true);
            return;
        case ST_PLAY: break;
    }
    g->tickAccumulator += dt;
    while (g->tickAccumulator >= TICK_SECONDS && g->state == ST_PLAY && g->phase == GS_PLAYING) {
        g->tickAccumulator -= TICK_SECONDS;
        Action a = g->queued;
        g->queued = ACT_WAIT;
        Game_Tick(g, a);
    }
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justPossess = g->justRelease = g->justStep = g->justPush = g->justSmash = g->justKey = g->justDoor = g->justPlate = g->justGate = false;
    g->justCaught = g->justClear = g->justGameOver = g->justNewLevel = g->justBell = g->justExtraLife = g->justBlocked = false;
}
