#ifndef MUSIC_H
#define MUSIC_H

#include <stdbool.h>

/* Background music for a Ghost Arcade game: the chiptune synth (see
 * musicsynth.h) streamed through raylib. The game names its theme by slug and
 * tells the module what state it is in; everything else (crossfades, the
 * muffled pause sound, layers coming in with intensity) is done here. Silent
 * no-op if the audio device is unavailable. Call after Sfx_Init, which opens
 * the device. */
typedef enum { MUSIC_MENU, MUSIC_PLAY, MUSIC_PAUSED, MUSIC_SILENT } MusicMode;

void Music_Init(const char *themeSlug);
void Music_Shutdown(void);

void Music_SetEnabled(bool enabled);
void Music_SetVolume(int percent0to100);

/* MENU: bass, arpeggio and a soft hat. PLAY: layers follow the intensity.
 * PAUSED: muffled and quiet. SILENT: fades out (game over; the stinger is
 * a sound effect). */
void Music_SetMode(MusicMode mode);
/* 0..1: how much is going on. Lead melody above 0.35, snare above 0.5. */
void Music_SetIntensity(float intensity);

/* Switch to another theme by slug (a crossfade). */
void Music_SetTheme(const char *themeSlug);

#endif
