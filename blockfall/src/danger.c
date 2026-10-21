#include "danger.h"
#include <math.h>
#include <string.h>

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Danger_Reset(Danger *d) {
    memset(d, 0, sizeof(*d));
    d->sinceWarn = 1000.0f; /* the first danger warns at once */
    d->freeRows = BOARD_MAX_H;
}

int Danger_FreeRows(const Board *b) {
    int total = Board_TotalH(b);
    for (int row = BOARD_HIDDEN; row < total; row++)
        for (int x = 0; x < b->w; x++)
            if (b->cells[row][x] != CELL_EMPTY) return row - BOARD_HIDDEN;
    return b->h;
}

bool Danger_NextBlocked(const Game *g) {
    if ((int)g->next < 0 || g->next >= PIECE_COUNT) return false;
    return !Board_TestFit(&g->board, g->next, 0, (g->board.w - 4) / 2, BOARD_HIDDEN);
}

bool Danger_Hysteresis(bool was, int freeRows) {
    if (freeRows <= DANGER_ON_ROWS) return true;
    if (freeRows >= DANGER_OFF_ROWS) return false;
    return was;
}

void Danger_Update(Danger *d, const Game *g, float dt) {
    d->warn = false;
    if (g->phase != GS_PLAYING) return; /* paused or over: it holds as it is */
    if (dt < 0.0f) dt = 0.0f;
    d->sinceWarn += dt;
    d->freeRows = Danger_FreeRows(&g->board);
    bool was = d->on;
    d->on = g->cfg.mode != MODE_ZEN && Danger_Hysteresis(was, d->freeRows);
    d->nextBlocked = d->on && Danger_NextBlocked(g);
    if (d->on && !was && d->sinceWarn >= DANGER_WARN_GAP_SECONDS) {
        d->warn = true;
        d->sinceWarn = 0.0f;
    }
    float step = DANGER_EASE_SECONDS > 0.0f ? dt / DANGER_EASE_SECONDS : 1.0f;
    if (d->on) {
        d->ease = Clamp01(d->ease + step);
        d->t += dt;
    } else {
        d->ease = Clamp01(d->ease - step);
        if (d->ease <= 0.0f) d->t = 0.0f;
    }
}

float Danger_Frame(const Danger *d, bool calm) {
    if (d->ease <= 0.0f) return 0.0f;
    if (calm) return d->ease;
    float wave = 0.5f + 0.5f * sinf(d->t * DANGER_PULSE_HZ * 2.0f * 3.14159265f);
    return d->ease * (DANGER_PULSE_MIN + (1.0f - DANGER_PULSE_MIN) * wave);
}

float Danger_Stripes(const Danger *d) { return d->ease; }

float Danger_Intensity(const Game *g, int freeRows, bool danger) {
    float v;
    switch (Game_Timed(g) ? MODE_ULTRA : g->cfg.mode) { /* a daily follows its clock as Ultra does (3.6.0) */
        case MODE_ZEN: return INTENSITY_ZEN;
        case MODE_SPRINT: v = 0.35f + 0.55f * Clamp01((float)g->linesCleared / (float)SPRINT_LINES); break;
        case MODE_ULTRA: {
            int sec = g->timeLeft <= 0.0f ? 0 : (int)g->timeLeft;
            v = 0.35f + 0.55f * Clamp01(1.0f - g->timeLeft / ULTRA_SECONDS);
            if (sec <= 10) v = 1.0f;
            break;
        }
        default: {
            float levelK = Clamp01((float)(g->level - 1) / (float)(INTENSITY_FULL_LEVEL - 1));
            int h = g->board.h > 0 ? g->board.h : 1;
            if (freeRows < 0) freeRows = 0;
            if (freeRows > h) freeRows = h;
            float stackK = (float)(h - freeRows) / (float)h;
            v = INTENSITY_FLOOR + INTENSITY_LEVEL_SHARE * levelK + INTENSITY_STACK_SHARE * stackK;
            break;
        }
    }
    if (danger) v += DANGER_INTENSITY_BONUS;
    return Clamp01(v);
}

bool Danger_UltraTick(int prevSecond, int second) {
    return second != prevSecond && second >= 1 && second <= ULTRA_TICK_SECONDS;
}

bool Danger_SprintGold(int linesLeft) { return linesLeft > 0 && linesLeft <= SPRINT_GOLD_LINES; }
