#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Rockdrift is the one landscape game: 800x640, with the 768x480 field under
 * a HUD strip. Menus, scores, settings and updates come from ghost-common's
 * ui.c (which lays itself out from the window size it was initialised with);
 * this module only draws the game itself -- entirely in lines. */
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 640
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on this machine's table, 0 = none */
    bool glow;            /* the additive halo around every line */
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (kills,
 * ship death, hyperspace) to start its debris and shake. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (drifting rock outlines); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
