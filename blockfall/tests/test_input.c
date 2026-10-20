/* Headless tests for the shared input reader (ghost-common/uiinput.c), its
 * key table (uikeymap.c) and text field (uitextfield.c), ported from Ghost
 * Launcher's tests; then Blockfall's own: the keys its menus add (W A S D,
 * [ ], T), what the keys and the pad do in a run (actions.c), the comfort
 * switches and the Motion setting in config.ini. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "uiinput.h"
#include "uikeymap.h"
#include "uitextfield.h"
#include "actions.h"
#include "controls.h"
#include "comfort.h"
#include "menukeys.h"
#include "persist.h"
#include "prefs.h"

#define DT (1.0f / 60.0f)

/* --------------------------------------------------------- the reader */

static void RawKey(InputRaw *raw, InputKey k, bool pressed, bool down) {
    memset(raw, 0, sizeof(*raw));
    raw->focused = true;
    raw->keyPressed[k] = pressed;
    raw->keyDown[k] = down;
}

/* Holds k for `frames` frames after a press at 60 fps; returns the frame
 * of the first repeat (0 = the press itself) and counts every step. */
static int HoldKey(InputState *st, InputKey k, int frames, int *steps) {
    InputRaw raw;
    Input in;
    int first = -1;
    *steps = 0;
    for (int f = 0; f <= frames; f++) {
        RawKey(&raw, k, f == 0, true);
        Input_Step(st, &raw, DT, &in);
        if (in.keyRepeat[k]) {
            (*steps)++;
            if (f > 0 && first < 0) first = f;
        }
    }
    RawKey(&raw, k, false, false);
    Input_Step(st, &raw, DT, &in);
    return first;
}

static void test_input_repeat(void) {
    InputState st;
    Input_Init(&st, 620, 720);
    int steps = 0;
    int first = HoldKey(&st, IK_RIGHT, 60, &steps);
    CHECK(first >= 24 && first <= 25, "input: a held arrow first repeats after 0.4 s");
    CHECK(steps >= 7 && steps <= 8, "input: then about ten steps a second (press + 6-7 repeats in the first second)");
    first = HoldKey(&st, IK_RIGHT, 24 + 60, &steps);
    CHECK(steps >= 11 && steps <= 12, "input: a second of repeating is about ten steps");
    first = HoldKey(&st, IK_RIGHT, 20, &steps);
    CHECK(first < 0 && steps == 1, "input: let go before 0.4 s and it moved once");
    first = HoldKey(&st, IK_BACKSPACE, 40, &steps);
    CHECK(first >= 24 && first <= 25, "input: Backspace repeats");
    first = HoldKey(&st, IK_DELETE, 40, &steps);
    CHECK(first >= 24 && first <= 25, "input: Delete repeats (a text field deletes on while it is held)");
    first = HoldKey(&st, IK_W, 120, &steps);
    CHECK(first < 0 && steps == 1, "input: by default only the arrows, the d-pad, Backspace and Delete repeat");
    MenuKeys_Install(&st);
    first = HoldKey(&st, IK_W, 60, &steps);
    CHECK(first >= 24 && first <= 25 && steps >= 7, "input: Blockfall makes W A S D repeat, as the arrows do in its menus");
    first = HoldKey(&st, IK_T, 60, &steps);
    CHECK(first < 0 && steps == 1, "input: ...but not T (one period per press)");
    Keymap_SetGameBinds(NULL, 0);

    /* A press-only action reads the press alone: holding Delete deletes text
     * on and on, but a press edge is only the first. */
    InputRaw rawDel;
    Input inDel;
    int edges = 0, deletes = 0;
    for (int f = 0; f < 90; f++) {
        RawKey(&rawDel, IK_DELETE, f == 0, true);
        Input_Step(&st, &rawDel, DT, &inDel);
        if (Keymap_PressedEdge(KA_TEXT_DELETE, &inDel)) edges++;
        if (Keymap_Pressed(KA_TEXT_DELETE, &inDel)) deletes++;
    }
    RawKey(&rawDel, IK_DELETE, false, false);
    Input_Step(&st, &rawDel, DT, &inDel);
    CHECK(edges == 1 && deletes > 5, "keymap: held Delete is one press edge but deletes on and on");

    /* A hitch (one very long frame) gives one step, not a burst. */
    InputRaw raw;
    Input in;
    RawKey(&raw, IK_LEFT, true, true);
    Input_Step(&st, &raw, DT, &in);
    RawKey(&raw, IK_LEFT, false, true);
    Input_Step(&st, &raw, 2.0f, &in);
    bool hitch = in.keyRepeat[IK_LEFT];
    Input_Step(&st, &raw, DT, &in);
    CHECK(hitch && !in.keyRepeat[IK_LEFT], "input: a two-second frame repeats once, then waits");
    RawKey(&raw, IK_LEFT, false, false);
    Input_Step(&st, &raw, DT, &in);

    /* A raylib tap shorter than a frame (pressed but no longer down) counts. */
    RawKey(&raw, IK_UP, true, false);
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.key[IK_UP] && in.keyRepeat[IK_UP], "input: a tap within one frame still counts");

    /* The screen changes while Right is held: the hold stays with the old
     * screen until the key is pressed again. */
    RawKey(&raw, IK_RIGHT, true, true);
    Input_Step(&st, &raw, DT, &in);
    for (int f = 0; f < 30; f++) { RawKey(&raw, IK_RIGHT, false, true); Input_Step(&st, &raw, DT, &in); }
    bool wasRepeating = in.keyRepeat[IK_RIGHT] || st.keyRep[IK_RIGHT].armed;
    Input_DropHolds(&st);
    int leaked = 0;
    for (int f = 0; f < 90; f++) { RawKey(&raw, IK_RIGHT, false, true); Input_Step(&st, &raw, DT, &in); if (in.keyRepeat[IK_RIGHT]) leaked++; }
    CHECK(wasRepeating && leaked == 0, "input: after a screen change a held arrow does not repeat on");
    RawKey(&raw, IK_RIGHT, false, false);
    Input_Step(&st, &raw, DT, &in);
    RawKey(&raw, IK_RIGHT, true, true);
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.keyRepeat[IK_RIGHT], "input: ...and the next press works at once");
    RawKey(&raw, IK_RIGHT, false, false);
    Input_Step(&st, &raw, DT, &in);
}

