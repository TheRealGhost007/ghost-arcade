#include "wipe.h"
#include <math.h>

void Wipe_Start(Wipe *w, bool motion) {
    w->on = motion;
    w->t = 0.0f;
}

void Wipe_Tick(Wipe *w, float dt) {
    if (!w->on) return;
    w->t += dt;
    if (w->t >= WIPE_SECONDS) w->on = false;
}

bool Wipe_Active(const Wipe *w) { return w->on && w->t < WIPE_SECONDS; }

float Wipe_RowStart(int row) {
    if (row < 0) row = 0;
    if (row > WIPE_ROWS - 1) row = WIPE_ROWS - 1;
    float span = WIPE_SECONDS - WIPE_ROW_FLASH - WIPE_ROW_ZIP;
    return span * (float)row / (float)(WIPE_ROWS - 1);
}

WipeCell Wipe_Cell(const Wipe *w, int row, int col) {
    if (!Wipe_Active(w)) return WIPE_GONE;
    float t = w->t - Wipe_RowStart(row);
    if (t < 0.0f) return WIPE_COVERED;
    if (t < WIPE_ROW_FLASH) return WIPE_FLASH;
    /* The zip: the middle blocks go first, then outward to both sides; at
     * its end the whole row is gone. */
    float k = (t - WIPE_ROW_FLASH) / WIPE_ROW_ZIP;
    float half = (float)WIPE_COLS / 2.0f;
    float from = fabsf((float)col + 0.5f - half) + 0.5f; /* 1 in the middle .. half at the edges */
    return k * half >= from ? WIPE_GONE : WIPE_FLASH;
}
