#include "backdrop.h"
#include <string.h>

int Sky_CellX(int col) { return SKY_X0 + col * SKY_CELL; }
int Sky_CellY(int row) { return SKY_BOTTOM - (row + 1) * SKY_CELL; }

int Sky_Height(const Skyline *s, int col) {
    for (int r = SKY_GRID_ROWS - 1; r >= 0; r--) if (s->cells[r][col] >= 0) return r + 1;
    return 0;
}

int Sky_MaxHeight(const Skyline *s) {
    int m = 0;
    for (int c = 0; c < SKY_COLS; c++) {
        int h = Sky_Height(s, c);
        if (h > m) m = h;
    }
    return m;
}

float Sky_SweepProgress(const Skyline *s) { return s->sweepT > 0.0f ? 1.0f - s->sweepT / SKY_SWEEP : 0.0f; }

static int Rand(Skyline *s, int lo, int hi) { return lo + (int)Rng_Range(&s->rng, (uint32_t)(hi - lo + 1)); }

/* The piece's cells, and the lowest local row (cy grows downward). */
static int Cells(PieceType t, int rot, Cell out[CELLS_PER_PIECE]) {
    Tetromino_GetCells(t, rot, out);
    int maxY = 0;
    for (int k = 0; k < CELLS_PER_PIECE; k++) if (out[k].y > maxY) maxY = out[k].y;
    return maxY;
}

static void Span(const Cell *c, int *minX, int *maxX) {
    *minX = 3;
    *maxX = 0;
    for (int k = 0; k < CELLS_PER_PIECE; k++) {
        if (c[k].x < *minX) *minX = c[k].x;
        if (c[k].x > *maxX) *maxX = c[k].x;
    }
}

/* The row the piece's lowest cells come to rest on, dropped straight down
 * at column col (the hard drop of play). */
static int LandRow(const Skyline *s, PieceType t, int rot, int col) {
    Cell c[CELLS_PER_PIECE];
    int maxY = Cells(t, rot, c);
    int base = 0;
    for (int k = 0; k < CELLS_PER_PIECE; k++) {
        int need = Sky_Height(s, col + c[k].x) - (maxY - c[k].y);
        if (need > base) base = need;
    }
    return base;
}

/* Where the top of the piece's box is when it rests there. */
static float LandY(const Skyline *s, const SkyPiece *p) {
    Cell c[CELLS_PER_PIECE];
    int maxY = Cells(p->type, p->rot, c);
    int base = LandRow(s, p->type, p->rot, p->col);
    return (float)(Sky_CellY(base) - maxY * SKY_CELL);
}

static bool ColumnsBusy(const Skyline *s, const SkyPiece *self, int lo, int hi) {
    for (int i = 0; i < SKY_PIECES; i++) {
        const SkyPiece *o = &s->p[i];
        if (o == self || o->wait < 0.0f) continue;
        Cell c[CELLS_PER_PIECE];
        Cells(o->type, o->rot, c);
        int a, b;
        Span(c, &a, &b);
        if (o->col + a <= hi && lo <= o->col + b) return true;
    }
    return false;
}

/* How good the skyline is with the piece dropped there: the usual stacker
 * weights (low, few holes, flat, rows filled), plus a little chance so it
 * is not the same every time, and it keeps away from columns another
 * falling piece is heading for. */