static void PadFrame(InputState *st, Input *in, PadButton b, bool down, bool focused, float dt) {
    InputRaw raw;
    memset(&raw, 0, sizeof(raw));
    raw.focused = focused;
    if (b < PB_COUNT) raw.padDown[b] = down && focused;
    Input_Step(st, &raw, dt, in);
}

static void test_input_pad(void) {
    InputState st;
    Input_Init(&st, 620, 720);
    Input in;
    PadFrame(&st, &in, PB_COUNT, false, true, DT); /* the window has focus */

    int confirms = 0;
    for (int f = 0; f < 30; f++) {
        PadFrame(&st, &in, PB_A, true, true, DT);
        if (Keymap_Pressed(KA_UI_PRESS, &in)) confirms++;
        CHECK(!in.key[IK_ENTER] && !in.key[IK_SPACE], "pad: A is not the Enter or Space key");
    }
    PadFrame(&st, &in, PB_A, false, true, DT);
    CHECK(confirms == 1, "pad: holding A confirms exactly once");

    PadFrame(&st, &in, PB_B, true, true, DT);
    CHECK(Keymap_Pressed(KA_BACK, &in) && !Keymap_Pressed(KA_UI_PRESS, &in), "pad: B is back");
    PadFrame(&st, &in, PB_B, false, true, DT);

    int steps = 0, first = -1;
    for (int f = 0; f <= 60; f++) {
        PadFrame(&st, &in, PB_RIGHT, true, true, DT);
        if (in.padRepeat[PB_RIGHT]) { steps++; if (f > 0 && first < 0) first = f; }
    }
    PadFrame(&st, &in, PB_RIGHT, false, true, DT);
    CHECK(first >= 24 && first <= 25 && steps >= 7 && steps <= 8, "pad: the d-pad repeats like the arrows");
    PadFrame(&st, &in, PB_LB, true, true, DT);
    bool lb = in.pad[PB_LB];
    steps = 0;
    for (int f = 0; f < 60; f++) { PadFrame(&st, &in, PB_LB, true, true, DT); if (in.padRepeat[PB_LB]) steps++; }
    PadFrame(&st, &in, PB_LB, false, true, DT);
    CHECK(lb && steps == 0, "pad: LB is read, and shoulder buttons never repeat");

    PadFrame(&st, &in, PB_B, true, false, DT);
    CHECK(!in.pad[PB_B], "pad: ignored while the window is in the background");
    PadFrame(&st, &in, PB_B, true, true, DT);
    bool fired = in.pad[PB_B];
    PadFrame(&st, &in, PB_B, true, true, DT);
    fired = fired || in.pad[PB_B];
    CHECK(!fired, "pad: a button held while focus comes back does not fire");
    PadFrame(&st, &in, PB_B, false, true, DT);
    PadFrame(&st, &in, PB_B, true, true, DT);
    CHECK(in.pad[PB_B], "pad: the next real press does");
    PadFrame(&st, &in, PB_B, false, true, DT);

    PadFrame(&st, &in, PB_DOWN, true, true, DT);
    for (int f = 0; f < 10; f++) PadFrame(&st, &in, PB_DOWN, true, true, DT);
    Input_DropHolds(&st);
    steps = 0;
    for (int f = 0; f < 90; f++) { PadFrame(&st, &in, PB_DOWN, true, true, DT); if (in.padRepeat[PB_DOWN] || in.pad[PB_DOWN]) steps++; }
    CHECK(steps == 0, "pad: after a screen change a held d-pad neither fires nor repeats");
    PadFrame(&st, &in, PB_DOWN, false, true, DT);
}

