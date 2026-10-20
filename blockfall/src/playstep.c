#include "playstep.h"
#include "persist.h"
#include <string.h>

PlayHandling PlayHandling_Default(void) { return (PlayHandling){PLAY_DAS_SECONDS, PLAY_ARR_SECONDS, PLAY_SOFT_DROP_SECONDS}; }

PlayHandling PlayHandling_From(int delayMs, int speedMs, int softSpeed) {
    if (delayMs < REPEAT_DELAY_MIN) delayMs = REPEAT_DELAY_MIN;
    if (delayMs > REPEAT_DELAY_MAX) delayMs = REPEAT_DELAY_MAX;
    if (speedMs < REPEAT_SPEED_MIN) speedMs = REPEAT_SPEED_MIN;
    if (speedMs > REPEAT_SPEED_MAX) speedMs = REPEAT_SPEED_MAX;
    PlayHandling h = PlayHandling_Default();
    /* today's numbers stay exactly today's floats */
    if (delayMs != REPEAT_DELAY_DEFAULT) h.delay = (float)delayMs / 1000.0f;
    if (speedMs != REPEAT_SPEED_DEFAULT) h.speed = (float)speedMs / 1000.0f;
    switch (softSpeed) {
        case SOFT_SLOW: h.soft = PLAY_SOFT_SLOW_SECONDS; break;
        case SOFT_FAST: h.soft = PLAY_SOFT_FAST_SECONDS; break;
        case SOFT_INSTANT: h.soft = 0.0f; break;
        default: break;
    }
    return h;
}

void PlayDriver_SetHandling(PlayDriver *d, PlayHandling h) {
    d->handling = h;
    d->handlingSet = true;
}

void PlayDriver_Start(PlayDriver *d, bool readyGo, bool restart) {
    StartCue *c = &d->cue;
    memset(c, 0, sizeof(*c));
    if (!readyGo) return;
    c->stage = restart ? CUE_GO : CUE_READY;
    c->goSound = restart; /* after R, GO at once */
}

void PlayDriver_Countdown(PlayDriver *d) {
    StartCue *c = &d->cue;
    if (c->stage == CUE_READY) return; /* READY still holds the run */
    memset(c, 0, sizeof(*c));
    c->stage = CUE_COUNT;
}

bool PlayDriver_Waiting(const PlayDriver *d) { return d->cue.stage == CUE_READY || d->cue.stage == CUE_COUNT; }

int PlayDriver_CountNumber(const StartCue *c) {
    if (c->stage != CUE_COUNT) return 0;
    int n = 3 - (int)(c->t / CUE_COUNT_STEP);
    return n < 1 ? 1 : (n > 3 ? 3 : n);
}

/* A turn, and whether a turn that was pressed could not be made. */
static void Turn(Game *g, int dir, PlayStepResult *out) {
    if (Game_Rotate(g, dir)) out->rotated = true;
    else if (g->phase == GS_PLAYING && g->current != PIECE_O) out->rotateBlocked = true;
}

static void KeepTurn(StartCue *c, int dir) {
    if (c->turns < CUE_MAX_TURNS) c->turnDir[c->turns++] = dir;
}

void Play_Step(PlayDriver *d, Game *g, const PlayIntent *in, float dt, float speed, PlayStepResult *out) {
    memset(out, 0, sizeof(*out));
    StartCue *c = &d->cue;
    PlayHandling h = d->handlingSet ? d->handling : PlayHandling_Default();

    if (c->stage == CUE_READY || c->stage == CUE_COUNT) {
        /* Nothing moves or falls and no clock runs. A key held now acts as
         * a fresh press at GO, so auto-repeat starts over. */
        RepeatButton_Update(&d->left, false, dt, h.delay, h.speed);
        RepeatButton_Update(&d->right, false, dt, h.delay, h.speed);
        RepeatButton_Update(&d->softDrop, false, dt, 0.0f, h.soft);
        if (g->phase != GS_PLAYING) return; /* paused: READY waits too */
        out->waiting = true;
        if (in->rotate) KeepTurn(c, 1);
        if (in->rotateLeft) KeepTurn(c, -1);
        if (in->hold) c->hold = true;
        c->t += dt;
        bool count = c->stage == CUE_COUNT;
        if (c->t < (count ? CUE_COUNT_SECONDS : CUE_READY_SECONDS)) return;
        /* GO (or the countdown's end): what was pressed while it waited
         * happens now (the hold first, so a turn is made on the piece that
         * comes in), and play begins with the next frame. */
        c->stage = count ? CUE_NONE : CUE_GO;
        c->t = 0.0f;
        c->goSound = !count;
        if (c->hold && Game_Hold(g)) out->held = true;
        for (int i = 0; i < c->turns; i++) Turn(g, c->turnDir[i], out);
        c->turns = 0;
        c->hold = false;
        return;
    }

    /* As the run has always been played (with your Feel settings). */
    bool leftWasHeld = d->left.wasHeld, rightWasHeld = d->right.wasHeld;
    bool doLeft = RepeatButton_Update(&d->left, in->left, dt, h.delay, h.speed);
    bool doRight = RepeatButton_Update(&d->right, in->right, dt, h.delay, h.speed);
    bool doSoftDrop = RepeatButton_Update(&d->softDrop, in->softDrop, dt, 0.0f, h.soft);
    if (g->phase != GS_PLAYING) return;

    /* A repeat speed of 0: once the delay is over, straight to the wall. */
    bool wall = h.speed <= 0.0f;
    if (doLeft && Game_MoveLeft(g)) {
        out->moved = true;
        if (leftWasHeld) {
            out->moveRepeat = true;
            if (wall) while (Game_MoveLeft(g)) { }
        }
    }
    if (doRight && Game_MoveRight(g)) {
        out->moved = true;
        if (rightWasHeld) {
            out->moveRepeat = true;
            if (wall) while (Game_MoveRight(g)) { }
        }
    }
    /* Instant: straight down (it does not lock; the lock delay does that). */
    if (doSoftDrop && Game_SoftDrop(g) && h.soft <= 0.0f) while (Game_SoftDrop(g)) { }
    if (in->rotate) Turn(g, 1, out);
    if (in->rotateLeft) Turn(g, -1, out);
    if (in->hardDrop) Game_HardDrop(g);
    if (in->hold && Game_Hold(g)) out->held = true;
    Game_Update(g, dt * speed);

    if (c->stage == CUE_GO) {
        c->t += dt;
        if (c->t >= CUE_GO_SECONDS) c->stage = CUE_NONE;
    }
}
