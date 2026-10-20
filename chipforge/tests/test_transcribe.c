/* Headless checks for the importer (transcribe.c). Everything is synthetic
 * and generated here: a square-wave melody with a sine-burst kick on every
 * beat is enough to prove the tempo, the beat grid, the melody and the kicks
 * land where they should. Compiled by inclusion from test_main.c, so no main
 * and every helper is static and prefixed. */
#include "check.h"
#include "../src/transcribe.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSCRIBE_BARS 8
#define TRANSCRIBE_BEAT_SECONDS 0.5  /* 120 bpm */
#define TRANSCRIBE_PI 3.14159265358979323846

static const int transcribe_Melody[4] = {60, 64, 67, 72}; /* C4 E4 G4 C5 */

static float transcribe_Hz(int midi) { return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f); }

/* Adds the test tune to x[from..n): 120 bpm square-wave quarter notes with a
 * decaying 60 Hz kick on every beat, looping the four-note melody for as long
 * as the buffer runs. Samples before `from` are left alone. */
static void transcribe_AddMelody(float *x, int n, int rate, int from) {
    for (int i = from; i < n; i++) {
        double t = (double)(i - from) / rate;
        int b = (int)floor(t / TRANSCRIBE_BEAT_SECONDS);
        double tb = t - b * TRANSCRIBE_BEAT_SECONDS;
        double hz = transcribe_Hz(transcribe_Melody[b % 4]);
        double ph = fmod(tb * hz, 1.0);
        double env = fmin(tb / 0.005, 1.0) * fmin((TRANSCRIBE_BEAT_SECONDS - tb) / 0.01, 1.0);
        double v = 0.3 * (ph < 0.5 ? 1.0 : -1.0) * env;
        if (tb < 0.06) v += 0.8 * sin(2.0 * TRANSCRIBE_PI * 60.0 * tb) * exp(-tb / 0.02);
        x[i] += (float)v;
    }
}

/* `seconds` of the test tune from sample 0. Caller frees. */
static float *transcribe_MakeSignalSeconds(int rate, double seconds, int *framesOut) {
    int n = (int)lround(seconds * rate);
    float *x = calloc((size_t)n, sizeof(float));
    transcribe_AddMelody(x, n, rate, 0);
    *framesOut = n;
    return x;
}

/* The standard eight bars (16 s). Caller frees. */
static float *transcribe_MakeSignal(int rate, int *framesOut) {
    return transcribe_MakeSignalSeconds(rate, TRANSCRIBE_BARS * 4 * TRANSCRIBE_BEAT_SECONDS, framesOut);
}

/* Deterministic uniform noise in -1..1 (a plain LCG, so the suite never
 * depends on rand()). */
static unsigned transcribe_NoiseState = 12345u;
static float transcribe_Noise(void) {
    transcribe_NoiseState = transcribe_NoiseState * 1103515245u + 12345u;
    return (float)((transcribe_NoiseState >> 8) & 0xFFFFu) / 32768.0f - 1.0f;
}

static bool transcribe_AnyDrumsIn(const Song *s, int from, int to) {
    for (int S = from; S < to; S++) if (s->patterns[s->order[S / SONG_STEPS]].drums[S % SONG_STEPS]) return true;
    return false;
}

static int transcribe_Lead(const Song *s, int step) {
    return s->patterns[s->order[step / SONG_STEPS]].tone[TRACK_LEAD][step % SONG_STEPS];
}

static int transcribe_Drums(const Song *s, int step) {
    return s->patterns[s->order[step / SONG_STEPS]].drums[step % SONG_STEPS];
}

static bool transcribe_SongSilent(const Song *s) {
    for (int p = 0; p < s->patternCount; p++) {
        for (int i = 0; i < SONG_STEPS; i++) {
            if (s->patterns[p].drums[i]) return false;
            for (int t = 0; t < SONG_TONE_TRACKS; t++) if (s->patterns[p].tone[t][i] != NOTE_NONE) return false;
        }
    }
    return true;
}

/* The checks shared by the 22050 and 48000 Hz runs: the beat grid is found,
 * the melody sits on it and the kicks sit on the beats. */
