/* Headless tests for the hall (src/hall.c and the hall's layout in
 * src/screens.c): the row's slide, hit testing the machines at every place
 * the row can be drawn, the pad map, keys, clicks, the wheel, the control
 * strip, and the card's status line; and for the pad's name board
 * (src/nameboard.c). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/hall.h"
#include "../src/screens.h"
#include "../src/nameboard.h"

/* ------------------------------------------------------------- helpers */

static Input Idle(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.lastDevice = in.hintDevice = DEV_KEYBOARD;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input KeyIn(InputKey k) {
    Input in = Idle();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input PadIn(PadButton b) {
    Input in = Idle();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = in.hintDevice = DEV_PAD;
    return in;
}

static Input WheelIn(float notches) {
    Input in = Idle();
    in.wheel = notches;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static Input At(float x, float y, bool moved) {
    Input in = Idle();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = x >= 0 && y >= 0 && x < KIT_SCREEN_W && y < KIT_SCREEN_H;
    in.mouseMoved = moved;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static const KitEvent kNoEvent = {KIT_EV_NONE, KIT_NONE, 0};

static HallIntent ReadKeys(Input in, int selected, int count, bool admin) {
    HallInput st = {0};
    return Hall_Read(&st, &in, &kNoEvent, selected, count, admin);
}

static HallIntent ReadPress(int id, int selected, int count, bool admin) {
    HallInput st = {0};
    Input in = Idle();
    KitEvent ev = {KIT_EV_PRESS, id, 0};
    return Hall_Read(&st, &in, &ev, selected, count, admin);
}

static bool Is(HallIntent h, int moveTo, HallVerb verb) { return h.moveTo == moveTo && h.verb == verb; }

static Manifest sHall;

static void MakeHall(int count) {
    memset(&sHall, 0, sizeof(sHall));
    sHall.count = count;
    for (int i = 0; i < count; i++) {
        snprintf(sHall.games[i].name, sizeof(sHall.games[i].name), "Machine %d", i + 1);
        snprintf(sHall.games[i].exec, sizeof(sHall.games[i].exec), "~/bin/machine%d", i + 1);
    }
}

static HallView View(int count, int selected, float scroll, bool admin, InputDevice device) {
    MakeHall(count);
    HallView v = {
        .arcadeName = "TRG",
        .admin = admin,
        .manifest = &sHall,
        .place = selected, /* no lineup: the catalog's order, so place = index */
        .scroll = scroll,
        .device = device,
        .play = count > 0 ? HP_PLAY : HP_NONE,
        .dailyReason = "",
    };
    return v;
}

static bool Overlap(KitRect a, KitRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static bool IsCabinet(int id) { return id >= WID_CABINET && id < WID_CABINET + MANIFEST_MAX_GAMES; }

/* ---------------------------------------------------------------- tests */

/* The slide that used to live in render.c, frame by frame. */
static void test_hall_row(void) {
    HallRow r;
    Hall_RowInit(&r);
    Hall_RowStep(&r, 7, 1.0f / 60.0f);
    CHECK(r.scroll == 7.0f && r.lastSelected == 7, "row: the first frame starts settled, no slide-in");
    float want = 7.0f;
    bool same = true;
    for (int f = 0; f < 40; f++) {
        Hall_RowStep(&r, 8, 1.0f / 60.0f);
        want += (8.0f - want) * (1.0f - expf(-16.0f / 60.0f));
        if (fabsf(8.0f - want) < 0.002f) want = 8.0f;
        if (fabsf(r.scroll - want) > 1e-5f) same = false;
    }
    CHECK(same && r.scroll == 8.0f, "row: it eases to the new machine and settles exactly");
    Hall_RowStep(&r, 9, 1.0f / 60.0f);
    CHECK(r.sinceSelect < 0.02f && Hall_MarqueePower(&r) == 0.9f, "row: a new machine's marquee starts its warm-up");
    bool dim = false, half = false;
    for (int f = 0; f < 15; f++) {
        Hall_RowStep(&r, 9, 1.0f / 60.0f);
        if (Hall_MarqueePower(&r) == 0.25f) dim = true;
        if (Hall_MarqueePower(&r) == 0.5f) half = true;
    }
    CHECK(dim && half && Hall_MarqueePower(&r) == 1.0f, "row: it stutters (dim, on, half), then burns steady");

    /* A wrap jumps; a jump of five (LB/RB) still slides. */
    Hall_RowInit(&r);
    Hall_RowStep(&r, 11, 1.0f / 60.0f);
    Hall_RowStep(&r, 0, 1.0f / 60.0f);
    CHECK(r.scroll == 0.0f && r.sinceSelect < 0.02f, "row: last to first jumps there (no slide past the whole row), the marquee still warms up");
    Hall_RowStep(&r, 5, 1.0f / 60.0f);
    CHECK(r.scroll > 0.0f && r.scroll < 5.0f, "row: five along still slides");
    for (int f = 0; f < 60; f++) Hall_RowStep(&r, 5, 1.0f / 60.0f);
    Hall_RowStep(&r, 11, 1.0f / 60.0f);
    CHECK(r.scroll == 11.0f, "row: six along jumps");
    Hall_RowSnap(&r, 3);
    float since = r.sinceSelect;
    Hall_RowStep(&r, 3, 1.0f / 60.0f);
    CHECK(r.scroll == 3.0f && r.lastSelected == 3 && r.sinceSelect > since, "row: a new order stands the row at its place at once, no flicker");
}

/* DrawCabinet's own arithmetic, written out again: 3 px units in the row,
 * 4 in the middle, the marquee one unit wider each side. */
static KitRect Drawn(float scroll, int i) {
    float offset = (float)i - scroll, focus = fmaxf(0.0f, 1.0f - fabsf(offset)), u = 3.0f + focus;
    float cx = 480.0f + offset * 200.0f, x0 = cx - 24.0f * u;
    return (KitRect){x0 - u, 432.0f - 84.0f * u, 50.0f * u, 84.0f * u};
}

static void test_hall_geometry(void) {
    KitRect lit = Hall_CabinetRect(4.0f, 4), side = Hall_CabinetRect(4.0f, 5);
    CHECK(lit.x == 380 && lit.y == 96 && lit.w == 200 && lit.h == 336, "geometry: the lit machine, 4 px units, 96..432");
    CHECK(side.x == 605 && side.y == 180 && side.w == 150 && side.h == 252, "geometry: its neighbour, 3 px units, 180..432");
    CHECK(Hall_CabinetShown(4.0f, 2) && Hall_CabinetShown(4.0f, 6) && !Hall_CabinetShown(4.0f, 8) && !Hall_CabinetShown(4.0f, 0),
          "geometry: two either side are in sight, further ones are not drawn");

    /* Every place the row passes while sliding, for rows of every length:
     * a point in the middle of each machine drawn hits that machine, the
     * gaps between them hit none, and nothing else sits over a machine. */
    bool rectsMatch = true, centresHit = true, gapsMiss = true, clear = true, onlyDrawn = true;
    static const int kCounts[] = {1, 2, 3, 5, 12, 64};
    for (size_t c = 0; c < sizeof(kCounts) / sizeof(kCounts[0]); c++) {
        int n = kCounts[c];
        for (int step = 0; step <= (n - 1) * 20; step++) {
            float scroll = (float)step / 20.0f;
            int selected = (int)lroundf(scroll);
            for (int dev = 0; dev < 2; dev++) {
                HallView v = View(n, selected, scroll, dev == 1, dev == 1 ? DEV_PAD : DEV_KEYBOARD);
                KitList l;
                Screens_Hall(&l, &v);
                for (int i = 0; i < n; i++) {
                    const KitItem *it = Kit_Get(&l, WID_CABINET + i);
                    bool drawn = fabsf((float)i - scroll) <= HALL_ROW_REACH && Drawn(scroll, i).x < 960 && Drawn(scroll, i).x + Drawn(scroll, i).w > 0;
                    if (!!it != drawn) onlyDrawn = false;
                    if (!it) continue;
                    KitRect d = Drawn(scroll, i);
                    float x0 = fmaxf(d.x, 0.0f), x1 = fminf(d.x + d.w, 960.0f);
                    if (fabsf(it->r.x - x0) > 0.01f || fabsf(it->r.x + it->r.w - x1) > 0.01f || fabsf(it->r.y - d.y) > 0.01f ||
                        fabsf(it->r.h - d.h) > 0.01f)
                        rectsMatch = false;
                    int hit = Kit_HitTest(&l, (x0 + x1) / 2.0f, d.y + d.h / 2.0f);
                    if (hit < 0 || l.items[hit].id != WID_CABINET + i) centresHit = false;
                    for (int j = 0; j < l.count; j++)
                        if (!IsCabinet(l.items[j].id) && Overlap(l.items[j].r, it->r)) clear = false;
                }
                /* The middle of the floor between two neighbours, low on the cabinets. */
                for (int i = 0; i + 1 < n; i++) {
                    KitRect a = Drawn(scroll, i), b = Drawn(scroll, i + 1);
                    float mid = (a.x + a.w + b.x) / 2.0f;
                    if (b.x - (a.x + a.w) < 2.0f) gapsMiss = false; /* the row always leaves floor between machines */
                    if (mid < 0 || mid >= 960) continue;
                    int hit = Kit_HitTest(&l, mid, 400.0f);
                    if (hit >= 0 && IsCabinet(l.items[hit].id)) gapsMiss = false;
                }
            }
        }
    }
    CHECK(onlyDrawn, "hit test: exactly the machines drawn take clicks, at every place the row slides through");
    CHECK(rectsMatch, "hit test: each machine's click area is the cabinet as drawn (clipped to the screen)");
    CHECK(centresHit, "hit test: the middle of every drawn machine hits that machine");
    CHECK(gapsMiss, "hit test: the floor between machines hits none");
    CHECK(clear, "hit test: no header chip, sign or strip button sits over a machine");

    /* Mid-slide the lit one is drawn last: where two overlap, it takes the click. */
    HallView v = View(12, 5, 4.5f, false, DEV_KEYBOARD);
    KitList l;
    Screens_Hall(&l, &v);
    int lastCab = -1;
    for (int i = 0; i < l.count; i++) if (IsCabinet(l.items[i].id)) lastCab = l.items[i].id;
    CHECK(lastCab == WID_CABINET + 5, "hit test: the lit machine is on top, as it is drawn");
    v.coinInFlight = true;
    Screens_Hall(&l, &v);
    CHECK(l.count == 1 && l.items[0].id == WID_CLOSE, "hit test: while the coin drops, no machine takes a click");
}

static void test_hall_jump_and_wheel(void) {
    static const int kJumps[][4] = {
        /* selected, count, delta, to */
        {7, 12, -5, 2}, {2, 12, -5, 0}, {0, 12, -5, 11}, {3, 12, 5, 8}, {9, 12, 5, 11}, {11, 12, 5, 0},
        {1, 3, 5, 2}, {2, 3, 5, 0}, {0, 3, -5, 2}, {0, 1, 5, 0}, {0, 1, -5, 0}, {0, 0, 5, 0},
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(kJumps) / sizeof(kJumps[0]); i++)
        if (Hall_Jump(kJumps[i][0], kJumps[i][1], kJumps[i][2]) != kJumps[i][3]) ok = false;
    CHECK(ok, "jump: five along, stopping at an end, and from the end round to the other");

    HallInput st = {0};
    Input in = WheelIn(-1.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 4, 12, false).moveTo == 5, "wheel: toward you is the next machine");
    in = WheelIn(1.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 4, 12, false).moveTo == 3, "wheel: away from you is the previous one");
    in = WheelIn(-3.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 4, 12, false).moveTo == 7, "wheel: one machine per notch");
    in = WheelIn(-0.5f);
    HallIntent a = Hall_Read(&st, &in, &kNoEvent, 4, 12, false), b = Hall_Read(&st, &in, &kNoEvent, 4, 12, false);
    CHECK(a.moveTo == -1 && b.moveTo == 5, "wheel: half notches add up (a touchpad moves one at a time)");
    in = WheelIn(-5.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 10, 12, false).moveTo == 11 && st.wheel == 0.0f, "wheel: it stops at the last machine");
    in = WheelIn(-1.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 11, 12, false).moveTo == -1, "wheel: and does not spin round to the first");
    in = WheelIn(2.0f);
    CHECK(Hall_Read(&st, &in, &kNoEvent, 0, 0, false).moveTo == -1, "wheel: an empty hall stays put");
    st.wheel = 0.0f;
    in = WheelIn(-0.5f);
    a = Hall_Read(&st, &in, &kNoEvent, 0, 12, false);
    b = Hall_Read(&st, &in, &kNoEvent, 0, 12, false);
    CHECK(a.moveTo == -1 && b.moveTo == 1, "wheel: half notches off the first machine add up too");
    in = WheelIn(0.5f);
    Hall_Read(&st, &in, &kNoEvent, 0, 12, false);
    CHECK(st.wheel == 0.0f, "wheel: but none saved up pushing against it");
}

