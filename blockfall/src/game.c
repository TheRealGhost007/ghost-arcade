#include "game.h"
#include <string.h>
#include <stddef.h>

/* Aligned so every piece's topmost cell is already inside the visible
 * playfield the instant it spawns (nothing appears to fall in from above
 * the box). BOARD_HIDDEN rows remain above this purely as kick/rotation
 * headroom. */

static float ComputeFallInterval(int level) {
    float v = 1.0f - (float)(level - 1) * 0.07f;
    if (v < 0.08f) v = 0.08f;
    return v;
}

static void Bag_Refill(Bag *bag) {
    for (int i = 0; i < PIECE_COUNT; i++) bag->bag[i] = (PieceType)i;
    for (int i = PIECE_COUNT - 1; i > 0; i--) {
        int j = (int)Rng_Range(&bag->rng, (uint32_t)(i + 1));
        PieceType tmp = bag->bag[i];
        bag->bag[i] = bag->bag[j];
        bag->bag[j] = tmp;
    }
    bag->bagPos = 0;
}

static PieceType Bag_Next(Bag *bag) {
    if (bag->bagPos >= PIECE_COUNT) Bag_Refill(bag);
    return bag->bag[bag->bagPos++];
}

/* Power mode: about one piece in seven carries a power-up on one block. */
static void RollNextPower(Game *g) {
    g->nextPower = PU_NONE;
    g->nextPowerCell = 0;
    if (g->cfg.mode != MODE_POWER) return;
    if (Rng_Range(&g->bag.rng, 100) < POWER_CHANCE_PERCENT) {
        g->nextPower = (PowerUp)(1 + (int)Rng_Range(&g->bag.rng, PU_COUNT - 1));
        g->nextPowerCell = (int)Rng_Range(&g->bag.rng, CELLS_PER_PIECE);
    }
}

static void Place(Game *g) {
    g->rotation = 0;
    g->px = (g->board.w - 4) / 2;
    g->py = BOARD_HIDDEN;
    g->fallTimer = 0.0f;
    g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->grounded = false;
}

static void SpawnNext(Game *g) {
    g->current = g->next;
    g->curPower = g->nextPower;
    g->curPowerCell = g->nextPowerCell;
    g->next = Bag_Next(&g->bag);
    RollNextPower(g);
    g->holdUsed = false;
    Place(g);

    if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py)) {
        if (g->cfg.mode == MODE_ZEN) {
            /* Zen has no losing: the stack is swept away and play carries on. */
            Board_Init(&g->board, g->cfg.width, g->cfg.height);
            g->zenResets++;
            g->justZenReset = true;
            g->combo = -1;
            Place(g);
        } else {
            g->phase = GS_GAMEOVER;
        }
    }
}

static float EffectiveFallInterval(const Game *g) {
    return g->fallInterval * (g->slowTimer > 0.0f ? SLOW_FACTOR : 1.0f);
}

/* Fires the power-up carried by the block that just locked at (x, y). */
static void ApplyPower(Game *g, PowerUp p, int x, int y) {
    g->lastPower = p;
    g->lastPowerX = x;
    g->lastPowerY = y;
    g->lastPowerRemoved = 0;
    g->score += 150; /* for picking one up */
    switch (p) {
        case PU_BOMB: {
            int n = Board_ClearBlock(&g->board, x, y, 1);
            g->lastPowerRemoved = n;
            g->score += 30L * n;
            break;
        }
        case PU_LASER: {
            int n = Board_ClearColumn(&g->board, x);
            g->lastPowerRemoved = n;
            g->score += 15L * n;
            break;
        }
        case PU_FREEZE: g->freezeTimer = FREEZE_SECONDS; break;
        case PU_SLOW: g->slowTimer = SLOW_SECONDS; break;
        case PU_SWEEP: {
            int n = Board_RemoveBottomRows(&g->board, 3);
            g->lastPowerRemoved = n;
            g->score += 200;
            break;
        }
        default: break;
    }
}