static void transcribe_CheckRun(int rate, const char *tag) {
    char msg[128];
    int n = 0;
    float *x = transcribe_MakeSignal(rate, &n);
    Song song;
    TranscribeOptions opt;
    TranscribeReport rep;
    Transcribe_DefaultOptions(&opt);
    bool ok = Transcribe_Run(x, n, rate, &opt, &song, &rep);
    snprintf(msg, sizeof(msg), "%s: Transcribe_Run succeeds", tag);
    CHECK(ok, msg);
    snprintf(msg, sizeof(msg), "%s: bpm within 2 of 120 (got %d)", tag, song.bpm);
    CHECK(song.bpm >= 118 && song.bpm <= 122, msg);
    CHECK(rep.bpm == song.bpm, "report bpm matches the song");
    CHECK(rep.bpmConfidence > 0.5f, "a metronomic signal is heard with confidence");
    CHECK(fabsf(rep.seconds - 16.0f) < 0.1f, "report length is 16 s");
    CHECK(rep.patterns == song.patternCount && rep.patterns >= 2 && rep.patterns <= 3, "128 steps fill two or three patterns");
    CHECK(rep.steps >= 128 && rep.steps <= rep.patterns * SONG_STEPS, "step count covers the recording");
    CHECK(!rep.truncated, "not truncated");
    CHECK(strcmp(song.name, "Imported") == 0, "imported song is named Imported");

    /* The active range is the song steps that carry a lead note or a kick;
     * padding before and silence after are left out. */
    int total = song.orderCount * SONG_STEPS;
    int first = -1, last = -1;
    for (int S = 0; S < total; S++) {
        if (transcribe_Lead(&song, S) >= 0 || (transcribe_Drums(&song, S) & DRUM_KICK)) {
            if (first < 0) first = S;
            last = S;
        }
    }
    CHECK(first >= 0, "the transcription is not empty");
    if (first < 0) { free(x); return; }
    first -= first % 4; /* the beat that holds the first hit */

    int beatSteps = 0, offSteps = 0, kickOnBeat = 0, kickOffBeat = 0;
    int bestMatch = 0;
    for (int phase = 0; phase < 4; phase++) {
        int match = 0;
        for (int S = first; S <= last; S += 4) {
            int j = (S - first) / 4;
            if (transcribe_Lead(&song, S) == transcribe_Melody[(j + phase) % 4]) match++;
        }
        if (match > bestMatch) bestMatch = match;
    }
    for (int S = first; S <= last; S++) {
        bool kick = (transcribe_Drums(&song, S) & DRUM_KICK) != 0;
        if ((S - first) % 4 == 0) { beatSteps++; if (kick) kickOnBeat++; }
        else { offSteps++; if (kick) kickOffBeat++; }
    }
    snprintf(msg, sizeof(msg), "%s: about 32 beats on the grid (got %d)", tag, beatSteps);
    CHECK(beatSteps >= 30 && beatSteps <= 34, msg);
    snprintf(msg, sizeof(msg), "%s: lead note at step 0 of at least 80%% of beats with the right pitch (%d of %d)", tag, bestMatch, beatSteps);
    CHECK(bestMatch * 5 >= beatSteps * 4, msg);
    snprintf(msg, sizeof(msg), "%s: kicks on at least 80%% of beat steps (%d of %d)", tag, kickOnBeat, beatSteps);
    CHECK(kickOnBeat * 5 >= beatSteps * 4, msg);
    snprintf(msg, sizeof(msg), "%s: kicks on fewer than 15%% of off-beat steps (%d of %d)", tag, kickOffBeat, offSteps);
    CHECK(kickOffBeat * 100 < offSteps * 15, msg);
    CHECK(rep.leadNotes >= 28 && rep.leadNotes <= 48, "lead note count is about one a beat");
    CHECK(rep.kicks >= kickOnBeat, "report counts the kicks");
    free(x);
}

