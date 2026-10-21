#include "game.h"
#include "finesse.h"
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

float Game_FallInterval(int level) { return ComputeFallInterval(level); }

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

/* Power mode: about one piece in seven carries a power-up on one block.
 * Rolled right after the piece is drawn from the bag, as it always was, so
 * the rng's calls come in the same order whether one piece is drawn ahead
 * (before 3.2.0) or QUEUE_MAX (the queue): the same seed gives the same
 * pieces with the same power-ups. */
static void RollPower(Game *g, PowerUp *power, int *cell) {
    *power = PU_NONE;
    *cell = 0;
    if (g->cfg.mode != MODE_POWER && !g->daily.powerUps) return; /* the Daily's Power twist rolls them too (3.6.0) */
    if (Rng_Range(&g->bag.rng, 100) < POWER_CHANCE_PERCENT) {
        *power = (PowerUp)(1 + (int)Rng_Range(&g->bag.rng, PU_COUNT - 1));
        *cell = (int)Rng_Range(&g->bag.rng, CELLS_PER_PIECE);
    }
}

/* Draws one more piece onto the end of the queue. */
static void Draw(Game *g, int at) {
    g->queue[at] = Bag_Next(&g->bag);
    RollPower(g, &g->queuePower[at], &g->queueCell[at]);
}

/* next (and its power-up) is the queue's first piece. */
static void MirrorNext(Game *g) {
    g->next = g->queue[0];
    g->nextPower = g->queuePower[0];
    g->nextPowerCell = g->queueCell[0];
}

static void Place(Game *g) {
    g->rotation = 0;
    g->px = (g->board.w - 4) / 2;
    g->py = BOARD_HIDDEN;
    g->fallTimer = 0.0f;
    g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = g->py;
    g->grounded = false;
    g->lastMoveWasTurn = false;
    g->lastKick = 0;
    g->lastTurnHalf = false;
    g->presses = 0; /* finesse counts from the piece coming in (a swap from Hold too) */
}

/* The stack reached the top: a piece that cannot come in (a spawn, or a
 * swap from Hold) or one that locked partly above the visible well. Zen has
 * no losing, so it sweeps the stack away and play carries on with the same
 * piece; every other mode ends. */
static void TopOut(Game *g) {
    if (g->cfg.mode == MODE_ZEN) {
        Board_Init(&g->board, g->cfg.width, g->cfg.height);
        g->zenResets++;
        g->justZenReset = true;
        g->combo = -1;
        g->stats.combo = -1;
        Place(g);
    } else {
        g->phase = GS_GAMEOVER;
    }
}

/* The piece that comes in is `next` as it stands (a test or the harness
 * may have set it alone, and that is honoured); then the queue moves up
 * one and a fresh piece is drawn onto its end. */
static void SpawnNext(Game *g) {
    g->current = g->next;
    g->curPower = g->nextPower;
    g->curPowerCell = g->nextPowerCell;
    for (int i = 0; i + 1 < QUEUE_MAX; i++) {
        g->queue[i] = g->queue[i + 1];
        g->queuePower[i] = g->queuePower[i + 1];
        g->queueCell[i] = g->queueCell[i + 1];
    }
    Draw(g, QUEUE_MAX - 1);
    MirrorNext(g);
    g->holdUsed = false;
    Place(g);

    if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py)) TopOut(g);
}

/* ------------------------------------------------------------ Dig (3.4.0) */

static bool HiddenRowsUsed(const Board *b);

float Game_DigInterval(float elapsed) {
    if (!(elapsed > 0.0f)) elapsed = 0.0f;
    float k = elapsed / DIG_RISE_RAMP_SECONDS;
    if (k > 1.0f) k = 1.0f;
    return DIG_RISE_START_SECONDS - (DIG_RISE_START_SECONDS - DIG_RISE_END_SECONDS) * k;
}

