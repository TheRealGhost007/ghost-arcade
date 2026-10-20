#include "layout.h"
#include <stdio.h>
#include <string.h>

bool LRect_Empty(LRect r) { return r.w <= 0 || r.h <= 0; }

bool LRect_Overlaps(LRect a, LRect b) {
    if (LRect_Empty(a) || LRect_Empty(b)) return false;
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool LRect_Inside(LRect inner, LRect outer) {
    if (LRect_Empty(inner)) return true;
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

HudSpec Layout_Spec(GameMode mode, bool modernScoring, bool showCard) {
    HudSpec s;
    memset(&s, 0, sizeof(s));
    s.chips = modernScoring;
    s.bars = mode == MODE_POWER;
    s.card = showCard;
    s.pipsStat = -1;
    switch (mode) {
        case MODE_SPRINT:
            s.stat[0] = STAT_LINES_LEFT; s.stat[1] = STAT_TIME; s.stat[2] = STAT_SCORE; s.stat[3] = STAT_BEST;
            s.mainChars = 2; /* "40" */
            break;
        case MODE_ULTRA:
            s.stat[0] = STAT_TIME_LEFT; s.stat[1] = STAT_SCORE; s.stat[2] = STAT_LINES; s.stat[3] = STAT_BEST;
            s.mainChars = 4; /* "1:59" */
            break;
        case MODE_ZEN:
            /* Zen's level never moves (it stays 1), so it has no pips. */
            s.stat[0] = STAT_FRESH; s.stat[1] = STAT_SCORE; s.stat[2] = STAT_LINES; s.stat[3] = STAT_LEVEL;
            s.mainChars = 2;
            break;
        default: /* Marathon, Power */
            s.stat[0] = STAT_SCORE; s.stat[1] = STAT_LEVEL; s.stat[2] = STAT_LINES; s.stat[3] = STAT_BEST;
            s.mainChars = 7; /* "188 450" */
            s.pipsStat = 1;
            break;
    }
    return s;
}

const char *Layout_StatLabel(StatKind k) {
    switch (k) {
        case STAT_SCORE: return "Score";
        case STAT_LEVEL: return "Level";
        case STAT_LINES: return "Lines";
        case STAT_BEST: return "Best";
        case STAT_TIME: return "Time";
        case STAT_TIME_LEFT: return "Time left";
        case STAT_LINES_LEFT: return "Lines left";
        case STAT_FRESH: return "Fresh starts";
        default: return "";
    }
}

/* Press Start 2P's advance: size px a glyph and size/8 between glyphs. */
static int CharsWidth(int chars, int size) { return chars > 0 ? chars * size + (chars - 1) * (size / 8) : 0; }

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Layout_PreviewCells(LRect box, PieceType piece, bool named, int outX[4], int outY[4]) {
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(piece, 0, cells);
    int minX = 99, maxX = -99, minY = 99, maxY = -99;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        if (cells[i].x < minX) minX = cells[i].x;
        if (cells[i].x > maxX) maxX = cells[i].x;
        if (cells[i].y < minY) minY = cells[i].y;
        if (cells[i].y > maxY) maxY = cells[i].y;
    }
    int shapeW = (maxX - minX + 1) * PREVIEW_CELL, shapeH = (maxY - minY + 1) * PREVIEW_CELL;
    int top = box.y + 22, bottom = box.y + box.h - (named ? 18 : 0);
    int originX = box.x + (box.w - shapeW) / 2, originY = top + (bottom - top - shapeH) / 2;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        outX[i] = originX + (cells[i].x - minX) * PREVIEW_CELL;
        outY[i] = originY + (cells[i].y - minY) * PREVIEW_CELL;
    }
}

int Layout_SideCell(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int cell = LAYOUT_MAX_CELL;
    if (SIDE_MAX_W / w < cell) cell = SIDE_MAX_W / w;
    if (SIDE_MAX_H / h < cell) cell = SIDE_MAX_H / h;
    return cell < 12 ? 12 : cell;
}

int Layout_StripCell(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int cell = LAYOUT_MAX_CELL;
    if (STRIP_MAX_W / w < cell) cell = STRIP_MAX_W / w;
    if (STRIP_MAX_H / h < cell) cell = STRIP_MAX_H / h;
    return cell < 12 ? 12 : cell;
}