static void test_input_keys_and_device(void) {
    InputState st;
    Input_Init(&st, 620, 720);
    InputRaw raw;
    Input in;

    RawKey(&raw, IK_ENTER, true, true);
    raw.alt = true;
    Input_Step(&st, &raw, DT, &in);
    CHECK(Keymap_Pressed(KA_FULLSCREEN, &in), "keys: Alt+Enter is the fullscreen key");
    CHECK(!Keymap_Pressed(KA_TEXT_SAVE, &in) && !Keymap_Pressed(KA_UI_PRESS, &in), "keys: Alt+Enter does nothing else (it never selects)");
    RawKey(&raw, IK_ENTER, true, true);
    Input_Step(&st, &raw, DT, &in);
    CHECK(Keymap_Pressed(KA_TEXT_SAVE, &in) && Keymap_Pressed(KA_UI_PRESS, &in) && !Keymap_Pressed(KA_FULLSCREEN, &in),
          "keys: plain Enter selects");
    RawKey(&raw, IK_F11, true, true);
    Input_Step(&st, &raw, DT, &in);
    CHECK(Keymap_Pressed(KA_FULLSCREEN, &in) && !Keymap_Pressed(KA_UI_PRESS, &in), "keys: F11 is fullscreen");
    RawKey(&raw, IK_SPACE, true, true);
    Input_Step(&st, &raw, DT, &in);
    CHECK(Keymap_Pressed(KA_UI_PRESS, &in) && !Keymap_Pressed(KA_TEXT_SAVE, &in), "keys: Space selects, but types a space in a text field");
    RawKey(&raw, IK_ESCAPE, true, true);
    Input_Step(&st, &raw, DT, &in);
    CHECK(Keymap_Pressed(KA_BACK, &in) && Keymap_Pressed(KA_TEXT_CANCEL, &in), "keys: Esc is back, and puts a name back");

    /* The device used last. */
    Input_Init(&st, 620, 720);
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && !in.any, "device: the keyboard until something is used");
    raw.mouseMoved = true;
    raw.mouseX = 100.0f;
    raw.mouseY = 50.0f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_MOUSE && in.any && in.mouseInside, "device: moving the mouse");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.padDown[PB_X] = true;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_PAD, "device: a pad button");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.padDown[PB_X] = true; /* still held: not a new use of the pad */
    raw.chars[0] = 'q';
    raw.charCount = 1;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && in.charCount == 1 && in.chars[0] == 'q', "device: typing is the keyboard");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.wheel = -1.0f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_MOUSE, "device: the wheel is the mouse");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.mouseMoved = true;
    raw.keyPressed[IK_L] = raw.keyDown[IK_L] = true;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD, "device: a key pressed while the mouse drifts is the keyboard");
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_KEYBOARD && !in.any, "device: remembered while nothing happens");

    /* Hints follow the keyboard or the pad, whichever was used last; the
     * mouse leaves them as they were. */
    CHECK(in.hintDevice == DEV_KEYBOARD, "hint device: the keyboard to start with");
    raw.padDown[PB_A] = true;
    Input_Step(&st, &raw, DT, &in);
    raw.padDown[PB_A] = false;
    raw.mouseMoved = true;
    raw.mouseX = 300.0f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.lastDevice == DEV_MOUSE && in.hintDevice == DEV_PAD, "hint device: a pad player who nudges the mouse keeps pad hints");
    raw.mouseMoved = false;
    raw.keyPressed[IK_D] = raw.keyDown[IK_D] = true;
    Input_Step(&st, &raw, DT, &in);
    raw.keyPressed[IK_D] = raw.keyDown[IK_D] = false;
    CHECK(in.hintDevice == DEV_KEYBOARD, "hint device: a key turns them back to keys");

    /* The pointer's screen is the game's (620 x 720 here, not the launcher's). */
    raw.mouseX = -1.0f;
    raw.mouseY = 10.0f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(!in.mouseInside, "mouse: the letterbox is outside the screen");
    raw.mouseX = 619.5f;
    raw.mouseY = 719.5f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.mouseInside, "mouse: the bottom-right pixel is inside");
    raw.mouseX = 620.0f;
    Input_Step(&st, &raw, DT, &in);
    CHECK(!in.mouseInside, "mouse: x = 620 is past the edge");
    raw.charCount = 999;
    Input_Step(&st, &raw, DT, &in);
    CHECK(in.charCount == INPUT_MAX_CHARS, "input: a runaway character count is capped");

    /* What a screen sees on the frame it opens: holds, no presses. */
    memset(&raw, 0, sizeof(raw));
    raw.focused = true;
    raw.keyPressed[IK_SPACE] = raw.keyDown[IK_SPACE] = true;
    raw.padDown[PB_LEFT] = true;
    raw.mousePressed[MB_LEFT] = raw.mouseReleased[MB_LEFT] = true;
    raw.wheel = 2.0f;
    Input_Step(&st, &raw, DT, &in);
    Input quiet;
    Input_Quiet(&in, &quiet);
    CHECK(!quiet.key[IK_SPACE] && quiet.keyDown[IK_SPACE] && quiet.padDown[PB_LEFT] && !quiet.pad[PB_LEFT],
          "quiet: what is held stays held, its press is gone");
    CHECK(!quiet.mousePressed[MB_LEFT] && !quiet.mouseReleased[MB_LEFT] && quiet.wheel == 0.0f && !quiet.any &&
              !Keymap_Pressed(KA_UI_PRESS, &quiet),
          "quiet: no click, no wheel, nothing pressed");
}

