/* Render-only reactions to the one-frame events the rules raise: the
 * power-ups' shows (powerfx.h says when each part happens), shake, the
 * pixels a clear bursts into and a hard drop's dust, the callouts and
 * popups hudfx.c decides, READY and GO and the countdown after a pause, and
 * a Sprint clear's rows bursting upward. The cleared rows, a Sweep's rows,
 * the landed piece and the falling piece are well.c's (from feel.h).
 * Nothing in here feeds back into the game. Every effect honours Screen
 * shake, Reduced flashing (no white, nothing blinks) and Motion
 * (comfort.h), and stays inside the well.
 *  - Bomb: the 3x3 it took stays in view and blinks (steady lit with
 *    Reduced flashing), then bursts in its own colours with a square
 *    shockwave (it fades in place with Reduced motion) and a 9 px shake.
 *  - Laser: a charge line runs down the column (at once with Reduced
 *    motion), then the beam, and the column's cells pop out from the power
 *    cell, nearest first.
 *  - Freeze: an ice wash crosses the well, frost grows in from the corners
 *    and holds, blinking in the last 1.5 s (steady, dimmer, with Reduced
 *    flashing); when it ends the frost breaks into pixels.
 *  - Slow: the orange frame and two faint afterimages trailing the piece
 *    (none with Reduced motion).
 *  - Sweep: the green bar crossing the bottom rows, bursting their cells. */
#include "renderpriv.h"
#include "blockdraw.h"
#include "comfort.h"
#include "feel.h"
#include "powerfx.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NEVER 9.0f
#define TRAIL 16

typedef struct { PieceType type; int rot, px, py; } TrailEntry;

static struct {
    float shakeAge, shakeAmp;
    Color powerColor;
    /* Bomb */
    int bombX, bombY;
    float bombAge, bombPrev;
    int8_t bombCells[3][3];   /* the 3x3 it took (CELL_EMPTY where there was nothing) */
    /* Laser */
    int laserX, laserY;
    float laserAge;
    int8_t laserCol[BOARD_MAX_TOTAL_H];
    bool laserPopped[BOARD_MAX_TOTAL_H];
    /* Freeze */
    float freezeAge, freezeWas, thawAge;
    float frostDrawn;
    /* Slow: where the rules had the piece these last frames (a ring) */
    TrailEntry trail[TRAIL];
    int trailHead, trailN;
    bool trailOn;
    int afterimagesDrawn;
} sFx;

#define SHAKE_TIME 0.16f
#define TRIPLE_SHAKE 3.0f
#define QUAD_SHAKE 8.0f

/* The well's own pixels: a clear's burst, a hard drop's dust, what the
 * power-ups take apart. A pool of its own (the shared one holds 256, and a
 * quad on a 24-wide well bursts into up to 288); a pixel that leaves the
 * well is gone. */
#define BITS 640
#define BIT_GRAVITY 640.0f
typedef struct {
    float x, y, vx, vy, life, maxLife;
    int size;
    Color c;
} Bit;
static Bit sBits[BITS];
static int sBitCount = 0; /* in use, for the harness */
static int sEndGone = 0;  /* a Sprint clear's rows burst so far */

static float Rand(int lo, int hi) { return (float)GetRandomValue(lo, hi); }

static void Emit(float x, float y, float vx, float vy, float life, int size, Color c) {
    for (int i = 0; i < BITS; i++) {
        Bit *b = &sBits[i];
        if (b->life > 0.0f) continue;
        *b = (Bit){x, y, vx, vy, life, life, size, c};
        return;
    }
}

void Effects_Reset(void) {
    memset(&sFx, 0, sizeof(sFx));
    sFx.shakeAge = sFx.bombAge = sFx.bombPrev = sFx.laserAge = sFx.freezeAge = sFx.thawAge = NEVER;
    memset(sBits, 0, sizeof(sBits));
    sBitCount = 0;
    sEndGone = 0;
}

int Effects_Bits(void) { return sBitCount; }

/* Particles from the shared pool (power-ups): half as many with Reduced motion. */
static void Burst(float cx, float cy, Color color, int count, float speed) {
    Ui_Burst(cx, cy, color, Comfort_Particles(count), speed);
}

