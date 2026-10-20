#ifndef MUSICSYNTH_H
#define MUSICSYNTH_H

#include <stdbool.h>
#include <stdint.h>
#include "song.h"

/* The chiptune synthesiser behind ghost-common's music: no raylib, so it can
 * be rendered and tested headless. It plays a Song (song.h): patterns of 64
 * sixteenth steps with four tracks,
 *
 *   bass   a tone track, triangle by default, carries the root
 *   arp    a tone track, 25% pulse by default, chord tones in sixteenths
 *   lead   a tone track, 50% pulse by default with a little vibrato
 *   drums  noise hats and snares, a sine-sweep kick
 *
 * INTENSITY (0..1) brings the layers in: bass and arp are always there, the
 * lead comes in above 0.35, the drums above 0.15 (hats) and 0.5 (snare).
 *
 * A THEME is the older, smaller description every game ships with: a handful
 * of numbers (tempo, key, scale, a four-bar chord progression, style picks and
 * a seed) from which a one-pattern song is composed deterministically.
 * Music_ThemeToSong does that; Chipforge offers the themes as presets. */

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

#define SYNTH_STEPS SONG_STEPS

typedef struct {
    const Song *song;
    int sampleRate;

    float intensity;
    float gain;
    float muffle;       /* 0 clear .. 1 dull: a one-pole low-pass, for pause */

    /* position */
    int orderPos;       /* index into song->order */
    int step;           /* 0..SONG_STEPS-1 */
    int loopPattern;    /* -1 follows the order; otherwise that pattern repeats (the editor) */
    double samplesIntoStep;
    double samplesPerStep;
    /* The WAV export sets stopAtOrderEnd so one pass through the order is
     * followed by the last notes ringing out instead of the song starting
     * again; finished is set once that end has been reached. */
    bool stopAtOrderEnd;
    bool finished;

    /* voice state, tone tracks indexed by TRACK_BASS/ARP/LEAD */
    double ph[SONG_TONE_TRACKS];
    float freq[SONG_TONE_TRACKS];
    float env[SONG_TONE_TRACKS];
    bool held[SONG_TONE_TRACKS];
    uint32_t bassAge;   /* samples since the bass was last triggered */
    double phKick;
    float envKick, envSnare, envHat;
    double vibPhase;    /* kept in 0..1: a float that only grew lost the vibrato after minutes */
    uint32_t noise;
    float lp;

    /* set by the editor from the game thread, picked up by the next render */
    volatile int previewTrack;  /* -1 none */
    volatile int previewNote;
} Synth;

const MusicTheme *Music_FindTheme(const char *slug);
int Music_ThemeCount(void);
const MusicTheme *Music_ThemeAt(int i);
/* Composes the theme into a one-pattern song (deterministic). */
void Music_ThemeToSong(const MusicTheme *theme, Song *out);

/* The synth keeps the pointer: the song must outlive it (and may be edited
 * while it plays; a torn step is a glitch, never a crash). */
void Synth_Init(Synth *s, const Song *song, int sampleRate);
void Synth_SetIntensity(Synth *s, float intensity);
/* Jump to a position; the next rendered sample triggers that step. */
void Synth_SetPosition(Synth *s, int orderPos, int step);
/* pattern >= 0 loops that pattern (edit mode); -1 follows the order. */
void Synth_SetLoopPattern(Synth *s, int pattern);
/* Sounds one note on a tone track (or a drum bit set on TRACK_DRUMS) at the
 * next render, without moving the song. */
void Synth_Preview(Synth *s, int track, int note);
/* The pattern the synth is playing right now. */
int Synth_CurrentPattern(const Synth *s);
/* Renders mono 16-bit samples. */
void Synth_Render(Synth *s, int16_t *out, int frames);

/* Seconds of one full loop (four bars) of a theme. */
float Synth_LoopSeconds(const MusicTheme *theme);
/* MIDI note -> Hz. */
float Synth_NoteHz(int midi);

/* Renders one pass through the song's order (plus a short tail) at full
 * intensity to a 16-bit mono WAV. maxSeconds caps the length (0 = no cap). */
bool Synth_RenderWavFile(const Song *song, const char *path, int sampleRate, float maxSeconds);

#endif
