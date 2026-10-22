#ifndef COMPOSE_H
#define COMPOSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "song.h"

/* The composer behind "Make a song", "Fill this track" and "Chords": a set of
 * writers that put a bass line, an arpeggio, a beat or a tune into one
 * SongPattern from a key, a chord progression and a style, plus the key tools
 * the editor's helpers share (which notes are in key, what chord a bar is on,
 * what key a song is probably in). No raylib, so all of it is tested headless.
 *
 * Styles 0..3 of bass, arp and drums and the lead writer are a port of
 * Music_ThemeToSong's composer in ghost-common/musicsynth.c and reproduce it
 * byte for byte (the theme-lock test pins that), so the built-in game themes
 * stay exactly what the games ship while Chipforge gets more styles here
 * without ghost-common changing. */

typedef struct {
    int root;   /* the tonic as a pitch class 0..11, C is 0 */
    int scale;  /* MusicScale as an int: 0 minor, 1 major, 2 dorian, 3 phrygian, 4 harmonic minor, 5 mixolydian */
} ComposeKey;

/* The scale degree (0..6) of each bar's chord root. */
typedef struct { int degree[4]; } ComposeChords;

typedef struct {
    const char *name;      /* sentence case, at most 14 glyphs: "Hero's anthem" */
    const char *numerals;  /* the degrees as Roman numerals: "I V vi IV" */
    ComposeChords chords;
} ComposeProgression;

/* ---- the writers --------------------------------------------------------- */

/* 6 for bass, arp and drums; 2 for the lead (New tune, Fit my rhythm). */
int Compose_StyleCount(SongTrack track);
/* Sentence case, at most 14 glyphs; "" out of range. */
const char *Compose_StyleName(SongTrack track, int style);

/* Each writer replaces its own track and touches nothing else. tonicMidi is
 * the key's tonic in the register the song sits in (a theme's rootMidi, or
 * Compose_TonicMidi): the bass plays an octave below it, the arp an octave
 * above, the lead at Compose_DegreeNote + 12 * (leadOctave - 2). A note that
 * would leave NOTE_MIN..NOTE_MAX is written an octave back in, so it keeps
 * its pitch class and every note stays in key. Styles out of range are
 * clamped. */
void Compose_Bass(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int style);
void Compose_Arp(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int style);
/* Styles 4 and 5 (Shuffle, Gallop) use the seed for their ghost notes; 0..3 ignore it. */
void Compose_Drums(SongPattern *p, int style, uint32_t seed);
/* A new tune: a rhythm mask per bar chosen by the seed, chord tones on the
 * strong beats, stepwise motion between them, the last bar ending on its
 * chord root. Any chords and any seed: a chorus is Compose_Reharmonise onto
 * new chords followed by this with a fresh seed. */
void Compose_Lead(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int leadOctave, uint32_t seed);
/* Fit my rhythm: keeps every note, hold and silence where the track has them
 * and chooses only the pitches, with the same rules as Compose_Lead; the
 * last note lands on its bar's chord root. */
void Compose_LeadPitches(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int leadOctave, uint32_t seed);

/* ---- notes and the key --------------------------------------------------- */

/* A scale degree (which may run past the octave, or below the root) as an
 * unclamped MIDI note. */
int Compose_DegreeNote(ComposeKey k, int tonicMidi, int degree);
/* The degree 0..6 of a note's pitch class, or -1 when it is not in key.
 * NOTE_NONE and NOTE_HOLD are -1 too. */
int Compose_NoteDegree(ComposeKey k, int note);
bool Compose_InKey(ComposeKey k, int note);
/* Whether a note's pitch class is the root, third or fifth of the chord on
 * that scale degree. */
bool Compose_IsChordTone(ComposeKey k, int degree, int note);
/* The nearest in-key note (ties go down); in-key notes, NOTE_NONE and
 * NOTE_HOLD come back unchanged. */
int Compose_SnapToKey(ComposeKey k, int note);
/* The in-key note delta scale steps away (an out-of-key note is snapped
 * first), stopping at the last in-key note inside NOTE_MIN..NOTE_MAX. */
int Compose_ScaleStep(ComposeKey k, int note, int delta);
/* "Am", "Bdim", "C", "Caug": the chord on a scale degree, spelt with sharps. */
void Compose_ChordName(ComposeKey k, int degree, char *out, size_t size);

/* The tonic in the register a pattern's bass (or, failing that, its arp)
 * sits in, 36..60 with the key's pitch class; without a pattern, or with no
 * notes to go by, the recipe register 40..51. */
int Compose_TonicMidi(ComposeKey k, const SongPattern *p);
/* Guesses the key from the notes (the stored key is ignored); false when the
 * song has no tone notes, such as a drums-only song. */
