/* The play screen on every board from 6x12 to 24x36 (475 sizes), in every
 * mode, with the controls card shown and hidden: the panel goes at the side
 * or in a strip across the top, whichever gives bigger cells; nothing
 * overlaps and nothing leaves the window; every callout, popup and pause
 * line fits the well; the card keeps its rows readable. The font is
 * measured as Press Start 2P measures (Ui_Measure: size px a glyph, one font
 * pixel between glyphs). */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/layout.h"
#include "uikit.h"

static int Measure(const char *text, int size) {
    int n = (int)strlen(text);
    return n ? n * size + (n - 1) * (size / 8) : 0;
}

static const LRect kWindow = {0, 0, LAYOUT_WINDOW_W, LAYOUT_FOOTER_Y};

/* Every rectangle of the play screen but the well, named for the messages. */
static int Rects(const PlayLayout *l, LRect *out, const char **names) {
    int n = 0;
    out[n] = (LRect){LAYOUT_MARGIN, LAYOUT_HEADER_Y, 330, 16}; names[n++] = "header";
    out[n] = l->next; names[n++] = "next";
    out[n] = l->hold; names[n++] = "hold";
    for (int i = 0; i < HUD_STATS; i++) { out[n] = l->stat[i]; names[n++] = "stat"; }
    out[n] = l->combo; names[n++] = "combo";
    out[n] = l->b2b; names[n++] = "b2b";
    out[n] = l->freeze; names[n++] = "freeze";
    out[n] = l->slow; names[n++] = "slow";
    out[n] = l->card; names[n++] = "card";
    return n;
}

static const char *const kCallouts[][3] = {
    {"QUAD", "BACK-TO-BACK", "COMBO 12"},
    {"TRIPLE", "COMBO 9", "LEVEL 99"},
    {"DOUBLE", "FREEZE", "NEW BEST"},
    {"FRESH START", "LEVEL 12", "SWEEP"},
    {"BACK-TO-BACK", "NEW BEST", "FRESH START"},
};
/* The widest words the controls card can hold (Controls_ShortName). */
static const char *const kCardKeys[CARD_ROWS] = {"LShift RShift", "RShift", "RShift", "RShift", "RShift", "RShift", "LShift Esc", "RShift", "M", "- ="};
static const char *const kCardLabels[CARD_ROWS] = {"Move", "Soft drop", "Rotate", "Rotate left", "Hard drop", "Hold", "Pause", "Restart", "Mute", "Volume"};
/* The widest pad caps a row can show: two pills, or Menu and two more. */
static const char *const kCardPads[CARD_ROWS][3] = {
    {"Right", "Right", NULL}, {"LB", "RB", NULL}, {"LB", "RB", NULL}, {"LB", "RB", NULL}, {"LB", "RB", NULL},
    {"LB", "RB", NULL},       {"Menu", "LB", "RB"}, {"LB", "RB", NULL}, {NULL, NULL, NULL}, {NULL, NULL, NULL},
};
static int PadCapsWidth(int row) {
    int w = 0;
    for (int i = 0; i < 3 && kCardPads[row][i]; i++) w += Kit_CapWidth(kCardPads[row][i], true) + (i ? 4 : 0);
    return w;
}

static bool CalloutsFit(const PlayLayout *l, const char *const *texts, int count) {
    CalloutText lines[CALLOUT_MAX];
    CalloutPlace at[CALLOUT_MAX];
    for (int rise = 0; rise <= CALLOUT_RISE; rise++) {
        for (int i = 0; i < count; i++) lines[i] = (CalloutText){texts[i], strcmp(texts[i], "BACK-TO-BACK") == 0 ? "B2B" : NULL, rise, 0};
        Layout_Callouts(l, lines, count, Measure, at);
        for (int i = 0; i < count; i++) {
            if (!at[i].fit.fits || at[i].fit.width > at[i].plateW) return false;
            LRect plate = {at[i].plateX, at[i].plateY, at[i].plateW, at[i].plateH};
            if (!LRect_Inside(plate, Layout_Well(l))) return false;
            if (at[i].textY < at[i].plateY || at[i].textY + Layout_FitHeight(at[i].fit) > at[i].plateY + at[i].plateH) return false;
            for (int j = 0; j < i; j++) {
                LRect other = {at[j].plateX, at[j].plateY, at[j].plateW, at[j].plateH};
                if (rise == CALLOUT_RISE && LRect_Overlaps(plate, other)) return false;
            }
        }
    }
    return true;
}

