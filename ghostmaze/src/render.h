#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Portrait like the rest of Ghost Arcade's tall games. The 16x12 house is
 * drawn at 34px a tile under a HUD strip, with the level's hint beneath it.
 * Menus, scores, settings and updates come from ghost-common's ui.c. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define TILE 34
#define FIELD_W (MAP_W * TILE)
#define FIELD_H (MAP_H * TILE)
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username;
    int lastRank;
    bool scanlines;
    bool showFps;
} FrameInfo;

/* One full gameplay frame (owns BeginDrawing/EndDrawing). Call BEFORE
 * Game_ConsumeFrameFlags: it reads the one-frame events for its bursts. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (drifting ghosts); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
