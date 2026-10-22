#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "game.h"
#include "raylib.h"

/* Drawing the pond: a winter night in town, the ice in a three-quarter view
 * (world y squashed), skaters in toques and scarves, and the effects that
 * only the eye needs (snow spray, breath, skate marks, falling snow). The
 * simulation never sees any of this, so floats and a render-only random
 * stream are fine here. */

#define SCREEN_W 960
#define SCREEN_H 640

/* What the scene needs to know beyond the game state. */
typedef struct {
    int rivals;                         /* which rival team is side 1 (a town team index) */
    const char *passKey[MAX_PLAYERS];   /* each player's Pass button, for the faceoff prompt */
    const char *shootKey[MAX_PLAYERS];  /* each player's Shoot button, for the shootout prompt */
    /* Who is on the ice, when it isn't the town teams: the Journey's crew
     * (names may be signed players) and a friend's crew. NULL = the usual. */
    const char *crewNames[TEAM_SIZE];   /* side 0's kids, as signed */
    const char *rivalName;              /* side 1's team name if it is a friend's crew */
    const char *rivalNames[TEAM_SIZE];
    /* The arena's big-screen replay: the frame being shown is an old state. */
    bool replay;
    float replayProgress;               /* 0..1 through the replay */
    /* Online: the other machine's round trip in ms (-1 = not online), and
     * whether the game is waiting for it. */
    int pingMs;
    bool waiting;
    const char *banner;                 /* an extra line under the scoreboard ("Daily pond: Wind"), or NULL */
} RenderInfo;

void Render_Init(void);
void Render_Shutdown(void);
/* A fresh pond: no skate marks, effects cleared. */
void Render_NewGame(void);
/* After every Game_Step: spawns effects for that tick's events and records skate marks. */
void Render_OnStep(const Game *g);
/* Once a frame, before Win_BeginFrame: moves effects and bakes new skate marks. */
void Render_Prepare(const Game *g, float dt);

/* The whole scene: backdrop, pond, skaters, HUD and banners. */
void Render_Game(const Game *g, const RenderInfo *ri);
/* Sky, northern lights, town, trees and snowfall; with the empty pond for menus. */
void Render_Backdrop(bool withPond);
/* The same scenery for a venue, with its empty ice (menus that show where you'll play). */
void Render_VenueBackdrop(int venue);

Vector2 Render_ToScreen(V2 p);
/* The name shown for a skater: P1..P4 for players, else the kid's name. */
const char *Render_SkaterName(const Game *g, int i, int rivals);
/* The same, using the names in ri when it has them (signed players, a friend's crew). */
const char *Render_SkaterNameIn(const Game *g, int i, const RenderInfo *ri);
Color Render_PlayerColor(int slot);
Color Render_TeamColor(int side, int rivals);

/* Text in the game's own look (raylib's pixel font, at whole multiples). */
void Render_Text(const char *text, int x, int y, int size, Color c);
void Render_TextCentred(const char *text, int cx, int y, int size, Color c);
int Render_TextWidth(const char *text, int size);

#endif
