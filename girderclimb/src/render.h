#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Portrait like the rest of Ghost Arcade's tall games: 620x720 with a 560x576
 * window onto the tower, which scrolls to follow you up. Menus, scores,
 * settings and updates come from ghost-common's ui.c. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define VIEW_W WORLD_W
#define VIEW_H 576
#define VIEW_X ((WINDOW_WIDTH - VIEW_W) / 2)
#define VIEW_Y 112

typedef struct {
    const char *username;
    int lastRank;
    bool scanlines;
    bool showFps;
} FrameInfo;

/* One full gameplay frame (owns BeginDrawing/EndDrawing). Call BEFORE
 * Game_ConsumeFrameFlags: it reads the one-frame events for its bursts. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (girders and a swinging rope); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