/* The owner's pad map: A play, Y daily, X this game, View stats, Menu or B
 * the Arcade menu, LB/RB five along, the d-pad one along (with repeat). */
static void test_hall_pad_map(void) {
    static const struct {
        PadButton b;
        int moveTo;
        HallVerb verb;
        KeyAction open;
    } kMap[PB_COUNT] = {
        {PB_A, -1, HV_PLAY, KA_NONE},     {PB_B, -1, HV_MENU, KA_NONE},     {PB_X, -1, HV_THIS_GAME, KA_NONE},
        {PB_Y, -1, HV_DAILY, KA_NONE},    {PB_LB, 0, HV_NONE, KA_NONE},     {PB_RB, 9, HV_NONE, KA_NONE},
        {PB_LT, -1, HV_NONE, KA_NONE},    {PB_RT, -1, HV_NONE, KA_NONE},    {PB_VIEW, -1, HV_OPEN, KA_OPEN_STATS},
        {PB_MENU, -1, HV_MENU, KA_NONE},  {PB_LS, -1, HV_NONE, KA_NONE},    {PB_RS, -1, HV_NONE, KA_NONE},
        {PB_UP, 3, HV_NONE, KA_NONE},     {PB_DOWN, 5, HV_NONE, KA_NONE},   {PB_LEFT, 3, HV_NONE, KA_NONE},
        {PB_RIGHT, 5, HV_NONE, KA_NONE},
    };
    bool all = true;
    char bad[64] = "";
    for (int i = 0; i < PB_COUNT; i++) {
        HallIntent h = ReadKeys(PadIn(kMap[i].b), 4, 12, true);
        if (h.moveTo != kMap[i].moveTo || h.verb != kMap[i].verb || (h.verb == HV_OPEN && h.open != kMap[i].open)) {
            all = false;
            snprintf(bad, sizeof(bad), "pad button %d", (int)kMap[i].b);
        }
    }
    CHECK(all, "pad map: every button of the pad does what the owner's map says on the hall");
    if (!all) printf("  (first mismatch: %s)\n", bad);
    CHECK(ReadKeys(PadIn(PB_RB), 11, 12, false).moveTo == 0 && ReadKeys(PadIn(PB_LB), 0, 12, false).moveTo == 11,
          "pad map: LB/RB from an end come round");

    /* A held d-pad repeats: the repeat steps arrive without a new press. */
    Input rep = Idle();
    rep.padRepeat[PB_RIGHT] = rep.padDown[PB_RIGHT] = true;
    CHECK(ReadKeys(rep, 4, 12, false).moveTo == 5, "pad map: holding the d-pad keeps moving");
    rep = Idle();
    rep.padDown[PB_A] = true;
    CHECK(ReadKeys(rep, 4, 12, false).verb == HV_NONE, "pad map: A held from before plays nothing");

    /* The pad's buttons never answer keyboard letters: the old shared pad
     * code read X, Y, Menu and View as the keys X, C, P and R. */
    static const InputKey kOldLetters[] = {IK_X, IK_Y, IK_C, IK_P, IK_R, IK_B, IK_Q};
    bool quiet = true;
    for (size_t i = 0; i < sizeof(kOldLetters) / sizeof(kOldLetters[0]); i++) {
        HallIntent h = ReadKeys(KeyIn(kOldLetters[i]), 4, 12, true);
        if (h.verb != HV_NONE || h.moveTo != -1) quiet = false;
    }
    CHECK(quiet, "pad map: typing X, Y, C, P, R, B or Q on the keyboard does nothing on the hall");
}

