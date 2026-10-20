/* Play setup: the five modes in a list (each with a word, and your pick
 * explained at the right), a board picker, Width and Height that always
 * work (hold to repeat, faster after a second), a preview of the board at
 * its true shape, your best and the world's on it, a button for its Scores,
 * and Start. It opens on last time's mode, so Enter, Enter replays your
 * last setup; Enter on any mode row (or Width, Height) starts too. Left/
 * Right, LB/RB or [ and ] change the board from anywhere (on Width and
 * Height, Left/Right change the number). Esc, pad B or [<- Back] go back. */
#include "screens.h"
#include "blockdraw.h"
#include "comfort.h"
#include "layout.h"
#include "menukeys.h"
#include "sfx.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define LEFT_X 24
#define LEFT_W 286
#define MODE_Y 136
#define MODE_PITCH 40
#define ROW_H 34
#define NOTE_X 176
#define SIZE_Y 366
#define SIZE_PITCH 40
#define STEP_X 156
#define RIGHT_X 334
#define RIGHT_W 262
#define PREVIEW_Y 248
#define PREVIEW_H 196
#define BESTS_Y 498
#define HINTS_CY 676

static KitRect ModeRect(int i) { return (KitRect){LEFT_X, (float)(MODE_Y + i * MODE_PITCH), LEFT_W, ROW_H}; }
static KitRect SizeRowRect(int i) { return (KitRect){LEFT_X, (float)(SIZE_Y + i * SIZE_PITCH), LEFT_W, ROW_H}; }
static const KitRect kScoresRect = {44, 452, 246, 28};
static const KitRect kStartRect = {40, 572, 540, 48};
static const KitRect kBoardRect = {382, 212, 214, 24};

static bool IsMode(int id) { return id >= SI_MODE0 && id < SI_MODE0 + MODE_COUNT; }
static bool IsSize(int id) { return id == SI_WIDTH || id == SI_HEIGHT; }

void ScrSetup_Layout(const App *a, KitList *l) {
    const Settings *s = &a->settings;
    int w, h;
    Setup_Size(s, &w, &h);
    Kit_Clear(l, KIT_NAV_Y);
    Scr_AddBack(l, SI_BACK);
    for (int m = 0; m < MODE_COUNT; m++) {
        KitItem *it = Kit_Add(l, SI_MODE0 + m, KIT_ROW, ModeRect(m), Game_ModeName((GameMode)m));
        if (!it) continue;
        snprintf(it->note, sizeof(it->note), "%s", Setup_ModeNote((GameMode)m));
        it->noteX = NOTE_X;
        if (m == s->setupMode) it->flags |= KIT_CHOSEN;
    }
    /* Width and Height are whole rows: their word at the left, "< n >" at
     * the right, so Up and Down from the modes land on them. */
    char num[8];
    snprintf(num, sizeof(num), "%d", w);
    KitItem *it = Kit_Add(l, SI_WIDTH, KIT_STEPPER, SizeRowRect(0), num);
    if (it) { it->value = w; it->min = BOARD_MIN_W; it->max = BOARD_MAX_W; snprintf(it->caption, sizeof(it->caption), "Width"); it->arrowX = STEP_X - LEFT_X; }
    snprintf(num, sizeof(num), "%d", h);
    it = Kit_Add(l, SI_HEIGHT, KIT_STEPPER, SizeRowRect(1), num);
    if (it) { it->value = h; it->min = BOARD_MIN_H; it->max = BOARD_MAX_H; snprintf(it->caption, sizeof(it->caption), "Height"); it->arrowX = STEP_X - LEFT_X; }

    char board[40];
    Setup_BoardLabel(s, board, sizeof(board));
    it = Kit_Add(l, SI_BOARD, KIT_STEPPER, kBoardRect, board);
    if (it) {
        it->textSize = 8;
        it->value = s->setupPreset;
        it->min = 0;
        it->max = BOARD_PRESET_COUNT - 1;
        it->flags |= KIT_NOFOCUS | KIT_WRAP; /* Left/Right, LB/RB and [ ] change it from anywhere; the mouse clicks it */
    }
    it = Kit_Add(l, SI_SCORES, KIT_BUTTON, kScoresRect, "Scores for this board");
    if (it) { it->style = KIT_STYLE_SMALL; it->textSize = 8; }
    it = Kit_Add(l, SI_START, KIT_BUTTON, kStartRect, "Start");
    if (it) it->flags |= KIT_ACCENT; /* always lit */
}

