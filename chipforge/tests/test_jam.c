/* Tests for playing along (jam.h, appini.h): the step times agree with the
 * REAL synth to the sample (swing and the parked first step included), the
 * clock follows bursty audio smoothly and re-anchors after an underrun, the
 * Timing test reads taps robustly, a virtual player with a known latency is
 * caught exactly (and only when that latency is passed in), and a session
 * writes each pass as one undo entry. Compiled by inclusion from
 * test_main.c. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "editor.h"
#include "take.h"
#include "jam.h"
#include "appini.h"

/* Static: an Editor carries 25 songs, a Jam a 32 KB take. */
static Editor sJamEd;
static Jam sJam;
static Song sJamSong;
static Synth sJamSynth;

static void jam_SongAt(Song *s, int bpm, float swing) {
    Song_Init(s, "Jam test song");
    s->bpm = bpm;
    s->swing = swing;
}

/* ------------------------------------------------- step times vs the synth */

/* Starts the synth exactly as player.c's ApplyPendingPlay does. */
static void jam_StartSynth(Synth *s, const Song *song, int startStep) {
    Synth_Init(s, song, JAM_RATE);
    Synth_SetIntensity(s, 1.0f);
    Synth_SetLoopPattern(s, 0);
    Synth_SetPosition(s, 0, startStep);
}

/* Renders up to `target` frames in total (from *rendered), in one call when
 * the step must not change on the way: returns false if it did. */
static bool jam_RenderQuiet(Synth *s, long *rendered, long target) {
    static int16_t buf[1 << 15];
    int before = s->step;
    while (*rendered < target) {
        long n = target - *rendered;
        if (n > (long)(sizeof(buf) / sizeof(buf[0]))) n = (long)(sizeof(buf) / sizeof(buf[0]));
        Synth_Render(s, buf, (int)n);
        *rendered += n;
        if (s->step != before) return false;
    }
    return true;
}

static void jam_test_time_vs_synth(void) {
    static const int kBpm[] = {60, 120, 173, 300};
    static const float kSwing[] = {0.0f, 0.10f, 0.30f};
    static const int kStart[] = {0, 5, 48};
    int worst = 0, runs = 0, okRuns = 0;
    for (int b = 0; b < 4; b++) for (int w = 0; w < 3; w++) for (int st = 0; st < 3; st++) {
        /* Rendering is the cost here (a second of audio is 44100 calls' worth
         * of work at -O0), so each run goes as far as it needs to: from step
         * 48 across the pattern's end (it wraps at k = 16), from step 5
         * across it once at the fastest tempo, otherwise far enough to see
         * the long and short steps alternate. */
        long kSteps = kStart[st] == 48 ? 18 : (kStart[st] == 5 && kBpm[b] == 300 ? 66 : 9);
        jam_SongAt(&sJamSong, kBpm[b], kSwing[w]);
        jam_StartSynth(&sJamSynth, &sJamSong, kStart[st]);
        long rendered = 0;
        bool ok = true;
        runs++;
        for (long k = 0; k <= kSteps && ok; k++) {
            long want = lround(Jam_TimeOfStep(&sJamSong, kStart[st], k) * JAM_RATE);
            /* Quietly up to just before the predicted sample (a step change
             * on the way is a miss), then sample by sample, noting where the
             * synth's step moves on. */
            if (want - 3 > rendered && !jam_RenderQuiet(&sJamSynth, &rendered, want - 3)) { ok = false; break; }
            int before = sJamSynth.step;
            long at = -1;
            int16_t one;
            while (rendered < want + 4) {
                Synth_Render(&sJamSynth, &one, 1);
                rendered++;
                if (sJamSynth.step != before) { at = rendered - 1; break; }
            }
            int expectStep = (int)((kStart[st] + k) % SONG_STEPS);
            if (at < 0 || sJamSynth.step != expectStep) { ok = false; break; }
            int diff = (int)labs(at - want);
            if (diff > worst) worst = diff;
            if (diff > 1) ok = false;
        }
        if (ok) okRuns++;
        else printf("  step times off: bpm %d swing %.2f start %d\n", kBpm[b], (double)kSwing[w], kStart[st]);
    }
    CHECK(okRuns == runs, "Jam_TimeOfStep matches the real synth within 1 sample (4 tempos x 3 swings x 3 starts, across the pattern's end)");
    CHECK(worst <= 1, "worst disagreement with the synth is at most 1 sample");
}

