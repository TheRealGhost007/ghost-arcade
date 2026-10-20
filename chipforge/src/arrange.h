#ifndef ARRANGE_H
#define ARRANGE_H

#include <stdbool.h>
#include <stdint.h>
#include "song.h"
#include "compose.h"

/* The arranger behind Grow (F9) and Vary (Ctrl+R): it turns one four-bar
 * pattern into the parts a song is made of (a fill, an answering phrase, a
 * chorus on other chords, a lift, a breakdown, a turnaround, an intro and an
 * outro) and lays them out as a game loop of about a minute. An 8-second
 * loop played for a whole level wears thin; the same idea heard as verse,
 * chorus, lift and breakdown does not, and nobody has to write the parts.
 *
 * Everything is built from the source pattern with compose.h's writers and
 * key tools, so every part stays in the source's key and register. No
 * raylib: main.c and the picker only call in, and the suite tests it all
 * headless.
 *
 * Games loop the order from position 0, so the loop shapes start on the
 * source and end on a turnaround that leads back to it; only "Song with an
 * ending" (meant for a WAV) opens with an intro and closes on an outro. */

typedef enum {
    VARY_FILL,        /* a drum fill and a run into the next pattern in the last bar */
    VARY_ANSWER,      /* bars 3-4 of the tune echo bars 1-2 on their own chords and end on the root */
    VARY_NEW_TUNE,    /* the same backing with a new lead */
    VARY_LIFT,        /* a chorus lift: driving bass, busier or higher arp, doubled hats, the tune higher */
    VARY_DROP,        /* a breakdown: half-time drums, long bass notes, a sparse held tune */
    VARY_INTRO,       /* no tune and no snare, the beat coming in for the second half */
    VARY_TURNAROUND,  /* the last bar on V (or I), with a fill leading back to the first chord */
    VARY_OUTRO,       /* the last bar on the tonic, every voice held */
    VARY_COUNT
} VaryKind;

/* The Vary picker offers the first five kinds; the last three exist for
 * Grow, which knows where in the song they go. */
#define ARRANGE_PICKER_KINDS 5

/* Sentence case, at most 15 glyphs ("Fill at the end"); "" out of range. */
const char *Arrange_VaryName(VaryKind v);

/* Writes a variation of src into out (which may be src itself). k is the
 * song's key; tonicMidi the register src sits in (Compose_TonicMidi), or 0
 * or less to work it out from src; c the chords src is on, or NULL to read
 * them (Compose_ReadChords). The seed picks between equally good takes
 * where a kind has a choice to make (the fill's roll, the lift's arp, the
 * new tune). Deterministic. A fill or turnaround leads into c's first chord,
 * which is right when the next pattern starts on the same chord as src. */
void Arrange_Vary(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *c, VaryKind kind, uint32_t seed, SongPattern *out);
/* The same, with the degree (0..6) of the first chord of whatever plays
 * next, so a fill or turnaround runs into that chord instead; nextDegree < 0
 * means c's first chord. The other kinds ignore it. */
void Arrange_VaryInto(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *c, VaryKind kind, uint32_t seed, int nextDegree, SongPattern *out);

/* A chorus: src moved onto other chords (Compose_Reharmonise without
 * fitting the tune, so bass and arp keep their rhythm exactly) with a new
 * tune on those chords (Compose_Lead from the seed, at the register src's
 * tune sits in), always a different tune from src's. from NULL reads src's
 * chords; to NULL uses Arrange_AnswerProgression's. tonicMidi as for
 * Arrange_Vary. */
void Arrange_Chorus(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *from, const ComposeChords *to, uint32_t seed, SongPattern *out);
/* The index (Compose_ProgressionAt) of a chorus partner for a verse on
 * these chords: the first named progression that opens on IV or VI and is
 * not the verse's own, else the first that opens on a different chord and
 * changes at least three bars. Always a different progression from c.
 * NULL reads as i i i i. */
int Arrange_AnswerProgression(const ComposeChords *c);

typedef enum { GROW_SHORT, GROW_GAME_LOOP, GROW_LONG, GROW_WITH_ENDING, GROW_COUNT } GrowShape;

/* "Game loop, about 1 min": sentence case, at most 30 glyphs; "" out of range. */
const char *Arrange_ShapeName(GrowShape g);
/* How long the shape aims to be at 120 bpm: 30, 60, 120, 75 (0 out of
 * range). The pass list is fixed, so a faster song is shorter. */
float Arrange_TargetSeconds(GrowShape g);
/* How many patterns Arrange_Grow adds after the existing ones: 2, 6, 6, 7
 * (0 out of range). */
int Arrange_PatternsNeeded(GrowShape g);
/* How many order entries the grown song has: 4, 8, 16, 9 (0 out of range). */
int Arrange_PassCount(GrowShape g);

/* Grows the song from one pattern: adds Arrange_PatternsNeeded(shape)
 * patterns after the existing ones and rewrites the whole order to the
 * shape's pass list, which starts on sourcePattern (after the intro, for
 * Song with an ending) and plays it at least twice:
 *   Short loop           A  Bf A  T
 *   Game loop            A  Af Bf A  A' B  B' T
 *   Long loop            A  Af Bf A  A' B  B' A  Af B  Bf A' A' B  Bf T
 *   Song with an ending  I  A  Af Bf A' B  B' A  O
 * A is the source, unchanged. B is its chorus on the answering progression
 * (Arrange_Chorus), A' a lift of A, B' a drop of B, T a turnaround from A,
 * I and O an intro and outro from A; an f marks a fill into the part after
 * it. Existing patterns, the name, tempo, voices and key are left alone.
 * Returns false and leaves the song byte for byte untouched when the shape
 * would need more than SONG_MAX_PATTERNS patterns or SONG_MAX_ORDER entries,
 * or for a bad song, source or shape. Writes the patterns, then patternCount,
 * then the order, then orderCount, so a player reading the song meanwhile
 * never follows an entry to a pattern that is not there yet; order entries
 * past the new end are then zeroed, as a parsed song has them. */
bool Arrange_Grow(Song *s, int sourcePattern, ComposeKey k, GrowShape shape, uint32_t seed);

#endif
