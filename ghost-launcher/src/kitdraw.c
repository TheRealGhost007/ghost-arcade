#include "kitdraw.h"
#include "palette.h"
#include "pixtext.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const Color kShade = {14, 11, 30, 255}; /* the dark lower edge of a bevel */

static int Ix(float v) { return (int)roundf(v); }

static void Fill(KitRect r, Color c) { DrawRectangle(Ix(r.x), Ix(r.y), Ix(r.w), Ix(r.h), c); }

/* Light edge top-left, dark edge bottom-right: a raised block. */
static void Bevel(KitRect r, Color base, Color hi, Color lo) {
    int x = Ix(r.x), y = Ix(r.y), w = Ix(r.w), h = Ix(r.h);
    DrawRectangle(x, y, w, h, base);
    DrawRectangle(x, y, w, 2, hi);
    DrawRectangle(x, y, 2, h, hi);
    DrawRectangle(x, y + h - 2, w, 2, lo);
    DrawRectangle(x + w - 2, y, 2, h, lo);
}

static void Bar(KitRect r, Color c) { DrawRectangle(Ix(r.x), Ix(r.y), 4, Ix(r.h), c); }

/* 8x8 pixel icons, drawn in the label's colour. */
static const char kIconBack[8][9] = {
    "...X....", "..XX....", ".XXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XXXXXXX", "..XX....", "...X....",
};
static const char kIconMenu[8][9] = {
    "XXXXXXXX", "XXXXXXXX", "........", "XXXXXXXX", "XXXXXXXX", "........", "XXXXXXXX", "XXXXXXXX",
};

/* The pad's View button: two windows, one over the other. */
static const char kIconView[8][9] = {
    "XXXXX...", "X...X...", "X..XXXXX", "X..X...X", "XXXX...X", "...X...X", "...XXXXX", "........",
};

/* A favourite's star: gold and solid when it is one, an outline when not. */
static const char kStarOn[8][9] = {
    "...XX...", "...XX...", "XXXXXXXX", ".XXXXXX.", "..XXXX..", "..XXXX..", ".XX..XX.", ".X....X.",
};
static const char kStarOff[8][9] = {
    "...XX...", "...XX...", "XXX..XXX", ".X....X.", "..X..X..", "..X..X..", ".X.XX.X.", ".X....X.",
};

/* The animations' clock: the hall sets it, and holds it still while it
 * rests behind a game. Until it is set, raylib's own time. */
static double sClock = -1.0;

void KitDraw_SetClock(double seconds) { sClock = seconds; }
double KitDraw_Clock(void) { return sClock >= 0.0 ? sClock : GetTime(); }

static void Pattern(const char (*p)[9], int x, int y, Color c) {
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (p[row][col] == 'X') DrawRectangle(x + col, y + row, 1, 1, c);
}

void KitDraw_Star(int x, int y, int scale, bool on, Color c) {
    const char (*p)[9] = on ? kStarOn : kStarOff;
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (p[row][col] == 'X') DrawRectangle(x + col * scale, y + row * scale, scale, scale, c);
}

static void Icon(KitIcon icon, int x, int y, Color c) {
    const char (*p)[9] = icon == KIT_ICON_BACK ? kIconBack : (icon == KIT_ICON_MENU ? kIconMenu : (icon == KIT_ICON_STAR ? kStarOn : NULL));
    if (p) Pattern(p, x, y, c);
}

typedef struct {
    Color fill, hi, lo, text;
    bool bar;
    int sink;
} BlockLook;

static BlockLook BlockColours(KitLook look) {
    switch (look) {
        case KIT_LOOK_HOVER: return (BlockLook){Pal_Mix(kCabinet, kCabinetEdge, 0.5f), Pal_Mix(kCabinetEdge, kText, 0.4f), kShade, kWarmWhite, false, 0};
        case KIT_LOOK_FOCUSED: return (BlockLook){kPanel, kPanel, kPanel, kPink, true, 0};
        case KIT_LOOK_PRESSED: return (BlockLook){kPink, Pal_Mix(kPink, kInk, 0.35f), kPink, kInk, false, 2};
        case KIT_LOOK_DISABLED: return (BlockLook){Fade(kCabinetDark, 0.5f), Fade(kCabinetDark, 0.5f), Fade(kCabinetDark, 0.5f), kFaint, false, 0};
        default: return (BlockLook){kCabinetDark, kCabinetEdge, kShade, kText, false, 0};
    }
}