static void jam_test_step_at(void) {
    static const int kBpm[] = {60, 120, 173, 300};
    static const float kSwing[] = {0.0f, 0.10f, 0.30f};
    static const int kStart[] = {0, 5, 48};
    bool exact = true, mono = true, mid = true;
    for (int b = 0; b < 4; b++) for (int w = 0; w < 3; w++) for (int st = 0; st < 3; st++) {
        jam_SongAt(&sJamSong, kBpm[b], kSwing[w]);
        double last = -1.0;
        for (long k = 0; k < 300; k++) {
            double t = Jam_TimeOfStep(&sJamSong, kStart[st], k);
            double t1 = Jam_TimeOfStep(&sJamSong, kStart[st], k + 1);
            if (Jam_StepAt(&sJamSong, kStart[st], t) != (double)k) exact = false;
            double half = Jam_StepAt(&sJamSong, kStart[st], 0.5 * (t + t1));
            if (fabs(half - ((double)k + 0.5)) > 1e-9) mid = false;
            if (!(half > last)) mono = false;
            last = half;
        }
    }
    CHECK(exact, "Jam_StepAt(Jam_TimeOfStep(k)) is exactly k");
    CHECK(mid, "half way between two triggers is k + 0.5, swung steps included");
    CHECK(mono, "Jam_StepAt only ever rises");
    jam_SongAt(&sJamSong, 120, 0.0f);
    CHECK(fabs(Jam_StepAt(&sJamSong, 48, -0.25) - (-2.0)) < 1e-9, "before the start it counts back in plain steps");
    /* Swing makes the odd steps late: at 120 bpm with 0.30 swing, from step
     * 0, step 1 fires 0.3 of a step late. */
    jam_SongAt(&sJamSong, 120, 0.30f);
    double t1 = Jam_TimeOfStep(&sJamSong, 0, 1), t2 = Jam_TimeOfStep(&sJamSong, 0, 2);
    CHECK(fabs(t1 - 0.125 * 1.3) < 1.5 / JAM_RATE && fabs(t2 - 0.25) < 1.5 / JAM_RATE, "swing delays the off sixteenth, the beat stays put");
    /* From an odd start the first step is the short one. */
    double o1 = Jam_TimeOfStep(&sJamSong, 5, 1);
    CHECK(fabs(o1 - 0.125 * 0.7) < 1.5 / JAM_RATE, "from step 5 the first (odd) step is short");
}

static void jam_test_frames_to_trigger(void) {
    static const float kSwing[] = {0.0f, 0.17f, 0.30f};
    static const int kBpm[] = {73, 120, 300};
    bool ok = true;
    int16_t buf[4096];
    for (int b = 0; b < 3; b++) for (int w = 0; w < 3; w++) {
        jam_SongAt(&sJamSong, kBpm[b], kSwing[w]);
        jam_StartSynth(&sJamSynth, &sJamSong, 48);
        CHECK(Jam_FramesToTrigger(&sJamSynth) == 0, "a fresh Play triggers on its first frame");
        Synth_Render(&sJamSynth, buf, 1);
        /* Step by step across the pattern's end (from 48 it wraps after 16),
         * stopping at odd places in between. */
        for (int k = 0; k < 24 && ok; k++) {
            int skip = 37 * (k % 5);
            int m = Jam_FramesToTrigger(&sJamSynth);
            if (skip < m) { Synth_Render(&sJamSynth, buf, skip); m = Jam_FramesToTrigger(&sJamSynth); }
            int before = sJamSynth.step;
            int left = m;
            while (left > 0) { int n = left > 4096 ? 4096 : left; Synth_Render(&sJamSynth, buf, n); left -= n; }
            if (sJamSynth.step != before) { ok = false; break; }
            Synth_Render(&sJamSynth, buf, 1);
            if (sJamSynth.step == before) ok = false;
        }
    }
    CHECK(ok, "Jam_FramesToTrigger names the exact frame the synth's next step fires on");
    Synth_Init(&sJamSynth, &sJamSong, JAM_RATE);
    sJamSynth.samplesIntoStep = -1e12; /* the player's parked preview synth */
    CHECK(Jam_FramesToTrigger(&sJamSynth) > 1000000, "a parked synth is never due");
}

/* ----------------------------------------------------------------- clock */

/* A tiny deterministic jitter in -1..1. */
static double jam_Jitter(unsigned *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return ((double)(*seed >> 8) / (double)(1u << 24)) * 2.0 - 1.0;
}

static void jam_test_clock(void) {
    JamClock c;
    JamClock_Reset(&c);
    CHECK(JamClock_Seconds(&c, 5.0, 0.04) == -0.04, "before any reading the clock is at the start, not yet heard");

    /* Audio bursts every 10 ms (441 frames) whose callbacks run up to 3 ms
     * early or late; the song starts at wall time 100.0. Frames read every
     * 1/60 s. */
    const double t0 = 100.0, period = 441.0 / JAM_RATE, frame = 1.0 / 60.0;
    unsigned seed = 7;
    double pubSec = 0.0, pubAt = t0;
    long cb = 0;
    double nextCb = t0;
    double prevOut = 0.0, worstErr = 0.0, worstStep = 0.0;
    bool havePrev = false;
    bool stalled = false;
    int jumpsBefore = 0;
    double stallStart = t0 + 2.0, stallLen = 0.100, lost = 0.0;
    double errAfter = 1.0;
    for (double now = t0 + 0.004; now < t0 + 4.0; now += frame) {
        /* Callbacks due by now publish. During the stall none run. */
        while (nextCb <= now) {
            if (nextCb >= stallStart && nextCb < stallStart + stallLen) { nextCb += period; stalled = true; lost += period; continue; }
            pubSec = (double)(cb * 441) / JAM_RATE;
            pubAt = nextCb + 0.003 * jam_Jitter(&seed);
            cb++;
            nextCb += period;
        }
        JamClock_Feed(&c, 1, pubSec, pubAt, now);
        double out = JamClock_Seconds(&c, now, 0.0);
        double truth = now - t0 - lost; /* the stall delays the song by its length */
        if (!stalled) {
            if (now > t0 + 0.3 && fabs(out - truth) > worstErr) worstErr = fabs(out - truth);
            if (havePrev && fabs((out - prevOut) - frame) > worstStep) worstStep = fabs((out - prevOut) - frame);
            jumpsBefore = c.jumps;
        } else if (now > stallStart + stallLen + 0.03) {
            if (errAfter > 0.5) errAfter = fabs(out - truth);
        }
        prevOut = out;
        havePrev = true;
    }
    CHECK(worstErr < 0.004, "with jittery bursts the clock stays within 4 ms of the song");
    CHECK(worstStep < 0.002, "and moves on by one frame's time, give or take 2 ms: continuous");
    CHECK(jumpsBefore == 0, "no re-anchoring while the audio runs smoothly");
    CHECK(c.jumps == 1, "a 100 ms underrun re-anchors once");
    CHECK(errAfter < 0.005, "and the clock is back on the song right after it");

    /* A new Play (generation) re-anchors at once, wherever the old one was. */
    JamClock_Feed(&c, 2, 0.0, 200.0, 200.001);
    CHECK(c.gen == 2 && fabs(JamClock_Seconds(&c, 200.001, 0.0) - 0.001) < 1e-9, "a new generation starts the clock again");
    CHECK(fabs(JamClock_Seconds(&c, 200.5, 0.07) - (0.5 - 0.07)) < 1e-9, "latency is taken off what the clock says");
}