float Game_DigFill(const Game *g) {
    if (g->cfg.mode != MODE_DIG || g->digInterval <= 0.0f) return 0.0f;
    float k = g->digTimer / g->digInterval;
    return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
}

bool Game_DigWarning(const Game *g) { return g->cfg.mode == MODE_DIG && Game_DigFill(g) >= 1.0f - DIG_WARN_SHARE; }

float Game_DigSecondsLeft(const Game *g) {
    if (g->cfg.mode != MODE_DIG) return 0.0f;
    float left = g->digInterval - g->digTimer;
    return left < 0.0f ? 0.0f : left;
}

void Game_GarbageRow(Board *b, int row, int gap) {
    if (row < 0 || row >= Board_TotalH(b)) return;
    for (int x = 0; x < b->w; x++) b->cells[row][x] = x == gap ? (int8_t)CELL_EMPTY : CELL_GARBAGE;
}

int Game_GarbageGap(const Board *b, int row) {
    if (row < 0 || row >= Board_TotalH(b)) return -1;
    int gap = -1;
    for (int x = 0; x < b->w; x++) {
        int8_t v = b->cells[row][x];
        if (v == CELL_EMPTY) {
            if (gap >= 0) return -1; /* two gaps */
            gap = x;
        } else if (v != CELL_GARBAGE) {
            return -1;
        }
    }
    return gap;
}

int Game_GarbageRows(const Board *b) {
    int n = 0;
    for (int row = BOARD_HIDDEN; row < Board_TotalH(b); row++)
        if (Board_RowHasGarbage(b, row)) n++;
    return n;
}

/* A gap column for a new garbage row: any but the newest row's, so two
 * rows never open the same way (a well one cell wide has no choice). */
static int DigPickGap(Game *g) {
    int w = g->board.w;
    if (w <= 1) return 0;
    int gap = (int)Rng_Range(&g->digRng, (uint32_t)(g->digLastGap >= 0 ? w - 1 : w));
    if (g->digLastGap >= 0 && gap >= g->digLastGap) gap++;
    g->digLastGap = gap;
    return gap;
}

/* The run's first rows: DIG_START_ROWS of garbage up from the floor. */
static void DigFillStart(Game *g) {
    g->digLastGap = -1;
    int total = Board_TotalH(&g->board);
    int rows = DIG_START_ROWS < g->board.h - 4 ? DIG_START_ROWS : g->board.h - 4; /* a short well keeps four rows clear */
    for (int i = 0; i < rows; i++) Game_GarbageRow(&g->board, total - 1 - i, DigPickGap(g));
    g->digTimer = 0.0f;
    g->digInterval = Game_DigInterval(0.0f);
}

/* A garbage row pushes up from the floor: the stack moves up a row, and the
 * falling piece with it when it would otherwise overlap. A block pushed
 * above the well, or a piece with nowhere to go, tops the run out. */
static void DigRise(Game *g) {
    Board *b = &g->board;
    int total = Board_TotalH(b);
    for (int y = 0; y + 1 < total; y++) memcpy(b->cells[y], b->cells[y + 1], sizeof(b->cells[y]));
    Game_GarbageRow(b, total - 1, DigPickGap(g));
    g->digRises++;
    g->justRose = true;
    g->digInterval = Game_DigInterval(g->elapsed);
    if (HiddenRowsUsed(b)) {
        TopOut(g);
        return;
    }
    if (!Board_TestFit(b, g->current, g->rotation, g->px, g->py)) {
        if (g->py <= 0 || !Board_TestFit(b, g->current, g->rotation, g->px, g->py - 1)) {
            TopOut(g);
            return;
        }
        g->py -= 1;
        g->lowestRow -= 1; /* the floor it measured from came up with it */
        g->justRosePiece = true;
    }
    g->grounded = !Board_TestFit(b, g->current, g->rotation, g->px, g->py + 1);
}

