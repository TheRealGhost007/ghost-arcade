#ifndef BLOCKDRAW_H
#define BLOCKDRAW_H

#include "raylib.h"
#include "tetromino.h"
#include "game.h"

/* The play screen's bevelled block, for every screen that shows blocks (the
 * menu's skyline, Play setup's preview), so they all look like play. */
void Render_BlockAt(int x, int y, int size, Color base, float alpha);
Color Render_PieceColor(PieceType t);
/* A piece's pattern over its block at (x, y, size) when Settings > Look >
 * Block patterns is on (look.h): darker on a light block, lighter on a dark
 * one. Nothing on blocks under 12 px. */
void Render_Pattern(int x, int y, int size, PieceType t, Color base, float alpha);
/* Both: the block and its pattern. */
void Render_PieceBlock(int x, int y, int size, PieceType t, Color base, float alpha);
/* A power-up's colour, and its mark on a block (as in the well). */
Color Render_PowerColor(PowerUp p);
void Render_PowerMark(int x, int y, int size, PowerUp p);

#endif