static void Shake(float amp) {
    if (amp > sFx.shakeAmp || sFx.shakeAge >= SHAKE_TIME) {
        sFx.shakeAmp = amp;
        sFx.shakeAge = 0.0f;
    }
}

static int BitSize(const PlayLayout *l) { return l->cell >= 20 ? 4 : 2; }

/* A cell bursting into a few pixels of its colour, thrown the way `dirX`
 * says (0: both ways). */
static void BurstCell(const PlayLayout *l, int col, int row, Color c, int count, float dirX, float dirY) {
    if (row < BOARD_HIDDEN) return;
    int n = Comfort_Particles(count), size = BitSize(l);
    float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
    for (int k = 0; k < n; k++) {
        float dx = dirX != 0.0f ? dirX : ((k & 1) ? 1.0f : -1.0f);
        Emit(x + Rand(-l->cell / 3, l->cell / 3), y + Rand(-l->cell / 3, l->cell / 3), dx * Rand(60, 210), dirY * Rand(30, 150) - Rand(30, 150),
             Rand(30, 50) / 100.0f, size, c);
    }
}

/* A hard drop throws dust sideways off the piece's underside. */
static void Dust(const PlayLayout *l, const Feel *f) {
    const LandFeel *land = &f->land;
    if (!land->on) return; /* a Zen fresh start swept it away */
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(land->type, land->rot, cells);
    Color c = Ui_Lerp(Render_PieceColor(land->type), kText, 0.5f);
    int landY = land->replayed ? land->y : land->liveY;
    int size = BitSize(l);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        bool under = true;
        for (int j = 0; j < CELLS_PER_PIECE; j++)
            if (cells[j].x == cells[i].x && cells[j].y == cells[i].y + 1) under = false;
        int row = landY + cells[i].y;
        if (!under || row < BOARD_HIDDEN) continue;
        float x = (float)(Rp_CellX(l, land->x + cells[i].x) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell - 2);
        int n = Comfort_Particles(2);
        for (int k = 0; k < n; k++) {
            float dir = (k & 1) ? 1.0f : -1.0f;
            Emit(x + dir * (float)(l->cell / 3), y, dir * Rand(70, 170), -Rand(20, 90), Rand(22, 36) / 100.0f, size, c);
        }
    }
}

/* The frost's stair-stepped wedges in the well's four corners, `size` px
 * along each wall. */
static void DrawFrost(const PlayLayout *l, int size, Color c) {
    if (size < 2) return;
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH;
    int step = l->cell >= 20 ? 4 : 2;
    for (int i = 0; i * step < size; i++) {
        int len = size - i * step; /* this stair's reach along the wall */
        int y = i * step;
        DrawRectangle(bx, by + y, len, step, c);                      /* top left */
        DrawRectangle(bx + bw - len, by + y, len, step, c);           /* top right */
        DrawRectangle(bx, by + bh - y - step, len, step, c);          /* bottom left */
        DrawRectangle(bx + bw - len, by + bh - y - step, len, step, c); /* bottom right */
    }
}

/* The frost breaks: pixels off each corner, inward. */
static void ThawBits(const PlayLayout *l) {
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH, size = BitSize(l);
    Color c = Rp_PowerColor(PU_FREEZE);
    int n = Comfort_Particles(6);
    for (int corner = 0; corner < 4; corner++) {
        float x = corner & 1 ? (float)(bx + bw - 4) : (float)(bx + 4), y = corner & 2 ? (float)(by + bh - 4) : (float)(by + 4);
        float dx = corner & 1 ? -1.0f : 1.0f;
        for (int k = 0; k < n; k++)
            Emit(x + dx * Rand(0, l->cell), y + (corner & 2 ? -Rand(0, l->cell) : Rand(0, l->cell)), dx * Rand(40, 160), -Rand(60, 200),
                 Rand(30, 50) / 100.0f, size, c);
    }
}