/* --------------------------------------------------------- the key table */

static Input Blank(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.lastDevice = DEV_KEYBOARD;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input Press(InputKey k) {
    Input in = Blank();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input PadPress(PadButton b) {
    Input in = Blank();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = DEV_PAD;
    return in;
}

/* An Input in which exactly binding b fires. */
static Input Fire(const KeyBind *b) {
    Input in = Blank();
    switch (b->kind) {
        case KB_KEY:
            in.key[b->code] = in.keyRepeat[b->code] = in.keyDown[b->code] = true;
            in.shift = (b->mods & KM_SHIFT) != 0;
            in.ctrl = (b->mods & KM_CTRL) != 0;
            in.alt = (b->mods & KM_ALT) != 0;
            break;
        case KB_PAD: in.pad[b->code] = in.padRepeat[b->code] = in.padDown[b->code] = true; break;
        case KB_MOUSE: in.mousePressed[b->code] = in.mouseDown[b->code] = true; break;
    }
    return in;
}

static void test_keymap(void) {
    MenuKeys_Install(NULL);
    bool everyActionBound = true, everyBindingFires = true;
    for (int a = 0; a < KA_KIT_COUNT; a++) {
        bool bound = false;
        for (int i = 0; i < Keymap_Count(); i++) if (Keymap_Get(i)->action == a) bound = true;
        if (!bound) everyActionBound = false;
    }
    for (int i = 0; i < Keymap_Count(); i++) {
        Input in = Fire(Keymap_Get(i));
        if (!Keymap_Pressed(Keymap_Get(i)->action, &in) || Keymap_Fired(Keymap_Get(i)->action, &in) < 0) everyBindingFires = false;
    }
    CHECK(everyActionBound, "keymap: every kit action has a key or button");
    CHECK(everyBindingFires, "keymap: every binding fires its action, Blockfall's own included");
    CHECK(Keymap_Count() > Keymap_BuiltinCount() && Keymap_Get(Keymap_Count()) == NULL, "keymap: the game's table comes after the kit's");

    Input tab = Press(IK_TAB);
    CHECK(Keymap_Pressed(KA_UI_NEXT_TAB, &tab) && !Keymap_Pressed(KA_UI_PREV_TAB, &tab), "keymap: Tab is the next tab");
    tab.shift = true;
    CHECK(Keymap_Pressed(KA_UI_PREV_TAB, &tab) && !Keymap_Pressed(KA_UI_NEXT_TAB, &tab), "keymap: Shift+Tab is the tab before");
    Input lb = PadPress(PB_LB);
    CHECK(Keymap_Pressed(KA_UI_PREV_TAB, &lb) && Keymap_Pressed(KA_SCROLL_PAGE_UP, &lb) && Keymap_Pressed(BA_BOARD_PREV, &lb),
          "keymap: LB is the tab before, a page up, and Blockfall's board before");
    Input w = Press(IK_W), s = Press(IK_S), a = Press(IK_A), d = Press(IK_D);
    CHECK(Keymap_Pressed(KA_UI_UP, &w) && Keymap_Pressed(KA_UI_DOWN, &s) && Keymap_Pressed(KA_UI_LEFT, &a) && Keymap_Pressed(KA_UI_RIGHT, &d),
          "keymap: W A S D move through Blockfall's menus, as they always have");
    Input br = Press(IK_RIGHT_BRACKET), t = Press(IK_T), y = PadPress(PB_Y);
    CHECK(Keymap_Pressed(BA_BOARD_NEXT, &br) && Keymap_Pressed(BA_PERIOD, &t) && Keymap_Pressed(BA_PERIOD, &y),
          "keymap: ] is the next board, T and pad Y the next period");

    Input enter = Press(IK_ENTER);
    int fired = Keymap_Fired(KA_UI_PRESS, &enter);
    Input held = Blank();
    held.keyDown[IK_ENTER] = true;
    CHECK(fired >= 0 && Keymap_Held(fired, &held) && !Keymap_Held(fired, &(Input){0}), "keymap: the key that pressed is held until let go");
    Input space = Press(IK_SPACE);
    int bySpace = Keymap_Fired(KA_UI_PRESS, &space);
    CHECK(bySpace >= 0 && bySpace != fired && !Keymap_Held(bySpace, &held), "keymap: Enter held does not hold a press Space began");
    Input dHeld = Blank();
    dHeld.keyDown[IK_D] = true;
    CHECK(Keymap_AnyHeld(KA_UI_RIGHT, &dHeld) && !Keymap_AnyHeld(KA_UI_LEFT, &dHeld), "keymap: D held is Right held");
    Input stick = Blank();
    stick.padDown[PB_LEFT] = true;
    CHECK(Keymap_AnyHeld(KA_UI_LEFT, &stick), "keymap: the d-pad (or stick) held is Left held");

    char name[48];
    Keymap_Names(KA_FULLSCREEN, KB_KEY, name, sizeof(name));
    CHECK(strcmp(name, "F11 / Alt+Enter") == 0, "keymap: fullscreen's keys, joined");
    Keymap_Names(KA_BACK, KB_MOUSE, name, sizeof(name));
    CHECK(strcmp(name, "Back button") == 0, "keymap: the mouse's back button");
    Keymap_Names(KA_UI_UP, KB_KEY, name, sizeof(name));
    CHECK(strcmp(name, "Up / W") == 0, "keymap: the game's keys join the kit's");
    Keymap_Hint(KA_UI_PRESS, DEV_KEYBOARD, name, sizeof(name));
    CHECK(strcmp(name, "Enter") == 0, "keymap: hint for keys");
    Keymap_Hint(KA_UI_PRESS, DEV_PAD, name, sizeof(name));
    CHECK(strcmp(name, "A") == 0, "keymap: hint after pad use");
    Keymap_Hint(KA_SCROLL_TOP, DEV_PAD, name, sizeof(name));
    CHECK(strcmp(name, "Home") == 0, "keymap: no pad button for it: the key");
    bool pad = true;
    Keymap_Cap(KA_TEXT_DELETE, DEV_KEYBOARD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "Del") == 0 && !pad, "keymap: a cap uses the short name (Del)");
    Keymap_Cap(KA_UI_DOWN, DEV_PAD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "Down") == 0 && pad, "keymap: the d-pad's cap is its direction");
    Keymap_Cap(BA_BOARD_PREV, DEV_KEYBOARD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "[") == 0 && !pad, "keymap: Blockfall's board key is [");
    Keymap_Cap(BA_BOARD_PREV, DEV_PAD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "LB") == 0 && pad, "keymap: ...and LB on the pad");
    char tiny[4];
    Keymap_Names(KA_UI_UP, KB_KEY, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny), "keymap: names never overrun");
    Keymap_SetGameBinds(NULL, 0);
    Input w2 = Press(IK_W);
    CHECK(!Keymap_Pressed(KA_UI_UP, &w2) && Keymap_Count() == Keymap_BuiltinCount(), "keymap: without a game's table, W is just W");
    MenuKeys_Install(NULL);
}

