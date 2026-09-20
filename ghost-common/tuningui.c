#include "tuningui.h"
#include <stdio.h>
#include "raylib.h"
#include "tuning.h"
#include "ui.h"
#include "winscale.h"

static bool sOpen = false;
static int sRow = 0;
static float sSavedFlash = 0.0f;

static void Draw(void) {
    if (!sOpen) {
        /* Runs are not recorded while a knob is off its default: never let that be a secret. */
        if (Tune_Modified()) {
            const UiTheme *t = Ui_Theme();
            Ui_Text("TUNED", Win_LogicalWidth() - Ui_Measure("TUNED", UI_T8) - 6, 4, UI_T8, t->gold);
        }
        return;
    }
    const UiTheme *t = Ui_Theme();
    int n = Tune_Count();
    int w = 540, h = 90 + n * 30 + 46;
    int sw = Win_LogicalWidth();
    int px = (sw - w) / 2, py = 40;
    DrawRectangle(px - 4, py - 4, w + 8, h + 8, t->rule);
    DrawRectangle(px, py, w, h, t->board);
    Ui_TextCentered("TUNING", UI_T16, sw / 2, py + 14, t->light);
    Ui_TextCentered(Tune_Modified() ? "Changed: runs won't count on the scoreboards" : "Everything at its default", UI_T8, sw / 2, py + 40,
                    Tune_Modified() ? t->gold : t->dim);
    for (int i = 0; i < n; i++) {
        Tunable *tu = Tune_At(i);
        int y = py + 70 + i * 30;
        bool sel = i == sRow;
        if (sel) { DrawRectangle(px + 8, y - 6, w - 16, 26, t->panel); DrawRectangle(px + 8, y - 6, 4, 26, t->light); }
        Ui_Text(tu->label, px + 24, y, UI_T8, sel ? t->light : t->text);
        char buf[24];
        snprintf(buf, sizeof(buf), "%.2f", tu->value);
        bool off = tu->value - tu->def > 0.0001f || tu->def - tu->value > 0.0001f;
        Ui_Text(buf, px + w - 24 - Ui_Measure(buf, UI_T8), y, UI_T8, off ? t->gold : t->text);
        /* the slider */
        int bx = px + 230, bw = 170;
        DrawRectangle(bx, y + 2, bw, 4, t->rule);
        float frac = (tu->value - tu->min) / (tu->max - tu->min);
        DrawRectangle(bx + (int)(frac * (float)bw) - 3, y - 2, 6, 12, sel ? t->light : t->dim);
        float dfrac = (tu->def - tu->min) / (tu->max - tu->min);
        DrawRectangle(bx + (int)(dfrac * (float)bw), y + 8, 2, 3, t->faint);
    }
    Ui_TextCentered("Up/Down pick  Left/Right change  Shift bigger steps", UI_T8, sw / 2, py + h - 44, t->dim);
    Ui_TextCentered(sSavedFlash > 0.0f ? "Saved" : "R reset row  Backspace reset all  S save  F2 close", UI_T8, sw / 2, py + h - 26,
                    sSavedFlash > 0.0f ? t->gold : t->faint);
}

void Tune_UiInit(void) { Win_SetOverlay(Draw); }

bool Tune_UiOpen(void) { return sOpen; }

bool Tune_UiUpdate(void) {
    if (IsKeyPressed(KEY_F2)) { sOpen = !sOpen; if (!sOpen) Tune_Save(); }
    if (sSavedFlash > 0.0f) sSavedFlash -= GetFrameTime();
    if (!sOpen) return false;

    int n = Tune_Count();
    if (n <= 0) return true;
    if (IsKeyPressed(KEY_ESCAPE)) { sOpen = false; Tune_Save(); return true; }
    if (IsKeyPressed(KEY_UP)) sRow = (sRow + n - 1) % n;
    if (IsKeyPressed(KEY_DOWN)) sRow = (sRow + 1) % n;
    Tunable *t = Tune_At(sRow);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    float step = t->step * (shift ? 5.0f : 1.0f);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) t->value += step;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) t->value -= step;
    if (t->value > t->max) t->value = t->max;
    if (t->value < t->min) t->value = t->min;
    /* snap so repeated presses don't accumulate float drift away from the default */
    float k = (t->value - t->def) / t->step;
    float rk = (float)(int)(k < 0 ? k - 0.5f : k + 0.5f);
    if (rk - k < 0.02f && k - rk < 0.02f) t->value = t->def + rk * t->step;
    if (IsKeyPressed(KEY_R)) t->value = t->def;
    if (IsKeyPressed(KEY_BACKSPACE)) Tune_ResetAll();
    if (IsKeyPressed(KEY_S)) { Tune_Save(); sSavedFlash = 1.2f; }
    return true;
}