static void test_hall_keys(void) {
    static const struct {
        InputKey k;
        bool shift;
        int moveTo;
        HallVerb verb;
        KeyAction open;
    } kKeys[] = {
        {IK_ENTER, false, -1, HV_PLAY, KA_NONE},        {IK_SPACE, false, -1, HV_PLAY, KA_NONE},
        {IK_D, false, -1, HV_DAILY, KA_NONE},           {IK_I, false, -1, HV_THIS_GAME, KA_NONE},
        {IK_ESCAPE, false, -1, HV_MENU, KA_NONE},       {IK_TAB, false, -1, HV_OPEN, KA_OPEN_SETTINGS},
        {IK_L, false, -1, HV_OPEN, KA_OPEN_STATS},      {IK_H, false, -1, HV_OPEN, KA_OPEN_TROPHIES},
        {IK_V, false, -1, HV_OPEN, KA_OPEN_UPDATES},    {IK_O, false, -1, HV_OPEN, KA_OPEN_ONLINE},
        {IK_U, false, -1, HV_OPEN, KA_OPEN_NAME},       {IK_U, true, -1, HV_OPEN, KA_OPEN_GAME_NAME},
        {IK_LEFT, false, 3, HV_NONE, KA_NONE},          {IK_RIGHT, false, 5, HV_NONE, KA_NONE},
        {IK_UP, false, 3, HV_NONE, KA_NONE},            {IK_DOWN, false, 5, HV_NONE, KA_NONE},
        {IK_W, false, 3, HV_NONE, KA_NONE},             {IK_S, false, 5, HV_NONE, KA_NONE},
        {IK_PAGE_UP, false, 0, HV_NONE, KA_NONE},       {IK_PAGE_DOWN, false, 9, HV_NONE, KA_NONE},
        {IK_A, false, -1, HV_ADD, KA_NONE},             {IK_E, false, -1, HV_EDIT, KA_NONE},
        {IK_DELETE, false, -1, HV_REMOVE, KA_NONE},
        {IK_F, false, -1, HV_FAVOURITE, KA_NONE},       {IK_T, false, -1, HV_ORDER, KA_NONE},
    };
    bool all = true;
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
        Input in = KeyIn(kKeys[i].k);
        in.shift = kKeys[i].shift;
        HallIntent h = ReadKeys(in, 4, 12, true);
        if (h.moveTo != kKeys[i].moveTo || h.verb != kKeys[i].verb || (h.verb == HV_OPEN && h.open != kKeys[i].open)) all = false;
    }
    CHECK(all, "keys: every old letter and arrow keeps its job on the hall; I is this game, F a favourite, T the next order");
    CHECK(ReadKeys(KeyIn(IK_A), 4, 12, false).verb == HV_NONE && ReadKeys(KeyIn(IK_E), 4, 12, false).verb == HV_NONE &&
              ReadKeys(KeyIn(IK_DELETE), 4, 12, false).verb == HV_NONE,
          "keys: A, E and Delete do nothing without admin");
    Input alt = KeyIn(IK_ENTER);
    alt.alt = true;
    CHECK(ReadKeys(alt, 4, 12, true).verb == HV_NONE, "keys: Alt+Enter is fullscreen, never play");
    CHECK(ReadKeys(KeyIn(IK_RIGHT), 11, 12, false).moveTo == 0 && ReadKeys(KeyIn(IK_LEFT), 0, 12, false).moveTo == 11,
          "keys: the arrows still wrap round the row");

    /* Two at once: the move, then one action, never two. */
    Input both = KeyIn(IK_RIGHT);
    both.key[IK_ENTER] = both.keyDown[IK_ENTER] = true;
    CHECK(Is(ReadKeys(both, 4, 12, false), 5, HV_PLAY), "keys: Right and Enter together play the next machine, as before");
    Input two = KeyIn(IK_ENTER);
    two.key[IK_DELETE] = two.keyDown[IK_DELETE] = true;
    CHECK(Is(ReadKeys(two, 4, 12, true), -1, HV_PLAY), "keys: Enter and Delete together: one action only");
    Input menu = KeyIn(IK_ESCAPE);
    menu.key[IK_ENTER] = true;
    CHECK(Is(ReadKeys(menu, 4, 12, false), -1, HV_MENU), "keys: Esc wins over anything else that frame");

    /* An empty hall: no machine to play, move or edit; Add and the screens still work. */
    CHECK(Is(ReadKeys(KeyIn(IK_ENTER), 0, 0, true), -1, HV_NONE) && Is(ReadKeys(KeyIn(IK_RIGHT), 0, 0, true), -1, HV_NONE) &&
              Is(ReadKeys(KeyIn(IK_DELETE), 0, 0, true), -1, HV_NONE) && Is(ReadKeys(KeyIn(IK_A), 0, 0, true), -1, HV_ADD) &&
              ReadKeys(KeyIn(IK_H), 0, 0, false).verb == HV_OPEN,
          "keys: an empty hall still opens screens and lets admin add");
    CHECK(Is(ReadKeys(KeyIn(IK_F), 0, 0, false), -1, HV_NONE) && Is(ReadKeys(KeyIn(IK_T), 0, 0, false), -1, HV_ORDER),
          "keys: on an empty row F has nothing to star; T still changes the order");
    Input fd = KeyIn(IK_F);
    fd.key[IK_DELETE] = fd.keyDown[IK_DELETE] = true;
    CHECK(Is(ReadKeys(fd, 4, 12, true), -1, HV_FAVOURITE), "keys: F and Delete together: one action only");
}