static bool PopupsFit(const PlayLayout *l) {
    /* A lock earns at most a few tens of thousands (a back-to-back quad with
     * a long combo at level 20 is about 40 000). */
    static const char *const kTexts[] = {"+100", "+1 350", "+12 000", "+99 999"};
    for (size_t t = 0; t < sizeof(kTexts) / sizeof(kTexts[0]); t++) {
        int xs[3] = {l->boardX, l->boardX + l->boardW / 2, l->boardX + l->boardW}, ys[3] = {l->boardY, l->boardY + l->boardH / 2, l->boardY + l->boardH};
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                for (int rise = 0; rise <= POPUP_RISE; rise += 4) {
                    PopupPlace p = Layout_Popup(l, kTexts[t], xs[a], ys[b], rise, Measure);
                    LRect plate = {p.x - 4, p.y - 4, p.w + 8, p.size + 8}; /* drawn on a plate */
                    if (!LRect_Inside(plate, Layout_Well(l))) return false;
                }
    }
    return true;
}

static void test_every_board(void) {
    int bad = 0, badOverlap = 0, badPanel = 0, badCard = 0, badCardText = 0, badStat = 0, badCallout = 0, badPopup = 0,
        badPlace = 0, cases = 0;
    char first[160] = "";
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++) {
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++) {
            int side = Layout_SideCell(w, h), strip = Layout_StripCell(w, h);
            for (int mode = 0; mode < MODE_COUNT; mode++) {
                for (int modern = 0; modern <= 1; modern++) {
                    for (int card = 0; card <= 1; card++) {
                        cases++;
                        HudSpec spec = Layout_Spec((GameMode)mode, modern, card);
                        PlayLayout l = Layout_Play(w, h, &spec);
                        if (l.cell != (strip > side ? strip : side) || (l.place == PLACE_STRIP) != (strip > side)) badPlace++;
                        LRect frame = Layout_WellFrame(&l);
                        if (l.cell < 14 || !LRect_Inside(frame, kWindow) || frame.y < LAYOUT_HEADER_Y + 16 + 4) bad++;

                        LRect r[16];
                        const char *names[16];
                        int n = Rects(&l, r, names);
                        for (int i = 0; i < n; i++) {
                            bool ok = LRect_Inside(r[i], kWindow) && !LRect_Overlaps(r[i], frame) && !LRect_Overlaps(r[i], l.chip);
                            for (int j = 0; j < i && ok; j++) ok = !LRect_Overlaps(r[i], r[j]);
                            if (!ok) {
                                badOverlap++;
                                if (!first[0]) snprintf(first, sizeof(first), "%dx%d mode %d: %s", w, h, mode, names[i]);
                            }
                            /* Everything but the card (under the well in the strip) lives in the panel. */
                            if (i > 0 && strcmp(names[i], "card") != 0 && !LRect_Inside(r[i], l.panel)) badPanel++;
                        }
                        if (card && !l.showCard) badCard++;
                        if (!card && (l.showCard || !LRect_Empty(l.card))) badCard++;
                        if (l.showCard) {
                            if (l.cardCols > 3) badCard++;
                            for (int i = 0; i < CARD_ROWS; i++) {
                                int x, right, y;
                                Layout_CardRow(&l, i, &x, &right, &y);
                                if (Measure(kCardLabels[i], 8) + 8 + Measure(kCardKeys[i], 8) > right - x) badCardText++;
                                if (Measure(kCardLabels[i], 8) + 8 + PadCapsWidth(i) > right - x) badCardText++;
                                if (y < l.card.y + 12 || y + 8 > l.card.y + l.card.h) badCardText++;
                                /* the pad's caps, 16 px tall, centred on the row */
                                if (y - 4 < l.card.y + 2 || y + CARD_ROW_BOTTOM > l.card.y + l.card.h || l.card.y + l.card.h > LAYOUT_FOOTER_Y) badCardText++;
                            }
                        }
                        for (int i = 0; i < HUD_STATS; i++) {
                            /* the widest values still fit at 8 px at least */
                            if (Measure("No best yet", 8) > l.stat[i].w || Measure("9 999 999", 8) > l.stat[i].w) badStat++;
                            if (l.statSize[i] != 16 && l.statSize[i] != 32) badStat++;
                        }
                        if (!LRect_Empty(l.combo) && Measure("Combo x99", 8) + 6 + 3 * 6 + 4 > l.combo.w) badStat++; /* four 4 px dots, 6 apart */
                        if (!LRect_Empty(l.freeze) && Measure("Freeze", 8) + 8 + 24 > l.freeze.w) badStat++;
                        if (spec.chips != !LRect_Empty(l.combo) || spec.bars != !LRect_Empty(l.slow)) badStat++;
                        if (mode != 0 || modern || !card) continue; /* the text checks need each board once */

                        for (size_t k = 0; k < sizeof(kCallouts) / sizeof(kCallouts[0]); k++)
                            for (int count = 1; count <= 3; count++)
                                if (!CalloutsFit(&l, kCallouts[k], count)) badCallout++;
                        if (!PopupsFit(&l)) badPopup++;
                    }
                }
            }
        }
    }
    if (first[0]) printf("  first overlap: %s\n", first);
    CHECK(cases == 475 * MODE_COUNT * 4, "layout: every board, mode, scoring and card setting was checked");
    CHECK(badPlace == 0, "layout: the panel goes where the cells come out bigger (the side on a tie)");
    CHECK(bad == 0, "layout: on every board the well and its frame stay inside the window, under the header, cells 14 px or more");
    CHECK(badOverlap == 0, "layout: nothing overlaps (header, Next, Hold, stats, status, card, well, volume chip) and nothing leaves the window");
    CHECK(badPanel == 0, "layout: Next, Hold, the stats and the status items stay in the panel or strip");
    CHECK(badCard == 0, "layout: the controls card shows on every board when wanted (three columns at most), never when hidden");
    CHECK(badCardText == 0, "layout: every card row holds its label and the widest keys, and the pad's caps");
    CHECK(badStat == 0, "layout: stat values, the combo chip and the bars have room");
    CHECK(badCallout == 0, "layout: one to three callouts fit inside the well on every board, at every point of their rise");
    CHECK(badPopup == 0, "layout: score popups stay inside the well on every board, wherever they start");
    /* The pause menu and the results card are centred on the window, the
     * same on every board (tests/test_around.c). */
}

