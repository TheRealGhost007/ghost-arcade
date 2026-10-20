#include "nameboard.h"
#include <stdio.h>
#include <string.h>

/* Built once from the rows: letters, digits and . - _ one column each, and
 * the last row's keys four columns wide. */
#define NB_MAX 48

static NameBoardCell sCells[NB_MAX];
static int sCount = 0;

static void Add(NameBoardKind kind, char ch, const char *label, int row, int col, int span) {
    NameBoardCell *c = &sCells[sCount++];
    c->kind = kind;
    c->ch = ch;
    snprintf(c->label, sizeof(c->label), "%s", label);
    c->row = row;
    c->col = col;
    c->span = span;
}

static void Build(void) {
    if (sCount) return;
    static const char *const kRows[3] = {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789.-_"};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < NB_COLS; c++) {
            char label[2] = {kRows[r][c], '\0'};
            Add(NBK_CHAR, kRows[r][c], label, r, c, 1);
        }
    Add(NBK_CHAR, '!', "!", 3, 0, 1);
    Add(NBK_SPACE, ' ', "Space", 3, 1, 4);
    Add(NBK_DELETE, 0, "Del", 3, 5, 4);
    Add(NBK_DONE, 0, "Done", 3, 9, 4);
}

int NameBoard_Count(void) {
    Build();
    return sCount;
}

const NameBoardCell *NameBoard_Cell(int i) {
    Build();
    return (i >= 0 && i < sCount) ? &sCells[i] : NULL;
}

int NameBoard_CellFor(char c) {
    Build();
    for (int i = 0; i < sCount; i++) if ((sCells[i].kind == NBK_CHAR || sCells[i].kind == NBK_SPACE) && sCells[i].ch == c) return i;
    return -1;
}

/* The cell covering (row, col). */
static int At(int row, int col) {
    for (int i = 0; i < sCount; i++)
        if (sCells[i].row == row && col >= sCells[i].col && col < sCells[i].col + sCells[i].span) return i;
    return -1;
}

void NameBoard_Home(NameBoardPos *p) { NameBoard_Set(p, 0); }

void NameBoard_Set(NameBoardPos *p, int cell) {
    const NameBoardCell *c = NameBoard_Cell(cell);
    p->cell = c ? cell : 0;
    p->col = c ? c->col : 0;
}

void NameBoard_Move(NameBoardPos *p, KitDir dir) {
    const NameBoardCell *c = NameBoard_Cell(p->cell);
    if (!c) {
        NameBoard_Home(p);
        return;
    }
    int col = p->col >= c->col && p->col < c->col + c->span ? p->col : c->col;
    switch (dir) {
        case KIT_LEFT: NameBoard_Set(p, At(c->row, (c->col + NB_COLS - 1) % NB_COLS)); return;
        case KIT_RIGHT: NameBoard_Set(p, At(c->row, (c->col + c->span) % NB_COLS)); return;
        case KIT_UP: p->cell = At((c->row + NB_ROWS - 1) % NB_ROWS, col); break;
        case KIT_DOWN: p->cell = At((c->row + 1) % NB_ROWS, col); break;
    }
    p->col = col;
}

KitRect NameBoard_Panel(KitRect field) {
    float x = field.x - NB_PAD, y = field.y + field.h + NB_GAP;
    if (y + NB_H > NB_BOTTOM) y = field.y - NB_GAP - NB_H;
    if (x + NB_W > KIT_SCREEN_W - 16) x = KIT_SCREEN_W - 16 - NB_W;
    return (KitRect){x, y, NB_W, NB_H};
}

KitRect NameBoard_CellRect(KitRect panel, int cell) {
    const NameBoardCell *c = NameBoard_Cell(cell);
    if (!c) return (KitRect){0, 0, 0, 0};
    return (KitRect){panel.x + NB_PAD + (float)(c->col * NB_PITCH), panel.y + NB_PAD + (float)(c->row * NB_PITCH),
                     (float)(c->span * NB_PITCH - (NB_PITCH - NB_CELL)), (float)NB_CELL};
}
