#ifndef JAM_H
#define JAM_H

#include <stdbool.h>
#include "song.h"
#include "musicsynth.h"
#include "editor.h"
#include "take.h"

/* Playing along (F10): recording over a looping pattern, caught to the beat.
 * No raylib, so the whole path from "a key went down at this moment" to "this
 * cell of the pattern changed" runs headless:
 *
 *   timing    when the synth really triggers each step (swing and the parked
 *             first step included), and the other way round
 *   JamClock  the audio thread publishes how far it has rendered and when;
 *             this turns those bursts into a smooth clock on the game thread
 *   taps      the Timing test: how late you hear (and tap) a click
 *   Jam       the session: passes, the count-in, the catch grid, the track,
 *             each pass written as one undo entry
 *
 * The player's sample rate is pinned here because the step times are counted
 * in its samples (player.c checks it with a _Static_assert). */

#define JAM_RATE 44100
/* Recording starts the loop on its last bar, so the first pass is preceded
 * by one bar of the song with four clicks: the count-in. */
#define JAM_START_STEP 48
#define JAM_COUNT_IN_STEPS (SONG_STEPS - JAM_START_STEP)

/* --- timing --------------------------------------------------------------- */

/* Seconds after a Play from startStep at which the synth triggers the k-th
 * step after it (k = 0 is startStep itself, at 0.0; the count runs on across
 * the pattern's end). Exact to the sample: the synth parks one step before the
 * start so the first rendered sample triggers it, then even steps run long
 * and odd ones short by swing x step, and it fires on the first whole sample
 * at or past each boundary. Tempo comes from the song as it is now. */
double Jam_TimeOfStep(const Song *s, int startStep, long k);
/* The inverse: how many steps after the start the synth is at `seconds`, with
 * the fraction of the way through the current step (a swung step's fraction
 * is of its own, swung length). Jam_StepAt(Jam_TimeOfStep(k)) is exactly k.
 * Before 0 it counts back in plain steps. */
double Jam_StepAt(const Song *s, int startStep, double seconds);
/* Frames Synth_Render will produce before it triggers the synth's next step
 * (0: the very next frame triggers it), worked out from the synth's own state
 * with its own arithmetic. The player uses it to start the click on exactly
 * the sample a beat step starts on. A large number for a finished synth. */
int Jam_FramesToTrigger(const Synth *s);

/* --- the clock ------------------------------------------------------------ */

/* The audio thread renders in bursts (a device period at a time) and
 * publishes, after each one, which song frame the burst began with and the
 * wall time it was asked for. Between bursts the song moves on in real time,
 * so that pair predicts where it is now; the clock follows those predictions
 * smoothly, nudging a tenth of the way toward each one, and only jumps (to the
 * new prediction) when one is out by more than JAM_CLOCK_JUMP: an underrun, a
 * stalled thread, or a new Play (a new generation). */
#define JAM_CLOCK_JUMP 0.050
#define JAM_CLOCK_SLEW 0.1

typedef struct {
    bool anchored;
    unsigned gen;       /* the Play the anchor belongs to */
    double anchorAt;    /* wall seconds */
    double anchorSec;   /* song seconds at anchorAt */
    int jumps;          /* times it re-anchored within one generation (tests read it) */
} JamClock;

void JamClock_Reset(JamClock *c);
/* One published reading: the song was at songSeconds when the audio thread
 * asked for it at wall time `at`; `now` is the wall time of this frame. */
void JamClock_Feed(JamClock *c, unsigned gen, double songSeconds, double at, double now);
/* Song seconds you are hearing at `now`: the clock minus the output latency
 * (the playhead the player knows about runs ahead of the speaker). Before the
 * first reading, -latency: the very start, not heard yet. */
double JamClock_Seconds(const JamClock *c, double now, double latency);

/* --- the Timing test ------------------------------------------------------ */

/* Taps needed among the counted beats before a timing is trusted. */
#define JAM_TAPS_NEEDED 6
#define JAM_TAP_BEATS_MAX 64

/* Clicks sound at k * beatSeconds for k in 0..beats-1, and taps are in the
 * same clock (seconds after the first click, latency 0). Each tap belongs to
 * the nearest click; only clicks from firstBeat on count (the first few are
 * for finding the beat), and a click with no tap or with two (a bounce, or a
 * guess) is left out. Returns the median offset in whole milliseconds, never
 * below 0, or -1 when fewer than JAM_TAPS_NEEDED clicks were tapped once. */
int Jam_LatencyFromTaps(const double *taps, int count, double beatSeconds, int firstBeat, int beats);

/* --- the session ---------------------------------------------------------- */

typedef struct {
    bool active;
    int pattern;        /* the looping pattern every pass is written into */
    int track;          /* where new notes go; Tab changes it */
    int grid;           /* the catch grid: 1 (1/16), 2 (1/8) or 4 (1/4) */
    int startStep;      /* where the loop was started: JAM_START_STEP */
    int pass;           /* the pass being played, from 1 (also during the count-in) */
    int kept;           /* passes written, each one undo entry */
    double pos;         /* what you hear now, in steps since the first downbeat */
    Take take;
} Jam;

/* Starts a session on the editor's pattern and track. The caller starts the
 * loop at JAM_START_STEP and feeds the steps it has played since. */
void Jam_Begin(Jam *j, const Editor *ed, int grid);
/* Moves to the heard position (steps since the Play) and writes each pass
 * once you can no longer play into it: half a catch step before the loop
 * comes round, after which a note is caught onto the next pass's step 0.
 * Each pass with notes is one Editor commit (one undo entry, the song marked
 * changed) with the status "Pass 3: 7 notes". */
void Jam_Tick(Jam *j, Editor *ed, double stepsSincePlay);
/* A key went down on the current track (a DRUM_ bit on the drums): returns
 * the press's id, for Jam_NoteOffId when the key comes up; -1 when not
 * recording or the pass is full. */
int Jam_NoteOn(Jam *j, int note);
/* A key came up: ends the press Jam_NoteOn returned for it, on whatever track
 * it went to (Tab may have left it). Nothing for -1, or for a press already
 * ended (Tab, a written pass, Backspace). By press, not by pitch: two keys
 * held at once can give one pitch (an octave change mid-hold, or the top
 * octave's clamp), and a release by pitch would end the other key's note. */
void Jam_NoteOffId(Jam *j, int id);
/* A key came up, by pitch: the latest held press of this note on that track.
 * For a caller that did not keep the id; Jam_NoteOffId is exact. */
void Jam_NoteOff(Jam *j, int track, int note);
/* Tab: later notes go to this track; notes still held are ended here. */
void Jam_SetTrack(Jam *j, int track);
/* F12: 1/16 -> 1/8 -> 1/4 -> 1/16. */
int Jam_NextGrid(int grid);
const char *Jam_GridName(int grid);   /* "1/16", "1/8", "1/4" */
/* Backspace: throws away the pass being played (nothing is written). Returns
 * the notes it had. */
int Jam_DropPass(Jam *j);
/* F10 or Esc: ends held notes and writes the pass being played (the notes
 * that started in it) as one more undo entry, then stops the session.
 * Returns the notes written by that last pass. */
int Jam_End(Jam *j, Editor *ed);
/* 4, 3, 2, 1 through the count-in bar, then 0. */
int Jam_CountIn(const Jam *j);
/* The pass being played, caught onto an empty pattern (held notes up to now),
 * for drawing in dim over the real one. False when it has no notes yet. */
bool Jam_LivePass(const Jam *j, SongPattern *out);

#endif