/* Any block in the rows above the visible well: solid, but never drawn. */
static bool HiddenRowsUsed(const Board *b) {
    for (int y = 0; y < BOARD_HIDDEN; y++)
        for (int x = 0; x < b->w; x++)
            if (b->cells[y][x] != CELL_EMPTY) return true;
    return false;
}

/* A step down to a row lower than any this piece has been on gives the
 * lock delay back in full, resets included (the guideline's extended
 * placement): the only thing that refreshes the fifteen. */
static void NoteDescent(Game *g) {
    if (g->py > g->lowestRow) {
        g->lowestRow = g->py;
        g->lockResets = 0;
        g->lockTimer = 0.0f;
    }
}

static float EffectiveFallInterval(const Game *g) {
    return g->fallInterval * (g->slowTimer > 0.0f ? SLOW_FACTOR : 1.0f);
}

/* Fires the power-up carried by the block that just locked, at (x, y) on the
 * board as it is after the line clear. Returns how many rows the whole board
 * moved down (a Sweep), so the landing hint can follow the piece. */
static int ApplyPower(Game *g, PowerUp p, int x, int y) {
    g->lastPower = p;
    g->lastPowerX = x;
    g->lastPowerY = y;
    g->lastPowerRemoved = 0;
    g->powerBoard = g->board; /* presentation only: what the power's show takes apart */
    g->stats.powers++;
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
            /* The block went down with the board, unless it was in the rows that went. */
            if (y + 3 < Board_TotalH(&g->board)) g->lastPowerY = y + 3;
            return 3;
        }
        default: break;
    }
    return 0;
}

/* The run's counters (the results card), on every board alike: the combo
 * and back-to-back are counted the way modern scoring counts them, whether
 * or not this board pays for them. */
static void CountClear(RunStats *st, int cleared, bool difficult) {
    if (cleared <= 0) {
        st->combo = -1;
        return;
    }
    if (cleared > 4) cleared = 4;
    st->clears[cleared]++;
    st->combo++;
    if (st->combo > st->bestCombo) st->bestCombo = st->combo;
    if (difficult && st->lastQuad) st->backToBacks++;
    st->lastQuad = difficult;
}

/* ---------------------------------------------------- T-spins (3.2.0) */

/* A cell round the T is "filled" when it holds a block or lies outside the
 * well (walls and floor count, as the guideline has it). */
static bool Filled(const Board *b, int x, int y) {
    if (x < 0 || x >= b->w || y < 0 || y >= Board_TotalH(b)) return true;
    return b->cells[y][x] != CELL_EMPTY;
}

TSpinKind Game_TSpinKind(const Game *g) {
    if (g->current != PIECE_T || !g->lastMoveWasTurn) return TSPIN_NONE;
    /* The T's centre is its local (1,1) in every rotation; its corners are
     * the box's. The two "front" corners are the ones at the side the T
     * points to: up (rotation 0), right (1), down (2), left (3). */
    const Board *b = &g->board;
    int cx = g->px + 1, cy = g->py + 1;
    bool tl = Filled(b, cx - 1, cy - 1), tr = Filled(b, cx + 1, cy - 1);
    bool bl = Filled(b, cx - 1, cy + 1), br = Filled(b, cx + 1, cy + 1);
    int filled = (tl ? 1 : 0) + (tr ? 1 : 0) + (bl ? 1 : 0) + (br ? 1 : 0);
    if (filled < 3) return TSPIN_NONE;
    bool front;
    switch (g->rotation & 3) {
        case 0: front = tl && tr; break;
        case 1: front = tr && br; break;
        case 2: front = bl && br; break;
        default: front = tl && bl; break;
    }
    /* The fifth-kick upgrade is a quarter turn's alone: a half turn's
     * index 4 is the 180 list's sideways step, not the TST kick. */
    if (front || (!g->lastTurnHalf && g->lastKick == TSPIN_FULL_KICK)) return TSPIN_FULL;
    return TSPIN_MINI;
}