/* ---------------------------------------------------------------- the taps */

static void jam_test_taps(void) {
    /* 100 bpm, 16 clicks, the first four to find the beat. */
    const double beat = 0.6;
    static const double kJitter[16] = {0.012, -0.015, 0.004, -0.009, 0.015, -0.011, 0.007, -0.002,
                                       0.010, -0.014, 0.001, -0.006, 0.013, -0.008, 0.003, -0.012};
    double taps[40];
    int n = 0;
    for (int k = 0; k < 16; k++) taps[n++] = k * beat + 0.055 + kJitter[k];
    int ms = Jam_LatencyFromTaps(taps, n, beat, 4, 16);
    CHECK(ms >= 52 && ms <= 58, "taps 55 ms late with 15 ms of jitter read as 52..58 ms");

    /* Miss beat 7, double-tap beat 9: both left out, the answer holds. */
    n = 0;
    for (int k = 0; k < 16; k++) {
        if (k == 7) continue;
        taps[n++] = k * beat + 0.055 + kJitter[k];
        if (k == 9) taps[n++] = k * beat + 0.180;
    }
    ms = Jam_LatencyFromTaps(taps, n, beat, 4, 16);
    CHECK(ms >= 52 && ms <= 58, "a missed click and a double tap are ignored");

    /* A wild tap a third of a beat late on one click does not move the median much. */
    taps[5] += 0.15;
    ms = Jam_LatencyFromTaps(taps, n, beat, 4, 16);
    CHECK(ms >= 52 && ms <= 60, "one wild tap barely moves it");

    n = 0;
    for (int k = 4; k < 9; k++) taps[n++] = k * beat + 0.055;
    CHECK(Jam_LatencyFromTaps(taps, n, beat, 4, 16) == -1, "five taps are not enough");
    n = 0;
    for (int k = 0; k < 4; k++) taps[n++] = k * beat + 0.055;
    CHECK(Jam_LatencyFromTaps(taps, n, beat, 4, 16) == -1, "taps on the warm-up clicks alone do not count");
    n = 0;
    for (int k = 0; k < 16; k++) taps[n++] = k * beat - 0.030;
    CHECK(Jam_LatencyFromTaps(taps, n, beat, 4, 16) == 0, "tapping ahead of the click reads as 0, not less");
    n = 0;
    for (int k = 0; k < 16; k++) taps[n++] = k * beat + 0.240;
    ms = Jam_LatencyFromTaps(taps, n, beat, 4, 16);
    CHECK(ms == 240, "a Bluetooth-sized 240 ms is still read");
    CHECK(Jam_LatencyFromTaps(NULL, 3, beat, 4, 16) == -1 && Jam_LatencyFromTaps(taps, 16, 0.0, 4, 16) == -1, "bad arguments give -1");
    taps[0] = 1e300;
    taps[1] = -1e300;
    CHECK(Jam_LatencyFromTaps(taps, 2, beat, 0, 16) == -1, "taps far outside the test are ignored");
}

/* ------------------------------------------------ the virtual player, end to end */

typedef struct { int step, len, note; } JamMelodyNote;

/* Plays a known 16-note melody over pass 1 the way a person with a
 * speaker 70 ms behind the playhead would: on what they hear, up to 20 ms
 * off, seen by a 60 Hz frame loop, with the audio thread publishing bursts
 * every 10 ms. Returns whether the lead came out exactly as written. */
