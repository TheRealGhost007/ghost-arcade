#ifndef TAKE_H
#define TAKE_H

#include <stdbool.h>
#include "song.h"

/* A take: the notes played along with a looping pattern, as they were
 * played (fractional step times), and the rules that catch them onto the
 * pattern's grid. No raylib, no clock: jam.c turns key presses into step
 * times and decides when a pass is over; this file only remembers and writes.
 *
 * Times are in steps since the loop's first downbeat (step 0 of the first
 * pass is 0.0, step 0 of the second is 64.0; the count-in is negative). A
 * note is caught to the nearest multiple of the catch grid (1 = sixteenths,
 * 2 = eighths, 4 = quarters), and the CAUGHT step decides which pass it
 * belongs to: a note played a hair before the loop comes round lands on
 * step 0 of the next pass, where it was meant to be. */

#define TAKE_MAX_EVENTS 1024

typedef struct {
    int track;          /* TRACK_BASS..TRACK_DRUMS */
    int note;           /* MIDI on a tone track, a DRUM_ bit set on TRACK_DRUMS */
    double on, off;     /* steps since the first downbeat; off is the release once held is false */
    /* The key is still down. A flag, not a negative off: the count-in's times
     * are negative, so a pickup let go before the first downbeat has a
     * negative release and is still a released note. */
    bool held;
    int id;             /* the press, as Take_NoteOn returned it */
} TakeEvent;

typedef struct {
    TakeEvent ev[TAKE_MAX_EVENTS];
    int count;
    int nextId;         /* the next press's id; counts on through Take_Forget */
} Take;

void Take_Init(Take *t);
/* Returns the press's id, or -1 when the take is full (or the track is not
 * one of the four). Ids count up from 0 after Take_Init and are not reused:
 * an index moves when Take_Forget drops a pass, an id never does. */
int Take_NoteOn(Take *t, int track, int note, double atStep);
/* Ends the press with that id if it is still held; nothing otherwise (-1, a
 * press already ended, or one whose pass was written and forgotten). This is
 * how a key's release finds its own press: two keys can give one pitch (an
 * octave change mid-hold, or the top octave's clamp). */
void Take_NoteOffId(Take *t, int id, double atStep);
/* Ends the latest still-held event with this track and note; nothing when
 * there is none (its pass may already have been written and forgotten). With
 * two presses of one pitch held it cannot tell them apart: Take_NoteOffId can. */
void Take_NoteOff(Take *t, int track, int note, double atStep);
/* Ends every held event, as when recording stops or the track changes. */
void Take_CloseAll(Take *t, double atStep);

/* Nearest multiple of grid (1, 2 or 4; anything else counts as 1); ties go
 * forward, so 4.5 on grid 1 is 5. 63.6 on grid 1 is 64, step 0 of the next
 * pass. */
int Take_Quantise(double atStep, int grid);

typedef enum { TAKE_MERGE, TAKE_REPLACE } TakeMode;

typedef struct {
    int grid;           /* the catch grid, 1, 2 or 4 */
    TakeMode mode;      /* MERGE writes over what is there; REPLACE clears the track first */
    double from, to;    /* caught steps in [from, to) are written; normally one pass, 64 wide */
    int onlyTrack;      /* -1 writes every track, else just that one */
} TakeCommit;

/* Writes the events whose caught step lies in [from, to) into the pattern at
 * that step mod 64:
 *  - a tone note sounds for its held length rounded to whole steps: NOTE_HOLD
 *    on each further step, never past step 63 and never wrapping to step 0
 *    (a note still held is held to the end of the pass);
 *  - a later note on the same track cuts the earlier one's holds from its own
 *    step on, and on the same step it wins (a tone track sounds one note);
 *  - a written note ends where it was played: a run of the pattern's own
 *    NOTE_HOLDs straight after it (an older note's tail, which would sound
 *    the new note) becomes NOTE_NONE;
 *  - drum hits OR their bits in and never toggle a hit off;
 *  - MERGE leaves every cell the take does not write as it was; REPLACE
 *    first clears the tracks being written (onlyTrack, or each track the take
 *    has a note for), and only those.
 * Nothing outside those tracks is touched. Returns the cells written (notes,
 * holds, the old holds ended and drum steps). */
int Take_Apply(const Take *t, const TakeCommit *c, SongPattern *p);
/* How many notes Take_Apply would write: distinct (track, step) onsets, so
 * two presses caught onto one step count once. */
int Take_NotesIn(const Take *t, const TakeCommit *c);
/* Forgets every event caught (on this grid) before step `before`: a pass
 * that has been written, or the count-in. Later events keep their order. */
void Take_Forget(Take *t, int grid, int before);

#endif
