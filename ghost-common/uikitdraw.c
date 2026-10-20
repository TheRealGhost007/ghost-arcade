#include "uikitdraw.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A copy of Ghost Launcher's src/kitdraw.c. The launcher's cabinet colours
 * are made from the shared theme here, and its pink is the game's light. */

static Color Mix(Color a, Color b, float t) { return Ui_Lerp(a, b, t); }

#define T (Ui_Theme())
#define kPanel (T->panel)
#define kLight (T->light)
#define kInk (T->bg)
#define kText (T->text)
#define kTextDim (T->dim)
#define kFaint (T->faint)
#define kGold (T->gold)
#define kDanger (T->danger)
#define kRule (T->rule)
#define kShade (T->board)                       /* the dark lower edge of a bevel */
#define kCabinetDark (Mix(T->bg, T->panel, 0.5f)) /* a button at rest */
#define kCabinet (Mix(T->panel, T->rule, 0.35f))  /* under the pointer */
#define kCabinetEdge (Mix(T->rule, T->dim, 0.12f))
#define kWarmWhite (Mix(T->text, WHITE, 0.6f))

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

static void Outline(KitRect r, float thick, Color c) {
    DrawRectangleLinesEx((Rectangle){(float)Ix(r.x), (float)Ix(r.y), (float)Ix(r.w), (float)Ix(r.h)}, thick, c);
}

void KitDraw_Plate(KitRect r) {
    Fill(r, kPanel);
    Bar(r, kLight);
}

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
/* The d-pad's four arrows, for its caps. */
static const char kArrowUp[8][9] = {
    "........", "...XX...", "..XXXX..", ".XXXXXX.", "XXXXXXXX", "...XX...", "...XX...", "........",
};
static const char kArrowDown[8][9] = {
    "........", "...XX...", "...XX...", "XXXXXXXX", ".XXXXXX.", "..XXXX..", "...XX...", "........",
};
static const char kArrowLeft[8][9] = {
    "...X....", "..XX....", ".XXXXXX.", "XXXXXXX.", "XXXXXXX.", ".XXXXXX.", "..XX....", "...X....",
};
static const char kArrowRight[8][9] = {
    "....X...", "....XX..", ".XXXXXX.", ".XXXXXXX", ".XXXXXXX", ".XXXXXX.", "....XX..", "....X...",
};
/* A favourite's star: gold and solid when it is one, an outline when not. */
static const char kStarOn[8][9] = {
    "...XX...", "...XX...", "XXXXXXXX", ".XXXXXX.", "..XXXX..", "..XXXX..", ".XX..XX.", ".X....X.",
};
static const char kStarOff[8][9] = {
    "...XX...", "...XX...", "XXX..XXX", ".X....X.", "..X..X..", "..X..X..", ".X.XX.X.", ".X....X.",
};

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

static BlockLook BlockColours(KitLook look, bool accent) {
    switch (look) {
        case KIT_LOOK_HOVER: return (BlockLook){Mix(kCabinet, kCabinetEdge, 0.5f), Mix(kCabinetEdge, kText, 0.4f), kShade, kWarmWhite, false, 0};
        case KIT_LOOK_FOCUSED: return (BlockLook){kPanel, kPanel, kPanel, kLight, true, 0};
        case KIT_LOOK_PRESSED: return (BlockLook){kLight, Mix(kLight, kInk, 0.35f), kLight, kInk, false, 2};
        case KIT_LOOK_DISABLED: return (BlockLook){Fade(kCabinetDark, 0.5f), Fade(kCabinetDark, 0.5f), Fade(kCabinetDark, 0.5f), kFaint, false, 0};
        default: return (BlockLook){kCabinetDark, kCabinetEdge, kShade, accent ? kLight : kText, false, 0};
    }
}

/* Icon and label, centred as one group. A block with a key reads like a
 * menu row instead: the label at the left, the key that does the same from
 * anywhere at the right; KIT_ALIGN_LEFT keeps the label there without one. */
