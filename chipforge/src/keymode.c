#include "keymode.h"
#include "musicsynth.h"
#include <stdio.h>
#include <string.h>

static int Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------------------ the piano rows */

/* The white keys' semitone offsets in main.c's piano table: Z X C V B N M on
 * the bottom row, Q W E R T Y U I O P on the top one. */
static const int kWhiteOffsets[KEYMODE_WHITE_KEYS] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26, 28};

int KeyMode_WhiteIndex(int semitoneOffset) {
    for (int i = 0; i < KEYMODE_WHITE_KEYS; i++) if (kWhiteOffsets[i] == semitoneOffset) return i;
    return -1;
}

int KeyMode_NoteForWhiteKey(ComposeKey k, int octave, int whiteIndex) {
    if (whiteIndex < 0 || whiteIndex >= KEYMODE_WHITE_KEYS) return NOTE_NONE;
    /* The tonic named in this octave, as Editor_NoteForKey puts C: then the
     * first note typed shows the octave number the header shows. Octaves
     * past the song's range are pulled in only so the sum cannot overflow;
     * the range clamp below does the real work. */
    int tonic = (ClampI(octave, -1, 9) + 1) * 12 + Mod(k.root, 12);
    int n = Compose_DegreeNote(k, tonic, whiteIndex);
    /* Past an end the row stops on the last in-key note rather than a clamped
     * one, so in-key mode never plays a wrong note even at C8. Every scale has
     * a note within three semitones, so these walks are short. */
    if (n > NOTE_MAX) {
        n = NOTE_MAX;
        while (!Compose_InKey(k, n)) n--;
    } else if (n < NOTE_MIN) {
        n = NOTE_MIN;
        while (!Compose_InKey(k, n)) n++;
    }
    return n;
}

int KeyMode_NoteForKey(ComposeKey k, int octave, int semitoneOffset) {
    int w = KeyMode_WhiteIndex(semitoneOffset);
    return w < 0 ? NOTE_NONE : KeyMode_NoteForWhiteKey(k, octave, w);
}

/* --------------------------------------------------------------------- moods */

static const char *const kMoodNames[SONG_SCALE_COUNT] = {
    "moody", "happy", "dreamy", "spooky", "mysterious", "adventurous",
};

/* Darkest to brightest, by how many degrees each scale raises over plain
 * minor: phrygian lowers the second, harmonic minor and dorian raise one
 * degree (the seventh, the sixth; the harmonic's flat sixth keeps it the
 * darker of the two), mixolydian two and major three. */
static const int kMoodLadder[SONG_SCALE_COUNT] = {
    SCALE_PHRYGIAN, SCALE_MINOR, SCALE_HARMONIC, SCALE_DORIAN, SCALE_MIXOLYDIAN, SCALE_MAJOR,
};

const char *KeyMode_MoodName(int scale) {
    return (scale >= 0 && scale < SONG_SCALE_COUNT) ? kMoodNames[scale] : "?";
}

int KeyMode_NextMood(int scale, int dir) {
    if (scale < 0 || scale >= SONG_SCALE_COUNT) scale = SCALE_MINOR;
    int pos = 0;
    for (int i = 0; i < SONG_SCALE_COUNT; i++) if (kMoodLadder[i] == scale) pos = i;
    return kMoodLadder[Mod(pos + dir % SONG_SCALE_COUNT, SONG_SCALE_COUNT)];
}

/* ---------------------------------------------------------- the transforms */

int KeyMode_FitNote(ComposeKey k, int note) {
    if (note < 0) return note;
    /* Compose_SnapToKey leaves an in-key note alone even outside the range,
     * so clamp first: a fitted song is one the editor would accept as is. */
    return Compose_SnapToKey(k, ClampI(note, NOTE_MIN, NOTE_MAX));
}

int KeyMode_FitPattern(SongPattern *p, ComposeKey k) {
    if (!p) return 0;
    int moved = 0;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        for (int st = 0; st < SONG_STEPS; st++) {
            int n = p->tone[t][st];
            int f = KeyMode_FitNote(k, n);
            if (f != n) { p->tone[t][st] = (int8_t)f; moved++; }
        }
    }
    return moved;
}

static int PatternCountOf(const Song *s) { return ClampI(s->patternCount, 0, SONG_MAX_PATTERNS); }

int KeyMode_FitSong(Song *s, ComposeKey k) {
    if (!s) return 0;
    int moved = 0;
    for (int i = 0; i < PatternCountOf(s); i++) moved += KeyMode_FitPattern(&s->patterns[i], k);
    return moved;
}

int KeyMode_ChangeScaleNote(ComposeKey from, int toScale, int note) {
    if (note < 0) return note;
    int n = KeyMode_FitNote(from, note);
    int deg = Compose_NoteDegree(from, n);
    if (deg < 0) return n; /* cannot happen after the fit; kept so a bad table never indexes out */
    ComposeKey to = {from.root, ClampI(toScale, 0, SONG_SCALE_COUNT - 1)};
    /* With tonic 0, Compose_DegreeNote is the degree's offset in the scale:
     * swap the old scale's offset for the new one's and the note keeps both
     * its degree and its octave. The fit afterwards only matters at the range
     * ends, where a shifted degree can step one semitone past them. */
    int moved = n - Compose_DegreeNote(from, 0, deg) + Compose_DegreeNote(to, 0, deg);
    return KeyMode_FitNote(to, moved);
}

