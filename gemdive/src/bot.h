#ifndef BOT_H
#define BOT_H

#include "game.h"

/* A cave-solving bot. It plans a route to the nearest gem (or the open exit)
 * over dirt and empty space, and before taking a step it plays the next few
 * ticks on a copy of the game to make sure it survives them. It is not clever,
 * but a cave it can finish is a cave a person can finish, which is what the
 * cave generator and the test suite use it for. Returns one of U D L R or '.'. */
char Bot_Move(const Game *g);

#endif
