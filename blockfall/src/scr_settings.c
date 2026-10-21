/* Settings with tabs (REWORK_PLAN.md, phase 6): Controls, Feel, Look, Sound
 * and Game across the top (LB/RB, Tab, Shift+Tab, Left/Right on the tabs or
 * a click). setpage.c lays each tab out and says what a change does; this
 * file reads the keys while a key or button is being set, saves every change
 * at once, plays the sounds and draws the page.
 *  - Controls: a cap pressed (Enter, Space, pad A or a click) waits five
 *    seconds for the new key ("Press a key"; Esc, pad B or a click give up)
 *    or button ("Press a button"; Menu, Esc or a click give up), with a bar
 *    draining under it. Shift, Ctrl and Alt bind when let go, so Alt+Enter
 *    still only goes fullscreen. M (the mute key), keys Blockfall cannot
 *    store, and Enter, Space or pad A for Pause or Restart (they pick menu
 *    items) are turned away with a note. A clash swaps the two actions, and
 *    the note line says what moved. Delete, Backspace or pad Y clear a
 *    second key or button. Reset controls asks first.
 *  - Feel: the presets, the three handling values, and a strip where a
 *    piece slides with them (hold Left or Right on it to try them).
 *  - Look, Sound, Game: switches, steppers and ten-block bars; the effects
 *    volume ticks at the level it is set to, and Test sound plays a clear.
 * Rows marked * are shared with every game (prefs.ini). Esc, pad B, the
 * mouse's back button or [<- Back] go back. */
#include "screens.h"
#include "comfort.h"
#include "controls.h"
#include "keynames.h"
#include "look.h"
#include "menukeys.h"
#include "music.h"
#include "prefs.h"
#include "sfx.h"
#include "blockdraw.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NOTE_SECONDS 6.0f
enum { TONE_DIM, TONE_GOLD, TONE_LIGHT };

static void Note(App *a, const char *text, int tone) {
    snprintf(a->set.note, sizeof(a->set.note), "%s", text ? text : "");
    a->set.noteTone = tone;
    a->set.noteT = 0.0f;
}

static SetView View(const App *a) {
    const SettingsScreenState *st = &a->set;
    SetView v = {&a->settings, Prefs_Get(), st->tab, KIT_NONE, IsAudioDeviceReady()};
    if (st->capture.on) v.captureId = SID_CAP_OF(st->capture.action, st->capture.column);
    return v;
}

static void Layout(const App *a, SetPage *p) {
    SetView v = View(a);
    SetPage_Layout(&v, p);
}

static void Rebuild(App *a) {
    Actions_Keys(&a->settings, Keys_CodeFromName, &a->keys);
    Controls_Words(&a->settings, &a->play.view.keys);
}

void ScrSettings_Enter(App *a, bool back) {
    SettingsScreenState *st = &a->set;
    if (back) return;
    if (st->tab < 0 || st->tab >= SET_TABS) st->tab = TAB_CONTROLS;
    Capture_End(&st->capture);
    st->dialog.open = false;
    st->note[0] = '\0';
    FeelStrip_Reset(&st->strip);
    static SetPage page; /* big: off the stack */
    Layout(a, &page);
    Kit_Enter(&st->kit, SetPage_FirstRowControl(&page));
    Kit_GlideReset(&st->glide);
    Scr_Cursor(false);
}

/* ---------------------------------------------------------- capturing */

static const char *ColumnWords(int column) {
    switch (column) {
        case 1: return " (second key)";
        case 3: return " (second button)";
        default: return "";
    }
}

static void CapturePrompt(App *a) {
    const Capture *c = &a->set.capture;
    char text[160];
    if (c->pad) snprintf(text, sizeof(text), "Press a button for %s%s. Menu or Esc gives up.", Controls_ActionLabel(c->action), ColumnWords(c->column));
    else snprintf(text, sizeof(text), "Press a key for %s%s. Esc gives up.", Controls_ActionLabel(c->action), ColumnWords(c->column));
    Note(a, text, TONE_LIGHT);
}

