#ifndef PALETTE_H
#define PALETTE_H

#include "raylib.h"

/* The launcher's colours: the arcade's indigo house palette, plus pink as
 * the launcher's one light colour (titles, the selection bar, a pressed
 * button). Shared by render.c and the button kit's drawing. */
extern const Color kWallTop, kWallBottom, kFloor, kBaseboard;
extern const Color kCabinet, kCabinetDark, kCabinetEdge, kScreenOff, kScreenOn;
extern const Color kInk, kWarmWhite, kText, kTextDim, kFaint, kPanel;
extern const Color kCoinRed, kPink, kTeal, kYellow, kCard;

/* a to b by t in [0, 1], opaque. */
Color Pal_Mix(Color a, Color b, float t);

#endif