/* Where the focus plate goes: Width and Height light their whole row. */
static KitRect PlateFor(int id) {
    if (IsMode(id)) return ModeRect(id - SI_MODE0);
    if (id == SI_WIDTH) return SizeRowRect(0);
    if (id == SI_HEIGHT) return SizeRowRect(1);
    if (id == SI_SCORES) return kScoresRect;
    if (id == SI_START) return kStartRect;
    return ModeRect(0);
}

static void LoadBests(App *a) {
    SetupState *st = &a->setup;
    const Settings *s = &a->settings;
    GameMode mode = (GameMode)(s->setupMode % MODE_COUNT);
    int w, h;
    Setup_Size(s, &w, &h);
    if (st->cacheMode == (int)mode && st->cacheW == w && st->cacheH == h) return;
    st->cacheMode = (int)mode;
    st->cacheW = w;
    st->cacheH = h;
    st->yourRank[0] = st->worldBest[0] = st->worldWho[0] = '\0';
    if (mode == MODE_ZEN) {
        snprintf(st->yourBest, sizeof(st->yourBest), "none in Zen");
        snprintf(st->worldBest, sizeof(st->worldBest), "none in Zen");
        return;
    }
    RecordUnit unit = Record_UnitOf(mode);
    /* Yours, and the place it takes on this machine's board (the board's own
     * best can be another profile's). */
    int rank = 0;
    long yours = Record_LoadYourBest(mode, w, h, a->username, a->save.highScore, &rank);
    Menu_BestText(unit, yours, "No best yet", st->yourBest, sizeof(st->yourBest));
    if (rank > 0) snprintf(st->yourRank, sizeof(st->yourRank), "#%d here", rank);
    char key[GHOSTLINK_MODE_LEN];
    Record_BoardKey(mode, w, h, key, sizeof(key));
    if (Record_PresetOf(w, h) < 0) {
        snprintf(st->worldBest, sizeof(st->worldBest), "this machine only"); /* custom sizes stay off the world board */
        return;
    }
    static GhostScore rows[GHOSTLINK_TOP_N];
    int n = Record_LoadBoard(key, true, rows, GHOSTLINK_TOP_N);
    if (n <= 0) {
        snprintf(st->worldBest, sizeof(st->worldBest), "none yet");
        return;
    }
    Menu_BestText(unit, rows[0].score, "none yet", st->worldBest, sizeof(st->worldBest));
    /* The holder's name fits between the words and the value: ten
     * characters, or up to nine and a full stop when it is longer (no space
     * before the stop: "The Real Dev Ghost" is "The Real."). */
    if (strlen(rows[0].username) > 10) {
        int keep = 9;
        while (keep > 1 && rows[0].username[keep - 1] == ' ') keep--;
        snprintf(st->worldWho, sizeof(st->worldWho), "%.*s.", keep, rows[0].username);
    } else {
        snprintf(st->worldWho, sizeof(st->worldWho), "%s", rows[0].username);
    }
}

void ScrSetup_Enter(App *a, bool back) {
    SetupState *st = &a->setup;
    /* It opens on last time's mode (Enter, Enter replays it); coming back
     * from Scores keeps the focus where it was. */
    Kit_Enter(&st->kit, back ? st->kit.focus : SI_MODE0 + a->settings.setupMode % MODE_COUNT);
    Kit_GlideReset(&st->glide);
    st->hold = (HoldStep){0};
    st->startBind = -1;
    st->preview.placed = false;
    st->cacheMode = -1; /* scores may have changed while away */
    LoadBests(a);
}

