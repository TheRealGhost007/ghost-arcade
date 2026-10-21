/* The panel at the side of the well, or the strip across the top of a flat
 * one (layout.c picks whichever gives bigger cells):
 *  - Next and Hold. A used Hold goes grey with a small lock; in Power mode
 *    each box names the power-up its piece carries, and the block shows its
 *    letter. When a piece comes in, the old Next slides out as the new one
 *    slides in; a held piece flies from the well into Hold, and Hold's edge
 *    flashes once when it is ready again (feel.h). The Next queue (3.2.0):
 *    under the large next piece (beside it in the strip) up to five small
 *    ones, the pieces after it, each a quarter size; they move up a slot
 *    with the slide as a piece comes in (a cut with Reduced motion), and a
 *    small block carrying a power-up is drawn in that power's colour.
 *  - Four stats, the first the mode's main number (32 px where it fits):
 *    the score counting up, Sprint's clock with hundredths (the lines left
 *    under it, gold in the last five) and its split line against your best
 *    pace (splits.h), Ultra's countdown (gold at 30 s, red at 10 s), Zen's
 *    fresh starts; Level with its ten pips; Best, which turns gold once the
 *    run passes it.
 *  - Combo and back-to-back chips, lit while they are on; Freeze and Slow as
 *    draining bars (Freeze's blinks in its last 1.5 s, steady with Reduced
 *    flashing). Dig's "Next row" bar (3.4.0) fills over the wait for the
 *    next garbage row and turns red, pulsing, in its last quarter (steady
 *    red with Reduced flashing); the Dug stat counts the garbage rows
 *    cleared.
 *  - The controls card: your keys, or the pad's buttons once the pad was
 *    used last, plus M and - / = (Settings can hide it). */
#include "renderpriv.h"
#include "blockdraw.h"
#include "comfort.h"
#include "danger.h"
#include "menumodel.h"
#include "powerfx.h"
#include "splits.h"
#include "uikit.h"
#include "uikitdraw.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* What the last panel drew for Sprint and Dig (Panel_Inspect). */
static char sTimer[16] = "", sSplit[SPLIT_TEXT_LEN] = "";
static bool sSplitGold = false, sLinesGold = false;
static float sRiseFill = 0.0f;
static bool sRiseWarn = false;

/* A small padlock, 8x8. */
static const char *const kLock[8] = {
    "..XXXX..",
    ".X....X.",
    ".X....X.",
    "XXXXXXXX",
    "XXX..XXX",
    "XXX..XXX",
    "XXXXXXXX",
    "........",
};

static void Pattern(const char *const *rows, int x, int y, Color c) {
    for (int r = 0; r < 8; r++)
        for (int k = 0; k < 8; k++)
            if (rows[r][k] == 'X') DrawRectangle(x + k, y + r, 1, 1, c);
}

/* A box's frame and label (and the lock on a used Hold); its edge lights
 * up while `ready` (Hold is ready again), half as bright with Reduced
 * flashing. */
static void DrawBox(LRect r, const char *label, bool used, float ready) {
    DrawRectangle(r.x, r.y, r.w, r.h, kBoardBg);
    if (Comfort_Calm()) ready *= 0.5f;
    DrawRectangleLinesEx((Rectangle){(float)r.x, (float)r.y, (float)r.w, (float)r.h}, 2.0f, ready > 0.0f ? Ui_Lerp(kRule, kLight, ready) : kRule);
    Ui_Text(label, r.x + 12, r.y + 10, UI_T8, used ? kFaint : kTextDim);
    if (used) Pattern(kLock, r.x + 12 + Ui_Measure(label, UI_T8) + 8, r.y + 10, kTextDim);
}

/* A piece in a box, shifted sideways by shiftX (the Next slide). */
static void DrawPieceIn(LRect r, PieceType piece, PowerUp power, int powerCell, bool used, int shiftX) {
    bool named = power != PU_NONE;
    int xs[CELLS_PER_PIECE], ys[CELLS_PER_PIECE];
    Layout_PreviewCells(r, piece, named, xs, ys);
    Color color = Rp_ColorFromU32(Tetromino_Color(piece));
    if (used) color = Ui_Lerp(color, kRule, 0.7f); /* spent until the next piece */
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        Render_PieceBlock(xs[i] + shiftX, ys[i], PREVIEW_CELL, piece, color, 1.0f);
        if (named && i == powerCell % CELLS_PER_PIECE) Rp_PowerMark(xs[i] + shiftX, ys[i], PREVIEW_CELL, power, used);
    }
    /* The power-up by name: a 4 px dot could not be read. */
    if (named) Ui_TextCentered(Game_PowerName(power), UI_T8, r.x + r.w / 2 + shiftX, r.y + r.h - 16, used ? kFaint : Rp_PowerColor(power));
}

