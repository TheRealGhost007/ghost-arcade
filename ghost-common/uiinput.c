#include "uiinput.h"
#include <string.h>

/* A copy of Ghost Launcher's src/input.c (see uiinput.h for what changed). */

void Input_Init(InputState *st, int screenW, int screenH) {
    memset(st, 0, sizeof(*st));
    st->screenW = (float)screenW;
    st->screenH = (float)screenH;
    for (int i = 0; i < IK_COUNT; i++) {
        st->keyRep[i].next = INPUT_REPEAT_DELAY;
        st->keyRepeats[i] = Input_KeyRepeats((InputKey)i);
    }
    for (int i = 0; i < PB_COUNT; i++) st->padRep[i].next = INPUT_REPEAT_DELAY;
    st->lastDevice = st->hintDevice = DEV_KEYBOARD;
}

/* Delete repeats for text fields; a screen that removes something reads the
 * press alone, so holding Delete there still asks about one thing. */
bool Input_KeyRepeats(InputKey k) {
    return k == IK_LEFT || k == IK_RIGHT || k == IK_UP || k == IK_DOWN || k == IK_BACKSPACE || k == IK_DELETE;
}

/* X too: on a name board it deletes, and keeps deleting while held. */
bool Input_PadRepeats(PadButton b) {
    return b == PB_UP || b == PB_DOWN || b == PB_LEFT || b == PB_RIGHT || b == PB_X;
}

void Input_SetKeyRepeat(InputState *st, InputKey k, bool repeats) {
    if (k >= 0 && k < IK_COUNT) st->keyRepeats[k] = repeats;
}

static void Disarm(InputRepeat *r) {
    r->held = 0.0f;
    r->next = INPUT_REPEAT_DELAY;
    r->armed = false;
}

/* True on the press, then once per INPUT_REPEAT_INTERVAL after the control
 * has been held for INPUT_REPEAT_DELAY. A long frame (a hitch) gives one
 * step, never a burst. A hold that began before the window could see the
 * press (it was in the background) never repeats. */
static bool RepeatStep(InputRepeat *r, bool pressed, bool down, float dt) {
    if (pressed) {
        r->held = 0.0f;
        r->next = INPUT_REPEAT_DELAY;
        r->armed = true;
        return true;
    }
    if (!down || !r->armed) {
        Disarm(r);
        return false;
    }
    r->held += dt;
    if (r->held < r->next) return false;
    r->next += INPUT_REPEAT_INTERVAL;
    if (r->next <= r->held) r->next = r->held + INPUT_REPEAT_INTERVAL;
    return true;
}

static void StepKeys(InputState *st, const InputRaw *raw, float dt, Input *out) {
    for (int k = 0; k < IK_COUNT; k++) {
        bool pressed = raw->keyPressed[k];
        out->key[k] = pressed;
        out->keyDown[k] = raw->keyDown[k];
        out->keyRepeat[k] = st->keyRepeats[k] ? RepeatStep(&st->keyRep[k], pressed, raw->keyDown[k], dt) : pressed;
    }
    out->shift = raw->shift;
    out->ctrl = raw->ctrl;
    out->alt = raw->alt;
    bool regained = raw->focused && !st->wasFocused; /* StepPad keeps wasFocused, after this */
    bool rose = (raw->shift && !st->shiftPrev) || (raw->ctrl && !st->ctrlPrev) || (raw->alt && !st->altPrev);
    out->modPressed = rose && !regained;
    st->shiftPrev = raw->shift;
    st->ctrlPrev = raw->ctrl;
    st->altPrev = raw->alt;
}

/* A button already held when the window gets focus back (B still down from
 * another program) is not a new press: only a press made here counts. */