static void EndCapture(App *a) {
    Capture_End(&a->set.capture);
    /* A key held from the capture (an arrow) must not go on to move the
     * focus once it repeats. */
    Input_DropHolds(&a->inputState);
}

static void Bound(App *a, BindResult r) {
    if (r.kind == BIND_SET || r.kind == BIND_SWAPPED) {
        Settings_Save(&a->settings);
        Rebuild(a);
        Scr_SoundConfirm();
    } else if (r.kind == BIND_REFUSED) {
        Sfx_Jingle(JINGLE_WARNING);
    } else {
        Scr_SoundBack();
    }
    Note(a, r.note, r.kind == BIND_SET || r.kind == BIND_SAME ? TONE_DIM : TONE_GOLD);
    EndCapture(a);
}

static void GiveUp(App *a, const char *why) {
    Scr_SoundBack();
    Note(a, why, TONE_DIM);
    EndCapture(a);
}

static bool FunctionKey(int code) { return code >= KEY_F1 && code <= KEY_F12; }

/* The first pad button pressed this frame that a cap could take (Menu gives
 * up, above), or -1. */
static int FirstPadButton(const Input *in) {
    for (int b = 0; b < PB_COUNT; b++)
        if (b != PB_MENU && in->pad[b]) return b;
    return -1;
}

static void UpdateCapture(App *a, const Input *in, float dt) {
    Capture *c = &a->set.capture;
    if (Capture_Tick(c, dt)) { GiveUp(a, "Nothing was pressed, so it stays as it was."); return; }
    bool click = in->mousePressed[MB_LEFT] || in->mousePressed[MB_RIGHT] || in->mousePressed[MB_BACK];
    bool esc = in->key[IK_ESCAPE];
    if (esc || click || in->pad[PB_MENU] || (!c->pad && in->pad[PB_B])) {
        while (GetKeyPressed() != 0) { } /* the Esc itself */
        GiveUp(a, "Left as it was.");
        return;
    }
    if (c->pad) {
        /* The button this frame would bind (the first down, as
         * Capture_FeedPad picks it): A for Pause or Restart is turned away
         * as Enter is for a key, and the cap keeps waiting. */
        int button = FirstPadButton(in);
        char why[96];
        if (button >= 0 && !Controls_PadAllowed(c->action, button, why, sizeof(why))) {
            while (GetKeyPressed() != 0) { }
            Note(a, why, TONE_GOLD);
            return;
        }
        BindResult r;
        CaptureFeed fed = Capture_FeedPad(c, &a->settings, in, GetKeyPressed() != 0, &r);
        if (fed == CAPFEED_BOUND) Bound(a, r);
        else if (fed == CAPFEED_KEY_WHILE_PAD) {
            while (GetKeyPressed() != 0) { }
            Note(a, "That is a key: press a button on the pad. Menu or Esc gives up.", TONE_GOLD);
        }
        return;
    }
    int code;
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    while ((code = GetKeyPressed()) != 0) {
        if (code == KEY_ENTER && alt) { c->pendingMod[0] = '\0'; continue; } /* Alt+Enter is fullscreen, never a binding */
        if (FunctionKey(code)) continue; /* F2, F3, F11 do what they always do */
        const char *name = Keys_NameFromCode(code);
        if (name && Capture_IsModifier(name)) {
            /* Shift, Ctrl or Alt bind when let go, unless a key comes with them. */
            snprintf(c->pendingMod, sizeof(c->pendingMod), "%s", name);
            continue;
        }
        char why[96];
        if (!Controls_KeyAllowed(c->action, name, why, sizeof(why))) {
            c->pendingMod[0] = '\0';
            Note(a, why, TONE_GOLD);
            continue;
        }
        Bound(a, Controls_BindKey(&a->settings, c->action, c->column, name));
        return;
    }
    if (c->pendingMod[0] && !IsKeyDown(Keys_CodeFromName(c->pendingMod))) Bound(a, Controls_BindKey(&a->settings, c->action, c->column, c->pendingMod));
}

/* ------------------------------------------------------------- a frame */

