#include "howtodemo.h"
#include <stdio.h>
#include <string.h>

static const char *const kPages[HOWTO_PAGES] = {"Basics", "Hold and next", "Scoring", "Modes", "Power-ups"};

const char *HowTo_PageName(int page) { return (page >= 0 && page < HOWTO_PAGES) ? kPages[page] : ""; }

/* ------------------------------------------------------------ scripts */

typedef enum { S_END, S_PIECE, S_NEXT, S_POWER, S_PRESS } StepKind;
typedef struct {
    StepKind kind;
    int arg;     /* a piece, a power-up (its cell * 16 + power) or a DemoPress */
    float after; /* seconds to the next step */
} Step;

#define POWER_ON(p, cell) ((cell) * 16 + (int)(p))

/* A T slides, turns both ways, soft-drops two rows and hard-drops into the
 * three-cell gap: a single. */
static const Step kBasics[] = {
    {S_PIECE, PIECE_T, 0.0f}, {S_NEXT, PIECE_O, 0.55f},
    {S_PRESS, DOP_LEFT, 0.35f}, {S_PRESS, DOP_RIGHT, 0.25f}, {S_PRESS, DOP_RIGHT, 0.45f},
    {S_PRESS, DOP_TURN, 0.45f}, {S_PRESS, DOP_TURN_LEFT, 0.45f},
    {S_PRESS, DOP_SOFT, 0.1f}, {S_PRESS, DOP_SOFT, 0.4f},
    {S_PRESS, DOP_HARD, 1.4f}, {S_END, 0, 0.0f},
};

/* The S goes into Hold and the I that was next comes in, with the O shown
 * in Next for the whole of its fall; the I clears the row, the O comes in
 * as Next said, then Hold swaps the S back in for it and the S drops. */
static const Step kHold[] = {
    {S_PIECE, PIECE_S, 0.0f}, {S_NEXT, PIECE_I, 0.7f},
    {S_PRESS, DOP_HOLD, 0.0f}, {S_NEXT, PIECE_O, 0.6f},
    {S_PRESS, DOP_LEFT, 0.25f}, {S_PRESS, DOP_LEFT, 0.45f},
    {S_PRESS, DOP_HARD, 0.9f},
    {S_PRESS, DOP_HOLD, 0.7f},
    {S_PRESS, DOP_HARD, 1.4f}, {S_END, 0, 0.0f},
};

/* Two I pieces stood up in the well's last column: a quad, then a
 * back-to-back quad. */
static const Step kScoring[] = {
    {S_PIECE, PIECE_I, 0.0f}, {S_NEXT, PIECE_I, 0.55f},
    {S_PRESS, DOP_TURN, 0.35f}, {S_PRESS, DOP_RIGHT, 0.2f}, {S_PRESS, DOP_RIGHT, 0.2f}, {S_PRESS, DOP_RIGHT, 0.45f},
    {S_PRESS, DOP_HARD, 1.3f},
    {S_NEXT, PIECE_T, 0.0f},
    {S_PRESS, DOP_TURN, 0.35f}, {S_PRESS, DOP_RIGHT, 0.2f}, {S_PRESS, DOP_RIGHT, 0.2f}, {S_PRESS, DOP_RIGHT, 0.45f},
    {S_PRESS, DOP_HARD, 1.8f}, {S_END, 0, 0.0f},
};

/* Modes: the Basics single, in the mode picked (in Power the T carries a
 * Bomb on its top block). */
static const Step kModes[] = {
    {S_PIECE, PIECE_T, 0.0f}, {S_NEXT, PIECE_O, 0.0f}, {S_POWER, -1, 0.55f},
    {S_PRESS, DOP_RIGHT, 0.45f}, {S_PRESS, DOP_TURN, 0.4f}, {S_PRESS, DOP_TURN_LEFT, 0.4f},
    {S_PRESS, DOP_HARD, 1.8f}, {S_END, 0, 0.0f},
};

/* Power-ups: an O carrying the power-up drops onto the stack; then the T
 * after it shows what it did (Freeze holds it up, Slow lets it drift). */
static const Step kPower[] = {
    {S_PIECE, PIECE_O, 0.0f}, {S_NEXT, PIECE_T, 0.0f}, {S_POWER, -1, 0.7f},
    {S_PRESS, DOP_HARD, 2.6f},
    {S_PRESS, DOP_HARD, 1.2f}, {S_END, 0, 0.0f},
};

static const Step *ScriptOf(int page) {
    switch (page) {
        case HOWTO_HOLD: return kHold;
        case HOWTO_SCORING: return kScoring;
        case HOWTO_MODES: return kModes;
        case HOWTO_POWER: return kPower;
        default: return kBasics;
    }
}