static void test_hall_clicks(void) {
    CHECK(Is(ReadPress(WID_CABINET + 6, 4, 12, false), 6, HV_NONE), "clicks: a side machine comes to the middle");
    CHECK(Is(ReadPress(WID_CABINET + 4, 4, 12, false), -1, HV_PLAY), "clicks: the lit machine plays (the owner's default)");
    CHECK(Is(ReadPress(WID_CABINET + 12, 4, 12, false), -1, HV_NONE), "clicks: no such machine, nothing");
    CHECK(ReadPress(WID_STRIP_PLAY, 4, 12, false).verb == HV_PLAY && ReadPress(WID_STRIP_DAILY, 4, 12, false).verb == HV_DAILY &&
              ReadPress(WID_STRIP_THIS_GAME, 4, 12, false).verb == HV_THIS_GAME &&
              ReadPress(WID_STRIP_MENU, 4, 12, false).verb == HV_MENU && ReadPress(WID_MENU_CHIP, 4, 12, false).verb == HV_MENU,
          "clicks: Play, Daily, This game and both Menu buttons");
    CHECK(ReadPress(WID_CARD, 4, 12, false).verb == HV_THIS_GAME && ReadPress(WID_CARD, 0, 0, false).verb == HV_NONE,
          "clicks: a click on the card opens the lit machine's page");
    HallIntent stats = ReadPress(WID_STRIP_STATS, 4, 12, false);
    CHECK(stats.verb == HV_OPEN && stats.open == KA_OPEN_STATS, "clicks: Stats");
    CHECK(Is(ReadPress(WID_STRIP_PREV, 0, 12, false), 11, HV_NONE) && Is(ReadPress(WID_STRIP_NEXT, 11, 12, false), 0, HV_NONE),
          "clicks: the strip's arrows step one machine and wrap, as the arrow keys do");
    CHECK(ReadPress(WID_EXIT_SIGN, 4, 12, false).verb == HV_LEAVE && ReadPress(WID_UPDATE_PILL, 4, 12, false).verb == HV_UPDATES &&
              ReadPress(WID_PLAYING_AS, 4, 12, false).verb == HV_PLAYER,
          "clicks: EXIT, NEW VERSION and Playing as");
    CHECK(ReadPress(WID_STRIP_ADD, 4, 12, true).verb == HV_ADD && ReadPress(WID_STRIP_EDIT, 4, 12, true).verb == HV_EDIT &&
              ReadPress(WID_STRIP_REMOVE, 4, 12, true).verb == HV_REMOVE && ReadPress(WID_STRIP_ADD, 4, 12, false).verb == HV_NONE,
          "clicks: admin's buttons, only for admin");
    CHECK(ReadPress(WID_STRIP_PLAY, 0, 0, false).verb == HV_NONE && ReadPress(WID_STRIP_REMOVE, 0, 0, true).verb == HV_NONE,
          "clicks: nothing to play or remove in an empty hall");

    /* Through the kit, as main.c runs it: press and let go on the same machine. */
    HallView v = View(12, 4, 4.0f, false, DEV_KEYBOARD);
    KitList l;
    Screens_Hall(&l, &v);
    KitState k;
    Kit_Init(&k);
    KitRect side = Kit_Get(&l, WID_CABINET + 5)->r;
    float x = side.x + side.w / 2, y = side.y + side.h / 2;
    Input hover = At(x, y, true);
    KitEvent ev = Kit_Update(&k, &l, &hover);
    CHECK(ev.type == KIT_EV_NONE && k.hover == WID_CABINET + 5 && Kit_WantsHand(&k, &l) &&
              Kit_Look(&k, Kit_Get(&l, WID_CABINET + 5), DEV_MOUSE) == KIT_LOOK_HOVER,
          "clicks: pointing at a side machine lights it (the glow) and shows the hand");
    CHECK(Kit_Look(&k, Kit_Get(&l, WID_CABINET + 5), DEV_KEYBOARD) == KIT_LOOK_NORMAL, "clicks: no glow once keys are in use");
    Input down = At(x, y, false), up = At(x, y, false);
    down.mousePressed[MB_LEFT] = down.mouseDown[MB_LEFT] = true;
    up.mouseReleased[MB_LEFT] = true;
    CHECK(Kit_Update(&k, &l, &down).type == KIT_EV_NONE, "clicks: nothing happens on the press");
    ev = Kit_Update(&k, &l, &up);
    HallInput st = {0};
    HallIntent h = Hall_Read(&st, &up, &ev, 4, 12, false);
    CHECK(ev.type == KIT_EV_PRESS && ev.id == WID_CABINET + 5 && h.moveTo == 5, "clicks: letting go on it brings it to the middle");
    Kit_Update(&k, &l, &down);
    Input away = At(480, 600 + 30, true);
    away.mouseReleased[MB_LEFT] = true;
    CHECK(Kit_Update(&k, &l, &away).type == KIT_EV_NONE, "clicks: dragged off before letting go, nothing");
    Input empty = At(480, 450, true);
    Kit_Update(&k, &l, &empty);
    CHECK(!Kit_WantsHand(&k, &l), "clicks: the carpet is not a button");
}