static bool jam_VirtualPlayer(double latencyPassed, int grid, float swing) {
    static const JamMelodyNote kTune[16] = {
        {0, 2, 69}, {2, 2, 72}, {4, 1, 76}, {5, 1, 74}, {6, 2, 72}, {8, 4, 69}, {12, 2, 67}, {14, 2, 69},
        {16, 3, 72}, {19, 1, 71}, {20, 4, 69}, {28, 2, 64}, {32, 6, 67}, {40, 2, 69}, {44, 8, 72}, {56, 6, 69},
    };
    static const double kPressJitter[16] = {0.018, -0.020, 0.006, -0.012, 0.015, -0.004, 0.011, -0.017,
                                            0.002, -0.009, 0.020, -0.015, 0.008, -0.001, 0.013, -0.019};
    const double ahead = 0.070; /* the playhead runs this far ahead of the speaker */
    jam_SongAt(&sJamSong, 120, swing);
    Editor_SetSong(&sJamEd, &sJamSong, "");
    sJamEd.track = TRACK_LEAD;
    Jam_Begin(&sJam, &sJamEd, grid);
    JamClock c;
    JamClock_Reset(&c);

    /* When each key goes down and comes up, in wall seconds after the Play. */
    double down[16], up[16];
    for (int i = 0; i < 16; i++) {
        long k0 = JAM_COUNT_IN_STEPS + kTune[i].step, k1 = k0 + kTune[i].len;
        down[i] = Jam_TimeOfStep(&sJamSong, JAM_START_STEP, k0) + ahead + kPressJitter[i];
        up[i] = Jam_TimeOfStep(&sJamSong, JAM_START_STEP, k1) + ahead - 0.010 + 0.5 * kPressJitter[15 - i];
    }
    const double period = 441.0 / JAM_RATE;
    double pubSec = 0.0, pubAt = 0.0, nextCb = 0.0;
    long cb = 0;
    bool published = false;
    double prev = -1.0;
    for (double now = 0.0021; now < Jam_TimeOfStep(&sJamSong, JAM_START_STEP, JAM_COUNT_IN_STEPS + 72) + 0.2; now += 1.0 / 60.0) {
        while (nextCb <= now) {
            pubSec = (double)(cb * 441) / JAM_RATE;
            pubAt = nextCb;
            published = true;
            cb++;
            nextCb += period;
        }
        if (published) JamClock_Feed(&c, 1, pubSec, pubAt, now);
        double k = Jam_StepAt(&sJamSong, JAM_START_STEP, JamClock_Seconds(&c, now, latencyPassed));
        Jam_Tick(&sJam, &sJamEd, k);
        for (int i = 0; i < 16; i++) {
            if (down[i] > prev && down[i] <= now) Jam_NoteOn(&sJam, kTune[i].note);
            if (up[i] > prev && up[i] <= now) Jam_NoteOff(&sJam, TRACK_LEAD, kTune[i].note);
        }
        prev = now;
    }
    int8_t want[SONG_STEPS];
    memset(want, NOTE_NONE, sizeof(want));
    for (int i = 0; i < 16; i++) {
        want[kTune[i].step] = (int8_t)kTune[i].note;
        for (int h = 1; h < kTune[i].len; h++) want[kTune[i].step + h] = NOTE_HOLD;
    }
    bool same = memcmp(sJamEd.song.patterns[0].tone[TRACK_LEAD], want, sizeof(want)) == 0;
    Jam_End(&sJam, &sJamEd);
    return same;
}

static void jam_test_virtual_player(void) {
    CHECK(jam_VirtualPlayer(0.070, 1, 0.0f), "a 16-note tune played 70 ms behind, with that latency passed in, is caught exactly (1/16)");
    CHECK(jam_VirtualPlayer(0.070, 1, 0.10f), "and exactly with swing too");
    CHECK(jam_VirtualPlayer(0.070, 2, 0.0f) == false, "on 1/8 the sixteenths of the tune are (rightly) caught to eighths");
    CHECK(!jam_VirtualPlayer(0.0, 1, 0.0f), "with latency 0 passed instead the same playing is caught late");
}

/* ------------------------------------------------------------- sessions */

static void jam_StartSession(int track, int grid) {
    jam_SongAt(&sJamSong, 120, 0.0f);
    Editor_SetSong(&sJamEd, &sJamSong, "");
    sJamEd.track = track;
    Jam_Begin(&sJam, &sJamEd, grid);
}

/* Moves the session to `pos` steps since the first downbeat. */
static void jam_At(double pos) {
    Jam_Tick(&sJam, &sJamEd, pos + JAM_COUNT_IN_STEPS);
}

static void jam_Play(double from, double to, int note) {
    jam_At(from);
    Jam_NoteOn(&sJam, note);
    jam_At(to);
    Jam_NoteOff(&sJam, sJam.track, note);
}

