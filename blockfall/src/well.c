/* The well: its frame, its grid, the stack, the ghost and the falling
 * piece, drawn the way feel.h says they move.
 *  - The frame flashes violet at a level up (hudfx.h) and gold after a quad
 *    (feel.h); with Reduced flashing it only half lights. In danger
 *    (danger.h) it eases to red and pulses (steady with Reduced flashing),
 *    faint red stripes hatch the top three rows, and the next piece's
 *    outline shows in red where it would come in when it would not fit.
 *  - While a line clear replays, the stack is the board as the piece was
 *    stamped: the full rows flash (white, or a soft tint of the piece's
 *    colour with Reduced flashing) and their cells zip out; then the live
 *    board's rows fall into place from where they were. A Sweep's bottom
 *    rows go as its green bar crosses them (effects.c draws the bar), or
 *    flash green and cut with Reduced motion.
 *  - The landed piece: a hard drop's smear, two-frame squash and landing
 *    flash; a natural lock's 1 px settle and brighter bevel.
 *  - The ghost: an outline and a light fill, with marks on the frame at
 *    both sides of the rows it would complete; hidden while cleared rows
 *    still fall.
 *  - The piece: where feel.h draws it (gliding, sliding in, the turn's pop),
 *    darkening as its lock delay runs out, with the soft drop's streaks.
 *  - The ghost can be an outline (as it always was), solid, or off, and the
 *    grid on, faint or off (Settings > Look; the pause menu turns the ghost
 *    off and on). With Block patterns on, every block carries its piece's
 *    mark (look.h).
 *  - Dig's garbage (3.4.0) is grey with no pattern (the cell value past the
 *    pieces'); after a row pushes up the stack is drawn a cell low and
 *    eases up into place (feel.h; a cut with Reduced motion).
 *  - The end show (results.h): a top-out's blocked piece blinks red (steady
 *    with Reduced flashing) and the rows go grey from the bottom up; a Sprint
 *    clear's rows go from the bottom up (effects.c bursts them); at Ultra's
 *    time up the stack dims. */
#include "renderpriv.h"
#include "blockdraw.h"
#include "comfort.h"
#include "danger.h"
#include "feel.h"
#include <math.h>

static int Px(float cells, int cell) { return (int)floorf(cells * (float)cell + 0.5f); }

static int sGrid = GRID_ON; /* the grid the frame drew (the end show redraws parts of it) */

Color Well_GridColor(int grid) {
    if (grid == GRID_FAINT) return Ui_Lerp(kBoardBg, kGrid, 0.45f);
    if (grid == GRID_OFF) return kBoardBg;
    return kGrid;
}

/* The light that sits over a flashing cell: near white, or with Reduced
 * flashing a tint of the piece that cleared (never above 40%). */
static Color FlashColor(Color piece, float k, bool calm) {
    if (calm) return Fade(Ui_Lerp(piece, WHITE, 0.35f), 0.40f * k);
    return Fade((Color){248, 246, 255, 255}, 0.92f * k);
}

static void Overlay(const PlayLayout *l, int x, int y, Color c) { DrawRectangle(x + 1, y + 1, l->cell - 2, l->cell - 2, c); }

static bool IsClearedRow(const ClearReplay *r, int row) {
    for (int i = 0; i < r->count && i < 4; i++)
        if (r->rows[i] == row) return true;
    return false;
}