/* The queue's small pieces (3.2.0): slot i holds queue[i + 1]. While Next
 * slides, each comes from the slot after it (in a row; the strip's grid
 * cuts), so the whole queue moves up together. */
static void DrawQueue(const Game *g, const PlayLayout *l, float slideK, bool sliding) {
    int n = l->queueSlots;
    if (n <= 0) return;
    int pitch = 0;
    if (sliding && l->queueCols == n) pitch = Layout_QueueSlot(l, 1).x - Layout_QueueSlot(l, 0).x;
    int shift = (int)((float)pitch * (1.0f - slideK) + 0.5f);
    BeginScissorMode(l->queue.x + 2, l->queue.y, l->queue.w - 4, l->queue.h);
    for (int i = 0; i < n && i + 1 < QUEUE_MAX; i++) {
        PieceType piece = g->queue[i + 1];
        if ((int)piece < 0 || piece >= PIECE_COUNT) continue;
        PowerUp power = g->queuePower[i + 1];
        int cell = l->queueCell;
        int xs[CELLS_PER_PIECE], ys[CELLS_PER_PIECE];
        Layout_QueueCells(l, i, piece, xs, ys);
        Color color = Rp_ColorFromU32(Tetromino_Color(piece));
        for (int c = 0; c < CELLS_PER_PIECE; c++) {
            int x = xs[c] + shift, y = ys[c];
            DrawRectangle(x, y, cell, cell, color);
            if (cell >= 6) DrawRectangle(x + 1, y + 1, cell - 2, cell - 2, Ui_Lerp(color, kBoardBg, 0.25f)); /* a bevel of one pixel */
            if (power != PU_NONE && c == g->queueCell[i + 1] % CELLS_PER_PIECE)
                DrawRectangle(x + 1, y + 1, cell - 2, cell - 2, Rp_PowerColor(power)); /* its power-up, by colour alone: no room for a letter */
        }
    }
    EndScissorMode();
}

static void DrawNext(const Game *g, const PlayLayout *l, const Feel *f) {
    LRect r = l->next, big = l->nextBig;
    DrawBox(r, "Next", false, 0.0f);
    float k;
    if (!Feel_NextSlide(f, &k)) {
        DrawPieceIn(big, g->next, g->nextPower, g->nextPowerCell, false, 0);
        DrawQueue(g, l, 1.0f, false);
        return;
    }
    /* The old preview slides out to the left as the new one comes in from
     * the right, inside the box. */
    int shift = (int)((float)big.w * k + 0.5f);
    BeginScissorMode(big.x + 2, big.y + 2, big.w - 4, big.h - 4);
    DrawPieceIn(big, f->slideOld, f->slideOldPower, f->slideOldCell, false, -shift);
    DrawPieceIn(big, g->next, g->nextPower, g->nextPowerCell, false, big.w - shift);
    EndScissorMode();
    DrawQueue(g, l, k, true);
}

static void DrawHold(const Game *g, LRect r, const Feel *f) {
    DrawBox(r, "Hold", g->holdUsed, Feel_HoldReady(f));
    if (Feel_HoldFlight(f, NULL)) return; /* the piece is still on its way in */
    if ((int)g->hold < 0) {
        Ui_TextCentered("empty", UI_T8, r.x + r.w / 2, r.y + r.h / 2 + 8, kFaint);
        return;
    }
    DrawPieceIn(r, g->hold, g->holdPower, g->holdPowerCell, g->holdUsed, 0);
}

/* The held piece flies from where it was drawn in the well into the box,
 * shrinking from the well's cells to the box's (0.12 s), and greys as it
 * goes (Hold is used until the next piece). */