static void jam_test_session(void) {
    /* Count-in. */
    jam_StartSession(TRACK_LEAD, 2);
    CHECK(sJam.active && sJam.pass == 1 && sJam.pattern == 0, "a session starts on pass 1 of the cursor's pattern");
    Jam_Tick(&sJam, &sJamEd, 0.0);
    CHECK(Jam_CountIn(&sJam) == 4, "count-in shows 4 on its first beat");
    Jam_Tick(&sJam, &sJamEd, 4.0);
    CHECK(Jam_CountIn(&sJam) == 3, "then 3");
    Jam_Tick(&sJam, &sJamEd, 11.9);
    CHECK(Jam_CountIn(&sJam) == 2, "then 2");
    Jam_Tick(&sJam, &sJamEd, 12.0);
    CHECK(Jam_CountIn(&sJam) == 1, "then 1");
    Jam_Tick(&sJam, &sJamEd, 16.0);
    CHECK(Jam_CountIn(&sJam) == 0, "and none once the pass starts");
    Jam_Tick(&sJam, &sJamEd, -100.0);
    CHECK(Jam_CountIn(&sJam) == 4, "before the audio has started it still says 4");

    /* A scripted three-pass session: three passes, three undo entries. */
    jam_StartSession(TRACK_LEAD, 2);
    int undo0 = sJamEd.undoCount;
    jam_At(-0.4);
    Jam_NoteOn(&sJam, 60);              /* just before the downbeat: step 0 */
    jam_At(0.5);
    Jam_NoteOff(&sJam, TRACK_LEAD, 60);
    jam_Play(8.1, 9.0, 62);
    jam_Play(15.8, 16.5, 64);
    jam_At(62.0);
    CHECK(sJamEd.undoCount == undo0, "nothing is written while the pass can still be played into");
    jam_At(63.0);                       /* half an eighth before the loop comes round */
    CHECK(sJamEd.undoCount == undo0 + 1 && sJam.pass == 2, "pass 1 is written half a catch step before the end");
    CHECK(strcmp(sJamEd.status, "Pass 1: 3 notes") == 0, "status names the pass and its notes");
    const SongPattern *p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_LEAD][0] == 60 && p->tone[TRACK_LEAD][8] == 62 && p->tone[TRACK_LEAD][16] == 64, "pass 1's notes are caught to eighths");
    CHECK(sJamEd.dirty, "a written pass marks the song changed");
    jam_Play(64.0 + 32.2, 64.0 + 33.0, 67);
    jam_At(128.0 - 0.9);
    CHECK(sJamEd.undoCount == undo0 + 2, "pass 2 is its own undo entry");
    jam_Play(128.0 + 40.0, 128.0 + 41.0, 69);
    jam_At(192.0);
    CHECK(sJamEd.undoCount == undo0 + 3 && sJam.kept == 3, "a three-pass session ends with exactly 3 undo entries");
    CHECK(strcmp(sJamEd.status, "Pass 3: 1 note") == 0, "one note is not 'notes'");
    CHECK(p->tone[TRACK_LEAD][0] == 60 && p->tone[TRACK_LEAD][32] == 67 && p->tone[TRACK_LEAD][40] == 69, "passes overdub: all three passes are in");

    /* An empty pass writes nothing and pushes nothing. */
    jam_At(256.0);
    CHECK(sJamEd.undoCount == undo0 + 3 && sJam.pass == 5, "an empty pass is no undo entry");

    /* Stopping mid-pass keeps the notes that started in it. */
    jam_Play(256.0 + 4.0, 256.0 + 5.0, 71);
    jam_At(256.0 + 10.0);
    Jam_NoteOn(&sJam, 72);              /* still held when F10 is pressed */
    jam_At(256.0 + 14.0);
    int last = Jam_End(&sJam, &sJamEd);
    CHECK(last == 2 && !sJam.active, "stopping mid-pass writes its two notes and ends the session");
    CHECK(sJamEd.undoCount == undo0 + 4, "as one more undo entry");
    CHECK(p->tone[TRACK_LEAD][4] == 71 && p->tone[TRACK_LEAD][10] == 72 && p->tone[TRACK_LEAD][13] == NOTE_HOLD &&
          p->tone[TRACK_LEAD][14] != NOTE_HOLD, "a held note ends where recording stopped");
    CHECK(Jam_NoteOn(&sJam, 60) == -1, "after the end, presses are not recorded");

    /* Each pass undoes on its own. */
    Editor_Apply(&sJamEd, ED_UNDO, 0);
    CHECK(p->tone[TRACK_LEAD][10] == NOTE_NONE && p->tone[TRACK_LEAD][40] == 69, "Ctrl+Z takes back only the last pass");

    /* Backspace drops the pass and nothing is written. */
    jam_StartSession(TRACK_LEAD, 2);
    undo0 = sJamEd.undoCount;
    memcpy(&sJamSong, &sJamEd.song, sizeof(Song));
    jam_Play(4.0, 6.0, 60);
    jam_Play(20.0, 22.0, 62);
    jam_At(30.0);
    Jam_NoteOn(&sJam, 64); /* still held */
    int dropped = Jam_DropPass(&sJam);
    jam_At(70.0);
    Jam_NoteOff(&sJam, TRACK_LEAD, 64);
    CHECK(dropped == 3, "Backspace reports the notes it threw away");
    CHECK(sJamEd.undoCount == undo0 && memcmp(&sJamEd.song, &sJamSong, sizeof(Song)) == 0, "a dropped pass writes nothing");
    jam_Play(64.0 + 2.0, 64.0 + 3.0, 65);
    Jam_End(&sJam, &sJamEd);
    CHECK(sJamEd.song.patterns[0].tone[TRACK_LEAD][2] == 65, "the pass after a drop records as usual");

    /* Tab mid-session: later notes go to the new track, all in one entry. */
    jam_StartSession(TRACK_BASS, 2);
    undo0 = sJamEd.undoCount;
    jam_Play(0.0, 4.0, 40);
    jam_At(10.0);
    Jam_NoteOn(&sJam, 45);              /* held while Tab is pressed */
    jam_At(12.0);
    Jam_SetTrack(&sJam, TRACK_LEAD);
    jam_At(14.0);
    Jam_NoteOff(&sJam, TRACK_BASS, 45);
    jam_Play(16.0, 18.0, 76);
    jam_At(64.0);
    p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_BASS][0] == 40 && p->tone[TRACK_BASS][10] == 45 && p->tone[TRACK_BASS][11] == NOTE_HOLD &&
          p->tone[TRACK_BASS][12] == NOTE_NONE, "the bass keeps its notes, the held one ending at the Tab");
    CHECK(p->tone[TRACK_LEAD][16] == 76 && p->tone[TRACK_BASS][16] == NOTE_NONE, "after Tab the notes go to the lead");
    CHECK(sJamEd.undoCount == undo0 + 1, "both tracks in one pass are one undo entry");
    Jam_End(&sJam, &sJamEd);

    /* A note held over the loop's end stops at 63; its later release is harmless. */
    jam_StartSession(TRACK_LEAD, 1);
    jam_At(60.0);
    Jam_NoteOn(&sJam, 67);
    jam_At(66.0);
    Jam_NoteOff(&sJam, TRACK_LEAD, 67);
    p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_LEAD][60] == 67 && p->tone[TRACK_LEAD][63] == NOTE_HOLD && p->tone[TRACK_LEAD][0] == NOTE_NONE,
          "held across the loop's end: holds to 63, step 0 untouched");
    Jam_End(&sJam, &sJamEd);

    /* Drums: pads add hits, a second pass over the same beat keeps them. */
    jam_StartSession(TRACK_DRUMS, 1);
    jam_At(0.0); Jam_NoteOn(&sJam, DRUM_KICK);
    jam_At(4.1); Jam_NoteOn(&sJam, DRUM_SNARE);
    jam_At(64.0); Jam_NoteOn(&sJam, DRUM_KICK);
    jam_At(64.0 + 4.0); Jam_NoteOn(&sJam, DRUM_HAT);
    jam_At(128.0);
    p = &sJamEd.song.patterns[0];
    CHECK(p->drums[0] == DRUM_KICK && p->drums[4] == (DRUM_SNARE | DRUM_HAT), "recorded pads add up over passes and never toggle off");
    int undoNow = sJamEd.undoCount;
    jam_At(128.0); Jam_NoteOn(&sJam, DRUM_KICK);
    jam_At(192.0);
    CHECK(sJamEd.undoCount == undoNow && strcmp(sJamEd.status, "Pass 3: the same notes again") == 0,
          "a pass that changes nothing is not an undo entry");
    Jam_End(&sJam, &sJamEd);

    /* The live pass: what is being played, caught, before it is written. */
    jam_StartSession(TRACK_LEAD, 2);
    SongPattern live;
    CHECK(!Jam_LivePass(&sJam, &live), "nothing to show before a note");
    jam_At(3.2);
    Jam_NoteOn(&sJam, 70);
    jam_At(7.0);
    CHECK(Jam_LivePass(&sJam, &live), "a held note shows");
    CHECK(live.tone[TRACK_LEAD][4] == 70 && live.tone[TRACK_LEAD][7] == NOTE_HOLD && live.tone[TRACK_LEAD][8] == NOTE_NONE,
          "caught onto its eighth, held as long as it has been down (3.8 steps, so 4)");
    CHECK(sJam.take.ev[0].held, "drawing it does not end the held note");
    CHECK(sJamEd.song.patterns[0].tone[TRACK_LEAD][4] == NOTE_NONE, "and nothing is in the song yet");
    Jam_End(&sJam, &sJamEd);

    /* Grid cycling and names. */
    CHECK(Jam_NextGrid(1) == 2 && Jam_NextGrid(2) == 4 && Jam_NextGrid(4) == 1, "F12 cycles 1/16, 1/8, 1/4");
    CHECK(strcmp(Jam_GridName(1), "1/16") == 0 && strcmp(Jam_GridName(2), "1/8") == 0 && strcmp(Jam_GridName(4), "1/4") == 0, "grid names");
    jam_StartSession(TRACK_LEAD, 3);
    CHECK(sJam.grid == 2, "an unknown grid starts on 1/8, the beginner's catch");
    Jam_End(&sJam, &sJamEd);

    /* A session on a pattern the song no longer has writes nothing. */
    jam_StartSession(TRACK_LEAD, 2);
    sJam.pattern = 5;
    jam_Play(0.0, 1.0, 60);
    undo0 = sJamEd.undoCount;
    Jam_End(&sJam, &sJamEd);
    CHECK(sJamEd.undoCount == undo0, "a pattern that is gone is not written");
}