/* Icon and label, centred as one group. A block with a key (a machine
 * page's buttons) reads like a menu row instead: the label at the left, the
 * key that does the same from anywhere at the right; KIT_ALIGN_LEFT keeps
 * the label there when the pad has no button to show. A star is gold when
 * the item is a favourite, an outline when not. */
static void BlockLabel(const KitItem *it, KitRect r, Color c, int sink, bool live) {
    int size = it->textSize == 8 ? 8 : 16;
    int tw = Kit_TextWidth(it->label, size);
    int iw = it->icon != KIT_ICON_NONE ? 8 + (it->label[0] ? 6 : 0) : 0;
    int x = it->key[0] || (it->flags & KIT_ALIGN_LEFT) ? Ix(r.x) + 20 : Ix(r.x + (r.w - (float)(tw + iw)) / 2.0f);
    int y = Ix(r.y + (r.h - (float)size) / 2.0f) + sink;
    if (it->icon == KIT_ICON_STAR) {
        bool on = it->value == 1 && live;
        Pattern(on ? kStarOn : kStarOff, x, y + (size - 8) / 2, on && sink == 0 ? kYellow : c);
    } else if (iw) {
        Icon(it->icon, x, y + (size - 8) / 2, c);
    }
    Pix_Text(it->label, x + iw, y, size, c);
    if (it->key[0]) Pix_TextRight(it->key, 8, Ix(r.x + r.w) - 12, Ix(r.y + r.h / 2.0f) - 4 + sink, live ? kTextDim : c);
}

static void DrawBlock(const KitItem *it, KitLook look) {
    BlockLook b = BlockColours(look);
    Bevel(it->r, b.fill, b.hi, b.lo);
    if (look == KIT_LOOK_DISABLED) DrawRectangleLinesEx((Rectangle){it->r.x, it->r.y, it->r.w, it->r.h}, 1.0f, Fade(kFaint, 0.6f));
    if (b.bar) Bar(it->r, kPink);
    BlockLabel(it, it->r, b.text, b.sink, look != KIT_LOOK_DISABLED && look != KIT_LOOK_PRESSED);
}

static void Badge(const char *text, int x, int centerY) {
    int w = Kit_TextWidth(text, 8) + 12;
    DrawRectangle(x, centerY - 8, w, 16, kTeal);
    Pix_Text(text, x + 6, centerY - 4, 8, kInk);
}

static void DrawRow(const KitItem *it, KitLook look) {
    KitRect r = it->r;
    Color text = kText, key = kTextDim;
    int sink = 0;
    switch (look) {
        case KIT_LOOK_HOVER: Fill(r, Pal_Mix(kPanel, kCabinet, 0.5f)); text = kWarmWhite; break;
        case KIT_LOOK_FOCUSED: Fill(r, kPanel); Bar(r, kPink); text = kPink; break;
        case KIT_LOOK_PRESSED: Fill(r, kPink); text = key = kInk; sink = 2; break;
        case KIT_LOOK_DISABLED: text = key = kFaint; break;
        default: break;
    }
    int size = it->textSize == 8 ? 8 : 16;
    int cy = Ix(r.y + r.h / 2.0f);
    int x = Ix(r.x) + 20;
    Pix_Text(it->label, x, cy - size / 2 + sink, size, text);
    if (it->badge[0]) Badge(it->badge, x + Kit_TextWidth(it->label, size) + 12, cy + sink);
    if (it->key[0]) Pix_TextRight(it->key, 8, Ix(r.x + r.w) - 12, cy - 4 + sink, key);
}

/* The 7x7 pixel X of the close button. */
static const char kCloseX[7][8] = {"X.....X", ".X...X.", "..X.X..", "...X...", "..X.X..", ".X...X.", "X.....X"};