static void test_bigger_cells(void) {
    int bigger = 0, strips = 0;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++) {
            int cell = 0;
            if (Layout_PlaceOf(w, h, &cell) == PLACE_STRIP) strips++;
            if (cell > Layout_SideCell(w, h)) bigger++;
        }
    printf("  layout: %d of 475 boards get bigger cells from the strip\n", bigger);
    CHECK(bigger == strips && bigger >= 140, "layout: about a third of the boards get bigger cells from the strip, and only those use it");
    int cell = 0;
    CHECK(Layout_PlaceOf(24, 12, &cell) == PLACE_STRIP && cell == 22 && Layout_SideCell(24, 12) == 14, "layout: 24x12 goes from 14 px to 22 px cells");
    CHECK(Layout_PlaceOf(24, 20, &cell) == PLACE_STRIP && cell == 22, "layout: 24x20 gets 22 px cells too");
    CHECK(Layout_PlaceOf(16, 16, &cell) == PLACE_STRIP && cell == 27, "layout: 16x16 grows from 21 px to 27 px");
    CHECK(Layout_PlaceOf(10, 20, &cell) == PLACE_SIDE && cell == 30, "layout: the classic board stays at the side, 30 px");
    CHECK(Layout_PlaceOf(6, 12, &cell) == PLACE_SIDE && cell == 30, "layout: the tiniest board stays at the side");
    CHECK(Layout_PlaceOf(24, 36, &cell) == PLACE_SIDE && cell == 14, "layout: the biggest board stays at the side (a tie)");
    CHECK(Layout_PlaceOf(14, 20, &cell) == PLACE_SIDE && cell == 24, "layout: Wide 14x20 stays at the side");
    CHECK(Layout_PlaceOf(18, 30, &cell) == PLACE_SIDE && cell == 18, "layout: Huge 18x30 stays at the side");
    CHECK(Layout_PlaceOf(8, 26, &cell) == PLACE_SIDE && cell == 23, "layout: Tall 8x26 stays at the side");
}