void Effects_React(const Game *g, const PlayLayout *l, const Feel *f) {
    int cell = l->cell;
    bool playing = g->phase == GS_PLAYING;

    /* The Slow's trail: where the piece was these last frames. */
    if (playing && g->slowTimer > 0.0f) {
        if (!sFx.trailOn || g->justLocked || g->justHeld || g->justZenReset) {
            sFx.trailHead = sFx.trailN = 0;
            sFx.trailOn = true;
        }
        sFx.trail[sFx.trailHead] = (TrailEntry){g->current, g->rotation, g->px, g->py};
        sFx.trailHead = (sFx.trailHead + 1) % TRAIL;
        if (sFx.trailN < TRAIL) sFx.trailN++;
    } else {
        sFx.trailOn = false;
    }
    /* The Freeze ran out: the frost breaks. */
    if (playing && sFx.freezeWas > 0.0f && g->freezeTimer <= 0.0f) {
        sFx.thawAge = 0.0f;
        ThawBits(l);
    }
    sFx.freezeWas = g->freezeTimer;

    if (!g->justLocked) return;

    if (g->lastPower != PU_NONE) {
        sFx.powerColor = Rp_PowerColor(g->lastPower);
        int px = g->lastPowerX, py = g->lastPowerY;
        float cx = (float)(Rp_CellX(l, px) + cell / 2), cy = (float)(Rp_CellY(l, py) + cell / 2);
        const Board *b = &g->powerBoard;
        switch (g->lastPower) {
            case PU_BOMB:
                sFx.bombX = px;
                sFx.bombY = py;
                sFx.bombAge = 0.0f;
                sFx.bombPrev = -1.0f;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int x = px + dx, y = py + dy;
                        bool in = x >= 0 && x < b->w && y >= 0 && y < Board_TotalH(b);
                        sFx.bombCells[dy + 1][dx + 1] = in ? b->cells[y][x] : (int8_t)CELL_EMPTY;
                    }
                break;
            case PU_LASER:
                sFx.laserX = px;
                sFx.laserY = py;
                sFx.laserAge = 0.0f;
                for (int y = 0; y < BOARD_MAX_TOTAL_H; y++) {
                    sFx.laserCol[y] = y < Board_TotalH(b) && px >= 0 && px < b->w ? b->cells[y][px] : (int8_t)CELL_EMPTY;
                    sFx.laserPopped[y] = false;
                }
                break;
            case PU_FREEZE:
                sFx.freezeAge = 0.0f;
                sFx.thawAge = NEVER;
                Burst(cx, cy, sFx.powerColor, 16, 170.0f);
                break;
            default: /* Slow, Sweep: a puff at the block (the Sweep's bar is feel.h's) */
                Burst(cx, cy, sFx.powerColor, 16, 170.0f);
                break;
        }
    }

    /* Each clear size its own weight: a triple shakes a little, a quad 8 px. */
    if (g->lastClearCount >= 4) Shake(QUAD_SHAKE);
    else if (g->lastClearCount == 3) Shake(TRIPLE_SHAKE);

    if (g->justHardDropped && g->lastDropCells > 0) {
        /* A thud scaled by how far it fell, and dust off its underside. */
        float amp = 2.0f + (float)g->lastDropCells * 0.35f;
        Shake(amp > 7.0f ? 7.0f : amp);
        Dust(l, f);
    }
}

/* The cleared cells that zipped out this frame burst into 1 to 3 pixels of
 * their own colour, away from where the piece landed (a quad's all at once,
 * with gold among them); a Sweep's cells burst as its bar passes, green
 * among them. */
