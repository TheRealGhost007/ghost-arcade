#include "kit.h"
#include "keymap.h"
#include <stdio.h>
#include <string.h>

/* Six tabs (Settings with Machines) fit across the top from x 80 and end
 * where the rule under them does (x 920). */
#define TAB_PAD 10
#define TAB_GAP 6

int Kit_TextWidth(const char *text, int size) {
    int n = (int)strlen(text);
    return n > 0 ? n * size + (n - 1) * (size / 8) : 0; /* every glyph is one em, plus one font pixel between */
}

int Kit_CapWidth(const char *cap, bool pad) {
    if (!cap || !cap[0]) return 0;
    int n = (int)strlen(cap);
    if (pad && n == 1) return KIT_CAP_H;
    if (pad && (strcmp(cap, "View") == 0 || strcmp(cap, "Menu") == 0)) return 20;
    int w = Kit_TextWidth(cap, 8) + 8;
    return w < KIT_CAP_H ? KIT_CAP_H : w;
}

int Kit_ChipWidth(const char *cap, bool pad, const char *label) {
    int capW = Kit_CapWidth(cap, pad);
    return 4 + capW + (capW ? 6 : 4) + Kit_TextWidth(label, 8) + 8;
}

/* ------------------------------------------------------------ the list */

void Kit_Clear(KitList *l, unsigned nav) {
    l->count = 0;
    l->nav = nav;
}

KitItem *Kit_Add(KitList *l, int id, KitKind kind, KitRect r, const char *label) {
    if (l->count >= KIT_MAX_ITEMS) return NULL;
    KitItem *it = &l->items[l->count++];
    memset(it, 0, sizeof(*it));
    it->id = id;
    it->kind = kind;
    it->r = r;
    it->textSize = 16;
    if (kind == KIT_AREA) it->flags = KIT_NOFOCUS;
    snprintf(it->label, sizeof(it->label), "%s", label ? label : "");
    return it;
}

static int IndexOf(const KitList *l, int id) {
    if (id == KIT_NONE) return KIT_NONE;
    for (int i = 0; i < l->count; i++) if (l->items[i].id == id) return i;
    return KIT_NONE;
}

const KitItem *Kit_Get(const KitList *l, int id) {
    int i = IndexOf(l, id);
    return i == KIT_NONE ? NULL : &l->items[i];
}

void Kit_Disable(KitItem *it, const char *reason) {
    if (!it) return;
    it->flags |= KIT_DISABLED;
    snprintf(it->reason, sizeof(it->reason), "%s", reason ? reason : "");
}

/* ------------------------------------------------------------ geometry */