long Game_ClearPoints(int cleared, TSpinKind tspin) {
    static const long kClassic[5] = {0, 100, 300, 500, 800};
    static const long kTSpin[5] = {400, 800, 1200, 1600, 1600};
    static const long kMini[5] = {100, 200, 400, 400, 400};
    if (cleared < 0) cleared = 0;
    if (cleared > 4) cleared = 4;
    if (tspin == TSPIN_FULL) return kTSpin[cleared];
    if (tspin == TSPIN_MINI) return kMini[cleared];
    return kClassic[cleared];
}

long Game_PerfectClearBonus(int cleared, bool b2bQuad) {
    static const long kBonus[5] = {0, 800, 1200, 1800, 2000};
    if (cleared <= 0) return 0;
    if (cleared > 4) cleared = 4;
    if (cleared == 4 && b2bQuad) return 3200;
    return kBonus[cleared];
}

static void LockAndAdvance(Game *g) {
    TSpinKind tspin = Game_TSpinKind(g); /* read before the T is stamped (its corners are never its own cells either way) */
    /* The Practice room's finesse hint (3.5.0): the fewest presses that
     * would have put the piece here, against the presses it took. Read
     * before the piece is stamped (its own cells would be in the way); a
     * placement finesse does not count (a tuck, a spin) gives no hint. It
     * is only looked up with hints on, and the rules never read it. */
    g->lastFinesseExtra = -1;
    g->lastFinesseOptimal = -1;
    g->lastFinessePresses = g->presses;
    if (g->practice.on && g->practice.hints) {
        int best = Finesse_Optimal(&g->board, g->current, g->rotation, g->px, g->py);
        g->lastFinesseOptimal = best;
        if (best >= 0) g->lastFinesseExtra = g->presses > best ? g->presses - best : 0;
    }
    Board_LockPiece(&g->board, g->current, g->rotation, g->px, g->py);
    g->piecesLocked++;
    g->lockBoard = g->board; /* presentation only: what the clear replays from */

    g->lastLockType = g->current;
    g->lastLockRotation = g->rotation;
    g->lastLockX = g->px;
    g->lastLockY = g->py;

    /* Where the power block landed, before the clear moves anything. */
    PowerUp power = g->curPower;
    int powerX = 0, powerY = 0;
    if (power != PU_NONE) {
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(g->current, g->rotation, cells);
        int idx = g->curPowerCell % CELLS_PER_PIECE;
        powerX = g->px + cells[idx].x;
        powerY = g->py + cells[idx].y;
    }

    /* Dig: the full rows that hold garbage are dug (counted before the
     * clear takes them). */
    int dug = 0;
    if (g->cfg.mode == MODE_DIG)
        for (int row = 0; row < Board_TotalH(&g->board); row++)
            if (Board_RowFull(&g->board, row) && Board_RowHasGarbage(&g->board, row)) dug++;
    int cleared = Board_ClearLines(&g->board, g->lastClearRows);
    g->lastClearCount = cleared;
    g->justLocked = true;
    g->lastDug = dug;
    g->dug += dug;
    bool difficult = cleared == 4 || (tspin != TSPIN_NONE && cleared > 0);
    bool perfect = cleared > 0 && Board_CountFilled(&g->board) == 0;
    CountClear(&g->stats, cleared, difficult);
    g->lastTSpin = tspin;
    g->lastPerfectClear = perfect;
    if (tspin != TSPIN_NONE) g->stats.tspins++;
    if (tspin != TSPIN_NONE && cleared > 0) g->stats.tspinClears[cleared > 4 ? 4 : cleared]++;
    if (perfect) g->stats.perfectClears++;

    if (cleared > 0 || (tspin != TSPIN_NONE && g->cfg.modernScoring)) {
        /* The classic table times the level; under modern rules a T-spin
         * has its own (lines or not), a difficult clear (a quad or a T-spin
         * clear) right after a difficult one pays half again, a chain of
         * clears adds its combo, and a clear that leaves the well empty adds
         * the perfect clear bonus. A T-spin that clears nothing neither
         * breaks nor makes back-to-back. */
        long points = Game_ClearPoints(cleared, g->cfg.modernScoring ? tspin : TSPIN_NONE) * g->level;
        if (g->cfg.modernScoring) {
            bool b2b = difficult && g->backToBack;
            if (b2b) {
                points += points / 2;
                g->lastWasB2B = true;
            }
            if (cleared > 0) {
                g->backToBack = difficult;
                g->combo++;
                g->lastCombo = g->combo;
                g->lastComboBonus = g->combo > 0 ? 50L * g->combo * g->level : 0;
                points += g->lastComboBonus;
            }
            if (perfect) points += Game_PerfectClearBonus(cleared, b2b && cleared == 4) * g->level;
        }
        g->score += points;
    }
    if (cleared > 0) {
        g->linesCleared += cleared;
        if (dug > 0) g->score += (long)DIG_ROW_BONUS * dug * g->level; /* Dig: each garbage row dug */
        /* Zen's level never moves, nor Dig's: there the rises are the pressure.
         * A practice run counts up from its start level (3.5.0). */
        int from = g->practice.on ? g->practice.startLevel : (g->daily.on ? g->daily.startLevel : 1);
        int newLevel = (g->cfg.mode == MODE_ZEN || g->cfg.mode == MODE_DIG) ? 1 : from + g->linesCleared / LINES_PER_LEVEL;
        if (newLevel != g->level) {
            g->level = newLevel;
            g->lastWasLevelUp = true;
            g->fallInterval = ComputeFallInterval(g->level);
        }
    } else if (g->cfg.modernScoring) {
        g->combo = -1;
    }

    /* Power-ups fire after the clear, so the rows the piece completed still
     * count (a Laser or Bomb used to spoil them). A power block in a row that
     * cleared fires where it was; one above the cleared rows fires where it
     * fell to. */
    if (power != PU_NONE) {
        bool inCleared = false;
        int below = 0;
        for (int i = 0; i < cleared && i < 4; i++) {
            if (g->lastClearRows[i] == powerY) inCleared = true;
            else if (g->lastClearRows[i] > powerY) below++;
        }
        g->lastLockY += ApplyPower(g, power, powerX, inCleared ? powerY : powerY + below);
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

    /* Lock-out: a piece that locked partly above the visible well (an upward
     * kick can lift it there) tops out like a blocked spawn, so nothing solid
     * ever sits where it cannot be seen. */
    if (HiddenRowsUsed(&g->board)) {
        TopOut(g);
        if (g->phase == GS_GAMEOVER) return;
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
        case MODE_DIG: return "Dig";
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

void Game_InitPractice(Game *g, GameConfig cfg, PracticeRules rules, uint64_t seed) {
    rules.on = true;
    Game_InitConfig(g, cfg, seed, 0, 1, 0);
    g->practice = rules;
    Game_Restart(g, seed); /* again, with the rules in place (the first start was a plain run's) */
}

void Game_InitDaily(Game *g, GameConfig cfg, DailyRules rules, uint64_t seed) {
    rules.on = true;
    Game_InitConfig(g, cfg, seed, 0, 1, 0);
    g->daily = rules;
    Game_Restart(g, seed); /* again, with the rules in place */
}

bool Game_Timed(const Game *g) { return g->cfg.mode == MODE_ULTRA || g->daily.on; }

void Game_Restart(Game *g, uint64_t seed) {
    long hs = g->highScore;
    int hl = g->highLevel;
    int hln = g->highLines;
    GameConfig cfg = g->cfg;
    PracticeRules practice = g->practice;
    DailyRules daily = g->daily;
    if (cfg.width == 0) cfg = Game_DefaultConfig(); /* a Game that was zeroed, not initialised */
    memset(g, 0, sizeof(*g));
    g->cfg = cfg;
    g->practice = practice;
    g->daily = daily;
    g->highScore = hs;
    g->highLevel = hl;
    g->highLines = hln;

    Board_Init(&g->board, cfg.width, cfg.height);
    Rng_Seed(&g->bag.rng, seed);
    g->bag.bagPos = PIECE_COUNT; /* force a shuffle on first draw */
    for (int i = 0; i < QUEUE_MAX; i++) Draw(g, i);
    MirrorNext(g);

    g->hold = (PieceType)-1;
    g->combo = -1;
    g->stats.combo = -1;
    g->timeLeft = cfg.mode == MODE_ULTRA ? ULTRA_SECONDS : (daily.on ? DAILY_SECONDS : 0.0f);
    g->level = 1;
    g->linesCleared = 0;
    g->score = 0;
    g->fallInterval = ComputeFallInterval(1);
    g->phase = GS_PLAYING;
    g->digLastGap = -1;
    g->lastFinesseExtra = -1;
    g->lastFinesseOptimal = -1;
    if (cfg.mode == MODE_DIG) {
        /* Its own rng, so the garbage never moves the bag: a seed gives the
         * pieces it gives in every mode. */
        Rng_Seed(&g->digRng, seed ^ 0xD16D16D16ull);
        DigFillStart(g);
    }
    if (g->daily.on) {
        /* The Daily challenge (3.6.0): the fast start's level (clamped, so a
         * twist can never ask for level 0). */
        int lvl = g->daily.startLevel;
        g->level = lvl < 1 ? 1 : (lvl > DAILY_LEVEL_MAX ? DAILY_LEVEL_MAX : lvl);
        g->daily.startLevel = g->level;
        g->fallInterval = ComputeFallInterval(g->level);
    }
    if (g->practice.on) {
        /* The Practice room (3.5.0): its start level, and its starting
         * garbage as Dig's rows (one gap each, from the garbage's own rng,
         * so the pieces are still the seed's). The level is clamped here
         * too, so a hand-edited config.ini cannot ask for level 0. */
        int lvl = g->practice.startLevel;
        g->level = lvl < PRACTICE_LEVEL_MIN ? PRACTICE_LEVEL_MIN : (lvl > PRACTICE_LEVEL_MAX ? PRACTICE_LEVEL_MAX : lvl);
        g->practice.startLevel = g->level;
        g->fallInterval = ComputeFallInterval(g->level);
        int rows = g->practice.garbageRows;
        if (rows > PRACTICE_GARBAGE_MAX) rows = PRACTICE_GARBAGE_MAX;
        if (rows > g->board.h - 4) rows = g->board.h - 4; /* a short well keeps four rows clear, as Dig does */
        if (rows > 0) {
            Rng_Seed(&g->digRng, seed ^ 0xD16D16D16ull);
            int total = Board_TotalH(&g->board);
            for (int i = 0; i < rows; i++) Game_GarbageRow(&g->board, total - 1 - i, DigPickGap(g));
        }
    }

    SpawnNext(g);
}

bool Game_IsPractice(const Game *g) { return g->practice.on; }

void Game_NotePress(Game *g) {
    if (g->phase == GS_PLAYING) g->presses++;
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;

    g->elapsed += dt;
    if (g->freezeTimer > 0.0f) { g->freezeTimer -= dt; if (g->freezeTimer < 0.0f) g->freezeTimer = 0.0f; }
    if (g->slowTimer > 0.0f) { g->slowTimer -= dt; if (g->slowTimer < 0.0f) g->slowTimer = 0.0f; }
    if (Game_Timed(g)) {
        g->timeLeft -= dt;
        if (g->timeLeft <= 0.0f) {
            g->timeLeft = 0.0f;
            g->won = true;
            g->phase = GS_GAMEOVER;
            return;
        }
    }

    if (g->cfg.mode == MODE_DIG) {
        /* The next garbage row: its wait runs with play (a pause holds it,
         * as READY does by never getting here). */
        g->digTimer += dt;
        while (g->digTimer >= g->digInterval && g->digInterval > 0.0f) {
            g->digTimer -= g->digInterval;
            DigRise(g);
            if (g->phase != GS_PLAYING) return;
        }
    }

    /* The Practice room (3.5.0): with gravity off the piece hangs where it
     * is (drops still work); with the lock off it never locks by itself (a
     * hard drop locks it). */
    bool noGravity = g->practice.on && g->practice.noGravity;
    bool noLock = g->practice.on && g->practice.noLock;
    bool canFall = Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1);
    if (canFall) {
        g->grounded = false;
        /* In the air the lock timer holds instead of starting over, so a
         * kick that lifts a landed piece buys no time (it used to: tapping
         * rotate kept a piece alive for ever). With the resets spent it runs
         * in the air too, and the piece locks the moment it is back down. */
        if (g->lockResets >= MAX_LOCK_RESETS && !noLock) g->lockTimer += dt;
        if (g->freezeTimer <= 0.0f && !noGravity) g->fallTimer += dt; /* Freeze: gravity waits */
        float interval = EffectiveFallInterval(g);
        if (g->fallTimer >= interval) {
            g->fallTimer -= interval;
            g->py += 1;
            g->lastMoveWasTurn = false;
            NoteDescent(g);
            if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1)) {
                g->grounded = true;
            }
        }
    } else {
        g->grounded = true;
        g->fallTimer = 0.0f;
        if (noLock) return;
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
        g->lastMoveWasTurn = false;
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
        g->lastMoveWasTurn = false;
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
        g->lastMoveWasTurn = false;
        g->justSoftDropped = true;
        NoteDescent(g);
        return true;
    }
    return false;
}

