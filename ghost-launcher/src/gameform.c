#include "gameform.h"
#include "screens.h"
#include "keymap.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const kPrompts[MANIFEST_FIELD_COUNT] = {
    "Name",
    "Path to the program",
    "Path to an icon (optional)",
    "One-line description (optional)",
    "Source folder to install it from (optional)",
};

const char *GameForm_Prompt(int field) { return field >= 0 && field < MANIFEST_FIELD_COUNT ? kPrompts[field] : ""; }

int GameForm_Chars(void) { return (FORM_W - 2 * FORM_PAD + 2) / FORM_CHAR_W; } /* the last one needs no gap */

/* A path is read from its end (the file's name); a name or a description from its start. */
static bool IsPath(int field) { return field == 1 || field == 2 || field == 4; }

bool GameForm_RestCut(int field, const char *text, int *view, int *lead) {
    int len = (int)strlen(text), chars = GameForm_Chars();
    *view = 0;
    *lead = 0;
    if (len <= chars) return false;
    if (IsPath(field)) {
        *lead = 3;
        *view = len - (chars - 3);
    }
    return true;
}

KitRect GameForm_BoxRect(int field) {
    return (KitRect){FORM_X, (float)(FORM_Y + field * FORM_PITCH + FORM_BOX_DY), FORM_W, FORM_BOX_H};
}

/* The field takes the keys, its cursor at the end. */
static void Begin(GameForm *f, int field) {
    f->active = field;
    TextField_Begin(&f->field, f->text[field], MANIFEST_FIELD_LEN - 1);
    TextField_KeepCursorVisible(&f->field, GameForm_Chars());
}

static void Open(GameForm *f, int editing) {
    f->editing = editing;
    memcpy(f->was, f->text, sizeof(f->was));
    f->carry = true;
    f->error[0] = '\0';
    f->errorField = -1;
    Begin(f, 0);
}

void GameForm_OpenAdd(GameForm *f) {
    memset(f, 0, sizeof(*f));
    Open(f, -1);
}

void GameForm_OpenEdit(GameForm *f, const GameEntry *e, int index) {
    memset(f, 0, sizeof(*f));
    const char *v[MANIFEST_FIELD_COUNT] = {e->name, e->exec, e->icon, e->desc, e->install};
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) snprintf(f->text[i], MANIFEST_FIELD_LEN, "%s", v[i]);
    Open(f, index);
}

const char *GameForm_Text(const GameForm *f, int field) {
    if (field < 0 || field >= MANIFEST_FIELD_COUNT) return "";
    return field == f->active && f->field.editing ? f->field.text : f->text[field];
}

bool GameForm_Changed(const GameForm *f) {
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) if (strcmp(GameForm_Text(f, i), f->was[i]) != 0) return true;
    return false;
}

void GameForm_Values(const GameForm *f, char out[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN]) {
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) {
        const char *t = GameForm_Text(f, i);
        while (*t == ' ') t++;
        int n = (int)strlen(t);
        while (n > 0 && t[n - 1] == ' ') n--;
        snprintf(out[i], MANIFEST_FIELD_LEN, "%.*s", n, t);
    }
}

