#ifndef UIKITDRAW_H
#define UIKITDRAW_H

#include "uikit.h"
#include "raylib.h"

/* The button kit's drawing (uikit.h), in the house style and the game's own
 * light colour (Ui_Theme()->light: Blockfall's violet):
 *   normal   a dark bevelled block;
 *   hover    lighter, while the mouse is over it;
 *   focused  chosen with keys or the pad: the panel colour and a 4 px bar
 *            in the light colour, never a size change;
 *   pressed  lit in the light colour, the label sunk 2 px like an arcade
 *            button (and for KIT_FLASH_FRAMES as it acts);
 *   disabled faint (the screen says why).
 * Only Press Start 2P at 8 or 16 px, through ui.h. Call inside the frame
 * (Win_BeginFrame .. Win_EndFrame). Opt-in; a copy of Ghost Launcher's
 * src/kitdraw.c recoloured by the theme. */

/* Every item of the list, in order, except areas and KIT_SELFDRAWN ones.
 * With kv->plate set, the focus plate is drawn there (gliding) under the
 * focused row, button or stepper instead of on it. */
void KitDraw_List(const KitView *kv);
/* One item with a given look: for a screen that draws an item in place. */
void KitDraw_Item(const KitItem *it, KitLook look, int hoverCell, int pressedCell);
/* The look of one item of the view (normal everywhere when state is NULL). */
KitLook KitDraw_LookOf(const KitView *kv, const KitItem *it);
/* The focus plate alone: the panel colour and the 4 px bar. */
void KitDraw_Plate(KitRect r);

/* A line of hint chips centred on cx, their middle at cy: each its caps
 * (keyboard keys, or pad buttons when the pad was used last) and its word. */
void KitDraw_Hints(const KitHint *h, int count, int cx, int cy, InputDevice device);
/* One key cap or pad button, x its left edge, cy its middle. */
void KitDraw_Cap(const char *cap, bool pad, int x, int cy);

/* The "are you sure?" box over the dimmed screen: its title, two lines and
 * its two buttons (kv's list from Kit_DialogLayout). */
void KitDraw_Dialog(const KitDialog *d, const KitView *kv);

/* A favourite's 8x8 star at x, y, each pixel scale x scale: solid when on,
 * an outline when not. */
void KitDraw_Star(int x, int y, int scale, bool on, Color c);

#endif
