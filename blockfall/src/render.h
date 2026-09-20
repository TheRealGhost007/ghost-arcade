#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "persist.h"

#define CELL_SIZE 30
/* Same window as every Ghost Arcade game. The extra width/height beyond the
 * fixed 300x600 board goes to chrome (side panel, menus); the pixel font
 * (Press Start 2P) runs much wider per character than a normal UI font. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame lock/clear events
 * to start its flashes, shake and particles. fps is shown only when showFps
 * is true (debug/perf overlay). */
void Render_Frame(const Game *g, const Settings *settings, bool showFps);

/* Menu backdrop (slowly falling tetrominoes); passed to Ui_DrawMenu. The
 * menu, scores, settings and updates screens themselves come from
 * ghost-common's ui.c. */
void Render_MenuBackdrop(float dt);

#endif
