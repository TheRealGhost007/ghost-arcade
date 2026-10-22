#ifndef RINK_H
#define RINK_H

#include "fixed.h"

/* Sizes in world units. The origin is centre ice, x grows toward the right
 * net and y toward the near side (screen terms: down). Each venue's ice is a
 * rounded rectangle (game.c's Game_IceFor has the pond's, the town rink's
 * and the arena's); the POND_ and net numbers below are the pond's, kept for
 * the tests and the menus' empty pond. No raylib. */

#define POND_HL FX_INT(420) /* half length */
#define POND_HW FX_INT(210) /* half width */
#define POND_R FX_INT(110)  /* corner radius */

/* Pond nets: low wooden-framed boxes with no goalie in front, so the mouth
 * is narrow. Team 0 attacks the right net (+x), team 1 the left one. The
 * goal line is the open front of each box; the box runs NET_DEPTH behind it. */
#define GOAL_X FX_INT(350)
#define NET_HALF_W FX_INT(30)
#define NET_DEPTH FX_INT(22)
#define POST_R FX_INT(3)

#define PUCK_R FX_INT(4)
#define SKATER_R FX_INT(11)
#define STICK_REACH FX_INT(13) /* centre of the body to the blade */
#define PICKUP_R FX_INT(11)    /* blade to puck centre, to take a loose puck */
#define RECEIVE_R FX_INT(19)   /* the same for the player a pass is meant for */

/* Where a team's three skaters line up for a faceoff, for team 0 (team 1 is
 * the mirror image): centre, wing, back. */
#define FORM_CENTRE_X FX_INT(-20)
#define FORM_WING_X FX_INT(-90)
#define FORM_WING_Y FX_INT(-110)
#define FORM_BACK_X FX_INT(-190)
#define FORM_BACK_Y FX_INT(70)

#endif
