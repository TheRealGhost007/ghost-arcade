#include "musicsynth.h"
#include "safefile.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_D 3.14159265358979323846

static const int kScales[SCALE_COUNT][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10},
    {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 5, 7, 9, 10},
};

/* One theme per game, plus the launcher's. The numbers are chosen so that no
 * two share a key, a tempo and a progression: they are meant to be told apart
 * by ear after two bars. */
static const MusicTheme kThemes[] = {
    /* slug          bpm root scale             prog           bass arp drum seed oct swing */
    {"ghostarcade",   96, 45, SCALE_MINOR,      {0, 5, 3, 4},   0, 1, 2, 101, 2, 0.10f},
    {"blockfall",    140, 50, SCALE_MINOR,      {0, 5, 2, 6},   3, 0, 0, 202, 2, 0.00f},
    {"brickburst",   128, 48, SCALE_MAJOR,      {0, 4, 5, 3},   0, 0, 1, 404, 2, 0.00f},
    {"skyraid",      150, 43, SCALE_HARMONIC,   {0, 5, 3, 4},   3, 1, 3, 505, 2, 0.00f},
    {"rockdrift",    112, 46, SCALE_MINOR,      {0, 6, 5, 6},   2, 1, 1, 707, 3, 0.00f},
    {"crawlshot",    124, 47, SCALE_HARMONIC,   {0, 3, 4, 0},   0, 3, 1, 909, 2, 0.00f},
    {"moondrop",      84, 53, SCALE_DORIAN,     {0, 4, 1, 4},   2, 1, 2, 111, 3, 0.10f},
    {"pondkings",    118, 55, SCALE_MAJOR,      {0, 3, 0, 4},   1, 1, 1, 444, 2, 0.18f}, /* a jig on the pond */
};
#define THEME_N ((int)(sizeof(kThemes) / sizeof(kThemes[0])))

const MusicTheme *Music_FindTheme(const char *slug) {
    if (slug) for (int i = 0; i < THEME_N; i++) if (strcmp(kThemes[i].slug, slug) == 0) return &kThemes[i];
    return &kThemes[0];
}
int Music_ThemeCount(void) { return THEME_N; }
const MusicTheme *Music_ThemeAt(int i) { return &kThemes[(i % THEME_N + THEME_N) % THEME_N]; }

float Synth_NoteHz(int midi) { return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f); }
float Synth_LoopSeconds(const MusicTheme *t) { return (float)SYNTH_STEPS * 60.0f / ((float)t->bpm * 4.0f); }

/* ---------------------------------------------------------------- composing */

static uint32_t Lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

/* A scale degree (which may run past the octave or below the root) as a MIDI note. */
static int Degree(const MusicTheme *t, int deg) {
    int oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    int idx = deg - oct * 7;
    return t->rootMidi + kScales[t->scale][idx] + oct * 12;
}

/* Chord tone k (0 root, 1 third, 2 fifth) of the chord rooted on scale degree d. */
static int ChordTone(const MusicTheme *t, int d, int k) { return Degree(t, d + k * 2); }

static int8_t Note8(int n) { return (int8_t)(n < 0 ? 0 : (n > 127 ? 127 : n)); }