static void ResetAsked(App *a, const Input *in) {
    SettingsScreenState *st = &a->set;
    KitList dl;
    Kit_DialogLayout(&st->dialog, &dl);
    KitEvent ev = Kit_Update(&st->dlgKit, &dl, in);
    KitDialogResult r = Kit_DialogAnswer(&st->dialog, &ev, in);
    if (r == KIT_DLG_ACT) {
        Controls_Reset(&a->settings);
        Settings_Save(&a->settings);
        Rebuild(a);
        Scr_SoundConfirm();
        Note(a, "Every key and button is back to how Blockfall came.", TONE_GOLD);
    } else if (r == KIT_DLG_SAFE) {
        Scr_SoundBack();
    }
    Scr_Cursor(st->dialog.open && Kit_WantsHand(&st->dlgKit, &dl));
}

static void Apply(App *a, SetEffect e) {
    Settings *s = &a->settings;
    ArcadePrefs *pr = Prefs_Get();
    SettingsScreenState *st = &a->set;
    switch (e.kind) {
        case SE_TAB: {
            st->tab = e.value;
            static SetPage page;
            Layout(a, &page);
            if (st->kit.focus != SID_TABS) st->kit.focus = SetPage_FirstRowControl(&page);
            FeelStrip_Reset(&st->strip);
            st->note[0] = '\0';
            Scr_SoundMove();
            return;
        }
        case SE_CAPTURE:
            Capture_Begin(&st->capture, e.captureAction, e.captureColumn);
            while (GetKeyPressed() != 0) { } /* only keys pressed from now on */
            Scr_SoundConfirm();
            CapturePrompt(a);
            return;
        case SE_RESET_ASK:
            Kit_DialogOpen(&st->dialog, &st->dlgKit, 0, 0, "Reset controls?", "Every key and button goes back to how Blockfall came.", "", "Keep mine", "Reset");
            Sfx_Jingle(JINGLE_WARNING);
            return;
        case SE_TEST_SOUND: Sfx_Play(SFX_CLEAR); return;
        case SE_HOWTO_NEXT:
            Settings_Save(s);
            Scr_SoundConfirm();
            Note(a, "How to play will open next time.", TONE_GOLD);
            return;
        case SE_CHANGED: break;
        default: return;
    }
    if (e.config) Settings_Save(s);
    if (e.prefs) Prefs_Save();
    if (e.motion) Comfort_SetReducedMotion(s->reducedMotion);
    if (e.pieceSnap) Comfort_SetPieceSnap(s->pieceSnap);
    if (e.patterns) Look_SetPatterns(s->blockPatterns);
    if (e.sound) {
        Sfx_SetEnabled(s->audioEnabled);
        Music_SetEnabled(s->audioEnabled);
    }
    if (e.musicVolume) Music_SetVolume(pr->musicVolume);
    if (e.effectsVolume) {
        Sfx_SetVolume(s->volumePercent);
        Music_SetVolume(pr->musicVolume);
        Sfx_PlayPitched(SFX_MOVE, 0.7f + 0.06f * (float)e.value); /* each block at its own pitch */
    } else {
        Sfx_Play(SFX_MOVE);
    }
    Note(a, "Saved.", TONE_DIM);
}

