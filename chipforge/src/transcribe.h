#ifndef TRANSCRIBE_H
#define TRANSCRIBE_H

#include <stdbool.h>
#include "song.h"

/* Turns a recording into a Ghost Arcade chiptune. No raylib: the caller
 * decodes the file (raylib's LoadWave does WAV/OGG/MP3/QOA) and hands over
 * mono float samples; this listens for the beat, follows it through the whole
 * track (so a tempo that drifts still lands on the grid), then for every
 * sixteenth step asks what the bass is doing, what the melody is doing, what
 * chord the bar is on, and where the kicks, snares and hats fall. The answer is
 * an ordinary Song the editor can take apart.
 *
 * It is a cover version by a very literal-minded chip, not a transcription a
 * musician would sign: expect the melody in the right place and the right
 * key, and expect to tidy it up. */

typedef struct {
    int bpm;          /* 0 = detect; otherwise the beat grid is forced to this tempo */
    int maxPatterns;  /* 0 = SONG_MAX_PATTERNS */
    bool noDrums;     /* skip the drum tracks */
    bool noArp;       /* skip the chord arpeggio */
} TranscribeOptions;

typedef struct {
    int bpm;              /* what the song was set to */
    float bpmConfidence;  /* 0..1, how clearly the beat stood out */
    float seconds;        /* of audio analysed */
    int steps;            /* sixteenths on the grid */
    int patterns;
    int leadNotes, bassNotes, kicks, snares, hats;
    bool truncated;       /* ran out of patterns before the end */
} TranscribeReport;

void Transcribe_DefaultOptions(TranscribeOptions *o);

/* mono: samples in -1..1. Returns false if there is too little audio to work
 * with (under about a second). out is always left as a valid song. */
bool Transcribe_Run(const float *mono, int frames, int sampleRate, const TranscribeOptions *opt, Song *out, TranscribeReport *rep);

/* Pieces exposed for the tests. */
/* Beat period estimate from an onset-strength envelope sampled at `fps`
 * frames per second; returns bpm (0 if nothing periodic) and the confidence. */
int Transcribe_EstimateBpm(const float *onset, int frames, float fps, float *confidence);
/* The harmonic-summation pitch salience of one magnitude spectrum for a MIDI
 * note (binHz = sampleRate / fftSize). */
float Transcribe_Salience(const float *mag, int bins, float binHz, int midi);

#endif
