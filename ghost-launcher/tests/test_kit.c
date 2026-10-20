/* Headless tests for the button kit (src/kit.c), the back stack (src/nav.c)
 * and every screen's buttons (src/screens.c). Inputs are built by hand, one
 * frame at a time, the way input.c hands them to the launcher. */
#include <string.h>
#include "check.h"
#include "../src/kit.h"
#include "../src/nav.h"
#include "../src/screens.h"

/* ------------------------------------------------------------- helpers */

static Input None(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.lastDevice = DEV_KEYBOARD;
    in.focused = true;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

/* The pointer at (x, y); moved = it got there this frame. */
static Input Pointer(float x, float y, bool moved) {
    Input in = None();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = x >= 0 && y >= 0 && x < KIT_SCREEN_W && y < KIT_SCREEN_H;
    in.mouseMoved = moved;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static Input MousePress(float x, float y) {
    Input in = Pointer(x, y, false);
    in.mousePressed[MB_LEFT] = in.mouseDown[MB_LEFT] = true;
    return in;
}

static Input MouseHeld(float x, float y) {
    Input in = Pointer(x, y, true);
    in.mouseDown[MB_LEFT] = true;
    return in;
}

static Input MouseRelease(float x, float y) {
    Input in = Pointer(x, y, false);
    in.mouseReleased[MB_LEFT] = true;
    return in;
}

static Input Key(InputKey k) {
    Input in = None();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input KeyHeld(InputKey k) {
    Input in = None();
    in.keyDown[k] = true;
    return in;
}

static Input Pad(PadButton b) {
    Input in = None();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = DEV_PAD;
    return in;
}

/* A whole click: press, then release, at one point. */
static KitEvent Click(KitState *s, const KitList *l, float x, float y) {
    Input down = MousePress(x, y), up = MouseRelease(x, y);
    KitEvent a = Kit_Update(s, l, &down);
    KitEvent b = Kit_Update(s, l, &up);
    return b.type != KIT_EV_NONE ? b : a;
}

/* A key tap: pressed this frame, let go the next. */
static KitEvent Tap(KitState *s, const KitList *l, InputKey k) {
    Input down = Key(k), up = None();
    KitEvent a = Kit_Update(s, l, &down);
    KitEvent b = Kit_Update(s, l, &up);
    return b.type != KIT_EV_NONE ? b : a;
}

static bool IsEvent(KitEvent ev, KitEventType type, int id) { return ev.type == type && ev.id == id; }

/* Buttons 1..6 in a 3 x 2 grid. */
static void Grid(KitList *l, unsigned nav) {
    Kit_Clear(l, nav);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) Kit_Add(l, 1 + r * 3 + c, KIT_BUTTON, (KitRect){100.0f + c * 120.0f, 100.0f + r * 60.0f, 100, 40}, "B");
}

/* Rows 1..5 down the screen. */
static void Column(KitList *l, unsigned nav) {
    Kit_Clear(l, nav);
    for (int i = 0; i < 5; i++) Kit_Add(l, 1 + i, KIT_ROW, (KitRect){16, 100.0f + i * 48.0f, 300, 40}, "Row");
}

/* --------------------------------------------------------------- tests */

static void test_kit_text_width(void) {
    CHECK(Kit_TextWidth("", 8) == 0, "text width: empty is 0");
    CHECK(Kit_TextWidth("A", 8) == 8, "text width: one glyph is one em");
    CHECK(Kit_TextWidth("Back", 8) == 35, "text width: 8 px adds one pixel between glyphs");
    CHECK(Kit_TextWidth("Stats", 16) == 88, "text width: 16 px adds two");
    CHECK(Kit_TextWidth("Updates", 24) == 7 * 24 + 6 * 3, "text width: 24 px adds three");
}

static void test_kit_hit(void) {
    KitList l;
    Kit_Clear(&l, 0);
    Kit_Add(&l, 1, KIT_AREA, (KitRect){0, 0, KIT_SCREEN_W, KIT_SCREEN_H}, "");
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){100, 100, 50, 20}, "A");
    Kit_Add(&l, 3, KIT_BUTTON, (KitRect){120, 100, 50, 20}, "B");
    CHECK(Kit_HitTest(&l, 110, 110) == 1, "hit: inside one button");
    CHECK(Kit_HitTest(&l, 130, 110) == 2, "hit: where two overlap, the one added last (on top) wins");
    CHECK(Kit_HitTest(&l, 100, 100) == 1, "hit: the top-left edge is inside");
    CHECK(Kit_HitTest(&l, 170, 110) == 0, "hit: the right edge is outside (falls to the area behind)");
    CHECK(Kit_HitTest(&l, 110, 120) == 0, "hit: the bottom edge is outside");
    CHECK(Kit_HitTest(&l, 5, 5) == 0, "hit: the full-screen area catches the rest");
    Kit_Clear(&l, 0);
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){100, 100, 50, 20}, "A");
    CHECK(Kit_HitTest(&l, 99, 110) == KIT_NONE, "hit: nothing there");

    /* The letterbox: a pointer outside the logical screen hovers nothing,
     * even where a rectangle reaches. */
    Kit_Clear(&l, 0);
    Kit_Add(&l, 1, KIT_BUTTON, (KitRect){-20, -20, 60, 60}, "edge");
    KitState s;
    Kit_Init(&s);
    Input in = Pointer(-5, 10, true);
    Kit_Update(&s, &l, &in);
    CHECK(s.hover == KIT_NONE, "hit: the letterbox bars hover nothing");
    in = Pointer(5, 10, true);
    Kit_Update(&s, &l, &in);
    CHECK(s.hover == 1, "hit: just inside the screen hovers it");

    /* Cells. */
    KitItem *t = Kit_Add(&l, 2, KIT_TOGGLE, (KitRect){200, 200, 120, 32}, "");
    CHECK(Kit_CellAt(t, 210, 210) == 0 && Kit_CellAt(t, 310, 210) == 1, "cells: a switch's two halves");
    KitItem *st = Kit_Add(&l, 3, KIT_STEPPER, (KitRect){400, 200, 200, 32}, "x");
    CHECK(Kit_CellAt(st, 405, 210) == 0 && Kit_CellAt(st, 595, 210) == 1, "cells: a stepper's arrows");
    CHECK(Kit_CellAt(st, 500, 210) == KIT_NONE, "cells: a stepper's value is not an arrow");
    KitRect arrow = Kit_CellRect(st, 0);
    CHECK(arrow.w >= 32 && arrow.h >= 32, "cells: stepper arrows are at least 32 x 32");
    static const char *const tabs[] = {"One", "Three"};
    KitItem *tb = Kit_Add(&l, 4, KIT_TABS, (KitRect){100, 300, 0, 40}, "");
    tb->cells = tabs;
    tb->cellCount = 2;
    KitRect c0 = Kit_CellRect(tb, 0), c1 = Kit_CellRect(tb, 1);
    CHECK(c0.w == Kit_TextWidth("One", 16) + 20 && c1.w == Kit_TextWidth("Three", 16) + 20, "cells: tabs are as wide as their labels");
    CHECK(c1.x > c0.x + c0.w, "cells: tabs do not touch");
    CHECK(Kit_CellAt(tb, c1.x + 4, 310) == 1, "cells: a click on the second tab");
    KitItem *sl = Kit_Add(&l, 5, KIT_SLIDER, (KitRect){100, 400, KIT_BLOCK_PITCH * 11, 32}, "");
    sl->max = 10;
    CHECK(Kit_SliderValueAt(sl, 104) == 0, "blocks: the space before the first block means none");
    CHECK(Kit_SliderValueAt(sl, 100 + 16 * 1 + 3) == 1, "blocks: the first block is 1");
    CHECK(Kit_SliderValueAt(sl, 100 + 16 * 10 + 8) == 10, "blocks: the last block is 10");
    CHECK(Kit_SliderValueAt(sl, 900) == 10 && Kit_SliderValueAt(sl, 0) == 0, "blocks: past either end is clamped");
}

