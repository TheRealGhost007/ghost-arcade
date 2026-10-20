#ifndef COMFORT_H
#define COMFORT_H

#include <stdbool.h>

/* The two comfort switches every effect asks, so no new effect can forget
 * them (REWORK_PLAN.md, "Comfort switches"):
 *  - Motion: Full or Reduced (Blockfall's own setting, config.ini's
 *    reduced_motion). Reduced turns slides, glides and drops into plain
 *    cuts, and halves the particles. Screen shake stays its own switch.
 *  - Reduced flashing (the arcade-wide setting in prefs.ini): no white
 *    flashes, and blinking things hold steady.
 * No raylib. */

void Comfort_SetReducedMotion(bool reduced);
/* Full motion: things may slide, glide and drop. */
bool Comfort_Motion(void);
/* Reduced flashing is on: no white flashes, nothing blinks. */
bool Comfort_Calm(void);
/* How many particles to make where n would be made: half with Reduced
 * motion (at least one when n is). */
int Comfort_Particles(int n);
/* Piece motion (Blockfall's Look setting, config.ini's piece_snap): Smooth
 * lets a tap or a gravity step glide; Snap moves the piece cell by cell.
 * Reduced motion snaps too. */
void Comfort_SetPieceSnap(bool snap);
bool Comfort_PieceGlide(void);

#endif