static const KitItem *Chip(const KitList *l, int id) { return Kit_Get(l, id); }

/* The card's Favourite: at the right of the name, a star that is gold on a
 * favourite, F on its cap whatever the device (the pad has no button for it). */
static void test_hall_card_favourite(void) {
    KitList l;
    HallView v = View(12, 4, 4.0f, false, DEV_KEYBOARD);
    v.canFavourite = true;
    Screens_Hall(&l, &v);
    const KitItem *fav = Kit_Get(&l, WID_CARD_FAVOURITE);
    CHECK(fav && strcmp(fav->label, "Favourite") == 0 && strcmp(fav->key, "F") == 0 && fav->icon == KIT_ICON_STAR && fav->value == 0 &&
              !(fav->flags & KIT_DISABLED) && !Kit_CanFocus(fav) && fav->style == KIT_STYLE_CHIP,
          "favourite: the card's button: F, a star, the word; for the mouse");
    CHECK(fav->r.x + fav->r.w == HALL_FAV_RIGHT && fav->r.y == HALL_FAV_Y && fav->r.y + fav->r.h < HALL_CARD_Y + 52 &&
              fav->r.x > HALL_CARD_LEFT + Kit_TextWidth("Girderclimb", 24) && fav->r.w >= Kit_ChipWidth("F", false, "Favourite") + KIT_CHIP_ICON_W,
          "favourite: on the name's line, ending where the left column does, above the description");
    CHECK(Is(ReadPress(WID_CARD_FAVOURITE, 4, 12, false), -1, HV_FAVOURITE) && ReadPress(WID_CARD_FAVOURITE, 0, 0, false).verb == HV_NONE,
          "favourite: a click stars (or unstars) the lit machine");
    v.favourite = true;
    v.device = DEV_PAD;
    Screens_Hall(&l, &v);
    fav = Kit_Get(&l, WID_CARD_FAVOURITE);
    CHECK(fav->value == 1 && strcmp(fav->key, "F") == 0 && !(fav->flags & KIT_PADCAP), "favourite: gold on a favourite; F even after pad use");
    v.canFavourite = false;
    Screens_Hall(&l, &v);
    fav = Kit_Get(&l, WID_CARD_FAVOURITE);
    CHECK((fav->flags & KIT_DISABLED) && fav->reason[0], "favourite: faint, with the reason, for a program name launcher.ini cannot keep");
    bool clear = true;
    for (int i = 0; i < l.count; i++)
        if (l.items[i].id != WID_CARD_FAVOURITE && l.items[i].id != WID_CARD && Overlap(l.items[i].r, fav->r)) clear = false;
    CHECK(clear, "favourite: nothing else sits on it but the card it is drawn on");
    const KitItem *card = Kit_Get(&l, WID_CARD);
    CHECK(card && card->r.x == HALL_CARD_X && card->r.y == HALL_CARD_Y && card->r.w == HALL_CARD_W && card->r.h == HALL_CARD_H &&
              (card->flags & KIT_SELFDRAWN) && !Kit_CanFocus(card),
          "card: the whole card takes a click, for the mouse only, drawn by the hall");
    int hitFav = Kit_HitTest(&l, fav->r.x + fav->r.w / 2, fav->r.y + fav->r.h / 2), hitCard = Kit_HitTest(&l, 200, 560);
    CHECK(hitFav >= 0 && l.items[hitFav].id == WID_CARD_FAVOURITE && hitCard >= 0 && l.items[hitCard].id == WID_CARD,
          "card: its Favourite stays on top of it; the rest of it opens the page");
    v = View(0, 0, 0.0f, false, DEV_KEYBOARD);
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CARD_FAVOURITE) && !Kit_Get(&l, WID_CARD), "favourite: none on an empty row, and no card to click");
    v = View(12, 4, 4.0f, false, DEV_KEYBOARD);
    v.coinInFlight = true;
    Screens_Hall(&l, &v);
    CHECK(!Kit_Get(&l, WID_CARD_FAVOURITE) && !Kit_Get(&l, WID_CARD), "favourite: none while the coin drops, and the card waits too");

    /* Chipforge: Open, not Play. */
    v = View(12, 11, 11.0f, false, DEV_KEYBOARD);
    v.tool = true;
    Screens_Hall(&l, &v);
    CHECK(strcmp(Chip(&l, WID_STRIP_PLAY)->label, "Open Chipforge") == 0 && !(Chip(&l, WID_STRIP_PLAY)->flags & KIT_DISABLED),
          "tool: Chipforge's Play reads Open Chipforge");
    const KitItem *afterOpen = Kit_Get(&l, WID_STRIP_THIS_GAME);
    CHECK(!Kit_Get(&l, WID_STRIP_DAILY) && afterOpen &&
              afterOpen->r.x == Chip(&l, WID_STRIP_PLAY)->r.x + Chip(&l, WID_STRIP_PLAY)->r.w + STRIP_GAP,
          "tool: no Daily chip at all on Chipforge, and the next chip closes up");
    v.tool = false;
    Screens_Hall(&l, &v);
    CHECK(Kit_Get(&l, WID_STRIP_DAILY) != NULL, "tool: every game keeps its Daily chip");
    v.tool = true;
    v.admin = true;
    Screens_Hall(&l, &v);
    const KitItem *open = Chip(&l, WID_STRIP_PLAY), *prev = Chip(&l, WID_STRIP_PREV), *remove = Chip(&l, WID_STRIP_REMOVE);
    CHECK(strcmp(open->label, "Open Chipforge") == 0 && remove->r.x + remove->r.w + STRIP_GROUP_GAP <= prev->r.x,
          "tool: with no Daily chip, Open Chipforge fits beside admin's buttons too");
    v = View(0, 0, 0.0f, false, DEV_KEYBOARD);
    v.allHidden = true;
    Screens_Hall(&l, &v);
    CHECK(strstr(Chip(&l, WID_STRIP_PLAY)->reason, "hidden") != NULL, "empty row: when every machine is hidden, the reason says where to find them");

    HallStatus st;
    HallStatusIn in = {.installed = true, .tool = true, .device = DEV_KEYBOARD, .error = "", .note = ""};
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Press Enter to open Chipforge") == 0 && st.size == 16, "tool: Press Enter to open Chipforge");
    in.device = DEV_PAD;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Press A to open Chipforge") == 0, "tool: and after pad use, A");
}

