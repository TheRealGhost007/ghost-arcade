/* Playing along: the synth's real step times, the clock that follows the
 * audio thread, the Timing test and the recording session. See jam.h. */
#include "jam.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- timing --------------------------------------------------------------- */

/* The step length exactly as musicsynth.c's StepSamples works it out. */
static double jam_StepSamples(const Song *s) {
    int bpm = (s && s->bpm > 0) ? s->bpm : 120;
    return (double)JAM_RATE * 60.0 / ((double)bpm * 4.0);
}

/* The swing delay as Synth_Render computes it: a FLOAT product, widened only
 * when it is added to the double step length. Doing the same here keeps the
 * model on the very sample the synth fires on. */
static double jam_SwingDelay(const Song *s, double stepSamples) {
    float swing = s ? s->swing : 0.0f;
    float delay = swing * (float)stepSamples;
    return (double)delay;
}

/* Where, in samples after the Play, the k-th step after startStep is due.
 * The synth parks one step before the start, measured by that step's own
 * length, so its first rendered sample triggers startStep: the timeline is
 * the steady one, shifted to start there. An even step runs long (it delays
 * the odd one after it) and an odd step short, so every pair is two plain
 * steps and an odd count is one swing delay off: late after a long step, early
 * after a short one. 64 steps a pattern keeps the parity across the wrap. */
static double jam_Due(const Song *s, int startStep, long k) {
    double sps = jam_StepSamples(s);
    double due = (double)k * sps;
    if (k & 1) {
        int p = ((startStep % SONG_STEPS) + SONG_STEPS) % SONG_STEPS;
        double sd = jam_SwingDelay(s, sps);
        due += (p % 2 == 0) ? sd : -sd;
    }
    return due;
}

/* The synth checks once per sample, before rendering it, whether the step is
 * over, so a step fires on the first whole sample at or past its due time. */
static double jam_Time(const Song *s, int startStep, long k) {
    return ceil(jam_Due(s, startStep, k)) / (double)JAM_RATE;
}

double Jam_TimeOfStep(const Song *s, int startStep, long k) {
    if (k <= 0) return (double)k * jam_StepSamples(s) / (double)JAM_RATE;
    return jam_Time(s, startStep, k);
}

double Jam_StepAt(const Song *s, int startStep, double seconds) {
    double stepSec = jam_StepSamples(s) / (double)JAM_RATE;
    if (!(seconds > 0.0)) return seconds / stepSec; /* before the start (or NaN, which stays NaN) */
    long k = (long)floor(seconds / stepSec);
    if (k < 0) k = 0;
    /* The swing shifts a boundary by under one step, so the plain guess is
     * at most one off either way. The comparisons use jam_Time itself, so a
     * time that came from Jam_TimeOfStep finds its own step exactly. */
    while (k > 0 && jam_Time(s, startStep, k) > seconds) k--;
    while (jam_Time(s, startStep, k + 1) <= seconds) k++;
    double a = k > 0 ? jam_Time(s, startStep, k) : 0.0;
    double b = jam_Time(s, startStep, k + 1);
    return (double)k + (b > a ? (seconds - a) / (b - a) : 0.0);
}

int Jam_FramesToTrigger(const Synth *s) {
    if (s->finished) return INT_MAX / 2;
    float swing = s->song ? s->song->swing : 0.0f;
    const float swingDelay = swing * (float)s->samplesPerStep;
    double stepLen = s->samplesPerStep + (((s->step + 1) % 2) == 1 ? swingDelay : -swingDelay);
    double need = stepLen - s->samplesIntoStep;
    if (need <= 0.0) return 0;
    if (need > (double)(INT_MAX / 4)) return INT_MAX / 2; /* the parked preview synth */
    /* Synth_Render fires on the first frame n whose count S + n has reached
     * the step length; settle the rounding of the subtraction either way. */
    int m = (int)ceil(need);
    while (m > 0 && s->samplesIntoStep + (double)(m - 1) >= stepLen) m--;
    while (s->samplesIntoStep + (double)m < stepLen) m++;
    return m;
}

/* --- the clock ------------------------------------------------------------ */

void JamClock_Reset(JamClock *c) {
    memset(c, 0, sizeof(*c));
}

static void jam_Anchor(JamClock *c, double songSeconds, double now) {
    c->anchorSec = songSeconds;
    c->anchorAt = now;
    c->anchored = true;
}