bool Kit_Inside(KitRect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static int TabWidth(const KitItem *it, int i);

KitRect Kit_Bounds(const KitItem *it) {
    if (it->kind != KIT_TABS || it->cellCount <= 0) return it->r;
    KitRect last = Kit_CellRect(it, it->cellCount - 1);
    return (KitRect){it->r.x, it->r.y, last.x + last.w - it->r.x, it->r.h};
}

int Kit_HitTest(const KitList *l, float x, float y) {
    for (int i = l->count - 1; i >= 0; i--) if (Kit_Inside(Kit_Bounds(&l->items[i]), x, y)) return i;
    return KIT_NONE;
}

bool Kit_CanFocus(const KitItem *it) {
    return it && it->kind != KIT_AREA && !(it->flags & (KIT_DISABLED | KIT_NOFOCUS));
}

int Kit_FirstFocus(const KitList *l) {
    for (int i = 0; i < l->count; i++) if (Kit_CanFocus(&l->items[i])) return l->items[i].id;
    return KIT_NONE;
}

static float CenterX(KitRect r) { return r.x + r.w * 0.5f; }
static float CenterY(KitRect r) { return r.y + r.h * 0.5f; }

static bool Overlap(float a, float aw, float b, float bw) { return a < b + bw && b < a + aw; }

static float Abs(float v) { return v < 0.0f ? -v : v; }

/* Lower is better. Items sharing a row (or column) always beat ones off to
 * the side, then the nearest wins, sideways distance counting double. */
static float Score(KitRect from, KitRect to, bool horiz, float along) {
    float cross = horiz ? Abs(CenterY(to) - CenterY(from)) : Abs(CenterX(to) - CenterX(from));
    bool inLine = horiz ? Overlap(from.y, from.h, to.y, to.h) : Overlap(from.x, from.w, to.x, to.w);
    return along + cross * 2.0f + (inLine ? 0.0f : 100000.0f);
}

static int Nearest(const KitList *l, int from, KitDir dir) {
    bool horiz = dir == KIT_LEFT || dir == KIT_RIGHT;
    float sign = (dir == KIT_RIGHT || dir == KIT_DOWN) ? 1.0f : -1.0f;
    KitRect a = Kit_Bounds(&l->items[from]);
    int best = KIT_NONE;
    float bestScore = 0.0f;
    for (int i = 0; i < l->count; i++) {
        if (i == from || !Kit_CanFocus(&l->items[i])) continue;
        KitRect b = Kit_Bounds(&l->items[i]);
        float along = sign * (horiz ? CenterX(b) - CenterX(a) : CenterY(b) - CenterY(a));
        if (along < 0.5f) continue; /* not that way */
        float s = Score(a, b, horiz, along);
        if (best == KIT_NONE || s < bestScore) { best = i; bestScore = s; }
    }
    return best;
}

/* Round the far side: the item furthest the other way, in line if possible. */
static int WrapAround(const KitList *l, int from, KitDir dir) {
    bool horiz = dir == KIT_LEFT || dir == KIT_RIGHT;
    float sign = (dir == KIT_RIGHT || dir == KIT_DOWN) ? 1.0f : -1.0f;
    KitRect a = Kit_Bounds(&l->items[from]);
    int best = KIT_NONE;
    float bestScore = 0.0f;
    for (int i = 0; i < l->count; i++) {
        if (i == from || !Kit_CanFocus(&l->items[i])) continue;
        KitRect b = Kit_Bounds(&l->items[i]);
        float along = sign * (horiz ? CenterX(b) : CenterY(b)) + 10000.0f;
        float s = Score(a, b, horiz, along);
        if (best == KIT_NONE || s < bestScore) { best = i; bestScore = s; }
    }
    return best;
}

int Kit_Navigate(const KitList *l, int fromId, KitDir dir) {
    int from = IndexOf(l, fromId);
    if (from == KIT_NONE) return Kit_FirstFocus(l);
    int to = Nearest(l, from, dir);
    bool horiz = dir == KIT_LEFT || dir == KIT_RIGHT;
    if (to == KIT_NONE && (l->nav & (horiz ? KIT_WRAP_X : KIT_WRAP_Y))) to = WrapAround(l, from, dir);
    return to == KIT_NONE ? fromId : l->items[to].id;
}

static int TabWidth(const KitItem *it, int i) {
    const char *label = (it->cells && it->cells[i]) ? it->cells[i] : "";
    return Kit_TextWidth(label, it->textSize) + 2 * TAB_PAD;
}

int Kit_CellCount(const KitItem *it) {
    switch (it->kind) {
        case KIT_TOGGLE:
        case KIT_STEPPER: return 2;
        case KIT_TABS: return it->cellCount > 0 ? it->cellCount : 0;
        default: return 1;
    }
}

KitRect Kit_CellRect(const KitItem *it, int cell) {
    KitRect r = it->r;
    switch (it->kind) {
        case KIT_TOGGLE: return (KitRect){r.x + (cell ? r.w * 0.5f : 0.0f), r.y, r.w * 0.5f, r.h};
        case KIT_STEPPER: return (KitRect){cell ? r.x + r.w - KIT_ARROW_W : r.x, r.y, KIT_ARROW_W, r.h};
        case KIT_TABS: {
            float x = r.x;
            for (int i = 0; i < cell; i++) x += (float)(TabWidth(it, i) + TAB_GAP);
            return (KitRect){x, r.y, (float)TabWidth(it, cell), r.h};
        }
        default: return r;
    }
}

int Kit_CellAt(const KitItem *it, float x, float y) {
    int n = Kit_CellCount(it);
    for (int i = 0; i < n; i++) if (Kit_Inside(Kit_CellRect(it, i), x, y)) return i;
    return KIT_NONE;
}

int Kit_SliderValueAt(const KitItem *it, float x) {
    if (x < it->r.x) return 0;
    int v = (int)((x - it->r.x) / (float)KIT_BLOCK_PITCH);
    if (v > it->max) v = it->max;
    return v;
}

/* ---------------------------------------------------------------- state */

void Kit_Init(KitState *s) {
    memset(s, 0, sizeof(*s));
    s->focus = s->hover = s->hoverCell = s->mouseDown = s->mouseCell = s->keyDown = s->keyBind = KIT_NONE;
}

void Kit_Enter(KitState *s, int focusId) {
    Kit_Init(s);
    s->focus = focusId;
}

static void ValueRange(const KitItem *it, int *lo, int *hi) {
    *lo = it->min;
    *hi = it->max;
    if (it->kind == KIT_TABS) { *lo = 0; *hi = it->cellCount - 1; }
    if (it->kind == KIT_TOGGLE) { *lo = 0; *hi = 1; }
    if (it->kind == KIT_SLIDER) *lo = 0;
}

static int StepValue(const KitItem *it, int delta) {
    int lo, hi;
    ValueRange(it, &lo, &hi);
    if (hi < lo) return it->value;
    int v = it->value + delta;
    if (it->flags & KIT_WRAP) {
        int span = hi - lo + 1;
        return lo + (((v - lo) % span) + span) % span;
    }
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool IsValueKind(KitKind k) { return k == KIT_TOGGLE || k == KIT_STEPPER || k == KIT_SLIDER || k == KIT_TABS; }

static KitEvent Change(const KitItem *it, int value) {
    if (value == it->value) return (KitEvent){KIT_EV_NONE, KIT_NONE, 0};
    return (KitEvent){KIT_EV_CHANGE, it->id, value};
}

static KitEvent Press(const KitItem *it) { return (KitEvent){KIT_EV_PRESS, it->id, it->value}; }

/* A focus on something gone, or on what can never take it, moves to the
 * first control. One that is only disabled for now (Check now while it
 * checks) keeps it, so the focus is still there when it comes back. */
static void RepairFocus(KitState *s, const KitList *l) {
    const KitItem *f = Kit_Get(l, s->focus);
    if (f && f->kind != KIT_AREA && !(f->flags & KIT_NOFOCUS)) return;
    s->focus = Kit_FirstFocus(l);
}

/* Hover follows the pointer every frame (a list can scroll under a still
 * pointer); the focus follows it only when the mouse itself moves. */
static void TrackPointer(KitState *s, const KitList *l, const Input *in) {
    int hit = in->mouseInside ? Kit_HitTest(l, in->mouseX, in->mouseY) : KIT_NONE;
    int was = s->hover;
    s->hover = hit == KIT_NONE ? KIT_NONE : l->items[hit].id;
    s->hoverCell = hit == KIT_NONE ? KIT_NONE : Kit_CellAt(&l->items[hit], in->mouseX, in->mouseY);
    if (s->hover != was) s->wheel = 0.0f;
    if (hit != KIT_NONE && in->mouseMoved && Kit_CanFocus(&l->items[hit]) && s->focus != s->hover) {
        s->focus = s->hover;
        s->keyDown = KIT_NONE;
    }
}

/* What letting go over the control it went down on does. */
static KitEvent Release(const KitItem *it, int cell) {
    switch (it->kind) {
        case KIT_TOGGLE: return Change(it, cell == 1 ? 1 : 0);
        case KIT_STEPPER: return Change(it, StepValue(it, cell == 1 ? 1 : -1));
        case KIT_TABS: return Change(it, cell);
        case KIT_SLIDER: return (KitEvent){KIT_EV_NONE, KIT_NONE, 0}; /* it changed while held */
        default: return Press(it);
    }
}

static KitEvent MouseDown(KitState *s, const KitList *l, const Input *in) {
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    s->mouseDown = KIT_NONE;
    const KitItem *it = Kit_Get(l, s->hover);
    if (!it || (it->flags & KIT_DISABLED) || s->hoverCell == KIT_NONE) return none;
    s->mouseDown = it->id;
    s->mouseCell = s->hoverCell;
    s->keyDown = KIT_NONE;
    if (Kit_CanFocus(it)) s->focus = it->id;
    return it->kind == KIT_SLIDER ? Change(it, Kit_SliderValueAt(it, in->mouseX)) : none;
}

static KitEvent MouseUp(KitState *s, const KitList *l) {
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    int id = s->mouseDown, cell = s->mouseCell;
    s->mouseDown = KIT_NONE;
    const KitItem *it = Kit_Get(l, id);
    if (!it || (it->flags & KIT_DISABLED)) return none;
    if (it->kind == KIT_SLIDER) return none;
    if (s->hover != id || s->hoverCell != cell) return none; /* let go somewhere else */
    return Release(it, cell);
}

static KitEvent Mouse(KitState *s, const KitList *l, const Input *in) {
    KitEvent ev = {KIT_EV_NONE, KIT_NONE, 0};
    if (in->mousePressed[MB_LEFT]) ev = MouseDown(s, l, in);
    if (s->mouseDown == KIT_NONE) return ev;
    const KitItem *it = Kit_Get(l, s->mouseDown);
    if (it && it->kind == KIT_SLIDER && in->mouseDown[MB_LEFT] && !in->mousePressed[MB_LEFT])
        ev = Change(it, Kit_SliderValueAt(it, in->mouseX));
    if (in->mouseReleased[MB_LEFT]) {
        KitEvent up = MouseUp(s, l);
        if (up.type != KIT_EV_NONE) ev = up;
    } else if (!in->mouseDown[MB_LEFT] && !in->mousePressed[MB_LEFT]) {
        s->mouseDown = KIT_NONE; /* the release happened while we were not looking */
    }
    return ev;
}

static KitEvent Wheel(KitState *s, const KitList *l, const Input *in) {
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    if (in->wheel == 0.0f || !Kit_OwnsWheel(s, l)) return none;
    const KitItem *it = Kit_Get(l, s->hover);
    s->wheel += in->wheel;
    int steps = (int)s->wheel; /* toward zero: part notches wait for the rest */
    s->wheel -= (float)steps;
    return steps ? Change(it, StepValue(it, steps)) : none;
}

static const KitItem *TabBar(const KitList *l) {
    for (int i = 0; i < l->count; i++) if (l->items[i].kind == KIT_TABS && !(l->items[i].flags & KIT_DISABLED)) return &l->items[i];
    return NULL;
}

/* LB/RB (and Tab/Shift+Tab) switch tabs wherever the focus is. */
static KitEvent Shoulders(const KitList *l, const Input *in) {
    const KitItem *tabs = TabBar(l);
    int d = (Keymap_Pressed(KA_UI_NEXT_TAB, in) ? 1 : 0) - (Keymap_Pressed(KA_UI_PREV_TAB, in) ? 1 : 0);
    if (!tabs || d == 0) return (KitEvent){KIT_EV_NONE, KIT_NONE, 0};
    return Change(tabs, StepValue(tabs, d));
}

static bool DirPressed(const Input *in, KitDir *dir) {
    if (Keymap_Pressed(KA_UI_LEFT, in)) *dir = KIT_LEFT;
    else if (Keymap_Pressed(KA_UI_RIGHT, in)) *dir = KIT_RIGHT;
    else if (Keymap_Pressed(KA_UI_UP, in)) *dir = KIT_UP;
    else if (Keymap_Pressed(KA_UI_DOWN, in)) *dir = KIT_DOWN;
    else return false;
    return true;
}

static KitEvent Arrows(KitState *s, const KitList *l, const Input *in) {
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    KitDir dir;
    if (!DirPressed(in, &dir)) return none;
    bool horiz = dir == KIT_LEFT || dir == KIT_RIGHT;
    const KitItem *f = Kit_Get(l, s->focus);
    if (f && horiz && IsValueKind(f->kind)) {
        if (f->flags & KIT_DISABLED) return none; /* focused while it waits: its value is not for changing */
        if (f->kind == KIT_TOGGLE) return Change(f, dir == KIT_RIGHT ? 1 : 0);
        return Change(f, StepValue(f, dir == KIT_RIGHT ? 1 : -1));
    }
    if (!(l->nav & (horiz ? KIT_NAV_X : KIT_NAV_Y))) return none;
    int to = Kit_Navigate(l, s->focus, dir);
    if (to != s->focus) {
        s->focus = to;
        s->keyDown = KIT_NONE; /* a press does not follow the focus */
    }
    return none;
}

/* A stepper steps on, as in the games' settings ("Enter changes"): its
 * right arrow is the half that lights while the key is held. */
static KitEvent KeyActivate(const KitItem *it) {
    switch (it->kind) {
        case KIT_TOGGLE: return Change(it, !it->value);
        case KIT_STEPPER: return Change(it, StepValue(it, 1));
        default: return Press(it);
    }
}

/* Enter, Space or pad A: pressed shows the control pressed; letting go acts. */
static KitEvent Activation(KitState *s, const KitList *l, const Input *in) {
    KitEvent none = {KIT_EV_NONE, KIT_NONE, 0};
    if (s->keyDown != KIT_NONE) {
        if (Keymap_Held(s->keyBind, in)) return none;
        int id = s->keyDown;
        s->keyDown = KIT_NONE;
        const KitItem *it = Kit_Get(l, id);
        return (id == s->focus && Kit_CanFocus(it)) ? KeyActivate(it) : none;
    }
    int bind = Keymap_Fired(KA_UI_PRESS, in);
    const KitItem *f = Kit_Get(l, s->focus);
    if (bind < 0 || !Kit_CanFocus(f)) return none;
    s->keyDown = f->id;
    s->keyBind = bind;
    return none;
}

KitEvent Kit_Update(KitState *s, const KitList *l, const Input *in) {
    RepairFocus(s, l);
    TrackPointer(s, l, in);
    KitEvent ev = Mouse(s, l, in);
    if (ev.type == KIT_EV_NONE) ev = Wheel(s, l, in);
    if (ev.type == KIT_EV_NONE) ev = Shoulders(l, in);
    if (ev.type == KIT_EV_NONE) ev = Arrows(s, l, in);
    KitEvent key = Activation(s, l, in);
    return ev.type != KIT_EV_NONE ? ev : key;
}

/* ------------------------------------------------------------- drawing */

static bool MouseHolding(const KitState *s, const KitItem *it) {
    if (s->mouseDown != it->id) return false;
    if (it->kind == KIT_SLIDER) return true; /* dragging: pressed wherever the pointer is */
    return s->hover == it->id && s->hoverCell == s->mouseCell;
}

KitLook Kit_Look(const KitState *s, const KitItem *it, InputDevice device) {
    if (it->flags & KIT_DISABLED) return KIT_LOOK_DISABLED;
    if (!s) return KIT_LOOK_NORMAL;
    if (s->keyDown == it->id || MouseHolding(s, it)) return KIT_LOOK_PRESSED;
    if (device == DEV_MOUSE) return s->hover == it->id && it->kind != KIT_AREA ? KIT_LOOK_HOVER : KIT_LOOK_NORMAL;
    return s->focus == it->id && Kit_CanFocus(it) ? KIT_LOOK_FOCUSED : KIT_LOOK_NORMAL;
}

int Kit_PressedCell(const KitState *s, const KitItem *it) {
    if (!s || (it->flags & KIT_DISABLED)) return KIT_NONE;
    if (MouseHolding(s, it)) return s->mouseCell;
    if (s->keyDown != it->id) return KIT_NONE;
    switch (it->kind) {
        case KIT_TOGGLE: return it->value ? 0 : 1; /* the half that is about to light */
        case KIT_STEPPER: return 1;
        case KIT_TABS: return it->value;
        default: return 0;
    }
}

int Kit_HoverCell(const KitState *s, const KitItem *it, InputDevice device) {
    if (!s || device != DEV_MOUSE || s->hover != it->id || (it->flags & KIT_DISABLED)) return KIT_NONE;
    return s->hoverCell;
}

const char *Kit_Reason(const KitState *s, const KitList *l, InputDevice device) {
    const KitItem *it = Kit_Get(l, s->hover);
    if (device != DEV_MOUSE || !it || !(it->flags & KIT_DISABLED)) return "";
    return it->reason;
}

bool Kit_WantsHand(const KitState *s, const KitList *l) {
    const KitItem *it = Kit_Get(l, s->hover);
    return it && it->kind != KIT_AREA && !(it->flags & KIT_DISABLED) && s->hoverCell != KIT_NONE;
}

bool Kit_OwnsWheel(const KitState *s, const KitList *l) {
    const KitItem *it = Kit_Get(l, s->hover);
    return it && !(it->flags & KIT_DISABLED) && (it->kind == KIT_SLIDER || it->kind == KIT_STEPPER);
}

/* ------------------------------------------------------------ scrolling */

void Kit_ScrollClamp(KitScroll *sc, int count, int visible) {
    int maxTop = count > visible ? count - visible : 0;
    if (sc->top > maxTop) sc->top = maxTop;
    if (sc->top < 0) sc->top = 0;
}

/* Part notches add up; away from you = up the list. */
static void WheelNotches(KitScroll *sc, float wheel) {
    if (wheel == 0.0f) return;
    sc->wheel += wheel;
    int notches = (int)sc->wheel;
    sc->wheel -= (float)notches;
    sc->top -= notches;
}

/* Clamped, and no credit piles up against an end; part notches away from
 * it count. */
static void SettleScroll(KitScroll *sc, int count, int visible) {
    Kit_ScrollClamp(sc, count, visible);
    int maxTop = count > visible ? count - visible : 0;
    if ((sc->top == 0 && sc->wheel > 0.0f) || (sc->top == maxTop && sc->wheel < 0.0f)) sc->wheel = 0.0f;
}

bool Kit_Scroll(KitScroll *sc, int count, int visible, const Input *in, bool useWheel) {
    int before = sc->top;
    int page = visible > 1 ? visible - 1 : 1; /* a page keeps one row in sight */
    if (Keymap_Pressed(KA_SCROLL_UP, in)) sc->top--;
    if (Keymap_Pressed(KA_SCROLL_DOWN, in)) sc->top++;
    if (Keymap_Pressed(KA_SCROLL_PAGE_UP, in)) sc->top -= page;
    if (Keymap_Pressed(KA_SCROLL_PAGE_DOWN, in)) sc->top += page;
    if (Keymap_Pressed(KA_SCROLL_TOP, in)) sc->top = 0;
    if (Keymap_Pressed(KA_SCROLL_END, in)) sc->top = count;
    if (useWheel) WheelNotches(sc, in->wheel);
    SettleScroll(sc, count, visible);
    return sc->top != before;
}

bool Kit_ScrollWheel(KitScroll *sc, int count, int visible, float wheel) {
    int before = sc->top;
    WheelNotches(sc, wheel);
    SettleScroll(sc, count, visible);
    return sc->top != before;
}

void Kit_ScrollShow(KitScroll *sc, int at, int count, int visible) {
    if (at < sc->top) sc->top = at;
    if (visible > 0 && at >= sc->top + visible) sc->top = at - visible + 1;
    Kit_ScrollClamp(sc, count, visible);
}

/* --------------------------------------------------------------- dialog */

void Kit_DialogOpen(KitDialog *d, KitState *s, int kind, int arg, const char *title, const char *body0,
                    const char *body1, const char *safe, const char *act) {
    memset(d, 0, sizeof(*d));
    d->open = true;
    d->kind = kind;
    d->arg = arg;
    snprintf(d->title, sizeof(d->title), "%s", title);
    snprintf(d->body[0], sizeof(d->body[0]), "%s", body0 ? body0 : "");
    snprintf(d->body[1], sizeof(d->body[1]), "%s", body1 ? body1 : "");
    snprintf(d->safe, sizeof(d->safe), "%s", safe);
    snprintf(d->act, sizeof(d->act), "%s", act);
    Kit_Enter(s, KIT_ID_DLG_SAFE);
}

static float ButtonWidth(const char *label) {
    float w = (float)Kit_TextWidth(label, 16) + 64.0f;
    return w < 176.0f ? 176.0f : w;
}

/* No wrap: Left on the safe answer stays on it. */
void Kit_DialogLayout(const KitDialog *d, KitList *l) {
    Kit_Clear(l, KIT_NAV_X);
    Kit_Add(l, KIT_ID_DLG_VEIL, KIT_AREA, (KitRect){0, 0, KIT_SCREEN_W, KIT_SCREEN_H}, "");
    Kit_Add(l, KIT_ID_DLG_BOX, KIT_AREA, (KitRect){KIT_DLG_X, KIT_DLG_Y, KIT_DLG_W, KIT_DLG_H}, "");
    float ws = ButtonWidth(d->safe), wa = ButtonWidth(d->act), gap = 32.0f;
    float x = KIT_DLG_X + (KIT_DLG_W - ws - wa - gap) / 2.0f, y = KIT_DLG_Y + KIT_DLG_H - 72;
    Kit_Add(l, KIT_ID_DLG_SAFE, KIT_BUTTON, (KitRect){x, y, ws, 44}, d->safe);
    Kit_Add(l, KIT_ID_DLG_ACT, KIT_BUTTON, (KitRect){x + ws + gap, y, wa, 44}, d->act);
}

KitDialogResult Kit_DialogAnswer(KitDialog *d, const KitEvent *ev, const Input *in) {
    if (!d->open) return KIT_DLG_SAFE;
    KitDialogResult r = KIT_DLG_OPEN;
    if (Keymap_Pressed(KA_BACK, in)) r = KIT_DLG_SAFE;
    else if (ev->type == KIT_EV_PRESS && ev->id == KIT_ID_DLG_ACT) r = KIT_DLG_ACT;
    else if (ev->type == KIT_EV_PRESS && (ev->id == KIT_ID_DLG_SAFE || ev->id == KIT_ID_DLG_VEIL)) r = KIT_DLG_SAFE;
    if (r != KIT_DLG_OPEN) d->open = false;
    return r;
}

InputDevice Kit_DialogDevice(const KitState *s, const KitList *l, InputDevice device) {
    const KitItem *it = Kit_Get(l, s->hover);
    bool onButton = it && it->kind != KIT_AREA;
    return (device == DEV_MOUSE && !onButton) ? DEV_KEYBOARD : device;
}
