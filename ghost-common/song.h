#ifndef SONG_H
#define SONG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* A Ghost Arcade chiptune, as the synth (musicsynth.h) plays it and as
 * Chipforge edits it. No raylib, so songs can be parsed, written and rendered
 * headless.
 *
 * A song is a list of PATTERNS, each 64 steps (four bars of sixteenths), played
 * in the ORDER given. Every pattern has four tracks:
 *
 *   bass, arp, lead   a MIDI note per step, NOTE_NONE for silence, NOTE_HOLD to
 *                     keep the previous note ringing instead of letting it decay
 *   drums             a bit set per step: DRUM_KICK, DRUM_SNARE, DRUM_HAT
 *
 * Tracks keep those names because the games bring the layers in by intensity
 * (bass and arp always, lead and drums as the action heats up), whatever the
 * song. Each tone track picks a waveform and a volume.
 *
 * On disk a song is a small text file (.gsong), see Song_Write. The player's
 * library lives in $XDG_DATA_HOME/ghost-arcade/music/, and
 * $XDG_CONFIG_HOME/ghost-arcade/music.ini says which song each game plays
 * instead of its built-in theme ("slug=file.gsong"). */

#define SONG_STEPS 64
#define SONG_MAX_PATTERNS 64
#define SONG_MAX_ORDER 128
#define SONG_NAME_LEN 40
#define SONG_FILE_LEN 64 /* a library file name, "name.gsong" */
#define SONG_TONE_TRACKS 3
#define SONG_TRACKS 4   /* the three tone tracks plus drums */

#define NOTE_NONE (-1)
#define NOTE_HOLD (-2)
#define NOTE_MIN 24   /* C1 */
#define NOTE_MAX 108  /* C8 */

#define DRUM_KICK 1
#define DRUM_SNARE 2
#define DRUM_HAT 4

#define SONG_BPM_MIN 40
#define SONG_BPM_MAX 300

/* The scales a song's key can name, in the synth's MusicScale order (compose.c
 * pins the two with a _Static_assert): minor, major, dorian, phrygian,
 * harmonic minor, mixolydian. */
#define SONG_SCALE_COUNT 6

typedef enum { TRACK_BASS, TRACK_ARP, TRACK_LEAD, TRACK_DRUMS } SongTrack;

typedef enum { WAVE_TRI, WAVE_PULSE50, WAVE_PULSE25, WAVE_PULSE12, WAVE_SAW, WAVE_COUNT } SongWave;

typedef struct {
    int8_t tone[SONG_TONE_TRACKS][SONG_STEPS]; /* indexed by TRACK_BASS/ARP/LEAD */
    uint8_t drums[SONG_STEPS];
} SongPattern;

typedef struct {
    char name[SONG_NAME_LEN];
    int bpm;
    float swing;                      /* 0..0.3 */
    /* The key, when the song knows it: Chipforge's composer and Music_ThemeToSong
     * set it, and the chord and in-key tools read it instead of guessing. A
     * song that never said (an older file, an import) carries -1; the synth
     * never looks at it. */
    int keyRoot;                      /* tonic as a pitch class 0..11 (C is 0), or -1 for unknown */
    int keyScale;                     /* 0..SONG_SCALE_COUNT-1 in MusicScale order; meaningful only when keyRoot >= 0 */
    SongWave wave[SONG_TONE_TRACKS];
    int volume[SONG_TRACKS];          /* 0..100 per track */
    int patternCount;
    int orderCount;
    uint8_t order[SONG_MAX_ORDER];    /* pattern index per position */
    SongPattern patterns[SONG_MAX_PATTERNS];
} Song;

/* An empty one-pattern song at 120 bpm with the classic voices and no key. */
void Song_Init(Song *s, const char *name);
void Song_ClearPattern(SongPattern *p);

/* Pattern/order edits used by the editor and the importer. All bounds-checked;
 * they return false (and change nothing) when the song is full or the index
 * is out of range. */
bool Song_AddPattern(Song *s, int *indexOut);
bool Song_DuplicatePattern(Song *s, int from, int *indexOut);
bool Song_RemovePattern(Song *s, int index);     /* refuses the last pattern; order entries are fixed up */
bool Song_OrderInsert(Song *s, int at, int pattern);
bool Song_OrderRemove(Song *s, int at);          /* refuses the last entry */
bool Song_OrderSet(Song *s, int at, int pattern);

/* Seconds for one pass through the order. */
float Song_Seconds(const Song *s);
float Song_StepSeconds(const Song *s);

/* Human names. Song_NoteName writes e.g. "C#4", "---" for none, "===" for hold. */
const char *Song_WaveName(SongWave w);
void Song_NoteName(int note, char out[4]);
/* "C#4" / "c#4" / "Db4" style text -> MIDI, or NOTE_NONE if it is not a note. */
int Song_NoteFromName(const char *text);

/* The key's names, as the file's "key" line spells them and the screens show
 * them: Song_KeyName writes e.g. "A minor" or "C# harmonic minor", and "" when
 * keyRoot is -1. Roots are spelt with sharps; the readers accept flats and
 * any case, and return -1 for text that is not a root or a scale. */
const char *Song_ScaleName(int scale);        /* "minor" .. "mixolydian", "?" out of range */
int Song_ScaleFromName(const char *text);     /* "harmonic" alone also means harmonic minor */
const char *Song_KeyRootName(int pitchClass); /* "C", "C#", ... "B", "?" out of range */
int Song_KeyRootFromName(const char *text);   /* "C#", "Db", "a" -> 0..11 */
void Song_KeyName(int keyRoot, int keyScale, char *out, size_t size);

/* ---- the file format -------------------------------------------------- */

/* Writes the song as text. Never fails on a healthy FILE. */
void Song_Write(const Song *s, FILE *f);
/* Parses the text form. Lenient: unknown lines are skipped, values clamped,
 * and whatever was read is kept; returns false only when the text is not a
 * song at all (no header or no patterns). */
bool Song_Parse(Song *s, const char *text, size_t len);
bool Song_Load(Song *s, const char *path);
/* Crash-safe (safefile.c): the old file survives a failure. */
bool Song_Save(const Song *s, const char *path);

/* ---- the player's library ---------------------------------------------- */

/* $XDG_DATA_HOME/ghost-arcade/music (created when create is true). */
bool Song_LibraryDir(char *out, size_t size, bool create);
/* out = <library>/<fileName>; fileName is sanitised (no slashes, ".gsong"
 * appended if missing). Returns false if there is no library dir. */
bool Song_LibraryPath(const char *fileName, char *out, size_t size, bool create);
/* A safe file name made from a song's title: "My Tune!" -> "my-tune.gsong". */
void Song_FileNameFor(const char *title, char *out, size_t size);
/* Lists the library's .gsong files (sorted by name) into names, each up to
 * SONG_FILE_LEN. Returns how many. */
int Song_LibraryList(char names[][SONG_FILE_LEN], int maxNames);

/* Which song a game plays: music.ini "slug=file". Song_Assigned writes the
 * full path of that file into out and returns true if one is set and exists.
 * Song_Assign with fileName NULL or "" clears the slug's line. */
bool Song_Assigned(const char *slug, char *out, size_t size);
bool Song_AssignedName(const char *slug, char *out, size_t size); /* just the file name */
bool Song_Assign(const char *slug, const char *fileName);

#endif