void Music_ThemeToSong(const MusicTheme *t, Song *song) {
    Song_Init(song, t->slug);
    song->bpm = t->bpm;
    song->swing = t->swing;
    song->keyRoot = t->rootMidi % 12;
    song->keyScale = (int)t->scale;
    SongPattern *p = &song->patterns[0];
    int8_t *bass = p->tone[TRACK_BASS], *arp = p->tone[TRACK_ARP], *lead = p->tone[TRACK_LEAD];

    for (int bar = 0; bar < 4; bar++) {
        int d = t->prog[bar];
        int root = ChordTone(t, d, 0) - 12;
        for (int i = 0; i < 16; i++) {
            int st = bar * 16 + i;
            /* bass */
            bool on = false;
            int note = root;
            switch (t->bassStyle) {
                case 0: on = (i % 2) == 0; break;
                case 1: on = (i % 4) == 2 || i == 0; break;
                case 2: on = (i % 4) == 0 || i == 6 || i == 14; note = (i == 6 || i == 14) ? root + 12 : root; break;
                default: on = true; note = (i % 4 == 2) ? root + 12 : root; break;
            }
            if (on) bass[st] = Note8(note);
            /* arpeggio */
            int k = 0;
            switch (t->arpStyle) {
                case 0: k = i % 3; break;
                case 1: { static const int up[6] = {0, 1, 2, 1, 0, 1}; k = up[i % 6]; break; }
                case 2: { static const int sk[4] = {0, 2, 1, 2}; k = sk[i % 4]; break; }
                default: { static const int br[8] = {0, 2, 1, 2, 0, 1, 2, 1}; k = br[i % 8]; break; }
            }
            if (i % 2 == 0 || t->arpStyle != 2) arp[st] = Note8(ChordTone(t, d, k) + 12);
            /* drums */
            uint8_t dr = 0;
            switch (t->drumStyle) {
                case 0: if (i % 4 == 0) dr |= 1; if (i % 2 == 1 || i % 4 == 2) dr |= 4; if (i % 8 == 4) dr |= 2; break;
                case 1: if (i == 0 || i == 10) dr |= 1; if (i == 4 || i == 12) dr |= 2; if (i % 2 == 0) dr |= 4; break;
                case 2: if (i == 0) dr |= 1; if (i == 8) dr |= 2; if (i % 4 == 2) dr |= 4; break;
                default: if (i == 0 || i == 7 || i == 10) dr |= 1; if (i == 4 || i == 12 || i == 15) dr |= 2; if (i % 2 == 1) dr |= 4; break;
            }
            p->drums[st] = dr;
        }
        /* lead: a bar-long motif, a rhythm mask chosen by the seed, stepwise motion that lands on chord tones on the strong beats */
        uint32_t rng = (uint32_t)t->leadSeed * 2654435761u + (uint32_t)(bar % 2) * 40503u + (bar == 3 ? 977u : 0u);
        static const uint16_t kMasks[8] = {0x9249, 0x8A8A, 0xA4A4, 0x9292, 0xAAAA, 0x8888 | 0x0505, 0xC0C0 | 0x1111, 0x9999};
        uint16_t mask = kMasks[Lcg(&rng) % 8];
        int deg = d + 4 + (int)(Lcg(&rng) % 3);
        int lastStrong = deg;
        for (int i = 0; i < 16; i++) {
            if (!((mask >> (15 - i)) & 1)) continue;
            bool strong = (i % 4) == 0;
            if (strong) {
                int k = (int)(Lcg(&rng) % 3);
                deg = d + k * 2 + 7 * (int)(Lcg(&rng) % 2);
                lastStrong = deg;
            } else {
                static const int mv[6] = {-2, -1, 1, 1, 2, 0};
                deg += mv[Lcg(&rng) % 6];
                if (deg > lastStrong + 4) deg = lastStrong + 2;
                if (deg < lastStrong - 4) deg = lastStrong - 2;
            }
            if (bar == 3 && i >= 12) deg = d + 7; /* the phrase ends on the chord root */
            lead[bar * 16 + i] = Note8(Degree(t, deg) + 12 * (t->leadOctave - 2));
        }
    }
}

/* ---------------------------------------------------------------- synthesis */

static double StepSamples(const Synth *s) {
    int bpm = (s->song && s->song->bpm > 0) ? s->song->bpm : 120;
    return (double)s->sampleRate * 60.0 / ((double)bpm * 4.0);
}

void Synth_Init(Synth *s, const Song *song, int sampleRate) {
    memset(s, 0, sizeof(*s));
    s->song = song;
    s->sampleRate = sampleRate;
    s->gain = 1.0f;
    s->intensity = 0.6f;
    s->noise = 0x12345678u;
    s->loopPattern = -1;
    s->previewTrack = -1;
    s->samplesPerStep = StepSamples(s);
    Synth_SetPosition(s, 0, 0);
}

void Synth_SetIntensity(Synth *s, float intensity) {
    s->intensity = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity);
}

void Synth_SetPosition(Synth *s, int orderPos, int step) {
    int oc = s->song ? s->song->orderCount : 1;
    if (oc < 1) oc = 1;
    s->orderPos = ((orderPos % oc) + oc) % oc;
    /* The next sample triggers `step`, so park one step before it. Step -1 is
     * a legal parking spot (Trigger only wraps at SONG_STEPS), which keeps
     * orderPos pointing at the position actually about to play. */
    s->step = ((step % SONG_STEPS) + SONG_STEPS) % SONG_STEPS - 1;
    /* The parked step must be exactly over, measured by ITS OWN length: with
     * swing an even step runs long and an odd one short, so parking on the
     * plain step length left an odd target waiting a whole swing delay. */
    const float swingDelay = (s->song ? s->song->swing : 0.0f) * (float)s->samplesPerStep; /* the same float arithmetic as Synth_Render, so the compare is exact */
    s->samplesIntoStep = s->samplesPerStep + (((s->step + 1) % 2) == 1 ? swingDelay : -swingDelay);
    s->finished = false;
}

