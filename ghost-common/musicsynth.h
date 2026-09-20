#ifndef MUSICSYNTH_H
#define MUSICSYNTH_H

#include <stdint.h>

/* The chiptune synthesiser behind ghost-common's music: no raylib, so it can
 * be rendered and tested headless. A THEME is a handful of numbers (tempo,
 * key, scale, a four-bar chord progression, style picks and a seed); the
 * bass, arpeggio, lead melody and drums are composed from it deterministically,
 * so each game gets its own tune without any audio files. Four voices:
 *
 *   bass   triangle, plays the chord root
 *   arp    25% pulse, walks the chord tones in sixteenths
 *   lead   50% pulse with a little vibrato, a seeded melody
 *   drums  noise hats and snares, a sine-sweep kick
 *
 * INTENSITY (0..1) brings the layers in: bass and arp are always there, the
 * lead comes in above 0.35, the drums above 0.15 (hats) and 0.5 (snare). */

typedef enum { SCALE_MINOR, SCALE_MAJOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_HARMONIC, SCALE_MIXOLYDIAN, SCALE_COUNT } MusicScale;

typedef struct {
    const char *slug;   /* "blockfall", "ghostarcade" (the launcher), ... */
    int bpm;
    int rootMidi;       /* the key's tonic, e.g. 45 = A2 */
    MusicScale scale;
    int prog[4];        /* scale degree (0-based) of each bar's chord root */
    int bassStyle;      /* 0 steady eighths, 1 offbeat, 2 octave jumps, 3 driving sixteenths */
    int arpStyle;       /* 0 up, 1 up-down, 2 skip, 3 broken */
    int drumStyle;      /* 0 four on the floor, 1 backbeat, 2 halftime, 3 breakbeat */
    int leadSeed;
    int leadOctave;     /* octaves above the root the lead sits */
    float swing;        /* 0..0.3 delays the off sixteenths */
} MusicTheme;

#define SYNTH_STEPS 64  /* four bars of sixteenths */

typedef struct {
    const MusicTheme *theme;
    int sampleRate;
    int8_t bass[SYNTH_STEPS], arp[SYNTH_STEPS], lead[SYNTH_STEPS]; /* midi note, or -1 for none */
    uint8_t drums[SYNTH_STEPS]; /* bit 0 kick, 1 snare, 2 hat */

    float intensity;
    float gain;
    float muffle;       /* 0 clear .. 1 dull: a one-pole low-pass, for pause */

    int step;           /* 0..SYNTH_STEPS-1 */
    double samplesIntoStep;
    double samplesPerStep;

    /* voice state */
    double phBass, phArp, phLead, phKick;
    float freqBass, freqArp, freqLead;
    float envBass, envArp, envLead, envKick, envSnare, envHat;
    float vibPhase;
    uint32_t noise;
    float lp;
} Synth;

const MusicTheme *Music_FindTheme(const char *slug);
int Music_ThemeCount(void);
const MusicTheme *Music_ThemeAt(int i);

void Synth_Init(Synth *s, const MusicTheme *theme, int sampleRate);
void Synth_SetIntensity(Synth *s, float intensity);
/* Renders mono 16-bit samples. */
void Synth_Render(Synth *s, int16_t *out, int frames);
/* Seconds of one full loop (four bars). */
float Synth_LoopSeconds(const MusicTheme *theme);
/* MIDI note -> Hz. */
float Synth_NoteHz(int midi);

#endif