void GameForm_WhatStays(const GameForm *f, const Manifest *m, const PlayStats *stats, const Profiles *p, const LauncherIni *ini,
                        CarryOver *c) {
    memset(c, 0, sizeof(*c));
    if (f->editing < 0 || !m || f->editing >= m->count) return;
    char v[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    GameForm_Values(f, v);
    const GameEntry *e = &m->games[f->editing];
    Machines_WhatStays(c, e->name, v[0], e->exec, v[1], stats, p, ini);
}

static bool Fail(int *field, int which, char *msg, size_t size, const char *why) {
    *field = which;
    snprintf(msg, size, "%s", why);
    return false;
}

bool GameForm_Validate(const GameForm *f, const Manifest *m, int *field, char *msg, size_t size) {
    char v[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    GameForm_Values(f, v);
    *field = -1;
    msg[0] = '\0';
    if (!v[0][0]) return Fail(field, 0, msg, size, "Give the machine a name first.");
    if (!v[1][0]) return Fail(field, 1, msg, size, "Enter the path to its program first.");
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++)
        if (strchr(v[i], '|')) return Fail(field, i, msg, size, "The | sign cannot go in a machine's details.");
    for (int j = 0; m && j < m->count; j++) {
        if (j == f->editing || strcasecmp(m->games[j].name, v[0]) != 0) continue;
        char why[96];
        snprintf(why, sizeof(why), "Another machine is already called %.40s.", m->games[j].name);
        return Fail(field, 0, msg, size, why);
    }
    return true;
}

void GameForm_Layout(const GameForm *f, const CarryOver *c, KitList *l) {
    Kit_Clear(l, 0); /* the form moves the keys itself (MoveKeys): the kit's own Right would climb from Cancel to the switch */
    Screens_AddBack(l);
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) {
        KitItem *box = Kit_Add(l, WID_FORM_FIELD + i, KIT_BUTTON, GameForm_BoxRect(i), kPrompts[i]);
        if (box) box->flags |= KIT_NOFOCUS | KIT_SELFDRAWN; /* render.c draws the text in it */
    }
    if (f->editing >= 0 && c && Machines_CanCarry(c)) {
        KitItem *t = Kit_Add(l, WID_FORM_CARRY, KIT_TOGGLE, (KitRect){FORM_TOGGLE_X, FORM_CARRY_Y, 144, 32}, "Carry them over");
        if (t) t->value = f->carry ? 1 : 0;
    }
    Kit_Add(l, WID_FORM_SAVE, KIT_BUTTON, (KitRect){FORM_X, FORM_BUTTONS_Y, FORM_BUTTON_W, FORM_BUTTON_H}, "Save");
    Kit_Add(l, WID_FORM_CANCEL, KIT_BUTTON, (KitRect){FORM_X + FORM_BUTTON_W + 16, FORM_BUTTONS_Y, FORM_BUTTON_W, FORM_BUTTON_H},
            "Cancel");
    Screens_AddClose(l);
}

KitLook GameForm_Look(const GameForm *f, KitLook look) {
    return look == KIT_LOOK_FOCUSED && f->active != FORM_ON_BUTTONS ? KIT_LOOK_NORMAL : look;
}

/* ------------------------------------------------------------ the keys */

/* Where Tab, Up and Down go: the fields, then the switch (when there is
 * one), then Save and Cancel. */
enum { STOP_CARRY = MANIFEST_FIELD_COUNT, STOP_SAVE, STOP_CANCEL, STOP_COUNT };

static void Commit(GameForm *f) {
    if (f->active == FORM_ON_BUTTONS) return;
    snprintf(f->text[f->active], MANIFEST_FIELD_LEN, "%s", f->field.text);
    TextField_End(&f->field);
}

static int StopNow(const GameForm *f, const KitState *k) {
    if (f->active != FORM_ON_BUTTONS) return f->active;
    if (k->focus == WID_FORM_CARRY) return STOP_CARRY;
    return k->focus == WID_FORM_CANCEL ? STOP_CANCEL : STOP_SAVE;
}

static void GoTo(GameForm *f, KitState *k, int stop) {
    Commit(f);
    k->keyDown = KIT_NONE; /* a press does not follow the keys elsewhere */
    if (stop < MANIFEST_FIELD_COUNT) {
        Begin(f, stop);
        return;
    }
    f->active = FORM_ON_BUTTONS;
    k->focus = stop == STOP_CARRY ? WID_FORM_CARRY : (stop == STOP_CANCEL ? WID_FORM_CANCEL : WID_FORM_SAVE);
}

static bool HasCarry(const KitList *l) { return Kit_Get(l, WID_FORM_CARRY) != NULL; }

/* Tab and Shift+Tab go round every stop. */
static int Cycle(int stop, int dir, bool carry) {
    do stop = (stop + dir + STOP_COUNT) % STOP_COUNT;
    while (stop == STOP_CARRY && !carry);
    return stop;
}

/* Up and Down: Save and Cancel are one row, and there is nothing below it. */
static int Line(int stop, int dir, bool carry) {
    if (stop == STOP_CANCEL) stop = STOP_SAVE;
    int to = stop + dir;
    if (to == STOP_CARRY && !carry) to += dir;
    return (to < 0 || to > STOP_SAVE) ? -1 : to;
}

/* Enter on a field: the old form's next step. The name and the program
 * cannot be left empty; the last field goes on to Save. */
static void Advance(GameForm *f, KitState *k) {
    const char *t = f->field.text;
    while (*t == ' ') t++;
    if (f->active <= 1 && !*t) {
        snprintf(f->error, sizeof(f->error), "%s", f->active == 0 ? "Give the machine a name first." : "Enter the path to its program first.");
        f->errorField = f->active;
        return;
    }
    f->error[0] = '\0';
    f->errorField = -1;
    GoTo(f, k, f->active + 1 < MANIFEST_FIELD_COUNT ? f->active + 1 : STOP_SAVE);
}

