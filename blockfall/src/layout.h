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
 * (Marathon and Power the score, Sprint the lines left, Ultra the time left,
 * Zen the fresh starts). */
typedef enum { STAT_SCORE, STAT_LEVEL, STAT_LINES, STAT_BEST, STAT_TIME, STAT_TIME_LEFT, STAT_LINES_LEFT, STAT_FRESH, STAT_KINDS } StatKind;
#define HUD_STATS 4
typedef struct {
    StatKind stat[HUD_STATS];
    int mainChars; /* a typical main number's length: it is drawn at 32 px where that fits */
    int pipsStat;  /* the stat with the ten level pips under it, or -1 */
    bool chips;    /* combo and back-to-back (modern scoring) */
    bool bars;     /* Freeze and Slow (Power) */
    bool card;     /* the controls card is wanted */
} HudSpec;
HudSpec Layout_Spec(GameMode mode, bool modernScoring, bool showCard);
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
    LRect next, hold;              /* the preview boxes */
    LRect stat[HUD_STATS];         /* label, value and (for one) the pips */
    int statSize[HUD_STATS];       /* the value's size: 32 or 16 */
    LRect combo, b2b, freeze, slow;/* status items; empty when the mode has none */
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

#endif