static void Changed(App *a) {
    Settings_Save(&a->settings);
    LoadBests(a);
    Scr_SoundMove();
}

static void StepBoard(App *a, int dir) {
    Setup_StepBoard(&a->settings, dir);
    Changed(a);
}

static void SetSize(App *a, int w, int h) {
    if (Setup_SetSize(&a->settings, w, h)) Changed(a);
}

static void ChooseMode(App *a, int mode) {
    if (a->settings.setupMode == mode) return;
    a->settings.setupMode = mode;
    Changed(a);
}

static void Start(App *a) {
    Settings_Save(&a->settings);
    Sfx_Jingle(JINGLE_READY);
    App_Open(a, SCR_PLAY);
}

/* The keys the screen times itself on Width and Height: the kit must not
 * also step the number, or treat Enter there as a step. */
static Input SizeKeysOut(const Input *in) {
    Input k = *in;
    const int keys[] = {IK_LEFT, IK_RIGHT, IK_A, IK_D, IK_ENTER, IK_SPACE};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) k.key[keys[i]] = k.keyRepeat[keys[i]] = false;
    k.pad[PB_LEFT] = k.padRepeat[PB_LEFT] = k.pad[PB_RIGHT] = k.padRepeat[PB_RIGHT] = false;
    k.pad[PB_A] = k.padRepeat[PB_A] = false;
    return k;
}

static void SizeKeys(App *a, const Input *in, float dt) {
    SetupState *st = &a->setup;
    int right = Keymap_AnyHeld(KA_UI_RIGHT, in) ? 1 : 0, left = Keymap_AnyHeld(KA_UI_LEFT, in) ? 1 : 0;
    int dir = right - left;
    bool pressed = Keymap_PressedEdge(KA_UI_RIGHT, in) || Keymap_PressedEdge(KA_UI_LEFT, in);
    if (pressed) st->holdDir = Keymap_PressedEdge(KA_UI_RIGHT, in) ? 1 : -1;
    if (dir != 0 && dir != st->holdDir && !pressed) dir = 0; /* the other way, held over: wait for its own press */
    int steps = HoldStep_Update(&st->hold, pressed, dir != 0, dt);
    if (steps <= 0) return;
    int w, h;
    Setup_Size(&a->settings, &w, &h);
    int d = st->holdDir * steps;
    if (st->kit.focus == SI_WIDTH) SetSize(a, w + d, h);
    else SetSize(a, w, h + d);
    Kit_Flash(&st->kit, st->kit.focus, st->holdDir > 0 ? 1 : 0);
}

