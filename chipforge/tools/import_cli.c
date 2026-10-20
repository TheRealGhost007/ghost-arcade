/* Headless importer: the same decode + transcribe path the app uses, from the
 * command line, so a recording can be turned into a .gsong (and a preview WAV)
 * without a window. Used to test the importer on real files and by anyone who
 * prefers a terminal.
 *
 *   chipforge-import <recording> [--out song.gsong] [--wav preview.wav]
 *                    [--bpm N] [--no-drums] [--no-arp] [--name "Title"]
 *
 * Build: make import-cli (links raylib for the decoding only). */
#include "raylib.h"
#include "song.h"
#include "musicsynth.h"
#include "transcribe.h"
#include "browser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float *DecodeMono(const char *path, int *framesOut, int *rateOut) {
    Wave w = LoadWave(path);
    if (!IsWaveValid(w) || w.frameCount == 0 || w.channels == 0) { UnloadWave(w); return NULL; }
    float *s = LoadWaveSamples(w);
    if (!s) { UnloadWave(w); return NULL; }
    float *mono = malloc(sizeof(float) * w.frameCount);
    if (mono) {
        unsigned int ch = w.channels;
        for (unsigned int i = 0; i < w.frameCount; i++) {
            float acc = 0.0f;
            for (unsigned int c = 0; c < ch; c++) acc += s[i * ch + c];
            mono[i] = acc / (float)ch;
        }
        *framesOut = (int)w.frameCount;
        *rateOut = (int)w.sampleRate;
    }
    UnloadWaveSamples(s);
    UnloadWave(w);
    return mono;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: chipforge-import <recording> [--out x.gsong] [--wav x.wav] [--bpm N] [--no-drums] [--no-arp] [--name T]\n"); return 2; }
    const char *in = argv[1], *out = NULL, *wav = NULL, *name = NULL;
    TranscribeOptions opt;
    Transcribe_DefaultOptions(&opt);
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) wav = argv[++i];
        else if (strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) opt.bpm = atoi(argv[++i]);
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) name = argv[++i];
        else if (strcmp(argv[i], "--no-drums") == 0) opt.noDrums = true;
        else if (strcmp(argv[i], "--no-arp") == 0) opt.noArp = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    SetTraceLogLevel(LOG_WARNING);
    int frames = 0, rate = 0;
    float *mono = DecodeMono(in, &frames, &rate);
    if (!mono) { fprintf(stderr, "could not decode %s\n", in); return 1; }

    static Song song;
    TranscribeReport rep;
    bool ok = Transcribe_Run(mono, frames, rate, &opt, &song, &rep);
    free(mono);
    if (!ok) { fprintf(stderr, "too little audio in %s\n", in); return 1; }
    char title[SONG_NAME_LEN];
    if (name) snprintf(title, sizeof(title), "%s", name);
    else Browser_TitleFromPath(in, title, sizeof(title));
    snprintf(song.name, sizeof(song.name), "%s", title);

    printf("%s\n  %.1f s at %d Hz -> bpm %d (confidence %.2f), %d steps, %d patterns%s\n  lead %d  bass %d  kicks %d  snares %d  hats %d\n",
           in, (double)rep.seconds, rate, rep.bpm, (double)rep.bpmConfidence, rep.steps, rep.patterns,
           rep.truncated ? " (truncated)" : "", rep.leadNotes, rep.bassNotes, rep.kicks, rep.snares, rep.hats);
    if (out) printf("  song: %s %s\n", out, Song_Save(&song, out) ? "written" : "FAILED");
    if (wav) printf("  wav:  %s %s\n", wav, Synth_RenderWavFile(&song, wav, 44100, 0.0f) ? "written" : "FAILED");
    return 0;
}