static void transcribe_TestEstimateBpm(void) {
    float fps = 22050.0f / 256.0f;
    int frames = (int)(16.0f * fps);
    float *env = calloc((size_t)frames, sizeof(float));
    for (int k = 0; k * TRANSCRIBE_BEAT_SECONDS * fps < frames; k++) {
        int f = (int)lround(k * TRANSCRIBE_BEAT_SECONDS * fps);
        if (f < frames) env[f] = 1.0f;
    }
    float conf = -1.0f;
    int bpm = Transcribe_EstimateBpm(env, frames, fps, &conf);
    CHECK(bpm >= 118 && bpm <= 122, "impulse train at 120 bpm estimates 118..122");
    CHECK(conf > 0.5f, "a clean impulse train is confident");
    memset(env, 0, sizeof(float) * (size_t)frames);
    conf = -1.0f;
    CHECK(Transcribe_EstimateBpm(env, frames, fps, &conf) == 0, "flat envelope gives 0 bpm");
    CHECK(conf == 0.0f, "flat envelope gives 0 confidence");
    CHECK(Transcribe_EstimateBpm(NULL, 0, fps, NULL) == 0, "no envelope gives 0 without crashing");
    free(env);
}

static void transcribe_TestForcedBpm(void) {
    int n = 0;
    float *x = transcribe_MakeSignal(22050, &n);
    Song song;
    TranscribeOptions opt;
    TranscribeReport rep;
    Transcribe_DefaultOptions(&opt);
    opt.bpm = 100;
    CHECK(Transcribe_Run(x, n, 22050, &opt, &song, &rep), "forced-bpm run succeeds");
    CHECK(song.bpm == 100 && rep.bpm == 100, "forced bpm 100 is honoured");
    CHECK(rep.leadNotes > 0, "forced-bpm run still finds notes");

    opt.bpm = 0;
    opt.maxPatterns = 1;
    CHECK(Transcribe_Run(x, n, 22050, &opt, &song, &rep), "capped run succeeds");
    CHECK(song.patternCount == 1 && rep.patterns == 1 && rep.truncated, "maxPatterns 1 truncates to one pattern");
    CHECK(rep.steps == SONG_STEPS, "truncated step count is one pattern");

    opt.maxPatterns = 0;
    opt.noDrums = true;
    opt.noArp = true;
    CHECK(Transcribe_Run(x, n, 22050, &opt, &song, &rep), "no-drums no-arp run succeeds");
    bool anyDrum = false, anyArp = false;
    for (int p = 0; p < song.patternCount; p++) {
        for (int i = 0; i < SONG_STEPS; i++) {
            if (song.patterns[p].drums[i]) anyDrum = true;
            if (song.patterns[p].tone[TRACK_ARP][i] != NOTE_NONE) anyArp = true;
        }
    }
    CHECK(!anyDrum && rep.kicks == 0 && rep.snares == 0 && rep.hats == 0, "noDrums leaves the drum track empty");
    CHECK(!anyArp, "noArp leaves the arp track empty");
    free(x);
}

static void transcribe_TestSilenceAndShort(void) {
    Song song;
    TranscribeReport rep;
    int n = 22050 * 3;
    float *zeros = calloc((size_t)n, sizeof(float));
    CHECK(Transcribe_Run(zeros, n, 22050, NULL, &song, &rep), "three seconds of silence import fine");
    CHECK(transcribe_SongSilent(&song), "silence yields a song with no notes");
    CHECK(rep.leadNotes == 0 && rep.bassNotes == 0 && rep.kicks == 0 && rep.snares == 0 && rep.hats == 0, "silence report counts nothing");
    CHECK(rep.bpmConfidence == 0.0f, "silence has no beat confidence");
    CHECK(song.bpm >= SONG_BPM_MIN && song.bpm <= SONG_BPM_MAX, "silence still gets a legal bpm");
    free(zeros);

    float tiny[100];
    for (int i = 0; i < 100; i++) tiny[i] = 0.5f * sinf((float)i * 0.3f);
    memset(&song, 0x7f, sizeof(song));
    CHECK(!Transcribe_Run(tiny, 100, 22050, NULL, &song, &rep), "100 samples is too little");
    CHECK(song.patternCount == 1 && song.orderCount == 1 && song.bpm == 120, "too-short input leaves a valid empty song");
    CHECK(transcribe_SongSilent(&song), "too-short input leaves no notes");
    CHECK(strcmp(song.name, "Imported") == 0, "too-short input still names the song");
    CHECK(!Transcribe_Run(NULL, 0, 22050, NULL, &song, &rep), "no samples returns false");
    CHECK(song.patternCount == 1, "no samples still leaves a valid song");
}

