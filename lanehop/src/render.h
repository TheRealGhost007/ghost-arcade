#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Same window as every Ghost Arcade game. The world is 13x13 tiles of 44px
 * (572x572) under a HUD strip. Menus, scores, settings and updates come from
 * ghost-common's ui.c; this module only draws the game itself. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define TILE 44
#define FIELD_W (LANE_COLS * TILE)
#define FIELD_H (LANE_ROWS * TILE)
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on this machine's table, 0 = none */
    bool scanlines;
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (deaths,
 * homecomings) to start its splashes and score pop-ups. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (traffic drifting across); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