/* A pickup let go before the first downbeat, and Tab or F10 in the count-in,
 * end the note there: the count-in's times are negative, and a negative
 * release once read as a key still down, held to step 63. */
static void jam_test_count_in_pickup(void) {
    for (int grid = 1; grid <= 4; grid *= 2) {
        jam_StartSession(TRACK_LEAD, grid);
        double on = -0.45 * grid, off = on + 0.35 * grid;
        jam_Play(on, off, 72);
        jam_At(64.0);
        const SongPattern *p = &sJamEd.song.patterns[0];
        CHECK(p->tone[TRACK_LEAD][0] == 72 && p->tone[TRACK_LEAD][1] == NOTE_NONE && p->tone[TRACK_LEAD][63] == NOTE_NONE,
              "a pickup tapped just before the first downbeat is written as one step");
        Jam_End(&sJam, &sJamEd);
    }

    /* Tab in the count-in with a pickup still down: it ends at the Tab. */
    jam_StartSession(TRACK_BASS, 2);
    jam_At(-0.9);
    int id = Jam_NoteOn(&sJam, 40);
    jam_At(-0.3);
    Jam_SetTrack(&sJam, TRACK_LEAD);
    jam_At(5.0);
    Jam_NoteOffId(&sJam, id);                           /* its key comes up later */
    jam_At(64.0);
    const SongPattern *p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_BASS][0] == 40 && p->tone[TRACK_BASS][1] == NOTE_NONE && p->tone[TRACK_BASS][63] == NOTE_NONE,
          "Tab in the count-in ends a held pickup there, not at step 63");
    Jam_End(&sJam, &sJamEd);

    /* F10 in the count-in keeps the pickup as the step it was. */
    jam_StartSession(TRACK_LEAD, 2);
    jam_At(-0.9);
    Jam_NoteOn(&sJam, 74);
    jam_At(-0.2);
    CHECK(Jam_End(&sJam, &sJamEd) == 1, "stopping in the count-in keeps a pickup");
    p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_LEAD][0] == 74 && p->tone[TRACK_LEAD][1] == NOTE_NONE, "as one step, not held to step 63");
}