void Well_DrawFrame(const Game *g, const PlayLayout *l, const HudFx *hud, const Feel *f, int ghost, int grid, const Danger *d) {
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH, cell = l->cell;
    float level = HudFx_FrameFlash(hud), quad = Feel_QuadFrame(f);
    if (Comfort_Calm()) {
        level *= 0.5f;
        quad *= 0.5f;
    }
    Color frame = kRule;
    /* Danger first: the frame eased to red, pulsing (steady with Reduced
     * flashing); a quad's gold or a level up's violet still flash over it. */
    float red = d ? Danger_Frame(d, Comfort_Calm()) : 0.0f;
    if (red > 0.0f) frame = Ui_Lerp(kRule, kDanger, red);
    if (quad > 0.0f) frame = Ui_Lerp(frame, kGold, quad); /* a quad outshines the level up it brings */
    else if (level > 0.0f) frame = Ui_Lerp(frame, kLight, level);
    DrawRectangle(bx - LAYOUT_FRAME, by - LAYOUT_FRAME, bw + 2 * LAYOUT_FRAME, bh + 2 * LAYOUT_FRAME, frame);
    DrawRectangle(bx, by, bw, bh, kBoardBg);
    /* The grid: On, Faint or Off (Settings > Look); a level up still
     * brightens what there is of it. */
    sGrid = grid;
    float glow = HudFx_GridGlow(hud);
    Color lines = Well_GridColor(grid);
    if (glow > 0.0f && grid != GRID_OFF) lines = Ui_Lerp(lines, kRule, glow * (grid == GRID_FAINT ? 0.5f : 1.0f));
    if (grid != GRID_OFF || glow > 0.0f) {
        if (grid == GRID_OFF) lines = Ui_Lerp(kBoardBg, kRule, glow * 0.5f);
        for (int x = 1; x < g->board.w; x++) DrawRectangle(bx + x * cell, by, 1, bh, lines);
        for (int y = 1; y < g->board.h; y++) DrawRectangle(bx, by + y * cell, bw, 1, lines);
    }

    /* The rows the ghost would complete: a light mark on the frame at both
     * sides of each (none while cleared rows still fall). */
    if (ghost == GHOST_OFF || g->phase != GS_PLAYING || !Feel_ShowGhost(f)) return;
    int rows[4];
    int n = Feel_GhostRows(g, Game_GhostY(g), rows);
    int h = cell - 4 > 4 ? cell - 4 : 4;
    for (int i = 0; i < n; i++) {
        int y = Rp_CellY(l, rows[i]) + (cell - h) / 2;
        DrawRectangle(bx - LAYOUT_FRAME, y, LAYOUT_FRAME, h, kLight);
        DrawRectangle(bx + bw, y, LAYOUT_FRAME, h, kLight);
    }
}

/* ------------------------------------------------------------ the stack */

/* The board as the piece was stamped: the full rows flash, then zip out;
 * a Sweep's rows go under its bar (or flash green with Reduced motion). */
static void DrawBefore(const PlayLayout *l, const Feel *f, ClearStage stage, bool calm) {
    const ClearReplay *r = &f->clear;
    const Board *b = &r->before;
    Color clearColor = Render_PieceColor(r->lockType), sweepColor = Rp_PowerColor(PU_SWEEP);
    float zipK = r->times.zip > 0.0f ? (r->t - r->times.flash) / r->times.zip : 1.0f;
    bool sweepCut = r->sweepCount > 0 && r->times.sweep <= 0.0f; /* Reduced motion: the rows flash, then cut */
    for (int row = BOARD_HIDDEN; row < Board_TotalH(b); row++) {
        bool cleared = IsClearedRow(r, row), swept = Feel_IsSweepRow(r, row);
        for (int col = 0; col < b->w; col++) {
            int8_t v = b->cells[row][col];
            if (v == CELL_EMPTY) continue;
            if (cleared && stage != CLEAR_FLASH && Feel_ClearCellGone(r, col, r->t)) continue;
            if (swept && stage == CLEAR_SWEEP && Feel_SweepCellGone(r, col, r->t)) continue;
            int x = Rp_CellX(l, col), y = Rp_CellY(l, row);
            Render_PieceBlock(x, y, l->cell, (PieceType)v, Render_PieceColor((PieceType)v), 1.0f);
            if (swept && (stage == CLEAR_SWEEP || (sweepCut && stage == CLEAR_FLASH))) {
                /* Lit green ahead of the bar (a soft tint with Reduced flashing). */
                Overlay(l, x, y, Fade(sweepColor, calm ? 0.30f : 0.50f));
                continue;
            }
            if (!cleared) continue;
            /* The flash, then the cells still waiting their turn stay lit. */
            float k = stage == CLEAR_FLASH ? 1.0f : 0.6f * (1.0f - zipK);
            if (k > 0.0f) Overlay(l, x, y, FlashColor(clearColor, k, calm));
        }
    }
}