static void DrawClose(const KitItem *it, KitLook look) {
    Color fill = {48, 18, 30, 255}, edge = {96, 32, 44, 255}, x = kCoinRed;
    int sink = 0;
    if (look == KIT_LOOK_HOVER) { fill = (Color){96, 32, 44, 255}; edge = kCoinRed; x = kWarmWhite; }
    if (look == KIT_LOOK_DISABLED) { fill = kCabinetDark; edge = kCabinetEdge; x = kFaint; }
    if (look == KIT_LOOK_PRESSED) { fill = kPink; edge = Pal_Mix(kPink, kInk, 0.35f); x = kInk; sink = 2; }
    KitRect r = it->r;
    Fill(r, fill);
    DrawRectangleLinesEx((Rectangle){r.x, r.y, r.w, r.h}, 2.0f, edge);
    int px = 2;
    int ox = Ix(r.x + (r.w - 7 * px) / 2.0f), oy = Ix(r.y + (r.h - 7 * px) / 2.0f) + sink;
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 7; col++)
            if (kCloseX[row][col] == 'X') DrawRectangle(ox + col * px, oy + row * px, px, px, x);
}

/* A pad's face button: a 16 px disc, built from rows so its edge stays
 * pixel art. */
static void Disc(int x, int y, Color c) {
    static const int kRow[KIT_CAP_H] = {6, 10, 12, 14, 14, 16, 16, 16, 16, 16, 16, 14, 14, 12, 10, 6};
    for (int i = 0; i < KIT_CAP_H; i++) DrawRectangle(x + (KIT_CAP_H - kRow[i]) / 2, y + i, kRow[i], 1, c);
}

/* A key cap (its name on a raised grey key), or a pad button in the house
 * colours, not the pad's own: a grey disc for A B X Y, a grey pill with the
 * View or Menu icon, or with the name (LB, RB). */
static void Cap(const char *cap, bool pad, int x, int cy, Color fill, Color ink) {
    int w = Kit_CapWidth(cap, pad), y = cy - KIT_CAP_H / 2;
    if (pad && strlen(cap) == 1) {
        Disc(x, y, fill);
        Pix_Text(cap, x + 4, cy - 4, 8, ink);
        return;
    }
    DrawRectangle(x + 1, y, w - 2, KIT_CAP_H, fill); /* corners cut: rounded at this size */
    DrawRectangle(x, y + 1, w, KIT_CAP_H - 2, fill);
    if (!pad) DrawRectangle(x + 1, y + KIT_CAP_H - 2, w - 2, 2, Pal_Mix(fill, kInk, 0.45f)); /* a key's lower lip */
    if (pad && strcmp(cap, "View") == 0) Pattern(kIconView, x + (w - 8) / 2, cy - 4, ink);
    else if (pad && strcmp(cap, "Menu") == 0) Pattern(kIconMenu, x + (w - 8) / 2, cy - 4, ink);
    else Pix_TextCentered(cap, 8, x + w / 2, cy - 4 - (pad ? 0 : 1), ink);
}

/* The hall's control strip: a small block, its key (or pad button) and the
 * word for what it does. */
static void DrawChip(const KitItem *it, KitLook look) {
    BlockLook b = BlockColours(look);
    Bevel(it->r, b.fill, b.hi, b.lo);
    if (look == KIT_LOOK_DISABLED) DrawRectangleLinesEx((Rectangle){it->r.x, it->r.y, it->r.w, it->r.h}, 1.0f, Fade(kFaint, 0.6f));
    if (b.bar) Bar(it->r, kPink);
    Color capFill = kTextDim, capInk = kInk;
    if (look == KIT_LOOK_HOVER) capFill = kText;
    if (look == KIT_LOOK_PRESSED) { capFill = kInk; capInk = kPink; }
    if (look == KIT_LOOK_DISABLED) capFill = Pal_Mix(kCabinetDark, kFaint, 0.7f);
    bool pad = (it->flags & KIT_PADCAP) != 0;
    int cy = Ix(it->r.y + it->r.h / 2.0f) + b.sink, x = Ix(it->r.x) + 4;
    int capW = Kit_CapWidth(it->key, pad);
    if (capW) Cap(it->key, pad, x, cy, capFill, capInk);
    int tx = x + capW + (capW ? 6 : 4);
    if (it->icon == KIT_ICON_STAR) {
        bool on = it->value == 1 && look != KIT_LOOK_DISABLED;
        Pattern(on ? kStarOn : kStarOff, tx, cy - 4, on && look != KIT_LOOK_PRESSED ? kYellow : b.text);
        tx += KIT_CHIP_ICON_W;
    }
    Pix_Text(it->label, tx, cy - 4, 8, b.text);
}