/* A note caught before the pass being played, after that pass's own range was
 * written and forgotten, is written into its cell rather than lost. */
static void jam_test_stragglers(void) {
    /* F12 1/4 -> 1/16 with a pickup waiting for the next pass's step 0. */
    jam_StartSession(TRACK_LEAD, 4);
    jam_At(62.2);                                       /* pass 1 closed at 62 on 1/4 */
    CHECK(sJam.pass == 2, "on 1/4 pass 1 is written half a beat before the end");
    jam_Play(62.5, 62.8, 72);                           /* caught on 64: pass 2's step 0 */
    sJam.grid = Jam_NextGrid(sJam.grid);                /* F12: 1/4 -> 1/16, as main.c's SetCatchGrid does */
    CHECK(sJam.grid == 1, "F12 from 1/4 goes to 1/16");
    SongPattern live;
    CHECK(Jam_LivePass(&sJam, &live) && live.tone[TRACK_LEAD][63] == 72, "the live pass still shows it, on 1/16's step 63");
    jam_At(127.6);                                      /* pass 2 written */
    const SongPattern *p = &sJamEd.song.patterns[0];
    CHECK(sJam.pass == 3 && p->tone[TRACK_LEAD][63] == 72 && strcmp(sJamEd.status, "Pass 2: 1 note") == 0,
          "re-caught into the pass already written, it is written with the next one, not dropped");
    Jam_End(&sJam, &sJamEd);

    /* The clock steps back over the loop's end (an underrun re-anchors it)
     * after pass 1 was written: a key played on what you hear is kept. */
    jam_StartSession(TRACK_LEAD, 1);
    jam_At(63.6);
    CHECK(sJam.pass == 2, "pass 1 written on the clock's guess");
    jam_At(63.08);                                      /* the audio thread says it is still on step 63 */
    jam_Play(63.08, 63.3, 77);
    Jam_End(&sJam, &sJamEd);
    p = &sJamEd.song.patterns[0];
    CHECK(p->tone[TRACK_LEAD][63] == 77, "a note caught on the written pass's last step after the clock stepped back is kept");

    /* The count-in is still left out. */
    jam_StartSession(TRACK_LEAD, 1);
    jam_Play(-6.0, -5.0, 60);
    jam_At(64.0);
    bool none = true;
    for (int st = 0; st < SONG_STEPS; st++) none = none && sJamEd.song.patterns[0].tone[TRACK_LEAD][st] == NOTE_NONE;
    CHECK(none && sJamEd.undoCount == 0, "a note played well inside the count-in is not written");
    Jam_End(&sJam, &sJamEd);
}

/* A key's release ends its own press, whatever pitch another key gives. */
static void jam_test_release_by_press(void) {
    jam_StartSession(TRACK_LEAD, 1);
    jam_At(0.0);
    int q = Jam_NoteOn(&sJam, 72);                      /* Q at octave 4 */
    jam_At(4.0);
    int z = Jam_NoteOn(&sJam, 72);                      /* '=' then Z: 72 again, Q still down */
    jam_At(4.5);
    Jam_NoteOffId(&sJam, q);                            /* Q up */
    jam_At(12.0);
    Jam_NoteOffId(&sJam, z);                            /* Z up */
    jam_At(64.0);
    const SongPattern *p = &sJamEd.song.patterns[0];
    bool zHeld = p->tone[TRACK_LEAD][4] == 72;
    for (int st = 5; st <= 11; st++) zHeld = zHeld && p->tone[TRACK_LEAD][st] == NOTE_HOLD;
    CHECK(q >= 0 && z >= 0 && q != z, "each press has its own id");
    CHECK(p->tone[TRACK_LEAD][0] == 72 && zHeld && p->tone[TRACK_LEAD][12] == NOTE_NONE,
          "the same pitch from two keys: the second note holds until its own key comes up");
    Jam_End(&sJam, &sJamEd);

    /* Backspace with a key down, then the same note again: the old key's
     * release does not end the new press. */
    jam_StartSession(TRACK_LEAD, 1);
    jam_At(2.0);
    int old = Jam_NoteOn(&sJam, 64);
    jam_At(3.0);
    Jam_DropPass(&sJam);
    jam_At(8.0);
    int now = Jam_NoteOn(&sJam, 64);
    jam_At(9.0);
    Jam_NoteOffId(&sJam, old);
    jam_At(14.0);
    Jam_NoteOffId(&sJam, now);
    Jam_End(&sJam, &sJamEd);
    p = &sJamEd.song.patterns[0];
    CHECK(old != now && p->tone[TRACK_LEAD][8] == 64 && p->tone[TRACK_LEAD][13] == NOTE_HOLD && p->tone[TRACK_LEAD][14] == NOTE_NONE,
          "a key held over Backspace does not end a new press of its note");
    CHECK(p->tone[TRACK_LEAD][2] == NOTE_NONE, "and the dropped press is not written");
}