void ScrSettings_Update(App *a, const Input *in, float dt) {
    SettingsScreenState *st = &a->set;
    st->noteT += dt;
    if (st->capture.on) { UpdateCapture(a, in, dt); return; }
    if (st->dialog.open) { ResetAsked(a, in); return; }
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    static SetPage page;
    Layout(a, &page);
    KitEvent ev = Kit_Update(&st->kit, &page.list, in);
    if (ev.type == KIT_EV_PRESS && ev.id == SID_BACK) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    SetEffect e = SetPage_Apply(&a->settings, Prefs_Get(), &ev);
    if (e.kind != SE_NONE) {
        Apply(a, e);
        if (st->capture.on || st->dialog.open || e.kind == SE_TAB) return;
        Layout(a, &page);
    }
    /* Delete, Backspace or pad Y: a second key or button cleared (Rotate
     * 180 and Undo, the optional actions, clear their first too). */
    int f = st->kit.focus;
    if (Keymap_Pressed(BA_CLEAR, in) && SetPage_IsCap(f) && (SetPage_CapColumn(f) % 2 == 1 || Controls_Optional(SetPage_CapAction(f)))) {
        int act = SetPage_CapAction(f), col = SetPage_CapColumn(f);
        bool done = col < 2 ? Controls_ClearKey(&a->settings, act, col) : Controls_ClearPad(&a->settings, act, col - 2);
        if (done) {
            Settings_Save(&a->settings);
            Rebuild(a);
            Scr_SoundBack();
            char text[96];
            snprintf(text, sizeof(text), "%s's %s%s is cleared.", Controls_ActionLabel(act), col % 2 == 1 ? "second " : "", col < 2 ? "key" : "button");
            Note(a, text, TONE_DIM);
        }
    }
    if (SetPage_Navigate(&st->kit, &page, in)) Scr_SoundMove();
    if (st->tab == TAB_FEEL) {
        bool onStrip = st->kit.focus == SID_STRIP && in->hintDevice != DEV_MOUSE;
        PlayHandling h = PlayHandling_From(a->settings.repeatDelayMs, a->settings.repeatSpeedMs, a->settings.softDropSpeed);
        FeelStrip_Tick(&st->strip, onStrip && Keymap_AnyHeld(KA_UI_LEFT, in), onStrip && Keymap_AnyHeld(KA_UI_RIGHT, in), &h, dt);
    }
    Scr_Cursor(Kit_WantsHand(&st->kit, &page.list));
}

/* --------------------------------------------------------------- drawing */

/* The row the keys are on: the panel colour and the 4 px bar, gliding from
 * row to row. The mouse lights controls, not rows. */
static const SetRow *LitRow(const App *a, const SetPage *p) {
    const SettingsScreenState *st = &a->set;
    if (st->capture.on) return SetPage_RowOf(p, SID_CAP_OF(st->capture.action, st->capture.column));
    if (a->input.lastDevice == DEV_MOUSE || st->dialog.open) return NULL;
    return SetPage_RowOf(p, st->kit.focus);
}

static void DrawRowText(const SetRow *r, bool lit) {
    const UiTheme *t = Ui_Theme();
    if (!r->label[0]) return;
    int y = (int)(r->r.y + r->r.h / 2.0f) - 8;
    if (r->r.h > SET_ROW_H) y = (int)r->r.y + 8; /* a tall row (the strip): the label at its top */
    Ui_Text(r->label, SET_LABEL_X, y, UI_T16, lit ? t->light : t->text);
    if (r->shared) Ui_Text("*", SET_LABEL_X + Kit_TextWidth(r->label, 16) + 6, y, UI_T16, t->light);
}

/* A key or pad cap of the Controls table: the kit's block, and on it the
 * key's name or the pad's button; waiting for a key it pulses (steady
 * with Reduced flashing) and a bar under it drains over five seconds. */