static void LockAndAdvance(Game *g) {
    Board_LockPiece(&g->board, g->current, g->rotation, g->px, g->py);

    g->lastLockType = g->current;
    g->lastLockRotation = g->rotation;
    g->lastLockX = g->px;
    g->lastLockY = g->py;

    if (g->curPower != PU_NONE) {
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(g->current, g->rotation, cells);
        int idx = g->curPowerCell % CELLS_PER_PIECE;
        ApplyPower(g, g->curPower, g->px + cells[idx].x, g->py + cells[idx].y);
    }

    int cleared = Board_ClearLines(&g->board, g->lastClearRows);
    g->lastClearCount = cleared;
    g->justLocked = true;

    if (cleared > 0) {
        static const int kScoreTable[5] = {0, 100, 300, 500, 800};
        long points = (long)kScoreTable[cleared] * g->level;
        if (g->cfg.modernScoring) {
            /* a quad after a quad pays half again as much */
            if (cleared == 4 && g->backToBack) points += points / 2;
            g->backToBack = (cleared == 4);
            g->combo++;
            g->lastCombo = g->combo;
            g->lastComboBonus = g->combo > 0 ? 50L * g->combo * g->level : 0;
            points += g->lastComboBonus;
        }
        g->score += points;
        g->linesCleared += cleared;
        int newLevel = g->cfg.mode == MODE_ZEN ? 1 : 1 + g->linesCleared / LINES_PER_LEVEL;
        if (newLevel != g->level) {
            g->level = newLevel;
            g->lastWasLevelUp = true;
            g->fallInterval = ComputeFallInterval(g->level);
        }
    } else if (g->cfg.modernScoring) {
        g->combo = -1;
    }

    if (g->score > g->highScore) g->highScore = g->score;
    if (g->level > g->highLevel) g->highLevel = g->level;
    if (g->linesCleared > g->highLines) g->highLines = g->linesCleared;

    /* Sprint: forty lines and you are done, with a bonus for being quick. */
    if (g->cfg.mode == MODE_SPRINT && g->linesCleared >= SPRINT_LINES) {
        float quick = 180.0f - g->elapsed;
        if (quick > 0.0f) g->score += (long)(quick * 50.0f);
        if (g->score > g->highScore) g->highScore = g->score;
        g->won = true;
        g->phase = GS_GAMEOVER;
        return;
    }

    /* GS_GAMEOVER may be set here by SpawnNext if the new piece can't fit;
     * caller (main loop) checks g->phase afterward either way. */
    SpawnNext(g);
}

static void NotifyActionSuccess(Game *g) {
    if (g->grounded && g->lockResets < MAX_LOCK_RESETS) {
        g->lockTimer = 0.0f;
        g->lockResets++;
    }
}

GameConfig Game_DefaultConfig(void) {
    return (GameConfig){MODE_MARATHON, BOARD_W, BOARD_H, false};
}

const char *Game_ModeName(GameMode m) {
    switch (m) {
        case MODE_MARATHON: return "Marathon";
        case MODE_SPRINT: return "Sprint";
        case MODE_ULTRA: return "Ultra";
        case MODE_ZEN: return "Zen";
        case MODE_POWER: return "Power";
        default: return "";
    }
}

const char *Game_PowerName(PowerUp p) {
    switch (p) {
        case PU_BOMB: return "Bomb";
        case PU_LASER: return "Laser";
        case PU_FREEZE: return "Freeze";
        case PU_SLOW: return "Slow";
        case PU_SWEEP: return "Sweep";
        default: return "";
    }
}

void Game_Init(Game *g, uint64_t seed, long highScore, int highLevel, int highLines) {
    Game_InitConfig(g, Game_DefaultConfig(), seed, highScore, highLevel, highLines);
}

void Game_InitConfig(Game *g, GameConfig cfg, uint64_t seed, long highScore, int highLevel, int highLines) {
    memset(g, 0, sizeof(*g));
    if (cfg.width < BOARD_MIN_W) cfg.width = BOARD_MIN_W;
    if (cfg.width > BOARD_MAX_W) cfg.width = BOARD_MAX_W;
    if (cfg.height < BOARD_MIN_H) cfg.height = BOARD_MIN_H;
    if (cfg.height > BOARD_MAX_H) cfg.height = BOARD_MAX_H;
    g->cfg = cfg;
    g->highScore = highScore;
    g->highLevel = highLevel;
    g->highLines = highLines;
    Game_Restart(g, seed);
}

void Game_Restart(Game *g, uint64_t seed) {
    long hs = g->highScore;
    int hl = g->highLevel;
    int hln = g->highLines;
    GameConfig cfg = g->cfg;
    if (cfg.width == 0) cfg = Game_DefaultConfig(); /* a Game that was zeroed, not initialised */
    memset(g, 0, sizeof(*g));
    g->cfg = cfg;
    g->highScore = hs;
    g->highLevel = hl;
    g->highLines = hln;

    Board_Init(&g->board, cfg.width, cfg.height);
    Rng_Seed(&g->bag.rng, seed);
    g->bag.bagPos = PIECE_COUNT; /* force a shuffle on first draw */
    g->next = Bag_Next(&g->bag);
    RollNextPower(g);

    g->hold = (PieceType)-1;
    g->combo = -1;
    g->timeLeft = cfg.mode == MODE_ULTRA ? ULTRA_SECONDS : 0.0f;
    g->level = 1;
    g->linesCleared = 0;
    g->score = 0;
    g->fallInterval = ComputeFallInterval(1);
    g->phase = GS_PLAYING;

    SpawnNext(g);
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;

    g->elapsed += dt;
    if (g->freezeTimer > 0.0f) { g->freezeTimer -= dt; if (g->freezeTimer < 0.0f) g->freezeTimer = 0.0f; }
    if (g->slowTimer > 0.0f) { g->slowTimer -= dt; if (g->slowTimer < 0.0f) g->slowTimer = 0.0f; }
    if (g->cfg.mode == MODE_ULTRA) {
        g->timeLeft -= dt;
        if (g->timeLeft <= 0.0f) {
            g->timeLeft = 0.0f;
            g->won = true;
            g->phase = GS_GAMEOVER;
            return;
        }
    }

    bool canFall = Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1);
    if (canFall) {
        g->grounded = false;
        g->lockTimer = 0.0f;
        if (g->freezeTimer <= 0.0f) g->fallTimer += dt; /* Freeze: gravity waits */
        float interval = EffectiveFallInterval(g);
        if (g->fallTimer >= interval) {
            g->fallTimer -= interval;
            g->py += 1;
            if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1)) {
                g->grounded = true;
            }
        }
    } else {
        g->grounded = true;
        g->fallTimer = 0.0f;
        g->lockTimer += dt;
        if (g->lockTimer >= LOCK_DELAY_SECONDS) {
            LockAndAdvance(g);
        }
    }
}