/* Passes add up, and a short note played over an earlier pass's long one
 * sounds for as long as it was held. */
static void jam_test_overdub_length(void) {
    jam_StartSession(TRACK_LEAD, 1);
    jam_Play(0.0, 16.0, 60);
    jam_Play(64.0 + 4.0, 64.0 + 4.25, 64);
    jam_At(128.0);
    const SongPattern *p = &sJamEd.song.patterns[0];
    bool ended = true;
    for (int st = 5; st < 16; st++) ended = ended && p->tone[TRACK_LEAD][st] == NOTE_NONE;
    CHECK(p->tone[TRACK_LEAD][0] == 60 && p->tone[TRACK_LEAD][3] == NOTE_HOLD && p->tone[TRACK_LEAD][4] == 64 && ended,
          "a tap over pass 1's long note is one step; the long note sounds up to it");
    CHECK(strcmp(sJamEd.status, "Pass 2: 1 note") == 0, "and it is pass 2's one note");
    Jam_End(&sJam, &sJamEd);
}

/* ---------------------------------------------------------------- appini */

static void jam_test_appini(void) {
    AppIni a;
    char path[700];
    CHECK(AppIni_Path(path, sizeof(path), false), "the config path resolves");
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char want[700];
    snprintf(want, sizeof(want), "%s/chipforge/config.ini", cfg ? cfg : "");
    CHECK(cfg && strcmp(path, want) == 0, "it is $XDG_CONFIG_HOME/chipforge/config.ini, not the shared prefs.ini");
    remove(path);
    CHECK(!AppIni_Load(&a) && a.timingMs == APPINI_TIMING_DEFAULT, "no file: the 40 ms default");

    a.timingMs = 65;
    CHECK(AppIni_Save(&a), "saves");
    AppIni b;
    CHECK(AppIni_Load(&b) && b.timingMs == 65, "and reads back");

    FILE *f = fopen(path, "w");
    if (f) {
        fputs("# hand edited\n\ngarbage line\n  timing_ms =  999  \nother=3\n", f);
        fclose(f);
    }
    CHECK(AppIni_Load(&b) && b.timingMs == APPINI_TIMING_MAX, "too large is clamped, spaces and junk are fine");
    f = fopen(path, "w");
    if (f) { fputs("timing_ms=-20\n", f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == 0, "negative is clamped to 0");
    f = fopen(path, "w");
    if (f) { fputs("timing_ms=fast\n", f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == APPINI_TIMING_DEFAULT, "a value that is not a number keeps the default");
    f = fopen(path, "w");
    if (f) { fputs("timing_ms=99999999999999999999\n", f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == APPINI_TIMING_MAX, "an absurd number is clamped, not wrapped");
    a.timingMs = 7000;
    AppIni_Save(&a);
    CHECK(AppIni_Load(&b) && b.timingMs == APPINI_TIMING_MAX, "a save clamps too");
    CHECK(AppIni_ClampTiming(-1) == 0 && AppIni_ClampTiming(301) == 300 && AppIni_ClampTiming(45) == 45, "clamp");

    /* Lines too long for the 255-character buffer are skipped whole: the tail
     * of a long comment is not a line of its own. */
    char text[1200];
    int n = snprintf(text, sizeof(text), "timing_ms=120\n# ");
    while (n < 14 + 255) text[n++] = 'x';
    n += snprintf(text + n, sizeof(text) - (size_t)n, "timing_ms=0\n");
    f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == 120, "a long comment ending in timing_ms=0 does not override the setting");
    n = snprintf(text, sizeof(text), "timing_ms=");
    while (n < 260) text[n++] = ' ';
    n += snprintf(text + n, sizeof(text) - (size_t)n, "120\ntiming_ms=85\n");
    f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == 85, "a padded line too long to read is skipped, and the next line is read");
    n = snprintf(text, sizeof(text), "timing_ms=95");
    while (n < 254) text[n++] = ' ';
    n += snprintf(text + n, sizeof(text) - (size_t)n, "\n");
    f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == 95, "a line of 254 characters still fits");
    f = fopen(path, "wb");
    if (f) { fwrite("# x\0y\ntiming_ms=70\n", 1, 20, f); fclose(f); }
    CHECK(AppIni_Load(&b) && b.timingMs == 70, "a line holding a NUL does not hide the next one");
    remove(path);
}

void test_jam_all(void) {
    jam_test_time_vs_synth();
    jam_test_step_at();
    jam_test_frames_to_trigger();
    jam_test_clock();
    jam_test_taps();
    jam_test_virtual_player();
    jam_test_session();
    jam_test_count_in_pickup();
    jam_test_stragglers();
    jam_test_release_by_press();
    jam_test_overdub_length();
    jam_test_appini();
}