void ScrSetup_Update(App *a, const Input *in, float dt) {
    SetupState *st = &a->setup;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    int boardDir = (Keymap_Pressed(BA_BOARD_NEXT, in) ? 1 : 0) - (Keymap_Pressed(BA_BOARD_PREV, in) ? 1 : 0);
    bool onSize = IsSize(st->kit.focus);
    if (onSize) {
        SizeKeys(a, in, dt);
        /* Enter, Space or A on Width or Height starts the run when let go,
         * like any button (so the press never reaches play). */
        if (st->startBind < 0) st->startBind = Keymap_Fired(KA_UI_PRESS, in);
        else if (!Keymap_Held(st->startBind, in)) {
            st->startBind = -1;
            Start(a);
            return;
        }
    } else {
        st->hold = (HoldStep){0};
        st->startBind = -1;
        boardDir += (Keymap_Pressed(KA_UI_RIGHT, in) ? 1 : 0) - (Keymap_Pressed(KA_UI_LEFT, in) ? 1 : 0);
    }
    if (boardDir) StepBoard(a, boardDir);

    KitList l;
    ScrSetup_Layout(a, &l);
    Input kin = onSize ? SizeKeysOut(in) : *in;
    int before = st->kit.focus;
    KitEvent ev = Kit_Update(&st->kit, &l, &kin);
    int now = st->kit.focus;
    /* Only Up/Down (keys, d-pad) move the mode with the bar. The pointer
     * moves the focus too (hover, and a click on a row the pointer was
     * already resting on), and that must never pick a mode on its own: a
     * click picks on release, and a second click on the chosen one starts. */
    bool byKeys = Keymap_Pressed(KA_UI_UP, &kin) || Keymap_Pressed(KA_UI_DOWN, &kin);
    if (now != before && byKeys) {
        /* Into the mode list by keys: onto the mode you have. Within it:
         * the mode moves with the bar. */
        if (IsMode(now) && !IsMode(before)) st->kit.focus = SI_MODE0 + a->settings.setupMode % MODE_COUNT;
        else if (IsMode(now)) ChooseMode(a, now - SI_MODE0);
        Scr_SoundMove();
    }
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == SI_BACK) { Scr_SoundBack(); App_Back(a); return; }
        if (IsMode(ev.id)) {
            int m = ev.id - SI_MODE0;
            bool click = in->lastDevice == DEV_MOUSE;
            if (click && m != a->settings.setupMode) ChooseMode(a, m); /* a click picks; a click on the one you have starts */
            else { ChooseMode(a, m); Start(a); }
            return;
        }
        if (ev.id == SI_SCORES) {
            Scr_SoundConfirm();
            int w, h;
            Setup_Size(&a->settings, &w, &h);
            ScrScores_OpenFor(a, (GameMode)(a->settings.setupMode % MODE_COUNT), w, h);
            return;
        }
        if (ev.id == SI_START) { Start(a); return; }
    } else if (ev.type == KIT_EV_CHANGE) {
        int w, h;
        Setup_Size(&a->settings, &w, &h);
        if (ev.id == SI_WIDTH) SetSize(a, ev.value, h);
        else if (ev.id == SI_HEIGHT) SetSize(a, w, ev.value);
        else if (ev.id == SI_BOARD) { a->settings.setupPreset = ev.value; Changed(a); }
    }
    Scr_Cursor(Kit_WantsHand(&st->kit, &l));
}

/* --------------------------------------------------------------- drawing */