void Synth_SetLoopPattern(Synth *s, int pattern) { s->loopPattern = pattern; }

void Synth_Preview(Synth *s, int track, int note) {
    s->previewNote = note;
    s->previewTrack = track;
}

int Synth_CurrentPattern(const Synth *s) {
    if (!s->song || s->song->patternCount <= 0) return -1;
    if (s->loopPattern >= 0 && s->loopPattern < s->song->patternCount) return s->loopPattern;
    if (s->song->orderCount <= 0) return 0;
    int pos = s->orderPos % s->song->orderCount;
    int p = s->song->order[pos];
    return p < s->song->patternCount ? p : 0;
}

static inline float Pulse(double phase, float duty) { return (phase - floor(phase)) < duty ? 1.0f : -1.0f; }
static inline float Tri(double phase) { double p = phase - floor(phase); return (float)(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }
static inline float Saw(double phase) { double p = phase - floor(phase); return (float)(2.0 * p - 1.0); }

static float Osc(SongWave w, double phase) {
    switch (w) {
        case WAVE_PULSE50: return Pulse(phase, 0.5f);
        case WAVE_PULSE25: return Pulse(phase, 0.25f);
        case WAVE_PULSE12: return Pulse(phase, 0.125f);
        case WAVE_SAW: return Saw(phase);
        default: return Tri(phase);
    }
}

/* Perceived loudness of each wave at the same amplitude, used so that the
 * classic voices keep their original mix and a swapped wave sits at the same
 * level. */
static float WaveLoud(SongWave w) {
    switch (w) {
        case WAVE_PULSE50: return 0.40f;
        case WAVE_PULSE25: return 0.45f;
        case WAVE_PULSE12: return 0.55f;
        case WAVE_SAW: return 0.45f;
        default: return 1.0f;
    }
}
static const SongWave kDefaultWave[SONG_TONE_TRACKS] = {WAVE_TRI, WAVE_PULSE25, WAVE_PULSE50};
static const float kTrackGain[SONG_TONE_TRACKS] = {0.34f, 0.10f, 0.13f};
static const float kDecay[SONG_TONE_TRACKS] = {0.99992f, 0.99930f, 0.99975f};

static void TriggerTone(Synth *s, int t, int note) {
    if (note == NOTE_HOLD) {
        /* A hold keeps the note sounding at the level it has reached; it never
         * raises the envelope (that re-attacked a note), and a voice that has
         * not sounded yet has nothing to hold: raising it would put a silent
         * oscillator's constant output on the mix as a DC thump. */
        s->held[t] = s->freq[t] > 0.0f;
    } else if (note >= 0) {
        s->freq[t] = Synth_NoteHz(note);
        s->env[t] = 1.0f;
        s->held[t] = false;
        if (t == TRACK_BASS) s->bassAge = 0;
    } else {
        s->held[t] = false;
    }
}

static void TriggerDrums(Synth *s, uint8_t d) {
    if (d & DRUM_KICK) { s->envKick = 1.0f; s->phKick = 0.0; }
    if (d & DRUM_SNARE) s->envSnare = 1.0f;
    if (d & DRUM_HAT) s->envHat = 1.0f;
}

/* True when the step after this one would wrap the order back to its start
 * and the synth was asked to stop there (the WAV export). */
static bool AtOrderEnd(const Synth *s) {
    if (!s->stopAtOrderEnd || s->loopPattern >= 0) return false;
    return !s->song || s->song->orderCount <= 0 || s->orderPos + 1 >= s->song->orderCount;
}

/* The pattern that follows step 63 of the current one, or -1 when nothing does. */
static int NextPattern(const Synth *s) {
    const Song *song = s->song;
    if (!song || song->patternCount <= 0) return -1;
    if (s->loopPattern >= 0 && s->loopPattern < song->patternCount) return s->loopPattern;
    if (AtOrderEnd(s)) return -1;
    if (song->orderCount <= 0) return 0;
    int p = song->order[(s->orderPos + 1) % song->orderCount];
    return p < song->patternCount ? p : 0;
}

static void Trigger(Synth *s) {
    if (s->step + 1 >= SONG_STEPS && AtOrderEnd(s)) {
        /* One pass is over: stay on the last step and let everything ring out
         * (a held note would otherwise sustain into the tail). */
        s->finished = true;
        for (int t = 0; t < SONG_TONE_TRACKS; t++) s->held[t] = false;
        return;
    }
    s->step++;
    if (s->step >= SONG_STEPS) {
        s->step = 0;
        if (s->loopPattern < 0 && s->song && s->song->orderCount > 0) s->orderPos = (s->orderPos + 1) % s->song->orderCount;
    }
    s->samplesPerStep = StepSamples(s);
    int pi = Synth_CurrentPattern(s);
    if (pi < 0) return;
    const SongPattern *p = &s->song->patterns[pi];
    int st = s->step;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        int note = p->tone[t][st];
        TriggerTone(s, t, note);
        /* A note followed by a hold is sustained from its attack. Learning of
         * the hold only when its own step triggers let the note decay for a
         * whole step first, then raised it again: heard as a second attack. */
        if (note != NOTE_NONE && s->freq[t] > 0.0f) {
            int next = NOTE_NONE;
            if (st < SONG_STEPS - 1) next = p->tone[t][st + 1];
            else {
                int np = NextPattern(s);
                if (np >= 0) next = s->song->patterns[np].tone[t][0];
            }
            if (next == NOTE_HOLD) s->held[t] = true;
        }
    }
    TriggerDrums(s, p->drums[st]);
}