static void BlockLabel(const KitItem *it, KitRect r, Color c, int sink, bool live) {
    int size = it->textSize == 8 ? 8 : 16;
    int tw = Kit_TextWidth(it->label, size);
    int iw = it->icon != KIT_ICON_NONE ? 8 + (it->label[0] ? 6 : 0) : 0;
    int x = it->key[0] || (it->flags & KIT_ALIGN_LEFT) ? Ix(r.x) + 20 : Ix(r.x + (r.w - (float)(tw + iw)) / 2.0f);
    int y = Ix(r.y + (r.h - (float)size) / 2.0f) + sink;
    if (it->icon == KIT_ICON_STAR) {
        bool on = it->value == 1 && live;
        Pattern(on ? kStarOn : kStarOff, x, y + (size - 8) / 2, on && sink == 0 ? kGold : c);
    } else if (iw) {
        Icon(it->icon, x, y + (size - 8) / 2, c);
    }
    Ui_Text(it->label, x + iw, y, size, c);
    if (it->key[0]) Ui_TextRight(it->key, 8, Ix(r.x + r.w) - 12, Ix(r.y + r.h / 2.0f) - 4 + sink, live ? kTextDim : c);
}

/* plated: the focus plate is drawn elsewhere (gliding), so the block shows
 * only its focused label here. */
static void DrawBlock(const KitItem *it, KitLook look, bool plated) {
    bool accent = (it->flags & KIT_ACCENT) != 0;
    BlockLook b = BlockColours(look, accent);
    if (!(plated && look == KIT_LOOK_FOCUSED)) Bevel(it->r, b.fill, b.hi, b.lo);
    if (look == KIT_LOOK_DISABLED) Outline(it->r, 1.0f, Fade(kFaint, 0.6f));
    if (accent && look != KIT_LOOK_PRESSED && look != KIT_LOOK_DISABLED) Outline(it->r, 2.0f, look == KIT_LOOK_NORMAL ? Fade(kLight, 0.6f) : kLight);
    if (b.bar && !plated) Bar(it->r, kLight);
    BlockLabel(it, it->r, b.text, b.sink, look != KIT_LOOK_DISABLED && look != KIT_LOOK_PRESSED);
}

static void Badge(const char *text, int x, int centerY) {
    int w = Kit_TextWidth(text, 8) + 12;
    DrawRectangle(x, centerY - 8, w, 16, kLight);
    Ui_Text(text, x + 6, centerY - 4, 8, kInk);
}

/* A list row: its label (16 or 8 px) at the left, then a word in a column
 * of its own (note), a dim second line (sub), a badge, and a key at the
 * right. A KIT_CHOSEN row (the current choice) keeps its label lit. */
static void DrawRow(const KitItem *it, KitLook look, bool plated) {
    KitRect r = it->r;
    bool chosen = (it->flags & KIT_CHOSEN) != 0;
    Color text = chosen ? kLight : kText, key = kTextDim, dim = kTextDim;
    int sink = 0;
    switch (look) {
        case KIT_LOOK_HOVER: Fill(r, Mix(kPanel, kCabinet, 0.5f)); text = chosen ? kLight : kWarmWhite; break;
        case KIT_LOOK_FOCUSED: if (!plated) { Fill(r, kPanel); Bar(r, kLight); } text = kLight; break;
        case KIT_LOOK_PRESSED: Fill(r, kLight); text = key = dim = kInk; sink = 2; break;
        case KIT_LOOK_DISABLED: text = key = dim = kFaint; break;
        default: break;
    }
    if (chosen && look == KIT_LOOK_NORMAL) DrawRectangle(Ix(r.x), Ix(r.y + r.h / 2.0f) - 2, 4, 4, kLight); /* a pip: chosen, not focused */
    int size = it->textSize == 8 ? 8 : 16;
    int cy = Ix(r.y + r.h / 2.0f);
    int x = Ix(r.x) + 20;
    int ty = it->sub[0] ? cy - (size + 6 + 8) / 2 : cy - size / 2;
    Ui_Text(it->label, x, ty + sink, size, text);
    if (it->sub[0]) Ui_Text(it->sub, x, ty + size + 6 + sink, 8, dim);
    if (it->note[0]) Ui_Text(it->note, Ix(r.x) + it->noteX, ty + (size - 8) / 2 + sink, 8, dim);
    if (it->badge[0]) Badge(it->badge, x + Kit_TextWidth(it->label, size) + 12, ty + size / 2 + sink);
    if (it->key[0]) Ui_TextRight(it->key, 8, Ix(r.x + r.w) - 12, cy - 4 + sink, key);
}

/* The 7x7 pixel X of the close button. */
static const char kCloseX[7][8] = {"X.....X", ".X...X.", "..X.X..", "...X...", "..X.X..", ".X...X.", "X.....X"};