static void test_kit_nav(void) {
    KitList l;
    Grid(&l, KIT_NAV_X | KIT_NAV_Y);
    CHECK(Kit_Navigate(&l, 1, KIT_RIGHT) == 2, "nav: right");
    CHECK(Kit_Navigate(&l, 2, KIT_LEFT) == 1, "nav: left");
    CHECK(Kit_Navigate(&l, 2, KIT_DOWN) == 5, "nav: down");
    CHECK(Kit_Navigate(&l, 5, KIT_UP) == 2, "nav: up");
    CHECK(Kit_Navigate(&l, 1, KIT_LEFT) == 1, "nav: no wrap, the left edge stays");
    CHECK(Kit_Navigate(&l, 3, KIT_RIGHT) == 3, "nav: no wrap, the right edge stays");
    CHECK(Kit_Navigate(&l, 4, KIT_DOWN) == 4, "nav: no wrap, the bottom stays");
    Grid(&l, KIT_NAV_X | KIT_NAV_Y | KIT_WRAP_X | KIT_WRAP_Y);
    CHECK(Kit_Navigate(&l, 1, KIT_LEFT) == 3, "nav: wrap X, left from the first goes to the end of its row");
    CHECK(Kit_Navigate(&l, 6, KIT_RIGHT) == 4, "nav: wrap X, right from the last goes to the start of its row");
    CHECK(Kit_Navigate(&l, 2, KIT_UP) == 5, "nav: wrap Y, up from the top goes to the bottom of its column");
    CHECK(Kit_Navigate(&l, 6, KIT_DOWN) == 3, "nav: wrap Y, down from the bottom goes to the top of its column");
    Grid(&l, KIT_NAV_X | KIT_NAV_Y | KIT_WRAP_X);
    CHECK(Kit_Navigate(&l, 2, KIT_UP) == 2, "nav: wrap X only does not wrap vertically");

    /* Disabled, mouse-only and areas are skipped. */
    Column(&l, KIT_NAV_Y | KIT_WRAP_Y);
    Kit_Disable(&l.items[2], "not now");
    CHECK(Kit_Navigate(&l, 2, KIT_DOWN) == 4, "nav: a disabled row is skipped");
    CHECK(Kit_Navigate(&l, 4, KIT_UP) == 2, "nav: ...both ways");
    l.items[4].flags |= KIT_NOFOCUS;
    CHECK(Kit_Navigate(&l, 4, KIT_DOWN) == 1, "nav: a mouse-only last row is skipped, wrapping to the top");
    CHECK(Kit_Navigate(&l, 1, KIT_UP) == 4, "nav: ...and back round from the top");
    CHECK(Kit_FirstFocus(&l) == 1, "nav: the first focusable item");
    CHECK(Kit_Navigate(&l, 99, KIT_DOWN) == 1, "nav: from something not in the list: the first item");
    CHECK(Kit_Navigate(&l, 3, KIT_DOWN) == 4, "nav: from a control disabled while focused, on from where it is");

    /* An item in line beats a nearer one off to the side. */
    Kit_Clear(&l, KIT_NAV_X);
    Kit_Add(&l, 1, KIT_BUTTON, (KitRect){100, 100, 40, 40}, "a");
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){150, 200, 40, 40}, "near, below");
    Kit_Add(&l, 3, KIT_BUTTON, (KitRect){400, 100, 40, 40}, "far, in line");
    CHECK(Kit_Navigate(&l, 1, KIT_RIGHT) == 3, "nav: in line wins over nearer off to the side");

    /* Nothing to focus. */
    Kit_Clear(&l, KIT_NAV_X | KIT_NAV_Y);
    Kit_Add(&l, 1, KIT_AREA, (KitRect){0, 0, 100, 100}, "");
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){0, 0, 10, 10}, "")->flags |= KIT_NOFOCUS;
    CHECK(Kit_FirstFocus(&l) == KIT_NONE, "nav: only areas and mouse-only buttons: nothing to focus");
    CHECK(Kit_Navigate(&l, KIT_NONE, KIT_DOWN) == KIT_NONE, "nav: ...and nowhere to go");

    /* The arrows move the focus only along the list's axes. */
    KitState s;
    Grid(&l, KIT_NAV_Y);
    Kit_Enter(&s, 1);
    Input in = Key(IK_RIGHT);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 1, "nav: Right is the screen's when the list has no X navigation");
    in = Key(IK_DOWN);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 4, "nav: Down moves along Y");
    in = Pad(PB_UP);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 1, "nav: the d-pad moves it too");
    in = None();
    in.keyRepeat[IK_DOWN] = true; /* a held key's repeat step */
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 4, "nav: a repeat step moves it");
}

