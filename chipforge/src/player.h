#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include "song.h"

/* Chipforge's own playback: the ghost-common synth (musicsynth.h) streamed
 * through raylib at full intensity, so you hear every track you wrote. Unlike
 * the games' music.c there are no modes or crossfades: it plays, loops a
 * pattern, or sits silent with its position frozen. The player keeps a
 * POINTER to the song, so edits are heard on the next step. Silent no-op if
 * the audio device is unavailable (Sfx_Init opens it; call that first). */

void Player_Init(const Song *song);
void Player_Shutdown(void);

/* Starts at orderPos/step. loopPattern >= 0 repeats that pattern instead of
 * following the order. */
void Player_Play(int orderPos, int step, int loopPattern);
void Player_Stop(void);
bool Player_IsPlaying(void);

/* Where the synth is right now (valid while playing; the last position once stopped). */
int Player_OrderPos(void);
int Player_Step(void);
int Player_Pattern(void);

/* Sounds one note (tone track) or drum bit set (TRACK_DRUMS) immediately,
 * playing or not. */
void Player_Preview(int track, int note);

void Player_SetVolume(int percent0to100);

/* How much of the song the games would play right now: the synth's
 * intensity (bass and arp always; hats from about 0.15, kick 0.25, lead
 * 0.35, snare 0.5; 1.0 is everything). The recipe screen uses it to audition
 * "what a game plays between the action". Only the song synth is affected,
 * previews always sound in full. Default 1.0; callers that lower it must put
 * it back to 1.0 on every way out or the editor plays quietly. */
void Player_SetIntensity(float intensity0to1);

/* --- playing along (F10) and the Timing test ------------------------------
 * The audio thread publishes, after every burst it renders, which song frame
 * the burst began with and the wall time it was asked for (Player_Now's
 * clock). jam.c's JamClock turns those readings into a smooth playhead on
 * the game thread. A new Play (or click test) starts a new generation, and
 * the reading is `current` only once it belongs to the latest one. */
typedef struct {
    bool current;       /* a reading of the latest Play has been published */
    unsigned gen;       /* which Play it belongs to */
    double seconds;     /* song seconds at the first frame of that burst (frames rendered since the Play / JAM_RATE) */
    double at;          /* Player_Now() when the burst was asked for */
} PlayerClock;
PlayerClock Player_Clock(void);
/* Monotonic wall seconds, the same clock the audio thread stamps with. */
double Player_Now(void);
/* A click on every beat step (0, 4, 8, ...; a higher one on each bar) of
 * the playing song, started on the exact sample the step starts on. Its own
 * little voice, so it never cuts off a previewed note. */
void Player_SetClick(bool on);
/* The Timing test: stops the song and clicks `beats` times at `bpm` from the
 * first frame (a higher click every fourth), publishing the clock as a Play
 * does. Player_Stop ends it; Player_Play leaves it. */
void Player_ClickTest(int bpm, int beats);

#endif
