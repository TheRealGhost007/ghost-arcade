#include "bot.h"
#include <string.h>

typedef struct { int x, y; } P;

static const int kDx[4] = {0, 0, -1, 1}, kDy[4] = {-1, 1, 0, 0};
static const char kMoveChar[4] = {'U', 'D', 'L', 'R'};

static bool Passable(const Game *g, int x, int y, int dx, int dy) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return false;
    switch (g->cell[y][x]) {
        case C_EMPTY: case C_DIRT: case C_GEM: return true;
        case C_EXIT: return g->exitOpen;
        case C_BOULDER: /* a sideways push onto empty ground */
            return dy == 0 && x + dx >= 0 && x + dx < COLS && g->cell[y][x + dx] == C_EMPTY;
        default: return false;
    }
}

/* First step of the shortest route to any target cell, or -1. */
static int FirstStep(const Game *g, bool wantExit, int avoidFirst) {
    static int dist[ROWS][COLS];
    static int first[ROWS][COLS];
    static P queue[ROWS * COLS];
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) { dist[y][x] = -1; first[y][x] = -1; }
    int head = 0, tail = 0;
    dist[g->py][g->px] = 0;
    queue[tail++] = (P){g->px, g->py};
    while (head < tail) {
        P p = queue[head++];
        bool target = wantExit ? g->cell[p.y][p.x] == C_EXIT : g->cell[p.y][p.x] == C_GEM;
        if (target && !(p.x == g->px && p.y == g->py)) return first[p.y][p.x];
        for (int d = 0; d < 4; d++) {
            if (dist[p.y][p.x] == 0 && d == avoidFirst) continue;
            int nx = p.x + kDx[d], ny = p.y + kDy[d];
            if (!Passable(g, nx, ny, kDx[d], kDy[d]) || dist[ny][nx] >= 0) continue;
            dist[ny][nx] = dist[p.y][p.x] + 1;
            first[ny][nx] = dist[p.y][p.x] == 0 ? d : first[p.y][p.x];
            queue[tail++] = (P){nx, ny};
        }
    }
    return -1;
}

/* Can the player get through `depth` more ticks alive (or finish the cave)? */
static bool Survives(const Game *g, int depth) {
    if (g->state == ST_CLEAR) return true;
    if (!g->playerAlive) return false;
    if (depth == 0) return true;
    static const char kAll[5] = {'.', 'U', 'D', 'L', 'R'};
    for (int i = 0; i < 5; i++) {
        Game copy = *g;
        Game_TickWithMove(&copy, kAll[i]);
        if (Survives(&copy, depth - 1)) return true;
    }
    return false;
}

static bool SafeAfter(const Game *g, char move, int depth) {
    Game copy = *g;
    Game_TickWithMove(&copy, move);
    if (copy.state == ST_CLEAR) return true;
    if (!copy.playerAlive) return false;
    return Survives(&copy, depth);
}

char Bot_Move(const Game *g) {
    bool wantExit = g->exitOpen;
    int step = FirstStep(g, wantExit, -1);

    char cands[6];
    int n = 0;
    if (step >= 0) cands[n++] = kMoveChar[step];
    /* Alternatives: a different way to the target, then waiting. */
    if (step >= 0) {
        int alt = FirstStep(g, wantExit, step);
        if (alt >= 0) cands[n++] = kMoveChar[alt];
    }
    cands[n++] = '.';
    for (int d = 0; d < 4; d++) {
        char c = kMoveChar[d];
        bool have = false;
        for (int i = 0; i < n; i++) if (cands[i] == c) have = true;
        if (!have && Passable(g, g->px + kDx[d], g->py + kDy[d], kDx[d], kDy[d])) cands[n++] = c;
    }
    for (int i = 0; i < n; i++) if (SafeAfter(g, cands[i], 3)) return cands[i];
    for (int i = 0; i < n; i++) if (SafeAfter(g, cands[i], 0)) return cands[i];
    return step >= 0 ? kMoveChar[step] : '.';
}