static void test_classic_look_kept(void) {
    /* The classic board's well and panel are where they always were. */
    HudSpec spec = Layout_Spec(MODE_MARATHON, false, true);
    PlayLayout l = Layout_Play(10, 20, &spec);
    CHECK(l.place == PLACE_SIDE && l.cell == 30 && l.boardX == 40 && l.boardY == 56 && l.panel.x == 380 && l.panel.w == 200,
          "layout: the classic board is unchanged");
    CHECK(l.next.x == 380 && l.next.y == 52 && l.next.w == 200 && l.next.h == 80 && l.hold.y == 140, "layout: Next and Hold where they were");
    int x, right, y;
    Layout_CardRow(&l, CARD_ROWS - 1, &x, &right, &y);
    CHECK(l.showCard && x == 380 && right == 580 && y + 8 == 56 + 600 - 12, "layout: the card's last row ends 12 px above the well's bottom");
    CHECK(l.statSize[0] == 16 && l.stat[0].y == 236, "layout: the classic score is 16 px (32 would not fit 200 px)");
    CHECK(LRect_Empty(l.combo) && LRect_Empty(l.freeze), "layout: the classic Marathon has no combo or power-up lines");

    /* Ultra's countdown is its big number. */
    spec = Layout_Spec(MODE_ULTRA, true, true);
    l = Layout_Play(10, 20, &spec);
    CHECK(spec.stat[0] == STAT_TIME_LEFT && l.statSize[0] == 32, "layout: Ultra's countdown is 32 px");
    spec = Layout_Spec(MODE_SPRINT, true, true);
    CHECK(spec.stat[0] == STAT_LINES_LEFT && spec.stat[1] == STAT_TIME, "layout: Sprint shows lines left and the clock");
    spec = Layout_Spec(MODE_ZEN, true, true);
    CHECK(spec.stat[0] == STAT_FRESH && spec.pipsStat < 0, "layout: Zen shows its fresh starts first, and no pips");

    /* A tiny board's roomy panel: Next and Hold side by side, a big score. */
    spec = Layout_Spec(MODE_MARATHON, true, true);
    l = Layout_Play(6, 12, &spec);
    CHECK(l.panel.w >= SIDE_ROOMY_W && l.next.y == l.hold.y && l.next.x < l.hold.x && l.statSize[0] == 32,
          "layout: a tiny board gets a roomy panel with a 32 px score");
    /* A flat board: the strip, the well centred under it, the card under the well. */
    l = Layout_Play(24, 12, &spec);
    CHECK(l.place == PLACE_STRIP && l.boardX + l.boardW / 2 == LAYOUT_WINDOW_W / 2 && l.card.y > l.boardY + l.boardH &&
          l.next.y == STRIP_Y && l.stat[0].y == STRIP_Y,
          "layout: on 24x12 the panel is a strip across the top and the card sits under the well");
    spec.card = false;
    l = Layout_Play(24, 12, &spec);
    CHECK(!l.showCard && LRect_Empty(l.card), "layout: a hidden card takes no room");
}