/* ------------------------------------------------------------ boards */

/* Row y from the bottom: '#' a block (each a piece's colour), '.' none. */
static void Row(Game *g, int y, const char *pattern) {
    int row = BOARD_HIDDEN + g->board.h - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = (int8_t)(c == '#' ? (x * 3 + y * 5) % PIECE_COUNT : CELL_EMPTY);
    }
}

static void SetUpBoard(Game *g, int page) {
    for (int y = 0; y < BOARD_MAX_TOTAL_H; y++)
        for (int x = 0; x < BOARD_MAX_W; x++) g->board.cells[y][x] = CELL_EMPTY;
    switch (page) {
        case HOWTO_HOLD: Row(g, 0, "....####"); break;
        case HOWTO_SCORING:
            for (int y = 0; y < 8; y++) Row(g, y, "#######.");
            break;
        case HOWTO_POWER:
            Row(g, 0, "#.######");
            Row(g, 1, "######.#");
            Row(g, 2, "####.###");
            Row(g, 3, "##.....#");
            break;
        default: Row(g, 0, "###...##"); break; /* Basics, Modes */
    }
}

static GameMode ModeOf(int page, int variant) {
    if (page == HOWTO_POWER) return MODE_POWER;
    if (page == HOWTO_MODES && variant >= 0 && variant < MODE_COUNT) return (GameMode)variant;
    return MODE_MARATHON;
}

/* The piece at the top, as a new one comes in. */
static void Spawn(Game *g, PieceType t) {
    g->current = t;
    g->curPower = PU_NONE;
    g->rotation = 0;
    g->px = (g->board.w - 4) / 2;
    g->py = BOARD_HIDDEN;
    g->fallTimer = g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = g->py;
    g->grounded = false;
    g->holdUsed = false;
}

void HowToDemo_Start(HowToDemo *d, int page, int variant) {
    memset(d, 0, sizeof(*d));
    d->page = page >= 0 && page < HOWTO_PAGES ? page : HOWTO_BASICS;
    d->variant = variant;
    GameConfig cfg = {ModeOf(d->page, variant), DEMO_W, DEMO_H, true}; /* not the classic board: modern scoring */
    Game_InitConfig(&d->game, cfg, 0x5EEDu + (uint64_t)page, 0, 1, 0);
    d->game.hold = (PieceType)-1;
    d->game.nextPower = PU_NONE;
    SetUpBoard(&d->game, d->page);
    d->press = DOP_NONE;
    d->pressAge = 99.0f;
    d->calloutAge = 99.0f;
}

/* ------------------------------------------------------------ playing */

#define LOOP_REST 0.0f /* the script's last wait is already the rest before it starts over */

static const char *ClearWord(int n) {
    static const char *const k[5] = {"", "", "DOUBLE", "TRIPLE", "QUAD"};
    return n >= 0 && n <= 4 ? k[n] : "";
}

static void Group(long v, char *out, size_t size) {
    char digits[24];
    snprintf(digits, sizeof(digits), "%ld", v < 0 ? -v : v);
    int n = (int)strlen(digits), o = 0;
    char buf[32];
    for (int i = 0; i < n && o < (int)sizeof(buf) - 2; i++) {
        if (i > 0 && (n - i) % 3 == 0) buf[o++] = ' ';
        buf[o++] = digits[i];
    }
    buf[o] = '\0';
    snprintf(out, size, "%s%s", v < 0 ? "-" : "", buf);
}