static void DrawCap(const App *a, const KitItem *it, KitLook look) {
    const UiTheme *t = Ui_Theme();
    const Capture *c = &a->set.capture;
    bool waiting = c->on && it->id == SID_CAP_OF(c->action, c->column);
    KitItem block = *it;
    block.label[0] = '\0';
    block.flags &= ~(unsigned)KIT_SELFDRAWN;
    KitRect r = it->r;
    int cx = (int)(r.x + r.w / 2.0f), cy = (int)(r.y + r.h / 2.0f);
    if (waiting) {
        float pulse = Comfort_Calm() ? 1.0f : 0.55f + 0.45f * sinf((float)GetTime() * 9.0f);
        DrawRectangle((int)r.x, (int)r.y, (int)r.w, (int)r.h, t->panel);
        DrawRectangleLinesEx((Rectangle){r.x, r.y, r.w, r.h}, 2.0f, Fade(t->light, pulse));
        Ui_TextCentered("Press", UI_T8, cx, cy - 4, Fade(t->light, 0.6f + 0.4f * pulse));
        DrawRectangle((int)r.x + 2, (int)(r.y + r.h) - 4, (int)((r.w - 4.0f) * Capture_Left(c)), 2, t->light);
        return;
    }
    KitDraw_Item(&block, look, KIT_NONE, KIT_NONE);
    int sink = look == KIT_LOOK_PRESSED ? 2 : 0;
    bool none = strcmp(it->label, "-") == 0;
    if (!none && (it->flags & KIT_PADCAP)) {
        KitDraw_Cap(it->label, true, cx - Kit_CapWidth(it->label, true) / 2, cy + sink);
        return;
    }
    Color ink = none ? t->faint : (look == KIT_LOOK_FOCUSED ? t->light : (look == KIT_LOOK_PRESSED ? t->bg : t->text));
    char shown[KIT_LABEL_LEN];
    snprintf(shown, sizeof(shown), "%s", it->label);
    if (Kit_TextWidth(shown, 8) > (int)r.w - 12) { /* "Right Shift" in a narrow cap: its short name */
        const char *k = Controls_KeyConst(&a->settings, SetPage_CapAction(it->id), SetPage_CapColumn(it->id));
        Controls_ShortName(k, shown, sizeof(shown));
    }
    Ui_TextCentered(shown, UI_T8, cx, cy - 4 + sink, ink);
}

/* Feel's strip: a short lane with a T sliding along it as your values make
 * it, outlined while the keys are on it. */
static void DrawStrip(const App *a, const KitItem *it, KitLook look) {
    const UiTheme *t = Ui_Theme();
    const FeelStrip *s = &a->set.strip;
    int x0 = SET_CONTROL_X, y0 = (int)it->r.y + 10, w = STRIP_CELLS * STRIP_CELL, h = 2 * STRIP_CELL;
    DrawRectangle(x0 - 2, y0 - 2, w + 4, h + 4, look == KIT_LOOK_FOCUSED || look == KIT_LOOK_HOVER ? t->light : t->rule);
    DrawRectangle(x0, y0, w, h, t->board);
    for (int c = 1; c < STRIP_CELLS; c++) DrawRectangle(x0 + c * STRIP_CELL, y0, 1, h, Ui_Lerp(t->board, t->rule, 0.5f));
    Color col = Render_PieceColor(PIECE_T);
    int px = x0 + s->x * STRIP_CELL;
    Render_PieceBlock(px + STRIP_CELL, y0, STRIP_CELL, PIECE_T, col, 1.0f);
    for (int i = 0; i < 3; i++) Render_PieceBlock(px + i * STRIP_CELL, y0 + STRIP_CELL, STRIP_CELL, PIECE_T, col, 1.0f);
    char words[48];
    snprintf(words, sizeof(words), "%d ms", a->settings.repeatDelayMs);
    Ui_Text(words, x0 + w + 12, y0 + 4, UI_T8, t->dim);
    snprintf(words, sizeof(words), "%d ms", a->settings.repeatSpeedMs);
    Ui_Text(words, x0 + w + 12, y0 + 20, UI_T8, t->dim);
}

/* Look's sample beside Block patterns: the seven pieces' blocks. */
static void DrawPatternSample(const SetPage *p) {
    const KitItem *tg = Kit_Get(&p->list, SID_PATTERNS);
    if (!tg) return;
    int x = (int)(tg->r.x + tg->r.w) + 12, y = (int)(tg->r.y + (tg->r.h - 14.0f) / 2.0f);
    for (int i = 0; i < PIECE_COUNT; i++) Render_PieceBlock(x + i * 15, y, 14, (PieceType)i, Render_PieceColor((PieceType)i), 1.0f);
}

/* The Controls table's headings. */
static void DrawHeadings(void) {
    const UiTheme *t = Ui_Theme();
    static const char *const kHeads[SET_CAP_COLUMNS] = {"Key", "Second key", "Pad", "Second pad"};
    for (int c = 0; c < SET_CAP_COLUMNS; c++) {
        KitRect r = SetPage_CapRect(0, c);
        Ui_TextCentered(kHeads[c], UI_T8, (int)(r.x + r.w / 2.0f), SET_HEAD_Y, t->dim);
    }
}