static void test_kit_press_release(void) {
    KitList l;
    KitState s;
    Kit_Clear(&l, KIT_NAV_Y);
    Kit_Add(&l, 1, KIT_BUTTON, (KitRect){100, 100, 100, 40}, "One");
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){100, 200, 100, 40}, "Two");
    Kit_Add(&l, 3, KIT_BUTTON, (KitRect){100, 300, 100, 40}, "Off");
    Kit_Disable(&l.items[2], "Not now");
    Kit_Init(&s);

    /* The mouse acts when let go over the same control. */
    Input in = MousePress(150, 120);
    KitEvent ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_NONE, "mouse: nothing happens on the press");
    CHECK(Kit_Look(&s, &l.items[0], DEV_MOUSE) == KIT_LOOK_PRESSED, "mouse: held down, it looks pressed");
    in = MouseRelease(150, 120);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_PRESS, 1), "mouse: letting go over it acts");
    CHECK(IsEvent(Click(&s, &l, 150, 220), KIT_EV_PRESS, 2), "mouse: a click on the other one");

    /* Drag off: nothing. Drag back on: it acts. */
    in = MousePress(150, 120);
    Kit_Update(&s, &l, &in);
    in = MouseHeld(400, 400);
    Kit_Update(&s, &l, &in);
    CHECK(Kit_Look(&s, &l.items[0], DEV_MOUSE) == KIT_LOOK_NORMAL, "mouse: dragged off, it no longer looks pressed");
    in = MouseRelease(400, 400);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_NONE, "mouse: let go elsewhere, nothing happens");
    in = MousePress(150, 120);
    Kit_Update(&s, &l, &in);
    in = MouseHeld(400, 400);
    Kit_Update(&s, &l, &in);
    in = MouseHeld(160, 125);
    Kit_Update(&s, &l, &in);
    CHECK(Kit_Look(&s, &l.items[0], DEV_MOUSE) == KIT_LOOK_PRESSED, "mouse: dragged back on, pressed again");
    in = MouseRelease(160, 125);
    CHECK(IsEvent(Kit_Update(&s, &l, &in), KIT_EV_PRESS, 1), "mouse: ...and letting go there acts");

    /* Pressed on one, let go on another: neither. */
    in = MousePress(150, 120);
    Kit_Update(&s, &l, &in);
    in = MouseRelease(150, 220);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "mouse: down on one, up on another: nothing");
    /* Pressed on nothing, let go on a button: nothing. */
    in = MousePress(400, 400);
    Kit_Update(&s, &l, &in);
    in = MouseRelease(150, 120);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "mouse: down on nothing, up on a button: nothing");

    /* A click shorter than a frame still counts. */
    in = Pointer(150, 120, false);
    in.mousePressed[MB_LEFT] = in.mouseReleased[MB_LEFT] = true;
    CHECK(IsEvent(Kit_Update(&s, &l, &in), KIT_EV_PRESS, 1), "mouse: press and release in one frame acts");

    /* Disabled: never acts, always faint. */
    CHECK(Click(&s, &l, 150, 320).type == KIT_EV_NONE, "disabled: a click does nothing");
    CHECK(Kit_Look(&s, &l.items[2], DEV_MOUSE) == KIT_LOOK_DISABLED, "disabled: it looks disabled");
    CHECK(strcmp(Kit_Reason(&s, &l, DEV_MOUSE), "Not now") == 0, "disabled: under the mouse, it says why");
    CHECK(strcmp(Kit_Reason(&s, &l, DEV_KEYBOARD), "") == 0, "disabled: the reason is the mouse's");
    CHECK(!Kit_WantsHand(&s, &l), "disabled: no hand over it");

    /* Keys act when let go, and show pressed while held. */
    Kit_Enter(&s, 1);
    in = Key(IK_ENTER);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_NONE, "keys: Enter does nothing on the press");
    CHECK(Kit_Look(&s, &l.items[0], DEV_KEYBOARD) == KIT_LOOK_PRESSED, "keys: held Enter shows it pressed");
    in = KeyHeld(IK_ENTER);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "keys: still held, still nothing");
    in = None();
    CHECK(IsEvent(Kit_Update(&s, &l, &in), KIT_EV_PRESS, 1), "keys: letting Enter go acts");
    CHECK(IsEvent(Tap(&s, &l, IK_SPACE), KIT_EV_PRESS, 1), "keys: Space too");
    in = Pad(PB_A);
    Kit_Update(&s, &l, &in);
    in = None();
    CHECK(IsEvent(Kit_Update(&s, &l, &in), KIT_EV_PRESS, 1), "pad: A too");
    in = Key(IK_ENTER);
    in.alt = true;
    Kit_Update(&s, &l, &in);
    in = None();
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "keys: Alt+Enter is fullscreen, never a press");
    /* A press does not follow the focus. */
    in = Key(IK_ENTER);
    Kit_Update(&s, &l, &in);
    in = KeyHeld(IK_ENTER);
    in.key[IK_DOWN] = in.keyRepeat[IK_DOWN] = true;
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 2, "keys: Down while Enter is held moves the focus");
    in = None();
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "keys: ...and letting Enter go presses nothing");

    /* The mouse hands the focus over; the first key carries on from there. */
    Kit_Enter(&s, 1);
    in = Pointer(150, 220, true);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 2, "focus: moving the mouse onto a control focuses it");
    in = Pointer(150, 120, false);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 2 && s.hover == 1, "focus: a list moving under a still pointer changes the hover, not the focus");
    in = Pointer(150, 320, true);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 2, "focus: a disabled control does not take it");
    in = Key(IK_UP);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 1, "focus: the first key press carries on from where the mouse left it");
    l.items[1].flags |= KIT_NOFOCUS;
    in = Pointer(150, 220, true);
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 1, "focus: a mouse-only control does not take it");
    l.items[1].flags &= ~(unsigned)KIT_NOFOCUS;

    /* A new screen forgets a half-made click. */
    in = MousePress(150, 120);
    Kit_Update(&s, &l, &in);
    Kit_Enter(&s, 1);
    in = MouseRelease(150, 120);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "enter: a click begun on the last screen never acts here");
    in = Key(IK_ENTER);
    Kit_Update(&s, &l, &in);
    Kit_Enter(&s, 1);
    in = None();
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "enter: neither does a held Enter");

    /* A focus on something that went away comes back to the first control. */
    Kit_Enter(&s, 42);
    in = None();
    Kit_Update(&s, &l, &in);
    CHECK(s.focus == 1, "focus: one that is not in the list is repaired");
}

