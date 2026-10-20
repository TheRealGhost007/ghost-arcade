#include "feel.h"
#include <math.h>
#include <string.h>

#define NEVER 9.0f /* a timer that has long run out */

static float Clamp01(float k) { return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k); }
static float EaseOut(float k) {
    k = Clamp01(k);
    return 1.0f - (1.0f - k) * (1.0f - k);
}
static float ClampCells(float v) { return v < -GLIDE_MAX_CELLS ? -GLIDE_MAX_CELLS : (v > GLIDE_MAX_CELLS ? GLIDE_MAX_CELLS : v); }
static float Tick(float t, float dt) { return t < NEVER ? t + dt : t; }

void Feel_Reset(Feel *f, const Game *g, bool glide) {
    memset(f, 0, sizeof(*f));
    f->quadFrameT = f->flightT = f->holdReadyT = f->slideT = f->land.t = NEVER;
    PieceFeel *p = &f->piece;
    p->glideT = p->popT = p->streakT = NEVER;
    if (g) {
        p->on = g->phase == GS_PLAYING;
        p->type = g->current;
        p->rot = g->rotation;
        p->px = g->px;
        p->py = g->py;
        f->lastNext = g->next;
        f->lastNextPower = g->nextPower;
        f->lastNextCell = g->nextPowerCell;
        f->lastHold = (int)g->hold;
        f->lastHoldUsed = g->holdUsed;
    }
    p->spawnT = 0.0f; /* the first piece slides in too */
    p->slid = glide;
}

/* ------------------------------------------------------------ the clear */

ClearTimes Feel_ClearTimes(int count, int level, bool motion) {
    /* Shorter at high levels, so the picture never trails the stack long:
     * 0.24 s, then 0.18 s at levels 10-14 and 0.12 s from 15. */
    float s = level >= 15 ? 0.5f : (level >= 10 ? 0.75f : 1.0f);
    if (!motion) return (ClearTimes){CLEAR_CUT_FLASH_SECONDS * s, 0.0f, 0.0f};
    if (count >= 4) return (ClearTimes){QUAD_FLASH_SECONDS * s, QUAD_GAP_SECONDS * s, CLEAR_DROP_SECONDS * s};
    if (count <= 0) return (ClearTimes){0.0f, 0.0f, CLEAR_DROP_SECONDS * s}; /* only a fall (a Sweep, phase 7) */
    return (ClearTimes){CLEAR_FLASH_SECONDS * s, CLEAR_ZIP_SECONDS * s, CLEAR_DROP_SECONDS * s};
}

float Feel_ClearTotal(ClearTimes t) { return t.flash + t.zip + t.drop; }

void Feel_RowSources(const int *rows, int count, int shift, int totalH, int *out) {
    /* The compaction Board_ClearLines does, bottom up, then a Sweep's move
     * down by `shift`. */
    int post[BOARD_MAX_TOTAL_H];
    if (totalH > BOARD_MAX_TOTAL_H) totalH = BOARD_MAX_TOTAL_H;
    int w = totalH - 1;
    for (int r = totalH - 1; r >= 0; r--) {
        bool gone = false;
        for (int i = 0; i < count && i < 4; i++)
            if (rows[i] == r) gone = true;
        if (!gone) post[w--] = r;
    }
    for (; w >= 0; w--) post[w] = -1;
    for (int r = 0; r < totalH; r++) out[r] = r - shift >= 0 ? post[r - shift] : -1;
}

static void StartClear(Feel *f, const Game *g, const FeelFrame *in, int count, int shift) {
    ClearReplay *r = &f->clear;
    r->on = true;
    r->before = g->lockBoard;
    r->count = count;
    for (int i = 0; i < 4; i++) r->rows[i] = i < count ? g->lastClearRows[i] : -1;
    r->shift = shift;
    Feel_RowSources(r->rows, count, shift, Board_TotalH(&g->board), r->from);
    /* The zip starts from the middle of the piece that landed. */
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->lastLockType, g->lastLockRotation, cells);
    int sum = 0;
    for (int i = 0; i < CELLS_PER_PIECE; i++) sum += cells[i].x;
    int col = g->lastLockX + (int)floorf((float)sum / (float)CELLS_PER_PIECE + 0.5f);
    r->lockCol = col < 0 ? 0 : (col >= g->board.w ? g->board.w - 1 : col);
    r->lockType = g->lastLockType;
    r->quad = count >= 4;
    r->b2b = g->lastWasB2B;
    r->times = Feel_ClearTimes(count, g->level, in->motion);
    r->t = r->prevT = 0.0f;
    if (r->quad) f->quadFrameT = 0.0f;
}