static void test_callout_details(void) {
    HudSpec spec = Layout_Spec(MODE_MARATHON, false, true);
    PlayLayout l = Layout_Play(10, 20, &spec);
    CalloutText one = {"QUAD", NULL, CALLOUT_RISE, 0};
    CalloutPlace at[3];
    Layout_Callouts(&l, &one, 1, Measure, at);
    CHECK(at[0].fit.size == 32 && at[0].plateH == 48 && at[0].plateY == 56 + 180 && at[0].textY == at[0].plateY + 8,
          "callouts: QUAD on the classic well is 32 px, 30% of the way down");
    one.rise = 0;
    Layout_Callouts(&l, &one, 1, Measure, at);
    CHECK(at[0].plateY == 56 + 180 + CALLOUT_RISE, "callouts: a new line starts 8 px lower and rises");
    CalloutText three[3] = {{"QUAD", NULL, 8, 0}, {"BACK-TO-BACK", "B2B", 8, 16}, {"COMBO 3", NULL, 8, 16}};
    Layout_Callouts(&l, three, 3, Measure, at);
    CHECK(at[1].plateY == at[0].plateY + at[0].plateH + CALLOUT_GAP && at[2].plateY == at[1].plateY + at[1].plateH + CALLOUT_GAP,
          "callouts: three stack top to bottom with a small gap");
    CHECK(at[0].fit.size == 32 && !at[1].useShort && at[1].fit.size == 16 && at[2].fit.size == 16,
          "callouts: the headline at 32 px, the lines under it at 16 at most");

    PlayLayout narrow = Layout_Play(6, 36, &spec);
    Layout_Callouts(&narrow, three, 3, Measure, at);
    CHECK(narrow.boardW == 96 && at[1].useShort && at[1].fit.fits, "callouts: on the 96 px well of 6x36 BACK-TO-BACK becomes B2B");
    CalloutText fresh = {"FRESH START", NULL, 8, 0};
    Layout_Callouts(&narrow, &fresh, 1, Measure, at);
    CHECK(at[0].fit.fits && at[0].fit.split > 0 && at[0].fit.size == 8, "callouts: FRESH START breaks into two lines there");

    PopupPlace p = Layout_Popup(&l, "+1 350", 40 + 150, 56 + 590, 0, Measure);
    CHECK(p.size == 16 && p.y + p.size <= 56 + 600 - 4, "popups: one on the bottom row is lifted inside the well");
    PopupPlace q = Layout_Popup(&l, "+1 350", 40 + 150, 56 + 300, 12, Measure);
    CHECK(q.y == 56 + 300 - 8 - 12 && q.x == 40 + 150 - Measure("+1 350", 16) / 2, "popups: centred where they start, rising");
}

static void test_fit_text(void) {
    TextFit f = Layout_FitText("PAUSED", 400, 32, Measure);
    CHECK(f.size == 32 && f.split == 0 && f.fits, "fit: 32 px when it fits");
    f = Layout_FitText("FRESH START", 200, 32, Measure);
    CHECK(f.size == 16 && f.split == 0, "fit: 16 px next");
    f = Layout_FitText("FRESH START", 100, 32, Measure);
    CHECK(f.size == 16 && f.split == 6 && f.width <= 100, "fit: then two lines at 16 px, split at the space");
    CHECK(Layout_FitHeight(f) == 40, "fit: two lines take 16 + 8 + 16 px");
    f = Layout_FitText("FRESHSTART", 100, 32, Measure);
    CHECK(f.size == 8 && f.split == 0 && f.fits, "fit: a word that cannot break drops to 8 px");
    f = Layout_FitText("QUAD", 400, 16, Measure);
    CHECK(f.size == 16, "fit: never above the size asked for");
}

void test_layout_all(void) {
    test_every_board();
    test_bigger_cells();
    test_classic_look_kept();
    test_callout_details();
    test_fit_text();
}
