#include "attract.h"
#include <string.h>

void Attract_Init(Attract *a) {
    memset(a, 0, sizeof(*a));
    a->sinceWake = ATTRACT_WAKE_SETTLE; /* nothing to settle from */
}

bool Attract_Settling(const Attract *a) { return !a->on && a->sinceWake < ATTRACT_WAKE_SETTLE; }

float Attract_After(int setting) {
    switch (setting) {
        case ATTRACT_1MIN: return 60.0f;
        case ATTRACT_2MIN: return 120.0f;
        default: return 0.0f;
    }
}

int Attract_Next(int from, int count, const bool *shown) {
    for (int step = 1; step < count; step++) {
        int p = ((from + step) % count + count) % count;
        if (!shown || shown[p]) return p;
    }
    return from;
}

int Attract_ShownPlace(const Attract *a, int place) { return a->on ? a->place : place; }

float Attract_CardSlide(const Attract *a, float distance) {
    float t = a->cardOut;
    return t * t * (3.0f - 2.0f * t) * distance; /* eases in and out: a slide, not a jump */
}

static void SlideCard(Attract *a, float dt) {
    if (a->on) {
        a->cardOut += dt / ATTRACT_CARD_OUT;
        if (a->cardOut > 1.0f) a->cardOut = 1.0f;
    } else {
        a->cardOut -= dt / ATTRACT_CARD_BACK;
        if (a->cardOut < 0.0f) a->cardOut = 0.0f;
    }
}

/* While it tours: on to the next machine every ATTRACT_GLIDE seconds. */
static AttractEvent Tour(Attract *a, const AttractIn *in, float dt) {
    if (a->place >= in->count) a->place = in->count - 1; /* the row got shorter */
    a->sinceGlide += dt;
    if (a->sinceGlide < ATTRACT_GLIDE) return ATTRACT_NONE;
    a->sinceGlide = 0.0f;
    int next = Attract_Next(a->place, in->count, in->shown);
    if (next == a->place) return ATTRACT_NONE;
    a->place = next;
    return ATTRACT_GLIDED;
}

AttractEvent Attract_Step(Attract *a, const AttractIn *in, float dt) {
    AttractEvent ev = ATTRACT_NONE;
    if (a->sinceWake < ATTRACT_WAKE_SETTLE) a->sinceWake += dt;
    if (a->on) {
        if (in->input) {
            a->on = false; /* the press only wakes the hall */
            a->sinceWake = 0.0f;
            ev = ATTRACT_WOKE;
        } else if (!in->onHall || in->gameRunning || in->count <= 0) {
            a->on = false; /* the hall went away by itself (a catalog that emptied) */
        } else {
            ev = Tour(a, in, dt);
        }
        if (!a->on) a->idle = 0.0f;
        SlideCard(a, dt);
        return ev;
    }
    float after = Attract_After(in->setting);
    bool quiet = in->onHall && !in->gameRunning && !in->input && !in->held && in->count > 0 && after > 0.0f;
    a->idle = quiet ? a->idle + dt : 0.0f;
    if (quiet && a->idle >= after) {
        a->on = true;
        a->place = in->place;
        a->sinceGlide = 0.0f;
        ev = ATTRACT_BEGAN;
    }
    SlideCard(a, dt);
    return ev;
}

/* ---------------------------------------------------------- the setting */

#define ATTRACT_KEY "attract_mode"
static const char *const kWords[ATTRACT_SETTINGS] = {[ATTRACT_OFF] = "off", [ATTRACT_1MIN] = "1min", [ATTRACT_2MIN] = "2min"};
static const char *const kNames[ATTRACT_SETTINGS] = {[ATTRACT_OFF] = "Off", [ATTRACT_1MIN] = "1 min", [ATTRACT_2MIN] = "2 min"};

/* Anything else in the file (a typo, a hand edit) is the default. */
int Attract_Setting(const LauncherIni *ini) {
    const char *v = LauncherIni_Get(ini, ATTRACT_KEY);
    for (int i = 0; v && i < ATTRACT_SETTINGS; i++) if (strcmp(v, kWords[i]) == 0) return i;
    return ATTRACT_1MIN;
}

bool Attract_SaveSetting(LauncherIni *ini, int setting) {
    if (setting < 0 || setting >= ATTRACT_SETTINGS) setting = ATTRACT_1MIN;
    return LauncherIni_Store(ini, ATTRACT_KEY, kWords[setting]);
}

const char *Attract_SettingName(int setting) { return setting >= 0 && setting < ATTRACT_SETTINGS ? kNames[setting] : ""; }