void Synth_Render(Synth *s, int16_t *out, int frames) {
    const double inv = 1.0 / (double)s->sampleRate;
    const Song *song = s->song;
    const float swing = song ? song->swing : 0.0f;
    const float swingDelay = swing * (float)s->samplesPerStep;

    if (s->previewTrack >= 0) {
        int t = s->previewTrack, n = s->previewNote;
        s->previewTrack = -1;
        if (t < SONG_TONE_TRACKS) TriggerTone(s, t, n);
        else TriggerDrums(s, (uint8_t)n);
    }

    float vol[SONG_TRACKS];
    SongWave wave[SONG_TONE_TRACKS];
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        wave[t] = song ? song->wave[t] : kDefaultWave[t];
        if (wave[t] < 0 || wave[t] >= WAVE_COUNT) wave[t] = kDefaultWave[t];
        vol[t] = kTrackGain[t] * WaveLoud(wave[t]) / WaveLoud(kDefaultWave[t]) * (song ? (float)song->volume[t] / 100.0f : 1.0f);
    }
    vol[TRACK_DRUMS] = song ? (float)song->volume[TRACK_DRUMS] / 100.0f : 1.0f;

    /* How long the bass floor below lasts after a trigger: long enough to
     * bridge the gaps in every built-in theme (under a second), short enough
     * that a bass note nothing follows does die away. */
    const uint32_t floorSamples = (uint32_t)(1.5 * (double)s->sampleRate);

    for (int n = 0; n < frames; n++) {
        double stepLen = s->samplesPerStep + (((s->step + 1) % 2) == 1 ? swingDelay : -swingDelay);
        if (!s->finished && s->samplesIntoStep >= stepLen) { s->samplesIntoStep -= stepLen; Trigger(s); }
        s->samplesIntoStep += 1.0;

        float iv = s->intensity;
        float leadOn = iv > 0.35f ? fminf(1.0f, (iv - 0.35f) / 0.15f) : 0.0f;
        float hatOn = iv > 0.15f ? fminf(1.0f, (iv - 0.15f) / 0.15f) : 0.0f;
        float snareOn = iv > 0.5f ? fminf(1.0f, (iv - 0.5f) / 0.15f) : 0.0f;
        float kickOn = iv > 0.25f ? fminf(1.0f, (iv - 0.25f) / 0.15f) : 0.0f;

        /* envelopes decay per sample; a held note barely does */
        for (int t = 0; t < SONG_TONE_TRACKS; t++) s->env[t] *= s->held[t] ? 0.999995f : kDecay[t];
        /* the bass keeps a floor between the retriggers of a bass line, but
         * only once it has sounded (a silent oscillator at phase 0 would sit at
         * a constant level) and only for a while after the last trigger, so a
         * final or previewed bass note does not hum on for ever */
        if (s->bassAge < floorSamples) {
            s->bassAge++;
            if (s->freq[TRACK_BASS] > 0.0f && s->env[TRACK_BASS] < 0.05f) s->env[TRACK_BASS] = 0.05f;
        }
        s->envKick *= 0.9990f;
        s->envSnare *= 0.9993f;
        s->envHat *= 0.9975f;

        s->vibPhase += 5.5 * inv;
        if (s->vibPhase >= 1.0) s->vibPhase -= 1.0;
        float vib = 1.0f + 0.006f * sinf((float)(2.0 * PI_D * s->vibPhase)) * (1.0f - s->env[TRACK_LEAD] * 0.6f);

        s->ph[TRACK_BASS] += s->freq[TRACK_BASS] * inv;
        s->ph[TRACK_ARP] += s->freq[TRACK_ARP] * inv;
        s->ph[TRACK_LEAD] += s->freq[TRACK_LEAD] * vib * inv;
        s->phKick += (48.0 + 110.0 * s->envKick * s->envKick) * inv;

        float v = 0.0f;
        v += Osc(wave[TRACK_BASS], s->ph[TRACK_BASS]) * vol[TRACK_BASS] * s->env[TRACK_BASS];
        v += Osc(wave[TRACK_ARP], s->ph[TRACK_ARP]) * vol[TRACK_ARP] * s->env[TRACK_ARP];
        v += Osc(wave[TRACK_LEAD], s->ph[TRACK_LEAD]) * vol[TRACK_LEAD] * s->env[TRACK_LEAD] * leadOn;

        s->noise = s->noise * 1664525u + 1013904223u;
        float nz = ((float)(s->noise >> 16) / 32768.0f) - 1.0f;
        float d = 0.0f;
        d += nz * 0.09f * s->envHat * s->envHat * hatOn;
        d += nz * 0.12f * s->envSnare * snareOn;
        d += sinf((float)(2.0 * PI_D * s->phKick)) * 0.36f * s->envKick * kickOn;
        v += d * vol[TRACK_DRUMS];

        /* one-pole low-pass for the muffled (paused) sound */
        float a = 1.0f - 0.92f * s->muffle;
        s->lp += (v - s->lp) * a;
        v = s->muffle > 0.001f ? s->lp : v;

        v *= s->gain;
        if (v > 0.98f) v = 0.98f;
        if (v < -0.98f) v = -0.98f;
        out[n] = (int16_t)(v * 32000.0f);
    }
}