static void test_hall_strip(void) {
    KitList l;
    HallView v = View(12, 4, 4.0f, false, DEV_KEYBOARD);
    Screens_Hall(&l, &v);
    static const int kIds[] = {WID_STRIP_PLAY, WID_STRIP_DAILY, WID_STRIP_THIS_GAME, WID_STRIP_STATS, WID_STRIP_MENU};
    static const char *const kLabels[] = {"Play", "Daily", "This game", "Stats", "Menu"};
    static const char *const kKeysCaps[] = {"Enter", "D", "I", "L", "Esc"};
    static const char *const kPadCaps[] = {"A", "Y", "X", "View", "Menu"};
    bool keys = true;
    for (int i = 0; i < 5; i++) {
        const KitItem *c = Chip(&l, kIds[i]);
        if (!c || strcmp(c->label, kLabels[i]) != 0 || strcmp(c->key, kKeysCaps[i]) != 0 || (c->flags & KIT_PADCAP) ||
            c->style != KIT_STYLE_CHIP || Kit_CanFocus(c))
            keys = false;
    }
    CHECK(keys, "strip: Play, Daily, This game, Stats, Menu, each with its key after keyboard use");
    v.device = DEV_PAD;
    Screens_Hall(&l, &v);
    bool pad = true;
    for (int i = 0; i < 5; i++) {
        const KitItem *c = Chip(&l, kIds[i]);
        if (!c || strcmp(c->key, kPadCaps[i]) != 0 || !(c->flags & KIT_PADCAP)) pad = false;
    }
    CHECK(pad, "strip: after pad use, the pad's buttons: A, Y, X, View, Menu");
    CHECK(!Chip(&l, WID_STRIP_ADD), "strip: no admin buttons without admin");
    v.admin = true;
    Screens_Hall(&l, &v);
    CHECK(Chip(&l, WID_STRIP_ADD) && strcmp(Chip(&l, WID_STRIP_ADD)->key, "A") == 0 && !(Chip(&l, WID_STRIP_ADD)->flags & KIT_PADCAP) &&
              strcmp(Chip(&l, WID_STRIP_REMOVE)->key, "Del") == 0 && strcmp(Chip(&l, WID_STRIP_EDIT)->label, "Edit") == 0,
          "strip: admin's Add, Edit, Remove name their keys, even after pad use (the pad has none)");

    v.device = DEV_KEYBOARD;
    v.dailyReason = "Machine 5 has no daily challenge.";
    Screens_Hall(&l, &v);
    const KitItem *daily = Chip(&l, WID_STRIP_DAILY);
    CHECK((daily->flags & KIT_DISABLED) && strcmp(daily->reason, v.dailyReason) == 0, "strip: Daily is faint, with the reason, where there is none");
    KitState k;
    Kit_Init(&k);
    Input over = At(daily->r.x + 10, daily->r.y + 10, true);
    Kit_Update(&k, &l, &over);
    CHECK(strcmp(Kit_Reason(&k, &l, DEV_MOUSE), v.dailyReason) == 0 && !Kit_WantsHand(&k, &l), "strip: pointing at it gives the reason, not a hand");
    Input down = over, up = over;
    down.mousePressed[MB_LEFT] = down.mouseDown[MB_LEFT] = true;
    up.mouseReleased[MB_LEFT] = true;
    Kit_Update(&k, &l, &down);
    CHECK(Kit_Update(&k, &l, &up).type == KIT_EV_NONE, "strip: a click on the faint Daily does nothing");

    v.admin = false;
    static const struct { HallPlay play; const char *label; bool off; } kPlay[] = {
        {HP_PLAY, "Play", false}, {HP_INSTALL, "Install", false}, {HP_INSTALLING, "Installing", true}, {HP_CANNOT, "Play", true},
    };
    bool plays = true;
    for (size_t i = 0; i < sizeof(kPlay) / sizeof(kPlay[0]); i++) {
        v.play = kPlay[i].play;
        Screens_Hall(&l, &v);
        const KitItem *p = Chip(&l, WID_STRIP_PLAY);
        if (strcmp(p->label, kPlay[i].label) != 0 || !!(p->flags & KIT_DISABLED) != kPlay[i].off || (kPlay[i].off && !p->reason[0])) plays = false;
    }
    CHECK(plays, "strip: Play, Install, Installing (faint) and a faint Play where neither can happen");
    v.admin = true;
    v.play = HP_INSTALLING;
    Screens_Hall(&l, &v);
    const KitItem *busy = Chip(&l, WID_STRIP_PLAY);
    CHECK(strcmp(busy->label, "Install") == 0 && (busy->flags & KIT_DISABLED) && busy->reason[0],
          "strip: with admin's buttons there is no room for Installing: a faint Install (the card says Installing...)");

    /* Every combination stays on one line: inside the screen, below the
     * card, nothing overlapping, the arrows round the widest count. */
    bool tidy = true;
    static const int kCounts[] = {0, 1, 2, 12, 64};
    for (size_t c = 0; c < sizeof(kCounts) / sizeof(kCounts[0]); c++)
        for (int admin = 0; admin < 2; admin++)
            for (int dev = 0; dev < 2; dev++)
                for (int p = HP_NONE; p <= HP_CANNOT + 1; p++) { /* one past: Play on Chipforge */
                    HallView w = View(kCounts[c], 0, 0.0f, admin, dev ? DEV_PAD : DEV_KEYBOARD);
                    w.play = p > HP_CANNOT ? HP_PLAY : (HallPlay)p;
                    w.tool = p > HP_CANNOT;
                    w.updateBehind = true;
                    Screens_Hall(&l, &w);
                    for (int i = 0; i < l.count; i++) {
                        const KitItem *a = &l.items[i];
                        if (a->id < WID_STRIP_PLAY || a->id > WID_STRIP_NEXT) continue;
                        if (a->r.x < 16 || a->r.x + a->r.w > KIT_SCREEN_W - 16 || a->r.y < 466 + 122 + 8 || a->r.y + a->r.h > KIT_SCREEN_H - 8)
                            tidy = false;
                        if (Kit_ChipWidth(a->key, (a->flags & KIT_PADCAP) != 0, a->label) > a->r.w && a->style == KIT_STYLE_CHIP) tidy = false;
                        for (int j = i + 1; j < l.count; j++)
                            if (l.items[j].id >= WID_STRIP_PLAY && l.items[j].id <= WID_STRIP_NEXT && Overlap(a->r, l.items[j].r)) tidy = false;
                    }
                    bool counter = Kit_Get(&l, WID_STRIP_PREV) && Kit_Get(&l, WID_STRIP_NEXT);
                    if (counter != (kCounts[c] > 0)) tidy = false;
                    if (counter && kCounts[c] == 1 && !(Kit_Get(&l, WID_STRIP_NEXT)->flags & KIT_DISABLED)) tidy = false;
                    if (counter && Kit_Get(&l, WID_STRIP_NEXT)->r.x + STRIP_ARROW_W != STRIP_RIGHT) tidy = false;
                }
    CHECK(tidy, "strip: every machine count, device, admin and play state (Chipforge's too) fits one tidy line under the card");
}

