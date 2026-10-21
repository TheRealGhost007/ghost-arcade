/* The Practice room (3.5.0, REWORK_PLAN.md "Practice room"): a screen off
 * Play setup's "Practice room" button with the room's options (start level,
 * gravity, the lock delay, Hold, starting garbage, finesse hints: menumodel.h's
 * Practice_*), the undo line naming your keys, and Start. Up and Down move
 * from row to row, Left and Right change a value (a click on its arrows or
 * halves too), Enter, Space or pad A press Start; every change saves at
 * once. Esc, pad B, the mouse's back button or [<- Back] go back to Play
 * setup. The run it starts is a Marathon on the board Play setup has, with
 * the rules bent, a PRACTICE tag over the well and nothing recorded. */
#include "screens.h"
#include "comfort.h"
#include "layout.h"
#include "menumodel.h"
#include "sfx.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define INTRO_Y 112
/* The rows' plates, labels and controls are layout.h's PRACTICE_*: inside
 * the 40..580 margin since 3.6.1 (20..600 before), the stepper's "<" in its
 * own 40 px cell as in Settings. */
#define ROW_X PRACTICE_ROW_X
#define ROW_W PRACTICE_ROW_W
#define LABEL_X PRACTICE_LABEL_X
#define ROW_Y 160
#define ROW_PITCH 44
#define ROW_H 32
#define CONTROL_H 28
#define STEPPER_X PRACTICE_STEPPER_X
#define STEPPER_W PRACTICE_STEPPER_W
#define TOGGLE_X PRACTICE_TOGGLE_X
#define TOGGLE_W PRACTICE_TOGGLE_W
#define UNDO_Y 448
#define HELP_Y 500
#define HINTS_CY 676

static const KitRect kStartRect = {40, 572, 540, 48};

static KitRect RowRect(int i) { return (KitRect){ROW_X, (float)(ROW_Y + i * ROW_PITCH), ROW_W, ROW_H}; }
static bool IsOption(int id) { return id >= PR_OPT0 && id < PR_OPT0 + PRO_COUNT; }

void ScrPractice_Layout(const App *a, KitList *l) {
    const Settings *s = &a->settings;
    Kit_Clear(l, 0); /* Up and Down walk the rows here (the controls differ in width, so the kit's own walk would skip some) */
    Scr_AddBack(l, PR_BACK);
    for (int o = 0; o < PRO_COUNT; o++) {
        KitRect row = RowRect(o);
        char words[32];
        Practice_OptionWords(s, (PracticeOption)o, words, sizeof(words));
        int lo, hi;
        Practice_OptionRange((PracticeOption)o, &lo, &hi);
        PracticeOptionKind kind = Practice_OptionKind((PracticeOption)o);
        KitItem *it;
        if (kind == PRO_KIND_TOGGLE) {
            /* Named for its row (never drawn): the harness asks for controls by name. */
            it = Kit_Add(l, PR_OPT0 + o, KIT_TOGGLE, (KitRect){TOGGLE_X, row.y + (row.h - CONTROL_H) / 2.0f, TOGGLE_W, CONTROL_H}, Practice_OptionLabel((PracticeOption)o));
            if (it) it->value = Practice_OptionValue(s, (PracticeOption)o);
        } else {
            it = Kit_Add(l, PR_OPT0 + o, KIT_STEPPER, (KitRect){STEPPER_X, row.y + (row.h - CONTROL_H) / 2.0f, STEPPER_W, CONTROL_H}, words);
            if (it) {
                it->value = Practice_OptionValue(s, (PracticeOption)o);
                it->min = lo;
                it->max = hi;
                if (kind == PRO_KIND_CHOICE) it->flags |= KIT_WRAP;
            }
        }
    }
    KitItem *it = Kit_Add(l, PR_START, KIT_BUTTON, kStartRect, "Start");
    if (it) it->flags |= KIT_ACCENT;
}

static KitRect PlateFor(int id) {
    if (IsOption(id)) return RowRect(id - PR_OPT0);
    if (id == PR_START) return kStartRect;
    return RowRect(0);
}

void ScrPractice_Enter(App *a, bool back) {
    PracticeState *st = &a->practice;
    Kit_Enter(&st->kit, back ? st->kit.focus : PR_OPT0);
    Kit_GlideReset(&st->glide);
    Scr_Cursor(false);
}

void ScrPlay_OpenPractice(App *a) {
    a->play.startPractice = true;
    App_Open(a, SCR_PLAY);
}

static void Start(App *a) {
    Settings_Save(&a->settings);
    Sfx_Jingle(JINGLE_READY);
    ScrPlay_OpenPractice(a);
}

/* Up and Down from row to row, then Start (no wrap), as Settings walks its
 * rows. True if the focus moved. */