/* ------------------------------------------------------------------ export */

static void PutLE32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((int)((v >> (8 * i)) & 0xFF), f); }
static void PutLE16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }

bool Synth_RenderWavFile(const Song *song, const char *path, int sampleRate, float maxSeconds) {
    if (!song || !path || sampleRate < 8000) return false;
    float seconds = Song_Seconds(song) + 1.0f;
    if (maxSeconds > 0.0f && seconds > maxSeconds) seconds = maxSeconds;
    uint32_t frames = (uint32_t)(seconds * (float)sampleRate);
    if (frames == 0) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    uint32_t dataBytes = frames * 2u;
    fwrite("RIFF", 1, 4, f);
    PutLE32(f, 36u + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    PutLE32(f, 16);
    PutLE16(f, 1);                       /* PCM */
    PutLE16(f, 1);                       /* mono */
    PutLE32(f, (uint32_t)sampleRate);
    PutLE32(f, (uint32_t)sampleRate * 2u);
    PutLE16(f, 2);                       /* block align */
    PutLE16(f, 16);                      /* bits */
    fwrite("data", 1, 4, f);
    PutLE32(f, dataBytes);

    Synth s;
    Synth_Init(&s, song, sampleRate);
    Synth_SetIntensity(&s, 1.0f);
    s.stopAtOrderEnd = true; /* the tail is the last notes decaying, not the song starting over */
    int16_t buf[1024];
    uint32_t done = 0;
    while (done < frames) {
        int chunk = (int)(frames - done > 1024 ? 1024 : frames - done);
        Synth_Render(&s, buf, chunk);
        for (int i = 0; i < chunk; i++) PutLE16(f, (uint16_t)buf[i]);
        done += (uint32_t)chunk;
    }
    return SafeFile_Close(f);
}
