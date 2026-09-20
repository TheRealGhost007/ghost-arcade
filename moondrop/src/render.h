#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Moondrop shares Rockdrift's landscape window: 800x640, the 768x480 field
 * under a HUD strip. Menus, scores, settings and updates come from
 * ghost-common's ui.c; this module only draws the game itself -- entirely
 * in lines, like a vector monitor. */
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
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (landing,
 * crash) to start its sparks and shake. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (a ridge line and a lander drifting down); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
