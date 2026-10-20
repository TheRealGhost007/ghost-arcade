/* Headless tests for the shared button kit (ghost-common/uikit.c) and back
 * stack (uinav.c), ported from Ghost Launcher's tests/test_kit.c with the
 * launcher's own screens left out: the screen is Blockfall's 620x720. Inputs
 * are built by hand, one frame at a time, the way uiinput.c hands them over.
 * Then what the kit added for the games: the press flash, the focus plate's
 * glide, a stepper that is a whole row, the chosen row, and hint chips. */
#include <string.h>
#include "check.h"
#include "uikit.h"
#include "uikeymap.h"
#include "uinav.h"

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
    in.mouseInside = x >= 0 && y >= 0 && x < Kit_ScreenW() && y < Kit_ScreenH();
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
    Kit_Add(&l, 1, KIT_AREA, (KitRect){0, 0, (float)Kit_ScreenW(), (float)Kit_ScreenH()}, "");
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
    Kit_DialogLayout(&d, &l);
    const KitItem *safe = Kit_Get(&l, KIT_ID_DLG_SAFE), *act = Kit_Get(&l, KIT_ID_DLG_ACT);
    KitRect box = Kit_DialogRect();
    CHECK(box.x >= 24 && box.x + box.w <= Kit_ScreenW() - 24 && box.y > 0 && box.y + box.h < Kit_ScreenH(),
          "dialog: the box fits the game's screen, centred");
    CHECK(safe && act && safe->r.x + safe->r.w <= act->r.x, "dialog: the safe answer is on the left");
    CHECK(safe && act && Kit_Inside(box, safe->r.x, safe->r.y) && Kit_Inside(box, act->r.x + act->r.w - 1, act->r.y + act->r.h - 1),
          "dialog: both answers are inside the box");
    CHECK(safe && strcmp(safe->label, "Keep") == 0 && act && strcmp(act->label, "Remove") == 0, "dialog: the answers' labels");

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
    ev = Click(&s, &l, box.x + 20, box.y + 20);
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
    Input down = MousePress(act->r.x + 10, act->r.y + 10), up = MouseRelease(box.x + 20, box.y + 20);
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
    CHECK(Kit_DialogDevice(&s, &l, DEV_PAD) == DEV_PAD, "dialog: the pad keeps its own looks");
}

static void test_nav_stack(void) {
    Nav n;
    Nav_Init(&n, 0);
    CHECK(Nav_Top(&n) == 0 && Nav_Depth(&n) == 1, "nav stack: starts at the root");
    CHECK(!Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: Back at the root does nothing");
    /* The root (0), a menu (8), a screen under it (3). */
    Nav_Push(&n, 8);
    Nav_Push(&n, 3);
    CHECK(Nav_Top(&n) == 3 && Nav_Under(&n) == 8 && Nav_Depth(&n) == 3, "nav stack: Stats from the menu");
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 8, "nav stack: Back goes to the screen it came from");
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: ...and Back again to the root");
    Nav_Push(&n, 3);
    CHECK(Nav_Back(&n) && Nav_Top(&n) == 0, "nav stack: opened from the root, it goes back to the root");
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


/* ------------------------------------------------- what the kit added */

static void test_kit_flash(void) {
    KitList l;
    KitState s;
    Column(&l, KIT_NAV_Y);
    Kit_Enter(&s, 2);
    Input in = Key(IK_ENTER);
    Kit_Update(&s, &l, &in);
    in = None();
    KitEvent ev = Kit_Update(&s, &l, &in);
    CHECK(IsEvent(ev, KIT_EV_PRESS, 2), "flash: Enter let go acts");
    CHECK(Kit_Look(&s, &l.items[1], DEV_KEYBOARD) == KIT_LOOK_PRESSED, "flash: on the frame it acts, it is lit");
    Kit_Update(&s, &l, &in);
    CHECK(Kit_Look(&s, &l.items[1], DEV_KEYBOARD) == KIT_LOOK_PRESSED, "flash: and the next frame (two in all)");
    Kit_Update(&s, &l, &in);
    CHECK(Kit_Look(&s, &l.items[1], DEV_KEYBOARD) == KIT_LOOK_FOCUSED, "flash: then it is only focused again");
    CHECK(KIT_FLASH_FRAMES == 2, "flash: two frames, as the plan has it");

    ev = Click(&s, &l, 100, 100 + 3 * 48 + 10);
    CHECK(IsEvent(ev, KIT_EV_PRESS, 4) && Kit_Look(&s, &l.items[3], DEV_MOUSE) == KIT_LOOK_PRESSED, "flash: a click flashes too");
    Kit_Enter(&s, 1);
    CHECK(Kit_Look(&s, &l.items[3], DEV_MOUSE) != KIT_LOOK_PRESSED, "flash: a screen that opens again has none left over");

    Kit_Flash(&s, 3, 0);
    CHECK(Kit_Look(&s, &l.items[2], DEV_KEYBOARD) == KIT_LOOK_PRESSED && Kit_PressedCell(&s, &l.items[2]) == 0,
          "flash: a screen can flash a control it acted on itself");

    Kit_Clear(&l, KIT_NAV_Y);
    KitItem *st = Kit_Add(&l, 1, KIT_STEPPER, (KitRect){100, 100, 200, 32}, "5");
    st->value = 5;
    st->max = 9;
    Kit_Enter(&s, 1);
    in = Key(IK_LEFT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 4 && Kit_PressedCell(&s, st) == 0, "flash: Left on a stepper lights its left arrow");
    st->value = 9;
    in = Key(IK_RIGHT);
    Kit_Update(&s, &l, &in);
    in = None();
    Kit_Update(&s, &l, &in);
    Kit_Update(&s, &l, &in);
    in = Key(IK_RIGHT);
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_NONE && Kit_PressedCell(&s, st) == KIT_NONE, "flash: a stepper at its end does not flash (nothing changed)");
}

