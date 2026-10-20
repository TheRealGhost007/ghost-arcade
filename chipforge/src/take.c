/* The take: notes played along with a loop, kept as played and caught onto
 * the pattern's grid when a pass is written. See take.h for the rules; the
 * reasons for them are here. */
#include "take.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/* A cell the take has not written: the pattern's own value stays. Below
 * NOTE_HOLD and NOTE_NONE, so it can never be mistaken for either. */
#define TAKE_UNTOUCHED (-100)

void Take_Init(Take *t) {
    t->count = 0;
    t->nextId = 0;
}

int Take_NoteOn(Take *t, int track, int note, double atStep) {
    if (track < TRACK_BASS || track > TRACK_DRUMS) return -1;
    if (t->count >= TAKE_MAX_EVENTS) return -1;
    TakeEvent *e = &t->ev[t->count++];
    e->track = track;
    e->note = note;
    e->on = atStep;
    e->off = atStep;
    e->held = true;
    e->id = t->nextId;
    /* Wraps rather than overflowing (and never to -1, the refusal); two
     * billion presses apart, a clash is not a worry. */
    t->nextId = t->nextId == INT_MAX ? 0 : t->nextId + 1;
    return e->id;
}

/* A release can only end a note after it began: the clock may be nudged back
 * a millisecond between frames, and a note must not end before it starts. */
static void take_Close(TakeEvent *e, double atStep) {
    e->off = atStep > e->on ? atStep : e->on;
    e->held = false;
}

void Take_NoteOffId(Take *t, int id, double atStep) {
    if (id < 0) return;
    for (int i = t->count - 1; i >= 0; i--) {
        TakeEvent *e = &t->ev[i];
        if (e->id != id) continue;
        if (e->held) take_Close(e, atStep);
        return;
    }
}

void Take_NoteOff(Take *t, int track, int note, double atStep) {
    for (int i = t->count - 1; i >= 0; i--) {
        TakeEvent *e = &t->ev[i];
        if (e->track == track && e->note == note && e->held) {
            take_Close(e, atStep);
            return;
        }
    }
}

void Take_CloseAll(Take *t, double atStep) {
    for (int i = 0; i < t->count; i++)
        if (t->ev[i].held) take_Close(&t->ev[i], atStep);
}

static int take_Grid(int grid) {
    return (grid == 2 || grid == 4) ? grid : 1;
}

int Take_Quantise(double atStep, int grid) {
    grid = take_Grid(grid);
    /* floor(x + 0.5) rounds halves up, which is "forward" in time, for
     * negative times (the count-in) as well. */
    return (int)floor(atStep / (double)grid + 0.5) * grid;
}

static bool take_InRange(const TakeCommit *c, int caught) {
    return (double)caught >= c->from && (double)caught < c->to;
}

static int take_Step(int caught) {
    return ((caught % SONG_STEPS) + SONG_STEPS) % SONG_STEPS;
}

/* The track's events in [from, to), oldest first. Presses arrive in time
 * order, so this insertion sort is a single pass in practice; it is only
 * there because the clock can be nudged back a hair between frames. Ties
 * keep the order they were played in. */
static int take_Collect(const Take *t, const TakeCommit *c, int track, int *idx) {
    int n = 0;
    int grid = take_Grid(c->grid);
    for (int i = 0; i < t->count; i++) {
        const TakeEvent *e = &t->ev[i];
        if (e->track != track || !take_InRange(c, Take_Quantise(e->on, grid))) continue;
        int j = n++;
        while (j > 0 && t->ev[idx[j - 1]].on > e->on) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = i;
    }
    return n;
}

/* Whole steps a note sounds: its held length rounded, so a key let go a
 * moment early still keeps the step it nearly filled, and at least the step
 * it starts on. Measured from the press, not from the caught step, so a note
 * caught a little earlier or later keeps the length it was played with. */
static int take_Length(const TakeEvent *e) {
    if (e->held) return SONG_STEPS; /* still held: to the end of the pass */
    int len = (int)floor(e->off - e->on + 0.5);
    return len < 1 ? 1 : len;
}

static int take_ClampNote(int note) {
    return note < NOTE_MIN ? NOTE_MIN : (note > NOTE_MAX ? NOTE_MAX : note);
}