void Panel_DrawFlight(const Game *g, const PlayLayout *l, const Feel *f) {
    float k;
    if (!Feel_HoldFlight(f, &k)) return;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(f->flightType, f->flightRot, cells);
    int tx[CELLS_PER_PIECE], ty[CELLS_PER_PIECE];
    Layout_PreviewCells(l->hold, f->flightType, f->flightPower != PU_NONE, tx, ty);
    Color color = Rp_ColorFromU32(Tetromino_Color(f->flightType));
    if (g->holdUsed) color = Ui_Lerp(color, Ui_Lerp(color, kRule, 0.7f), k);
    int size = (int)((float)l->cell + (float)(PREVIEW_CELL - l->cell) * k + 0.5f);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        float fx = (float)l->boardX + (f->flightX + (float)cells[i].x) * (float)l->cell;
        float fy = (float)l->boardY + (f->flightY + (float)cells[i].y - (float)BOARD_HIDDEN) * (float)l->cell;
        int x = (int)(fx + ((float)tx[i] - fx) * k + 0.5f), y = (int)(fy + ((float)ty[i] - fy) * k + 0.5f);
        Render_PieceBlock(x, y, size, f->flightType, color, 1.0f);
        if (f->flightPower != PU_NONE && i == f->flightPowerCell % CELLS_PER_PIECE) Rp_PowerMark(x, y, size, f->flightPower, false);
    }
}

static void Clock(float seconds, char *out, size_t size) {
    if (seconds < 0.0f) seconds = 0.0f;
    int s = (int)seconds;
    snprintf(out, size, "%d:%02d", s / 60, s % 60);
}

/* A stat's value and its colour this frame. */
static Color StatValue(StatKind k, const Game *g, const RenderView *v, const HudFx *h, char *buf, size_t size) {
    Color c = kText;
    switch (k) {
        case STAT_SCORE: Menu_Group(h->shown, buf, size); break;
        case STAT_LEVEL: snprintf(buf, size, "%d", g->level); break;
        case STAT_LINES: snprintf(buf, size, "%d", g->linesCleared); break;
        case STAT_LINES_LEFT: {
            int left = g->linesCleared >= SPRINT_LINES ? 0 : SPRINT_LINES - g->linesCleared;
            snprintf(buf, size, "%d", left);
            sLinesGold = Danger_SprintGold(left); /* the last five lines */
            if (sLinesGold) c = kGold;
            break;
        }
        case STAT_TIME: Clock(g->elapsed, buf, size); break;
        case STAT_TIME_LEFT: {
            Clock((float)Hud_UltraSeconds(g->timeLeft), buf, size);
            UltraTint t = Hud_UltraTint(g->timeLeft);
            if (t == ULTRA_GOLD) c = kGold;
            if (t == ULTRA_RED) c = kDanger;
            break;
        }
        case STAT_FRESH: snprintf(buf, size, "%d", g->zenResets); break;
        case STAT_DUG: snprintf(buf, size, "%d", g->dug); break;
        case STAT_UNDO: /* the Practice room: pieces that can be taken back; faint at none */
            snprintf(buf, size, "%d", v->undoLeft);
            if (v->undoLeft <= 0) c = kFaint;
            break;
        case STAT_BEST:
            if (v->best <= 0) snprintf(buf, size, "No best yet"); /* not "Best 0" */
            else if (v->unit == UNIT_TIME) Record_FormatValue(UNIT_TIME, v->best, buf, size); /* Sprint: the time to beat */
            else {
                /* Once passed it turns gold and follows the score up. */
                Menu_Group(h->shown > v->best ? h->shown : v->best, buf, size);
                if (v->passedBest) c = kGold;
            }
            break;
        default: buf[0] = '\0'; break;
    }
    return c;
}

/* Sprint's clock (3.1.0): "1:12" at 32 px with ".40" at 16 px beside it
 * where that fits the panel, else "1:12.40" at 16 or 8; the split line at
 * the right of its label ("10: -1.4 s", gold when ahead of your best pace,
 * or the split's time when there is none to race). */
static void DrawSprintClock(LRect r, int size, const Game *g, const RenderView *v, const HudFx *h) {
    char full[16];
    Record_FormatTime(Splits_Centis(g->elapsed), full, sizeof(full));
    snprintf(sTimer, sizeof(sTimer), "%s", full);
    int lift = HudFx_Nudge(h, STAT_TIME, Comfort_Motion());
    Ui_Text(Layout_StatLabel(STAT_TIME), r.x, r.y, UI_T8, kTextDim);
    sSplit[0] = '\0';
    sSplitGold = false;
    if (v->splits && Splits_Text(v->splits, sSplit, sizeof(sSplit), &sSplitGold))
        Ui_TextRight(sSplit, UI_T8, r.x + r.w, r.y - lift, sSplitGold ? kGold : kTextDim);
    char *dot = strrchr(full, '.');
    if (size == UI_T32 && dot) {
        char big[8];
        snprintf(big, sizeof(big), "%.*s", (int)(dot - full), full);
        int bw = Ui_Measure(big, UI_T32), sw = Ui_Measure(dot, UI_T16);
        if (bw + 4 + sw <= r.w) {
            Ui_Text(big, r.x, r.y + 12 - lift, UI_T32, kText);
            Ui_Text(dot, r.x + bw + 4, r.y + 12 + 16 - lift, UI_T16, kText); /* bottoms level */
            return;
        }
    }
    int s = size > UI_T16 ? UI_T16 : size;
    while (s > UI_T8 && Ui_Measure(full, s) > r.w) s /= 2;
    Ui_Text(full, r.x, r.y + 12 + (size - s) / 2 - lift, s, kText);
}