ClearStage Feel_ClearStage(const Feel *f) {
    const ClearReplay *r = &f->clear;
    if (!r->on) return CLEAR_NONE;
    if (r->t < r->times.flash) return CLEAR_FLASH;
    if (r->t < r->times.flash + r->times.zip) return CLEAR_ZIP;
    if (r->t < Feel_ClearTotal(r->times)) return CLEAR_DROP;
    return CLEAR_NONE;
}

bool Feel_ClearCellGone(const ClearReplay *r, int col, float t) {
    if (t < r->times.flash) return false;
    if (r->quad || r->times.zip <= 0.0f) return true; /* a quad (and Reduced motion) bursts at once */
    int w = r->before.w;
    int far = r->lockCol > w - 1 - r->lockCol ? r->lockCol : w - 1 - r->lockCol;
    float reach = (t - r->times.flash) / r->times.zip * (float)(far + 1);
    int d = col > r->lockCol ? col - r->lockCol : r->lockCol - col;
    return (float)d < reach;
}

int Feel_RowLift(const Feel *f, int liveRow, int cell) {
    const ClearReplay *r = &f->clear;
    if (Feel_ClearStage(f) != CLEAR_DROP || liveRow < 0 || liveRow >= BOARD_MAX_TOTAL_H) return 0;
    int src = r->from[liveRow];
    if (src < 0 || liveRow - src <= 0) return 0;
    float p = Clamp01((r->t - r->times.flash - r->times.zip) / r->times.drop);
    if (p < CLEAR_FALL_PART) {
        float k = p / CLEAR_FALL_PART; /* falls faster and faster, like a real drop */
        return (int)floorf((float)((liveRow - src) * cell) * (1.0f - k * k) + 0.5f);
    }
    float s = (p - CLEAR_FALL_PART) / (1.0f - CLEAR_FALL_PART);
    return -(int)floorf((float)CLEAR_SETTLE_PX * sinf(s * 3.14159265f) + 0.5f); /* 2 px down and back */
}

float Feel_ClearPitch(int count, bool b2b) {
    if (count >= 4) return b2b ? 1.122f : 1.0f;
    if (count == 3) return 1.26f;
    if (count == 2) return 1.12f;
    return 1.0f;
}

float Feel_QuadFrame(const Feel *f) { return f->quadFrameT < QUAD_FRAME_SECONDS ? 1.0f - f->quadFrameT / QUAD_FRAME_SECONDS : 0.0f; }

bool Feel_ShowGhost(const Feel *f) { return Feel_ClearStage(f) == CLEAR_NONE; }

/* ------------------------------------------------------------ the piece */

static void GlideNow(const PieceFeel *p, float *x, float *y) {
    /* Even steps: at 60 frames a second an eased 0.04 s is gone in a frame. */
    float left = 1.0f - Clamp01(p->glideT / GLIDE_SECONDS);
    *x = p->ox0 * left;
    *y = p->oy0 * left;
}

void Feel_PieceOffset(const Feel *f, float *dx, float *dy) {
    const PieceFeel *p = &f->piece;
    float x, y;
    GlideNow(p, &x, &y);
    if (p->slid && p->spawnT < SPAWN_SECONDS) y -= SPAWN_CELLS * (1.0f - EaseOut(p->spawnT / SPAWN_SECONDS));
    *dx = ClampCells(x);
    *dy = ClampCells(y);
}

float Feel_SpawnFill(const Feel *f) {
    const PieceFeel *p = &f->piece;
    if (!p->slid || p->spawnT >= SPAWN_SECONDS) return 1.0f;
    return 0.35f + 0.65f * EaseOut(p->spawnT / SPAWN_SECONDS);
}

int Feel_PopInset(const Feel *f) { return f->piece.popT < POP_SECONDS ? POP_PX : 0; }

float Feel_LockShade(const Game *g) {
    if (g->phase != GS_PLAYING || g->lockTimer <= 0.0f) return 0.0f;
    /* The timer runs on the ground, and in the air once the resets are spent. */
    if (!g->grounded && g->lockResets < MAX_LOCK_RESETS) return 0.0f;
    return LOCK_SHADE_MAX * Clamp01(g->lockTimer / LOCK_DELAY_SECONDS);
}

