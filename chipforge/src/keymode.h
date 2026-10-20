#ifndef KEYMODE_H
#define KEYMODE_H

#include <stdbool.h>
#include "compose.h"
#include "editor.h"
#include "song.h"

/* In-key typing, mood change and fit to key: the tools that stop a beginner
 * from hitting a wrong note. In-key typing turns the white-key positions of
 * the FastTracker piano rows into the song's scale, so every key lands in
 * key; a mood change keeps every note on its scale degree while the scale
 * changes (minor to major brightens it, phrygian darkens it); fit to key
 * moves stray notes to the nearest in-key note. No raylib, so all of it is
 * tested headless; main.c maps the keys and render.c shows the key.
 *
 * Scales are MusicScale values as ints (0 minor, 1 major, 2 dorian,
 * 3 phrygian, 4 harmonic minor, 5 mixolydian), as in ComposeKey and
 * Song.keyScale. */

/* Z X C V B N M, Q W E R T Y U, I O P: seven white keys per octave on the
 * two piano rows, plus three that run into the next octave. */
#define KEYMODE_WHITE_KEYS 17

/* ---- the piano rows ------------------------------------------------------ */

/* 0..16 for the white-key positions of the FastTracker layout (semitone
 * offsets 0 2 4 5 7 9 11 12 14 16 17 19 21 23 24 26 28, the offsets main.c's
 * piano table uses), -1 for the black ones and anything outside 0..28. */
int KeyMode_WhiteIndex(int semitoneOffset);

/* The in-key note a white-key position plays: white index i is scale degree
 * i, counted from the key's tonic in the given octave, which is the tonic
 * whose name carries that octave number ("octave 4" in A minor starts the row
 * on A4, in C major on C4, so C major is exactly the plain white keys). Never
 * out of key and always inside NOTE_MIN..NOTE_MAX: a degree past either end
 * becomes the last in-key note before that end. NOTE_NONE for an index
 * outside 0..16. */
int KeyMode_NoteForWhiteKey(ComposeKey k, int octave, int whiteIndex);

/* What a piano key plays in in-key mode, from the same semitone offset and
 * octave all-notes mode uses (Editor_NoteForKey): NOTE_NONE for a black key,
 * so main.c can skip it. */
int KeyMode_NoteForKey(ComposeKey k, int octave, int semitoneOffset);

/* ---- moods ----------------------------------------------------------------- */

/* The recipe screen's word for a scale: "moody" (minor), "happy" (major),
 * "dreamy" (dorian), "spooky" (phrygian), "mysterious" (harmonic minor),
 * "adventurous" (mixolydian); "?" out of range. */
const char *KeyMode_MoodName(int scale);

/* The scale dir steps along the mood ladder, darkest to brightest:
 * phrygian, minor, harmonic minor, dorian, mixolydian, major. Positive dir
 * is brighter, negative darker; it wraps at both ends so one key can visit
 * every mood. An out-of-range scale counts as minor. */
int KeyMode_NextMood(int scale, int dir);

/* ---- the pure transforms ---------------------------------------------------- */

/* The note fit to the key: notes outside NOTE_MIN..NOTE_MAX are clamped
 * first, then an out-of-key note goes to the nearest in-key note inside the
 * range (ties go down, as Compose_SnapToKey). In-key notes in range,
 * NOTE_NONE and NOTE_HOLD come back unchanged. */
int KeyMode_FitNote(ComposeKey k, int note);

/* Fits every tone note of the pattern (or of every pattern of the song,
 * 0..patternCount-1, whether or not the order plays it). Drums, holds and
 * silences are untouched, so every onset stays on its step. Returns how many
 * notes moved. The song's key line is left alone. */
int KeyMode_FitPattern(SongPattern *p, ComposeKey k);
int KeyMode_FitSong(Song *s, ComposeKey k);

/* The mood change for one note: the note is fit to `from` first (stray notes
 * have no degree to keep), then keeps its scale degree and octave while the
 * scale becomes toScale (the root stays). The result is in the new key and in
 * range; a degree that would step past NOTE_MIN or NOTE_MAX lands on the last
 * in-key note before the end. */
int KeyMode_ChangeScaleNote(ComposeKey from, int toScale, int note);

/* The mood change over a pattern, or over every pattern of a song. Returns
 * how many notes changed. The Song form also writes the key line (keyRoot =
 * from.root, keyScale = toScale), since the old line would be wrong
 * afterwards. Changing to a scale and back is the identity for any song that
 * was in key and clear of the range ends. */
int KeyMode_ChangeScalePattern(SongPattern *p, ComposeKey from, int toScale);
int KeyMode_ChangeScale(Song *s, ComposeKey from, int toScale);

/* Stores Compose_GuessKey's key in a song that has none (keyRoot < 0).
 * Returns true when the song has a key afterwards, false when it had none
 * and there are no tone notes in the order to guess one from. */
bool KeyMode_EnsureSongKey(Song *s);

/* ---- the editor actions ------------------------------------------------------ */

/* KeyMode_EnsureSongKey on the editor's song. No undo entry and no dirty
 * flag: the key is metadata guessed from the notes, so storing it is not an
 * edit (an undo past this point can bring back a keyless snapshot; readers
 * use Compose_SongKey, which guesses the same key again). Sets no status. */
bool KeyMode_EnsureKey(Editor *ed);

/* Fit to key over the whole song as ONE undo entry, with the status line
 * set: "Fitted 12 notes", "Fitted 1 note", "Already in key" (nothing
 * changes, no undo), or "No notes to guess a key from" for a keyless song
 * with no tone notes. A keyless song is fit to its guessed key, and that key
 * is stored in the same undo entry, so the header stops showing it as a
 * guess. Returns how many notes moved. */
int KeyMode_FitToKey(Editor *ed);

/* The mood change over the whole song: the next mood along the ladder (dir
 * as KeyMode_NextMood) with the same root, the key line updated, ONE undo
 * entry, and the status "Mood happy (C major)", with ", 3 notes fitted"
 * added when stray notes had to be fit to the old key first. A keyless song
 * changes from its guessed key, which is stored with the change. Returns
 * false and changes nothing (no status either) for dir 0 or a dir that wraps
 * back to the same scale with nothing to fit, and for a keyless song with no
 * tone notes (status "No notes to guess a key from"). */
bool KeyMode_ChangeMood(Editor *ed, int dir);

#endif