static void test_kit_values(void) {
    KitList l;
    KitState s;
    Kit_Clear(&l, KIT_NAV_Y);
    KitItem *t = Kit_Add(&l, 1, KIT_TOGGLE, (KitRect){100, 100, 120, 32}, "");
    KitItem *st = Kit_Add(&l, 2, KIT_STEPPER, (KitRect){100, 200, 200, 32}, "v");
    st->value = 2;
    st->max = 4;
    KitItem *sl = Kit_Add(&l, 3, KIT_SLIDER, (KitRect){100, 300, KIT_BLOCK_PITCH * 11, 32}, "");
    sl->value = 3;
    sl->max = 10;
    static const char *const tabs[] = {"One", "Two", "Three"};
    KitItem *tb = Kit_Add(&l, 4, KIT_TABS, (KitRect){100, 400, 0, 40}, "");
    tb->cells = tabs;
    tb->cellCount = 3;
    Kit_Init(&s);

    /* The switch. */
    KitEvent ev = Click(&s, &l, 200, 110);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 1) && ev.value == 1, "switch: a click on On turns it on");
    t->value = 1;
    CHECK(Click(&s, &l, 200, 110).type == KIT_EV_NONE, "switch: On again changes nothing");
    ev = Click(&s, &l, 110, 110);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 1) && ev.value == 0, "switch: a click on Off turns it off");
    Input in = MousePress(110, 110);
    Kit_Update(&s, &l, &in);
    in = MouseRelease(200, 110);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "switch: down on one half, up on the other: nothing");
    Kit_Enter(&s, 1);
    in = Key(IK_LEFT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 1) && ev.value == 0, "switch: Left is Off");
    CHECK(s.focus == 1, "switch: Left changes it, the focus stays");
    t->value = 0;
    in = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 1) && ev.value == 1, "switch: Right is On");
    ev = Tap(&s, &l, IK_ENTER);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 1) && ev.value == 1, "switch: Enter flips it");

    /* The stepper. */
    ev = Click(&s, &l, 290, 210);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 3, "stepper: the right arrow adds one");
    ev = Click(&s, &l, 110, 210);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 1, "stepper: the left arrow takes one off");
    CHECK(Click(&s, &l, 200, 210).type == KIT_EV_NONE, "stepper: a click on the value does nothing");
    Kit_Enter(&s, 2);
    in = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 3, "stepper: Right adds one");
    st->value = 4;
    in = Key(IK_RIGHT);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "stepper: at the top it stays");
    st->flags |= KIT_WRAP;
    in = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 0, "stepper: wrapping, past the top comes round to the bottom");
    st->value = 0;
    in = Pad(PB_LEFT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 4, "stepper: wrapping, the d-pad goes round the other way");
    in = Pointer(200, 210, false);
    in.wheel = 1.0f;
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 1, "stepper: the wheel over it steps it");
    in = Key(IK_ENTER);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE && Kit_PressedCell(&s, st) == 1,
          "stepper: held Enter lights the right arrow");
    in = None();
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 1, "stepper: letting Enter go steps it on, as that arrow would");
    st->value = 4;
    ev = Tap(&s, &l, IK_SPACE);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 0, "stepper: Space wraps it past the top");
    st->flags &= ~(unsigned)KIT_WRAP;
    CHECK(Tap(&s, &l, IK_ENTER).type == KIT_EV_NONE, "stepper: no wrap, at the top Enter leaves it");

    /* The ten blocks. */
    Kit_Init(&s);
    in = MousePress(100 + 16 * 6 + 4, 310);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 3) && ev.value == 6, "blocks: a click sets the value at once");
    sl->value = 6;
    in = MouseHeld(100 + 16 * 9 + 4, 330);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 9, "blocks: dragging follows the pointer");
    sl->value = 9;
    CHECK(Kit_Look(&s, sl, DEV_MOUSE) == KIT_LOOK_PRESSED, "blocks: pressed while dragging");
    in = MouseHeld(20, 500);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 0, "blocks: dragged off the left end is none, even off the row");
    sl->value = 0;
    in = MouseRelease(20, 500);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "blocks: letting go adds nothing");
    sl->value = 3;
    in = Pointer(150, 310, false);
    in.wheel = 1.0f;
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 4, "blocks: a wheel notch is one block");
    in.wheel = 0.5f;
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "blocks: half a notch (a touchpad) waits");
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 4, "blocks: the other half makes one step");
    CHECK(Kit_OwnsWheel(&s, &l), "blocks: the wheel over them is theirs, not the screen's");
    Kit_Enter(&s, 3);
    sl->value = 10;
    in = Key(IK_RIGHT);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "blocks: Right at ten stays");
    in = Key(IK_LEFT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 9, "blocks: Left takes one off");
    Kit_Disable(sl, "Waiting");
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_NONE && s.focus == 3, "blocks: disabled while focused, the focus stays and Left changes nothing");
    in = Pointer(150, 310, false);
    in.wheel = 1.0f;
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "blocks: ...nor does the wheel");
    sl->flags &= ~(unsigned)KIT_DISABLED;

    /* The tabs. */
    Kit_Init(&s);
    KitRect c2 = Kit_CellRect(tb, 2);
    ev = Click(&s, &l, c2.x + 5, c2.y + 5);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 4) && ev.value == 2, "tabs: a click opens that tab");
    Kit_Enter(&s, 1); /* the focus is on the switch, not the tabs */
    in = Pad(PB_RB);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 4) && ev.value == 1, "tabs: RB is the next tab, wherever the focus is");
    tb->value = 1;
    in = Pad(PB_LB);
    ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 4) && ev.value == 0, "tabs: LB the one before");
    tb->value = 2;
    in = Pad(PB_RB);
    CHECK(Kit_Update(&s, &l, &in).type == KIT_EV_NONE, "tabs: RB on the last one stays without wrap");
    tb->flags |= KIT_WRAP;
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 0, "tabs: ...and goes round with it");
    Kit_Enter(&s, 4);
    tb->value = 0;
    in = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 1, "tabs: focused, Right is the next tab");
}

