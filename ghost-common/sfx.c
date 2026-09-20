#include "sfx.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>

#define VOICES 3
#define JINGLE_RATE 44100

static Sound sSounds[SFX_MAX];
static Sound sAlias[SFX_MAX][VOICES - 1];
static int sNext[SFX_MAX];
static bool sLoaded[SFX_MAX];
static int sCount = 0;
static Sound sJingles[JINGLE_COUNT];
static bool sJingleLoaded[JINGLE_COUNT];
static bool sDeviceReady = false;
static bool sEnabled = true;
static float sVolume = 0.7f;
static unsigned sVaryState = 0x9E3779B9u;

/* ------------------------------------------------------------- jingles */

typedef struct { float hz, seconds; } Note;

/* A little square-wave melody with a per-note envelope, as a Wave. */
static Sound MakeJingle(const Note *notes, int n, float amp, float duty) {
    int total = 0;
    for (int i = 0; i < n; i++) total += (int)(notes[i].seconds * JINGLE_RATE);
    short *data = (short *)MemAlloc((unsigned)(total * (int)sizeof(short)));
    int pos = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)(notes[i].seconds * JINGLE_RATE);
        double phase = 0.0;
        for (int k = 0; k < len; k++) {
            float t = (float)k / (float)len;
            float env = t < 0.04f ? t / 0.04f : (t > 0.7f ? (1.0f - t) / 0.3f : 1.0f);
            phase += notes[i].hz / JINGLE_RATE;
            float v = notes[i].hz <= 0.0f ? 0.0f : ((phase - floor(phase)) < duty ? 1.0f : -1.0f);
            data[pos++] = (short)(v * amp * env * 32000.0f);
        }
    }
    Wave w = {(unsigned)total, JINGLE_RATE, 16, 1, data};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static void BuildJingles(void) {
    static const Note kReady[] = {{392, 0.09f}, {523, 0.09f}, {659, 0.09f}, {784, 0.24f}};
    static const Note kGo[] = {{1046, 0.06f}, {1568, 0.22f}};
    static const Note kOver[] = {{392, 0.17f}, {330, 0.17f}, {262, 0.17f}, {196, 0.5f}};
    static const Note kBest[] = {{523, 0.07f}, {659, 0.07f}, {784, 0.07f}, {1046, 0.07f}, {1318, 0.07f}, {1568, 0.3f}};
    static const Note kLevel[] = {{523, 0.08f}, {659, 0.08f}, {784, 0.08f}, {1046, 0.22f}};
    static const Note kCoin[] = {{988, 0.07f}, {1318, 0.32f}};
    static const Note kConfirm[] = {{660, 0.05f}, {880, 0.08f}};
    static const Note kBack[] = {{660, 0.05f}, {440, 0.08f}};
    static const Note kWarn[] = {{880, 0.07f}, {0, 0.04f}, {880, 0.07f}, {0, 0.04f}, {880, 0.07f}};
    struct { const Note *n; int c; float amp, duty; } defs[JINGLE_COUNT] = {
        {kReady, 4, 0.20f, 0.5f}, {kGo, 2, 0.20f, 0.5f}, {kOver, 4, 0.20f, 0.5f}, {kBest, 6, 0.18f, 0.25f},
        {kLevel, 4, 0.20f, 0.5f}, {kCoin, 2, 0.20f, 0.5f}, {kConfirm, 2, 0.16f, 0.5f}, {kBack, 2, 0.16f, 0.5f}, {kWarn, 5, 0.16f, 0.5f},
    };
    for (int i = 0; i < JINGLE_COUNT; i++) {
        sJingles[i] = MakeJingle(defs[i].n, defs[i].c, defs[i].amp, defs[i].duty);
        sJingleLoaded[i] = true;
        SetSoundVolume(sJingles[i], sVolume);
    }
}

/* ----------------------------------------------------------------- core */

void Sfx_Init(const char *assetsDir, const char *const *fileNames, int count) {
    InitAudioDevice();
    sDeviceReady = IsAudioDeviceReady();
    if (!sDeviceReady) return;

    sCount = count > SFX_MAX ? SFX_MAX : count;
    char path[512];
    for (int i = 0; i < sCount; i++) {
        snprintf(path, sizeof(path), "%s/sounds/%s", assetsDir, fileNames[i]);
        if (FileExists(path)) {
            sSounds[i] = LoadSound(path);
            for (int v = 0; v < VOICES - 1; v++) sAlias[i][v] = LoadSoundAlias(sSounds[i]);
            sLoaded[i] = true;
            SetSoundVolume(sSounds[i], sVolume);
            for (int v = 0; v < VOICES - 1; v++) SetSoundVolume(sAlias[i][v], sVolume);
        }
    }
    BuildJingles();
}

void Sfx_Shutdown(void) {
    if (!sDeviceReady) return;
    for (int i = 0; i < sCount; i++) {
        if (!sLoaded[i]) continue;
        for (int v = 0; v < VOICES - 1; v++) UnloadSoundAlias(sAlias[i][v]);
        UnloadSound(sSounds[i]);
    }
    for (int i = 0; i < JINGLE_COUNT; i++) if (sJingleLoaded[i]) UnloadSound(sJingles[i]);
    CloseAudioDevice();
}

void Sfx_SetEnabled(bool enabled) {
    sEnabled = enabled;
}

void Sfx_SetVolume(int percent0to100) {
    if (percent0to100 < 0) percent0to100 = 0;
    if (percent0to100 > 100) percent0to100 = 100;
    sVolume = (float)percent0to100 / 100.0f;
    if (!sDeviceReady) return;
    for (int i = 0; i < sCount; i++) {
        if (!sLoaded[i]) continue;
        SetSoundVolume(sSounds[i], sVolume);
        for (int v = 0; v < VOICES - 1; v++) SetSoundVolume(sAlias[i][v], sVolume);
    }
    for (int i = 0; i < JINGLE_COUNT; i++) if (sJingleLoaded[i]) SetSoundVolume(sJingles[i], sVolume);
}

/* Round-robin over the sound and its aliases so a retrigger doesn't cut itself off. */
static Sound *NextVoice(int id) {
    int v = sNext[id];
    sNext[id] = (v + 1) % VOICES;
    return v == 0 ? &sSounds[id] : &sAlias[id][v - 1];
}

void Sfx_Play(int id) {
    if (!sEnabled || !sDeviceReady) return;
    if (id < 0 || id >= sCount || !sLoaded[id]) return;
    Sound *s = NextVoice(id);
    SetSoundPitch(*s, 1.0f);
    PlaySound(*s);
}

void Sfx_PlayPitched(int id, float pitch) {
    if (!sEnabled || !sDeviceReady) return;
    if (id < 0 || id >= sCount || !sLoaded[id]) return;
    Sound *s = NextVoice(id);
    SetSoundPitch(*s, pitch);
    PlaySound(*s);
}

void Sfx_PlayVaried(int id, float variance) {
    sVaryState = sVaryState * 1664525u + 1013904223u;
    float r = (float)((sVaryState >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    Sfx_PlayPitched(id, 1.0f + r * variance);
}

void Sfx_Jingle(SfxJingle j) {
    if (!sEnabled || !sDeviceReady || j < 0 || j >= JINGLE_COUNT || !sJingleLoaded[j]) return;
    PlaySound(sJingles[j]);
}