/* Is (col, row) one of the landed piece's blocks on the live board? */
static bool IsLanded(const LandFeel *land, const Cell *cells, int col, int row) {
    for (int i = 0; i < CELLS_PER_PIECE; i++)
        if (land->x + cells[i].x == col && land->liveY + cells[i].y == row) return true;
    return false;
}

/* A bevelled block of any shape: the squash. */
static void BlockRect(int x, int y, int w, int h, Color c) {
    DrawRectangle(x + 1, y + 1, w - 2, h - 2, c);
    int bevel = w >= 24 ? 4 : 2;
    Color hi = Ui_Lerp(c, WHITE, 0.33f), lo = Ui_Lerp(c, BLACK, 0.33f);
    DrawRectangle(x + 1, y + 1, w - 2, bevel, hi);
    DrawRectangle(x + 1, y + 1, bevel, h - 2, hi);
    DrawRectangle(x + 1, y + h - 1 - bevel, w - 2, bevel, lo);
    DrawRectangle(x + w - 1 - bevel, y + 1, bevel, h - 2, lo);
}

/* The live board; while the clear's rows fall, each row from where it was. */
static void DrawLive(const Game *g, const PlayLayout *l, const Feel *f, bool falling) {
    const LandFeel *land = &f->land;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(land->type, land->rot, cells);
    bool squash = Feel_Squash(f);
    int settle = Feel_Settle(f);
    bool moving = squash || settle > 0; /* the landed piece is drawn on its own */
    int rise = Feel_RiseDrop(f, l->cell); /* Dig: the stack easing up after a rise */
    for (int row = BOARD_HIDDEN; row < Board_TotalH(&g->board); row++) {
        int lift = (falling ? Feel_RowLift(f, row, l->cell) : 0) - rise;
        for (int col = 0; col < g->board.w; col++) {
            int8_t v = g->board.cells[row][col];
            if (v == CELL_EMPTY) continue;
            Color c = Render_PieceColor((PieceType)v);
            int x = Rp_CellX(l, col), y = Rp_CellY(l, row) - lift;
            if (!moving || !IsLanded(land, cells, col, row)) {
                Render_PieceBlock(x, y, l->cell, (PieceType)v, c, 1.0f);
                continue;
            }
            if (squash) BlockRect(x - SQUASH_PX / 2, y + SQUASH_PX, l->cell + SQUASH_PX, l->cell - SQUASH_PX, c); /* 2 px flatter and wider */
            else Render_PieceBlock(x, y + settle, l->cell, (PieceType)v, c, 1.0f);
        }
    }
}

/* A hard drop's smear: from each column's top block where it started to
 * where it landed, fading up towards the start. */
static void DrawSmear(const PlayLayout *l, const Feel *f) {
    float k = Feel_Smear(f);
    if (k <= 0.0f) return;
    const LandFeel *land = &f->land;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(land->type, land->rot, cells);
    Color c = Render_PieceColor(land->type);
    int end = land->replayed ? land->y : land->liveY, start = end - (land->y - land->fromY);
    int w = l->cell * 3 / 5;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        bool top = true;
        for (int j = 0; j < CELLS_PER_PIECE; j++)
            if (cells[j].x == cells[i].x && cells[j].y < cells[i].y) top = false;
        if (!top) continue;
        int x = Rp_CellX(l, land->x + cells[i].x) + (l->cell - w) / 2;
        int y0 = Rp_CellY(l, start + cells[i].y), y1 = Rp_CellY(l, end + cells[i].y);
        const int bands = 6;
        for (int b = 0; b < bands; b++) {
            int ya = y0 + (y1 - y0) * b / bands, yb = y0 + (y1 - y0) * (b + 1) / bands;
            DrawRectangle(x, ya, w, yb - ya, Fade(c, 0.45f * k * (float)(b + 1) / (float)bands));
        }
    }
}

