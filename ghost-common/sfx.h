#ifndef SFX_H
#define SFX_H

#include <stdbool.h>

/* Sound effects for a Ghost Arcade game. The game owns an enum of effect
 * ids and a matching table of file names (under assetsDir/sounds/); this
 * module owns the audio device. Everything is a silent no-op if the device
 * fails to open or a file is missing -- sound is never worth a crash.
 *
 * Effects can overlap themselves (a few voices per sound), can be varied a
 * little each time so a rapid repeat doesn't machine-gun, and the module
 * carries a set of built-in arcade jingles every game shares. */
#define SFX_MAX 32

void Sfx_Init(const char *assetsDir, const char *const *fileNames, int count);
void Sfx_Shutdown(void);

void Sfx_SetEnabled(bool enabled);
void Sfx_SetVolume(int percent0to100);

void Sfx_Play(int id);
/* Same effect at a different pitch (1.0 = as recorded), e.g. a rising
 * chain of hits from one file. */
void Sfx_PlayPitched(int id, float pitch);
/* Plays with a random pitch within +-variance (e.g. 0.06) so repeats differ. */
void Sfx_PlayVaried(int id, float variance);

typedef enum {
    JINGLE_READY, JINGLE_GO, JINGLE_GAME_OVER, JINGLE_NEW_BEST, JINGLE_LEVEL_UP,
    JINGLE_COIN, JINGLE_CONFIRM, JINGLE_BACK, JINGLE_WARNING, JINGLE_COUNT
} SfxJingle;
void Sfx_Jingle(SfxJingle j);

#endif