static void test_kit_looks(void) {
    KitList l;
    KitState s;
    Kit_Clear(&l, KIT_NAV_Y);
    Kit_Add(&l, 1, KIT_BUTTON, (KitRect){100, 100, 100, 40}, "One");
    Kit_Add(&l, 2, KIT_BUTTON, (KitRect){100, 200, 100, 40}, "Two");
    Kit_Add(&l, 3, KIT_AREA, (KitRect){0, 500, 960, 140}, "");
    Kit_Enter(&s, 1);
    Input in = Pointer(150, 220, false); /* resting over Two, focus on One */
    Kit_Update(&s, &l, &in);
    CHECK(Kit_Look(&s, &l.items[0], DEV_KEYBOARD) == KIT_LOOK_FOCUSED, "looks: keys in use, the focus shows");
    CHECK(Kit_Look(&s, &l.items[1], DEV_KEYBOARD) == KIT_LOOK_NORMAL, "looks: keys in use, the hover does not");
    CHECK(Kit_Look(&s, &l.items[1], DEV_MOUSE) == KIT_LOOK_HOVER, "looks: the mouse in use, the hover shows");
    CHECK(Kit_Look(&s, &l.items[0], DEV_MOUSE) == KIT_LOOK_NORMAL, "looks: the mouse in use, the focus bar hides");
    CHECK(Kit_Look(&s, &l.items[0], DEV_PAD) == KIT_LOOK_FOCUSED, "looks: the pad shows the focus too");
    CHECK(Kit_Look(NULL, &l.items[0], DEV_KEYBOARD) == KIT_LOOK_NORMAL, "looks: no state (dimmed behind an overlay): at rest");
    CHECK(Kit_WantsHand(&s, &l), "looks: a hand over a button");
    in = Pointer(150, 600, true);
    Kit_Update(&s, &l, &in);
    CHECK(!Kit_WantsHand(&s, &l), "looks: no hand over an area");
    CHECK(Kit_Look(&s, &l.items[2], DEV_MOUSE) == KIT_LOOK_NORMAL, "looks: an area never lights");
}

static void test_kit_scroll(void) {
    KitScroll sc = {0, 0.0f};
    Input in = Key(IK_DOWN);
    for (int i = 0; i < 10; i++) Kit_Scroll(&sc, 20, 13, &in, true);
    CHECK(sc.top == 7, "scroll: Down stops when the last row is in sight");
    in = Key(IK_UP);
    CHECK(Kit_Scroll(&sc, 20, 13, &in, true) && sc.top == 6, "scroll: Up");
    in = Key(IK_HOME);
    Kit_Scroll(&sc, 20, 13, &in, true);
    CHECK(sc.top == 0, "scroll: Home");
    in = Key(IK_END);
    Kit_Scroll(&sc, 20, 13, &in, true);
    CHECK(sc.top == 7, "scroll: End");
    in = Key(IK_PAGE_UP);
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 0, "scroll: PgUp, a page less one row");
    in = Key(IK_PAGE_DOWN);
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 12, "scroll: PgDn");
    in = Pad(PB_RB);
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 24, "scroll: RB pages too");
    in = Pad(PB_DOWN);
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 25, "scroll: the d-pad steps");
    sc = (KitScroll){5, 0.0f};
    in = None();
    in.wheel = -1.0f;
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 6, "scroll: the wheel toward you goes down the list");
    in.wheel = 0.4f;
    CHECK(!Kit_Scroll(&sc, 40, 13, &in, true), "scroll: part of a notch waits");
    in.wheel = 0.6f;
    CHECK(Kit_Scroll(&sc, 40, 13, &in, true) && sc.top == 5, "scroll: ...for the rest of it");
    in.wheel = 1.0f;
    CHECK(!Kit_Scroll(&sc, 40, 13, &in, false), "scroll: a wheel the screen does not own does nothing");
    sc = (KitScroll){0, 0.0f};
    in.wheel = 3.0f;
    Kit_Scroll(&sc, 40, 13, &in, true);
    in.wheel = -1.0f;
    Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 1, "scroll: notches pushed against the top are not saved up");
    sc = (KitScroll){0, 0.0f};
    in.wheel = -0.5f;
    for (int i = 0; i < 6; i++) Kit_Scroll(&sc, 40, 13, &in, true);
    CHECK(sc.top == 3, "scroll: a touchpad's half notches leave the top (only credit against the end is dropped)");
    sc = (KitScroll){4, 0.0f};
    in = Key(IK_DOWN);
    Kit_Scroll(&sc, 10, 13, &in, true);
    CHECK(sc.top == 0, "scroll: a list that fits never scrolls");
}