static void transcribe_TestSalience(void) {
    const int bins = 1025;
    float binHz = 22050.0f / 2048.0f;
    float *mag = calloc((size_t)bins, sizeof(float));
    /* G4 with five harmonics falling off like a sawtooth, spread over the
     * neighbouring bins the way a Hann window would. */
    int target = 67;
    for (int h = 1; h <= 5; h++) {
        float pos = (float)h * transcribe_Hz(target) / binHz;
        int k = (int)pos;
        float frac = pos - (float)k;
        mag[k] += (1.0f - frac) / (float)h;
        mag[k + 1] += frac / (float)h;
    }
    int best = -1;
    float bestV = -1.0f;
    for (int m = 28; m <= 95; m++) {
        float s = Transcribe_Salience(mag, bins, binHz, m);
        if (s > bestV) { bestV = s; best = m; }
    }
    CHECK(best == target, "salience peaks at the note that owns the harmonics");
    CHECK(Transcribe_Salience(mag, bins, binHz, target) > Transcribe_Salience(mag, bins, binHz, target - 12), "the fundamental beats the octave below");
    CHECK(Transcribe_Salience(mag, bins, binHz, target) > Transcribe_Salience(mag, bins, binHz, target + 12), "the fundamental beats the octave above");
    CHECK(Transcribe_Salience(mag, bins, binHz, 40) == 0.0f, "a note with no energy has no salience");
    CHECK(Transcribe_Salience(NULL, bins, binHz, 60) == 0.0f, "no spectrum gives no salience");
    free(mag);
}

/* A recording that sits a little off zero (a cheap mic, a tape transfer) must
 * not have its silence read as signal: before DC removal the per-step RMS of a
 * quiet intro was the offset itself, which beat the 2% gate and let the arp
 * pass fit chords to the noise floor. */
static void transcribe_TestDcOffset(void) {
    Song song;
    TranscribeReport rep;
    const int rate = 22050;

    /* A pure offset is silence to a listener and must be silence to the importer. */
    int n = rate * 3;
    float *flat = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) flat[i] = 0.5f;
    CHECK(Transcribe_Run(flat, n, rate, NULL, &song, &rep), "a constant 0.5 imports fine");
    CHECK(transcribe_SongSilent(&song), "a constant 0.5 yields a song with no notes");
    CHECK(rep.leadNotes == 0 && rep.bassNotes == 0 && rep.kicks == 0 && rep.snares == 0 && rep.hats == 0, "a constant offset counts nothing");
    for (int i = 0; i < n; i++) flat[i] = -1.0f;
    CHECK(Transcribe_Run(flat, n, rate, NULL, &song, &rep) && transcribe_SongSilent(&song), "a constant -1.0 yields a song with no notes");
    free(flat);

    /* Eight seconds of faint noise, then the tune, the whole file offset by
     * 0.02 (about -34 dBFS). The intro is 64 steps; the first 56 song steps
     * are inside it whatever the downbeat padding chose. */
    int intro = rate * 8;
    n = intro + (int)lround(TRANSCRIBE_BARS * 4 * TRANSCRIBE_BEAT_SECONDS * rate);
    float *x = calloc((size_t)n, sizeof(float));
    transcribe_NoiseState = 12345u;
    for (int i = 0; i < n; i++) x[i] = 0.002f * transcribe_Noise() + 0.02f;
    transcribe_AddMelody(x, n, rate, intro);
    CHECK(Transcribe_Run(x, n, rate, NULL, &song, &rep), "offset intro file imports fine");
    int introArp = 0, introLead = 0, introBass = 0;
    for (int S = 0; S < 56; S++) {
        const SongPattern *p = &song.patterns[song.order[S / SONG_STEPS]];
        if (p->tone[TRACK_ARP][S % SONG_STEPS] != NOTE_NONE) introArp++;
        if (p->tone[TRACK_LEAD][S % SONG_STEPS] != NOTE_NONE) introLead++;
        if (p->tone[TRACK_BASS][S % SONG_STEPS] != NOTE_NONE) introBass++;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "no arp chords on the silent offset intro (got %d cells)", introArp);
    CHECK(introArp == 0, msg);
    snprintf(msg, sizeof(msg), "no lead or bass on the silent offset intro (got %d and %d)", introLead, introBass);
    CHECK(introLead == 0 && introBass == 0, msg);
    CHECK(rep.leadNotes >= 28 && rep.leadNotes <= 48, "the tune after the intro is still transcribed");
    CHECK(song.bpm >= 118 && song.bpm <= 122, "offset does not disturb the tempo");

    /* The analysis has no random state: the same samples give the same song. */
    Song again;
    CHECK(Transcribe_Run(x, n, rate, NULL, &again, NULL), "second run of the same file succeeds");
    CHECK(memcmp(&song, &again, sizeof(Song)) == 0, "the import is deterministic");
    free(x);
}