bool Game_Rotate(Game *g, int dir) {
    if (g->phase != GS_PLAYING) return false;
    if (g->current == PIECE_O) return false; /* symmetric, nothing to do */

    int from = ((g->rotation % 4) + 4) % 4;
    int newRot = (from + (dir == 2 ? 2 : (dir >= 0 ? 1 : 3))) % 4;

    /* SRS (3.2.0): the published wall-kick table for this piece and turn
     * (tetromino.c), the first offset that fits. It replaced a generic
     * nudge list that let pieces climb the stack. */
    Cell kicks[TETROMINO_MAX_KICKS];
    int n = Tetromino_Kicks(g->current, from, newRot, kicks);
    for (int i = 0; i < n; i++) {
        int tx = g->px + kicks[i].x;
        int ty = g->py + kicks[i].y;
        if (Board_TestFit(&g->board, g->current, newRot, tx, ty)) {
            g->rotation = newRot;
            g->px = tx;
            g->py = ty;
            g->lastMoveWasTurn = true;
            g->lastKick = i;
            g->lastTurnHalf = dir == 2;
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
    if (cells > 0) g->lastMoveWasTurn = false; /* a drop that moved is a move; one that did not leaves a T-spin a T-spin */
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
        /* Zen sweeps here too: it promises no game over. */
        if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py)) TopOut(g);
    }
    g->holdUsed = !(g->practice.on && g->practice.infiniteHold); /* the Practice room's infinite hold (3.5.0) */
    g->justHeld = true;
    g->stats.holds++;
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
    g->justSoftDropped = false;
    g->lastWasLevelUp = false;
    g->lastClearCount = 0;
    g->justHeld = false;
    g->lastPower = PU_NONE;
    g->justZenReset = false;
    g->lastCombo = 0;
    g->lastComboBonus = 0;
    g->lastWasB2B = false;
    g->lastTSpin = TSPIN_NONE;
    g->lastPerfectClear = false;
    g->lastDug = 0;
    g->justRose = false;
    g->justRosePiece = false;
    g->lastFinesseExtra = -1;
    g->lastFinesseOptimal = -1;
    g->lastFinessePresses = 0;
    g->justUndid = false;
}