static void DrawClose(const KitItem *it, KitLook look) {
    Color fill = Mix(kInk, kDanger, 0.18f), edge = Mix(kInk, kDanger, 0.4f), x = kDanger;
    int sink = 0;
    if (look == KIT_LOOK_HOVER) { fill = Mix(kInk, kDanger, 0.4f); edge = kDanger; x = kWarmWhite; }
    if (look == KIT_LOOK_DISABLED) { fill = kCabinetDark; edge = kCabinetEdge; x = kFaint; }
    if (look == KIT_LOOK_PRESSED) { fill = kLight; edge = Mix(kLight, kInk, 0.35f); x = kInk; sink = 2; }
    KitRect r = it->r;
    Fill(r, fill);
    Outline(r, 2.0f, edge);
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

static const char (*DpadArrow(const char *cap))[9] {
    if (strcmp(cap, "Up") == 0) return kArrowUp;
    if (strcmp(cap, "Down") == 0) return kArrowDown;
    if (strcmp(cap, "Left") == 0) return kArrowLeft;
    if (strcmp(cap, "Right") == 0) return kArrowRight;
    return NULL;
}

/* A key cap (its name on a raised grey key), or a pad button in the house
 * colours, not the pad's own: a grey disc for A B X Y and the d-pad's
 * arrows, a grey pill with the View or Menu icon, or with the name (LB). */
static void Cap(const char *cap, bool pad, int x, int cy, Color fill, Color ink) {
    int w = Kit_CapWidth(cap, pad), y = cy - KIT_CAP_H / 2;
    const char (*arrow)[9] = pad ? DpadArrow(cap) : NULL;
    if (pad && (strlen(cap) == 1 || arrow)) {
        Disc(x, y, fill);
        if (arrow) Pattern(arrow, x + 4, cy - 4, ink);
        else Ui_Text(cap, x + 4, cy - 4, 8, ink);
        return;
    }
    DrawRectangle(x + 1, y, w - 2, KIT_CAP_H, fill); /* corners cut: rounded at this size */
    DrawRectangle(x, y + 1, w, KIT_CAP_H - 2, fill);
    if (!pad) DrawRectangle(x + 1, y + KIT_CAP_H - 2, w - 2, 2, Mix(fill, kInk, 0.45f)); /* a key's lower lip */
    if (pad && strcmp(cap, "View") == 0) Pattern(kIconView, x + (w - 8) / 2, cy - 4, ink);
    else if (pad && strcmp(cap, "Menu") == 0) Pattern(kIconMenu, x + (w - 8) / 2, cy - 4, ink);
    else Ui_TextCentered(cap, 8, x + w / 2, cy - 4 - (pad ? 0 : 1), ink);
}

void KitDraw_Cap(const char *cap, bool pad, int x, int cy) { Cap(cap, pad, x, cy, kTextDim, kInk); }

/* A small block with its key (or pad button) and the word for what it does. */
static void DrawChip(const KitItem *it, KitLook look) {
    BlockLook b = BlockColours(look, false);
    Bevel(it->r, b.fill, b.hi, b.lo);
    if (look == KIT_LOOK_DISABLED) Outline(it->r, 1.0f, Fade(kFaint, 0.6f));
    if (b.bar) Bar(it->r, kLight);
    Color capFill = kTextDim, capInk = kInk;
    if (look == KIT_LOOK_HOVER) capFill = kText;
    if (look == KIT_LOOK_PRESSED) { capFill = kInk; capInk = kLight; }
    if (look == KIT_LOOK_DISABLED) capFill = Mix(kCabinetDark, kFaint, 0.7f);
    bool pad = (it->flags & KIT_PADCAP) != 0;
    int cy = Ix(it->r.y + it->r.h / 2.0f) + b.sink, x = Ix(it->r.x) + 4;
    int capW = Kit_CapWidth(it->key, pad);
    if (capW) Cap(it->key, pad, x, cy, capFill, capInk);
    int tx = x + capW + (capW ? 6 : 4);
    if (it->icon == KIT_ICON_STAR) {
        bool on = it->value == 1 && look != KIT_LOOK_DISABLED;
        Pattern(on ? kStarOn : kStarOff, tx, cy - 4, on && look != KIT_LOOK_PRESSED ? kGold : b.text);
        tx += KIT_CHIP_ICON_W;
    }
    Ui_Text(it->label, tx, cy - 4, 8, b.text);
}

static void DrawToggle(const KitItem *it, KitLook look, int hoverCell, int pressedCell) {
    static const char *const kOffOn[2] = {"Off", "On"};
    const char *const *labels = it->cells ? it->cells : kOffOn;
    int size = it->textSize == 8 ? 8 : 16;
    if (look == KIT_LOOK_FOCUSED) DrawRectangle(Ix(it->r.x) - 12, Ix(it->r.y), 4, Ix(it->r.h), kLight);
    for (int c = 0; c < 2; c++) {
        KitRect r = Kit_CellRect(it, c);
        bool lit = it->value == c;
        Color fill = lit ? kLight : (look == KIT_LOOK_FOCUSED ? kPanel : kCabinetDark), text = lit ? kInk : kTextDim;
        int sink = 0;
        if (hoverCell == c) { fill = lit ? Mix(kLight, kWarmWhite, 0.2f) : kCabinet; text = lit ? kInk : kText; }
        if (pressedCell == c) { fill = kLight; text = kInk; sink = 2; }
        if (look == KIT_LOOK_DISABLED) { fill = lit ? Fade(kFaint, 0.5f) : Fade(kCabinetDark, 0.5f); text = kFaint; }
        Fill(r, fill);
        Outline(r, 1.0f, kShade);
        Ui_TextCentered(labels[c], size, Ix(r.x + r.w / 2.0f), Ix(r.y + (r.h - (float)size) / 2.0f) + sink, text);
    }
}

/* "< value >", the arrows' cells lighting under the mouse; the value sits
 * between them. A stepper with a caption is a whole row: the caption at the
 * left, the arrows from arrowX. */
static void DrawStepper(const KitItem *it, KitLook look, int hoverCell, int pressedCell, bool plated) {
    KitRect r = it->r;
    int size = it->textSize == 8 ? 8 : 16;
    bool off = look == KIT_LOOK_DISABLED;
    if (look == KIT_LOOK_FOCUSED && !plated) { Fill(r, kPanel); Bar(r, kLight); }
    if (look == KIT_LOOK_HOVER && it->caption[0]) Fill((KitRect){r.x, r.y, (float)it->arrowX, r.h}, Mix(kPanel, kCabinet, 0.5f));
    int ty = Ix(r.y + (r.h - (float)size) / 2.0f);
    if (it->caption[0])
        Ui_Text(it->caption, Ix(r.x) + 20, Ix(r.y + (r.h - 16.0f) / 2.0f), 16,
                off ? kFaint : (look == KIT_LOOK_FOCUSED ? kLight : (look == KIT_LOOK_HOVER ? kWarmWhite : kText)));
    Color value = off ? kFaint : (look == KIT_LOOK_FOCUSED ? kLight : kGold);
    for (int c = 0; c < 2; c++) {
        KitRect cell = Kit_CellRect(it, c);
        Color arrow = off ? kFaint : (look == KIT_LOOK_FOCUSED ? kLight : kTextDim);
        int sink = 0;
        if (hoverCell == c) { Fill(cell, kCabinet); arrow = kWarmWhite; }
        if (pressedCell == c) { Fill(cell, kLight); arrow = kInk; sink = 2; }
        Ui_TextCentered(c == 0 ? "<" : ">", size, Ix(cell.x + cell.w / 2.0f), ty + sink, arrow);
    }
    Ui_TextCentered(it->label, size, Ix(r.x + (float)it->arrowX + (r.w - (float)it->arrowX) / 2.0f), ty, value);
}

/* Ten blocks (or max): filled up to the value, the first cell is "none". */
static void DrawSlider(const KitItem *it, KitLook look) {
    KitRect r = it->r;
    if (look == KIT_LOOK_FOCUSED) DrawRectangle(Ix(r.x) + 2, Ix(r.y), 4, Ix(r.h), kLight);
    Color on = kLight, off = kRule;
    if (look == KIT_LOOK_HOVER) off = kCabinetEdge;
    if (look == KIT_LOOK_PRESSED) on = Mix(kLight, kWarmWhite, 0.3f);
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
        Color text = open ? kLight : kTextDim;
        int sink = 0;
        if (open) { Fill(r, kPanel); DrawRectangle(Ix(r.x), Ix(r.y + r.h) - 4, Ix(r.w), 4, kLight); }
        if (open && look == KIT_LOOK_FOCUSED) Bar(r, kLight);
        if (hoverCell == c && !open) { Fill(r, Mix(kPanel, kCabinet, 0.5f)); text = kText; }
        if (pressedCell == c) { Fill(r, kLight); text = kInk; sink = 2; }
        if (look == KIT_LOOK_DISABLED) text = kFaint;
        const char *label = (it->cells && it->cells[c]) ? it->cells[c] : "";
        Ui_TextCentered(label, size, Ix(r.x + r.w / 2.0f), Ix(r.y + (r.h - (float)size) / 2.0f) - 2 + sink, text);
    }
}