PanelPlace Layout_PlaceOf(int w, int h, int *cell) {
    int side = Layout_SideCell(w, h), strip = Layout_StripCell(w, h);
    if (strip > side) {
        if (cell) *cell = strip;
        return PLACE_STRIP;
    }
    if (cell) *cell = side;
    return PLACE_SIDE;
}

/* The status items of the side panel: combo and back-to-back on one line
 * where the panel is wide enough, then Freeze and Slow a line each. */
#define COMBO_W 108 /* "Combo x99" and four dots */
#define B2B_W 80    /* "B2B ready" */
static int SideStatus(PlayLayout *l, const HudSpec *s, int y) {
    int px = l->panel.x, pw = l->panel.w;
    if (s->chips) {
        l->combo = (LRect){px, y, COMBO_W, 8};
        if (COMBO_W + 12 + B2B_W <= pw) {
            l->b2b = (LRect){px + COMBO_W + 12, y, B2B_W, 8};
        } else {
            y += STATUS_PITCH;
            l->b2b = (LRect){px, y, B2B_W, 8};
        }
        y += STATUS_PITCH;
    }
    if (s->bars) {
        l->freeze = (LRect){px, y, pw, 8};
        y += STATUS_PITCH;
        l->slow = (LRect){px, y, pw, 8};
        y += STATUS_PITCH;
    }
    return y - STATUS_PITCH + 8; /* the last line's bottom */
}

static void SidePanel(PlayLayout *l, const HudSpec *s) {
    int px = l->panel.x, pw = l->panel.w;
    int y = SIDE_BOARD_Y - LAYOUT_FRAME;
    if (pw >= SIDE_ROOMY_W) {
        int bw = (pw - 8) / 2;
        l->next = (LRect){px, y, bw, PREVIEW_BOX_H};
        l->hold = (LRect){px + pw - bw, y, bw, PREVIEW_BOX_H};
        y += PREVIEW_BOX_H;
    } else {
        l->next = (LRect){px, y, pw, PREVIEW_BOX_H};
        l->hold = (LRect){px, y + PREVIEW_BOX_H + 8, pw, PREVIEW_BOX_H};
        y += 2 * PREVIEW_BOX_H + 8;
    }
    y += 16;
    for (int i = 0; i < HUD_STATS; i++) {
        int size = (i == 0 && CharsWidth(s->mainChars, 32) <= pw) ? 32 : 16;
        int h = 12 + size + (s->pipsStat == i ? 8 : 0);
        l->stat[i] = (LRect){px, y, pw, h};
        l->statSize[i] = size;
        y += h + 12;
    }
    int bottom = y - 12;
    if (s->chips || s->bars) bottom = SideStatus(l, s, y);

    /* The card's last line of text sits 12 px above the well's bottom edge,
     * as it always did, and its divider (12 px above the first row) always
     * clears the status lines. */
    int rowsH = (CARD_ROWS - 1) * CARD_PITCH + CARD_ROW_BOTTOM;
    int rowY = l->boardY + l->boardH - 12 - ((CARD_ROWS - 1) * CARD_PITCH + 8);
    if (rowY < bottom + 24) rowY = bottom + 24;
    l->cardCols = 1;
    l->cardRowsPerCol = CARD_ROWS;
    l->cardRowY = rowY;
    l->showCard = s->card && rowY + rowsH <= LAYOUT_FOOTER_Y - 2;
    if (l->showCard) l->card = (LRect){px, rowY - 12, pw, 12 + rowsH};
}

