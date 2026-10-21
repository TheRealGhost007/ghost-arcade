#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>
#include "game.h"

/* Where the play screen puts things for a board of any size, and how text
 * that must stay inside the well is sized. No raylib: the measuring function
 * is passed in (the drawing passes Ui_Measure), so tests can check every
 * board from 6x12 to 24x36, in every mode.
 *
 * The panel goes at the side of the well for tall boards, or in a strip
 * across the top for flat ones, whichever gives bigger cells (the side on a
 * tie, so the classic board looks as it always did). Either way everything
 * has a rectangle of its own here, and the tests check that no two of them
 * overlap and that none leaves the window. */

#define LAYOUT_WINDOW_W 620
#define LAYOUT_WINDOW_H 720
#define LAYOUT_MARGIN 40
#define LAYOUT_FRAME 4      /* the well's frame, drawn outside the well */
#define LAYOUT_HEADER_Y 16  /* BLOCKFALL and the mode */
#define LAYOUT_FOOTER_Y 690 /* below this only the volume chip and F3's counter */
#define LAYOUT_MAX_CELL 30

/* The side placement: the well at (40, 56), at most 340 px wide (so the
 * panel keeps 160) and 606 tall. */
#define SIDE_BOARD_Y 56
#define SIDE_MAX_W 340
#define SIDE_MAX_H 606
#define SIDE_PANEL_MIN_W 160
#define SIDE_ROOMY_W 300 /* a panel this wide puts Next and Hold side by side */
/* The strip placement: the strip from y 48, the well under it (centred),
 * the controls card under the well. */
#define STRIP_Y 48
#define STRIP_BOARD_Y 156
#define STRIP_MAX_W 540
#define STRIP_MAX_H 440 /* leaves room under the well for the card in three columns */

typedef struct { int x, y, w, h; } LRect;
bool LRect_Empty(LRect r);
bool LRect_Overlaps(LRect a, LRect b);
bool LRect_Inside(LRect inner, LRect outer);

typedef enum { PLACE_SIDE, PLACE_STRIP } PanelPlace;

/* What the panel shows: four stats, the first the mode's main number
 * (Marathon, Power and Dig the score, Sprint the lines left, Ultra the time
 * left, Zen the fresh starts; Dig shows its Dug count: 3.4.0). */
typedef enum { STAT_SCORE, STAT_LEVEL, STAT_LINES, STAT_BEST, STAT_TIME, STAT_TIME_LEFT, STAT_LINES_LEFT, STAT_FRESH, STAT_DUG, STAT_UNDO, STAT_KINDS } StatKind;
#define HUD_STATS 4
typedef struct {
    StatKind stat[HUD_STATS];
    int mainChars; /* a typical main number's length: it is drawn at 32 px where that fits */
    int pipsStat;  /* the stat with the ten level pips under it, or -1 */
    bool chips;    /* combo and back-to-back (modern scoring) */
    bool bars;     /* Freeze and Slow (Power) */
    bool rise;     /* the next garbage row's warning bar (Dig, 3.4.0) */
    bool card;     /* the controls card is wanted */
    int queue;     /* small pieces shown after Next, 0 to QUEUE_SHOWN_MAX (3.2.0; 0: the Next box as it always was) */
} HudSpec;
HudSpec Layout_Spec(GameMode mode, bool modernScoring, bool showCard);
/* The spec for a game as it is: its mode's, and in the Practice room
 * (3.5.0) the pieces that can be taken back (STAT_UNDO, "Undos left") in
 * Best's place, since a practice run has no best. */
HudSpec Layout_SpecFor(const Game *g, bool showCard);
const char *Layout_StatLabel(StatKind k);

/* The controls card: ten rows (the eight play actions, with Pause's two
 * keys on one row since 3.0.0, then Mute, Volume), 18 px
 * apart; a row's pad buttons reach 4 px above its text and 12 below. */
#define CARD_ROWS 10
#define CARD_PITCH 18
#define CARD_ROW_BOTTOM 12
#define STATUS_PITCH 14
#define PREVIEW_CELL 16
#define PREVIEW_BOX_H 80
#define STRIP_BOX_W 96
/* The Next queue (3.2.0): after the large next piece, up to five small
 * ones. At the side they sit in a row under it, making the Next box
 * QUEUE_ROW_H taller (Hold and the stats move down by that); in the strip
 * they sit in two columns to its right, making the box QUEUE_STRIP_W wider.
 * Each takes a slot of 4x2 small cells; the cell shrinks from 8 px until
 * the slots fit (never under QUEUE_MIN_CELL). */
