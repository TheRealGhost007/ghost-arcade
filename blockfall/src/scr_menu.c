/* The main menu: a living title screen. The letters of BLOCKFALL drop in on
 * launch, faint pieces land on a skyline along the bottom, the eight items
 * (Stats since 3.3.0, Daily challenge since 3.6.0, each above Quit) are
 * buttons with a gliding focus bar, Start game carries your last setup, the
 * Daily item says today's twist, your best today and the streak, and "Your
 * bests" names the board you last set up. Esc or pad B first moves the bar
 * to Quit; a second press quits. */
#include "screens.h"
#include "achievements.h"
#include "blockdraw.h"
#include "comfort.h"
#include "ghostlink.h"
#include "layout.h"
#include "sfx.h"
#include "ui.h"
#include "version.h"
#include <stdio.h>
#include <string.h>

#define ROW_X 140
#define ROW_W 340
/* 3.6.0: eight rows. The title and tagline moved up 16 px, the two rows
 * with a second line (Start game, Daily challenge) are 48 tall and the
 * rest 36 at a 38 px pitch, so Quit ends at 508 and Your bests still stays
 * above the skyline (y 568 at its tallest, MENU_ROWS_BOTTOM and
 * MENU_SKYLINE_TOP in layout.h). Until 3.3.0 the rows were 40
 * tall at 44; until 3.6.0 at 42. */
#define START_Y 176
#define START_H 48     /* a row with a second line: Start game, Daily challenge */
#define ROWS_Y 232     /* How to play, the first plain row */
#define ROW_H 36
#define ROW_PITCH 38
#define TITLE_X 70     /* (620 - 480) / 2: BLOCKFALL at 48 px is 480 wide */
#define TITLE_Y 80
#define TITLE_ADVANCE 54
#define TAGLINE_Y 148
/* Your bests sits under Quit (layout.h's MENU_BESTS_*: four lines from 516 to 568) */
#define HINTS_CY 676
#define TOAST_IN 0.25f
#define TOAST_HOLD 4.0f
#define PLAYING_AS_RIGHT (WINDOW_WIDTH - 22) /* "Playing as NAME" ends here */

static const char *const kItems[MI_COUNT] = {"Start game", "How to play", "Scores", "Settings", "Updates", "Stats", "Daily challenge", "Quit"};
#define BEST_MODES MENU_BESTS_MODES
static const GameMode kBestModes[BEST_MODES] = {MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_POWER, MODE_DIG};

static KitRect ItemRect(int i) {
    if (i == MI_START) return (KitRect){ROW_X, START_Y, ROW_W, START_H};
    if (i == MI_DAILY) return (KitRect){ROW_X, (float)(ROWS_Y + (MI_DAILY - 1) * ROW_PITCH), ROW_W, START_H};
    if (i == MI_QUIT) return (KitRect){ROW_X, (float)(ROWS_Y + (MI_DAILY - 1) * ROW_PITCH + START_H + 2), ROW_W, ROW_H};
    return (KitRect){ROW_X, (float)(ROWS_Y + (i - 1) * ROW_PITCH), ROW_W, ROW_H};
}

void ScrMenu_Layout(const App *a, KitList *l) {
    Kit_Clear(l, KIT_NAV_Y | KIT_WRAP_Y);
    for (int i = 0; i < MI_COUNT; i++) {
        KitItem *it = Kit_Add(l, i, KIT_ROW, ItemRect(i), kItems[i]);
        if (it && i == MI_START) Setup_Summary(&a->settings, it->sub, sizeof(it->sub));
        if (it && i == MI_DAILY) snprintf(it->sub, sizeof(it->sub), "%s", a->menu.dailySub);
    }
}

/* The Daily item's second line (3.6.0): today's twist, your best today on
 * this machine and the streak (challenge.h), read as the menu opens. */
static void LoadDaily(App *a) {
    MenuState *m = &a->menu;
    int date;
    uint64_t seed;
    DailyTwist twist;
    ScrDaily_Today(&date, &seed, &twist);
    char key[GHOSTLINK_MODE_LEN];
    Daily_BoardKey(date, key, sizeof(key));
    long best = Record_LoadYourBestKey(key, a->username, NULL);
    static DailyLog log; /* a few kilobytes: off the stack */
    DailyLog_Load(&log);
    Daily_MenuWords(twist, best, DailyLog_Streak(&log, date), m->dailySub, sizeof(m->dailySub));
}

