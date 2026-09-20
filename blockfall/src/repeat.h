#ifndef REPEAT_H
#define REPEAT_H

#include <stdbool.h>

/* Delay-Auto-Shift style repeat-button state machine, timed with an
 * accumulator (not frame counts) so behavior is stable across frame rates. */
typedef struct {
    bool wasHeld;
    bool dasElapsed;
    float timer;
} RepeatButton;

/* Returns true on the frame an action should fire: immediately on initial
 * press, then after dasDelay seconds, then every arrRate seconds while
 * still held. */
bool RepeatButton_Update(RepeatButton *rb, bool held, float dt, float dasDelay, float arrRate);

#endif