static void ClearBursts(const PlayLayout *l, const Feel *f) {
    const ClearReplay *r = &f->clear;
    if (!r->on || r->t == r->prevT) return;
    int size = BitSize(l);
    for (int i = 0; i < r->count && i < 4; i++) {
        int row = r->rows[i];
        if (row < BOARD_HIDDEN) continue;
        for (int col = 0; col < r->before.w; col++) {
            int8_t v = r->before.cells[row][col];
            if (v == CELL_EMPTY || !Feel_ClearCellGone(r, col, r->t) || Feel_ClearCellGone(r, col, r->prevT)) continue;
            Color c = Render_PieceColor((PieceType)v);
            int n = Comfort_Particles(r->quad ? 3 : (r->count >= 3 ? GetRandomValue(2, 3) : GetRandomValue(1, 2)));
            float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
            float dir = col > r->lockCol ? 1.0f : (col < r->lockCol ? -1.0f : (GetRandomValue(0, 1) ? 1.0f : -1.0f));
            for (int k = 0; k < n; k++) {
                Color bc = (r->quad && (k & 1)) ? kGold : c;
                Emit(x + Rand(-l->cell / 3, l->cell / 3), y + Rand(-l->cell / 3, l->cell / 3), dir * Rand(60, 210), -Rand(30, 150),
                     Rand(30, 50) / 100.0f, size, bc);
            }
        }
    }
    Color green = Rp_PowerColor(PU_SWEEP);
    for (int i = 0; i < r->sweepCount; i++) {
        int row = r->sweepRows[i];
        if (row < BOARD_HIDDEN) continue;
        for (int col = 0; col < r->before.w; col++) {
            int8_t v = r->before.cells[row][col];
            if (v == CELL_EMPTY || !Feel_SweepCellGone(r, col, r->t) || Feel_SweepCellGone(r, col, r->prevT)) continue;
            Color c = Render_PieceColor((PieceType)v);
            int n = Comfort_Particles(2);
            float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
            for (int k = 0; k < n; k++)
                Emit(x + Rand(-l->cell / 3, l->cell / 3), y + Rand(-l->cell / 3, l->cell / 3), Rand(40, 160), -Rand(80, 220), Rand(30, 50) / 100.0f,
                     size, (k & 1) ? green : c);
        }
    }
}

void Effects_EndShow(const Game *g, const PlayLayout *l, const Results *end) {
    if (!end || !end->on || end->card.kind != END_SPRINT_CLEAR) {
        sEndGone = 0;
        return;
    }
    int gone = Results_RowsTurned(end, g->board.h);
    int size = BitSize(l);
    for (int i = sEndGone; i < gone; i++) {
        int row = BOARD_HIDDEN + g->board.h - 1 - i;
        for (int col = 0; col < g->board.w; col++) {
            int8_t v = g->board.cells[row][col];
            if (v == CELL_EMPTY) continue;
            Color c = Render_PieceColor((PieceType)v);
            float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
            int n = Comfort_Particles(2);
            for (int k = 0; k < n; k++)
                Emit(x + Rand(-l->cell / 3, l->cell / 3), y, Rand(-70, 70), -Rand(240, 460), Rand(45, 75) / 100.0f, size, c);
        }
    }
    sEndGone = gone;
}

/* The Bomb's blink ended: its 3x3 bursts, the shockwave starts, 9 px shake. */
static void BombBurst(const PlayLayout *l) {
    bool calm = Comfort_Calm();
    float cx = (float)(Rp_CellX(l, sFx.bombX) + l->cell / 2), cy = (float)(Rp_CellY(l, sFx.bombY) + l->cell / 2);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int8_t v = sFx.bombCells[dy + 1][dx + 1];
            if (v == CELL_EMPTY) continue;
            BurstCell(l, sFx.bombX + dx, sFx.bombY + dy, Render_PieceColor((PieceType)v), 3, (float)dx, (float)dy);
        }
    Burst(cx, cy, sFx.powerColor, 20, 260.0f);
    Burst(cx, cy, calm ? sFx.powerColor : WHITE, 10, 180.0f);
    Shake(BOMB_SHAKE_PX);
}

/* The Laser's cells pop out from the power cell as their time comes. */
static void LaserPops(const PlayLayout *l) {
    if (PowerFx_LaserStage(sFx.laserAge) == LASER_NONE) return;
    for (int y = 0; y < BOARD_MAX_TOTAL_H; y++) {
        if (sFx.laserPopped[y] || sFx.laserCol[y] == CELL_EMPTY) continue;
        if (sFx.laserAge < PowerFx_LaserPopAt(y - sFx.laserY)) continue;
        sFx.laserPopped[y] = true;
        BurstCell(l, sFx.laserX, y, Render_PieceColor((PieceType)sFx.laserCol[y]), 3, 0.0f, 0.0f);
    }
}