static void test_hall_status(void) {
    HallStatus st;
    HallStatusIn in = {.installed = true, .device = DEV_KEYBOARD, .error = "", .note = "", .installLog = "~/.local/share/ghost-launcher/install.log"};
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Press Enter to play") == 0 && st.size == 16 && st.blink && st.tone == HT_PLAY, "status: Press Enter to play, blinking");
    in.device = DEV_PAD;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Press A to play") == 0, "status: after pad use, Press A to play");
    in.device = DEV_KEYBOARD;
    in.installed = false;
    in.canInstall = true;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Press Enter to install") == 0 && st.tone == HT_INSTALL, "status: Press Enter to install");
    in.canInstall = false;
    Hall_Status(&in, &st);
    CHECK(st.tone == HT_DIM && st.size == 8 && Kit_TextWidth(st.text, 8) <= HALL_STATUS_W,
          "status: neither installed nor installable: says so, inside its column");
    in.installing = true;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Installing") == 0 && st.dots && Kit_TextWidth("Installing...", 16) <= HALL_STATUS_W, "status: Installing...");
    in.installing = false;
    in.installFailed = true;
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, "Install failed: ~/.local/share/ghost-launcher/install.log") == 0 && Kit_TextWidth(st.text, 8) <= HALL_STATUS_W &&
              st.tone == HT_BAD,
          "status: a failed install names its real log, and fits its column now");
    in.installLog = "~/a/rather/deep/folder/that/someone/chose/for/their/data/ghost-launcher/install.log";
    Hall_Status(&in, &st);
    CHECK(Kit_TextWidth(st.text, 8) <= HALL_STATUS_W && strncmp(st.text, "Install failed: ...", 19) == 0 &&
              strstr(st.text, "ghost-launcher/install.log") != NULL,
          "status: a long log path loses its start, never its file name");
    in.installFailed = false;
    in.installed = true;
    in.error = "Can't find /tmp/ghost-shots.AbCdEf/bin/a-machine-whose-program-has-a-very-long-name-indeed";
    Hall_Status(&in, &st);
    CHECK(st.tone == HT_BAD && Kit_TextWidth(st.text, 8) <= HALL_STATUS_W && strstr(st.text, "...") != NULL, "status: a long error is cut to its column");
    in.error = "";
    in.note = "Machine 5 has no daily challenge.";
    Hall_Status(&in, &st);
    CHECK(strcmp(st.text, in.note) == 0 && st.tone == HT_NOTE && !st.blink, "status: why Daily did nothing, in place of the prompt");

    char why[96];
    Hall_DailyReason("Blockfall", true, false, why, sizeof(why));
    CHECK(strcmp(why, "Blockfall has no daily challenge.") == 0, "daily: a machine without one says so");
    Hall_DailyReason("Skyraid", false, true, why, sizeof(why));
    CHECK(strstr(why, "Install") == why, "daily: a machine not installed yet says to install it");
    Hall_DailyReason("Skyraid", true, true, why, sizeof(why));
    CHECK(why[0] == '\0', "daily: and none where it works");
    Hall_DailyReason("A machine with a name far longer than the card", true, false, why, sizeof(why));
    CHECK(Kit_TextWidth(why, 8) <= HALL_STATUS_W, "daily: a long name still fits the line");

    char shown[128];
    Hall_Tilde("/home/trg/.local/share/ghost-launcher/install.log", "/home/trg", shown, sizeof(shown));
    CHECK(strcmp(shown, "~/.local/share/ghost-launcher/install.log") == 0, "tilde: the home folder is ~");
    Hall_Tilde("/home/trgx/install.log", "/home/trg", shown, sizeof(shown));
    CHECK(strcmp(shown, "/home/trgx/install.log") == 0, "tilde: only the whole home folder");
}