static GameFormResult Submit(GameForm *f, KitState *k, const Manifest *m) {
    Commit(f);
    int field;
    char why[96];
    if (GameForm_Validate(f, m, &field, why, sizeof(why))) {
        f->error[0] = '\0';
        f->errorField = -1;
        return GF_SAVE;
    }
    snprintf(f->error, sizeof(f->error), "%s", why);
    f->errorField = field;
    GoTo(f, k, field);
    return GF_NONE;
}

/* A press on a box gives it the keys, the cursor where it landed on the
 * text as drawn (GameForm_RestCut: a long path at rest shows its end). */
static bool ClickIntoField(GameForm *f, KitState *k, const Input *in) {
    if (!in->mousePressed[MB_LEFT] || !in->mouseInside) return false;
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) {
        KitRect box = GameForm_BoxRect(i);
        if (!Kit_Inside(box, in->mouseX, in->mouseY)) continue;
        int view = f->field.view, lead = 0;
        if (i != f->active) {
            GameForm_RestCut(i, f->text[i], &view, &lead);
            GoTo(f, k, i);
        }
        f->field.view = view;
        TextField_ClickAt(&f->field, in->mouseX, box.x + FORM_PAD + (float)(lead * FORM_CHAR_W), FORM_CHAR_W);
        TextField_KeepCursorVisible(&f->field, GameForm_Chars());
        return true;
    }
    return false;
}

/* Left and Right go between Save and Cancel only: in a field they move the
 * cursor, and on the switch they turn it (the kit does that). */
static bool MoveKeys(GameForm *f, KitState *k, const KitList *l, const Input *in) {
    int stop = StopNow(f, k), to = -1;
    bool carry = HasCarry(l), onRow = stop >= STOP_SAVE;
    if (Keymap_Pressed(KA_UI_NEXT_TAB, in)) to = Cycle(stop, 1, carry);
    else if (Keymap_Pressed(KA_UI_PREV_TAB, in)) to = Cycle(stop, -1, carry);
    else if (Keymap_Pressed(KA_UI_DOWN, in)) to = Line(stop, 1, carry);
    else if (Keymap_Pressed(KA_UI_UP, in)) to = Line(stop, -1, carry);
    else if (onRow && Keymap_Pressed(KA_UI_LEFT, in)) to = STOP_SAVE;
    else if (onRow && Keymap_Pressed(KA_UI_RIGHT, in)) to = STOP_CANCEL;
    else return false;
    if (to >= 0 && to != stop) GoTo(f, k, to);
    return true;
}

GameFormResult GameForm_Update(GameForm *f, KitState *k, const KitList *l, const Input *in, const char *clipboard,
                               const Manifest *m) {
    Input mouse;
    const Input *kin = in;
    if (f->active != FORM_ON_BUTTONS) { /* the keys are the field's */
        Input_MouseOnly(in, &mouse);
        kin = &mouse;
    }
    KitEvent ev = Kit_Update(k, l, kin);
    if (ev.type == KIT_EV_PRESS && ev.id == WID_CLOSE) return GF_QUIT;
    if (ev.type == KIT_EV_PRESS && (ev.id == WID_BACK || ev.id == WID_FORM_CANCEL)) return GF_CANCEL;
    if (ev.type == KIT_EV_PRESS && ev.id == WID_FORM_SAVE) return Submit(f, k, m);
    if (ev.type == KIT_EV_CHANGE && ev.id == WID_FORM_CARRY) {
        f->carry = ev.value != 0;
        return GF_NONE;
    }
    if (Keymap_Pressed(KA_BACK, in)) return GF_CANCEL;
    if (ClickIntoField(f, k, in)) return GF_NONE;
    if (MoveKeys(f, k, l, in)) return GF_NONE;
    if (f->active == FORM_ON_BUTTONS) return GF_NONE; /* the kit had the rest */
    if (Keymap_Pressed(KA_FORM_NEXT, in)) {
        Advance(f, k);
        return GF_NONE;
    }
    if (TextField_Update(&f->field, in, clipboard) == TF_CHANGED && f->errorField == f->active) f->error[0] = '\0';
    TextField_KeepCursorVisible(&f->field, GameForm_Chars());
    return GF_NONE;
}