float Feel_Streak(const Feel *f) { return f->piece.streakT < STREAK_SECONDS ? 1.0f - f->piece.streakT / STREAK_SECONDS : 0.0f; }

int Feel_GhostRows(const Game *g, int ghostY, int out[4]) {
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->current, g->rotation, cells);
    int n = 0, total = Board_TotalH(&g->board);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int y = ghostY + cells[i].y;
        if (y < BOARD_HIDDEN || y >= total) continue;
        bool seen = false;
        for (int k = 0; k < n; k++) if (out[k] == y) seen = true;
        if (seen) continue;
        int filled = 0;
        for (int x = 0; x < g->board.w; x++) {
            bool piece = false;
            for (int j = 0; j < CELLS_PER_PIECE; j++)
                if (ghostY + cells[j].y == y && g->px + cells[j].x == x) piece = true;
            if (piece || g->board.cells[y][x] != CELL_EMPTY) filled++;
        }
        if (filled == g->board.w && n < 4) out[n++] = y;
    }
    return n;
}

/* ------------------------------------------------------------ landing */

bool Feel_Squash(const Feel *f) {
    const LandFeel *l = &f->land;
    return l->on && l->hard && !l->replayed && l->motion && l->t < SQUASH_SECONDS;
}

int Feel_Settle(const Feel *f) {
    const LandFeel *l = &f->land;
    return (l->on && !l->hard && !l->replayed && l->motion && l->t < SETTLE_SECONDS) ? SETTLE_PX : 0;
}

float Feel_Bevel(const Feel *f) {
    const LandFeel *l = &f->land;
    return (l->on && !l->hard && l->t < BEVEL_SECONDS) ? 1.0f - l->t / BEVEL_SECONDS : 0.0f;
}

float Feel_Impact(const Feel *f) {
    const LandFeel *l = &f->land;
    return (l->on && l->hard && l->t < IMPACT_SECONDS) ? 1.0f - l->t / IMPACT_SECONDS : 0.0f;
}

float Feel_Smear(const Feel *f) {
    const LandFeel *l = &f->land;
    return (l->on && l->hard && l->motion && l->fromY < l->y && l->t < SMEAR_SECONDS) ? 1.0f - l->t / SMEAR_SECONDS : 0.0f;
}

/* ------------------------------------------------------------ Hold and Next */

bool Feel_HoldFlight(const Feel *f, float *k) {
    if (!f->flightOn || f->flightT >= HOLD_FLIGHT_SECONDS) return false;
    if (k) *k = EaseOut(f->flightT / HOLD_FLIGHT_SECONDS);
    return true;
}

float Feel_HoldReady(const Feel *f) { return f->holdReadyT < HOLD_READY_SECONDS ? 1.0f - f->holdReadyT / HOLD_READY_SECONDS : 0.0f; }

bool Feel_NextSlide(const Feel *f, float *k) {
    if (!f->slideOn || f->slideT >= NEXT_SLIDE_SECONDS) return false;
    if (k) *k = EaseOut(f->slideT / NEXT_SLIDE_SECONDS);
    return true;
}

/* ------------------------------------------------------------ the frame */