/* --------------------------------------------------------- name board */

static void test_nameboard_grid(void) {
    CHECK(NameBoard_Count() == 43, "board: A-Z, 0-9, . - _ !, Space, Del, Done");
    CHECK(NameBoard_CellFor('A') == 0 && NameBoard_Cell(NameBoard_CellFor('Z'))->row == 1 && NameBoard_Cell(NameBoard_CellFor('Z'))->col == 12 &&
              NameBoard_Cell(NameBoard_CellFor('9'))->row == 2 && NameBoard_CellFor('_') >= 0 && NameBoard_CellFor('!') >= 0 &&
              NameBoard_CellFor(' ') >= 0 && NameBoard_CellFor('a') == -1,
          "board: the letters where an arcade has them; capitals only");
    bool wide = true;
    for (int i = 0; i < NameBoard_Count(); i++) {
        const NameBoardCell *c = NameBoard_Cell(i);
        if ((c->kind != NBK_CHAR) != (c->span == 4)) wide = false;
    }
    CHECK(wide && NameBoard_Cell(NameBoard_Count() - 1)->kind == NBK_DONE, "board: Space, Del and Done are the wide keys, Done last");

    NameBoardPos p;
    NameBoard_Home(&p);
    CHECK(p.cell == 0, "board: starts on A");
    NameBoard_Move(&p, KIT_LEFT);
    CHECK(p.cell == NameBoard_CellFor('M'), "board: Left from A comes round to M");
    NameBoard_Move(&p, KIT_RIGHT);
    CHECK(p.cell == 0, "board: Right from M comes round to A");
    NameBoard_Move(&p, KIT_UP);
    CHECK(p.cell == NameBoard_CellFor('!'), "board: Up from A comes round to !");
    NameBoard_Move(&p, KIT_DOWN);
    NameBoard_Move(&p, KIT_DOWN);
    NameBoard_Move(&p, KIT_DOWN);
    CHECK(p.cell == NameBoard_CellFor('0'), "board: Down A, N, 0");
    NameBoard_Set(&p, NameBoard_CellFor('G'));
    NameBoard_Move(&p, KIT_UP);
    const NameBoardCell *c = NameBoard_Cell(p.cell);
    CHECK(c->kind == NBK_DELETE, "board: Up from G is Del (the wide key under it)");
    NameBoard_Move(&p, KIT_DOWN);
    CHECK(p.cell == NameBoard_CellFor('G'), "board: and Down again is G, not the key's first column");
    NameBoard_Set(&p, NameBoard_CellFor('!'));
    int seen[4];
    for (int i = 0; i < 4; i++) {
        NameBoard_Move(&p, KIT_RIGHT);
        seen[i] = NameBoard_Cell(p.cell)->kind;
    }
    CHECK(seen[0] == NBK_SPACE && seen[1] == NBK_DELETE && seen[2] == NBK_DONE && p.cell == NameBoard_CellFor('!'),
          "board: the bottom row: ! Space Del Done, then round");

    /* Under the name being typed, or over it near the bottom: never on it,
     * always on screen and above the hint line. */
    bool placed = true, cellsIn = true;
    for (float y = 160; y <= 520; y += 2) {
        KitRect field = {304, y, 336, 32};
        KitRect b = NameBoard_Panel(field);
        if (Overlap(b, field) || b.x < 0 || b.y < 0 || b.x + b.w > KIT_SCREEN_W || b.y + b.h > NB_BOTTOM) placed = false;
        for (int i = 0; i < NameBoard_Count(); i++) {
            KitRect r = NameBoard_CellRect(b, i);
            if (r.x < b.x || r.y < b.y || r.x + r.w > b.x + b.w || r.y + r.h > b.y + b.h) cellsIn = false;
            for (int j = i + 1; j < NameBoard_Count(); j++) if (Overlap(r, NameBoard_CellRect(b, j))) cellsIn = false;
        }
    }
    CHECK(placed, "board: under or over the name, never on it, never off screen");
    CHECK(cellsIn, "board: every cell inside the panel, none overlapping");
    KitRect arcade = NameBoard_Panel((KitRect){304, 186, 336, 32}), last = NameBoard_Panel((KitRect){304, 498, 336, 32});
    CHECK(arcade.y > 218 && last.y + last.h < 498, "board: under the arcade name, over the bottom name of the list");
}

void test_hall_all(void) {
    test_hall_row();
    test_hall_geometry();
    test_hall_jump_and_wheel();
    test_hall_pad_map();
    test_hall_keys();
    test_hall_clicks();
    test_hall_strip();
    test_hall_card_favourite();
    test_hall_status();
    test_nameboard_grid();
}