/* Wrapped 8 px text, at most two lines. */
static void Lines(const char *text, int x, int y, int maxW, Color c) {
    char lines[2][SCR_WRAP_LEN];
    int n = Scr_Wrap(text, 8, maxW, lines, 2);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], x, y + i * 14, UI_T8, c);
}

static void DrawHelp(const App *a, const SetPage *p, const SetRow *lit, const KitView *kv) {
    const UiTheme *t = Ui_Theme();
    const char *text = NULL;
    if (kv->state && kv->device == DEV_MOUSE) {
        const char *why = Kit_Reason(kv->state, kv->list, kv->device);
        const SetRow *hovered = SetPage_RowOf(p, kv->state->hover);
        if (why[0]) text = why;
        else if (hovered) text = hovered->help;
    } else if (kv->state) {
        const KitItem *f = Kit_Get(kv->list, kv->state->focus);
        if (f && (f->flags & KIT_DISABLED)) text = f->reason;
        else if (lit) text = lit->help;
        else if (kv->state->focus == SID_TABS) text = "LB and RB, Tab, or Left and Right here change the tab.";
    }
    (void)a;
    if (text && text[0]) Lines(text, SET_LABEL_X, SET_HELP_Y, WINDOW_WIDTH - 2 * SET_LABEL_X, t->dim);
}

static void DrawFootnote(const char *text) {
    const UiTheme *t = Ui_Theme();
    if (!text || !text[0]) return;
    if (text[0] == '*') {
        Ui_Text("*", SET_LABEL_X, SET_FOOT_Y, UI_T8, t->light);
        Lines(text + 1, SET_LABEL_X + 18, SET_FOOT_Y, WINDOW_WIDTH - 2 * SET_LABEL_X - 18, t->dim);
    } else {
        Lines(text, SET_LABEL_X, SET_FOOT_Y, WINDOW_WIDTH - 2 * SET_LABEL_X, t->dim);
    }
}

/* The hint line names what the keys do where the focus is (it fits the
 * window: on a cap the tabs' chip gives way to clear). */
static void DrawHints(const App *a, const SetPage *p) {
    const SettingsScreenState *st = &a->set;
    InputDevice d = a->input.hintDevice;
    if (st->dialog.open) {
        KitHint h[3] = {{2, {KA_UI_LEFT, KA_UI_RIGHT}, "choose"}, {1, {KA_UI_PRESS}, "select"}, {1, {KA_BACK}, "keep mine"}};
        KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, SET_HINTS_CY, d);
        return;
    }
    if (st->capture.on) {
        const UiTheme *t = Ui_Theme();
        Ui_TextCentered(st->capture.pad ? "Press a button on the pad" : "Press a key on the keyboard", UI_T8, WINDOW_WIDTH / 2, SET_HINTS_CY - 4, t->dim);
        return;
    }
    KitHint h[6];
    int n = 0, f = st->kit.focus;
    const KitItem *it = Kit_Get(&p->list, f);
    bool cap = SetPage_IsCap(f), value = it && (it->kind == KIT_TOGGLE || it->kind == KIT_STEPPER || it->kind == KIT_SLIDER);
    h[n++] = (KitHint){2, {KA_UI_UP, KA_UI_DOWN}, "choose"};
    const char *side = SetPage_SideKeys(p, f);
    if (side) h[n++] = (KitHint){2, {KA_UI_LEFT, KA_UI_RIGHT}, side};
    if (cap) h[n++] = (KitHint){1, {KA_UI_PRESS}, "set"};
    else if (it && !value && f != SID_STRIP && f != SID_TABS) h[n++] = (KitHint){1, {KA_UI_PRESS}, "press"};
    if (cap && (SetPage_CapColumn(f) % 2 == 1 || Controls_Optional(SetPage_CapAction(f)))) h[n++] = (KitHint){1, {BA_CLEAR}, "clear"};
    else if (d == DEV_PAD) h[n++] = (KitHint){2, {KA_UI_PREV_TAB, KA_UI_NEXT_TAB}, "tabs"};
    else if (f != SID_TABS) h[n++] = (KitHint){1, {KA_UI_NEXT_TAB}, "tabs"};
    h[n++] = (KitHint){1, {KA_BACK}, "back"};
    KitDraw_Hints(h, n, WINDOW_WIDTH / 2, SET_HINTS_CY, d);
}

