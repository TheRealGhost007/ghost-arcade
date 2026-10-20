#include "comfort.h"
#include "prefs.h"

static bool sReducedMotion = false;
static bool sPieceSnap = false;

void Comfort_SetReducedMotion(bool reduced) { sReducedMotion = reduced; }

bool Comfort_Motion(void) { return !sReducedMotion; }

bool Comfort_Calm(void) { return Prefs_Get()->reducedFlashing; }

int Comfort_Particles(int n) {
    if (n <= 0 || !sReducedMotion) return n;
    return n / 2 > 0 ? n / 2 : 1;
}

void Comfort_SetPieceSnap(bool snap) { sPieceSnap = snap; }

bool Comfort_PieceGlide(void) { return !sPieceSnap && !sReducedMotion; }
