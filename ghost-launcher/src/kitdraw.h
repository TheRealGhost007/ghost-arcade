#ifndef KITDRAW_H
#define KITDRAW_H

#include "kit.h"
#include "raylib.h"

/* The button kit's drawing: the five looks in the house style.
 *   normal   a dark bevelled block;
 *   hover    lighter, while the mouse is over it;
 *   focused  chosen with keys or the pad: the panel colour and a 4 px pink
 *            bar, never a size change;
 *   pressed  lit pink, the label sunk 2 px like an arcade button;
 *   disabled faint (the screen shows why).
 * Only Press Start 2P at 8 or 16 px. Call inside the logical frame. */

/* Every item of the list, in order, except areas and KIT_SELFDRAWN ones. */
void KitDraw_List(const KitView *kv);
/* One item with a given look: for a screen that draws an item in place,
 * and for the kit gallery. */
void KitDraw_Item(const KitItem *it, KitLook look, int hoverCell, int pressedCell);
/* The look of one item of the view (normal everywhere when state is NULL). */
KitLook KitDraw_LookOf(const KitView *kv, const KitItem *it);

/* The clock every animation reads (the pill's breath, the hall's lights and
 * demos): main.c sets it each frame and stops it while the hall rests. */
void KitDraw_SetClock(double seconds);
double KitDraw_Clock(void);
/* A favourite's 8x8 star at x, y, each pixel scale x scale: solid when on,
 * an outline when not. */
void KitDraw_Star(int x, int y, int scale, bool on, Color c);

#endif