/* The landed piece lights up: a hard drop's flash (white, or a tint with
 * Reduced flashing), a natural lock's brighter bevel (never white). While a
 * clear flashes it is on the stamped board; while rows fall it waits. */
static void DrawLanding(const Game *g, const PlayLayout *l, const Feel *f, ClearStage stage, bool calm) {
    const LandFeel *land = &f->land;
    float impact = Feel_Impact(f), bevel = Feel_Bevel(f);
    if (impact <= 0.0f && bevel <= 0.0f) return;
    bool onBefore = stage == CLEAR_FLASH || stage == CLEAR_ZIP || stage == CLEAR_SWEEP;
    if (stage == CLEAR_DROP || (land->replayed && !onBefore)) return;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(land->type, land->rot, cells);
    Color pc = Render_PieceColor(land->type);
    Color c = impact > 0.0f ? (calm ? Fade(Ui_Lerp(pc, WHITE, 0.35f), 0.30f * impact) : (Color){255, 255, 255, (unsigned char)(200.0f * impact)})
                            : Fade(Ui_Lerp(pc, WHITE, 0.45f), 0.5f * bevel);
    bool squash = Feel_Squash(f);
    int settle = Feel_Settle(f);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int col = land->x + cells[i].x;
        int row = (onBefore ? land->y : land->liveY) + cells[i].y;
        if (row < BOARD_HIDDEN || row >= BOARD_HIDDEN + g->board.h || col < 0 || col >= g->board.w) continue;
        if (onBefore) {
            if (IsClearedRow(&f->clear, row)) continue; /* those rows flash on their own */
            if (Feel_IsSweepRow(&f->clear, row) && (stage == CLEAR_SWEEP || f->clear.times.sweep <= 0.0f)) continue; /* lit green, or gone */
        } else if (g->board.cells[row][col] == CELL_EMPTY) {
            continue; /* a Bomb or a Laser took it */
        }
        int x = Rp_CellX(l, col), y = Rp_CellY(l, row);
        if (squash && !onBefore) DrawRectangle(x - SQUASH_PX / 2 + 1, y + SQUASH_PX + 1, l->cell + SQUASH_PX - 2, l->cell - SQUASH_PX - 2, c);
        else Overlay(l, x, y + (onBefore ? 0 : settle), c);
    }
}

/* ------------------------------------------------------------ ghost and piece */

/* The ghost: an outline with a light fill (as it always was), or solid
 * (Settings > Look): a dimmed block, its pattern too. */
static void DrawGhost(const Game *g, const PlayLayout *l, const Cell *cells, Color base, int style) {
    int ghostY = Game_GhostY(g);
    if (ghostY == g->py) return;
    Color outline = base, fill = Fade(base, 0.12f);
    outline.a = 150;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int row = ghostY + cells[i].y;
        if (row < BOARD_HIDDEN) continue;
        int x = Rp_CellX(l, g->px + cells[i].x), y = Rp_CellY(l, row);
        if (style == GHOST_SOLID) {
            Render_PieceBlock(x, y, l->cell, g->current, Ui_Lerp(base, kBoardBg, 0.55f), 1.0f);
            continue;
        }
        DrawRectangle(x + 2, y + 2, l->cell - 4, l->cell - 4, fill);
        DrawRectangleLinesEx((Rectangle){(float)x + 2, (float)y + 2, (float)l->cell - 4, (float)l->cell - 4}, 2, outline);
    }
}