static bool Near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }

static void test_kit_glide(void) {
    KitGlide g = {0};
    KitRect a = {100, 100, 300, 40}, b = {100, 200, 300, 40}, c = {100, 300, 300, 52};
    KitRect at = Kit_GlideTo(&g, a, 1.0f / 60.0f, true);
    CHECK(Near(at.y, 100) && Near(at.h, 40), "glide: the first placement is straight there");
    at = Kit_GlideTo(&g, b, 1.0f / 60.0f, true);
    CHECK(at.y > 100.0f && at.y < 200.0f, "glide: a new place: one frame on, it is on its way");
    float firstStep = at.y - 100.0f;
    at = Kit_GlideTo(&g, b, 1.0f / 60.0f, true);
    CHECK(at.y - 100.0f > firstStep && at.y - 100.0f - firstStep < firstStep, "glide: it eases in (slower each frame)");
    for (int i = 0; i < 3; i++) at = Kit_GlideTo(&g, b, 1.0f / 60.0f, true);
    CHECK(Near(at.y, 200), "glide: there within 0.08 s (five frames)");
    at = Kit_GlideTo(&g, c, 1.0f / 60.0f, true);
    float mid = at.y;
    at = Kit_GlideTo(&g, a, 1.0f / 60.0f, true);
    CHECK(at.y < mid && at.y > 100.0f, "glide: turned round half-way, it goes back from where it is (no jump)");
    for (int i = 0; i < 10; i++) at = Kit_GlideTo(&g, a, 1.0f / 60.0f, true);
    at = Kit_GlideTo(&g, c, 1.0f / 60.0f, false);
    CHECK(Near(at.y, 300) && Near(at.h, 52), "glide: Reduced motion jumps straight there, height and all");
    Kit_GlideReset(&g);
    at = Kit_GlideTo(&g, a, 1.0f / 60.0f, true);
    CHECK(Near(at.y, 100), "glide: after a reset (a screen opening) it jumps");
    CHECK(KIT_GLIDE_SECONDS > 0.07f && KIT_GLIDE_SECONDS < 0.09f, "glide: 0.08 s, as the plan has it");
}

/* A stepper that is a whole row ("Width  < 10 >"): the row is the control,
 * so Up and Down land on it, and only its arrows take clicks. */
