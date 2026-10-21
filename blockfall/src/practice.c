#include "practice.h"
#include <string.h>

static Game *Slot(UndoStack *u, int i) { return &u->past[(u->head + i) % UNDO_SLOTS]; }

/* The state as a start: nothing from the frame it was taken on. */
static void Keep(Game *dst, const Game *g) {
    *dst = *g;
    Game_ConsumeFrameFlags(dst);
}

void Undo_Reset(UndoStack *u, const Game *g) {
    u->count = 1;
    u->head = 0;
    u->undone = 0;
    Keep(&u->past[0], g);
}

void Undo_Push(UndoStack *u, const Game *g) {
    if (u->count <= 0) { Undo_Reset(u, g); return; }
    if (u->count < UNDO_SLOTS) {
        Keep(Slot(u, u->count), g);
        u->count++;
    } else {
        /* Full: the oldest start makes room (it can no longer be reached). */
        Keep(Slot(u, 0), g);
        u->head = (u->head + 1) % UNDO_SLOTS;
    }
}

bool Undo_Pop(UndoStack *u, Game *g) {
    if (u->count < 2) return false;
    u->count--;
    *g = *Slot(u, u->count - 1);
    g->justUndid = true;
    u->undone++;
    return true;
}

int Undo_Left(const UndoStack *u) { return u->count > 1 ? u->count - 1 : 0; }