static void LoadBests(App *a) {
    MenuState *m = &a->menu;
    int w, h;
    Setup_Size(&a->settings, &w, &h);
    Setup_BoardLabel(&a->settings, m->bestsBoard, sizeof(m->bestsBoard));
    for (int i = 0; i < BEST_MODES; i++) {
        long best = Record_LoadYourBest(kBestModes[i], w, h, a->username, a->save.highScore, NULL);
        Menu_BestText(Record_UnitOf(kBestModes[i]), best, "no run yet", m->bests[i], sizeof(m->bests[i]));
    }
    m->bestsLoaded = true;
}

void ScrMenu_Enter(App *a, bool back) {
    MenuState *m = &a->menu;
    Kit_Enter(&m->kit, m->shown ? m->kit.focus : MI_START);
    Kit_GlideReset(&m->glide);
    if (!m->shown) {
        m->shown = true;
        Sky_Init(&m->sky, 0x426c6f636bULL); /* the same skyline every launch: screenshots repeat */
        m->dropping = Comfort_Motion();     /* a run never comes first, so a run's end never drops it */
        m->dropT = 0.0f;
        m->thuds = 0;
    }
    (void)back;
    LoadBests(a); /* after a run, after Play setup: never per frame */
    LoadDaily(a);
}

static void Activate(App *a, int id) {
    switch (id) {
        case MI_START:
            /* Re-read the profile each run so a name change made in Ghost
             * Launcher applies without restarting the game. */
            GhostLink_GetUsername("Blockfall", a->username, sizeof(a->username));
            Scr_SoundConfirm();
            App_Open(a, SCR_SETUP);
            break;
        case MI_HOWTO: Scr_SoundConfirm(); ScrHowTo_Open(a); break;
        case MI_SCORES:
            Scr_SoundConfirm();
            ScrScores_OpenFromMenu(a); /* on the board you play */
            break;
        case MI_SETTINGS: Scr_SoundConfirm(); App_Open(a, SCR_SETTINGS); break;
        case MI_UPDATES: Scr_SoundConfirm(); App_Open(a, SCR_UPDATES); break;
        case MI_STATS: Scr_SoundConfirm(); App_Open(a, SCR_STATS); break;
        case MI_DAILY: Scr_SoundConfirm(); App_Open(a, SCR_DAILY); break;
        case MI_QUIT: a->quit = true; break;
        default: break;
    }
}

/* A key, a pad button, a click or the wheel: what skips the title drop. The
 * pointer only drifting does not (a window that opens under the pointer, or
 * a hand still on the mouse from the launcher, would cut it every launch). */
static bool AnyPress(const Input *in) {
    if (in->modPressed || in->wheel != 0.0f) return true;
    for (int i = 0; i < IK_COUNT; i++) if (in->key[i]) return true;
    for (int i = 0; i < PB_COUNT; i++) if (in->pad[i]) return true;
    for (int i = 0; i < MB_COUNT; i++) if (in->mousePressed[i]) return true;
    return false;
}

static void StepTitle(MenuState *m, const Input *in, float dt) {
    if (!m->dropping) return;
    if (AnyPress(in)) { m->dropping = false; return; } /* any key skips it, and acts too */
    m->dropT += dt;
    while (m->thuds < TITLE_LETTERS && Title_LetterLanded(m->thuds, m->dropT)) {
        Sfx_PlayPitched(SFX_HARD_DROP, 1.45f + 0.03f * (float)m->thuds); /* a small thud each */
        m->thuds++;
    }
    if (m->dropT >= Title_DropSeconds()) m->dropping = false;
}

static void StepToast(MenuState *m, float dt) {
    if (!m->toastOn && Ach_PopToast(m->toast, sizeof(m->toast))) {
        m->toastOn = true;
        m->toastAge = 0.0f;
    }
    if (!m->toastOn) return;
    m->toastAge += dt;
    if (m->toastAge >= TOAST_HOLD + TOAST_IN) m->toastOn = false;
}