static void test_dialog_flow(void) {
    KitDialog d;
    KitState s;
    KitList l;
    Kit_Init(&s);
    Kit_DialogOpen(&d, &s, 7, 3, "Remove Blockfall?", "It leaves the row.", NULL, "Keep", "Remove");
    CHECK(d.open && d.kind == 7 && d.arg == 3, "dialog: open, with the caller's kind and argument");
    CHECK(s.focus == KIT_ID_DLG_SAFE, "dialog: the safe answer has the focus");
    Screens_Dialog(&l, &d);
    const KitItem *safe = Kit_Get(&l, KIT_ID_DLG_SAFE), *act = Kit_Get(&l, KIT_ID_DLG_ACT);
    KitRect box = {KIT_DLG_X, KIT_DLG_Y, KIT_DLG_W, KIT_DLG_H};
    CHECK(safe && act && safe->r.x + safe->r.w <= act->r.x, "dialog: the safe answer is on the left");
    CHECK(safe && act && Kit_Inside(box, safe->r.x, safe->r.y) && Kit_Inside(box, act->r.x + act->r.w - 1, act->r.y + act->r.h - 1),
          "dialog: both answers are inside the box");
    CHECK(safe && strcmp(safe->label, "Keep") == 0 && act && strcmp(act->label, "Remove") == 0, "dialog: the answers' labels");
    CHECK(Kit_HitTest(&l, CLOSE_BTN_X + 12, CLOSE_BTN_Y + 12) == l.count - 1, "dialog: the X stays on top of it");

    /* Every way of saying no. */
    Input backs[4];
    backs[0] = Key(IK_ESCAPE);
    backs[1] = Pad(PB_B);
    backs[2] = None();
    backs[2].mousePressed[MB_BACK] = true;
    backs[3] = None();
    const char *names[4] = {"dialog: Esc is the safe answer", "dialog: pad B is the safe answer",
                            "dialog: the mouse's back button is the safe answer", "dialog: an idle frame keeps it open"};
    for (int i = 0; i < 4; i++) {
        Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
        KitEvent ev = Kit_Update(&s, &l, &backs[i]);
        KitDialogResult r = Kit_DialogAnswer(&d, &ev, &backs[i]);
        CHECK(i < 3 ? (r == KIT_DLG_SAFE && !d.open) : (r == KIT_DLG_OPEN && d.open), names[i]);
    }
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    KitEvent ev = Click(&s, &l, 20, 20);
    Input idle = None();
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_SAFE, "dialog: a click outside the box is the safe answer");
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    ev = Click(&s, &l, KIT_DLG_X + 20, KIT_DLG_Y + 20);
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_OPEN && d.open, "dialog: a click on the box itself does nothing");

    /* Saying yes. */
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    Input right = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &right);
    CHECK(s.focus == KIT_ID_DLG_ACT, "dialog: Right moves to the other answer");
    ev = Tap(&s, &l, IK_ENTER);
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_ACT && !d.open, "dialog: Enter on it says yes");
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    Input left = Key(IK_LEFT);
    Kit_Update(&s, &l, &left);
    CHECK(s.focus == KIT_ID_DLG_SAFE, "dialog: Left on the safe answer stays there (no wrap to yes)");
    ev = Tap(&s, &l, IK_ENTER);
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_SAFE, "dialog: Enter on it says no");
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    act = Kit_Get(&l, KIT_ID_DLG_ACT);
    ev = Click(&s, &l, act->r.x + 10, act->r.y + 10);
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_ACT, "dialog: a click on yes says yes");
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    Input down = MousePress(act->r.x + 10, act->r.y + 10), up = MouseRelease(KIT_DLG_X + 20, KIT_DLG_Y + 20);
    Kit_Update(&s, &l, &down);
    ev = Kit_Update(&s, &l, &up);
    CHECK(Kit_DialogAnswer(&d, &ev, &idle) == KIT_DLG_OPEN, "dialog: pressing yes and dragging off answers nothing");
    d.open = false;
    CHECK(Kit_DialogAnswer(&d, &ev, &right) == KIT_DLG_SAFE, "dialog: a closed one never says yes");

    /* Opened by a click, it still shows which answer Enter would give. */
    Kit_DialogOpen(&d, &s, 7, 3, "Remove?", "", NULL, "Keep", "Remove");
    safe = Kit_Get(&l, KIT_ID_DLG_SAFE);
    Input veil = Pointer(20, 20, true);
    Kit_Update(&s, &l, &veil);
    InputDevice dev = Kit_DialogDevice(&s, &l, DEV_MOUSE);
    CHECK(Kit_Look(&s, safe, dev) == KIT_LOOK_FOCUSED, "dialog: by mouse, off the buttons, the safe answer shows its focus");
    Input onAct = Pointer(act->r.x + 10, act->r.y + 10, true);
    Kit_Update(&s, &l, &onAct);
    dev = Kit_DialogDevice(&s, &l, DEV_MOUSE);
    CHECK(Kit_Look(&s, act, dev) == KIT_LOOK_HOVER && Kit_Look(&s, safe, dev) == KIT_LOOK_NORMAL,
          "dialog: on an answer the mouse looks take over");
    Input onX = Pointer(CLOSE_BTN_X + 12, CLOSE_BTN_Y + 12, true);
    Kit_Update(&s, &l, &onX);
    CHECK(Kit_DialogDevice(&s, &l, DEV_MOUSE) == DEV_MOUSE, "dialog: the X still lights under the mouse");
    CHECK(Kit_DialogDevice(&s, &l, DEV_PAD) == DEV_PAD, "dialog: the pad keeps its own looks");
}

static void test_nav_stack(void) {
    Nav n;
    Nav_Init(&n, 0);
    CHECK(Nav_Top(&n) == 0 && Nav_Depth(&n) == 1, "nav stack: starts at the root");
    CHECK(!Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: Back at the root does nothing");
    /* The hall (0), the Arcade menu (8), Stats (3). */
    Nav_Push(&n, 8);
    Nav_Push(&n, 3);
    CHECK(Nav_Top(&n) == 3 && Nav_Under(&n) == 8 && Nav_Depth(&n) == 3, "nav stack: Stats from the menu");
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 8, "nav stack: Back from Stats goes to the menu it came from");
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: ...and Back again to the hall");
    Nav_Push(&n, 3);
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: Stats from the hall goes back to the hall");
    Nav_Push(&n, 8);
    Nav_Push(&n, 7);
    Nav_Push(&n, 8);
    CHECK(Nav_Top(&n) == 8 && Nav_Depth(&n) == 2, "nav stack: opening one already open goes back to it (no loops)");
    Nav_Push(&n, 0);
    CHECK(Nav_Depth(&n) == 1, "nav stack: opening the root goes home");
    Nav_Push(&n, 8);
    Nav_Push(&n, 5);
    CHECK(Nav_Contains(&n, 8) && !Nav_Contains(&n, 3), "nav stack: what is open");
    Nav_Home(&n);
    CHECK(Nav_Top(&n) == 0 && Nav_Depth(&n) == 1, "nav stack: home");
    for (int i = 1; i < NAV_DEPTH; i++) Nav_Push(&n, i);
    CHECK(Nav_Depth(&n) == NAV_DEPTH && !Nav_Push(&n, 99) && Nav_Top(&n) == NAV_DEPTH - 1, "nav stack: full, a push is refused");
    CHECK(Nav_Under(&n) == NAV_DEPTH - 2, "nav stack: under the top");
}

/* Every list: on screen, focusable items never overlap, labels fit. */
static bool ListIsSane(const KitList *l) {
    for (int i = 0; i < l->count; i++) {
        const KitItem *a = &l->items[i];
        KitRect ab = Kit_Bounds(a);
    if (ab.x < 0 || ab.y < 0 || ab.x + ab.w > KIT_SCREEN_W || ab.y + ab.h > KIT_SCREEN_H) return false;
        if ((a->kind == KIT_BUTTON) && a->label[0] && a->style != KIT_STYLE_PILL && !(a->flags & KIT_SELFDRAWN) &&
            Kit_TextWidth(a->label, a->textSize) + (a->icon ? 14 : 0) > a->r.w - 8) return false;
        for (int j = i + 1; j < l->count; j++) {
            const KitItem *b = &l->items[j];
            if (!Kit_CanFocus(a) || !Kit_CanFocus(b)) continue;
            if (a->r.x < b->r.x + b->r.w && b->r.x < a->r.x + a->r.w && a->r.y < b->r.y + b->r.h && b->r.y < a->r.y + a->r.h) return false;
        }
    }
    return true;
}