void JamClock_Feed(JamClock *c, unsigned gen, double songSeconds, double at, double now) {
    /* Where that reading says the song is now: it has played on in real time
     * since the audio thread asked for the burst. */
    double predicted = songSeconds + (now - at);
    if (!c->anchored || gen != c->gen) {
        c->gen = gen;
        c->jumps = 0;
        jam_Anchor(c, predicted, now);
        return;
    }
    double err = predicted - (c->anchorSec + (now - c->anchorAt));
    if (fabs(err) > JAM_CLOCK_JUMP) {
        /* Not jitter: the audio stalled (an underrun) or skipped. Believe the
         * audio thread, since that is what comes out of the speaker. */
        jam_Anchor(c, predicted, now);
        c->jumps++;
        return;
    }
    /* Bursts arrive a few milliseconds early or late; following each one
     * would make the playhead stutter, so drift toward them instead. This
     * also absorbs the sound card's clock running a hair apart from ours. */
    c->anchorSec += err * JAM_CLOCK_SLEW;
}

double JamClock_Seconds(const JamClock *c, double now, double latency) {
    if (!c->anchored) return -latency;
    return c->anchorSec + (now - c->anchorAt) - latency;
}

/* --- the Timing test ------------------------------------------------------ */

static int jam_CompareDouble(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int Jam_LatencyFromTaps(const double *taps, int count, double beatSeconds, int firstBeat, int beats) {
    if (!taps || count <= 0 || !(beatSeconds > 0.0)) return -1;
    if (beats > JAM_TAP_BEATS_MAX) beats = JAM_TAP_BEATS_MAX;
    if (firstBeat < 0) firstBeat = 0;
    int hits[JAM_TAP_BEATS_MAX] = {0};
    double offset[JAM_TAP_BEATS_MAX] = {0};
    for (int i = 0; i < count; i++) {
        double beat = floor(taps[i] / beatSeconds + 0.5);
        if (!(beat >= (double)firstBeat && beat < (double)beats)) continue;
        int k = (int)beat;
        hits[k]++;
        offset[k] = taps[i] - (double)k * beatSeconds;
    }
    double kept[JAM_TAP_BEATS_MAX];
    int n = 0;
    for (int k = firstBeat; k < beats; k++)
        if (hits[k] == 1) kept[n++] = offset[k];
    if (n < JAM_TAPS_NEEDED) return -1;
    /* The median, not the mean: one tap that came a beat late (or a beat
     * guessed early) must not move the answer. */
    qsort(kept, (size_t)n, sizeof(kept[0]), jam_CompareDouble);
    double median = (n % 2) ? kept[n / 2] : 0.5 * (kept[n / 2 - 1] + kept[n / 2]);
    int ms = (int)floor(median * 1000.0 + 0.5);
    return ms < 0 ? 0 : ms; /* tapping ahead of the click is anticipation, not a speaker that plays early */
}

/* --- the session ---------------------------------------------------------- */

static int jam_Grid(int grid) {
    return (grid == 1 || grid == 4) ? grid : 2;
}

void Jam_Begin(Jam *j, const Editor *ed, int grid) {
    j->active = true;
    j->pattern = ed->pattern;
    j->track = ed->track;
    j->grid = jam_Grid(grid);
    j->startStep = JAM_START_STEP;
    j->pass = 1;
    j->kept = 0;
    j->pos = -(double)(SONG_STEPS - j->startStep);
    Take_Init(&j->take);
}

/* What the pass being played writes, from step 0 rather than from the pass's
 * own start. Every earlier pass's events were forgotten when it was written,
 * so one still here and caught before this pass is a straggler: a pickup that
 * F12 re-caught from 1/4 onto a finer grid, back into the last pass, or a key
 * played just after the clock stepped back over the loop's end (an underrun
 * re-anchoring it). Its cell is in this same looping pattern, so it is written
 * there rather than forgotten unwritten. The count-in (below 0) stays out.
 * `to` is the end of this pass, or of the one after on the last write. */
static TakeCommit jam_PassCommit(const Jam *j, bool last) {
    TakeCommit c = {
        .grid = j->grid, .mode = TAKE_MERGE,
        .from = 0.0,
        .to = (double)SONG_STEPS * (j->pass + (last ? 1 : 0)),
        .onlyTrack = -1,
    };
    return c;
}

/* Writes the take's notes for the pass being played (through the end of the
 * one after when this is the last write, so nothing pressed is lost), as one
 * commit, and forgets them. Returns the notes written. */
static int jam_Commit(Jam *j, Editor *ed, bool last) {
    TakeCommit c = jam_PassCommit(j, last);
    int notes = Take_NotesIn(&j->take, &c);
    if (notes > 0 && j->pattern >= 0 && j->pattern < ed->song.patternCount) {
        SongPattern before, after;
        memcpy(&before, &ed->song.patterns[j->pattern], sizeof(before));
        memcpy(&after, &before, sizeof(after));
        Take_Apply(&j->take, &c, &after);
        /* The pattern is written in place (the player is reading it) and the
         * commit is one undo entry for the whole pass. */
        Editor_ReplacePattern(ed, j->pattern, &after);
        char buf[EDITOR_STATUS_LEN];
        if (Editor_CommitPattern(ed, j->pattern, &before)) {
            j->kept++;
            snprintf(buf, sizeof(buf), "Pass %d: %d note%s", j->pass, notes, notes == 1 ? "" : "s");
        } else {
            snprintf(buf, sizeof(buf), "Pass %d: the same notes again", j->pass);
        }
        Editor_SetStatus(ed, buf);
    }
    Take_Forget(&j->take, j->grid, (int)c.to);
    return notes;
}

/* The latest you can play into a pass: half a catch step before the loop
 * comes round. A press from there on is caught onto the next pass's step 0. */
static double jam_PassClosesAt(const Jam *j) {
    return (double)SONG_STEPS * j->pass - (double)j->grid / 2.0;
}

void Jam_Tick(Jam *j, Editor *ed, double stepsSincePlay) {
    if (!j->active) return;
    j->pos = stepsSincePlay - (double)(SONG_STEPS - j->startStep);
    /* A frame normally crosses one boundary at most; the cap only matters if
     * the clock leaps, and the rest are then written on later frames. */
    for (int guard = 0; guard < 4 && j->pos >= jam_PassClosesAt(j); guard++) {
        jam_Commit(j, ed, false);
        j->pass++;
    }
}

int Jam_NoteOn(Jam *j, int note) {
    if (!j->active) return -1;
    return Take_NoteOn(&j->take, j->track, note, j->pos);
}

void Jam_NoteOffId(Jam *j, int id) {
    if (!j->active) return;
    Take_NoteOffId(&j->take, id, j->pos);
}

void Jam_NoteOff(Jam *j, int track, int note) {
    if (!j->active) return;
    Take_NoteOff(&j->take, track, note, j->pos);
}

void Jam_SetTrack(Jam *j, int track) {
    if (!j->active || track == j->track) return;
    /* A held note belongs to the track it was played on; its key will come
     * up later, but the note ends where the player moved on. */
    Take_CloseAll(&j->take, j->pos);
    j->track = track;
}

int Jam_NextGrid(int grid) {
    switch (grid) {
        case 1: return 2;
        case 2: return 4;
        default: return 1;
    }
}

const char *Jam_GridName(int grid) {
    switch (grid) {
        case 1: return "1/16";
        case 4: return "1/4";
        default: return "1/8";
    }
}

int Jam_DropPass(Jam *j) {
    if (!j->active) return 0;
    TakeCommit all = {.grid = j->grid, .mode = TAKE_MERGE, .from = -1e18, .to = 1e18, .onlyTrack = -1};
    int notes = Take_NotesIn(&j->take, &all);
    /* Every event goes but the ids count on (not Take_Init): a key still down
     * from the dropped pass must not end a new press when it comes up. */
    j->take.count = 0;
    return notes;
}

int Jam_End(Jam *j, Editor *ed) {
    if (!j->active) return 0;
    Take_CloseAll(&j->take, j->pos);
    int notes = jam_Commit(j, ed, true);
    Take_Init(&j->take);
    j->active = false;
    return notes;
}

int Jam_CountIn(const Jam *j) {
    if (!j->active || j->pos >= 0.0) return 0;
    double beats = ceil(-j->pos / 4.0);
    if (beats > 4.0) return 4;
    return beats < 1.0 ? 1 : (int)beats;
}

bool Jam_LivePass(const Jam *j, SongPattern *out) {
    /* A copy so held notes can be ended at "now" for the picture without
     * ending them in the take. Static: a take is 32 KB and this runs every
     * frame on the game thread. */
    static Take copy;
    Song_ClearPattern(out);
    if (!j->active || j->take.count == 0) return false;
    copy.count = j->take.count;
    memcpy(copy.ev, j->take.ev, (size_t)j->take.count * sizeof(TakeEvent));
    Take_CloseAll(&copy, j->pos);
    /* The same range the pass will be written with, stragglers and all. */
    TakeCommit c = jam_PassCommit(j, false);
    return Take_Apply(&copy, &c, out) > 0;
}
