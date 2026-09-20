#ifndef TUNINGUI_H
#define TUNINGUI_H

#include <stdbool.h>

/* The F2 panel. Call Tune_UiUpdate once per frame; while it returns true the
 * game should hold still (skip its update). The panel draws itself through the
 * Win overlay hook. Up/Down pick a knob, Left/Right change it (Shift = x5),
 * R resets the row, Backspace resets all, S saves, F2 or Esc closes. */
void Tune_UiInit(void);
bool Tune_UiUpdate(void);
bool Tune_UiOpen(void);

#endif
