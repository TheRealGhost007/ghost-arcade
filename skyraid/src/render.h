#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Same window as every Ghost Arcade game; the 572x572 field sits under a
 * HUD strip. Menus, scores, settings and updates come from ghost-common's
 * ui.c -- this module only draws the game itself. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720
#define FIELD_X ((WINDOW_WIDTH - FIELD_W) / 2)
#define FIELD_Y 112

typedef struct {
    const char *username; /* shown on the game-over card */
    int lastRank;         /* 1-based rank the finished run reached on this machine's table, 0 = none */
    bool scanlines;       /* CRT overlay on the playfield */
    bool showFps;
} FrameInfo;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame events (kills,
 * hits, new waves) to start its bursts, score pop-ups and shake. */
void Render_Frame(const Game *g, const FrameInfo *info);

/* Menu backdrop (a drifting formation); passed to Ui_DrawMenu. */
void Render_MenuBackdrop(float dt);

#endif