static void DrawItem(const KitItem *it, KitLook look, int hoverCell, int pressedCell, bool plated) {
    switch (it->kind) {
        case KIT_AREA: return;
        case KIT_ROW: DrawRow(it, look, plated); return;
        case KIT_TOGGLE: DrawToggle(it, look, hoverCell, pressedCell); return;
        case KIT_STEPPER: DrawStepper(it, look, hoverCell, pressedCell, plated); return;
        case KIT_SLIDER: DrawSlider(it, look); return;
        case KIT_TABS: DrawTabs(it, look, hoverCell, pressedCell); return;
        default: break;
    }
    switch (it->style) {
        case KIT_STYLE_CLOSE: DrawClose(it, look); return;
        case KIT_STYLE_CHIP: DrawChip(it, look); return;
        default: DrawBlock(it, look, plated); return;
    }
}

void KitDraw_Item(const KitItem *it, KitLook look, int hoverCell, int pressedCell) { DrawItem(it, look, hoverCell, pressedCell, false); }

KitLook KitDraw_LookOf(const KitView *kv, const KitItem *it) {
    return Kit_Look(kv->state, it, kv->device);
}

/* The kinds whose focus is a full plate under them, which can glide. */
static bool Plates(const KitItem *it) {
    return it->kind == KIT_ROW || it->kind == KIT_STEPPER ||
           (it->kind == KIT_BUTTON && (it->style == KIT_STYLE_BLOCK || it->style == KIT_STYLE_SMALL));
}

