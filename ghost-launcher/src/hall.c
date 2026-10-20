#include "hall.h"
#include "screens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- the row */

void Hall_RowInit(HallRow *r) {
    r->scroll = 0.0f;
    r->lastSelected = -1;
    r->sinceSelect = 10.0f;
}

void Hall_RowStep(HallRow *r, int selected, float dt) {
    if (selected != r->lastSelected) {
        bool far = fabsf((float)selected - r->scroll) > (float)HALL_JUMP;
        if (r->lastSelected < 0 || far) r->scroll = (float)selected; /* first frame, or a wrap: no slide */
        r->lastSelected = selected;
        r->sinceSelect = 0.0f;
    }
    r->sinceSelect += dt;
    float target = (float)selected;
    float k = 1.0f - expf(-16.0f * dt); /* frame-rate independent ease-out */
    r->scroll += (target - r->scroll) * k;
    if (fabsf(target - r->scroll) < 0.002f) r->scroll = target;
}

void Hall_RowSnap(HallRow *r, int selected) {
    r->scroll = (float)selected;
    r->lastSelected = selected;
}

bool Hall_Quiet(bool gameRunning, bool focused) { return gameRunning && !focused; }

int Hall_TargetFps(bool quiet) { return quiet ? HALL_QUIET_FPS : HALL_FPS; }

float Hall_MarqueePower(const HallRow *r) {
    float s = r->sinceSelect;
    if (s < 0.05f) return 0.9f;
    if (s < 0.10f) return 0.25f;
    if (s < 0.15f) return 1.0f;
    if (s < 0.19f) return 0.5f;
    return 1.0f;
}

float Hall_Focus(float offset) {
    float f = 1.0f - fabsf(offset);
    return f < 0.0f ? 0.0f : f;
}

float Hall_CabinetCenterX(float scroll, int index) {
    return KIT_SCREEN_W / 2.0f + ((float)index - scroll) * HALL_SLOT_SPACING;
}

/* render.c's DrawCabinet: 3 px units in the row, 4 px in the middle; the
 * marquee box sticks out one unit either side. */
KitRect Hall_CabinetRect(float scroll, int index) {
    float u = 3.0f + Hall_Focus((float)index - scroll);
    float x0 = Hall_CabinetCenterX(scroll, index) - (HALL_CAB_W_UNITS * u) / 2.0f;
    float y0 = (float)HALL_FLOOR_Y - 4.0f - HALL_CAB_H_UNITS * u;
    return (KitRect){x0 - u, y0, (HALL_CAB_W_UNITS + 2) * u, HALL_CAB_H_UNITS * u};
}

bool Hall_CabinetShown(float scroll, int index) {
    if (fabsf((float)index - scroll) > HALL_ROW_REACH) return false;
    KitRect r = Hall_CabinetRect(scroll, index);
    return r.x < KIT_SCREEN_W && r.x + r.w > 0.0f;
}

int Hall_Jump(int selected, int count, int delta) {
    if (count <= 0) return 0;
    int to = selected + delta;
    if (to < 0) return selected == 0 ? count - 1 : 0;
    if (to >= count) return selected == count - 1 ? 0 : count - 1;
    return to;
}

/* ---------------------------------------------------------------- input */

/* Away from you is back along the row, as up is up a list. The wheel stops
 * at the ends rather than spinning round: a flick never lands anywhere odd. */
static int Wheel(HallInput *st, int selected, int count, float wheel) {
    if (wheel == 0.0f || count <= 0) return selected;
    st->wheel += wheel;
    int notches = (int)st->wheel; /* toward zero: part notches wait for the rest */
    st->wheel -= (float)notches;
    int to = selected - notches;
    if (to < 0) to = 0;
    if (to > count - 1) to = count - 1;
    /* No credit piles up against an end; part notches away from it count. */
    if ((to == 0 && st->wheel > 0.0f) || (to == count - 1 && st->wheel < 0.0f)) st->wheel = 0.0f;
    return to;
}

static int Step(int selected, int count, int d) { return count > 0 ? (selected + d + count) % count : 0; }

static HallIntent Verb(HallVerb v) { return (HallIntent){-1, v, KA_NONE}; }