/* --------------------------------------------------------- text field */

static Input Chars(const char *s) {
    Input in = Blank();
    for (int i = 0; s[i] && i < INPUT_MAX_CHARS; i++) in.chars[in.charCount++] = (unsigned char)s[i];
    return in;
}

static TextFieldResult Type(TextField *f, const char *s) { Input in = Chars(s); return TextField_Update(f, &in, NULL); }
static TextFieldResult KeyTF(TextField *f, InputKey k) { Input in = Press(k); return TextField_Update(f, &in, NULL); }

static void test_textfield(void) {
    TextField f;
    TextField_Begin(&f, "TRG", 32);
    CHECK(f.editing && f.cursor == 3 && TextField_Length(&f) == 3 && !TextField_Changed(&f), "text: begins with the cursor at the end");
    CHECK(Type(&f, "abc") == TF_CHANGED && strcmp(f.text, "TRGabc") == 0 && f.cursor == 6, "text: letters go in at the cursor");
    KeyTF(&f, IK_LEFT);
    KeyTF(&f, IK_LEFT);
    Type(&f, "X");
    CHECK(strcmp(f.text, "TRGaXbc") == 0 && f.cursor == 5, "text: Left moves the cursor; typing goes in there");
    KeyTF(&f, IK_HOME);
    CHECK(f.cursor == 0 && KeyTF(&f, IK_LEFT) == TF_NONE && f.cursor == 0, "text: Home goes to the start, and Left stops there");
    CHECK(KeyTF(&f, IK_BACKSPACE) == TF_NONE && strcmp(f.text, "TRGaXbc") == 0, "text: Backspace at the start does nothing");
    KeyTF(&f, IK_DELETE);
    CHECK(strcmp(f.text, "RGaXbc") == 0 && f.cursor == 0, "text: Delete takes the character after the cursor");
    KeyTF(&f, IK_END);
    CHECK(f.cursor == 6 && KeyTF(&f, IK_DELETE) == TF_NONE && KeyTF(&f, IK_RIGHT) == TF_NONE, "text: End; Delete and Right stop there");
    KeyTF(&f, IK_LEFT);
    KeyTF(&f, IK_BACKSPACE);
    CHECK(strcmp(f.text, "RGaXc") == 0 && f.cursor == 4, "text: Backspace takes the character before the cursor");
    Input rep = Blank();
    rep.keyRepeat[IK_BACKSPACE] = rep.keyDown[IK_BACKSPACE] = true;
    TextField_Update(&f, &rep, NULL);
    TextField_Update(&f, &rep, NULL);
    CHECK(strcmp(f.text, "RGc") == 0, "text: Backspace repeats while held");

    TextField_Begin(&f, "", 32);
    Type(&f, "a|b");
    CHECK(strcmp(f.text, "ab") == 0, "text: '|' never goes in (it splits the arcade's text files)");
    Input odd = Blank();
    int weird[] = {7, 9, 127, 200, 233, 0x263A, 'z'};
    for (int i = 0; i < 7; i++) odd.chars[odd.charCount++] = weird[i];
    TextField_Update(&f, &odd, NULL);
    CHECK(strcmp(f.text, "abz") == 0, "text: only printable ASCII goes in");
    Input ctrlA = Chars("a");
    ctrlA.ctrl = true;
    TextField_Update(&f, &ctrlA, NULL);
    CHECK(strcmp(f.text, "abz") == 0, "text: a Ctrl chord types nothing");

    TextField_Begin(&f, "", 5);
    Type(&f, "abcdefg");
    CHECK(strcmp(f.text, "abcde") == 0 && f.cursor == 5, "text: new text stops at the limit");
    TextField_Begin(&f, "ab", 10);
    KeyTF(&f, IK_LEFT);
    Input paste = Press(IK_V);
    paste.ctrl = true;
    paste.chars[0] = 'v';
    paste.charCount = 1;
    CHECK(TextField_WantsPaste(&paste), "text: Ctrl+V asks for the clipboard");
    TextField_Update(&f, &paste, "Hi|there\nyou");
    CHECK(strcmp(f.text, "aHithereyb") == 0 && f.cursor == 9, "text: paste goes in at the cursor, cleaned, up to the limit");

    TextField_Begin(&f, "TRG", 32);
    Type(&f, "x");
    Input altEnter = Press(IK_ENTER);
    altEnter.alt = true;
    CHECK(TextField_Update(&f, &altEnter, NULL) != TF_SUBMIT && f.editing, "text: Alt+Enter is fullscreen, not save");
    CHECK(KeyTF(&f, IK_ENTER) == TF_SUBMIT && !f.editing && strcmp(f.text, "TRGx") == 0, "text: Enter keeps it");
    TextField_Begin(&f, "TRG", 32);
    Type(&f, "xyz");
    CHECK(KeyTF(&f, IK_ESCAPE) == TF_CANCEL && !f.editing && strcmp(f.text, "TRG") == 0, "text: Esc puts the old text back");
    TextField_Begin(&f, "TRG", 32);
    Type(&f, "q");
    Input b = PadPress(PB_B);
    CHECK(TextField_Update(&f, &b, NULL) == TF_CANCEL && strcmp(f.text, "TRG") == 0, "text: pad B puts it back too");

    TextField_Begin(&f, "abcdef", 32);
    f.view = 0;
    TextField_ClickAt(&f, 100.0f + 18 * 2 + 5, 100.0f, 18);
    CHECK(f.cursor == 2, "text: a click on a character's left half: before it");
    TextField_ClickAt(&f, 900.0f, 100.0f, 18);
    CHECK(f.cursor == 6, "text: past the end: the end");
    char longName[41];
    memset(longName, 'n', 40);
    longName[40] = '\0';
    TextField_Begin(&f, longName, 64);
    TextField_KeepCursorVisible(&f, 13);
    CHECK(f.view == 28, "text: a long text scrolls so the cursor at its end shows");
}