static void StripPanel(PlayLayout *l, const HudSpec *s) {
    int x0 = LAYOUT_MARGIN;
    l->next = (LRect){x0, STRIP_Y, STRIP_BOX_W, PREVIEW_BOX_H};
    l->hold = (LRect){x0 + STRIP_BOX_W + 8, STRIP_Y, STRIP_BOX_W, PREVIEW_BOX_H};
    /* Four stats in two columns of two: the main number first. */
    int sx = x0 + 2 * STRIP_BOX_W + 8 + 16, colW = (LAYOUT_WINDOW_W - LAYOUT_MARGIN - sx - 12) / 2;
    for (int i = 0; i < HUD_STATS; i++) {
        int col = i / 2, row = i % 2;
        l->stat[i] = (LRect){sx + col * (colW + 12), STRIP_Y + row * 44, colW, 12 + 16 + (s->pipsStat == i ? 8 : 0)};
        l->statSize[i] = 16;
    }
    /* One status row under the strip: a slot each. */
    int slot = 0, sy = STRIP_Y + PREVIEW_BOX_H + 8;
    LRect *items[4] = {s->chips ? &l->combo : NULL, s->chips ? &l->b2b : NULL, s->bars ? &l->freeze : NULL, s->bars ? &l->slow : NULL};
    for (int i = 0; i < 4; i++) {
        if (!items[i]) continue;
        *items[i] = (LRect){x0 + slot * STRIP_SLOT_W, sy, STRIP_SLOT_W - 8, 8};
        slot++;
    }
    /* The card under the well, in as few columns as fit above the footer. */
    int rowY = l->boardY + l->boardH + LAYOUT_FRAME + 22;
    l->cardRowY = rowY;
    l->showCard = false;
    if (!s->card) return;
    for (int cols = 2; cols <= 4; cols++) {
        int per = (CARD_ROWS + cols - 1) / cols;
        int rowsH = (per - 1) * CARD_PITCH + CARD_ROW_BOTTOM;
        if (rowY + rowsH > LAYOUT_FOOTER_Y - 2) continue;
        l->cardCols = cols;
        l->cardRowsPerCol = per;
        l->showCard = true;
        l->card = (LRect){x0, rowY - 12, STRIP_MAX_W, 12 + rowsH};
        return;
    }
}

PlayLayout Layout_Play(int w, int h, const HudSpec *spec) {
    PlayLayout l;
    memset(&l, 0, sizeof(l));
    HudSpec none;
    if (!spec) {
        none = Layout_Spec(MODE_MARATHON, false, true);
        spec = &none;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    l.place = Layout_PlaceOf(w, h, &l.cell);
    l.boardW = w * l.cell;
    l.boardH = h * l.cell;
    if (l.place == PLACE_SIDE) {
        l.boardX = LAYOUT_MARGIN;
        l.boardY = SIDE_BOARD_Y;
        l.panel.x = l.boardX + l.boardW + 40;
        l.panel.y = SIDE_BOARD_Y - LAYOUT_FRAME;
        l.panel.w = LAYOUT_WINDOW_W - LAYOUT_MARGIN - l.panel.x;
        l.panel.h = LAYOUT_FOOTER_Y - l.panel.y;
        SidePanel(&l, spec);
    } else {
        l.boardX = LAYOUT_MARGIN + (STRIP_MAX_W - l.boardW) / 2;
        l.boardY = STRIP_BOARD_Y;
        l.panel = (LRect){LAYOUT_MARGIN, STRIP_Y, STRIP_MAX_W, STRIP_BOARD_Y - LAYOUT_FRAME - 4 - STRIP_Y};
        StripPanel(&l, spec);
    }
    l.chip = (LRect){(LAYOUT_WINDOW_W - 280) / 2, LAYOUT_FOOTER_Y, 280, 24};
    return l;
}

LRect Layout_Well(const PlayLayout *l) { return (LRect){l->boardX, l->boardY, l->boardW, l->boardH}; }

LRect Layout_WellFrame(const PlayLayout *l) {
    return (LRect){l->boardX - LAYOUT_FRAME, l->boardY - LAYOUT_FRAME, l->boardW + 2 * LAYOUT_FRAME, l->boardH + 2 * LAYOUT_FRAME};
}

void Layout_CardRow(const PlayLayout *l, int i, int *labelX, int *keysRight, int *y) {
    int cols = l->cardCols > 0 ? l->cardCols : 1, per = l->cardRowsPerCol > 0 ? l->cardRowsPerCol : CARD_ROWS;
    int col = i / per, row = i % per;
    int colW = l->card.w / cols;
    int x = l->card.x + col * colW;
    if (labelX) *labelX = x;
    if (keysRight) *keysRight = col < cols - 1 ? x + colW - 16 : l->card.x + l->card.w;
    if (y) *y = l->cardRowY + row * CARD_PITCH;
}

static int SplitPoint(const char *text) {
    int n = (int)strlen(text), mid = n / 2, best = 0;
    for (int i = 1; i < n; i++) {
        if (text[i] != ' ') continue;
        int d = i > mid ? i - mid : mid - i, bd = best > mid ? best - mid : mid - best;
        if (!best || d < bd) best = i;
    }
    return best; /* the space; line two starts after it */
}

static int LineWidth(const char *text, int from, int to, int size, MeasureFn measure) {
    char buf[96];
    int n = to - from;
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, text + from, (size_t)n);
    buf[n] = '\0';
    return measure(buf, size);
}