static bool HasBackAndClose(const KitList *l) {
    const KitItem *back = Kit_Get(l, WID_BACK);
    bool backOk = back && back->r.x == BACK_BTN_X && back->r.y == BACK_BTN_Y && !Kit_CanFocus(back);
    bool closeOnTop = Kit_HitTest(l, CLOSE_BTN_X + 12, CLOSE_BTN_Y + 12) == l->count - 1 && l->items[l->count - 1].id == WID_CLOSE;
    return backOk && closeOnTop;
}

static bool Overlaps(KitRect a, KitRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

/* "Playing as" in the header: a chip that never runs into its neighbours. */
static void test_playing_as(void) {
    PlayingAs pa;
    KitRect chip = {MENU_CHIP_X, CLOSE_BTN_Y, MENU_CHIP_W, CLOSE_BTN_SIZE};
    KitRect pill = {PILL_X, CLOSE_BTN_Y, PILL_W, CLOSE_BTN_SIZE};
    Screens_PlayingAs(&pa, "TRG", false, false);
    CHECK(strcmp(pa.label, "Playing as ") == 0 && strcmp(pa.name, "TRG") == 0 && !pa.empty, "playing as: label and name");
    CHECK(pa.r.x + pa.r.w == MENU_CHIP_X - HEADER_GAP && pa.nameX + Kit_TextWidth(pa.name, 8) == (int)(pa.r.x + pa.r.w) - 8 &&
              pa.labelX + Kit_TextWidth(pa.label, 8) == pa.nameX,
          "playing as: a chip 12 px left of Menu, its words right-aligned inside it");
    CHECK(!Overlaps(pa.r, chip) && pa.r.x == pa.labelX - 8 && pa.r.y == CLOSE_BTN_Y && pa.r.h == CLOSE_BTN_SIZE,
          "playing as: the chip covers the words and its padding, at the header's height");
    Screens_PlayingAs(&pa, "TRG", false, true);
    CHECK(pa.r.x + pa.r.w == PILL_X - HEADER_GAP && !Overlaps(pa.r, pill), "playing as: left of NEW VERSION when there is one");
    Screens_PlayingAs(&pa, "", false, false);
    CHECK(pa.empty && strcmp(pa.name, "Set your name") == 0 && !pa.label[0], "playing as: no name is a prompt to set one");
    Screens_PlayingAs(&pa, "A very long name for the header", true, true);
    CHECK(!pa.label[0] && strcmp(pa.name, "A very long name for the") == 0 && pa.r.x >= Screens_HeaderLeft(true),
          "playing as: admin and the notice crowd it: the label goes first, the name (24 at most) stays whole");
    bool fits = true;
    static const char *const names[] = {"", "A", "TRG", "Montgomery", "Montgomery-Abernathy", "Montgomery-Abernathy-Smith-Jones-Q"};
    for (int admin = 0; admin < 2; admin++)
        for (int behind = 0; behind < 2; behind++)
            for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
                Screens_PlayingAs(&pa, names[i], admin, behind);
                float right = (behind ? PILL_X : MENU_CHIP_X) - HEADER_GAP;
                if (pa.r.x < Screens_HeaderLeft(admin) || pa.r.x + pa.r.w != right) fits = false;
                if (Kit_TextWidth(pa.label, 8) + Kit_TextWidth(pa.name, 8) + 16 > pa.r.w) fits = false;
                if (Overlaps(pa.r, chip) || (behind && Overlaps(pa.r, pill))) fits = false;
            }
    CHECK(fits, "playing as: every name, with and without admin and the notice, fits between its neighbours");
}