void ScrMenu_Update(App *a, const Input *in, float dt) {
    MenuState *m = &a->menu;
    StepTitle(m, in, dt);
    Sky_Step(&m->sky, dt);
    StepToast(m, dt);

    KitList l;
    ScrMenu_Layout(a, &l);
    if (Keymap_Pressed(KA_BACK, in)) {
        if (Menu_OnBack(m->kit.focus == MI_QUIT) == MENU_BACK_QUIT) {
            a->quit = true;
            return;
        }
        m->kit.focus = MI_QUIT; /* the first press only moves the bar to Quit */
        m->kit.keyDown = KIT_NONE;
        Scr_SoundMove();
        return;
    }
    int before = m->kit.focus;
    KitEvent ev = Kit_Update(&m->kit, &l, in);
    bool byKeys = Keymap_Pressed(KA_UI_UP, in) || Keymap_Pressed(KA_UI_DOWN, in);
    if (m->kit.focus != before && byKeys) Scr_SoundMove(); /* the pointer moves the bar silently */
    if (ev.type == KIT_EV_PRESS) Activate(a, ev.id);
    Scr_Cursor(Kit_WantsHand(&m->kit, &l));
}

/* --------------------------------------------------------------- drawing */

static void DrawSky(const Skyline *s) {
    const UiTheme *t = Ui_Theme();
    bool calm = Comfort_Calm();
    float fade = 1.0f - Sky_SweepProgress(s);
    for (int r = 0; r < SKY_GRID_ROWS; r++) {
        for (int c = 0; c < SKY_COLS; c++) {
            int v = s->cells[r][c];
            if (v < 0) continue;
            Color col = Render_PieceColor((PieceType)v);
            float alpha = 0.20f * fade;
            if (!calm && s->blink[r][c] > 0.0f) alpha += 0.30f * s->blink[r][c] / SKY_LOCK_BLINK; /* it glows as it lands */
            Render_BlockAt(Sky_CellX(c), Sky_CellY(r), SKY_CELL, col, alpha);
            if (s->clearing[r]) {
                /* A full row flashes before it goes: white, or with Reduced
                 * flashing a steady tint of its own colour. */
                if (calm) Render_BlockAt(Sky_CellX(c), Sky_CellY(r), SKY_CELL, Ui_Lerp(col, t->text, 0.35f), 0.45f);
                else DrawRectangle(Sky_CellX(c), Sky_CellY(r), SKY_CELL, SKY_CELL, Fade(WHITE, 0.45f * s->clearT / SKY_CLEAR_FLASH));
            }
        }
    }
    for (int i = 0; i < SKY_PIECES; i++) {
        const SkyPiece *p = &s->p[i];
        if (p->y < -4.0f * SKY_CELL) continue;
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(p->type, p->rot, cells);
        Color col = Render_PieceColor(p->type);
        for (int k = 0; k < CELLS_PER_PIECE; k++)
            Render_BlockAt(Sky_CellX(p->col + cells[k].x), (int)p->y + cells[k].y * SKY_CELL, SKY_CELL, col, 0.16f);
    }
}

static void DrawTitle(const MenuState *m) {
    const UiTheme *t = Ui_Theme();
    static const char kTitle[] = "BLOCKFALL";
    for (int i = 0; i < TITLE_LETTERS; i++) {
        int lift = m->dropping ? Title_LetterLift(i, m->dropT) : 0;
        char letter[2] = {kTitle[i], '\0'};
        Ui_Text(letter, TITLE_X + i * TITLE_ADVANCE, TITLE_Y - lift, UI_T48, t->light);
    }
}

static void DrawBests(const MenuState *m) {
    const UiTheme *t = Ui_Theme();
    static const char *const kNames[BEST_MODES] = {"Marathon", "Sprint", "Ultra", "Power", "Dig"};
    char head[64];
    snprintf(head, sizeof(head), "Your bests on %s", m->bestsBoard);
    /* Two to a line on three lines since 3.6.1 (layout.h's Layout_MenuBest),
     * so a label never runs into its value, "no run yet" included; still
     * above the skyline. */
    Ui_Text(head, MENU_BESTS_X, MENU_BESTS_Y, UI_T8, t->dim);
    for (int i = 0; i < BEST_MODES; i++) {
        int x, right, y;
        Layout_MenuBest(i, &x, &right, &y);
        bool none = strcmp(m->bests[i], "no run yet") == 0;
        Ui_Text(kNames[i], x, y, UI_T8, t->dim);
        Ui_TextRight(m->bests[i], UI_T8, right, y, none ? t->faint : t->text);
    }
}