void Effects_Tick(float dt, const PlayLayout *l, const Feel *f) {
    sFx.shakeAge += dt;
    sFx.bombPrev = sFx.bombAge;
    if (sFx.bombAge < NEVER) sFx.bombAge += dt;
    if (sFx.laserAge < NEVER) sFx.laserAge += dt;
    if (sFx.freezeAge < NEVER) sFx.freezeAge += dt;
    if (sFx.thawAge < NEVER) sFx.thawAge += dt;
    if (PowerFx_BombBursts(sFx.bombAge, sFx.bombPrev)) BombBurst(l);
    LaserPops(l);
    ClearBursts(l, f);
}

int Effects_ShakeY(void) {
    if (!Prefs_Get()->screenShake || sFx.shakeAge >= SHAKE_TIME) return 0;
    /* Vertical only: the thing that happened was a drop. */
    float k = 1.0f - sFx.shakeAge / SHAKE_TIME;
    return (int)roundf(sFx.shakeAmp * k * (fmodf(sFx.shakeAge * 60.0f, 2.0f) < 1.0f ? 1.0f : -0.6f));
}

/* The well's pixels move on and are drawn; one that leaves the well is gone. */
static void Bits(const PlayLayout *l, float dt) {
    float left = (float)l->boardX, right = (float)(l->boardX + l->boardW), bottom = (float)(l->boardY + l->boardH);
    sBitCount = 0;
    for (int i = 0; i < BITS; i++) {
        Bit *b = &sBits[i];
        if (b->life <= 0.0f) continue;
        b->life -= dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->vy += BIT_GRAVITY * dt;
        b->vx *= 1.0f - 2.0f * dt;
        if (b->life <= 0.0f || b->x < left || b->x >= right || b->y >= bottom) {
            b->life = 0.0f;
            continue;
        }
        sBitCount++;
        int s = b->life < b->maxLife * 0.4f && b->size > 2 ? b->size - 2 : b->size;
        /* Snapped to a 2 px grid so they stay in the same pixel world. */
        DrawRectangle(((int)b->x / 2) * 2 - s / 2, ((int)b->y / 2) * 2 - s / 2, s, s, b->c);
    }
}

/* The Freeze: the tint, the wash, the frost (and the frost breaking), the
 * ice frame. */
static void DrawFreeze(const Game *g, const PlayLayout *l, bool calm, bool motion) {
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH, cell = l->cell;
    Color ice = Rp_PowerColor(PU_FREEZE);
    float thaw = PowerFx_Thaw(sFx.thawAge);
    sFx.frostDrawn = 0.0f;
    if (g->freezeTimer <= 0.0f) {
        if (thaw > 0.0f) DrawFrost(l, (int)((float)cell * 2.5f), Fade(ice, 0.45f * thaw)); /* breaking */
        return;
    }
    float lit = PowerFx_FreezeLit(g->freezeTimer, calm);
    DrawRectangle(bx, by, bw, bh, Fade(ice, 0.10f));
    float wash = PowerFx_FreezeWash(sFx.freezeAge, motion);
    if (wash >= 0.0f) {
        /* A band three cells tall passing down the well, brightest at its
         * leading edge (never white). */
        int band = cell * 3, y = by - band + (int)(wash * (float)(bh + band));
        for (int i = 0; i < 3; i++) {
            int yy = y + i * cell, top = yy < by ? by : yy, bot = yy + cell > by + bh ? by + bh : yy + cell;
            if (bot > top) DrawRectangle(bx, top, bw, bot - top, Fade(ice, (0.36f - 0.1f * (float)i) * (calm ? 0.6f : 1.0f)));
        }
    }
    float frost = PowerFx_Frost(sFx.freezeAge, g->freezeTimer, motion);
    sFx.frostDrawn = frost;
    DrawFrost(l, (int)(frost * (float)cell * 2.5f), Fade(ice, 0.45f * lit));
    DrawRectangleLinesEx((Rectangle){(float)bx, (float)by, (float)bw, (float)bh}, 3.0f, Fade(ice, 0.7f * lit));
}