/* The hall's update notice: a teal tab that breathes (steady with reduced
 * flashing), its key written beside it. */
static void DrawPill(const KitItem *it, KitLook look) {
    float pulse = Prefs_Get()->reducedFlashing ? 1.0f : 0.75f + 0.25f * sinf((float)KitDraw_Clock() * 3.0f);
    KitRect r = it->r;
    Color fill = Pal_Mix(kCabinetDark, kTeal, 0.35f * pulse), text = kWarmWhite;
    int sink = 0;
    Color bar = kTeal;
    if (look == KIT_LOOK_HOVER) fill = Pal_Mix(kCabinetDark, kTeal, 0.6f);
    if (look == KIT_LOOK_PRESSED) { fill = kPink; text = bar = kInk; sink = 2; }
    if (look == KIT_LOOK_DISABLED) { fill = kCabinetDark; text = bar = kFaint; }
    Fill(r, fill);
    Bar(r, bar);
    int ty = Ix(r.y + (r.h - 8.0f) / 2.0f);
    Pix_Text(it->label, Ix(r.x) + 10, ty + sink, 8, text);
    if (!it->key[0]) return;
    char hint[24];
    snprintf(hint, sizeof(hint), "%s to view", it->key);
    Pix_TextRight(hint, 8, Ix(r.x) - 10, ty, kTextDim);
}

static void DrawToggle(const KitItem *it, KitLook look, int hoverCell, int pressedCell) {
    static const char *const kOffOn[2] = {"Off", "On"};
    const char *const *labels = it->cells ? it->cells : kOffOn;
    int size = it->textSize == 8 ? 8 : 16;
    if (look == KIT_LOOK_FOCUSED) DrawRectangle(Ix(it->r.x) - 12, Ix(it->r.y), 4, Ix(it->r.h), kPink);
    for (int c = 0; c < 2; c++) {
        KitRect r = Kit_CellRect(it, c);
        bool lit = it->value == c;
        Color fill = lit ? kPink : (look == KIT_LOOK_FOCUSED ? kPanel : kCabinetDark), text = lit ? kInk : kTextDim;
        int sink = 0;
        if (hoverCell == c) { fill = lit ? Pal_Mix(kPink, kWarmWhite, 0.2f) : kCabinet; text = lit ? kInk : kText; }
        if (pressedCell == c) { fill = kPink; text = kInk; sink = 2; }
        if (look == KIT_LOOK_DISABLED) { fill = lit ? Fade(kFaint, 0.5f) : Fade(kCabinetDark, 0.5f); text = kFaint; }
        Fill(r, fill);
        DrawRectangleLinesEx((Rectangle){r.x, r.y, r.w, r.h}, 1.0f, kShade);
        Pix_TextCentered(labels[c], size, Ix(r.x + r.w / 2.0f), Ix(r.y + (r.h - (float)size) / 2.0f) + sink, text);
    }
}

/* "< value >", the arrows' cells lighting under the mouse. */
static void DrawStepper(const KitItem *it, KitLook look, int hoverCell, int pressedCell) {
    KitRect r = it->r;
    int size = it->textSize == 8 ? 8 : 16, gap = size + size / 8;
    bool off = look == KIT_LOOK_DISABLED;
    if (look == KIT_LOOK_FOCUSED) { Fill(r, kPanel); Bar(r, kPink); }
    int ty = Ix(r.y + (r.h - (float)size) / 2.0f), x = Ix(r.x) + 16;
    Color value = off ? kFaint : kYellow;
    for (int c = 0; c < 2; c++) {
        KitRect cell = Kit_CellRect(it, c);
        Color arrow = off ? kFaint : (look == KIT_LOOK_FOCUSED ? kPink : kYellow);
        int sink = 0;
        if (hoverCell == c) { Fill(cell, kCabinet); arrow = kWarmWhite; }
        if (pressedCell == c) { Fill(cell, kPink); arrow = kInk; sink = 2; }
        int ax = c == 0 ? x : x + Kit_TextWidth(it->label, size) + 4 * gap - size;
        Pix_Text(c == 0 ? "<" : ">", ax, ty + sink, size, arrow);
    }
    Pix_Text(it->label, x + 2 * gap, ty, size, value);
}

