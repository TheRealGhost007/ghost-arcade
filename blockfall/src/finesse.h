#ifndef FINESSE_H
#define FINESSE_H

#include "board.h"
#include "tetromino.h"

/* Finesse (the Practice room's hints, 3.5.0, REWORK_PLAN.md "Practice
 * room"): the fewest presses that put a piece where it locked, counted the
 * way a finesse drill counts them, with no raylib so it is tested
 * headlessly.
 *  - A press is a tap of Left or Right (one cell), Left or Right held to
 *    the wall (one press, however far it slides), or a turn (clockwise or
 *    counter-clockwise, with the SRS kicks). Soft drop, hard drop and Hold
 *    are not presses, and a half turn is two (the optional Rotate 180 key is
 *    not assumed).
 *  - The moves are made at the top, where the piece comes in, and the piece
 *    is then dropped straight down. What lands on exactly the cells the
 *    piece locked on counts, whatever its rotation number (an S lying
 *    flat is the same shape either way up).
 *  - A placement that needs the stack (a tuck under an overhang, a spin
 *    into a slot) cannot be reached that way: there is no hint for it.
 * Game.presses is the count playstep.c keeps as the player moves. */

/* The fewest presses from the spawn to a hard drop that leaves the piece
 * on the cells of (type, rotation, x, y) on this board, or -1 when no such
 * sequence exists (the board as it is BEFORE the piece is stamped). */
int Finesse_Optimal(const Board *b, PieceType type, int rotation, int x, int y);

/* How the hint reads: "2 extra presses", "1 extra press" (extra > 0);
 * "" for 0 or none. shortText: "+2". */
void Finesse_Words(int extra, char *out, int size, char *shortOut, int shortSize);

#endif
