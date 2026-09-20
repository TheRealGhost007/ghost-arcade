#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Same window as every Ghost Arcade game. The 572x572 field sits under a
 * HUD strip; FIELD_X/FIELD_Y are public so main.c can turn a mouse position
 * into field coordinates. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on the shared table, 0 = none */
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (bricks
 * broken, explosions, lives lost) to start its particles, shake and
 * flashes. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (a faint wall and a ball rattling under it); passed to
 * Ui_DrawMenu. The menu, scores, settings and updates screens themselves
 * come from ghost-common's ui.c. */
void Render_MenuBackdrop(float dt);

#endif