/* --------------------------------------------- what keys do in a run */

static bool sDown[512], sPressed[512];
static bool KDown(int code) { return code > 0 && code < 512 && sDown[code]; }
static bool KPressed(int code) { return code > 0 && code < 512 && sPressed[code]; }
static const KeyReader kKb = {KDown, KPressed};
static void NoKeys(void) { memset(sDown, 0, sizeof(sDown)); memset(sPressed, 0, sizeof(sPressed)); }
static void Tap(int code) { sDown[code] = sPressed[code] = true; }

/* config.ini's names, as raylib's codes for the few keys these tests use. */
static int Code(const char *name) {
    static const struct { const char *n; int c; } k[] = {
        {"LEFT", 263}, {"RIGHT", 262}, {"UP", 265}, {"DOWN", 264}, {"SPACE", 32}, {"ESCAPE", 256}, {"A", 65}, {"D", 68},
        {"S", 83}, {"W", 87}, {"C", 67}, {"P", 80}, {"R", 82}, {"J", 74}, {"X", 88}, {"Z", 90},
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (strcmp(name, k[i].n) == 0) return k[i].c;
    return 0;
}

static void test_actions(void) {
    Settings s;
    Settings_Default(&s);
    PlayKeys k;
    Actions_Keys(&s, Code, &k);
    CHECK(k.key[ACT_LEFT][0] == 263 && k.key[ACT_LEFT][1] == 65 && k.key[ACT_HARD][0] == 32 && k.key[ACT_PAUSE][1] == 256 && !k.escTaken,
          "actions: config.ini's names as key codes");
    CHECK(k.key[ACT_HARD][1] == 0 && k.key[ACT_ROTATE_LEFT][0] == 90, "actions: a second key of none is no key; Rotate left is Z");
    Input pad = Blank();
    PlayInput pi;

    NoKeys();
    sDown[263] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.left && !pi.right, "actions: Left held moves left");
    sDown[262] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(!pi.left && !pi.right, "actions: Left and Right held together cancel");
    NoKeys();
    sDown[65] = true;
    pad.padDown[PB_RIGHT] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(!pi.left && !pi.right, "actions: ...the second key and the pad included");
    pad = Blank();

    NoKeys();
    Tap(32);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hardDrop && !pi.rotate && !pi.back, "actions: Space hard-drops");
    Actions_Read(&k, &kKb, &pad, true, &pi);
    CHECK(!pi.hardDrop, "actions: on the frame a run opens, no press acts (Space that started it never drops)");
    NoKeys();
    sDown[264] = true;
    Actions_Read(&k, &kKb, &pad, true, &pi);
    CHECK(pi.softDrop, "actions: ...but what is held still counts there");
    NoKeys();
    Tap(90);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.rotateLeft && !pi.rotate, "actions: Z turns left");

    NoKeys();
    Tap(256);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.back, "actions: Esc pauses (and leaves from pause)");
    Controls_BindKey(&s, ACT_PAUSE, 1, "J");
    Actions_Keys(&s, Code, &k);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.back && k.key[ACT_PAUSE][1] == 74, "actions: Esc still works when Pause's second key is another");
    Settings_Default(&s);
    snprintf(s.quit, KEYNAME_LEN, "SPACE");
    snprintf(s.hardDrop, KEYNAME_LEN, "ESCAPE"); /* a 2.1.0 config.ini: Back to Space gave Hard drop Esc */
    Actions_Keys(&s, Code, &k);
    CHECK(k.escTaken, "actions: Esc on Hard drop (an older config.ini) is seen");
    NoKeys();
    Tap(256);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hardDrop && !pi.back, "actions: then Esc hard-drops, as Settings says, and does not pause");
    NoKeys();
    Tap(32);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.back && !pi.hardDrop, "actions: ...and Back's own key leaves");
    Settings_Default(&s);
    Actions_Keys(&s, Code, &k);

    /* The pad: its own buttons, whatever the keys are. */
    Controls_BindKey(&s, ACT_HARD, 0, "X");
    Actions_Keys(&s, Code, &k);
    NoKeys();
    pad = PadPress(PB_A);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hardDrop, "actions: pad A hard-drops, even with Hard drop rebound to X");
    pad = PadPress(PB_X);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(!pi.hardDrop && pi.rotateLeft, "actions: ...and pad X is not the X key: it turns left");
    pad = PadPress(PB_UP);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.rotate, "actions: d-pad Up rotates");
    pad = PadPress(PB_Y);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hold, "actions: Y holds");
    pad = PadPress(PB_LB);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hold, "actions: so does LB");
    pad = PadPress(PB_MENU);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.pause, "actions: Menu pauses");
    pad = PadPress(PB_B);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.back && !pi.pause, "actions: B is Back (the pause menu)");
    pad = PadPress(PB_VIEW);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.restartPad && !pi.restartKey, "actions: View is the pad's restart");
    pad = Blank();
    pad.padDown[PB_DOWN] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.softDrop, "actions: the d-pad held down soft-drops");

    /* Pad buttons rebind too: Hold to A swaps with Hard drop. */
    Settings_Default(&s);
    BindResult r = Controls_BindPad(&s, ACT_HOLD, 0, PB_A);
    Actions_Keys(&s, Code, &k);
    pad = PadPress(PB_A);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(r.kind == BIND_SWAPPED && pi.hold && !pi.hardDrop, "actions: pad A holds once Hold has it");
    pad = PadPress(PB_Y);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.hardDrop && !pi.hold, "actions: and Y hard-drops (the swap)");
    /* Pause's Menu to Y: Hold keeps LB and never gets Menu; Menu still pauses. */
    Settings_Default(&s);
    r = Controls_BindPad(&s, ACT_PAUSE, 0, PB_Y);
    Actions_Keys(&s, Code, &k);
    CHECK(r.kind == BIND_SWAPPED && r.withheld && Controls_Pad(&s, ACT_HOLD, 0) == PB_LB && Controls_Pad(&s, ACT_HOLD, 1) == PAD_NONE &&
              !Controls_MenuTaken(&s),
          "actions: Pause's Menu to Y: Hold keeps LB and is never given Menu");
    pad = PadPress(PB_MENU);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.pause && !pi.hold, "actions: Menu still pauses");
    pad = PadPress(PB_Y);
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.pause && !pi.hold, "actions: and so does Y now");

    NoKeys();
    Tap(77);
    Tap(45);
    Tap(334);
    Tap(292);
    pad = Blank();
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.mute && pi.volumeDown && pi.volumeUp && pi.fps, "actions: M, -, the keypad's + and F3 still work in a run");
    NoKeys();
}

