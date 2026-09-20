#include "music.h"
#include "musicsynth.h"
#include "raylib.h"
#include <string.h>

#define RATE 44100

static AudioStream sStream;
static bool sReady = false;
static Synth sSynth;
static bool sEnabled = true;
static float sVolume = 0.7f;

/* Written by the game thread, read by the audio thread. Torn reads of a float
 * here at worst give one buffer of a slightly odd level, which nobody hears. */
static volatile float sTargetIntensity = 0.6f;
static volatile float sTargetGain = 0.0f;
static volatile float sTargetMuffle = 0.0f;
static volatile const MusicTheme *sPendingTheme = NULL;

static float sCurGain = 0.0f, sCurMuffle = 0.0f, sCurIntensity = 0.6f;
static bool sFadingOut = false;

static void Fill(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    unsigned int done = 0;
    while (done < frames) {
        unsigned int chunk = frames - done > 256 ? 256 : frames - done;
        /* ease the levels toward their targets a little every chunk */
        float k = 0.02f;
        float wantGain = sTargetGain;
        const MusicTheme *pending = (const MusicTheme *)sPendingTheme;
        if (pending && pending != sSynth.theme) { wantGain = 0.0f; sFadingOut = true; }
        sCurGain += (wantGain - sCurGain) * k;
        sCurMuffle += (sTargetMuffle - sCurMuffle) * k;
        sCurIntensity += (sTargetIntensity - sCurIntensity) * k;
        if (sFadingOut && sCurGain < 0.02f && pending) {
            Synth_Init(&sSynth, pending, RATE);
            sPendingTheme = NULL;
            sFadingOut = false;
        }
        sSynth.gain = sCurGain;
        sSynth.muffle = sCurMuffle;
        Synth_SetIntensity(&sSynth, sCurIntensity);
        Synth_Render(&sSynth, out + done, (int)chunk);
        done += chunk;
    }
}

void Music_Init(const char *themeSlug) {
    if (!IsAudioDeviceReady()) return;
    Synth_Init(&sSynth, Music_FindTheme(themeSlug), RATE);
    SetAudioStreamBufferSizeDefault(2048);
    sStream = LoadAudioStream(RATE, 16, 1);
    if (!IsAudioStreamValid(sStream)) return;
    SetAudioStreamCallback(sStream, Fill);
    PlayAudioStream(sStream);
    sReady = true;
}

void Music_Shutdown(void) {
    if (!sReady) return;
    StopAudioStream(sStream);
    UnloadAudioStream(sStream);
    sReady = false;
}

void Music_SetEnabled(bool enabled) { sEnabled = enabled; if (!enabled) sTargetGain = 0.0f; }

void Music_SetVolume(int percent0to100) {
    if (percent0to100 < 0) percent0to100 = 0;
    if (percent0to100 > 100) percent0to100 = 100;
    sVolume = (float)percent0to100 / 100.0f;
}

static float BaseGain(void) { return sEnabled ? sVolume * 0.55f : 0.0f; }

void Music_SetMode(MusicMode mode) {
    switch (mode) {
        case MUSIC_MENU: sTargetGain = BaseGain() * 0.8f; sTargetMuffle = 0.0f; sTargetIntensity = 0.3f; break;
        case MUSIC_PLAY: sTargetGain = BaseGain(); sTargetMuffle = 0.0f; break;
        case MUSIC_PAUSED: sTargetGain = BaseGain() * 0.45f; sTargetMuffle = 1.0f; break;
        case MUSIC_SILENT: sTargetGain = 0.0f; break;
    }
}

void Music_SetIntensity(float intensity) { sTargetIntensity = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity); }

void Music_SetTheme(const char *themeSlug) { sPendingTheme = Music_FindTheme(themeSlug); }