/* The Slow: the orange frame and the afterimages behind the piece. */
static void DrawSlow(const Game *g, const PlayLayout *l, bool motion) {
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH;
    Color orange = Rp_PowerColor(PU_SLOW);
    sFx.afterimagesDrawn = 0;
    if (g->slowTimer <= 0.0f) return;
    float frame = PowerFx_SlowEnding(g->slowTimer) ? 0.6f * (g->slowTimer / SLOW_ENDING_SECONDS) + 0.2f : 0.6f; /* eases off at the end */
    DrawRectangleLinesEx((Rectangle){(float)bx, (float)by, (float)bw, (float)bh}, 3.0f, Fade(orange, frame));
    if (g->phase != GS_PLAYING || !sFx.trailOn) return;
    Cell now[CELLS_PER_PIECE];
    Tetromino_GetCells(g->current, g->rotation, now);
    Color base = Render_PieceColor(g->current);
    int n = PowerFx_Afterimages(motion);
    for (int i = 0; i < n; i++) {
        int back = SLOW_AFTERIMAGE_GAP * (i + 1);
        if (back >= sFx.trailN) break;
        int idx = ((sFx.trailHead - 1 - back) % TRAIL + TRAIL) % TRAIL;
        PieceType type = sFx.trail[idx].type;
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(type, sFx.trail[idx].rot, cells);
        Color c = Fade(base, PowerFx_AfterimageAlpha(i));
        bool drew = false;
        for (int k = 0; k < CELLS_PER_PIECE; k++) {
            int col = sFx.trail[idx].px + cells[k].x, row = sFx.trail[idx].py + cells[k].y;
            if (row < BOARD_HIDDEN) continue;
            bool under = false; /* not where the piece is now: it is behind it */
            for (int j = 0; j < CELLS_PER_PIECE; j++)
                if (g->px + now[j].x == col && g->py + now[j].y == row) under = true;
            if (under) continue;
            DrawRectangle(Rp_CellX(l, col) + 2, Rp_CellY(l, row) + 2, l->cell - 4, l->cell - 4, c);
            drew = true;
        }
        if (drew) sFx.afterimagesDrawn++;
    }
}

/* The Bomb: the 3x3 blinking where it stood, then the shockwave. */
static void DrawBomb(const PlayLayout *l, const Feel *f, bool calm, bool motion) {
    int cell = l->cell;
    BombStage stage = PowerFx_BombStage(sFx.bombAge);
    if (stage == BOMB_NONE) return;
    int cx = Rp_CellX(l, sFx.bombX) + cell / 2, cy = Rp_CellY(l, sFx.bombY) + cell / 2;
    if (stage == BOMB_BLINK) {
        if (Feel_ClearStage(f) != CLEAR_NONE) return; /* the stamped board is showing: the cells are still in it */
        bool lit = PowerFx_BombLit(sFx.bombAge, calm);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int8_t v = sFx.bombCells[dy + 1][dx + 1];
                int row = sFx.bombY + dy;
                if (v == CELL_EMPTY || row < BOARD_HIDDEN) continue;
                int x = Rp_CellX(l, sFx.bombX + dx), y = Rp_CellY(l, row);
                Render_PieceBlock(x, y, cell, (PieceType)v, Render_PieceColor((PieceType)v), 1.0f);
                if (lit) DrawRectangle(x + 1, y + 1, cell - 2, cell - 2, calm ? Fade(sFx.powerColor, 0.40f) : Fade(WHITE, 0.65f));
            }
        return;
    }
    float k = PowerFx_BombShock(sFx.bombAge);
    int r = (int)((motion ? 1.0f + k * 0.8f : 1.8f) * (float)cell * 1.5f);
    if (!calm) DrawRectangle(cx - r, cy - r, r * 2, r * 2, Fade(WHITE, 0.55f * (1.0f - k))); /* no white flash with Reduced flashing */
    DrawRectangleLinesEx((Rectangle){(float)(cx - r), (float)(cy - r), (float)(r * 2), (float)(r * 2)}, 3.0f, Fade(sFx.powerColor, 1.0f - k));
}