static void DrawPiece(const Game *g, const PlayLayout *l, const Feel *f, const Cell *cells, Color base) {
    float fx, fy;
    Feel_PieceOffset(f, &fx, &fy);
    int ox = Px(fx, l->cell), oy = Px(fy, l->cell);
    int inset = Feel_PopInset(f);
    float fill = Feel_SpawnFill(f), shade = Feel_LockShade(g);
    Color c = shade > 0.0f ? Ui_Lerp(base, BLACK, shade) : base; /* about to stick */

    /* The soft drop's streaks, above each column's top block. */
    float streak = Feel_Streak(f);
    if (streak > 0.0f) {
        int len = (int)((float)l->cell * 0.7f * streak);
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            bool top = true;
            for (int j = 0; j < CELLS_PER_PIECE; j++)
                if (cells[j].x == cells[i].x && cells[j].y < cells[i].y) top = false;
            int row = g->py + cells[i].y;
            if (!top || row < BOARD_HIDDEN) continue;
            int x = Rp_CellX(l, g->px + cells[i].x) + ox, y = Rp_CellY(l, row) + oy;
            DrawRectangle(x + l->cell / 3 - 1, y - len, 2, len, Fade(base, 0.6f * streak));
            DrawRectangle(x + l->cell * 2 / 3 - 1, y - len, 2, len, Fade(base, 0.6f * streak));
        }
    }

    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int row = g->py + cells[i].y;
        if (row < BOARD_HIDDEN) continue; /* still above the visible field */
        int x = Rp_CellX(l, g->px + cells[i].x) + ox, y = Rp_CellY(l, row) + oy;
        Render_PieceBlock(x + inset, y + inset, l->cell - 2 * inset, g->current, c, fill);
        if (fill < 1.0f) DrawRectangleLinesEx((Rectangle){(float)x + 1, (float)y + 1, (float)l->cell - 2, (float)l->cell - 2}, 2, c);
    }
    if (g->curPower != PU_NONE) {
        int idx = g->curPowerCell % CELLS_PER_PIECE;
        int row = g->py + cells[idx].y;
        if (row >= BOARD_HIDDEN) Rp_PowerMark(Rp_CellX(l, g->px + cells[idx].x) + ox, Rp_CellY(l, row) + oy, l->cell, g->curPower, false);
    }
}

/* ------------------------------------------------------------ danger */

/* Faint red hatching over the top rows while the stack is near the top,
 * the top row strongest: 2 px bands stepping 2 px across every 2 px down
 * (a 45-degree hatch), under the stack, so a block on those rows hides
 * it. Still, in both Motion settings. */
static void DrawDangerStripes(const Game *g, const PlayLayout *l, const Danger *d) {
    float k = d ? Danger_Stripes(d) : 0.0f;
    if (k <= 0.0f) return;
    int rows = DANGER_STRIPE_ROWS < g->board.h ? DANGER_STRIPE_ROWS : g->board.h;
    for (int i = 0; i < rows; i++) {
        int y = Rp_CellY(l, BOARD_HIDDEN + i);
        float a = k * (0.22f - 0.06f * (float)i);
        DrawRectangle(l->boardX, y, l->boardW, l->cell, Fade(kDanger, a * 0.35f));
        for (int sy = 0; sy < l->cell; sy += 2) {
            int phase = ((i * l->cell + sy) / 2 * 2) % 8;
            for (int x = phase; x < l->boardW; x += 8) {
                int w = x + 2 <= l->boardW ? 2 : l->boardW - x;
                DrawRectangle(l->boardX + x, y + sy, w, 2, Fade(kDanger, a));
            }
        }
    }
}

/* The next piece, unturned, where pieces come in, in red: it would not fit
 * there now. Pulses with the frame (steady with Reduced flashing). */
static void DrawNextOutline(const Game *g, const PlayLayout *l, const Danger *d) {
    if (!d || !d->nextBlocked) return;
    float k = Danger_Frame(d, Comfort_Calm());
    if (k <= 0.0f) return;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->next, 0, cells);
    int px = (g->board.w - 4) / 2;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int row = BOARD_HIDDEN + cells[i].y, col = px + cells[i].x;
        if (col < 0 || col >= g->board.w || row >= BOARD_HIDDEN + g->board.h) continue;
        int x = Rp_CellX(l, col), y = Rp_CellY(l, row);
        DrawRectangle(x + 2, y + 2, l->cell - 4, l->cell - 4, Fade(kDanger, 0.12f * k));
        DrawRectangleLinesEx((Rectangle){(float)x + 2, (float)y + 2, (float)l->cell - 4, (float)l->cell - 4}, 2, Fade(kDanger, 0.45f + 0.5f * k));
    }
}