/* Ten blocks (or max): filled up to the value, the first cell is "none". */
static void DrawSlider(const KitItem *it, KitLook look) {
    KitRect r = it->r;
    if (look == KIT_LOOK_FOCUSED) DrawRectangle(Ix(r.x) + 2, Ix(r.y), 4, Ix(r.h), kPink);
    Color on = kPink, off = kBaseboard;
    if (look == KIT_LOOK_HOVER) off = kCabinetEdge;
    if (look == KIT_LOOK_PRESSED) on = Pal_Mix(kPink, kWarmWhite, 0.3f);
    if (look == KIT_LOOK_DISABLED) { on = kFaint; off = kCabinetDark; }
    int y = Ix(r.y + (r.h - 16.0f) / 2.0f);
    for (int b = 0; b < it->max; b++)
        DrawRectangle(Ix(r.x) + KIT_BLOCK_PITCH * (b + 1) + 2, y, 12, 16, b < it->value ? on : off);
}

static void DrawTabs(const KitItem *it, KitLook look, int hoverCell, int pressedCell) {
    int size = it->textSize == 8 ? 8 : 16;
    for (int c = 0; c < it->cellCount; c++) {
        KitRect r = Kit_CellRect(it, c);
        bool open = it->value == c;
        Color text = open ? kPink : kTextDim;
        int sink = 0;
        if (open) { Fill(r, kPanel); DrawRectangle(Ix(r.x), Ix(r.y + r.h) - 4, Ix(r.w), 4, kPink); }
        if (open && look == KIT_LOOK_FOCUSED) Bar(r, kPink);
        if (hoverCell == c && !open) { Fill(r, Pal_Mix(kPanel, kCabinet, 0.5f)); text = kText; }
        if (pressedCell == c) { Fill(r, kPink); text = kInk; sink = 2; }
        if (look == KIT_LOOK_DISABLED) text = kFaint;
        const char *label = (it->cells && it->cells[c]) ? it->cells[c] : "";
        Pix_TextCentered(label, size, Ix(r.x + r.w / 2.0f), Ix(r.y + (r.h - (float)size) / 2.0f) - 2 + sink, text);
    }
}

void KitDraw_Item(const KitItem *it, KitLook look, int hoverCell, int pressedCell) {
    switch (it->kind) {
        case KIT_AREA: return;
        case KIT_ROW: DrawRow(it, look); return;
        case KIT_TOGGLE: DrawToggle(it, look, hoverCell, pressedCell); return;
        case KIT_STEPPER: DrawStepper(it, look, hoverCell, pressedCell); return;
        case KIT_SLIDER: DrawSlider(it, look); return;
        case KIT_TABS: DrawTabs(it, look, hoverCell, pressedCell); return;
        default: break;
    }
    switch (it->style) {
        case KIT_STYLE_CLOSE: DrawClose(it, look); return;
        case KIT_STYLE_PILL: DrawPill(it, look); return;
        case KIT_STYLE_CHIP: DrawChip(it, look); return;
        default: DrawBlock(it, look); return;
    }
}

KitLook KitDraw_LookOf(const KitView *kv, const KitItem *it) {
    return Kit_Look(kv->state, it, kv->device);
}

void KitDraw_List(const KitView *kv) {
    for (int i = 0; i < kv->list->count; i++) {
        const KitItem *it = &kv->list->items[i];
        if (it->kind == KIT_AREA || (it->flags & KIT_SELFDRAWN)) continue;
        KitDraw_Item(it, KitDraw_LookOf(kv, it), Kit_HoverCell(kv->state, it, kv->device), Kit_PressedCell(kv->state, it));
    }
}
