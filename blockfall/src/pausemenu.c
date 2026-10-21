#include "pausemenu.h"
#include "layout.h"
#include "menumodel.h"
#include "runrecord.h"
#include "uikeymap.h"
#include <stdio.h>
#include <string.h>

static const char *const kItems[PAUSE_ITEMS] = {"Resume", "Restart", "Settings", "How to play", "Quit to menu"};

void PauseMenu_Open(PauseMenu *m, bool focusLost) {
    memset(m, 0, sizeof(*m));
    m->open = true;
    m->focusLost = focusLost;
    m->page = PAUSE_MAIN;
    Kit_Enter(&m->kit, PI_RESUME);
    Kit_GlideReset(&m->glide);
    Kit_Init(&m->dlgKit);
}

void PauseMenu_Close(PauseMenu *m) {
    m->open = false;
    m->dialog.open = false;
}

void PauseMenu_Tick(PauseMenu *m, float dt) {
    if (m->open && m->t < 10.0f) m->t += dt;
}

void PauseMenu_Ask(PauseMenu *m, PauseAsk what, const char *title, const char *body0, const char *body1, bool fromPlay) {
    if (!m->open) PauseMenu_Open(m, false);
    char act[24];
    snprintf(act, sizeof(act), "%s", what == PAUSE_ASK_QUIT ? "Leave" : "Restart");
    Kit_DialogOpen(&m->dialog, &m->dlgKit, (int)what, 0, title, body0, body1, "Keep playing", act);
    m->askFromPlay = fromPlay;
}

bool PauseMenu_Asking(const PauseMenu *m) { return m->open && m->dialog.open; }

/* ------------------------------------------------------------- geometry */

KitRect PauseMenu_Card(const PauseMenu *m) {
    float h = (float)PAUSE_CARD_H + (m->focusLost && m->page == PAUSE_MAIN ? (float)PAUSE_NOTE_H : 0.0f);
    return (KitRect){(float)(LAYOUT_WINDOW_W - PAUSE_CARD_W) / 2.0f, ((float)LAYOUT_WINDOW_H - h) / 2.0f, (float)PAUSE_CARD_W, h};
}

static float Ease(float k) { return 1.0f - (1.0f - k) * (1.0f - k); }

static float InK(const PauseMenu *m, bool motion) {
    if (!motion) return 1.0f;
    float k = m->t / PAUSE_IN_SECONDS;
    return k >= 1.0f ? 1.0f : (k <= 0.0f ? 0.0f : Ease(k));
}

int PauseMenu_SlideOffset(const PauseMenu *m, bool motion) {
    return (int)((float)PAUSE_SLIDE_PX * (1.0f - InK(m, motion)) + 0.5f);
}

float PauseMenu_Dim(const PauseMenu *m, bool motion) { return InK(m, motion); }

static KitRect ButtonRect(const PauseMenu *m, int i) {
    KitRect c = PauseMenu_Card(m);
    float top = c.y + 80.0f + (m->focusLost ? (float)PAUSE_NOTE_H : 0.0f);
    return (KitRect){c.x + (float)(PAUSE_CARD_W - PAUSE_BUTTON_W) / 2.0f, top + (float)(i * PAUSE_PITCH), PAUSE_BUTTON_W, PAUSE_BUTTON_H};
}

KitRect PauseMenu_RowRect(const PauseMenu *m, int id) {
    KitRect c = PauseMenu_Card(m);
    int i = id - PQ_EFFECTS;
    if (i < 0) i = 0;
    if (i >= PAUSE_QUICK_ROWS) i = PAUSE_QUICK_ROWS - 1;
    return (KitRect){c.x + 12.0f, c.y + 60.0f + (float)(i * PAUSE_ROW_PITCH), (float)PAUSE_CARD_W - 24.0f, PAUSE_ROW_H};
}

const char *PauseMenu_RowLabel(int id) {
    switch (id) {
        case PQ_EFFECTS: return "Effects volume";
        case PQ_MUSIC: return "Music volume";
        case PQ_SOUND: return "Sound";
        case PQ_GHOST: return "Ghost piece";
        case PQ_SHAKE: return "Screen shake";
        default: return "";
    }
}

void PauseMenu_Layout(const PauseMenu *m, const QuickValues *q, KitList *l) {
    Kit_Clear(l, KIT_NAV_Y | KIT_WRAP_Y);
    if (m->page == PAUSE_MAIN) {
        for (int i = 0; i < PAUSE_ITEMS; i++) Kit_Add(l, PI_RESUME + i, KIT_BUTTON, ButtonRect(m, i), kItems[i]);
        return;
    }
    KitRect c = PauseMenu_Card(m);
    for (int id = PQ_EFFECTS; id <= PQ_SHAKE; id++) {
        KitRect row = PauseMenu_RowRect(m, id);
        bool slider = id == PQ_EFFECTS || id == PQ_MUSIC;
        float w = slider ? (float)PAUSE_SLIDER_W : (float)PAUSE_TOGGLE_W, h = slider ? row.h : row.h - 4.0f;
        KitRect r = {row.x + row.w - 8.0f - w, row.y + (row.h - h) / 2.0f, w, h};
        KitItem *it = Kit_Add(l, id, slider ? KIT_SLIDER : KIT_TOGGLE, r, PauseMenu_RowLabel(id));
        if (!it) continue;
        it->textSize = 8;
        if (slider) {
            it->max = 10;
            it->value = id == PQ_EFFECTS ? q->effects : q->music;
        } else {
            it->value = id == PQ_SOUND ? q->sound : (id == PQ_GHOST ? q->ghost : q->shake);
        }
    }
    KitRect done = {c.x + (float)(PAUSE_CARD_W - PAUSE_BUTTON_W) / 2.0f, c.y + 60.0f + (float)(PAUSE_QUICK_ROWS * PAUSE_ROW_PITCH) + 12.0f,
                    PAUSE_BUTTON_W, PAUSE_BUTTON_H};
    Kit_Add(l, PQ_DONE, KIT_BUTTON, done, "Done");
}