static float Evaluate(Skyline *s, const SkyPiece *self, PieceType t, int rot, int col) {
    int8_t g[SKY_GRID_ROWS][SKY_COLS];
    memcpy(g, s->cells, sizeof(g));
    Cell c[CELLS_PER_PIECE];
    int maxY = Cells(t, rot, c);
    int base = LandRow(s, t, rot, col);
    for (int k = 0; k < CELLS_PER_PIECE; k++) {
        int r = base + (maxY - c[k].y);
        if (r >= SKY_GRID_ROWS) return -1.0e6f;
        g[r][col + c[k].x] = (int8_t)t;
    }
    int lines = 0, to = 0;
    for (int r = 0; r < SKY_GRID_ROWS; r++) {
        bool full = true;
        for (int x = 0; x < SKY_COLS && full; x++) full = g[r][x] >= 0;
        if (full) { lines++; continue; }
        if (to != r) memcpy(g[to], g[r], sizeof(g[r]));
        to++;
    }
    for (; to < SKY_GRID_ROWS; to++) memset(g[to], -1, sizeof(g[to]));
    int heights[SKY_COLS], holes = 0, bump = 0, over = 0, top = 0;
    for (int x = 0; x < SKY_COLS; x++) {
        int h = 0;
        for (int r = SKY_GRID_ROWS - 1; r >= 0 && !h; r--) if (g[r][x] >= 0) h = r + 1;
        heights[x] = h;
        if (h > top) top = h;
        for (int r = 0; r < h; r++) if (g[r][x] < 0) holes++;
        if (h > SKY_ROWS) over += h - SKY_ROWS;
        if (x > 0) bump += heights[x] > heights[x - 1] ? heights[x] - heights[x - 1] : heights[x - 1] - heights[x];
    }
    /* The piece's own highest cell, so low gaps fill before anything grows. */
    int pieceTop = base + maxY + 1;
    float score = 6.0f * (float)lines - 8.0f * (float)pieceTop - 3.0f * (float)top - 5.0f * (float)holes - 0.3f * (float)bump -
                  10.0f * (float)over;
    (void)self;
    return score + (float)Rand(s, 0, 100) * 0.004f;
}

/* It never lands in columns another falling piece is heading for (unless
 * there is nowhere else), so the ground it planned for is the ground it
 * meets: a row clear only lowers all of it at once. */
static void Plan(Skyline *s, SkyPiece *p) {
    float best = -1.0e9f;
    bool found = false;
    for (int pass = 0; pass < 2 && !found; pass++) {
        for (int rot = 0; rot < ROTATIONS_PER_PIECE; rot++) {
            Cell c[CELLS_PER_PIECE];
            Cells(p->type, rot, c);
            int minX, maxX;
            Span(c, &minX, &maxX);
            for (int col = -minX; col + maxX < SKY_COLS; col++) {
                if (pass == 0 && ColumnsBusy(s, p, col + minX, col + maxX)) continue;
                float v = Evaluate(s, p, p->type, rot, col);
                if (!found || v > best) {
                    best = v;
                    p->rot = rot;
                    p->col = col;
                    found = true;
                }
            }
        }
    }
}

/* S and Z cannot lie on flat ground without leaving a hole, so they come
 * rarely; the skyline then fills its rows and clears them now and then. */
static void Choose(Skyline *s, SkyPiece *p) {
    static const PieceType kMostly[] = {PIECE_I, PIECE_O, PIECE_T, PIECE_J, PIECE_L};
    int r = Rand(s, 0, 99);
    p->type = r < 8 ? PIECE_S : (r < 16 ? PIECE_Z : kMostly[r % 5]);
    Plan(s, p);
}

/* The ground changed (a landing, a clear, a sweep): a piece still above the
 * screen can think again; one already in sight keeps its column. */
static void Replan(Skyline *s) {
    for (int i = 0; i < SKY_PIECES; i++) {
        SkyPiece *p = &s->p[i];
        if (p->wait < 0.0f || p->y > -4.0f * SKY_CELL) continue;
        float wait = p->wait;
        p->wait = -1.0f; /* not in its own way */
        Plan(s, p);
        p->wait = wait;
    }
}

static void Spawn(Skyline *s, SkyPiece *p, bool scatter) {
    p->wait = -1.0f; /* not counted as falling while it chooses */
    Choose(s, p);
    p->speed = (float)Rand(s, 26, 64);
    p->wait = scatter ? 0.0f : (float)Rand(s, 0, 30) / 10.0f;
    float land = LandY(s, p);
    p->y = scatter ? (float)Rand(s, -720, (int)land - 32) : -4.0f * SKY_CELL;
    if (p->y > land) p->y = land;
}

static void FindFullRows(Skyline *s) {
    bool any = false;
    for (int r = 0; r < SKY_GRID_ROWS; r++) {
        bool full = true;
        for (int c = 0; c < SKY_COLS && full; c++) full = s->cells[r][c] >= 0;
        if (full && !s->clearing[r]) {
            s->clearing[r] = true;
            any = true;
        }
    }
    if (any) s->clearT = SKY_CLEAR_FLASH;
}