int KeyMode_ChangeScalePattern(SongPattern *p, ComposeKey from, int toScale) {
    if (!p) return 0;
    int changed = 0;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        for (int st = 0; st < SONG_STEPS; st++) {
            int n = p->tone[t][st];
            int c = KeyMode_ChangeScaleNote(from, toScale, n);
            if (c != n) { p->tone[t][st] = (int8_t)c; changed++; }
        }
    }
    return changed;
}

int KeyMode_ChangeScale(Song *s, ComposeKey from, int toScale) {
    if (!s) return 0;
    int changed = 0;
    for (int i = 0; i < PatternCountOf(s); i++) changed += KeyMode_ChangeScalePattern(&s->patterns[i], from, toScale);
    s->keyRoot = Mod(from.root, 12);
    s->keyScale = ClampI(toScale, 0, SONG_SCALE_COUNT - 1);
    return changed;
}

static bool HasStoredKey(const Song *s) { return s->keyRoot >= 0 && s->keyRoot < 12; }

/* The key an action works in: the stored one, else Compose_GuessKey's (the
 * plan's rule for keyless songs). False when there is neither, so the
 * actions refuse instead of silently working in A minor, which is what
 * Compose_SongKey falls back to for display. */
static bool ActionKey(const Song *s, ComposeKey *out, bool *guessed) {
    if (HasStoredKey(s)) {
        out->root = s->keyRoot;
        out->scale = ClampI(s->keyScale, 0, SONG_SCALE_COUNT - 1);
        *guessed = false;
        return true;
    }
    *guessed = true;
    return Compose_GuessKey(s, out);
}

bool KeyMode_EnsureSongKey(Song *s) {
    if (!s) return false;
    if (HasStoredKey(s)) return true;
    ComposeKey k;
    if (!Compose_GuessKey(s, &k)) return false;
    s->keyRoot = Mod(k.root, 12);
    s->keyScale = ClampI(k.scale, 0, SONG_SCALE_COUNT - 1);
    return true;
}

/* ------------------------------------------------------- the editor actions */

static const char *const kNoNotesToGuess = "No notes to guess a key from";

bool KeyMode_EnsureKey(Editor *ed) {
    return ed ? KeyMode_EnsureSongKey(&ed->song) : false;
}

int KeyMode_FitToKey(Editor *ed) {
    if (!ed) return 0;
    ComposeKey k;
    bool guessed;
    if (!ActionKey(&ed->song, &k, &guessed)) {
        Editor_SetStatus(ed, kNoNotesToGuess);
        return 0;
    }
    /* The whole transform is worked on a copy and lands with one Replace, so
     * the live song (which the player may be reading) changes in one pass,
     * and one Commit makes it one undo. memcpy, not assignment, so the
     * snapshot matches the live song byte for byte and Commit's "nothing
     * changed" test cannot be fooled by padding. */
    Song before, work;
    memcpy(&before, &ed->song, sizeof(Song));
    memcpy(&work, &ed->song, sizeof(Song));
    int moved = KeyMode_FitSong(&work, k);
    if (moved == 0) {
        Editor_SetStatus(ed, "Already in key");
        return 0;
    }
    if (guessed) {
        work.keyRoot = Mod(k.root, 12);
        work.keyScale = ClampI(k.scale, 0, SONG_SCALE_COUNT - 1);
    }
    Editor_ReplaceSong(ed, &work);
    Editor_CommitSong(ed, &before);
    char msg[EDITOR_STATUS_LEN];
    if (moved == 1) snprintf(msg, sizeof(msg), "Fitted 1 note");
    else snprintf(msg, sizeof(msg), "Fitted %d notes", moved);
    Editor_SetStatus(ed, msg);
    return moved;
}

bool KeyMode_ChangeMood(Editor *ed, int dir) {
    if (!ed || dir == 0) return false;
    ComposeKey from;
    bool guessed;
    if (!ActionKey(&ed->song, &from, &guessed)) {
        Editor_SetStatus(ed, kNoNotesToGuess);
        return false;
    }
    int to = KeyMode_NextMood(from.scale, dir);
    Song before, work;
    memcpy(&before, &ed->song, sizeof(Song));
    memcpy(&work, &ed->song, sizeof(Song));
    /* Stray notes have no degree to keep, so ChangeScale fits them to the old
     * key first; fitting them here as well changes nothing in the result but
     * counts them, because the status should own up to moving notes the user
     * may have put off the scale on purpose. */
    int strays = KeyMode_FitSong(&work, from);
    /* Writes the key line too, so a guessed key is stored with the change and
     * Ctrl+Z takes both back together. */
    KeyMode_ChangeScale(&work, from, to);
    Editor_ReplaceSong(ed, &work);
    /* Only a dir that wraps all the way round on an in-key song with a stored
     * key changes nothing; then there is no edit to report. */
    if (!Editor_CommitSong(ed, &before)) return false;
    /* The longest real line is about 55 characters ("Mood mysterious (C#
     * harmonic minor), 120 notes fitted"); the buffer is roomier than the
     * status so the compiler can see nothing is cut, and Editor_SetStatus
     * trims. */
    char key[24], msg[128];
    Song_KeyName(work.keyRoot, work.keyScale, key, sizeof(key));
    if (strays == 1) snprintf(msg, sizeof(msg), "Mood %s (%s), 1 note fitted", KeyMode_MoodName(to), key);
    else if (strays > 1) snprintf(msg, sizeof(msg), "Mood %s (%s), %d notes fitted", KeyMode_MoodName(to), key, strays);
    else snprintf(msg, sizeof(msg), "Mood %s (%s)", KeyMode_MoodName(to), key);
    Editor_SetStatus(ed, msg);
    return true;
}