/* --------------------------------------------------- comfort switches */

static void test_comfort(void) {
    Comfort_SetReducedMotion(false);
    CHECK(Comfort_Motion() && Comfort_Particles(12) == 12, "comfort: Full motion moves, every particle");
    Comfort_SetReducedMotion(true);
    CHECK(!Comfort_Motion() && Comfort_Particles(12) == 6 && Comfort_Particles(1) == 1 && Comfort_Particles(0) == 0,
          "comfort: Reduced motion cuts, and halves the particles (never to none)");
    Comfort_SetReducedMotion(false);
    bool was = Prefs_Get()->reducedFlashing;
    Prefs_Get()->reducedFlashing = true;
    CHECK(Comfort_Calm(), "comfort: Reduced flashing is the arcade-wide setting");
    Prefs_Get()->reducedFlashing = was;

    /* Motion lives in config.ini as reduced_motion: missing means Full. */
    Settings s;
    Settings_Default(&s);
    CHECK(!s.reducedMotion, "motion: Full by default");
    s.reducedMotion = true;
    CHECK(Settings_Save(&s), "motion: saved");
    Settings back;
    Settings_Load(&back);
    CHECK(back.reducedMotion, "motion: Reduced read back");
    char path[600];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    snprintf(path, sizeof(path), "%s/blockfall/config.ini", xdg ? xdg : ".");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("left=J\nvolume=40\nsetup_mode=2\n", f); /* a config.ini from before 2.2.0 */
        fclose(f);
    }
    Settings_Load(&back);
    CHECK(!back.reducedMotion && strcmp(back.left, "J") == 0 && back.volumePercent == 40 && back.setupMode == 2,
          "motion: an older config.ini loads unchanged, Motion Full");
    CHECK(!back.hideControls, "controls card: an older config.ini shows it");
    back.hideControls = true;
    CHECK(Settings_Save(&back), "controls card: saved");
    Settings again;
    Settings_Load(&again);
    CHECK(again.hideControls && strcmp(again.left, "J") == 0 && again.volumePercent == 40, "controls card: Hidden read back, the rest unchanged");
    f = fopen(path, "r");
    char text[1024] = "";
    if (f) {
        size_t got = fread(text, 1, sizeof(text) - 1, f);
        text[got] = '\0';
        fclose(f);
    }
    CHECK(strstr(text, "hide_controls=1\n") && strstr(text, "reduced_motion=0\n") && strstr(text, "left=J\n"),
          "controls card: config.ini keeps every old line and gains hide_controls (an older build ignores it)");

    /* 2.4.0: Piece motion (piece_snap) and READY-GO (ready_go). A config.ini
     * from before has neither: Smooth, READY-GO on. */
    Settings_Default(&s);
    CHECK(!s.pieceSnap && s.readyGo, "feel settings: Smooth and READY-GO by default");
    f = fopen(path, "w");
    if (f) {
        fputs("left=J\nvolume=40\nsetup_mode=2\nreduced_motion=1\nhide_controls=1\n", f); /* a 2.3.0 config.ini */
        fclose(f);
    }
    Settings_Load(&back);
    CHECK(!back.pieceSnap && back.readyGo && back.reducedMotion && back.hideControls && strcmp(back.left, "J") == 0 && back.volumePercent == 40,
          "feel settings: a 2.3.0 config.ini loads unchanged, Smooth with READY-GO");
    back.pieceSnap = true;
    back.readyGo = false;
    CHECK(Settings_Save(&back), "feel settings: saved");
    Settings_Load(&again);
    CHECK(again.pieceSnap && !again.readyGo && again.reducedMotion && again.hideControls && strcmp(again.left, "J") == 0,
          "feel settings: Snap and READY-GO off read back, the rest unchanged");
    f = fopen(path, "r");
    text[0] = '\0';
    if (f) {
        size_t got = fread(text, 1, sizeof(text) - 1, f);
        text[got] = '\0';
        fclose(f);
    }
    CHECK(strstr(text, "piece_snap=1\n") && strstr(text, "ready_go=0\n") && strstr(text, "hide_controls=1\n") && strstr(text, "left=J\n"),
          "feel settings: config.ini keeps every old line and gains piece_snap and ready_go (an older build ignores them)");
    f = fopen(path, "w");
    if (f) {
        fputs("ready_go=1\npiece_snap=0\n", f);
        fclose(f);
    }
    Settings_Load(&again);
    CHECK(again.readyGo && !again.pieceSnap, "feel settings: On and Smooth read back from their lines");
    Comfort_SetReducedMotion(false);
    Comfort_SetPieceSnap(false);
    CHECK(Comfort_PieceGlide(), "comfort: Smooth with Motion Full glides");
    Comfort_SetPieceSnap(true);
    CHECK(!Comfort_PieceGlide(), "comfort: Snap does not");
    Comfort_SetPieceSnap(false);
    Comfort_SetReducedMotion(true);
    CHECK(!Comfort_PieceGlide(), "comfort: nor does Reduced motion");
    Comfort_SetReducedMotion(false);
    Settings_Default(&s);
    Settings_Save(&s);
}

void test_input_all(void) {
    test_input_repeat();
    test_input_pad();
    test_input_keys_and_device();
    test_keymap();
    test_textfield();
    test_actions();
    test_comfort();
}