void ScrSettings_Draw(App *a, float dt) {
    SettingsScreenState *st = &a->set;
    const UiTheme *t = Ui_Theme();
    static SetPage page;
    Layout(a, &page);
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);
    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Settings", 40, 44);
    DrawRectangle(SET_ROW_X, SET_TABS_Y + SET_TABS_H, SET_ROW_W, 2, t->rule);
    if (page.intro) Lines(page.intro, SET_LABEL_X, SET_INTRO_Y, WINDOW_WIDTH - 2 * SET_LABEL_X, t->dim);

    const SetRow *lit = LitRow(a, &page);
    if (lit) KitDraw_Plate(Kit_GlideTo(&st->glide, lit->r, dt, Comfort_Motion()));
    else Kit_GlideReset(&st->glide);
    for (int i = 0; i < page.rowCount; i++) DrawRowText(&page.rows[i], &page.rows[i] == lit);
    if (st->tab == TAB_CONTROLS) DrawHeadings();

    /* A row with one control shows its focus on the row; a row of several
     * (the caps, the presets) also marks which of them has it. */
    const KitState *state = st->dialog.open ? NULL : &st->kit;
    KitView kv = Scr_View(a, &page.list, state, NULL);
    for (int i = 0; i < page.list.count; i++) {
        const KitItem *it = &page.list.items[i];
        KitLook look = KitDraw_LookOf(&kv, it);
        const SetRow *row = SetPage_RowOf(&page, it->id);
        if (look == KIT_LOOK_FOCUSED && row && row == lit && row->count == 1) look = KIT_LOOK_NORMAL;
        if (SetPage_IsCap(it->id)) { DrawCap(a, it, look); continue; }
        if (it->id == SID_STRIP) { DrawStrip(a, it, look); continue; }
        KitDraw_Item(it, look, Kit_HoverCell(state, it, kv.device), Kit_PressedCell(state, it));
    }
    if (st->tab == TAB_LOOK) DrawPatternSample(&page);
    if (st->tab == TAB_FEEL && SetPage_PresetOf(&a->settings) < 0) {
        /* none of the three: "Custom" after the row's label */
        const SetRow *presets = SetPage_RowOf(&page, SID_PRESET);
        if (presets)
            Ui_Text("Custom", SET_LABEL_X + Kit_TextWidth(presets->label, 16) + 14, (int)(presets->r.y + presets->r.h / 2.0f) - 4, UI_T8, t->gold);
    }

    if (st->note[0] && (st->capture.on || st->noteT < NOTE_SECONDS)) {
        Color c = st->noteTone == TONE_GOLD ? t->gold : (st->noteTone == TONE_LIGHT ? t->light : t->dim);
        Lines(st->note, SET_LABEL_X, SET_NOTE_Y, WINDOW_WIDTH - 2 * SET_LABEL_X, c);
    }
    DrawHelp(a, &page, lit, &kv);
    DrawFootnote(page.footnote);
    if (st->dialog.open) {
        KitList dl;
        Kit_DialogLayout(&st->dialog, &dl);
        KitView dv = {&dl, &st->dlgKit, Kit_DialogDevice(&st->dlgKit, &dl, a->input.lastDevice), a->input.hintDevice, NULL};
        KitDraw_Dialog(&st->dialog, &dv);
    }
    DrawHints(a, &page);
    Scr_EndSlide();
    Scr_EndFrame(a);
}

void ScrSettings_Layout(const App *a, KitList *l) {
    static SetPage page;
    Layout(a, &page);
    *l = page.list;
}
