#ifndef RENDERPRIV_H
#define RENDERPRIV_H

/* Shared by the play screen's four drawing files only:
 *   render.c   the frame: header, the cards over it, the volume chip;
 *   well.c     the well: frame, grid, the stack (and a clear replaying),
 *              the landed piece, the ghost and the falling piece;
 *   effects.c  what happens in it: blasts, shake, the well's pixels,
 *              callouts, popups, READY and GO (render-only reactions to the
 *              rules' one-frame events);
 *   panel.c    the panel or strip: Next, Hold (and the piece flying into
 *              it), the stats, the status items and the controls card.
 * Where everything goes comes from layout.c, what is lively from hudfx.c
 * and how play moves from feel.c, all raylib-free and tested. */

#include "raylib.h"
#include "render.h"
#include "layout.h"
#include "hudfx.h"
#include "feel.h"
#include "ui.h"
#include <stdint.h>

/* The shared palette lives in ghost-common's ui.c; these just read it. This
 * window is the screen of the Blockfall cabinet in Ghost Launcher's arcade
 * hall, so it lives in the hall's indigo; violet is the machine's own light. */
#define kBg (Ui_Theme()->bg)
#define kBoardBg (Ui_Theme()->board)
#define kPanel (Ui_Theme()->panel)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light) /* this game's own colour */
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)
#define kGrid ((Color){26, 21, 54, 255})
#define kPlate ((Color){13, 10, 30, 215}) /* under text drawn over the well */

Color Rp_ColorFromU32(uint32_t c);
Color Rp_PowerColor(PowerUp p);
const char *Rp_PowerGlyph(PowerUp p);
/* A power-up's mark on a block: a light core, its colour round it and its
 * letter (B, L, F, S, W), steady with Reduced flashing. grey: the box it
 * sits in is spent (a used Hold). */
void Rp_PowerMark(int x, int y, int size, PowerUp p, bool grey);
/* Text the layout fitted (one line or two), centred on cx. */
void Rp_DrawFitted(const char *text, TextFit f, int cx, int y, Color c);
/* A board cell's top-left, row counted with the hidden rows. */
int Rp_CellX(const PlayLayout *l, int col);
int Rp_CellY(const PlayLayout *l, int row);

/* well.c */
/* The frame (its level-up and quad flashes, the ghost's row marks), the
 * well and its grid. ghost: GHOST_OFF, GHOST_OUTLINE or GHOST_SOLID; grid:
 * GRID_ON, GRID_FAINT or GRID_OFF (persist.h). */
void Well_DrawFrame(const Game *g, const PlayLayout *l, const HudFx *hud, const Feel *f, int ghost, int grid);
/* Inside the well's clip: the stack or the clear replaying, the landed
 * piece, the ghost and the falling piece. */
void Well_DrawInside(const Game *g, const PlayLayout *l, const Feel *f, int ghost);
/* The grid's colour for a GRID_* look (the well's GridOver uses it too). */
Color Well_GridColor(int grid);
/* The end show over the stack (results.h): the blocked piece blinking red
 * and the rows going grey, a Sprint clear's rows gone, time up's dim. */
void Well_DrawEnd(const Game *g, const PlayLayout *l, const Results *end);

/* effects.c */
void Effects_Reset(void);
/* After Feel_React (a hard drop's dust is thrown from where it landed). */
void Effects_React(const Game *g, const PlayLayout *l, const Feel *f);
/* After Feel_Tick (the cleared cells that zipped out burst). */
void Effects_Tick(float dt, const PlayLayout *l, const Feel *f);
/* A Sprint clear's end show: each row that goes bursts upward in its
 * blocks' colours (NULL: no end yet). */
void Effects_EndShow(const Game *g, const PlayLayout *l, const Results *end);
/* The well's shake this frame (whole pixels, 0 with Screen shake off). */
int Effects_ShakeY(void);
/* Inside the well's clip: tints, blasts and the pixels. */
void Effects_DrawInWell(const Game *g, const PlayLayout *l, float dt);
/* The callouts and popups (while the run is on). */
void Effects_DrawCallouts(const PlayLayout *l, const HudFx *hud);
/* READY, GO, or the countdown's 3, 2, 1 over the well. */
void Effects_DrawCue(const PlayLayout *l, const StartCue *cue, const HudFx *hud);
/* The well's pixels in flight (for the harness). */
int Effects_Bits(void);

/* panel.c */
void Panel_Draw(const Game *g, const RenderView *v, const PlayLayout *l, const HudFx *hud, const Feel *f);
/* The piece flying from the well into Hold, over everything. */
void Panel_DrawFlight(const Game *g, const PlayLayout *l, const Feel *f);

#endif
