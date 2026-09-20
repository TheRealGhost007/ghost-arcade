#include "musicsynth.h"
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
    {"coilrush",     132, 52, SCALE_DORIAN,     {0, 3, 4, 3},   1, 2, 1, 303, 2, 0.06f},
    {"brickburst",   128, 48, SCALE_MAJOR,      {0, 4, 5, 3},   0, 0, 1, 404, 2, 0.00f},
    {"skyraid",      150, 43, SCALE_HARMONIC,   {0, 5, 3, 4},   3, 1, 3, 505, 2, 0.00f},
    {"ghostmaze",     88, 41, SCALE_PHRYGIAN,   {0, 1, 0, 6},   2, 3, 2, 606, 2, 0.12f},
    {"rockdrift",    112, 46, SCALE_MINOR,      {0, 6, 5, 6},   2, 1, 1, 707, 3, 0.00f},
    {"lanehop",      136, 55, SCALE_MIXOLYDIAN, {0, 6, 3, 0},   1, 2, 3, 808, 2, 0.08f},
    {"crawlshot",    124, 47, SCALE_HARMONIC,   {0, 3, 4, 0},   0, 3, 1, 909, 2, 0.00f},
    {"moondrop",      84, 53, SCALE_DORIAN,     {0, 4, 1, 4},   2, 1, 2, 111, 3, 0.10f},
    {"gemdive",       118, 44, SCALE_MINOR,      {0, 2, 5, 4},   1, 0, 1, 222, 2, 0.05f},
    {"girderclimb",  144, 49, SCALE_MIXOLYDIAN, {0, 3, 6, 4},   3, 2, 0, 333, 2, 0.00f},
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

static void Compose(Synth *s) {
    const MusicTheme *t = s->theme;
    memset(s->bass, -1, sizeof(s->bass));
    memset(s->arp, -1, sizeof(s->arp));
    memset(s->lead, -1, sizeof(s->lead));
    memset(s->drums, 0, sizeof(s->drums));

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
            if (on) s->bass[st] = (int8_t)note;
            /* arpeggio */
            int k = 0;
            switch (t->arpStyle) {
                case 0: k = i % 3; break;
                case 1: { static const int up[6] = {0, 1, 2, 1, 0, 1}; k = up[i % 6]; break; }
                case 2: { static const int sk[4] = {0, 2, 1, 2}; k = sk[i % 4]; break; }
                default: { static const int br[8] = {0, 2, 1, 2, 0, 1, 2, 1}; k = br[i % 8]; break; }
            }
            if (i % 2 == 0 || t->arpStyle != 2) s->arp[st] = (int8_t)(ChordTone(t, d, k) + 12);
            /* drums */
            uint8_t dr = 0;
            switch (t->drumStyle) {
                case 0: if (i % 4 == 0) dr |= 1; if (i % 2 == 1 || i % 4 == 2) dr |= 4; if (i % 8 == 4) dr |= 2; break;
                case 1: if (i == 0 || i == 10) dr |= 1; if (i == 4 || i == 12) dr |= 2; if (i % 2 == 0) dr |= 4; break;
                case 2: if (i == 0) dr |= 1; if (i == 8) dr |= 2; if (i % 4 == 2) dr |= 4; break;
                default: if (i == 0 || i == 7 || i == 10) dr |= 1; if (i == 4 || i == 12 || i == 15) dr |= 2; if (i % 2 == 1) dr |= 4; break;
            }
            s->drums[st] = dr;
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
            s->lead[bar * 16 + i] = (int8_t)(Degree(t, deg) + 12 * (t->leadOctave - 2));
        }
    }
}

/* ---------------------------------------------------------------- synthesis */

void Synth_Init(Synth *s, const MusicTheme *theme, int sampleRate) {
    memset(s, 0, sizeof(*s));
    s->theme = theme;
    s->sampleRate = sampleRate;
    s->gain = 1.0f;
    s->intensity = 0.6f;
    s->noise = 0x12345678u;
    s->samplesPerStep = (double)sampleRate * 60.0 / ((double)theme->bpm * 4.0);
    s->samplesIntoStep = s->samplesPerStep; /* trigger step 0 on the first sample */
    s->step = SYNTH_STEPS - 1;
    Compose(s);
}