void Feel_React(Feel *f, const Game *g, const FeelFrame *in) {
    PieceFeel *p = &f->piece;
    bool playing = g->phase == GS_PLAYING;

    /* Hold: the piece that was drawn last frame flies into the box (not
     * when it locked this frame and the one held was never drawn: a hard
     * drop and a hold in the same frame). */
    if (g->justHeld && !g->justLocked && in->motion && p->on) {
        float dx, dy;
        Feel_PieceOffset(f, &dx, &dy);
        f->flightOn = true;
        f->flightT = 0.0f;
        f->flightType = p->type;
        f->flightRot = p->rot;
        f->flightX = (float)p->px + dx;
        f->flightY = (float)p->py + dy;
        f->flightPower = g->holdPower;
        f->flightPowerCell = g->holdPowerCell;
    }

    if (g->justLocked) {
        LandFeel *l = &f->land;
        int sweep = g->lastPower == PU_SWEEP ? 3 : 0; /* a Sweep moved the board (and the piece) down 3 after the clear */
        l->on = true;
        l->type = g->lastLockType;
        l->rot = g->lastLockRotation;
        l->x = g->lastLockX;
        l->liveY = g->lastLockY;
        l->y = g->lastLockY - sweep;
        l->hard = g->justHardDropped;
        l->fromY = l->y - (g->justHardDropped ? g->lastDropCells : 0);
        l->motion = in->motion;
        l->t = 0.0f;
        /* A new lock ends the replay before it at once. A Zen fresh start
         * swept the board, so there is nothing to replay. A Sweep with no
         * clear only drops the stack: phase 7 gives it its own look (the
         * replay can already fall without a clear: count 0, shift 3). */
        f->clear.on = false;
        int count = g->lastClearCount < 0 ? 0 : (g->lastClearCount > 4 ? 4 : g->lastClearCount);
        if (count > 0 && !g->justZenReset) StartClear(f, g, in, count, sweep);
        l->replayed = f->clear.on;
    }
    /* A Zen fresh start swept the board (a lock above the well, or a Hold
     * whose piece could not come in): a clear still replaying and the piece
     * that landed are gone with it, so neither may keep drawing. */
    if (g->justZenReset) {
        f->clear.on = false;
        f->land.on = false;
    }

    bool newPiece = g->justLocked || g->justHeld || g->justZenReset;
    if (playing && (newPiece || !p->on)) {
        p->on = true;
        p->ox0 = p->oy0 = 0.0f;
        p->glideT = p->popT = p->streakT = NEVER;
        p->spawnT = 0.0f;
        p->slid = in->glide;
    } else if (playing) {
        int dx = g->px - p->px, dy = g->py - p->py;
        if (dx || dy) {
            float x, y;
            GlideNow(p, &x, &y);
            /* A tap or a gravity step glides from where it was drawn, at
             * most half a cell behind; auto-repeat, soft drop and Snap
             * snap. A turn's kick glides like a tap. */
            float nx = (!in->glide || (dx && in->moveRepeat)) ? 0.0f : ClampCells(x - (float)dx);
            float ny = (!in->glide || g->justSoftDropped) ? 0.0f : ClampCells(y - (float)dy);
            p->ox0 = nx;
            p->oy0 = ny;
            p->glideT = 0.0f;
        }
        if (g->justRotated && in->glide) p->popT = 0.0f;
        if (g->justSoftDropped && in->motion) p->streakT = 0.0f;
    }
    if (playing) {
        p->type = g->current;
        p->rot = g->rotation;
        p->px = g->px;
        p->py = g->py;
    } else if (g->phase == GS_GAMEOVER) {
        p->on = false;
    }

    /* Hold is ready again: the next piece came in after a hold. */
    if (f->lastHoldUsed && !g->holdUsed && playing) f->holdReadyT = 0.0f;
    /* A piece came in from Next: the old preview slides out. */
    bool spawned = g->justLocked || (g->justHeld && f->lastHold < 0);
    if (spawned && in->motion) {
        f->slideOn = true;
        f->slideT = 0.0f;
        f->slideOld = f->lastNext;
        f->slideOldPower = f->lastNextPower;
        f->slideOldCell = f->lastNextCell;
    }
    f->lastNext = g->next;
    f->lastNextPower = g->nextPower;
    f->lastNextCell = g->nextPowerCell;
    f->lastHold = (int)g->hold;
    f->lastHoldUsed = g->holdUsed;
}

void Feel_Tick(Feel *f, float dt) {
    ClearReplay *r = &f->clear;
    r->prevT = r->t;
    if (r->on) r->t = Tick(r->t, dt);
    PieceFeel *p = &f->piece;
    p->glideT = Tick(p->glideT, dt);
    p->spawnT = Tick(p->spawnT, dt);
    p->popT = Tick(p->popT, dt);
    p->streakT = Tick(p->streakT, dt);
    f->land.t = Tick(f->land.t, dt);
    f->quadFrameT = Tick(f->quadFrameT, dt);
    f->flightT = Tick(f->flightT, dt);
    if (f->flightT >= HOLD_FLIGHT_SECONDS) f->flightOn = false;
    f->holdReadyT = Tick(f->holdReadyT, dt);
    f->slideT = Tick(f->slideT, dt);
    if (f->slideT >= NEXT_SLIDE_SECONDS) f->slideOn = false;
}
