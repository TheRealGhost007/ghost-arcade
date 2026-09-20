#ifndef RENDER_H
#define RENDER_H

#include "game.h"

#define CELL_SIZE 24
/* Same window size as Blockfall so the Ghost Arcade games feel like a set.
 * The 576x576 board sits under a HUD strip; the pixel font (Press Start 2P)
 * runs wide, so the HUD gets a full-width row rather than a side panel. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on the shared table, 0 = none */
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (snakes crawling across the screen); passed to Ui_DrawMenu.
 * The menu, scores, settings and updates screens themselves come from
 * ghost-common's ui.c. */
void Render_MenuBackdrop(float dt);

#endif
