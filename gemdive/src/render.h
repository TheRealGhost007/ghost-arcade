#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Landscape like Rockdrift and Moondrop: 800x640, with a 768x480 view (32x20
 * tiles of 24px) onto the 40x22 cave, which scrolls to follow the digger.
 * Menus, scores, settings and updates come from ghost-common's ui.c; this
 * module only draws the cave. */
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 640
#define TILE 24
#define VIEW_COLS 32
#define VIEW_ROWS 20
#define VIEW_W (VIEW_COLS * TILE)
#define VIEW_H (VIEW_ROWS * TILE)
#define VIEW_X ((WINDOW_WIDTH - VIEW_W) / 2)
#define VIEW_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on this machine's table, 0 = none */
    bool scanlines;
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (gems,
 * blasts, deaths) to start its sparkles and shake. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (drifting gems); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