/* The Laser: the charge line, the beam, the column's cells waiting to pop. */
static void DrawLaser(const PlayLayout *l, const Feel *f, bool calm, bool motion) {
    int cell = l->cell, by = l->boardY, bh = l->boardH;
    LaserStage stage = PowerFx_LaserStage(sFx.laserAge);
    if (stage == LASER_NONE) return;
    int x0 = Rp_CellX(l, sFx.laserX);
    if (Feel_ClearStage(f) == CLEAR_NONE) {
        /* The cells it takes are still there until they pop. */
        for (int y = BOARD_HIDDEN; y < BOARD_MAX_TOTAL_H; y++) {
            int8_t v = sFx.laserCol[y];
            if (v == CELL_EMPTY || sFx.laserPopped[y]) continue;
            Render_PieceBlock(x0, Rp_CellY(l, y), cell, (PieceType)v, Render_PieceColor((PieceType)v), 1.0f);
        }
    }
    if (stage == LASER_CHARGE) {
        float k = PowerFx_LaserCharge(sFx.laserAge, motion);
        int h = (int)(k * (float)bh), w = cell >= 20 ? 4 : 2;
        Color line = calm ? sFx.powerColor : Ui_Lerp(sFx.powerColor, WHITE, 0.5f);
        DrawRectangle(x0 + (cell - w) / 2, by, w, h, Fade(line, 0.9f));
        DrawRectangle(x0 + cell / 2 - w, by + h - w * 2, w * 2, w * 2, line); /* its head */
        return;
    }
    float fade = PowerFx_LaserBeam(sFx.laserAge);
    int w = (int)((float)cell * (1.0f - (1.0f - fade) * 0.7f));
    DrawRectangle(x0 + (cell - w) / 2, by, w, bh, Fade(sFx.powerColor, (calm ? 0.30f : 0.85f) * fade));
    if (!calm) DrawRectangle(x0 + cell / 2 - 1, by, 2, bh, Fade(WHITE, 0.8f * fade)); /* the bright core */
}

/* The Sweep's bar crossing the bottom rows, with a trail behind it. */
static void DrawSweepBar(const PlayLayout *l, const Feel *f, bool calm) {
    const ClearReplay *r = &f->clear;
    if (!r->on || Feel_ClearStage(f) != CLEAR_SWEEP) return;
    float bar = Feel_SweepBar(r, r->t);
    if (bar < 0.0f) return;
    int top = BOARD_MAX_TOTAL_H, bottom = -1;
    for (int i = 0; i < r->sweepCount; i++) {
        if (r->sweepRows[i] < top) top = r->sweepRows[i];
        if (r->sweepRows[i] > bottom) bottom = r->sweepRows[i];
    }
    if (top < BOARD_HIDDEN) top = BOARD_HIDDEN;
    if (bottom < top) return;
    int y = Rp_CellY(l, top), h = (bottom - top + 1) * l->cell;
    int x = l->boardX + (int)(bar * (float)l->cell), w = l->cell / 2 < 4 ? 4 : l->cell / 2;
    Color green = Rp_PowerColor(PU_SWEEP);
    for (int i = 1; i <= 3; i++) /* the trail */
        DrawRectangle(x - i * w, y, w, h, Fade(green, 0.30f - 0.08f * (float)i));
    DrawRectangle(x, y, w, h, calm ? Fade(green, 0.9f) : Ui_Lerp(green, WHITE, 0.45f));
}

void Effects_DrawInWell(const Game *g, const PlayLayout *l, const Feel *f, float dt) {
    bool calm = Comfort_Calm(), motion = Comfort_Motion();
    /* Freeze and Slow tint the well while they last (the Freeze's frost over
     * the Slow's frame when both run), then what the lock set off. */
    DrawSlow(g, l, motion);
    DrawFreeze(g, l, calm, motion);
    DrawBomb(l, f, calm, motion);
    DrawLaser(l, f, calm, motion);
    DrawSweepBar(l, f, calm);
    Bits(l, dt);
    Ui_UpdateParticles(dt);
}