static void RemoveClearedRows(Skyline *s) {
    int to = 0;
    for (int r = 0; r < SKY_GRID_ROWS; r++) {
        if (s->clearing[r]) { s->clears++; continue; }
        if (to != r) {
            memcpy(s->cells[to], s->cells[r], sizeof(s->cells[r]));
            memcpy(s->blink[to], s->blink[r], sizeof(s->blink[r]));
        }
        to++;
    }
    for (; to < SKY_GRID_ROWS; to++) {
        memset(s->cells[to], -1, sizeof(s->cells[to]));
        memset(s->blink[to], 0, sizeof(s->blink[to]));
    }
    memset(s->clearing, 0, sizeof(s->clearing));
}

static void Lock(Skyline *s, SkyPiece *p, bool glow) {
    Cell c[CELLS_PER_PIECE];
    int maxY = Cells(p->type, p->rot, c);
    int base = LandRow(s, p->type, p->rot, p->col);
    for (int k = 0; k < CELLS_PER_PIECE; k++) {
        int x = p->col + c[k].x, r = base + (maxY - c[k].y);
        if (x < 0 || x >= SKY_COLS || r < 0 || r >= SKY_GRID_ROWS) continue;
        s->cells[r][x] = (int8_t)p->type;
        s->blink[r][x] = glow ? SKY_LOCK_BLINK : 0.0f;
    }
    s->landed++;
    FindFullRows(s);
}

/* Too tall, and no clear on its way to bring it down: sweep it all away. */
static void CheckHeight(Skyline *s) {
    if (s->sweepT > 0.0f || s->clearT > 0.0f) return;
    if (Sky_MaxHeight(s) > SKY_ROWS) {
        s->sweepT = SKY_SWEEP;
        s->sweeps++;
    }
}

void Sky_Init(Skyline *s, uint64_t seed) {
    memset(s, 0, sizeof(*s));
    memset(s->cells, -1, sizeof(s->cells));
    Rng_Seed(&s->rng, seed);
    for (int i = 0; i < SKY_PIECES; i++) s->p[i].wait = -1.0f;
    /* A skyline already there on the first frame: a few pieces dropped at
     * once, clears done at once. */
    SkyPiece tmp;
    memset(&tmp, 0, sizeof(tmp));
    for (int n = 0; n < 22; n++) {
        Choose(s, &tmp);
        Lock(s, &tmp, false);
        if (s->clearT > 0.0f) { RemoveClearedRows(s); s->clearT = 0.0f; }
        if (Sky_MaxHeight(s) >= 3) break;
    }
    s->landed = s->clears = 0;
    for (int i = 0; i < SKY_PIECES; i++) Spawn(s, &s->p[i], true);
}

void Sky_Step(Skyline *s, float dt) {
    if (dt <= 0.0f) return;
    for (int r = 0; r < SKY_GRID_ROWS; r++)
        for (int c = 0; c < SKY_COLS; c++)
            if (s->blink[r][c] > 0.0f) s->blink[r][c] = s->blink[r][c] > dt ? s->blink[r][c] - dt : 0.0f;
    if (s->clearT > 0.0f) {
        s->clearT -= dt;
        if (s->clearT <= 0.0f) {
            s->clearT = 0.0f;
            RemoveClearedRows(s);
            Replan(s);
        }
    }
    if (s->sweepT > 0.0f) {
        s->sweepT -= dt;
        if (s->sweepT <= 0.0f) {
            s->sweepT = 0.0f;
            memset(s->cells, -1, sizeof(s->cells));
            memset(s->blink, 0, sizeof(s->blink));
            memset(s->clearing, 0, sizeof(s->clearing));
            Replan(s);
        }
    }
    for (int i = 0; i < SKY_PIECES; i++) {
        SkyPiece *p = &s->p[i];
        if (p->wait > 0.0f) {
            p->wait -= dt;
            if (p->wait > 0.0f) continue;
            p->wait = 0.0f;
        }
        p->y += p->speed * dt;
        float land = LandY(s, p);
        if (p->y < land) continue;
        if (s->sweepT > 0.0f) { Spawn(s, p, false); continue; } /* nothing lands while it is swept */
        p->y = land;
        Lock(s, p, true);
        CheckHeight(s);
        Spawn(s, p, false);
        Replan(s);
    }
    CheckHeight(s);
}