TextFit Layout_FitText(const char *text, int maxW, int maxSize, MeasureFn measure) {
    static const int kSizes[3] = {32, 16, 8};
    TextFit f = {8, 0, 0, false};
    if (!text) text = "";
    int n = (int)strlen(text);
    int space = SplitPoint(text);
    for (int k = 0; k < 3; k++) {
        int size = kSizes[k];
        if (size > maxSize) continue;
        int one = measure(text, size);
        if (one <= maxW) return (TextFit){size, 0, one, true};
        if (size == 32 || !space) continue; /* big text stays on one line */
        int a = LineWidth(text, 0, space, size, measure), b = LineWidth(text, space + 1, n, size, measure);
        int wide = a > b ? a : b;
        if (wide <= maxW) return (TextFit){size, space + 1, wide, true};
    }
    f.width = measure(text, 8);
    return f;
}

int Layout_FitHeight(TextFit f) { return f.split ? f.size * 2 + 8 : f.size; }

/* The long form wherever it fits at all, else the short one. */
static TextFit FitCallout(const CalloutText *c, int maxW, int maxSize, MeasureFn measure, bool *useShort) {
    *useShort = false;
    TextFit f = Layout_FitText(c->text, maxW, maxSize, measure);
    if (f.fits || !c->shortText || !c->shortText[0]) return f;
    TextFit s = Layout_FitText(c->shortText, maxW, maxSize, measure);
    if (s.fits) {
        *useShort = true;
        return s;
    }
    return f;
}

void Layout_Callouts(const PlayLayout *l, const CalloutText *lines, int count, MeasureFn measure, CalloutPlace *out) {
    if (count > CALLOUT_MAX) count = CALLOUT_MAX;
    if (count <= 0) return;
    int maxW = l->boardW - 2 * CALLOUT_PAD_X - 8;
    int room = l->boardH - 8 - CALLOUT_RISE;
    int total = 0;
    /* One size cap for the whole stack, lowered until the stack fits. */
    for (int maxSize = 32;; maxSize /= 2) {
        total = 0;
        for (int i = 0; i < count; i++) {
            int cap = lines[i].maxSize > 0 && lines[i].maxSize < maxSize ? lines[i].maxSize : maxSize;
            out[i].fit = FitCallout(&lines[i], maxW, cap, measure, &out[i].useShort);
            out[i].plateH = Layout_FitHeight(out[i].fit) + 2 * CALLOUT_PAD_Y;
            total += out[i].plateH + (i ? CALLOUT_GAP : 0);
        }
        if (total <= room || maxSize <= 8) break;
    }
    int top = l->boardY + l->boardH * 3 / 10;
    int bottom = l->boardY + l->boardH - 4;
    if (top + total + CALLOUT_RISE > bottom) top = bottom - CALLOUT_RISE - total;
    if (top < l->boardY + 4) top = l->boardY + 4;
    int y = top;
    for (int i = 0; i < count; i++) {
        int rise = Clamp(lines[i].rise, 0, CALLOUT_RISE);
        out[i].plateW = out[i].fit.width + 2 * CALLOUT_PAD_X;
        out[i].plateX = l->boardX + l->boardW / 2 - out[i].plateW / 2;
        out[i].plateY = y + (CALLOUT_RISE - rise);
        out[i].textY = out[i].plateY + CALLOUT_PAD_Y;
        y += out[i].plateH + CALLOUT_GAP;
    }
}

PopupPlace Layout_Popup(const PlayLayout *l, const char *text, int cx, int cy, int rise, MeasureFn measure) {
    PopupPlace p;
    p.size = 16;
    p.w = measure(text, 16);
    if (p.w > l->boardW - 8) {
        p.size = 8;
        p.w = measure(text, 8);
    }
    p.x = Clamp(cx - p.w / 2, l->boardX + 4, l->boardX + l->boardW - 4 - p.w);
    if (p.w > l->boardW - 8) p.x = l->boardX + (l->boardW - p.w) / 2; /* wider than any lock can earn: centred, clipped */
    p.y = Clamp(cy - p.size / 2 - rise, l->boardY + 4, l->boardY + l->boardH - 4 - p.size);
    return p;
}