static void DrawStat(LRect r, int size, StatKind k, const Game *g, const RenderView *v, const HudFx *h, bool pips) {
    if (k == STAT_TIME && g->cfg.mode == MODE_SPRINT) { DrawSprintClock(r, size, g, v, h); return; }
    char buf[32];
    Color c = StatValue(k, g, v, h, buf, sizeof(buf));
    int lift = HudFx_Nudge(h, k, Comfort_Motion());
    Ui_Text(Layout_StatLabel(k), r.x, r.y, UI_T8, kTextDim);
    /* The biggest size the panel has room for: 32, 16, then 8 (the narrow
     * panel of a wide board had "No best yet" reaching into the margin). */
    int s = size;
    while (s > UI_T8 && Ui_Measure(buf, s) > r.w) s /= 2;
    Ui_Text(buf, r.x, r.y + 12 + (size - s) / 2 - lift, s, c);
    if (!pips) return;
    int into = g->linesCleared % LINES_PER_LEVEL, lit = HudFx_PipsLit(h, into, Comfort_Motion());
    int pw = r.w >= 140 ? 12 : 10, py = r.y + 12 + size + 4;
    for (int i = 0; i < LINES_PER_LEVEL; i++) DrawRectangle(r.x + i * pw, py, pw - 4, 4, i < lit ? kLight : kRule);
}

/* A draining bar; `lit` dims it (Freeze's blink in its last 1.5 s, steady
 * with Reduced flashing: powerfx.h). */
static void DrawBar(LRect r, const char *label, float left, float full, Color c, float lit) {
    if (LRect_Empty(r) || left <= 0.0f) return; /* only while it runs */
    Color lc = lit < 1.0f ? Ui_Lerp(kRule, c, lit) : c;
    Ui_Text(label, r.x, r.y, UI_T8, lc);
    int bx = r.x + Ui_Measure(label, UI_T8) + 8, bw = r.x + r.w - bx;
    if (bw < 8) return;
    float k = left / full;
    if (k > 1.0f) k = 1.0f;
    DrawRectangle(bx, r.y + 1, bw, 6, kRule);
    DrawRectangle(bx, r.y + 1, (int)((float)bw * k + 0.5f), 6, lc);
}

/* Dig's "Next row" bar (3.4.0): it fills over the wait for the next garbage
 * row, dim while there is time, then red in the wait's last quarter,
 * pulsing twice a second (steady red with Reduced flashing). Looks only:
 * Game_DigFill is the rules' own clock. */
static void DrawRiseBar(const Game *g, LRect r) {
    if (LRect_Empty(r) || g->cfg.mode != MODE_DIG) return;
    float fill = Game_DigFill(g);
    bool warn = Game_DigWarning(g);
    sRiseFill = fill;
    sRiseWarn = warn;
    Color c = kTextDim;
    if (warn) {
        float pulse = 1.0f;
        if (!Comfort_Calm()) pulse = 0.6f + 0.4f * (0.5f + 0.5f * sinf(g->digTimer * 2.0f * 2.0f * 3.14159265f));
        c = Ui_Lerp(kRule, kDanger, pulse);
    }
    Ui_Text("Next row", r.x, r.y, UI_T8, c);
    int bx = r.x + Ui_Measure("Next row", UI_T8) + 8, bw = r.x + r.w - bx;
    if (bw < 8) return;
    DrawRectangle(bx, r.y + 1, bw, 6, kRule);
    DrawRectangle(bx, r.y + 1, (int)((float)bw * fill + 0.5f), 6, c);
}