void Synth_SetIntensity(Synth *s, float intensity) {
    s->intensity = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity);
}

static inline float Pulse(double phase, float duty) { return (phase - floor(phase)) < duty ? 1.0f : -1.0f; }
static inline float Tri(double phase) { double p = phase - floor(phase); return (float)(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }

static void Trigger(Synth *s) {
    s->step = (s->step + 1) % SYNTH_STEPS;
    int st = s->step;
    if (s->bass[st] >= 0) { s->freqBass = Synth_NoteHz(s->bass[st]); s->envBass = 1.0f; }
    if (s->arp[st] >= 0) { s->freqArp = Synth_NoteHz(s->arp[st]); s->envArp = 1.0f; }
    if (s->lead[st] >= 0) { s->freqLead = Synth_NoteHz(s->lead[st]); s->envLead = 1.0f; }
    uint8_t d = s->drums[st];
    if (d & 1) { s->envKick = 1.0f; s->phKick = 0.0; }
    if (d & 2) s->envSnare = 1.0f;
    if (d & 4) s->envHat = 1.0f;
}

void Synth_Render(Synth *s, int16_t *out, int frames) {
    const double inv = 1.0 / (double)s->sampleRate;
    const float swingDelay = s->theme->swing * (float)s->samplesPerStep;
    for (int n = 0; n < frames; n++) {
        double stepLen = s->samplesPerStep + (((s->step + 1) % 2) == 1 ? swingDelay : -swingDelay);
        if (s->samplesIntoStep >= stepLen) { s->samplesIntoStep -= stepLen; Trigger(s); }
        s->samplesIntoStep += 1.0;

        float iv = s->intensity;
        float leadOn = iv > 0.35f ? fminf(1.0f, (iv - 0.35f) / 0.15f) : 0.0f;
        float hatOn = iv > 0.15f ? fminf(1.0f, (iv - 0.15f) / 0.15f) : 0.0f;
        float snareOn = iv > 0.5f ? fminf(1.0f, (iv - 0.5f) / 0.15f) : 0.0f;
        float kickOn = iv > 0.25f ? fminf(1.0f, (iv - 0.25f) / 0.15f) : 0.0f;

        /* envelopes decay per sample */
        s->envBass *= 0.99992f; if (s->envBass < 0.05f) s->envBass = 0.05f;
        s->envArp *= 0.99930f;
        s->envLead *= 0.99975f;
        s->envKick *= 0.9990f;
        s->envSnare *= 0.9993f;
        s->envHat *= 0.9975f;

        s->vibPhase += 5.5 * inv;
        float vib = 1.0f + 0.006f * sinf((float)(2.0 * PI_D * s->vibPhase)) * (1.0f - s->envLead * 0.6f);

        s->phBass += s->freqBass * inv;
        s->phArp += s->freqArp * inv;
        s->phLead += s->freqLead * vib * inv;
        s->phKick += (48.0 + 110.0 * s->envKick * s->envKick) * inv;

        float v = 0.0f;
        v += Tri(s->phBass) * 0.34f * s->envBass;
        v += Pulse(s->phArp, 0.25f) * 0.10f * s->envArp;
        v += Pulse(s->phLead, 0.5f) * 0.13f * s->envLead * leadOn;

        s->noise = s->noise * 1664525u + 1013904223u;
        float nz = ((float)(s->noise >> 16) / 32768.0f) - 1.0f;
        v += nz * 0.09f * s->envHat * s->envHat * hatOn;
        v += nz * 0.12f * s->envSnare * snareOn + (float)sin(2.0 * PI_D * s->phKick) * 0.0f;
        v += sinf((float)(2.0 * PI_D * s->phKick)) * 0.36f * s->envKick * kickOn;

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
