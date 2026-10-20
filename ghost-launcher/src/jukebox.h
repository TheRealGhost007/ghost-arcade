#ifndef JUKEBOX_H
#define JUKEBOX_H

#include <stdbool.h>
#include <stdint.h>
#include "song.h"

/* Chipforge drawn as a jukebox: its screen scrolls the notes of the song
 * the hall is playing, at that song's tempo. This is the raylib-free half:
 * where the song is after so many seconds, and the notes in a window of
 * steps around that point, a held note carried in from before the window
 * and the song going round from its end to its start. render.c draws the
 * window; hallmusic.h says which song plays. */

#define JUKE_STEPS 32 /* steps a screen shows */
#define JUKE_LEAD 8   /* of them, before the playhead */

typedef struct {
    int note;   /* MIDI, or NOTE_NONE */
    bool start; /* struck on this step; false while a held note rings on */
} JukeNote;

typedef struct {
    double playhead;  /* steps from the song's start, with the part of a step */
    int first;        /* the song's step shown in column 0 */
    JukeNote tone[SONG_TONE_TRACKS][JUKE_STEPS];
    uint8_t drums[JUKE_STEPS];
    int lo[SONG_TONE_TRACKS], hi[SONG_TONE_TRACKS]; /* each track's notes in the window, NOTE_NONE for none */
    bool any;         /* a note or a drum somewhere in the window */
} JukeWindow;

/* Steps in one pass through the song's order (at least 1). */
int Jukebox_Length(const Song *s);
/* Steps since the song began after `seconds`, round its length. */
double Jukebox_Playhead(const Song *s, double seconds);
/* What a track plays at a step of the song (any step: it goes round): a
 * note struck there, a held one still ringing, or NOTE_NONE. */
JukeNote Jukebox_NoteAt(const Song *s, int track, int step);
/* The window: JUKE_LEAD steps before the playhead, the rest after. */
void Jukebox_Window(const Song *s, double seconds, JukeWindow *w);

#endif
