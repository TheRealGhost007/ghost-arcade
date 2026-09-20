#include "repeat.h"

bool RepeatButton_Update(RepeatButton *rb, bool held, float dt, float dasDelay, float arrRate) {
    if (!held) {
        rb->wasHeld = false;
        rb->dasElapsed = false;
        rb->timer = 0.0f;
        return false;
    }

    if (!rb->wasHeld) {
        rb->wasHeld = true;
        rb->dasElapsed = false;
        rb->timer = 0.0f;
        return true; /* fire immediately on initial press */
    }

    rb->timer += dt;
    if (!rb->dasElapsed) {
        if (rb->timer >= dasDelay) {
            rb->dasElapsed = true;
            rb->timer -= dasDelay;
            return true;
        }
        return false;
    }

    if (rb->timer >= arrRate) {
        rb->timer -= arrRate;
        return true;
    }
    return false;
}
