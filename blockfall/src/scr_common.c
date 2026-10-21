#include "screens.h"
#include "blockdraw.h"
#include "comfort.h"
#include "rlgl.h"
#include "sfx.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

void Scr_SoundMove(void) { Sfx_PlayVaried(SFX_MOVE, 0.06f); }
void Scr_SoundConfirm(void) { Sfx_Jingle(JINGLE_CONFIRM); }
void Scr_SoundBack(void) { Sfx_Jingle(JINGLE_BACK); }

void Scr_BeginSlide(const App *a) {
    rlPushMatrix();
    rlTranslatef((float)App_SlideOffset(a), 0.0f, 0.0f);
}

void Scr_EndSlide(void) { rlPopMatrix(); }

void Scr_Title(const char *title, int x, int y) {
    const UiTheme *t = Ui_Theme();
    Ui_Text(title, x, y, UI_T32, t->text);
    DrawRectangle(x, y + 44, Kit_TextWidth(title, UI_T32), 4, t->light);
}

void Scr_PlayingAs(const char *username, int rightX, int y) {
    const UiTheme *t = Ui_Theme();
    char name[32];
    snprintf(name, sizeof(name), "%.24s", username);
    int nw = Kit_TextWidth(name, UI_T8);
    Ui_Text(name, rightX - nw, y, UI_T8, t->gold);
    Ui_TextRight("Playing as", UI_T8, rightX - nw - 9, y, t->dim);
}

KitItem *Scr_AddBack(KitList *l, int id) {
    KitItem *it = Kit_Add(l, id, KIT_BUTTON, SCR_BACK_RECT, "Back");
    if (!it) return NULL;
    it->style = KIT_STYLE_SMALL;
    it->textSize = 8;
    it->icon = KIT_ICON_BACK;
    it->flags |= KIT_NOFOCUS; /* Esc and pad B are the keys' way back */
    return it;
}

KitView Scr_View(const App *a, const KitList *l, const KitState *s, const KitRect *plate) {
    return (KitView){l, s, a->input.lastDevice, a->input.hintDevice, plate};
}

void Scr_Cursor(bool hand) {
    static int sNow = -1;
    int want = hand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT;
    if (want == sNow) return;
    sNow = want;
    SetMouseCursor(want);
}

int Scr_Wrap(const char *text, int size, int maxW, char lines[][SCR_WRAP_LEN], int maxLines) {
    int n = 0;
    char cur[SCR_WRAP_LEN] = "";
    const char *p = text ? text : "";
    while (*p && n < maxLines) {
        while (*p == ' ') p++;
        const char *w0 = p;
        while (*p && *p != ' ') p++;
        int wl = (int)(p - w0);
        if (wl <= 0) break;
        char trial[2 * SCR_WRAP_LEN];
        snprintf(trial, sizeof(trial), "%s%s%.*s", cur, cur[0] ? " " : "", wl, w0);
        if (!cur[0] || Kit_TextWidth(trial, size) <= maxW) {
            snprintf(cur, sizeof(cur), "%.*s", SCR_WRAP_LEN - 1, trial);
        } else {
            snprintf(lines[n++], SCR_WRAP_LEN, "%s", cur);
            snprintf(cur, sizeof(cur), "%.*s", wl < SCR_WRAP_LEN - 1 ? wl : SCR_WRAP_LEN - 1, w0);
        }
    }
    if (cur[0] && n < maxLines) snprintf(lines[n++], SCR_WRAP_LEN, "%s", cur);
    return n;
}

bool Scr_Kit(App *a, KitList *l, const KitState **state) {
    switch (App_Top(a)) {
        case SCR_MENU: ScrMenu_Layout(a, l); *state = &a->menu.kit; return true;
        case SCR_SETUP: ScrSetup_Layout(a, l); *state = &a->setup.kit; return true;
        case SCR_UPDATES: ScrUpdates_Layout(a, l); *state = &a->updates.kit; return true;
        case SCR_SCORES: ScrScores_Layout(a, l); *state = &a->scores.kit; return true;
        case SCR_SETTINGS:
            if (a->set.dialog.open) {
                Kit_DialogLayout(&a->set.dialog, l);
                *state = &a->set.dlgKit;
                return true;
            }
            ScrSettings_Layout(a, l);
            *state = &a->set.kit;
            return true;
        case SCR_HOWTO: ScrHowTo_Layout(a, l); *state = &a->howto.kit; return true;
        case SCR_STATS: ScrStats_Layout(a, l); *state = &a->stats.kit; return true;
        case SCR_PRACTICE: ScrPractice_Layout(a, l); *state = &a->practice.kit; return true;
        case SCR_DAILY: ScrDaily_Layout(a, l); *state = &a->daily.kit; return true;
        case SCR_PLAY: return ScrPlay_Kit(a, l, state);
        default: Kit_Clear(l, 0); *state = NULL; return false;
    }
}

/* ----------------------------------------------------------- the wipe */

/* The wall in the game's block language: each block a muted piece colour,
 * the same pattern every time, lit as its row clears (white, or a tint of
 * the game's light with Reduced flashing). */
void Wipe_Draw(const Wipe *w) {
    if (!Wipe_Active(w)) return;
    const UiTheme *t = Ui_Theme();
    bool calm = Comfort_Calm();
    for (int r = 0; r < WIPE_ROWS; r++) {
        for (int c = 0; c < WIPE_COLS; c++) {
            WipeCell cell = Wipe_Cell(w, r, c);
            if (cell == WIPE_GONE) continue;
            int x = WIPE_X0 + c * WIPE_BLOCK, y = r * WIPE_BLOCK;
            Color base = Ui_Lerp(Render_PieceColor((PieceType)((r * 3 + c * 5) % PIECE_COUNT)), t->panel, 0.55f);
            DrawRectangle(x, y, WIPE_BLOCK, WIPE_BLOCK, t->bg);
            Render_BlockAt(x, y, WIPE_BLOCK, base, 1.0f);
            if (cell == WIPE_FLASH)
                DrawRectangle(x + 1, y + 1, WIPE_BLOCK - 2, WIPE_BLOCK - 2, calm ? Fade(t->light, 0.40f) : Fade((Color){248, 246, 255, 255}, 0.6f));
        }
    }
}

void Scr_EndFrame(const App *a) {
    Wipe_Draw(&a->wipe);
    Win_EndFrame();
}
