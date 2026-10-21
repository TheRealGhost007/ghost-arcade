#ifndef POWERFX_H
#define POWERFX_H

#include <stdbool.h>

/* How each power-up's show runs in time (REWORK_PLAN.md, "Power-ups with
 * their own look and sound"), decided without raylib so it can be tested
 * headlessly; effects.c only draws it. All of it is looks only: the rules
 * fire every power-up on the frame the piece locks, exactly as before, and
 * nothing here changes when anything falls, locks or scores.
 *  - Bomb: the 3x3 it took stays in view and blinks for 0.12 s (three
 *    beats; steady lit with Reduced flashing), then bursts in its own
 *    colours as a square shockwave spreads for 0.35 s and the well shakes
 *    9 px. Its sound has the fuse built in.
 *  - Laser: a charge line runs down the column for 0.12 s (at once with
 *    Reduced motion), then the beam shows for 0.28 s and the column's cells
 *    pop out from the power cell, 0.0065 s per row of distance.
 *  - Freeze: an ice wash crosses the well in 0.30 s, frost grows in from
 *    the corners over 0.6 s (at once with Reduced motion) and holds. In the
 *    last 1.5 s the frost blinks at 4 Hz (steady, a little dimmer, with
 *    Reduced flashing). When it ends the frost breaks over 0.3 s, with a
 *    thaw tick.
 *  - Slow: the orange frame, two faint afterimages trailing the piece (3
 *    and 6 frames behind; none with Reduced motion), the draining bar and a
 *    wind-up sound as it ends.
 *  - Sweep: feel.h's replay (a green bar crosses the bottom three rows
 *    and bursts them, then the stack drops). */

#define BOMB_BLINK_SECONDS 0.12f
#define BOMB_BLINK_BEAT 0.04f
#define BOMB_SHOCK_SECONDS 0.35f
#define BOMB_SHAKE_PX 9.0f
#define LASER_CHARGE_SECONDS 0.12f
#define LASER_BEAM_SECONDS 0.28f
#define LASER_POP_PER_ROW 0.0065f /* the farthest cell of a 36-tall well pops before the beam fades */
#define FREEZE_WASH_SECONDS 0.30f
#define FROST_GROW_SECONDS 0.60f
#define FREEZE_BLINK_SECONDS 1.5f
#define FREEZE_BLINK_HZ 4.0f
#define FREEZE_CALM_LIT 0.7f
#define FREEZE_BLINK_LOW 0.35f
#define THAW_SECONDS 0.30f
#define SLOW_AFTERIMAGES 2
#define SLOW_AFTERIMAGE_GAP 3   /* frames between the piece and each afterimage */
#define SLOW_ENDING_SECONDS 1.5f

typedef enum { BOMB_NONE, BOMB_BLINK, BOMB_SHOCK } BombStage;
/* The stage at `age` seconds after the lock (BOMB_NONE once it is over). */
BombStage PowerFx_BombStage(float age);
/* The burst happens between these two frames (the blink just ended). */
bool PowerFx_BombBursts(float age, float prevAge);
/* The blinking cells are lit this frame (steady lit with Reduced flashing). */
bool PowerFx_BombLit(float age, bool calm);
/* The shockwave's progress, 0 to 1 (-1 outside it). Reduced motion: it does
 * not spread, so the caller draws it at full size and only fades it. */
float PowerFx_BombShock(float age);

typedef enum { LASER_NONE, LASER_CHARGE, LASER_BEAM } LaserStage;
LaserStage PowerFx_LaserStage(float age);
/* How far down the column the charge line has got, 0 to 1 (1 at once with
 * Reduced motion). */
float PowerFx_LaserCharge(float age, bool motion);
/* The beam's brightness, 1 fading to 0. */
float PowerFx_LaserBeam(float age);
/* When a cell `dist` rows from the power cell pops, seconds after the lock. */
float PowerFx_LaserPopAt(int dist);

/* The ice wash's way down the well, 0 to 1 (-1 once it is past; never with
 * Reduced motion). */
float PowerFx_FreezeWash(float age, bool motion);
/* The frost's size, 0 to 1, `age` seconds after the Freeze fired (full at
 * once with Reduced motion); 0 once the timer is out. */
float PowerFx_Frost(float age, float timer, bool motion);
/* How lit the frost and the ice frame are: 1, blinking between 1 and 0.35
 * in the last 1.5 s (steady 0.7 with Reduced flashing). */
float PowerFx_FreezeLit(float timer, bool calm);
/* The frost breaking after the Freeze ended, 1 fading to 0 over 0.3 s. */
float PowerFx_Thaw(float age);

/* The Slow is in its last 1.5 s (the frame eases off). */
bool PowerFx_SlowEnding(float timer);
/* How many afterimages to draw (none with Reduced motion). */
int PowerFx_Afterimages(bool motion);
/* An afterimage's alpha: the first 0.22, the second 0.12. */
float PowerFx_AfterimageAlpha(int i);

#endif
