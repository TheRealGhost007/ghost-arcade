#ifndef SOUNDPLAN_H
#define SOUNDPLAN_H

#include <stdbool.h>
#include "game.h"
#include "danger.h"

/* Which sounds a frame of play asks for (REWORK_PLAN.md, "Sound pass"),
 * decided without raylib so it can be tested headlessly; scr_play.c only
 * hands the cues to the shared sound code (sfx.h). Nothing here feeds back
 * into the rules.
 *  - Seventeen sounds, all made in code (tools/gen_sounds.py): the
 *    seven from 1.0.0, then a soft tick for a natural lock, the hold
 *    swoosh, one for each power-up (and the thaw tick when a Freeze ends,
 *    the wind-up when a Slow ends), the sweep and a back-to-back sparkle.
 *    (3.1.0's own danger sound went in 3.6.1: danger uses the shared
 *    WARNING.)
 *  - Variety from the calls that already exist: a move varies a little, a
 *    single, a double and a triple rise in pitch (feel.h), each combo step
 *    adds a semitone, and the results card's count-up ticks rise.
 *  - One rule: each lock plays at most ONE big sound (a clear, a quad or a
 *    power-up: power-up first, then a perfect clear's quad sound pitched up
 *    (3.2.0), then a quad, then the clear) plus ONE accent (the
 *    back-to-back sparkle, a T-spin's sparkle pitched up (3.2.0), a
 *    power-up's soft coin, the hard drop's thud or the natural lock's tick,
 *    in that order). The level-up sound waits 0.15 s so it does not clash
 *    with the clear.
 *  - The shared jingles: GO at the start of play (READY is Play setup's),
 *    WARNING once as Ultra enters its last ten seconds and for danger,
 *    COIN for a power-up and for passing your best mid-run (NEW BEST is the
 *    results card's).
 *  - Danger (danger.h decides when, at most every 8 s, never in Zen): the
 *    shared WARNING (3.6.1), one WARNING in a frame however many ask.
 *    Ultra's last five seconds each tick, rising (the move's sound pitched
 *    up, as the results card's count-up ticks are).
 *  - Dig (3.4.0): a garbage row pushing up thuds (the hard drop's sound,
 *    low: DIG_RISE_PITCH), a small sound, so a lock on the same frame keeps
 *    its own. */

/* Blockfall's sound effects, in main.c's table order (the file names). */
enum {
    SFX_MOVE, SFX_ROTATE, SFX_HARD_DROP, SFX_CLEAR, SFX_QUAD, SFX_LEVEL_UP, SFX_GAME_OVER, /* 1.0.0 */
    SFX_LOCK, SFX_HOLD, SFX_BOMB, SFX_LASER, SFX_FREEZE, SFX_THAW, SFX_SLOW, SFX_WINDUP, SFX_SWEEP, SFX_B2B, /* 3.1.0 */
    /* (3.1.0's SFX_DANGER, last, went in 3.6.1: danger plays the shared
     * WARNING. Nothing persists these numbers.) */
    SFX_COUNT
};

#define LEVEL_UP_DELAY_SECONDS 0.15f
#define SEMITONE 1.059463f
#define COMBO_PITCH_STEPS 12      /* the combo's rise stops an octave up */
#define MOVE_VARIANCE 0.04f
#define ULTRA_WARN_SECONDS 10     /* WARNING as the countdown turns red at 0:10 */
#define ULTRA_TICK_PITCH(sec) (1.30f + 0.05f * (float)(ULTRA_TICK_SECONDS - (sec))) /* 5 to 1: each tick a little higher */
#define SOUND_CUES 8
#define SOUND_LATER 4
#define TSPIN_PITCH 1.30f         /* the sparkle, brighter, for a T-spin that is not back to back */
#define PERFECT_CLEAR_PITCH 1.25f /* the quad's sound, higher, for a perfect clear */
#define DIG_RISE_PITCH 0.6f       /* the hard drop's thud, low, as a garbage row pushes up (3.4.0) */
#define UNDO_PITCH 0.7f           /* the hold's swoosh, low, as the Practice room takes a piece back (3.5.0) */

/* How a cue is played: Sfx_Play, Sfx_PlayPitched(arg), Sfx_PlayVaried(arg)
 * or Sfx_Jingle (id is the SfxJingle). */
typedef enum { SND_PLAY, SND_PITCHED, SND_VARIED, SND_JINGLE } SoundHow;
/* Its weight under the one-big-sound rule. */
typedef enum { SND_SMALL, SND_ACCENT, SND_BIG } SoundWeight;
typedef struct {
    SoundHow how;
    int id;
    float arg;
    SoundWeight weight;
} SoundCue;

/* What the frame did besides the rules' own one-frame flags. */
typedef struct {
    bool moved, rotated, rotateBlocked, held; /* playstep.h's results (held: the hold was made) */
    bool go;           /* GO began this frame */
    bool passedBest;   /* the run just passed the best it started with */
    bool ending;       /* the run ended this frame: the end has its one sound, nothing else plays */
    bool danger;       /* danger began this frame and may warn (danger.h's Danger.warn) */
} SoundEvents;

typedef struct {
    SoundCue now[SOUND_CUES];
    int count;
    struct { SoundCue cue; float due; } later[SOUND_LATER];
    int laters;
    float freezeWas, slowWas; /* last frame's timers: an end is a sound */
    bool ultraWarned;
    int ultraSecond;          /* Ultra: the whole second last seen (the last five tick) */
    bool inPlay;              /* the game was GS_PLAYING last frame */
} SoundPlan;

/* A new run or a restart: nothing waiting, nothing remembered. */
void SoundPlan_Reset(SoundPlan *p, const Game *g);
/* This frame's events (call it BEFORE Game_ConsumeFrameFlags). */
void SoundPlan_React(SoundPlan *p, const Game *g, const SoundEvents *e);
/* Moves time on: a cue whose wait is over joins this frame's. */
void SoundPlan_Tick(SoundPlan *p, float dt);
/* This frame's cues, in the order to play them; they are taken. */
int SoundPlan_Take(SoundPlan *p, SoundCue *out, int max);

/* The clear sound's pitch: feel.h's by size and back-to-back, then a
 * semitone per combo step (combo as RunStats counts it: 0 for the first
 * clear of a chain), up to an octave. */
float SoundPlan_ClearPitch(int count, bool b2b, int combo);
/* The power-up's own sound. */
int SoundPlan_PowerSfx(PowerUp p);

#endif
