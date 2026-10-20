#ifndef LOOK_H
#define LOOK_H

#include <stdbool.h>
#include "tetromino.h"

/* Block patterns (Settings > Look > Block patterns): a mark of its own on
 * every block of each piece, so the pieces can be told apart without their
 * colours (for colour-blind players). Every screen that draws a piece's
 * blocks asks here: the well, Next and Hold, the piece flying into Hold,
 * How to play and the Feel tab's strip. No raylib.
 *
 * Each mark is a 5x5 pattern drawn over the block's middle in a darker
 * shade of its colour:
 *   I a bar across     O a ring          T a T
 *   S a rising line    Z a falling line  J a bar down     L four dots */

#define LOOK_PATTERN_N 5

void Look_SetPatterns(bool on);
bool Look_Patterns(void);
/* The pattern's cell (x, y), 0..4 from the top left. */
bool Look_PatternCell(PieceType t, int x, int y);

#endif