#define QUEUE_SHOWN_MAX 5
#define QUEUE_ROW_H 24
#define QUEUE_STRIP_W 52
#define QUEUE_CELL 8
#define QUEUE_MIN_CELL 4
#define QUEUE_GAP 4
#define QUEUE_PAD 8
#define STRIP_SLOT_W 135 /* a status item's slot in the strip's status row */

/* Where a piece's four blocks sit in a Next or Hold box: PREVIEW_CELL px
 * each, unturned, centred under the box's label (and above the power-up's
 * name when `named`). Each block's top-left, in the piece's cell order. The
 * panel draws them there, and Hold's flight lands there. */
void Layout_PreviewCells(LRect box, PieceType piece, bool named, int outX[4], int outY[4]);

typedef struct {
    PanelPlace place;
    int cell;                      /* block size in px */
    int boardX, boardY, boardW, boardH; /* the well's inside */
    LRect panel;                   /* the side panel or the strip */
    LRect next, hold;              /* the preview boxes (next holds the queue's slots too when there are any) */
    LRect nextBig;                 /* the part of the Next box the large piece is centred in (the whole box without a queue) */
    LRect queue;                   /* where the queue's small pieces go; empty without a queue */
    int queueSlots, queueCols, queueCell; /* how many slots, in how many columns, with cells this big */
    LRect stat[HUD_STATS];         /* label, value and (for one) the pips */
    int statSize[HUD_STATS];       /* the value's size: 32 or 16 */
    LRect combo, b2b, freeze, slow;/* status items; empty when the mode has none */
    LRect rise;                    /* Dig's "Next row" warning bar (3.4.0); empty elsewhere */
    bool showCard;
    LRect card;                    /* divider and rows; empty when not shown */
    int cardCols, cardRowsPerCol, cardRowY;
    LRect chip;                    /* the volume chip */
} PlayLayout;

/* The cell size each placement gives a board, and the placement taken. */
int Layout_SideCell(int w, int h);
int Layout_StripCell(int w, int h);
PanelPlace Layout_PlaceOf(int w, int h, int *cell);
PlayLayout Layout_Play(int w, int h, const HudSpec *spec);
/* The queue's slot i (0 = the piece after Next), a box of 4x2 small cells (3.2.0). */
LRect Layout_QueueSlot(const PlayLayout *l, int i);
/* Where a small piece's four blocks sit in slot i: queueCell px each,
 * unturned, centred in the slot. */
void Layout_QueueCells(const PlayLayout *l, int i, PieceType piece, int outX[4], int outY[4]);
LRect Layout_Well(const PlayLayout *l);      /* the well's inside */
LRect Layout_WellFrame(const PlayLayout *l); /* with its frame */
/* Row i of the controls card: its label's left edge and its keys' right
 * edge, and its text's top. */
void Layout_CardRow(const PlayLayout *l, int i, int *labelX, int *keysRight, int *y);

/* Press Start 2P is an 8x8 design, so text is drawn at 32, 16 or 8 px only.
 * A line that does not fit at one size tries the next, and at 16 or 8 it may
 * break in two at the space nearest the middle. */
typedef int (*MeasureFn)(const char *text, int size);
typedef struct {
    int size;   /* 32, 16 or 8 */
    int split;  /* index where the second line starts, 0 for one line */
    int width;  /* widest line, px */
    bool fits;  /* false: even 8 px in two lines is too wide (draw it clipped) */
} TextFit;
TextFit Layout_FitText(const char *text, int maxW, int maxSize, MeasureFn measure);
/* The height the fitted text takes (8 px between two lines). */
int Layout_FitHeight(TextFit f);

/* Callouts over the well (TRIPLE, COMBO 3, BACK-TO-BACK, LEVEL 4, ...): up
 * to three stacked lines, each on a plate, each at the biggest size that
 * fits the well (32, 16, two lines, 8; then its short form, B2B for
 * BACK-TO-BACK), the whole stack anchored 30% down the well and kept inside
 * it at every point of each line's 8 px rise. */
#define CALLOUT_MAX 3
#define CALLOUT_PAD_X 12
#define CALLOUT_PAD_Y 8
#define CALLOUT_GAP 4
#define CALLOUT_RISE 8
typedef struct {
    const char *text, *shortText; /* shortText may be NULL */
    int rise;                     /* 0..CALLOUT_RISE: how far it has risen */
    int maxSize;                  /* 32, or 16 for a line under the headline; 0 = 32 */
} CalloutText;
typedef struct {
    TextFit fit;
    bool useShort;
    int plateX, plateY, plateW, plateH, textY;
} CalloutPlace;
void Layout_Callouts(const PlayLayout *l, const CalloutText *lines, int count, MeasureFn measure, CalloutPlace *out);