bool Game_MoveLeft(Game *g) {
    if (g->phase != GS_PLAYING) return false;
    if (Board_TestFit(&g->board, g->current, g->rotation, g->px - 1, g->py)) {
        g->px -= 1;
        g->justMoved = true;
        NotifyActionSuccess(g);
        return true;
    }
    return false;
}

bool Game_MoveRight(Game *g) {
    if (g->phase != GS_PLAYING) return false;
    if (Board_TestFit(&g->board, g->current, g->rotation, g->px + 1, g->py)) {
        g->px += 1;
        g->justMoved = true;
        NotifyActionSuccess(g);
        return true;
    }
    return false;
}

bool Game_SoftDrop(Game *g) {
    if (g->phase != GS_PLAYING) return false;
    if (Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1)) {
        g->py += 1;
        g->score += 1;
        g->fallTimer = 0.0f;
        return true;
    }
    return false;
}

bool Game_Rotate(Game *g, int dir) {
    if (g->phase != GS_PLAYING) return false;
    if (g->current == PIECE_O) return false; /* symmetric, nothing to do */

    int newRot = g->rotation + (dir >= 0 ? 1 : -1);

    /* Generic small kick search (not a literal SRS kick table): try the
     * in-place rotation first, then nudge left/right/up, then corners, then
     * the wider shifts an I-piece can need. Keeps rotation forgiving near
     * walls, floors and stack edges without per-transition kick data that
     * would be easy to get subtly wrong from memory. */
    static const int kKicks[][2] = {
        {0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-1, -1}, {1, -1},
        {-2, 0}, {2, 0}, {0, -2}, {-1, 1}, {1, 1},
    };

    for (size_t i = 0; i < sizeof(kKicks) / sizeof(kKicks[0]); i++) {
        int tx = g->px + kKicks[i][0];
        int ty = g->py + kKicks[i][1];
        if (Board_TestFit(&g->board, g->current, newRot, tx, ty)) {
            g->rotation = ((newRot % 4) + 4) % 4;
            g->px = tx;
            g->py = ty;
            g->justRotated = true;
            NotifyActionSuccess(g);
            return true;
        }
    }
    return false;
}

void Game_HardDrop(Game *g) {
    if (g->phase != GS_PLAYING) return;
    int cells = 0;
    while (Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1)) {
        g->py += 1;
        cells++;
    }
    g->score += 2L * cells;
    g->justHardDropped = true;
    g->lastDropCells = cells;
    LockAndAdvance(g);
}

bool Game_Hold(Game *g) {
    if (g->phase != GS_PLAYING || g->holdUsed) return false;
    PieceType outgoing = g->current;
    PowerUp outPower = g->curPower;
    int outCell = g->curPowerCell;
    if ((int)g->hold < 0) {
        g->hold = outgoing;
        g->holdPower = outPower;
        g->holdPowerCell = outCell;
        SpawnNext(g);
    } else {
        g->current = g->hold;
        g->curPower = g->holdPower;
        g->curPowerCell = g->holdPowerCell;
        g->hold = outgoing;
        g->holdPower = outPower;
        g->holdPowerCell = outCell;
        Place(g);
        if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py)) g->phase = GS_GAMEOVER;
    }
    g->holdUsed = true;
    g->justHeld = true;
    return true;
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

int Game_GhostY(const Game *g) {
    int y = g->py;
    while (Board_TestFit(&g->board, g->current, g->rotation, g->px, y + 1)) y++;
    return y;
}

void Game_ConsumeFrameFlags(Game *g) {
    g->lastDropCells = 0;
    g->justHardDropped = false;
    g->justLocked = false;
    g->justRotated = false;
    g->justMoved = false;
    g->lastWasLevelUp = false;
    g->lastClearCount = 0;
    g->justHeld = false;
    g->lastPower = PU_NONE;
    g->justZenReset = false;
    g->lastCombo = 0;
    g->lastComboBonus = 0;
}