void KitDraw_List(const KitView *kv) {
    const KitItem *plated = NULL;
    if (kv->plate && kv->state) {
        for (int i = 0; i < kv->list->count; i++) {
            const KitItem *it = &kv->list->items[i];
            if (it->kind == KIT_AREA || (it->flags & KIT_SELFDRAWN) || !Plates(it)) continue;
            if (KitDraw_LookOf(kv, it) == KIT_LOOK_FOCUSED) plated = it;
        }
        if (plated) KitDraw_Plate(*kv->plate);
    }
    for (int i = 0; i < kv->list->count; i++) {
        const KitItem *it = &kv->list->items[i];
        if (it->kind == KIT_AREA || (it->flags & KIT_SELFDRAWN)) continue;
        DrawItem(it, KitDraw_LookOf(kv, it), Kit_HoverCell(kv->state, it, kv->device), Kit_PressedCell(kv->state, it), it == plated);
    }
}

void KitDraw_Hints(const KitHint *h, int count, int cx, int cy, InputDevice device) {
    int x = cx - Kit_HintsWidth(h, count, device) / 2;
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < KIT_HINT_CAPS && c < h[i].count; c++) {
            char cap[16];
            bool pad;
            Kit_HintCap(h[i].actions[c], device, cap, sizeof(cap), &pad);
            if (c) x += 2;
            KitDraw_Cap(cap, pad, x, cy);
            x += Kit_CapWidth(cap, pad);
        }
        if (h[i].label && h[i].label[0]) {
            x += 6;
            Ui_Text(h[i].label, x, cy - 4, 8, kTextDim);
            x += Kit_TextWidth(h[i].label, 8);
        }
        x += KIT_HINT_GAP;
    }
}

void KitDraw_Dialog(const KitDialog *d, const KitView *kv) {
    DrawRectangle(0, 0, Kit_ScreenW(), Kit_ScreenH(), Fade(kShade, 0.6f));
    KitRect box = Kit_DialogRect();
    int x = Ix(box.x), y = Ix(box.y);
    Fill(box, Fade(kShade, 0.98f));
    Outline(box, 2.0f, kRule);
    DrawRectangle(x, y, Ix(box.w), 4, kLight);
    Ui_Text(d->title, x + 32, y + 32, 16, kWarmWhite);
    for (int i = 0; i < 2; i++) if (d->body[i][0]) Ui_Text(d->body[i], x + 32, y + 68 + i * 20, 8, kTextDim);
    KitDraw_List(kv);
}