static void test_screens(void) {
    KitList l;
    HallView hv = {.arcadeName = "TRG", .dailyReason = ""};
    Screens_Hall(&l, &hv);
    CHECK(Kit_Get(&l, WID_MENU_CHIP) && Kit_Get(&l, WID_EXIT_SIGN) && Kit_Get(&l, WID_CLOSE), "hall: Menu, EXIT and the X");
    const KitItem *pa = Kit_Get(&l, WID_PLAYING_AS);
    CHECK(pa && !Kit_CanFocus(pa) && (pa->flags & KIT_SELFDRAWN) && !Overlaps(pa->r, Kit_Get(&l, WID_MENU_CHIP)->r),
          "hall: Playing as is a button for the mouse, drawn in the header");
    CHECK(!Kit_Get(&l, WID_BACK), "hall: no Back (Esc opens the menu there)");
    CHECK(!Kit_Get(&l, WID_UPDATE_PILL), "hall: no update notice while there is nothing new");
    CHECK(Kit_FirstFocus(&l) == KIT_NONE, "hall: its buttons are the mouse's; the keys keep the row");
    CHECK(ListIsSane(&l), "hall: buttons on screen, labels fit");
    const KitItem *chip = Kit_Get(&l, WID_MENU_CHIP);
    CHECK(chip && chip->r.x + chip->r.w < CLOSE_BTN_X, "hall: Menu sits left of the X");
    hv.updateBehind = true;
    Screens_Hall(&l, &hv);
    CHECK(Kit_Get(&l, WID_UPDATE_PILL) && strcmp(Kit_Get(&l, WID_UPDATE_PILL)->label, "NEW VERSION") == 0,
          "hall: the notice says NEW VERSION, not a count of changes");
    CHECK(Kit_Get(&l, WID_UPDATE_PILL)->r.y == CLOSE_BTN_Y && Kit_Get(&l, WID_UPDATE_PILL)->r.x + PILL_W + HEADER_GAP == MENU_CHIP_X,
          "hall: the notice is a header chip, left of Menu");
    hv.coinInFlight = true;
    Screens_Hall(&l, &hv);
    CHECK(l.count == 1 && l.items[0].id == WID_CLOSE, "hall: while a coin drops, only the X works");

    /* The Arcade menu. */
    Screens_Menu(&l, false, DEV_KEYBOARD);
    static const char *const labels[AM_COUNT] = {"Back to the hall", "Daily challenge", "Settings", "Stats", "Trophies", "Updates", "Leave the arcade"};
    static const char *const keys[AM_COUNT] = {"Esc", "", "Tab", "L", "H", "V", ""};
    bool itemsOk = true, insidePanel = true, fits = true;
    for (int i = 0; i < AM_COUNT; i++) {
        const KitItem *it = Kit_Get(&l, WID_MENU_ITEM + i);
        if (!it || strcmp(it->label, labels[i]) != 0 || strcmp(it->key, keys[i]) != 0 || !Kit_CanFocus(it)) itemsOk = false;
        if (it && it->r.x + it->r.w > MENU_PANEL_W) insidePanel = false;
        if (it && 20 + Kit_TextWidth(it->label, 16) + 12 + Kit_TextWidth(it->key, 8) + 12 > it->r.w) fits = false;
        if (Screens_MenuItemFor(WID_MENU_ITEM + i) != i) itemsOk = false;
    }
    CHECK(itemsOk, "menu: the six items, Settings in place of Your name and Online sharing, each with its key");
    CHECK(insidePanel, "menu: every item is inside the panel over the left wall");
    CHECK(fits, "menu: label and key fit each row");
    CHECK(Screens_MenuItemFor(WID_CLOSE) == -1 && Screens_MenuItemFor(WID_MENU_ITEM + AM_COUNT) == -1, "menu: other ids are no item");
    CHECK(Kit_FirstFocus(&l) == WID_MENU_ITEM + AM_HALL, "menu: opens on Back to the hall");
    CHECK(Kit_Navigate(&l, WID_MENU_ITEM + AM_HALL, KIT_UP) == WID_MENU_ITEM + AM_LEAVE, "menu: Up from the top wraps to Leave");
    CHECK(Kit_Navigate(&l, WID_MENU_ITEM + AM_LEAVE, KIT_DOWN) == WID_MENU_ITEM + AM_HALL, "menu: Down from Leave wraps to the top");
    CHECK(Kit_Get(&l, WID_MENU_ITEM + AM_UPDATES)->badge[0] == '\0', "menu: no badge with nothing new");
    CHECK(Kit_HitTest(&l, MENU_PANEL_W + 50, 300) >= 0 && l.items[Kit_HitTest(&l, MENU_PANEL_W + 50, 300)].id == WID_MENU_VEIL,
          "menu: beside the panel is the veil (a click there closes it)");
    CHECK(l.items[Kit_HitTest(&l, MENU_PANEL_W - 8, 600)].id == WID_MENU_PANEL, "menu: the panel's empty space is not the veil");
    CHECK(ListIsSane(&l), "menu: buttons on screen");
    Screens_Menu(&l, true, DEV_KEYBOARD);
    CHECK(strcmp(Kit_Get(&l, WID_MENU_ITEM + AM_UPDATES)->badge, "1 new") == 0, "menu: Updates says 1 new");
    Screens_Menu(&l, false, DEV_PAD);
    static const char *const padKeys[AM_COUNT] = {"B", "", "", "View", "", "", ""};
    bool padOk = true;
    for (int i = 0; i < AM_COUNT; i++) if (strcmp(Kit_Get(&l, WID_MENU_ITEM + i)->key, padKeys[i]) != 0) padOk = false;
    CHECK(padOk, "menu: after pad use, the pad's buttons where it has one (B back, View stats), no keyboard keys");

    Input in = Key(IK_L);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_STATS, "keys: L is Stats");
    in = Key(IK_H);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_TROPHIES, "keys: H is Trophies");
    in = Key(IK_U);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_NAME, "keys: U is your name");
    in.shift = true;
    CHECK(Screens_Shortcut(&in) == KA_OPEN_GAME_NAME, "keys: Shift+U is the name for this game");
    in = Key(IK_O);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_ONLINE, "keys: O is online");
    in = Key(IK_V);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_UPDATES, "keys: V is Updates");
    in = Key(IK_TAB);
    CHECK(Screens_Shortcut(&in) == KA_OPEN_SETTINGS, "keys: Tab is Settings");
    in = Key(IK_Q);
    CHECK(Screens_Shortcut(&in) == KA_NONE, "keys: other letters open nothing");

    /* The other screens. */
    Screens_Online(&l);
    CHECK(HasBackAndClose(&l) && ListIsSane(&l), "online: Back, the X, buttons sane");
    CHECK(Kit_Navigate(&l, WID_ONLINE_YES, KIT_RIGHT) == WID_ONLINE_NO, "online: Right from Yes comes round to No (Left/Right flip)");
    CHECK(Kit_Navigate(&l, WID_ONLINE_NO, KIT_LEFT) == WID_ONLINE_YES, "online: Left from No comes round to Yes");
    Screens_Trophies(&l, "Blockfall", 2, 12);
    const KitItem *st = Kit_Get(&l, WID_TROPHY_GAME);
    CHECK(HasBackAndClose(&l) && ListIsSane(&l), "trophies: Back, the X, buttons sane");
    CHECK(st && st->kind == KIT_STEPPER && st->value == 2 && st->max == 11 && (st->flags & KIT_WRAP), "trophies: the game is a stepper that wraps");
    CHECK(st && Kit_CellRect(st, 0).w >= 32 && Kit_CellRect(st, 1).h >= 32, "trophies: its arrows are easy to hit");
    CHECK(Kit_FirstFocus(&l) == WID_TROPHY_GAME && !(l.nav & KIT_NAV_X), "trophies: Left/Right change the game");
    Screens_Trophies(&l, "", 0, 0);
    CHECK(!Kit_Get(&l, WID_TROPHY_GAME), "trophies: no machines, no stepper");
    Screens_Updates(&l, false, false);
    const KitItem *chk = Kit_Get(&l, WID_CHECK_NOW);
    CHECK(HasBackAndClose(&l) && ListIsSane(&l), "updates: Back, the X, buttons sane");
    CHECK(chk && strcmp(chk->label, "Check now") == 0 && Kit_CanFocus(chk), "updates: Check now");
    Screens_Updates(&l, true, false);
    chk = Kit_Get(&l, WID_CHECK_NOW);
    CHECK(chk && strcmp(chk->label, "Checking...") == 0 && (chk->flags & KIT_DISABLED), "updates: Checking... while it waits");
    Screens_Updates(&l, false, true);
    chk = Kit_Get(&l, WID_CHECK_NOW);
    CHECK(chk && (chk->flags & KIT_DISABLED) && chk->reason[0], "updates: checks switched off: faint, with the reason");
    Screens_Plain(&l);
    CHECK(HasBackAndClose(&l) && l.count == 2, "stats and the game form: Back and the X");
}

void test_kit_all(void) {
    test_kit_text_width();
    test_kit_hit();
    test_kit_nav();
    test_kit_press_release();
    test_kit_values();
    test_kit_looks();
    test_kit_scroll();
    test_dialog_flow();
    test_nav_stack();
    test_screens();
    test_playing_as();
}