/* A "+1 350" popup, centred on (cx, cy) in px when it starts and risen by
 * `rise` px, kept inside the well. */
#define POPUP_RISE 24
typedef struct { int size, x, y, w; } PopupPlace;
PopupPlace Layout_Popup(const PlayLayout *l, const char *text, int cx, int cy, int rise, MeasureFn measure);

/* The pause menu and the results card are centred on the whole window, so
 * they fit every board whatever its shape (pausemenu.h, results.h). */

/* ------------------------------------------- the menus' lines that must fit
 * (3.6.1). Where four menu screens put text whose width depends on the
 * words, so the tests can check every word the screens can show against
 * the 40..580 margin and against its neighbours, measured as the screens
 * measure (Ui_Measure). */

/* The main menu's "Your bests": a header line under Quit, then the five
 * modes (Marathon, Sprint, Ultra, Power, Dig) two to a line on three lines,
 * each label at the left of its column and its value right-aligned at the
 * column's right edge, all at 8 px. Until 3.6.1 they were three columns of
 * 160 px, too narrow for "Marathon" and "no run yet" side by side. The
 * block ends above the skyline at its tallest. */
#define MENU_ROWS_BOTTOM 508   /* Quit's bottom edge */
#define MENU_SKYLINE_TOP 568   /* SKY_BOTTOM - SKY_ROWS * SKY_CELL (backdrop.h) */
#define MENU_BESTS_X 60
#define MENU_BESTS_Y 516       /* the header */
#define MENU_BESTS_FIRST 16    /* from the header to the first modes' line */
#define MENU_BESTS_PITCH 14
#define MENU_BESTS_COLS 2
#define MENU_BESTS_COL_PITCH 280
#define MENU_BESTS_COL_W 220
#define MENU_BESTS_MODES 5
void Layout_MenuBest(int i, int *labelX, int *valueRight, int *y);

/* The Stats screen's three record tiles: a label, the value at 32 px and
 * under it the day it was set; under All modes the mode on its own line and
 * the day on the next (one line, "Marathon, 2026-10-18", ran into the next
 * tile until 3.6.1). Everything in a tile keeps STATS_TILE_W, so 8 px stay
 * clear before the next, and the third ends at 580. */
#define STATS_TILE_X0 40
#define STATS_TILE_PITCH 182
#define STATS_TILE_W 174
#define STATS_TILE_Y 432
#define STATS_TILE_VALUE_Y 448
#define STATS_TILE_SUB_Y 488
#define STATS_TILE_SUB_PITCH 14
#define STATS_RULE2_Y 520      /* the rule under the tiles: 512 until 3.6.1 */
int Layout_StatsTileX(int i);

/* The Practice room's rows: plates inside the margin (they ran from 20 to
 * 600 until 3.6.1), the label at the left at 16 px and a stepper ("< value
 * >", each arrow in its own KIT_ARROW_W cell) or an Off/On toggle at the
 * right, 8 px clear of the longest label and of the plate's right end. */
#define PRACTICE_ROW_X 40
#define PRACTICE_ROW_W 540
#define PRACTICE_LABEL_X 52
#define PRACTICE_STEPPER_X 346
#define PRACTICE_STEPPER_W 226
#define PRACTICE_TOGGLE_X 352
#define PRACTICE_TOGGLE_W 144

/* Play setup: the six mode rows, the Size header and its two rows on the
 * left; the right column ends at 580 (596 until 3.6.1). The two best lines
 * there are the word ("Your best", "World best"), then your place or who
 * holds the world best at SETUP_BEST_MID_X, then the value right-aligned
 * at the column's right edge. */
#define SETUP_LEFT_X 24
#define SETUP_LEFT_W 286
#define SETUP_ROW_H 32
#define SETUP_MODE_Y 136
#define SETUP_MODE_PITCH 36
#define SETUP_HEAD_DY 18      /* a header's text sits this far above its first row */
#define SETUP_SIZE_Y 376      /* 372 until 3.6.1, its header 6 px under Dig's row */
#define SETUP_SIZE_PITCH 36
#define SETUP_SCORES_Y 452    /* the Scores button, under Height */
#define SETUP_RIGHT_X 334
#define SETUP_RIGHT_W 246
#define SETUP_BESTS_Y 498
#define SETUP_BEST_MID_X 432  /* "World best" ends at 423 */
/* The words between a best line's word and its value, cut to fit 8 px
 * clear of the value: a name keeps as many letters as fit and ends in a
 * full stop ("The Real."), a place drops its " here" ("#3"); "" when not
 * even that fits. Returns the width it takes. */
int Layout_SetupMiddle(const char *middle, bool name, const char *value, MeasureFn measure, char *out, int size);

#endif
