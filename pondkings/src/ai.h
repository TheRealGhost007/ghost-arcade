#ifndef AI_H
#define AI_H

#include "game.h"

/* The computer players. Each tick every skater nobody is steering gets a
 * command worked out from the game state alone, with g->rng for its
 * hesitations, so a computer player is as deterministic as the rules (and
 * costs nothing to send over a network: it isn't sent). Human skaters get
 * the same kind of command from their pad, so one code path moves both.
 * No raylib, no float. */

typedef struct {
    V2 move;       /* where to skate: a unit vector, or zero to glide */
    uint8_t press; /* BTN_* pressed this tick */
    uint8_t hold;  /* BTN_* held this tick (a slapshot winds up while Shoot is held) */
} Cmd;

/* Fills *out for skater i. Advances g->rng; call in skater order. */
void Ai_Command(Game *g, int i, Cmd *out);

/* How a difficulty plays, for game.c. */
fx Ai_SpeedFactor(int level);               /* computer skaters' top speed, x1.0 at best */
int Ai_FaceoffReaction(Game *g, int team);  /* ticks after the drop a computer centre goes for it */
int Ai_ShotScatter(int level);              /* extra spread on a computer's shots, in world units */

#endif
