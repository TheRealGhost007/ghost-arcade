#ifndef PRACTICE_H
#define PRACTICE_H

#include <stdbool.h>
#include "game.h"

/* The Practice room's undo (3.5.0, REWORK_PLAN.md "Practice room"), with no
 * raylib so it is tested headlessly. The whole Game is plain data (the
 * board, the bag and its rng, the queue, Hold, the score, the level, the
 * clocks), so a copy of it is the state exactly, and putting it back is an
 * undo that cannot drift: the next pieces come as they would have.
 *  - The stack holds the state at the start of each piece: the run's start
 *    first, then one more as each piece locks (Undo_Push on the frame
 *    justLocked is set; the copy's one-frame flags are cleared, so the lock
 *    is not replayed when it comes back).
 *  - Undo (Undo_Pop) throws the newest start away and puts the one before
 *    it back: the last piece is un-locked and comes in again, untouched.
 *    With no piece locked since the start there is nothing to take back.
 *  - PRACTICE_UNDO_MAX pieces can be taken back in a row; older starts fall
 *    off the far end. */

#define UNDO_SLOTS (PRACTICE_UNDO_MAX + 1)

typedef struct {
    Game past[UNDO_SLOTS]; /* a ring: the starts kept, oldest to newest */
    int count;             /* starts kept, 1 .. UNDO_SLOTS once a run began */
    int head;              /* where the oldest is */
    int undone;            /* undos made this run (the panel's count) */
} UndoStack;

/* A run (or a restart) begins: its start is the one state kept. */
void Undo_Reset(UndoStack *u, const Game *g);
/* A piece locked: the state now is the next piece's start. */
void Undo_Push(UndoStack *u, const Game *g);
/* Takes the last piece back; false when there is none to take. On true
 * *g is the start of that piece again, with justUndid set for the frame. */
bool Undo_Pop(UndoStack *u, Game *g);
/* Pieces that can still be taken back, 0 .. PRACTICE_UNDO_MAX. */
int Undo_Left(const UndoStack *u);

#endif
