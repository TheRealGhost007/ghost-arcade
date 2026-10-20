#ifndef NAMEBOARD_H
#define NAMEBOARD_H

#include <stdbool.h>
#include "kit.h"

/* The pad's name board: an arcade high-score letter grid under the name
 * being typed, so a name can be entered without a keyboard. No raylib.
 *
 *   A B C D E F G H I J K L M
 *   N O P Q R S T U V W X Y Z
 *   0 1 2 3 4 5 6 7 8 9 . - _
 *   ! [ Space ] [  Del  ] [ Done ]
 *
 * The d-pad moves round it (each edge comes round to the other side), A
 * types the cell, X deletes, Y types a space, Menu finishes, B puts the old
 * name back. The mouse clicks cells; the keyboard types straight into the
 * name as ever. settings.c owns the typing; this is the grid. */

#define NB_COLS 13
#define NB_ROWS 4
#define NB_CELL 32  /* a letter's square */
#define NB_PITCH 36 /* cell and gap */
#define NB_PAD 16   /* the panel round the grid */
#define NB_W (NB_COLS * NB_PITCH - (NB_PITCH - NB_CELL) + 2 * NB_PAD)
#define NB_H (NB_ROWS * NB_PITCH - (NB_PITCH - NB_CELL) + 2 * NB_PAD)
#define NB_GAP 8        /* between the name and the board */
#define NB_BOTTOM 604   /* the board stays above the hint line */

typedef enum { NBK_CHAR, NBK_SPACE, NBK_DELETE, NBK_DONE } NameBoardKind;

typedef struct {
    NameBoardKind kind;
    char ch;           /* NBK_CHAR: what it types */
    char label[8];     /* what it shows */
    int row, col, span;
} NameBoardCell;

int NameBoard_Count(void);
/* NULL out of range. */
const NameBoardCell *NameBoard_Cell(int i);
/* The cell that types c, or -1. */
int NameBoard_CellFor(char c);

/* Where the board's cursor is. col is the column up and down keep to, so
 * G, up to Del and down again is G, not F. */
typedef struct {
    int cell;
    int col;
} NameBoardPos;

/* On the first cell, A. */
void NameBoard_Home(NameBoardPos *p);
/* Straight to a cell (a click): its own column. */
void NameBoard_Set(NameBoardPos *p, int cell);
/* The d-pad: every edge comes round to the other side. */
void NameBoard_Move(NameBoardPos *p, KitDir dir);

/* Under the name's box, or over it when there is no room below. */
KitRect NameBoard_Panel(KitRect field);
KitRect NameBoard_CellRect(KitRect panel, int cell);

#endif