void Effects_Inspect(char *power, size_t n, float *frost, int *afterimages) {
    BombStage b = PowerFx_BombStage(sFx.bombAge);
    LaserStage ls = PowerFx_LaserStage(sFx.laserAge);
    const char *word = "none";
    if (b == BOMB_BLINK) word = "bomb-blink";
    else if (b == BOMB_SHOCK) word = "bomb-shock";
    else if (ls == LASER_CHARGE) word = "laser-charge";
    else if (ls == LASER_BEAM) word = "laser-beam";
    else if (PowerFx_Thaw(sFx.thawAge) > 0.0f) word = "thaw";
    snprintf(power, n, "%s", word);
    *frost = sFx.frostDrawn;
    *afterimages = sFx.afterimagesDrawn;
}

static Color TintOf(const Callout *c) {
    switch (c->tint) {
        case TINT_GOLD: return kGold;
        case TINT_POWER: return Rp_PowerColor(c->power);
        case TINT_DIM: return kTextDim; /* the Practice room's finesse hint */
        default: return kLight;
    }
}

void Effects_DrawCallouts(const PlayLayout *l, const HudFx *hud) {
    bool motion = Comfort_Motion();
    /* Popups first, from the cleared rows: the callouts sit over them. */
    for (int i = 0; i < hud->pops; i++) {
        const Popup *p = &hud->pop[i];
        int cx = l->boardX + (int)(p->col * (float)l->cell), cy = l->boardY + (int)(p->row * (float)l->cell);
        PopupPlace pp = Layout_Popup(l, p->text, cx, cy, HudFx_PopupRise(p, motion), Ui_Measure);
        DrawRectangle(pp.x - 4, pp.y - 4, pp.w + 8, pp.size + 8, kPlate);
        Ui_Text(p->text, pp.x, pp.y, pp.size, p->gold ? kGold : kLight);
    }

    const Callout *cs[CALLOUT_MAX];
    int n = HudFx_Callouts(hud, cs);
    if (n <= 0) return;
    CalloutText lines[CALLOUT_MAX];
    CalloutPlace at[CALLOUT_MAX];
    for (int i = 0; i < n; i++)
        lines[i] = (CalloutText){cs[i]->text, cs[i]->shortText, HudFx_CalloutRise(cs[i], motion), HudFx_CalloutMaxSize(hud, cs[i])};
    /* Rises a little and holds, then cuts: no fades on pixel type. */
    Layout_Callouts(l, lines, n, Ui_Measure, at);
    for (int i = 0; i < n; i++) {
        DrawRectangle(at[i].plateX, at[i].plateY, at[i].plateW, at[i].plateH, kPlate);
        Rp_DrawFitted(at[i].useShort ? cs[i]->shortText : cs[i]->text, at[i].fit, l->boardX + l->boardW / 2, at[i].textY, TintOf(cs[i]));
    }
}

/* READY (violet) drops 8 px in and holds while the run waits; GO (gold)
 * shows as play begins. After a pause (with the countdown on) 3, 2 and 1
 * in violet, each dropping in the same way. Placed like a callout, so it
 * fits every well; a callout that arrives during GO takes its place. */
#define CUE_DROP_SECONDS 0.12f
void Effects_DrawCue(const PlayLayout *l, const StartCue *cue, const HudFx *hud) {
    if (cue->stage == CUE_NONE || (cue->stage == CUE_GO && hud->calls > 0)) return;
    bool ready = cue->stage == CUE_READY, count = cue->stage == CUE_COUNT;
    float into = count ? fmodf(cue->t, CUE_COUNT_STEP) : cue->t; /* each number drops in afresh */
    int rise = 0;
    if ((ready || count) && Comfort_Motion() && into < CUE_DROP_SECONDS) rise = CALLOUT_RISE - (int)(into / CUE_DROP_SECONDS * (float)CALLOUT_RISE);
    static const char *const kCount[4] = {"", "1", "2", "3"};
    CalloutText line = {count ? kCount[PlayDriver_CountNumber(cue)] : (ready ? "READY" : "GO"), NULL, rise, 32};
    CalloutPlace at;
    Layout_Callouts(l, &line, 1, Ui_Measure, &at);
    DrawRectangle(at.plateX, at.plateY, at.plateW, at.plateH, kPlate);
    Rp_DrawFitted(line.text, at.fit, l->boardX + l->boardW / 2, at.textY, (ready || count) ? kLight : kGold);
}