/* A press on one of the hall's widgets. */
static bool Pressed(const KitEvent *ev, int selected, int count, bool admin, HallIntent *out) {
    if (ev->type != KIT_EV_PRESS) return false;
    int cab = ev->id - WID_CABINET;
    if (cab >= 0 && cab < count) {
        *out = cab == selected ? Verb(HV_PLAY) : (HallIntent){cab, HV_NONE, KA_NONE};
        return true;
    }
    switch (ev->id) {
        case WID_MENU_CHIP:
        case WID_STRIP_MENU: *out = Verb(HV_MENU); return true;
        case WID_EXIT_SIGN: *out = Verb(HV_LEAVE); return true;
        case WID_CHALKBOARD: *out = Verb(HV_DESK); return true;
        case WID_UPDATE_PILL: *out = Verb(HV_UPDATES); return true;
        case WID_PLAYING_AS: *out = Verb(HV_PLAYER); return true;
        case WID_STRIP_PLAY: *out = Verb(count > 0 ? HV_PLAY : HV_NONE); return true;
        case WID_STRIP_DAILY: *out = Verb(count > 0 ? HV_DAILY : HV_NONE); return true;
        case WID_STRIP_THIS_GAME:
        case WID_CARD: *out = Verb(count > 0 ? HV_THIS_GAME : HV_NONE); return true;
        case WID_STRIP_STATS: *out = (HallIntent){-1, HV_OPEN, KA_OPEN_STATS}; return true;
        case WID_STRIP_PREV: *out = (HallIntent){count > 0 ? Step(selected, count, -1) : -1, HV_NONE, KA_NONE}; return true;
        case WID_STRIP_NEXT: *out = (HallIntent){count > 0 ? Step(selected, count, 1) : -1, HV_NONE, KA_NONE}; return true;
        case WID_CARD_FAVOURITE: *out = Verb(count > 0 ? HV_FAVOURITE : HV_NONE); return true;
        case WID_STRIP_ADD: *out = Verb(admin ? HV_ADD : HV_NONE); return true;
        case WID_STRIP_EDIT: *out = Verb(admin && count > 0 ? HV_EDIT : HV_NONE); return true;
        case WID_STRIP_REMOVE: *out = Verb(admin && count > 0 ? HV_REMOVE : HV_NONE); return true;
        default: return false;
    }
}

/* Keys, d-pad, LB/RB and the wheel: arrows wrap round, as they always have. */
static int Move(HallInput *st, const Input *in, int selected, int count) {
    if (count <= 0) return selected;
    int to = selected;
    if (Keymap_Pressed(KA_HALL_PREV, in)) to = Step(to, count, -1);
    if (Keymap_Pressed(KA_HALL_NEXT, in)) to = Step(to, count, 1);
    if (Keymap_Pressed(KA_HALL_JUMP_PREV, in)) to = Hall_Jump(to, count, -HALL_JUMP);
    if (Keymap_Pressed(KA_HALL_JUMP_NEXT, in)) to = Hall_Jump(to, count, HALL_JUMP);
    return Wheel(st, to, count, in->wheel);
}

/* One a frame: two keys landing together (Enter and Delete) must never
 * leave a coin in flight under the remove question. */
static HallVerb Action(const Input *in, int count, bool admin) {
    if (count > 0) {
        if (Keymap_Pressed(KA_HALL_PLAY, in)) return HV_PLAY;
        if (Keymap_Pressed(KA_HALL_DAILY, in)) return HV_DAILY;
    }
    if (Keymap_Pressed(KA_HALL_THIS_GAME, in)) return HV_THIS_GAME; /* on an empty arcade it opens Trophies, as it always did */
    if (count > 0 && Keymap_Pressed(KA_HALL_FAVOURITE, in)) return HV_FAVOURITE;
    if (Keymap_Pressed(KA_HALL_ORDER, in)) return HV_ORDER; /* an empty row can still be put in order */
    if (count > 0) {
        if (admin && Keymap_Pressed(KA_ADMIN_REMOVE, in)) return HV_REMOVE;
        if (admin && Keymap_Pressed(KA_ADMIN_EDIT, in)) return HV_EDIT;
    }
    if (admin && Keymap_Pressed(KA_ADMIN_ADD, in)) return HV_ADD;
    return HV_NONE;
}

HallIntent Hall_Read(HallInput *st, const Input *in, const KitEvent *ev, int selected, int count, bool admin) {
    HallIntent out = Verb(HV_NONE);
    if (Pressed(ev, selected, count, admin, &out)) return out;
    if (Keymap_Pressed(KA_HALL_MENU, in)) return Verb(HV_MENU);
    KeyAction open = Screens_Shortcut(in);
    if (open != KA_NONE) return (HallIntent){-1, HV_OPEN, open};
    int to = Move(st, in, selected, count);
    out.moveTo = to != selected ? to : -1;
    out.verb = Action(in, count, admin);
    return out;
}

