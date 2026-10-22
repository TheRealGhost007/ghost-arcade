#ifndef TEAMS_H
#define TEAMS_H

#include <stdint.h>

/* Who plays: your crew, the Rink Rats, and the town's other teams. Names,
 * colours, each kid's build and signature move, and the goalie who plays
 * for them on the rinks. No raylib, so the menus, the renderer, the
 * Journey and the settings file all agree. Rosters are in build order:
 * quick, balanced, big. */

typedef struct {
    uint8_t r, g, b;
} TeamRGB;

typedef struct {
    const char *name;
    TeamRGB jersey, trim, toque;
    const char *kids[3];
    uint8_t traits[3];  /* Trait (game.h) */
    const char *goalie;
    uint8_t goalieStyle; /* GoalieStyle (game.h) */
} TeamStyle;

#define RIVAL_COUNT 5

const TeamStyle *Team_Crew(void);
const TeamStyle *Team_Rival(int index); /* wraps round */
/* The style for side 0 (the crew) or side 1 (the chosen rivals). */
const TeamStyle *Team_ForSide(int side, int rivalIndex);

#endif
