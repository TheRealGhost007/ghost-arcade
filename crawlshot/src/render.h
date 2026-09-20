#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Same window as every Ghost Arcade game. The field is 30x32 tiles of 18px
 * (540x576) under a HUD strip. Menus, scores, settings and updates come from
 * ghost-common's ui.c; this module only draws the game itself. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define TILE 18
#define FIELD_W (COLS * TILE)
#define FIELD_H (ROWS * TILE)
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on this machine's table, 0 = none */
    bool scanlines;
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (kills,
 * deaths, splits) to start its bursts and score pop-ups. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Screen -> field tile coordinates, for the mouse control. */
float Render_ScreenToFieldX(float sx);
float Render_ScreenToFieldY(float sy);

/* Menu backdrop (a caterpillar wandering through toadstools); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
