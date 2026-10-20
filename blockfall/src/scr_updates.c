/* Updates, on the button kit: what changed in each version. Up/Down (held,
 * they repeat) and the wheel scroll, LB/RB or PgUp/PgDn a page, Home/End to
 * the ends; Esc, pad B or [<- Back] go back. */
#include "screens.h"
#include "changelog.h"
#include "comfort.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>

#define VIEW_X 40
#define VIEW_Y 116
#define VIEW_W (WINDOW_WIDTH - 2 * VIEW_X)
#define VIEW_H 524
#define HEADER_H 32
#define NOTE_H 16
#define NOTE_GAP 8
#define ENTRY_GAP 24
#define TEXT_X (VIEW_X + 20)
#define TEXT_W (VIEW_X + VIEW_W - TEXT_X)
#define STEP_PX 48
#define HINTS_CY 676

enum { UI_BACK = 1 };

static int ContentHeight(void) {
    char lines[8][SCR_WRAP_LEN];
    int h = 0;
    for (int e = 0; e < CHANGELOG_COUNT; e++) {
        h += HEADER_H;
        for (int n = 0; kChangelog[e].notes[n]; n++) h += Scr_Wrap(kChangelog[e].notes[n], UI_T8, TEXT_W, lines, 8) * NOTE_H + NOTE_GAP;
        h += ENTRY_GAP;
    }
    return h;
}

static float MaxScroll(const UpdatesState *u) { return u->contentH > VIEW_H ? (float)(u->contentH - VIEW_H) : 0.0f; }

void ScrUpdates_Enter(App *a, bool back) {
    UpdatesState *u = &a->updates;
    Kit_Enter(&u->kit, KIT_NONE);
    u->contentH = ContentHeight();
    if (!back) {
        u->scroll = 0.0f;
        u->wheel = 0.0f;
    }
}

void ScrUpdates_Layout(const App *a, KitList *l) {
    (void)a;
    Kit_Clear(l, 0);
    Scr_AddBack(l, UI_BACK);
}

void ScrUpdates_Update(App *a, const Input *in, float dt) {
    (void)dt;
    UpdatesState *u = &a->updates;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    KitList l;
    ScrUpdates_Layout(a, &l);
    KitEvent ev = Kit_Update(&u->kit, &l, in);
    if (ev.type == KIT_EV_PRESS && ev.id == UI_BACK) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    float before = u->scroll, page = (float)(VIEW_H - STEP_PX);
    if (Keymap_Pressed(KA_SCROLL_UP, in)) u->scroll -= STEP_PX;
    if (Keymap_Pressed(KA_SCROLL_DOWN, in)) u->scroll += STEP_PX;
    if (Keymap_Pressed(KA_SCROLL_PAGE_UP, in)) u->scroll -= page;
    if (Keymap_Pressed(KA_SCROLL_PAGE_DOWN, in)) u->scroll += page;
    if (Keymap_Pressed(KA_SCROLL_TOP, in)) u->scroll = 0.0f;
    if (Keymap_Pressed(KA_SCROLL_END, in)) u->scroll = MaxScroll(u);
    u->scroll -= in->wheel * STEP_PX; /* away from you: up the page */
    if (u->scroll < 0.0f) u->scroll = 0.0f;
    if (u->scroll > MaxScroll(u)) u->scroll = MaxScroll(u);
    if (u->scroll != before && in->wheel == 0.0f) Scr_SoundMove();
    Scr_Cursor(Kit_WantsHand(&u->kit, &l));
}

static void DrawScrollBar(const UpdatesState *u) {
    const UiTheme *t = Ui_Theme();
    if (u->contentH <= VIEW_H) return;
    int x = VIEW_X + VIEW_W + 10, h = VIEW_H;
    DrawRectangle(x, VIEW_Y, 4, h, t->panel);
    int thumb = (int)((float)h * (float)VIEW_H / (float)u->contentH);
    if (thumb < 16) thumb = 16;
    int y = VIEW_Y + (int)((float)(h - thumb) * u->scroll / MaxScroll(u));
    DrawRectangle(x, y, 4, thumb, t->light);
}

void ScrUpdates_Draw(App *a, float dt) {
    (void)dt;
    UpdatesState *u = &a->updates;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);
    int ox = App_SlideOffset(a);
    KitList l;
    ScrUpdates_Layout(a, &l);
    KitView kv = Scr_View(a, &l, &u->kit, NULL);
    KitDraw_List(&kv);
    Scr_Title("Updates", 40, 48);

    BeginScissorMode(VIEW_X + ox, VIEW_Y, VIEW_W, VIEW_H); /* the scissor does not move with the slide on its own */
    int y = VIEW_Y - (int)lroundf(u->scroll);
    char lines[8][SCR_WRAP_LEN];
    for (int e = 0; e < CHANGELOG_COUNT; e++) {
        char header[64];
        snprintf(header, sizeof(header), "Version %s", kChangelog[e].version);
        Ui_Text(header, VIEW_X, y, UI_T16, t->light);
        Ui_TextRight(kChangelog[e].date, UI_T8, VIEW_X + VIEW_W, y + 4, t->dim);
        y += HEADER_H;
        for (int n = 0; kChangelog[e].notes[n]; n++) {
            int count = Scr_Wrap(kChangelog[e].notes[n], UI_T8, TEXT_W, lines, 8);
            DrawRectangle(VIEW_X + 4, y + 2, 4, 4, t->light);
            for (int i = 0; i < count; i++) {
                Ui_Text(lines[i], TEXT_X, y, UI_T8, t->text);
                y += NOTE_H;
            }
            y += NOTE_GAP;
        }
        y += ENTRY_GAP;
    }
    EndScissorMode();
    /* Soft edges, so text scrolled past does not end in a hard cut: only
     * where there is text past the edge (at the top of the list the first
     * version's header shows whole, not washed out). */
    Color clear = t->bg;
    clear.a = 0;
    if (u->scroll > 0.0f) DrawRectangleGradientV(VIEW_X, VIEW_Y, VIEW_W, 12, t->bg, clear);
    if (u->scroll < MaxScroll(u)) DrawRectangleGradientV(VIEW_X, VIEW_Y + VIEW_H - 12, VIEW_W, 12, clear, t->bg);
    DrawScrollBar(u);

    KitHint h[3] = {
        {2, {KA_SCROLL_UP, KA_SCROLL_DOWN}, "scroll"},
        {2, {KA_SCROLL_PAGE_UP, KA_SCROLL_PAGE_DOWN}, "page"},
        {1, {KA_BACK}, "back"},
    };
    KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
    Scr_EndSlide();
    Scr_EndFrame(a);
}