/* --------------------------------------------------------------- input */

static void ToPage(PauseMenu *m, PausePage page, int focus) {
    m->page = page;
    Kit_Enter(&m->kit, focus);
    Kit_GlideReset(&m->glide);
}

static bool ArrowPressed(const Input *in, bool horizontal) {
    if (horizontal) return Keymap_Pressed(KA_UI_LEFT, in) || Keymap_Pressed(KA_UI_RIGHT, in);
    return Keymap_Pressed(KA_UI_UP, in) || Keymap_Pressed(KA_UI_DOWN, in);
}

static PauseAction AnswerBox(PauseMenu *m, const RunKeys *k, const Input *in) {
    PauseAction a = {PA_NONE, 0, 0, false, false, false};
    KitList l;
    Kit_DialogLayout(&m->dialog, &l);
    int before = m->dlgKit.focus;
    KitEvent ev = Kit_Update(&m->dlgKit, &l, in);
    if (m->dlgKit.focus != before && ArrowPressed(in, true)) a.moved = true;
    int kind = m->dialog.kind;
    KitDialogResult r = Kit_DialogAnswer(&m->dialog, &ev, in);
    if (r == KIT_DLG_OPEN && k->back) { /* your own Back key keeps playing too */
        m->dialog.open = false;
        r = KIT_DLG_SAFE;
    }
    if (r == KIT_DLG_ACT) {
        a.kind = kind == PAUSE_ASK_QUIT ? PA_DO_QUIT : PA_DO_RESTART;
    } else if (r == KIT_DLG_SAFE) {
        a.closed = true;
        if (m->askFromPlay) a.kind = PA_RESUME; /* asked from play: straight back to it */
        m->askFromPlay = false;
    }
    return a;
}

PauseAction PauseMenu_Update(PauseMenu *m, const QuickValues *q, const RunKeys *k, const Input *in) {
    PauseAction a = {PA_NONE, 0, 0, false, false, false};
    if (!m->open) return a;
    if (m->dialog.open) return AnswerBox(m, k, in);
    if (k->pause) { a.kind = PA_RESUME; return a; }
    /* Esc and pad B are Back in a menu, even when a swap gave Esc to Restart
     * in play (Back is checked first). */
    if (Keymap_Pressed(KA_BACK, in) || k->back) {
        if (m->page == PAUSE_QUICK) {
            ToPage(m, PAUSE_MAIN, PI_SETTINGS);
            a.closed = true;
        } else {
            a.kind = PA_RESUME;
        }
        return a;
    }
    if (k->restart) { a.kind = PA_RESTART; return a; }
    KitList l;
    PauseMenu_Layout(m, q, &l);
    int before = m->kit.focus;
    KitEvent ev = Kit_Update(&m->kit, &l, in);
    if (m->kit.focus != before && ArrowPressed(in, false)) a.moved = true;
    if (ev.type == KIT_EV_PRESS) {
        switch (ev.id) {
            case PI_RESUME: a.kind = PA_RESUME; break;
            case PI_RESTART: a.kind = PA_RESTART; break;
            case PI_QUIT: a.kind = PA_QUIT; break;
            case PI_HOWTO: a.kind = PA_HOWTO; break;
            case PI_SETTINGS: ToPage(m, PAUSE_QUICK, PQ_EFFECTS); a.opened = true; break;
            case PQ_DONE: ToPage(m, PAUSE_MAIN, PI_SETTINGS); a.closed = true; break;
            default: break;
        }
    } else if (ev.type == KIT_EV_CHANGE) {
        a.kind = PA_SET;
        a.id = ev.id;
        a.value = ev.value;
    }
    return a;
}

/* ---------------------------------------------------------------- words */

/* A practice run (3.5.0) has nothing to lose: it never asks. */
bool PauseMenu_ShouldAsk(const Game *g) { return g->score > 0 && !g->practice.on; }

void PauseMenu_AskWords(PauseAsk what, const Game *g, bool tuned, const char *board, char *title, size_t titleSize,
                        char *body, size_t bodySize, char *act, size_t actSize) {
    snprintf(title, titleSize, "%s", what == PAUSE_ASK_QUIT ? "Leave this run?" : "Restart this run?");
    snprintf(act, actSize, "%s", what == PAUSE_ASK_QUIT ? "Leave" : "Restart");
    RunRecord r = Record_Classify(g, tuned);
    char points[24];
    Menu_Group(g->score, points, sizeof(points));
    if (g->practice.on) snprintf(body, bodySize, "Practice: nothing is recorded.");
    else if (tuned) snprintf(body, bodySize, "A tuned run is never recorded.");
    else if (g->cfg.mode == MODE_ZEN) snprintf(body, bodySize, "Zen keeps no scores. It counts as a run played.");
    else if (g->cfg.mode == MODE_SPRINT && !g->won) snprintf(body, bodySize, "A Sprint left before %d lines is not ranked.", SPRINT_LINES);
    else if (r.toBoard && g->daily.on) snprintf(body, bodySize, "It counts for %s on today's daily board.", points);
    else if (r.toBoard) snprintf(body, bodySize, "It counts for %s on %s.", points, board && board[0] ? board : "this board");
    else snprintf(body, bodySize, "It counts for %s.", points);
}
