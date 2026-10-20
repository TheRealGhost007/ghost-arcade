#include "app.h"
#include "comfort.h"

static void Changed(App *a, int dir, bool back) {
    a->slideDir = dir;
    a->slideT = 0.0f;
    a->wentBack = back;
    a->changed = true;
}

void App_Open(App *a, ScreenId s) {
    if (Nav_Push(&a->nav, (int)s)) Changed(a, 1, false);
}

bool App_Back(App *a) {
    if (!Nav_Back(&a->nav)) return false;
    Changed(a, -1, true);
    return true;
}

void App_Home(App *a) {
    Nav_Home(&a->nav);
    Changed(a, -1, true);
}

ScreenId App_Top(const App *a) { return (ScreenId)Nav_Top(&a->nav); }

int App_SlideOffset(const App *a) { return Comfort_Motion() ? Slide_Offset(a->slideT, a->slideDir) : 0; }

void App_Wipe(App *a) {
    Wipe_Start(&a->wipe, Comfort_Motion());
    if (a->wipe.on) a->slideT = SLIDE_SECONDS; /* the wipe is the change; nothing slides under it */
}

void App_Tick(App *a, float dt) {
    if (a->slideT < SLIDE_SECONDS) a->slideT += dt;
    Wipe_Tick(&a->wipe, dt);
}