/* After a lock: what it cleared, paid and fired, and the callout. */
static void Locked(HowToDemo *d, long scoreBefore, int filledBefore) {
    Game *g = &d->game;
    d->filledBefore = filledBefore;
    d->filledAfter = Board_CountFilled(&g->board);
    d->fired = g->lastPower;
    int n = g->lastClearCount;
    if (n > 0 && d->clearCount < DEMO_MAX_CLEARS) {
        /* What the rows paid: the lock's gain less the drop's points and a
         * power-up's own (game.c: 150 to pick one up, and its blocks). */
        long power = 0;
        switch (g->lastPower) {
            case PU_BOMB: power = 150 + 30L * g->lastPowerRemoved; break;
            case PU_LASER: power = 150 + 15L * g->lastPowerRemoved; break;
            case PU_SWEEP: power = 150 + 200; break;
            case PU_FREEZE: case PU_SLOW: power = 150; break;
            default: break;
        }
        long gained = g->score - scoreBefore - 2L * g->lastDropCells - power;
        d->clears[d->clearCount] = n;
        d->gained[d->clearCount] = gained;
        d->clearCount++;
        char pts[24];
        Group(gained, pts, sizeof(pts));
        if (g->lastWasB2B) snprintf(d->callout, sizeof(d->callout), "BACK-TO-BACK +%s", pts);
        else if (ClearWord(n)[0]) snprintf(d->callout, sizeof(d->callout), "%s +%s", ClearWord(n), pts);
        else snprintf(d->callout, sizeof(d->callout), "+%s", pts);
        d->calloutAge = 0.0f;
    }
    if (g->lastPower != PU_NONE) {
        char word[16];
        snprintf(word, sizeof(word), "%s", Game_PowerName(g->lastPower));
        for (char *c = word; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        snprintf(d->callout, sizeof(d->callout), "%s", word);
        d->calloutAge = 0.0f;
    }
}

/* Returns true when the press locked the piece (a hard drop). */
static bool Press(HowToDemo *d, DemoPress p) {
    Game *g = &d->game;
    d->press = p;
    d->pressAge = 0.0f;
    long before = g->score;
    int filled = Board_CountFilled(&g->board);
    switch (p) {
        case DOP_LEFT: Game_MoveLeft(g); break;
        case DOP_RIGHT: Game_MoveRight(g); break;
        case DOP_TURN: Game_Rotate(g, 1); break;
        case DOP_TURN_LEFT: Game_Rotate(g, -1); break;
        case DOP_SOFT: Game_SoftDrop(g); break;
        case DOP_HOLD: Game_Hold(g); break;
        case DOP_HARD:
            Game_HardDrop(g);
            Locked(d, before, filled);
            return true;
        default: break;
    }
    return false;
}

/* The power-up the script puts on the piece: the page's own, or Modes'
 * Bomb in Power. */
static void PutPower(HowToDemo *d) {
    Game *g = &d->game;
    if (d->page == HOWTO_POWER && d->variant > PU_NONE && d->variant < PU_COUNT) {
        g->curPower = (PowerUp)d->variant;
        g->curPowerCell = 2; /* the O's lower left block */
    } else if (d->page == HOWTO_MODES && g->cfg.mode == MODE_POWER) {
        g->curPower = PU_BOMB;
        g->curPowerCell = 0; /* the T's top block */
    }
}

void HowToDemo_Tick(HowToDemo *d, float dt) {
    Game *g = &d->game;
    d->t += dt;
    d->pressAge += dt;
    d->calloutAge += dt;
    if (d->ended) {
        d->wait -= dt;
        if (d->wait <= 0.0f) {
            int loops = d->loops + 1;
            HowToDemo_Start(d, d->page, d->variant);
            d->loops = loops;
        }
        return;
    }
    const Step *script = ScriptOf(d->page);
    bool pressLocked = false;
    d->wait -= dt;
    while (d->wait <= 0.0f && !d->ended) {
        const Step *s = &script[d->step];
        switch (s->kind) {
            case S_END:
                d->ended = true;
                d->wait = LOOP_REST;
                break;
            case S_PIECE: Spawn(g, (PieceType)s->arg); break;
            case S_NEXT:
                g->next = (PieceType)s->arg;
                g->nextPower = PU_NONE;
                break;
            case S_POWER: PutPower(d); break;
            case S_PRESS: pressLocked = Press(d, (DemoPress)s->arg) || pressLocked; break;
        }
        if (d->ended) break;
        d->wait += s->after;
        d->step++;
    }
    if (d->ended) return;
    /* The rules run on: gravity, the lock delay, the clocks. A piece that
     * locks by itself counts as a lock too. */
    long before = g->score;
    int filled = Board_CountFilled(&g->board);
    Game_Update(g, dt);
    if (g->justLocked && !pressLocked) Locked(d, before, filled);
    if (g->cfg.mode == MODE_POWER && g->nextPower != PU_NONE) g->nextPower = PU_NONE; /* only the script's power-ups */
    Game_ConsumeFrameFlags(g);
}

DemoClaim HowToDemo_Claim(int page, int variant) {
    (void)variant;
    DemoClaim c;
    memset(&c, 0, sizeof(c));
    switch (page) {
        case HOWTO_BASICS:
        case HOWTO_HOLD:
        case HOWTO_MODES:
            c.clears[0] = 1;
            c.gained[0] = 100;
            c.count = 1;
            break;
        case HOWTO_SCORING:
            c.clears[0] = 4;
            c.gained[0] = 800;
            c.clears[1] = 4;
            c.gained[1] = 1250; /* 800 and half again, and the combo's 50 */
            c.count = 2;
            break;
        default: break; /* Power-ups clear no rows: the power-up does its work */
    }
    return c;
}