static void DrawStatus(const Game *g, const PlayLayout *l) {
    char buf[24];
    if (!LRect_Empty(l->combo)) {
        if (g->combo >= 1) {
            snprintf(buf, sizeof(buf), "Combo x%d", g->combo);
            Ui_Text(buf, l->combo.x, l->combo.y, UI_T8, kLight);
            int x = l->combo.x + Ui_Measure(buf, UI_T8) + 6, dots = g->combo > 4 ? 4 : g->combo;
            for (int i = 0; i < dots; i++) DrawRectangle(x + i * 6, l->combo.y + 2, 4, 4, kLight);
        } else {
            Ui_Text("Combo", l->combo.x, l->combo.y, UI_T8, kFaint);
        }
    }
    if (!LRect_Empty(l->b2b)) {
        /* Lit after a quad: the next quad pays half again. */
        if (g->backToBack) Ui_Text("B2B ready", l->b2b.x, l->b2b.y, UI_T8, kGold);
        else Ui_Text("B2B", l->b2b.x, l->b2b.y, UI_T8, kFaint);
    }
    DrawBar(l->freeze, "Freeze", g->freezeTimer, FREEZE_SECONDS, Rp_PowerColor(PU_FREEZE), PowerFx_FreezeLit(g->freezeTimer, Comfort_Calm()));
    DrawBar(l->slow, "Slow", g->slowTimer, SLOW_SECONDS, Rp_PowerColor(PU_SLOW), 1.0f);
    DrawRiseBar(g, l->rise);
}

/* The pad's buttons for a row, right-aligned, as the menus' hints draw them. */
static void PadCaps(const char *caps, int right, int cy) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", caps);
    const char *parts[4];
    int n = 0;
    for (char *tok = strtok(buf, " "); tok && n < 4; tok = strtok(NULL, " ")) parts[n++] = tok;
    int total = 0;
    for (int i = 0; i < n; i++) total += Kit_CapWidth(parts[i], true) + (i ? 4 : 0);
    int x = right - total;
    for (int i = 0; i < n; i++) {
        KitDraw_Cap(parts[i], true, x, cy);
        x += Kit_CapWidth(parts[i], true) + 4;
    }
}

static void DrawCard(const Game *g, const RenderView *v, const PlayLayout *l) {
    if (!l->showCard) return;
    DrawRectangle(l->card.x, l->card.y, l->card.w, 2, kRule);
    const ControlWords *k = &v->keys;
    /* label, your keys, your pad's buttons (NULL: keyboard only). Pause
     * is one row: your Pause key and Esc (or your Back key), Menu and B.
     * In the Practice room (3.5.0) Undo takes the Volume row. */
    bool practice = g->practice.on;
    const char *rows[CARD_ROWS][3] = {
        {"Move", k->move, k->padMove},       {"Soft drop", k->softDrop, k->padSoft}, {"Rotate", k->rotate, k->padRotate},
        {"Rotate left", k->rotateLeft, k->padRotateLeft}, {"Hard drop", k->hardDrop, k->padHard}, {"Hold", k->hold, k->padHold},
        {"Pause", k->pause, k->padPause},    {"Restart", k->restart, k->padRestart}, {"Mute", "M", NULL},
        {practice ? "Undo" : "Volume", practice ? k->undo : "- =", practice ? k->padUndo : NULL},
    };
    for (int i = 0; i < CARD_ROWS; i++) {
        int x, right, y;
        Layout_CardRow(l, i, &x, &right, &y);
        Ui_Text(rows[i][0], x, y, UI_T8, kTextDim);
        if (v->padHints && rows[i][2]) {
            if (rows[i][2][0]) PadCaps(rows[i][2], right, y + 4);
            else Ui_TextRight("-", UI_T8, right, y, kFaint); /* no button for it */
        } else {
            Ui_TextRight(rows[i][1], UI_T8, right, y, kFaint);
        }
    }
}

void Panel_Inspect(char *timer, size_t tn, char *split, size_t sn, bool *splitGold, bool *linesGold) {
    snprintf(timer, tn, "%s", sTimer);
    snprintf(split, sn, "%s", sSplit);
    *splitGold = sSplitGold;
    *linesGold = sLinesGold;
}

void Panel_InspectRise(float *fill, bool *warn) {
    *fill = sRiseFill;
    *warn = sRiseWarn;
}

void Panel_Draw(const Game *g, const RenderView *v, const PlayLayout *l, const HudFx *h, const Feel *f) {
    HudSpec spec = Layout_SpecFor(g, v->showControls);
    if (g->cfg.mode != MODE_SPRINT) { sTimer[0] = sSplit[0] = '\0'; sSplitGold = sLinesGold = false; }
    if (g->cfg.mode != MODE_DIG) { sRiseFill = 0.0f; sRiseWarn = false; }
    DrawNext(g, l, f);
    DrawHold(g, l->hold, f);
    for (int i = 0; i < HUD_STATS; i++) DrawStat(l->stat[i], l->statSize[i], spec.stat[i], g, v, h, spec.pipsStat == i);
    DrawStatus(g, l);
    DrawCard(g, v, l);
}
