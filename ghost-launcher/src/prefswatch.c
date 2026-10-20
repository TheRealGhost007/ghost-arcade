#include "prefswatch.h"
#include "prefs.h"

void PrefsWatch_Init(PrefsWatch *w, bool focused) { w->focused = focused; }

bool PrefsWatch_Update(PrefsWatch *w, bool focused, bool gameExited, bool aboutToWrite) {
    bool regained = focused && !w->focused;
    w->focused = focused;
    if (!regained && !gameExited && !aboutToWrite) return false;
    Prefs_Load();
    return true;
}
