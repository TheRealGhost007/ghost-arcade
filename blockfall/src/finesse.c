#include "finesse.h"
#include <stdio.h>
#include <string.h>

/* The search runs over (rotation, x, y) at the top of the well. x runs
 * from -3 (a piece's box may hang past the left wall while its cells are
 * inside) to the width; y from the spawn row up to a few rows under it (a
 * kick can lower the piece a row or two; a move never lifts it above the
 * hidden rows, which Board_TestFit keeps inside the grid). */
#define FIN_X_MIN (-3)
#define FIN_XS (BOARD_MAX_W + 4)
#define FIN_YS 6
#define FIN_STATES (ROTATIONS_PER_PIECE * FIN_XS * FIN_YS)

typedef struct { int rot, x, y; } FinState;

/* A piece's four cells on the board, sorted, so two placements compare. */
static void CellsOf(PieceType t, int rot, int x, int y, int out[4][2]) {
    Cell c[CELLS_PER_PIECE];
    Tetromino_GetCells(t, rot, c);
    for (int i = 0; i < 4; i++) { out[i][0] = x + c[i].x; out[i][1] = y + c[i].y; }
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && (out[j][1] < out[j - 1][1] || (out[j][1] == out[j - 1][1] && out[j][0] < out[j - 1][0])); j--) {
            int tx = out[j][0], ty = out[j][1];
            out[j][0] = out[j - 1][0]; out[j][1] = out[j - 1][1];
            out[j - 1][0] = tx; out[j - 1][1] = ty;
        }
}

static int Index(const FinState *s) {
    int xi = s->x - FIN_X_MIN, yi = s->y - BOARD_HIDDEN;
    if (s->rot < 0 || s->rot >= ROTATIONS_PER_PIECE || xi < 0 || xi >= FIN_XS || yi < 0 || yi >= FIN_YS) return -1;
    return (s->rot * FIN_XS + xi) * FIN_YS + yi;
}

static int DropY(const Board *b, PieceType t, const FinState *s) {
    int y = s->y;
    while (Board_TestFit(b, t, s->rot, s->x, y + 1)) y++;
    return y;
}

int Finesse_Optimal(const Board *b, PieceType type, int rotation, int x, int y) {
    if ((int)type < 0 || type >= PIECE_COUNT) return -1;
    int want[4][2];
    CellsOf(type, rotation, x, y, want);

    static unsigned char dist[FIN_STATES];
    static FinState queue[FIN_STATES];
    memset(dist, 0xFF, sizeof(dist));
    FinState start = {0, (b->w - 4) / 2, BOARD_HIDDEN};
    if (!Board_TestFit(b, type, start.rot, start.x, start.y)) return -1;
    int head = 0, tail = 0;
    dist[Index(&start)] = 0;
    queue[tail++] = start;
    int best = -1;

    while (head < tail) {
        FinState s = queue[head++];
        int d = dist[Index(&s)];
        if (best >= 0 && d >= best) break; /* breadth first: nothing later is shorter */
        /* Dropped from here: is it the placement? */
        FinState landed = s;
        landed.y = DropY(b, type, &s);
        int got[4][2];
        CellsOf(type, landed.rot, landed.x, landed.y, got);
        if (memcmp(got, want, sizeof(got)) == 0) { best = d; break; }

        FinState next[8];
        int n = 0;
        /* A tap left or right. */
        for (int dir = -1; dir <= 1; dir += 2) {
            FinState m = s;
            m.x += dir;
            if (Board_TestFit(b, type, m.rot, m.x, m.y)) next[n++] = m;
        }
        /* Held to the wall: as far as it slides. */
        for (int dir = -1; dir <= 1; dir += 2) {
            FinState m = s;
            while (Board_TestFit(b, type, m.rot, m.x + dir, m.y)) m.x += dir;
            if (m.x != s.x) next[n++] = m;
        }
        /* A turn either way, with the first kick that fits (game.c's rule). */
        if (type != PIECE_O) {
            for (int dir = -1; dir <= 1; dir += 2) {
                int to = (s.rot + (dir > 0 ? 1 : 3)) % ROTATIONS_PER_PIECE;
                Cell kicks[TETROMINO_MAX_KICKS];
                int k = Tetromino_Kicks(type, s.rot, to, kicks);
                for (int i = 0; i < k; i++) {
                    FinState m = {to, s.x + kicks[i].x, s.y + kicks[i].y};
                    if (!Board_TestFit(b, type, m.rot, m.x, m.y)) continue;
                    next[n++] = m;
                    break;
                }
            }
        }
        for (int i = 0; i < n; i++) {
            int idx = Index(&next[i]);
            if (idx < 0 || dist[idx] != 0xFF) continue;
            dist[idx] = (unsigned char)(d + 1);
            queue[tail++] = next[i];
        }
    }
    return best;
}

void Finesse_Words(int extra, char *out, int size, char *shortOut, int shortSize) {
    if (extra <= 0) {
        if (out && size > 0) out[0] = '\0';
        if (shortOut && shortSize > 0) shortOut[0] = '\0';
        return;
    }
    if (out) snprintf(out, (size_t)size, "%d extra press%s", extra, extra == 1 ? "" : "es");
    if (shortOut) snprintf(shortOut, (size_t)shortSize, "+%d", extra);
}