static void test_kit_row_stepper(void) {
    KitList l;
    KitState s;
    Kit_Clear(&l, KIT_NAV_Y);
    Kit_Add(&l, 1, KIT_ROW, (KitRect){24, 296, 286, 34}, "Power");
    KitItem *w = Kit_Add(&l, 2, KIT_STEPPER, (KitRect){24, 366, 286, 34}, "10");
    w->value = 10;
    w->min = 6;
    w->max = 24;
    w->arrowX = 132;
    snprintf(w->caption, sizeof(w->caption), "Width");
    Kit_Add(&l, 3, KIT_BUTTON, (KitRect){44, 452, 246, 28}, "Scores");
    CHECK(Kit_Navigate(&l, 1, KIT_DOWN) == 2, "row stepper: Down from the row above lands on it, not past it");
    KitRect left = Kit_CellRect(w, 0), right = Kit_CellRect(w, 1);
    CHECK(Near(left.x, 24 + 132) && Near(right.x + right.w, 24 + 286), "row stepper: its arrows start at arrowX and end at its edge");
    Kit_Enter(&s, 1);
    CHECK(Click(&s, &l, 60, 380).type == KIT_EV_NONE, "row stepper: a click on its caption steps nothing");
    KitEvent ev = Click(&s, &l, left.x + 4, 380);
    CHECK(IsEvent(ev, KIT_EV_CHANGE, 2) && ev.value == 9, "row stepper: a click on its left arrow takes one off");
    Input in = Pointer(60, 380, false);
    in.wheel = -1.0f;
    ev = Kit_Update(&s, &l, &in);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == 9, "row stepper: the wheel anywhere over the row steps it");
}

static void test_kit_chosen_and_hints(void) {
    KitList l;
    Kit_Clear(&l, KIT_NAV_Y);
    KitItem *it = Kit_Add(&l, 1, KIT_ROW, (KitRect){24, 100, 286, 34}, "Sprint");
    it->flags |= KIT_CHOSEN;
    CHECK(Kit_CanFocus(it), "chosen: a chosen row is still an ordinary control");

    KitHint updown = {2, {KA_UI_UP, KA_UI_DOWN}, "choose"};
    int keys = Kit_HintWidth(&updown, DEV_KEYBOARD), pad = Kit_HintWidth(&updown, DEV_PAD);
    CHECK(keys == Kit_CapWidth("Up", false) + 2 + Kit_CapWidth("Down", false) + 6 + Kit_TextWidth("choose", 8),
          "hints: key caps, a 2 px gap, then the word");
    CHECK(pad == 2 * KIT_CAP_H + 2 + 6 + Kit_TextWidth("choose", 8), "hints: after pad use, the d-pad's arrows are two discs");
    KitHint a = {1, {KA_UI_PRESS}, "select"};
    CHECK(Kit_HintWidth(&a, DEV_PAD) == KIT_CAP_H + 6 + Kit_TextWidth("select", 8), "hints: pad A is one disc");
    KitHint two[2] = {updown, a};
    CHECK(Kit_HintsWidth(two, 2, DEV_PAD) == pad + KIT_HINT_GAP + Kit_HintWidth(&a, DEV_PAD), "hints: a line of them, a gap between");
    char cap[16];
    bool isPad = false;
    Kit_HintCap(KA_UI_PRESS, DEV_MOUSE, cap, sizeof(cap), &isPad);
    CHECK(strcmp(cap, "Enter") == 0 && !isPad, "hints: the mouse's hints are the keyboard's");
    CHECK(Kit_CapWidth("Up", true) == KIT_CAP_H && Kit_CapWidth("LB", true) > KIT_CAP_H && Kit_CapWidth("View", true) == 20,
          "caps: the d-pad's arrow is a disc, LB a pill, View its icon");
}

void test_kit_all(void) {
    Kit_SetScreen(620, 720);
    CHECK(Kit_ScreenW() == 620 && Kit_ScreenH() == 720, "kit: the game's own screen size");
    test_kit_text_width();
    test_kit_hit();
    test_kit_nav();
    test_kit_press_release();
    test_kit_values();
    test_kit_looks();
    test_kit_scroll();
    test_dialog_flow();
    test_nav_stack();
    test_kit_flash();
    test_kit_glide();
    test_kit_row_stepper();
    test_kit_chosen_and_hints();
}