static void StepPad(InputState *st, const InputRaw *raw, float dt, Input *out) {
    bool regained = raw->focused && !st->wasFocused;
    for (int b = 0; b < PB_COUNT; b++) {
        bool down = raw->padDown[b];
        bool pressed = down && !st->padPrev[b] && !regained;
        out->pad[b] = pressed;
        out->padDown[b] = down;
        out->padRepeat[b] = Input_PadRepeats((PadButton)b) ? RepeatStep(&st->padRep[b], pressed, down, dt) : pressed;
        st->padPrev[b] = down;
    }
    st->wasFocused = raw->focused;
}

static void StepMouse(const InputState *st, const InputRaw *raw, Input *out) {
    out->mouseX = raw->mouseX;
    out->mouseY = raw->mouseY;
    out->mouseInside = raw->mouseX >= 0.0f && raw->mouseY >= 0.0f && raw->mouseX < st->screenW && raw->mouseY < st->screenH;
    out->mouseMoved = raw->mouseMoved;
    for (int b = 0; b < MB_COUNT; b++) {
        out->mousePressed[b] = raw->mousePressed[b];
        out->mouseReleased[b] = raw->mouseReleased[b];
        out->mouseDown[b] = raw->mouseDown[b];
    }
    out->wheel = raw->wheel;
}

static bool AnyOf(const bool *v, int n) {
    for (int i = 0; i < n; i++) if (v[i]) return true;
    return false;
}

/* Mouse first, so a key or button pressed while the hand is still on the
 * mouse wins: that is the device the player is using now. */
static void TrackDevice(InputState *st, Input *out) {
    bool mouse = out->mouseMoved || AnyOf(out->mousePressed, MB_COUNT) || out->wheel != 0.0f;
    bool keyboard = AnyOf(out->key, IK_COUNT) || out->charCount > 0;
    bool pad = AnyOf(out->pad, PB_COUNT);
    if (mouse) st->lastDevice = DEV_MOUSE;
    if (keyboard) st->lastDevice = st->hintDevice = DEV_KEYBOARD;
    if (pad) st->lastDevice = st->hintDevice = DEV_PAD;
    out->lastDevice = st->lastDevice;
    out->hintDevice = st->hintDevice;
    out->any = mouse || keyboard || pad;
}

void Input_DropHolds(InputState *st) {
    for (int i = 0; i < IK_COUNT; i++) Disarm(&st->keyRep[i]);
    for (int i = 0; i < PB_COUNT; i++) Disarm(&st->padRep[i]);
}

void Input_Step(InputState *st, const InputRaw *raw, float dt, Input *out) {
    memset(out, 0, sizeof(*out));
    out->focused = raw->focused;
    StepKeys(st, raw, dt, out);
    StepPad(st, raw, dt, out);
    StepMouse(st, raw, out);
    int n = raw->charCount < 0 ? 0 : (raw->charCount > INPUT_MAX_CHARS ? INPUT_MAX_CHARS : raw->charCount);
    memcpy(out->chars, raw->chars, (size_t)n * sizeof(int));
    out->charCount = n;
    TrackDevice(st, out);
}

void Input_MouseOnly(const Input *in, Input *out) {
    *out = *in;
    memset(out->key, 0, sizeof(out->key));
    memset(out->keyRepeat, 0, sizeof(out->keyRepeat));
    memset(out->keyDown, 0, sizeof(out->keyDown));
    memset(out->pad, 0, sizeof(out->pad));
    memset(out->padRepeat, 0, sizeof(out->padRepeat));
    memset(out->padDown, 0, sizeof(out->padDown));
    out->charCount = 0;
}

void Input_Quiet(const Input *in, Input *out) {
    *out = *in;
    memset(out->key, 0, sizeof(out->key));
    memset(out->keyRepeat, 0, sizeof(out->keyRepeat));
    memset(out->pad, 0, sizeof(out->pad));
    memset(out->padRepeat, 0, sizeof(out->padRepeat));
    memset(out->mousePressed, 0, sizeof(out->mousePressed));
    memset(out->mouseReleased, 0, sizeof(out->mouseReleased));
    out->wheel = 0.0f;
    out->mouseMoved = false;
    out->charCount = 0;
    out->any = false;
    out->modPressed = false;
}