void Well_DrawInside(const Game *g, const PlayLayout *l, const Feel *f, int ghost, const Danger *d) {
    bool calm = Comfort_Calm();
    ClearStage stage = Feel_ClearStage(f);
    DrawDangerStripes(g, l, d);
    DrawSmear(l, f);
    if (stage == CLEAR_FLASH || stage == CLEAR_ZIP || stage == CLEAR_SWEEP) DrawBefore(l, f, stage, calm);
    else DrawLive(g, l, f, stage == CLEAR_DROP);
    DrawLanding(g, l, f, stage, calm);

    if (g->phase != GS_PLAYING) return;
    Color pieceColor = Render_PieceColor(g->current);
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->current, g->rotation, cells);
    if (ghost != GHOST_OFF && Feel_ShowGhost(f)) DrawGhost(g, l, cells, pieceColor, ghost);
    DrawPiece(g, l, f, cells, pieceColor);
    DrawNextOutline(g, l, d);
}

/* ------------------------------------------------------------- the end */

/* The well's own grid over a stretch of it (where rows were cleared away). */
static void GridOver(const Game *g, const PlayLayout *l, int x, int y, int w, int h) {
    if (sGrid == GRID_OFF) return;
    Color kGridNow = Well_GridColor(sGrid);
    for (int c = 1; c < g->board.w; c++) {
        int gx = l->boardX + c * l->cell;
        if (gx >= x && gx < x + w) DrawRectangle(gx, y, 1, h, kGridNow);
    }
    for (int r = 0; r <= g->board.h; r++) {
        int gy = l->boardY + r * l->cell;
        if (gy > l->boardY && gy >= y && gy < y + h) DrawRectangle(x, gy, w, 1, kGridNow);
    }
}

void Well_DrawEnd(const Game *g, const PlayLayout *l, const Results *end) {
    if (!end || !end->on) return;
    int rows = g->board.h, turned = Results_RowsTurned(end, rows);
    int bottom = BOARD_HIDDEN + rows - 1;
    switch (end->card.kind) {
        case END_TOPOUT: {
            /* The rows go grey from the bottom up... */
            Color grey = Ui_Lerp(kRule, kText, 0.22f);
            for (int i = 0; i < turned; i++) {
                int row = bottom - i;
                for (int col = 0; col < g->board.w; col++)
                    if (g->board.cells[row][col] != CELL_EMPTY) Render_BlockAt(Rp_CellX(l, col), Rp_CellY(l, row), l->cell, grey, 1.0f);
            }
            /* ...after the piece that could not come in blinks red. */
            if (Results_PieceRed(end, Comfort_Calm())) {
                Cell cells[CELLS_PER_PIECE];
                Tetromino_GetCells(g->current, g->rotation, cells);
                for (int i = 0; i < CELLS_PER_PIECE; i++) {
                    int row = g->py + cells[i].y;
                    if (row < BOARD_HIDDEN) continue;
                    int x = Rp_CellX(l, g->px + cells[i].x), y = Rp_CellY(l, row);
                    Render_BlockAt(x, y, l->cell, kDanger, 0.9f);
                    DrawRectangleLinesEx((Rectangle){(float)x + 1, (float)y + 1, (float)l->cell - 2, (float)l->cell - 2}, 2, kDanger);
                }
            }
            break;
        }
        case END_SPRINT_CLEAR:
            /* The rows go from the bottom up (they burst: effects.c). */
            if (turned > 0) {
                int y = Rp_CellY(l, bottom - turned + 1), h = turned * l->cell;
                DrawRectangle(l->boardX, y, l->boardW, h, kBoardBg);
                GridOver(g, l, l->boardX, y, l->boardW, h);
            }
            break;
        case END_TIME_UP: {
            float dim = Results_RowsDim(end);
            if (dim > 0.0f) DrawRectangle(l->boardX, l->boardY, l->boardW, l->boardH, Fade(kBoardBg, 0.7f * dim));
            break;
        }
    }
}