static void DrawPreview(App *a, float dt) {
    const UiTheme *t = Ui_Theme();
    int w, h;
    Setup_Size(&a->settings, &w, &h);
    PreviewFit f = Setup_PreviewFit(w, h, RIGHT_W - 12, PREVIEW_H - 12);
    SizeTween_Step(&a->setup.preview, (float)(f.pw + 4), (float)(f.ph + 4), dt, PREVIEW_RESHAPE_SECONDS, Comfort_Motion());
    int fw = (int)(a->setup.preview.w + 0.5f), fh = (int)(a->setup.preview.h + 0.5f);
    int cx = RIGHT_X + RIGHT_W / 2, cy = PREVIEW_Y + PREVIEW_H / 2;
    int fx = cx - fw / 2, fy = cy - fh / 2;
    DrawRectangle(fx, fy, fw, fh, t->board);
    DrawRectangleLinesEx((Rectangle){(float)fx, (float)fy, (float)fw, (float)fh}, 2.0f, t->rule);
    /* The cells at a whole size that fits the frame as it is now, so the
     * board grows or shrinks with it and stays pixel art. */
    int c = (fw - 4) / w < (fh - 4) / h ? (fw - 4) / w : (fh - 4) / h;
    if (c < 1) return;
    int bx = cx - (c * w) / 2, by = cy - (c * h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (Setup_PreviewRubble(w, h, x, y))
                Render_BlockAt(bx + x * c, by + (h - 1 - y) * c, c, Render_PieceColor((PieceType)((x * 3 + y * 5) % PIECE_COUNT)), 0.85f);
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(PIECE_T, 0, cells);
    int px = w / 2 - 2, py = 1;
    for (int k = 0; k < CELLS_PER_PIECE; k++)
        Render_BlockAt(bx + (px + cells[k].x) * c, by + (py + cells[k].y) * c, c, Render_PieceColor(PIECE_T), 1.0f);
}

static void DrawRight(App *a, float dt) {
    const UiTheme *t = Ui_Theme();
    const Settings *s = &a->settings;
    GameMode mode = (GameMode)(s->setupMode % MODE_COUNT);
    Ui_Text(Game_ModeName(mode), RIGHT_X, 120, UI_T16, t->light);
    char lines[4][SCR_WRAP_LEN];
    int n = Scr_Wrap(Setup_ModeAbout(mode), UI_T8, RIGHT_W, lines, 4);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], RIGHT_X, 146 + i * 14, UI_T8, t->text);
    Ui_Text("Board", RIGHT_X, 220, UI_T8, t->dim);
    DrawPreview(a, dt);
    int w, h;
    Setup_Size(s, &w, &h);
    char l1[40], l2[40];
    Setup_InPlay(w, h, l1, sizeof(l1), l2, sizeof(l2));
    Ui_Text(l1, RIGHT_X, PREVIEW_Y + PREVIEW_H + 12, UI_T8, t->dim);
    Ui_Text(l2, RIGHT_X, PREVIEW_Y + PREVIEW_H + 26, UI_T8, t->dim);

    /* Each line: the word, then your place or who holds it, and the value
     * at the right edge (a word in faint when there is none). */
    const SetupState *st = &a->setup;
    bool none = st->yourBest[0] < '0' || st->yourBest[0] > '9';
    Ui_Text("Your best", RIGHT_X, BESTS_Y, UI_T8, t->dim);
    if (st->yourRank[0]) Ui_Text(st->yourRank, RIGHT_X + 96, BESTS_Y, UI_T8, t->gold);
    Ui_TextRight(st->yourBest, UI_T8, RIGHT_X + RIGHT_W, BESTS_Y, none ? t->faint : t->text);
    none = st->worldBest[0] < '0' || st->worldBest[0] > '9';
    Ui_Text("World best", RIGHT_X, BESTS_Y + 18, UI_T8, t->dim);
    if (st->worldWho[0]) Ui_Text(st->worldWho, RIGHT_X + 96, BESTS_Y + 18, UI_T8, t->dim);
    Ui_TextRight(st->worldBest, UI_T8, RIGHT_X + RIGHT_W, BESTS_Y + 18, none ? t->faint : t->text);
}

static void DrawHints(const App *a) {
    bool onSize = IsSize(a->setup.kit.focus);
    /* Up/Down change the mode only in the mode list; anywhere else they move
     * the bar (back into the list, onto the mode you have). */
    KitHint h[4] = {
        {2, {KA_UI_UP, KA_UI_DOWN}, IsMode(a->setup.kit.focus) ? "mode" : "move"},
        onSize ? (KitHint){2, {KA_UI_LEFT, KA_UI_RIGHT}, "size"} : (KitHint){2, {BA_BOARD_PREV, BA_BOARD_NEXT}, "board"},
        {1, {KA_UI_PRESS}, "start"},
        {1, {KA_BACK}, "back"},
    };
    KitDraw_Hints(h, 4, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
}

void ScrSetup_Draw(App *a, float dt) {
    SetupState *st = &a->setup;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);

    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Play setup", 40, 48);
    Ui_Text("Mode", LEFT_X + 20, MODE_Y - 18, UI_T8, t->dim);
    Ui_Text("Size", LEFT_X + 20, SIZE_Y - 18, UI_T8, t->dim);
    DrawRectangle(318, MODE_Y - 18, 2, 400, t->rule);

    KitList l;
    ScrSetup_Layout(a, &l);
    KitRect plate = Kit_GlideTo(&st->glide, PlateFor(st->kit.focus), dt, Comfort_Motion());
    KitView kv = Scr_View(a, &l, &st->kit, &plate);
    KitDraw_List(&kv);

    DrawRight(a, dt);
    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}