static bool Vertical(PracticeState *st, int dir) {
    int f = st->kit.focus, next = f;
    if (IsOption(f)) {
        int i = f - PR_OPT0 + dir;
        next = i < 0 ? f : (i >= PRO_COUNT ? PR_START : PR_OPT0 + i);
    } else if (f == PR_START) {
        next = dir < 0 ? PR_OPT0 + PRO_COUNT - 1 : f;
    } else {
        next = PR_OPT0;
    }
    if (next == f) return false;
    st->kit.focus = next;
    st->kit.keyDown = KIT_NONE; /* a press does not follow the focus */
    return true;
}

void ScrPractice_Update(App *a, const Input *in, float dt) {
    PracticeState *st = &a->practice;
    (void)dt;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    KitList l;
    ScrPractice_Layout(a, &l);
    KitEvent ev = Kit_Update(&st->kit, &l, in);
    bool moved = false;
    if (Keymap_Pressed(KA_UI_UP, in)) moved = Vertical(st, -1);
    else if (Keymap_Pressed(KA_UI_DOWN, in)) moved = Vertical(st, 1);
    if (moved) Scr_SoundMove();
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == PR_BACK) { Scr_SoundBack(); App_Back(a); return; }
        if (ev.id == PR_START) { Start(a); return; }
    } else if (ev.type == KIT_EV_CHANGE && IsOption(ev.id)) {
        if (Practice_SetOption(&a->settings, (PracticeOption)(ev.id - PR_OPT0), ev.value)) {
            Settings_Save(&a->settings);
            Scr_SoundMove();
        }
    }
    Scr_Cursor(Kit_WantsHand(&st->kit, &l));
}

/* --------------------------------------------------------------- drawing */

static void DrawHints(const App *a) {
    bool onOption = IsOption(a->practice.kit.focus);
    KitHint h[4] = {
        {2, {KA_UI_UP, KA_UI_DOWN}, "choose"},
        {2, {KA_UI_LEFT, KA_UI_RIGHT}, "change"},
        {1, {KA_UI_PRESS}, "start"},
        {1, {KA_BACK}, "back"},
    };
    if (onOption) KitDraw_Hints(h, 4, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
    else {
        KitHint k[3] = {h[0], h[2], h[3]};
        KitDraw_Hints(k, 3, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
    }
}

void ScrPractice_Draw(App *a, float dt) {
    PracticeState *st = &a->practice;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);

    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Practice", 40, 48);
    char board[40], intro[120];
    Setup_BoardLabel(&a->settings, board, sizeof(board));
    snprintf(intro, sizeof(intro), "A Marathon on %s with the rules bent. Nothing here is recorded.", board);
    char lines[2][SCR_WRAP_LEN];
    int n = Scr_Wrap(intro, UI_T8, WINDOW_WIDTH - 2 * LABEL_X, lines, 2);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], LABEL_X, INTRO_Y + i * 14, UI_T8, t->dim);

    KitList l;
    ScrPractice_Layout(a, &l);
    bool lit = a->input.lastDevice != DEV_MOUSE; /* the mouse lights controls, not rows */
    KitRect plate = Kit_GlideTo(&st->glide, PlateFor(st->kit.focus), dt, Comfort_Motion());
    if (lit) KitDraw_Plate(plate);
    for (int o = 0; o < PRO_COUNT; o++) {
        KitRect row = RowRect(o);
        bool focused = lit && st->kit.focus == PR_OPT0 + o;
        Ui_Text(Practice_OptionLabel((PracticeOption)o), LABEL_X, (int)(row.y + (row.h - 16) / 2), UI_T16, focused ? t->light : t->text);
    }
    /* A row with one control shows its focus on the row, not the control. */
    KitView kv = Scr_View(a, &l, &st->kit, NULL);
    for (int i = 0; i < l.count; i++) {
        const KitItem *it = &l.items[i];
        KitLook look = KitDraw_LookOf(&kv, it);
        if (IsOption(it->id) && look == KIT_LOOK_FOCUSED && lit) look = KIT_LOOK_NORMAL;
        KitDraw_Item(it, look, Kit_HoverCell(&st->kit, it, kv.device), Kit_PressedCell(&st->kit, it));
    }

    /* The undo line, with your keys, and the chosen row's help. */
    char undo[128];
    Practice_UndoWords(&a->play.view.keys, undo, sizeof(undo));
    n = Scr_Wrap(undo, UI_T8, WINDOW_WIDTH - 2 * LABEL_X, lines, 2);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], LABEL_X, UNDO_Y + i * 14, UI_T8, t->gold);
    const char *help = NULL;
    if (kv.device == DEV_MOUSE && IsOption(st->kit.hover)) help = Practice_OptionHelp((PracticeOption)(st->kit.hover - PR_OPT0));
    else if (kv.device != DEV_MOUSE && IsOption(st->kit.focus)) help = Practice_OptionHelp((PracticeOption)(st->kit.focus - PR_OPT0));
    if (help) {
        n = Scr_Wrap(help, UI_T8, WINDOW_WIDTH - 2 * LABEL_X, lines, 2);
        for (int i = 0; i < n; i++) Ui_Text(lines[i], LABEL_X, HELP_Y + i * 14, UI_T8, t->dim);
    }
    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}