/* The step grid runs on to the end of the final beat, a little past the last
 * STFT frame. Those trailing steps used to clamp onto the final frame and
 * each re-read its hit, so a recording cut just after a downbeat ended in a
 * roll of three or four identical kicks. */
static void transcribe_TestTrailingSteps(void) {
    char msg[160];
    const double cuts[3] = {16.02, 16.03, 16.04};
    for (int c = 0; c < 3; c++) {
        int n = 0;
        float *x = transcribe_MakeSignalSeconds(22050, cuts[c], &n);
        Song song;
        TranscribeReport rep;
        CHECK(Transcribe_Run(x, n, 22050, NULL, &song, &rep), "cut recording imports fine");
        /* 33 downbeats are in the audio (0.0 s to 16.0 s); the first one at
         * sample 0 has nothing before it to make flux against, so 32 or 33. */
        snprintf(msg, sizeof(msg), "cut at %.2f s: one kick per beat, no roll at the end (got %d)", cuts[c], rep.kicks);
        CHECK(rep.kicks >= 31 && rep.kicks <= 33, msg);
        int last = -1;
        for (int S = 0; S < rep.steps; S++) if (song.patterns[song.order[S / SONG_STEPS]].drums[S % SONG_STEPS] & DRUM_KICK) last = S;
        CHECK(last >= 4, "the cut recording has kicks");
        if (last < 4) { free(x); continue; }
        /* The final hit is a downbeat: a kick one beat earlier, nothing on the
         * three off-beat steps between them, and nothing after it. */
        bool beatBefore = (song.patterns[song.order[(last - 4) / SONG_STEPS]].drums[(last - 4) % SONG_STEPS] & DRUM_KICK) != 0;
        snprintf(msg, sizeof(msg), "cut at %.2f s: the last kick sits a beat after the one before", cuts[c]);
        CHECK(beatBefore, msg);
        snprintf(msg, sizeof(msg), "cut at %.2f s: the three steps before the last kick carry no drums", cuts[c]);
        CHECK(!transcribe_AnyDrumsIn(&song, last - 3, last), msg);
        snprintf(msg, sizeof(msg), "cut at %.2f s: the steps past the end of the audio carry no drums", cuts[c]);
        CHECK(!transcribe_AnyDrumsIn(&song, last + 1, rep.steps), msg);
        free(x);
    }
}

void test_transcribe_all(void) {
    transcribe_TestSalience();
    transcribe_TestEstimateBpm();
    transcribe_CheckRun(22050, "22050 Hz");
    transcribe_CheckRun(48000, "48000 Hz");
    transcribe_TestForcedBpm();
    transcribe_TestSilenceAndShort();
    transcribe_TestDcOffset();
    transcribe_TestTrailingSteps();
}