/* ---------------------------------------------------------- status line */

static int Width(const char *text, int size) { return Kit_TextWidth(text, size); }

void Hall_Fit(char *text, size_t size, int maxW, int textSize, bool path) {
    if (Width(text, textSize) <= maxW) return;
    char whole[HALL_STATUS_LEN * 4];
    snprintf(whole, sizeof(whole), "%.*s", (int)sizeof(whole) - 1, text);
    int len = (int)strlen(whole);
    if (path) {
        /* Keep what comes first up to the path, then the path's end. */
        const char *slash = strchr(whole, '/');
        const char *tilde = strchr(whole, '~');
        const char *start = tilde && (!slash || tilde < slash) ? tilde : slash;
        int head = start ? (int)(start - whole) : 0;
        for (int cut = head + 1; cut < len; cut++) {
            snprintf(text, size, "%.*s...%s", head, whole, whole + cut);
            if (Width(text, textSize) <= maxW) return;
        }
    }
    for (int n = len - 1; n > 0; n--) {
        snprintf(text, size, "%.*s...", n, whole);
        if (Width(text, textSize) <= maxW) return;
    }
    snprintf(text, size, "...");
}

static void Line(HallStatus *out, const char *text, int size, HallTone tone) {
    snprintf(out->text, sizeof(out->text), "%.*s", (int)sizeof(out->text) - 1, text);
    out->size = size;
    out->tone = tone;
}

/* The prompt names the key the player is using: Enter, or the pad's A. */
static void Prompt(HallStatus *out, InputDevice device, const char *verb, HallTone tone) {
    char key[24], text[HALL_STATUS_LEN];
    Keymap_Hint(KA_HALL_PLAY, device == DEV_PAD ? DEV_PAD : DEV_KEYBOARD, key, sizeof(key));
    snprintf(text, sizeof(text), "Press %s to %s", key, verb);
    Line(out, text, 16, tone);
    out->blink = true;
}

void Hall_Status(const HallStatusIn *in, HallStatus *out) {
    memset(out, 0, sizeof(*out));
    if (in->installing) {
        Line(out, "Installing", 16, HT_BUSY);
        out->dots = true;
        return; /* "Installing..." fits at 16 px */
    }
    /* Fitted while whole: a long path or error is cut once, in the right place. */
    char text[HALL_STATUS_LEN * 4] = "";
    int size = 8;
    bool path = false;
    if (in->installFailed) {
        snprintf(text, sizeof(text), "Install failed: %.300s", in->installLog && in->installLog[0] ? in->installLog : "see install.log");
        path = true;
        Hall_Fit(text, sizeof(text), HALL_STATUS_W, size, path);
        Line(out, text, size, HT_BAD);
    } else if (in->error && in->error[0]) {
        snprintf(text, sizeof(text), "%.300s", in->error);
        Hall_Fit(text, sizeof(text), HALL_STATUS_W, size, false);
        Line(out, text, size, HT_BAD);
    } else if (in->note && in->note[0]) {
        snprintf(text, sizeof(text), "%.300s", in->note);
        Hall_Fit(text, sizeof(text), HALL_STATUS_W, size, false);
        Line(out, text, size, HT_NOTE);
    } else if (in->installed) {
        Prompt(out, in->device, in->tool ? "open Chipforge" : "play", HT_PLAY);
    } else if (in->canInstall) {
        Prompt(out, in->device, "install", HT_INSTALL);
    } else {
        Line(out, "Not installed, and no folder to install it from.", 8, HT_DIM);
    }
    if (out->size == 16 && Width(out->text, 16) > HALL_STATUS_W) out->size = 8;
    Hall_Fit(out->text, sizeof(out->text), HALL_STATUS_W, out->size, false);
}

void Hall_DailyReason(const char *name, bool installed, bool hasDaily, char *out, size_t size) {
    if (installed && hasDaily) {
        snprintf(out, size, "%s", "");
        return;
    }
    if (!hasDaily) snprintf(out, size, "%.22s has no daily challenge.", name && name[0] ? name : "This machine");
    else snprintf(out, size, "Install it first to play its daily challenge.");
}

void Hall_Tilde(const char *path, const char *home, char *out, size_t size) {
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(path, home, hl) == 0 && path[hl] == '/') snprintf(out, size, "~%s", path + hl);
    else snprintf(out, size, "%s", path);
}
