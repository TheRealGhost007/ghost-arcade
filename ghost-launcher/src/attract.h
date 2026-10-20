#ifndef ATTRACT_H
#define ATTRACT_H

#include <stdbool.h>
#include "launcherini.h"

/* Attract mode, without raylib: what a real arcade does when nobody is
 * playing. After a while with no input on the hall (Settings > Screen >
 * Attract mode: Off, 1 min or 2 min; launcher.ini attract_mode), the card
 * slides away, a large PRESS ANY BUTTON shows, and every few seconds the
 * row glides on to the next installed machine, whose demo plays. Your own
 * machine never changes: the tour only moves what is drawn lit. Any key,
 * pad button, click, wheel or pointer move wakes it and the row glides back
 * to your machine; that press does nothing else (main.c skips the frame's
 * input), so waking can never drop a coin. */

typedef enum { ATTRACT_OFF, ATTRACT_1MIN, ATTRACT_2MIN, ATTRACT_SETTINGS } AttractSetting;

#define ATTRACT_GLIDE 6.0f     /* seconds on each machine */
#define ATTRACT_CARD_OUT 0.4f  /* the card sliding away */
#define ATTRACT_CARD_BACK 0.25f
#define ATTRACT_WAKE_SETTLE 0.4f /* a click this soon after waking is part of the waking */

typedef struct {
    bool on;          /* touring the row */
    float idle;       /* seconds with nothing pressed on the hall */
    float sinceGlide; /* seconds on the machine shown */
    int place;        /* the machine shown lit while on: a place in the row */
    float cardOut;    /* 0 the card is in place .. 1 it has slid away */
    float sinceWake;  /* seconds since it last woke */
} Attract;

typedef struct {
    int setting;       /* AttractSetting */
    bool onHall;       /* the hall is up with nothing over it and no coin in flight */
    bool gameRunning;  /* the quiet hall wins: a game the launcher started is up */
    bool input;        /* any key, pad button, click, wheel or pointer move this frame */
    bool held;         /* anything still held down: someone is there */
    int place;         /* your machine */
    int count;         /* machines on the row */
    const bool *shown; /* per place: installed, so it has a demo to show (NULL = all) */
} AttractIn;

typedef enum {
    ATTRACT_NONE,
    ATTRACT_BEGAN,  /* it just started: forget presses half made on the hall */
    ATTRACT_GLIDED, /* on to the next machine */
    ATTRACT_WOKE,   /* this frame's input woke it, and must do nothing else */
} AttractEvent;

void Attract_Init(Attract *a);
/* Seconds of quiet before it starts: 0 when off. */
float Attract_After(int setting);
AttractEvent Attract_Step(Attract *a, const AttractIn *in, float dt);
/* The next place after `from` with something to show, round the end;
 * `from` itself when no other place has. */
int Attract_Next(int from, int count, const bool *shown);
/* How far down the card is, eased, for a slide of `distance` pixels. */
float Attract_CardSlide(const Attract *a, float distance);
/* Just woken: the row is still gliding back to your machine, and a click
 * now (the second half of the move that woke it) must not land on whatever
 * machine is passing under the pointer. */
bool Attract_Settling(const Attract *a);
/* The place drawn lit: the tour's while it is on, else yours. */
int Attract_ShownPlace(const Attract *a, int place);

/* launcher.ini attract_mode=off|1min|2min (1 min unless set). */
int Attract_Setting(const LauncherIni *ini);
bool Attract_SaveSetting(LauncherIni *ini, int setting);
/* "Off", "1 min", "2 min". */
const char *Attract_SettingName(int setting);

#endif