int Take_Apply(const Take *t, const TakeCommit *c, SongPattern *p) {
    int idx[TAKE_MAX_EVENTS];
    int written = 0;
    int grid = take_Grid(c->grid);
    for (int track = TRACK_BASS; track <= TRACK_DRUMS; track++) {
        if (c->onlyTrack >= 0 && track != c->onlyTrack) continue;
        int n = take_Collect(t, c, track, idx);
        if (c->mode == TAKE_REPLACE && (n > 0 || c->onlyTrack == track)) {
            if (track == TRACK_DRUMS) memset(p->drums, 0, sizeof(p->drums));
            else memset(p->tone[track], NOTE_NONE, sizeof(p->tone[track]));
        }
        if (n == 0) continue;

        if (track == TRACK_DRUMS) {
            /* A pad press adds a hit. Toggling (the editor's way) would let a
             * second pass over the same beat delete the first pass's kick. */
            for (int i = 0; i < n; i++) {
                const TakeEvent *e = &t->ev[idx[i]];
                int st = take_Step(Take_Quantise(e->on, grid));
                p->drums[st] |= (uint8_t)(e->note & (DRUM_KICK | DRUM_SNARE | DRUM_HAT));
                written++;
            }
            continue;
        }

        /* Built in a scratch row first, so a later note can take back the
         * holds an earlier one wrote without disturbing the pattern's own
         * cells under them. */
        int cell[SONG_STEPS];
        for (int st = 0; st < SONG_STEPS; st++) cell[st] = TAKE_UNTOUCHED;
        for (int i = 0; i < n; i++) {
            const TakeEvent *e = &t->ev[idx[i]];
            int st = take_Step(Take_Quantise(e->on, grid));
            /* Events come in time order, so every hold already past this
             * step belongs to an earlier note, which this one cuts off: a
             * tone track sounds one note at a time. */
            for (int k = st + 1; k < SONG_STEPS; k++)
                if (cell[k] == NOTE_HOLD) cell[k] = TAKE_UNTOUCHED;
            cell[st] = take_ClampNote(e->note);
            int last = st + take_Length(e) - 1;
            if (last > SONG_STEPS - 1) last = SONG_STEPS - 1; /* never wraps into step 0 */
            for (int k = st + 1; k <= last; k++) cell[k] = NOTE_HOLD;
        }
        /* A note ends where it was played. A hold sounds whatever note is
         * sounding, so in MERGE the pattern's own holds after a written note
         * (the tail of an older long note, which the new one has cut anyway:
         * one note at a time) would ring the NEW note on to their end. The
         * whole run goes, not just its first cell: a hold after a rest stops
         * the dying note's decay and keeps it sounding. */
        for (int st = 0; st < SONG_STEPS - 1; st++) {
            if (cell[st] == TAKE_UNTOUCHED || cell[st + 1] != TAKE_UNTOUCHED) continue;
            for (int k = st + 1; k < SONG_STEPS && cell[k] == TAKE_UNTOUCHED && p->tone[track][k] == NOTE_HOLD; k++)
                cell[k] = NOTE_NONE;
        }
        for (int st = 0; st < SONG_STEPS; st++) {
            if (cell[st] == TAKE_UNTOUCHED) continue;
            p->tone[track][st] = (int8_t)cell[st];
            written++;
        }
    }
    return written;
}

int Take_NotesIn(const Take *t, const TakeCommit *c) {
    bool hit[SONG_TRACKS][SONG_STEPS];
    memset(hit, 0, sizeof(hit));
    int grid = take_Grid(c->grid), notes = 0;
    for (int i = 0; i < t->count; i++) {
        const TakeEvent *e = &t->ev[i];
        if (e->track < TRACK_BASS || e->track > TRACK_DRUMS) continue;
        if (c->onlyTrack >= 0 && e->track != c->onlyTrack) continue;
        int caught = Take_Quantise(e->on, grid);
        if (!take_InRange(c, caught)) continue;
        int st = take_Step(caught);
        if (!hit[e->track][st]) { hit[e->track][st] = true; notes++; }
    }
    return notes;
}

void Take_Forget(Take *t, int grid, int before) {
    int kept = 0;
    for (int i = 0; i < t->count; i++) {
        if (Take_Quantise(t->ev[i].on, grid) < before) continue;
        if (kept != i) t->ev[kept] = t->ev[i];
        kept++;
    }
    t->count = kept;
}