static void DrawHints(const App *a) {
    bool onQuit = a->menu.kit.focus == MI_QUIT;
    KitHint h[3] = {
        {2, {KA_UI_UP, KA_UI_DOWN}, "choose"},
        {1, {KA_UI_PRESS}, "select"},
        {1, {KA_BACK}, onQuit ? "quit" : "to Quit"},
    };
    KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
}

/* The top row: the version badge at the left, "Playing as NAME" at the
 * right (where Scr_PlayingAs puts them). */
static int BadgeRight(void) { return 66 + Kit_TextWidth("v" BLOCKFALL_VERSION, UI_T8); }
static int PlayingAsLeft(const char *username) {
    char name[32];
    snprintf(name, sizeof(name), "%.24s", username);
    return PLAYING_AS_RIGHT - Kit_TextWidth(name, UI_T8) - 9 - Kit_TextWidth("Playing as", UI_T8);
}

/* The toast sits in the gap between the badge and "Playing as" when it
 * fits there; when it does not (a long player name), it spans the whole top
 * row, so it covers the badge and "Playing as" whole while it shows instead
 * of cutting through their text. */
static void DrawToast(const MenuState *m, const char *username) {
    if (!m->toastOn) return;
    const UiTheme *t = Ui_Theme();
    int tw = Kit_TextWidth(m->toast, UI_T16), lw = Kit_TextWidth("Achievement unlocked", UI_T8);
    int inner = tw > lw ? tw : lw;
    int gapL = BadgeRight() + 8, gapR = PlayingAsLeft(username) - 8;
    bool box = inner + 32 <= gapR - gapL;
    int w = box ? inner + 32 : WINDOW_WIDTH, cx = box ? (gapL + gapR) / 2 : WINDOW_WIDTH / 2;
    int top = box ? 10 : 0, h = box ? 58 : 68; /* the strip covers the row from the window's top edge */
    float age = m->toastAge, off = 0.0f, travel = (float)(top + h);
    if (Comfort_Motion()) {
        if (age < TOAST_IN) off = (1.0f - age / TOAST_IN) * travel;
        else if (age > TOAST_HOLD) off = (age - TOAST_HOLD) / TOAST_IN * travel; /* it slides back out */
    }
    int x = cx - w / 2, y = top - (int)off;
    DrawRectangle(x, y, w, h, box ? Fade(t->board, 0.94f) : t->board); /* the strip hides the row under it whole */
    DrawRectangle(x, y + h - 4, w, 4, t->gold);
    Ui_TextCentered("Achievement unlocked", UI_T8, cx, y + h - 48, t->gold);
    Ui_TextCentered(m->toast, UI_T16, cx, y + h - 30, t->text);
}

void ScrMenu_Draw(App *a, float dt) {
    MenuState *m = &a->menu;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    DrawSky(&m->sky);

    Scr_BeginSlide(a);
    Ui_CornerBadge(a->iconPath, "v" BLOCKFALL_VERSION);
    Scr_PlayingAs(a->username, PLAYING_AS_RIGHT, 28);
    DrawTitle(m);
    Ui_TextCentered("Stack them. Clear them. Keep up.", UI_T8, WINDOW_WIDTH / 2, TAGLINE_Y, t->dim);

    KitList l;
    ScrMenu_Layout(a, &l);
    KitRect plate = Kit_GlideTo(&m->glide, ItemRect(m->kit.focus >= 0 && m->kit.focus < MI_COUNT ? m->kit.focus : 0), dt, Comfort_Motion());
    KitView kv = Scr_View(a, &l, &m->kit, &plate);
    KitDraw_List(&kv);

    DrawBests(m);
    DrawHints(a);
    Ui_TextCentered("Made by The Real Ghost", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 20, t->faint);
    Scr_EndSlide();

    DrawToast(m, a->username);
    Scr_EndFrame(a);
}