bool Compose_GuessKey(const Song *s, ComposeKey *out);
/* The song's key: the stored one, else a guess (guessedOut set), else A minor. */
ComposeKey Compose_SongKey(const Song *s, bool *guessedOut);
/* Reads each bar's chord root from the bass, then from the arp, else degree 0;
 * false when no bar had a bass or arp note to read. */
bool Compose_ReadChords(const SongPattern *p, ComposeKey k, ComposeChords *out);

/* ---- changing the chords (Chords, Ctrl+K) -------------------------------- */

/* Moves a pattern from one set of bar chords to another while keeping its
 * rhythm. A bar whose chord does not change is left exactly as it is, so the
 * same chords change nothing. In a bar that changes:
 *   - each bass onset keeps its distance in scale steps from the old bar
 *     root (octave jumps stay octave jumps), the root moving the shorter
 *     way, at most three steps; a note outside the key is snapped into it
 *     first;
 *   - each arp onset that is a tone of the old chord moves to the tone of
 *     the new chord its voice leads to (common tones stay, the rest take the
 *     nearest new tone, a few semitones at most); any other arp note moves
 *     to the nearest tone of the new chord (a tie goes down).
 * A round trip A -> B -> A gives back every bass and arp note of a pattern
 * whose bars sit on their chords, such as a theme. NOTE_HOLD, NOTE_NONE and
 * the drums are never touched, and the lead is touched only when fitLead is
 * set (Compose_FitLead on the new chords). An empty bass or arp is written
 * with style 0 on the new chords, in the register the rest of the pattern
 * sits in. When the remap would put more than half the arp's onsets on one
 * pitch that did not already hold more than half, the arp is rewritten with
 * the style it most resembles instead (Compose_GuessStyle). Notes that would
 * leave NOTE_MIN..NOTE_MAX move an octave back in. from NULL reads the
 * pattern's own chords (Compose_ReadChords); to NULL does nothing. */
void Compose_Reharmonise(SongPattern *p, ComposeKey k, const ComposeChords *from, const ComposeChords *to, bool fitLead);

/* Reads a typed chord line: digits 1..7 ("1 6 4 5"), chord names in the key
 * ("Am F C G" in A minor), or Roman numerals ("i VI iv V"), separated by
 * spaces, commas, dashes or bars. One chord a bar; fewer than four repeat in
 * order ("1 5" is 1 5 1 5). A chord name only has to have its root in the
 * key: the chord used is always the key's own triad on that root, so "A" in
 * A minor means Am. On success fills out, clears err and returns true; else
 * leaves out alone, writes a short sentence-case reason into err (when err
 * is not NULL) and returns false. */
bool Compose_ParseChordLine(const char *text, ComposeKey k, ComposeChords *out, char *err, size_t errSize);

/* Fit the tune: every lead note on a step that is a multiple of 4 moves to
 * the nearest tone of its bar's chord within 2 semitones, every other note
 * to the nearest in-key note; a tie goes down. Away from the ends of the
 * range, only the middle of a diminished chord's six-semitone gap is out of
 * a chord tone's reach: a note there that is in key (harmonic minor's)
 * stays, one that is out of key (the other modes': B on ii of C minor) moves
 * the 3 semitones to the chord tone, so every note ends in key and a second
 * call returns false. Rhythm and holds are kept. Returns whether any note
 * moved. */
bool Compose_FitLead(SongPattern *p, ComposeKey k, const ComposeChords *c);

/* ---- reading a pattern back --------------------------------------------- */

/* The style of Compose_Bass, Compose_Arp or Compose_Drums a track sounds
 * most like (its note/hold/silence shape first, then which chord tones it
 * plays, read with Compose_ReadChords and Compose_TonicMidi), so a writer
 * can keep or vary what is there. Exact for every style a writer puts down
 * (drums at any seed); 0 for an empty track and for the lead. */
int Compose_GuessStyle(const SongPattern *p, ComposeKey k, SongTrack track);
/* The leadOctave Compose_Lead would need to sit where this pattern's lead
 * does (by its average note), 1..5; 2 for an empty lead. Exact for the
 * built-in themes, so a new tune keeps the register of the one it replaces. */
int Compose_LeadOctave(ComposeKey k, int tonicMidi, const SongPattern *p);

/* ---- the named progressions --------------------------------------------- */

int Compose_ProgressionCount(void);
const ComposeProgression *Compose_ProgressionAt(int i); /* wraps */
/* The table index of a progression with these degrees, or -1. */
int Compose_ProgressionFind(const ComposeChords *c);

#endif
