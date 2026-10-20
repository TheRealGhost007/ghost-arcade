/* Tests for in-key typing, the mood change and fit to key (keymode.h): the
 * piano-row mapping over every key and octave, the scale change's round
 * trips on the twelve game themes, fit to key on stray and random notes, and
 * the editor actions' undo and status lines. Compiled by inclusion from
 * test_main.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"
#include "editor.h"
#include "keymode.h"

/* Big (two dozen undo songs), so it lives outside any stack frame. */
static Editor keymode_ed;

/* ---------------------------------------------------------------- helpers */

static ComposeKey keymode_Key(int root, int scale) {
    ComposeKey k = {root, scale};
    return k;
}

static ComposeKey keymode_ThemeKey(const MusicTheme *t) { return keymode_Key(t->rootMidi % 12, (int)t->scale); }

/* Every tone note of every pattern is in key and in range. */
static bool keymode_AllInKey(const Song *s, ComposeKey k) {
    for (int i = 0; i < s->patternCount; i++)
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = s->patterns[i].tone[t][st];
                if (n < 0) continue;
                if (n < NOTE_MIN || n > NOTE_MAX || !Compose_InKey(k, n)) return false;
            }
    return true;
}

/* The note/hold/none shape of every tone track and the drums are unchanged:
 * nothing appeared, vanished or moved to another step. */
static bool keymode_SameShape(const Song *a, const Song *b) {
    if (a->patternCount != b->patternCount) return false;
    for (int i = 0; i < a->patternCount; i++) {
        const SongPattern *pa = &a->patterns[i], *pb = &b->patterns[i];
        if (memcmp(pa->drums, pb->drums, sizeof(pa->drums)) != 0) return false;
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            for (int st = 0; st < SONG_STEPS; st++) {
                int ca = pa->tone[t][st] >= 0 ? 1 : pa->tone[t][st];
                int cb = pb->tone[t][st] >= 0 ? 1 : pb->tone[t][st];
                if (ca != cb) return false;
            }
    }
    return true;
}

static int keymode_CountDiffs(const Song *a, const Song *b) {
    int d = 0;
    for (int i = 0; i < a->patternCount; i++)
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            for (int st = 0; st < SONG_STEPS; st++)
                if (a->patterns[i].tone[t][st] != b->patterns[i].tone[t][st]) d++;
    return d;
}

static uint32_t keymode_rng = 2024u;
static uint32_t keymode_Rand(void) { keymode_rng = keymode_rng * 1664525u + 1013904223u; return keymode_rng >> 8; }

/* What an import or an all-notes session leaves: three patterns of random
 * chromatic notes, holds and rests over the whole range, plus random drums,
 * and no key line. */
static void keymode_RandomSong(Song *s) {
    Song_Init(s, "Stray");
    s->patternCount = 3;
    s->orderCount = 3;
    for (int i = 0; i < 3; i++) {
        s->order[i] = (uint8_t)i;
        SongPattern *p = &s->patterns[i];
        Song_ClearPattern(p);
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            for (int st = 0; st < SONG_STEPS; st++) {
                uint32_t r = keymode_Rand() % 10;
                if (r < 5) p->tone[t][st] = (int8_t)(NOTE_MIN + (int)(keymode_Rand() % (NOTE_MAX - NOTE_MIN + 1)));
                else if (r < 7) p->tone[t][st] = NOTE_HOLD;
            }
        for (int st = 0; st < SONG_STEPS; st++) p->drums[st] = (uint8_t)(keymode_Rand() & 7);
    }
}

/* A theme with its bass track shifted (and its key line kept), so it has
 * strays on one track. */
static void keymode_ShiftedTheme(const MusicTheme *t, Song *s, int track, int semis) {
    Music_ThemeToSong(t, s);
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = s->patterns[0].tone[track][st];
        if (n >= 0) s->patterns[0].tone[track][st] = (int8_t)(n + semis);
    }
}

/* Song_Write only takes a FILE, so the round trip goes through a tmpfile. */
static bool keymode_WriteParse(const Song *in, Song *out) {
    FILE *f = tmpfile();
    if (!f) return false;
    Song_Write(in, f);
    long n = ftell(f);
    rewind(f);
    char *text = malloc((size_t)n + 1);
    bool ok = text && fread(text, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (ok) ok = Song_Parse(out, text, (size_t)n);
    free(text);
    return ok;
}

/* ------------------------------------------------------------ piano rows */

static void keymode_test_white_index(void) {
    static const int white[KEYMODE_WHITE_KEYS] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26, 28};
    static const int black[12] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22, 25, 27};
    bool order = true, blacks = true;
    for (int i = 0; i < KEYMODE_WHITE_KEYS; i++) if (KeyMode_WhiteIndex(white[i]) != i) order = false;
    for (int i = 0; i < 12; i++) if (KeyMode_WhiteIndex(black[i]) != -1) blacks = false;
    CHECK(order, "keymode: the 17 white positions map to 0..16 in order");
    CHECK(blacks, "keymode: the 12 black positions (S D G H J, 2 3 5 6 7 9 0) map to -1");
    CHECK(KeyMode_WhiteIndex(-1) == -1 && KeyMode_WhiteIndex(29) == -1 && KeyMode_WhiteIndex(1000) == -1,
          "keymode: offsets outside the piano rows map to -1");
    int whites = 0;
    for (int off = 0; off <= 28; off++) if (KeyMode_WhiteIndex(off) >= 0) whites++;
    CHECK(whites == KEYMODE_WHITE_KEYS, "keymode: exactly 17 of the 29 piano offsets are white");
}

static void keymode_test_note_for_white_key(void) {
    int total = 0, good = 0;
    bool rises = true, octaveUp = true, degrees = true, tonicNamed = true, nondecreasing = true, clampSeen = false;
    for (int root = 0; root < 12; root++)
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++)
            for (int oct = 1; oct <= 7; oct++) {
                ComposeKey k = keymode_Key(root, scale);
                int tonic = (oct + 1) * 12 + root;
                int prev = -1000;
                bool prevClamped = false;
                for (int w = 0; w < KEYMODE_WHITE_KEYS; w++) {
                    int n = KeyMode_NoteForWhiteKey(k, oct, w);
                    int raw = Compose_DegreeNote(k, tonic, w);
                    bool clamped = raw < NOTE_MIN || raw > NOTE_MAX;
                    if (clamped) clampSeen = true;
                    total++;
                    if (n >= NOTE_MIN && n <= NOTE_MAX && Compose_InKey(k, n)) good++;
                    if (n < prev) nondecreasing = false;
                    if (!clamped && !prevClamped && w > 0 && n <= prev) rises = false;
                    if (!clamped) {
                        if (n != raw) rises = false;
                        if (Compose_NoteDegree(k, n) != w % 7) degrees = false;
                        if (w + 7 < KEYMODE_WHITE_KEYS) {
                            int up = KeyMode_NoteForWhiteKey(k, oct, w + 7);
                            int rawUp = Compose_DegreeNote(k, tonic, w + 7);
                            if (rawUp <= NOTE_MAX && up != n + 12) octaveUp = false;
                        }
                    }
                    if (w == 0 && !clamped && (n % 12 != root || n / 12 - 1 != oct)) tonicNamed = false;
                    prev = n;
                    prevClamped = clamped;
                }
            }
    printf("  keymode: %d/%d white-key notes in key and in range\n", good, total);
    CHECK(total == 12 * 6 * 7 * 17 && good == total, "keymode: every root x scale x octave 1..7 x white key is in key and in range (8568/8568)");
    CHECK(rises, "keymode: the row rises strictly where nothing clamps");
    CHECK(nondecreasing, "keymode: the row never falls, even where the range clamps it");
    CHECK(octaveUp, "keymode: a white key seven along plays exactly an octave up");
    CHECK(degrees, "keymode: each octave of the row plays the scale's seven degrees in order");
    CHECK(tonicNamed, "keymode: the first white key is the tonic named in the current octave");
    CHECK(clampSeen, "keymode: the sweep reaches the top of the range (so the clamp is tested)");

    /* The clamp holds at the very top: octave 7 of B major runs out at C8. */
    ComposeKey bMaj = keymode_Key(11, SCALE_MAJOR);
    int top = KeyMode_NoteForWhiteKey(bMaj, 7, 16);
    CHECK(top <= NOTE_MAX && Compose_InKey(bMaj, top), "keymode: past the top the row stops on an in-key note in range");
    bool highest = true;
    for (int n = top + 1; n <= NOTE_MAX; n++) if (Compose_InKey(bMaj, n)) highest = false;
    CHECK(highest, "keymode: that note is the highest in-key note in range");
    CHECK(KeyMode_NoteForWhiteKey(bMaj, 0, 0) >= NOTE_MIN && Compose_InKey(bMaj, KeyMode_NoteForWhiteKey(bMaj, 0, 0)),
          "keymode: below the range the row starts on the lowest in-key note");
    CHECK(KeyMode_NoteForWhiteKey(bMaj, 4, -1) == NOTE_NONE && KeyMode_NoteForWhiteKey(bMaj, 4, 17) == NOTE_NONE,
          "keymode: a white index outside 0..16 plays nothing");

    /* A minor at octave 4 starts on A4 (69) and walks the natural minor. */
    ComposeKey aMin = keymode_Key(9, SCALE_MINOR);
    static const int aRow[8] = {69, 71, 72, 74, 76, 77, 79, 81};
    bool aOk = true;
    for (int w = 0; w < 8; w++) if (KeyMode_NoteForWhiteKey(aMin, 4, w) != aRow[w]) aOk = false;
    CHECK(aOk, "keymode: A minor octave 4 plays A4 B4 C5 D5 E5 F5 G5 A5 along Z..M and Q");
}

static void keymode_test_note_for_key(void) {
    /* C major is exactly the plain white keys, so switching modes in C major
     * changes nothing a FastTracker user knows. */
    Editor_Init(&keymode_ed);
    ComposeKey cMaj = keymode_Key(0, SCALE_MAJOR);
    bool same = true, blackSilent = true, matches = true;
    for (int oct = 1; oct <= 7; oct++) {
        keymode_ed.octave = oct;
        for (int off = 0; off <= 28; off++) {
            int n = KeyMode_NoteForKey(cMaj, oct, off);
            int w = KeyMode_WhiteIndex(off);
            if (w < 0) {
                if (n != NOTE_NONE) blackSilent = false;
                continue;
            }
            if (n != KeyMode_NoteForWhiteKey(cMaj, oct, w)) matches = false;
            if ((oct + 1) * 12 + off <= NOTE_MAX && n != Editor_NoteForKey(&keymode_ed, off)) same = false;
        }
    }
    CHECK(same, "keymode: in C major the in-key row is exactly today's white keys");
    CHECK(blackSilent, "keymode: black-key positions play nothing in in-key mode");
    CHECK(matches, "keymode: NoteForKey agrees with NoteForWhiteKey on the white keys");
    bool anyKeyBlack = true;
    for (int root = 0; root < 12; root++)
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++)
            for (int off = 0; off <= 28; off++)
                if (KeyMode_WhiteIndex(off) < 0 && KeyMode_NoteForKey(keymode_Key(root, scale), 4, off) != NOTE_NONE) anyKeyBlack = false;
    CHECK(anyKeyBlack, "keymode: black keys are silent in every key");
}

/* ------------------------------------------------------------------ moods */

static void keymode_test_moods(void) {
    bool named = true;
    for (int s = 0; s < SONG_SCALE_COUNT; s++) {
        const char *m = KeyMode_MoodName(s);
        if (!m || !m[0] || strcmp(m, "?") == 0) named = false;
    }
    CHECK(named, "keymode: every scale has a mood name");
    CHECK(strcmp(KeyMode_MoodName(SCALE_MINOR), "moody") == 0 && strcmp(KeyMode_MoodName(SCALE_MAJOR), "happy") == 0 &&
          strcmp(KeyMode_MoodName(SCALE_DORIAN), "dreamy") == 0 && strcmp(KeyMode_MoodName(SCALE_PHRYGIAN), "spooky") == 0 &&
          strcmp(KeyMode_MoodName(SCALE_HARMONIC), "mysterious") == 0 && strcmp(KeyMode_MoodName(SCALE_MIXOLYDIAN), "adventurous") == 0,
          "keymode: mood names are the recipe's words");
    CHECK(strcmp(KeyMode_MoodName(-1), "?") == 0 && strcmp(KeyMode_MoodName(SONG_SCALE_COUNT), "?") == 0,
          "keymode: an unknown scale has no mood name");

    CHECK(KeyMode_NextMood(SCALE_PHRYGIAN, 1) == SCALE_MINOR && KeyMode_NextMood(SCALE_MINOR, 1) == SCALE_HARMONIC &&
          KeyMode_NextMood(SCALE_HARMONIC, 1) == SCALE_DORIAN && KeyMode_NextMood(SCALE_DORIAN, 1) == SCALE_MIXOLYDIAN &&
          KeyMode_NextMood(SCALE_MIXOLYDIAN, 1) == SCALE_MAJOR,
          "keymode: brighter climbs phrygian, minor, harmonic, dorian, mixolydian, major");
    CHECK(KeyMode_NextMood(SCALE_MAJOR, 1) == SCALE_PHRYGIAN && KeyMode_NextMood(SCALE_PHRYGIAN, -1) == SCALE_MAJOR,
          "keymode: the ladder wraps at both ends");
    bool inverse = true, cycle = true, visits = true;
    for (int s = 0; s < SONG_SCALE_COUNT; s++) {
        if (KeyMode_NextMood(KeyMode_NextMood(s, 1), -1) != s) inverse = false;
        int x = s, seen = 0;
        for (int i = 0; i < SONG_SCALE_COUNT; i++) { seen |= 1 << x; x = KeyMode_NextMood(x, 1); }
        if (x != s) cycle = false;
        if (seen != (1 << SONG_SCALE_COUNT) - 1) visits = false;
        if (KeyMode_NextMood(s, 2) != KeyMode_NextMood(KeyMode_NextMood(s, 1), 1)) inverse = false;
        if (KeyMode_NextMood(s, 0) != s) inverse = false;
    }
    CHECK(inverse, "keymode: darker undoes brighter, and steps add up");
    CHECK(cycle && visits, "keymode: six steps visit every mood and come home");
    CHECK(KeyMode_NextMood(99, 1) == KeyMode_NextMood(SCALE_MINOR, 1), "keymode: an unknown scale counts as minor");
}

/* ------------------------------------------------------- the scale change */

static void keymode_test_change_scale(void) {
    int pairs = 0, exact = 0;
    bool mask = true, range = true, inKey = true, degreeKept = true, semitone = true, keyLine = true, drums = true, counted = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        ComposeKey k = keymode_ThemeKey(t);
        Song orig;
        Music_ThemeToSong(t, &orig);
        for (int to = 0; to < SONG_SCALE_COUNT; to++) {
            Song s;
            memcpy(&s, &orig, sizeof(Song));
            int changed = KeyMode_ChangeScale(&s, k, to);
            ComposeKey nk = keymode_Key(k.root, to);
            if (changed != keymode_CountDiffs(&orig, &s)) counted = false;
            if (!keymode_SameShape(&orig, &s)) mask = false;
            if (memcmp(orig.patterns[0].drums, s.patterns[0].drums, sizeof(s.patterns[0].drums)) != 0) drums = false;
            if (!keymode_AllInKey(&s, nk)) inKey = false;
            if (s.keyRoot != k.root || s.keyScale != to) keyLine = false;
            for (int tr = 0; tr < SONG_TONE_TRACKS; tr++)
                for (int st = 0; st < SONG_STEPS; st++) {
                    int a = orig.patterns[0].tone[tr][st], b = s.patterns[0].tone[tr][st];
                    if (a < 0) continue;
                    if (b < NOTE_MIN || b > NOTE_MAX) range = false;
                    if (Compose_NoteDegree(nk, b) != Compose_NoteDegree(k, a)) degreeKept = false;
                    if (abs(b - a) > 1) semitone = false;
                }
            /* ...and back again. */
            KeyMode_ChangeScale(&s, nk, k.scale);
            pairs++;
            if (memcmp(&s, &orig, sizeof(Song)) == 0) exact++;
            else printf("  keymode: %s to scale %d and back is not exact\n", t->slug, to);
        }
    }
    CHECK(pairs == 72 && exact == pairs, "keymode: ChangeScale there and back is byte-exact for 12 themes x 6 scales (72/72)");
    CHECK(mask, "keymode: ChangeScale never changes the note/hold/none shape");
    CHECK(drums, "keymode: ChangeScale leaves the drums alone");
    CHECK(range, "keymode: ChangeScale keeps every note in NOTE_MIN..NOTE_MAX");
    CHECK(inKey, "keymode: after ChangeScale every note is in the new key");
    CHECK(degreeKept, "keymode: every note keeps its scale degree");
    CHECK(semitone, "keymode: every note moves at most a semitone (the scales differ by one at most per degree)");
    CHECK(keyLine, "keymode: ChangeScale writes the new key line");
    CHECK(counted, "keymode: ChangeScale returns how many notes changed");

    /* The task's own wording: minor to major to minor is the identity, and
     * major really is brighter (some note went up, none down). */
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        if (t->scale != SCALE_MINOR) continue;
        Song orig, s;
        Music_ThemeToSong(t, &orig);
        memcpy(&s, &orig, sizeof(Song));
        ComposeKey k = keymode_ThemeKey(t);
        int changed = KeyMode_ChangeScale(&s, k, SCALE_MAJOR);
        bool up = true;
        for (int tr = 0; tr < SONG_TONE_TRACKS; tr++)
            for (int st = 0; st < SONG_STEPS; st++)
                if (s.patterns[0].tone[tr][st] < orig.patterns[0].tone[tr][st]) up = false;
        KeyMode_ChangeScale(&s, keymode_Key(k.root, SCALE_MAJOR), SCALE_MINOR);
        char what[96];
        snprintf(what, sizeof(what), "keymode: %s minor to major to minor is the identity, and major only raises notes", t->slug);
        CHECK(changed > 0 && up && memcmp(&s, &orig, sizeof(Song)) == 0, what);
    }

    /* The key line survives the file. */
    {
        Song s, back;
        Music_ThemeToSong(Music_ThemeAt(0), &s);
        KeyMode_ChangeScale(&s, keymode_ThemeKey(Music_ThemeAt(0)), SCALE_PHRYGIAN);
        bool ok = keymode_WriteParse(&s, &back);
        CHECK(ok && back.keyRoot == s.keyRoot && back.keyScale == SCALE_PHRYGIAN &&
              memcmp(back.patterns, s.patterns, sizeof(SongPattern) * (size_t)s.patternCount) == 0,
              "keymode: the changed key line and notes survive Song_Write/Song_Parse");
    }

    /* Stray notes are fit first, so the result is in the new key. */
    {
        Song s;
        keymode_RandomSong(&s);
        ComposeKey k = keymode_Key(2, SCALE_DORIAN);
        Song before;
        memcpy(&before, &s, sizeof(Song));
        KeyMode_ChangeScale(&s, k, SCALE_HARMONIC);
        CHECK(keymode_AllInKey(&s, keymode_Key(2, SCALE_HARMONIC)), "keymode: ChangeScale on stray notes lands every note in the new key");
        CHECK(keymode_SameShape(&before, &s), "keymode: ChangeScale on stray notes keeps the shape and the drums");
    }

    /* The range ends: C8 is the third of A minor and would go up to C#8. */
    {
        SongPattern p;
        Song_ClearPattern(&p);
        p.tone[TRACK_LEAD][0] = NOTE_MAX;
        p.tone[TRACK_LEAD][1] = NOTE_HOLD;
        p.tone[TRACK_LEAD][2] = (int8_t)127;      /* out of range, as a raw file could hold */
        p.tone[TRACK_BASS][0] = NOTE_MIN;          /* C1 */
        int n = KeyMode_ChangeScalePattern(&p, keymode_Key(9, SCALE_MINOR), SCALE_MAJOR);
        ComposeKey aMaj = keymode_Key(9, SCALE_MAJOR);
        CHECK(p.tone[TRACK_LEAD][0] == 107 && Compose_InKey(aMaj, 107), "keymode: a degree pushed past C8 lands on the last in-key note (B7)");
        CHECK(p.tone[TRACK_LEAD][1] == NOTE_HOLD, "keymode: holds pass through the scale change");
        CHECK(p.tone[TRACK_LEAD][2] >= NOTE_MIN && p.tone[TRACK_LEAD][2] <= NOTE_MAX && Compose_InKey(aMaj, p.tone[TRACK_LEAD][2]),
              "keymode: an out-of-range note is clamped into the range and the key");
        CHECK(p.tone[TRACK_BASS][0] >= NOTE_MIN && Compose_InKey(aMaj, p.tone[TRACK_BASS][0]), "keymode: the bottom of the range stays in range");
        CHECK(n == 3, "keymode: the count includes clamped notes");

        /* C1 is the second of A# minor; phrygian flattens it below C1. */
        Song_ClearPattern(&p);
        p.tone[TRACK_BASS][0] = NOTE_MIN;
        KeyMode_ChangeScalePattern(&p, keymode_Key(10, SCALE_MINOR), SCALE_PHRYGIAN);
        CHECK(p.tone[TRACK_BASS][0] == NOTE_MIN + 1 && Compose_InKey(keymode_Key(10, SCALE_PHRYGIAN), NOTE_MIN + 1),
              "keymode: a degree pushed below C1 lands on the lowest in-key note (C#1)");
    }
    CHECK(KeyMode_ChangeScale(NULL, keymode_Key(0, 0), 1) == 0 && KeyMode_ChangeScalePattern(NULL, keymode_Key(0, 0), 1) == 0,
          "keymode: ChangeScale on NULL changes nothing");
}

/* ------------------------------------------------------------ fit to key */

static void keymode_test_fit(void) {
    bool inKeyThemes = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song orig, s;
        Music_ThemeToSong(t, &orig);
        memcpy(&s, &orig, sizeof(Song));
        if (KeyMode_FitSong(&s, keymode_ThemeKey(t)) != 0 || memcmp(&s, &orig, sizeof(Song)) != 0) inKeyThemes = false;
    }
    CHECK(inKeyThemes, "keymode: FitSong moves nothing on the 12 in-key themes (memcmp)");

    /* One track shifted a semitone: every note that left the key moves back
     * into it, and nothing else moves. */
    bool shiftedCount = true, shiftedOthers = true, shiftedInKey = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        ComposeKey k = keymode_ThemeKey(t);
        for (int tr = 0; tr < SONG_TONE_TRACKS; tr++) {
            Song orig, s;
            keymode_ShiftedTheme(t, &orig, tr, 1);
            memcpy(&s, &orig, sizeof(Song));
            int out = 0;
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = orig.patterns[0].tone[tr][st];
                if (n >= 0 && !Compose_InKey(k, n)) out++;
            }
            int moved = KeyMode_FitSong(&s, k);
            if (moved != out || out == 0) shiftedCount = false;
            for (int other = 0; other < SONG_TONE_TRACKS; other++)
                if (other != tr && memcmp(s.patterns[0].tone[other], orig.patterns[0].tone[other], SONG_STEPS) != 0) shiftedOthers = false;
            if (!keymode_AllInKey(&s, k)) shiftedInKey = false;
        }
    }
    CHECK(shiftedCount, "keymode: on a theme with one track shifted a semitone, FitSong returns that track's out-of-key notes");
    CHECK(shiftedOthers, "keymode: FitSong leaves the in-key tracks untouched");
    CHECK(shiftedInKey, "keymode: after FitSong the shifted theme is in key again");

    /* Random chromatic songs in every key: no stray survives, in-key notes
     * stay put, every onset keeps its step, each stray goes to a nearest
     * in-key note, and a second fit finds nothing. */
    bool allIn = true, kept = true, shape = true, nearest = true, idem = true, counted = true, drumsSame = true;
    for (int root = 0; root < 12; root++)
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = keymode_Key(root, scale);
            Song orig, s;
            keymode_RandomSong(&orig);
            memcpy(&s, &orig, sizeof(Song));
            int moved = KeyMode_FitSong(&s, k);
            if (!keymode_AllInKey(&s, k)) allIn = false;
            if (!keymode_SameShape(&orig, &s)) shape = false;
            if (moved != keymode_CountDiffs(&orig, &s)) counted = false;
            for (int i = 0; i < orig.patternCount; i++) {
                if (memcmp(orig.patterns[i].drums, s.patterns[i].drums, SONG_STEPS) != 0) drumsSame = false;
                for (int tr = 0; tr < SONG_TONE_TRACKS; tr++)
                    for (int st = 0; st < SONG_STEPS; st++) {
                        int a = orig.patterns[i].tone[tr][st], b = s.patterns[i].tone[tr][st];
                        if (a < 0) continue;
                        if (Compose_InKey(k, a) && a != b) kept = false;
                        int d = abs(b - a);
                        for (int m = NOTE_MIN; m <= NOTE_MAX; m++)
                            if (abs(m - a) < d && Compose_InKey(k, m)) nearest = false;
                    }
            }
            Song again;
            memcpy(&again, &s, sizeof(Song));
            if (KeyMode_FitSong(&again, k) != 0 || memcmp(&again, &s, sizeof(Song)) != 0) idem = false;
        }
    CHECK(allIn, "keymode: FitSong leaves no out-of-key onset on random chromatic songs in all 72 keys");
    CHECK(kept, "keymode: FitSong never moves an in-key note");
    CHECK(shape, "keymode: every onset stays on its step, holds and rests too");
    CHECK(drumsSame, "keymode: FitSong leaves the drums alone");
    CHECK(nearest, "keymode: each stray goes to a nearest in-key note");
    CHECK(counted, "keymode: FitSong returns how many notes moved");
    CHECK(idem, "keymode: FitSong is idempotent");

    /* Notes outside the range come in, even when their pitch class is in key. */
    SongPattern p;
    Song_ClearPattern(&p);
    p.tone[TRACK_ARP][0] = (int8_t)120; /* C9: in C major, but past C8 */
    p.tone[TRACK_ARP][1] = (int8_t)12;  /* C0 */
    p.tone[TRACK_ARP][2] = NOTE_NONE;
    p.tone[TRACK_ARP][3] = NOTE_HOLD;
    p.drums[0] = DRUM_KICK | DRUM_HAT;
    ComposeKey cMaj = keymode_Key(0, SCALE_MAJOR);
    int moved = KeyMode_FitPattern(&p, cMaj);
    CHECK(moved == 2 && p.tone[TRACK_ARP][0] == NOTE_MAX && p.tone[TRACK_ARP][1] == NOTE_MIN,
          "keymode: FitPattern clamps out-of-range notes into NOTE_MIN..NOTE_MAX");
    CHECK(p.tone[TRACK_ARP][2] == NOTE_NONE && p.tone[TRACK_ARP][3] == NOTE_HOLD && p.drums[0] == (DRUM_KICK | DRUM_HAT),
          "keymode: FitPattern passes rests, holds and drums through");
    CHECK(KeyMode_FitNote(cMaj, 61) == 60 && KeyMode_FitNote(cMaj, 66) == 65, "keymode: a tie between two in-key notes goes down");
    CHECK(KeyMode_FitSong(NULL, cMaj) == 0 && KeyMode_FitPattern(NULL, cMaj) == 0, "keymode: FitSong on NULL moves nothing");
}

/* ---------------------------------------------------------- key guessing */

static void keymode_test_ensure_key(void) {
    bool stored = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song s;
        Music_ThemeToSong(t, &s);
        s.keyRoot = -1;
        ComposeKey g;
        bool guessed = Compose_GuessKey(&s, &g);
        if (!KeyMode_EnsureSongKey(&s) || !guessed || s.keyRoot != g.root || s.keyScale != g.scale) stored = false;
    }
    CHECK(stored, "keymode: EnsureSongKey stores Compose_GuessKey's key for a theme whose key line was stripped");

    Song drums;
    Song_Init(&drums, "Beat");
    for (int st = 0; st < SONG_STEPS; st += 4) drums.patterns[0].drums[st] = DRUM_KICK;
    Song copy;
    memcpy(&copy, &drums, sizeof(Song));
    CHECK(!KeyMode_EnsureSongKey(&drums) && memcmp(&drums, &copy, sizeof(Song)) == 0,
          "keymode: EnsureSongKey refuses a drums-only song and leaves it alone");

    Song keyed;
    Music_ThemeToSong(Music_ThemeAt(3), &keyed);
    keyed.keyRoot = 4;
    keyed.keyScale = SCALE_DORIAN; /* deliberately not what the notes say */
    CHECK(KeyMode_EnsureSongKey(&keyed) && keyed.keyRoot == 4 && keyed.keyScale == SCALE_DORIAN,
          "keymode: EnsureSongKey keeps a stored key, even one the notes disagree with");

    /* The editor form: no undo, no dirty flag. */
    Song stripped;
    Music_ThemeToSong(Music_ThemeAt(0), &stripped);
    stripped.keyRoot = -1;
    Editor_SetSong(&keymode_ed, &stripped, "");
    int undo = keymode_ed.undoCount;
    CHECK(KeyMode_EnsureKey(&keymode_ed) && keymode_ed.song.keyRoot == Music_ThemeAt(0)->rootMidi % 12,
          "keymode: EnsureKey stores the guessed key in the editor's song");
    CHECK(keymode_ed.undoCount == undo && !keymode_ed.dirty, "keymode: EnsureKey is metadata: no undo entry, not dirty");
    Editor_SetSong(&keymode_ed, &drums, "");
    CHECK(!KeyMode_EnsureKey(&keymode_ed) && keymode_ed.song.keyRoot == -1, "keymode: EnsureKey returns false for a drums-only song");
    CHECK(!KeyMode_EnsureKey(NULL), "keymode: EnsureKey on NULL is false");
}

/* -------------------------------------------------------- editor actions */

static void keymode_test_editor_fit(void) {
    static Song before, orig;
    const MusicTheme *t = Music_ThemeAt(1);
    ComposeKey k = keymode_ThemeKey(t);

    /* An in-key theme: nothing to do, no undo, not dirty. */
    Music_ThemeToSong(t, &orig);
    Editor_SetSong(&keymode_ed, &orig, "");
    memcpy(&before, &keymode_ed.song, sizeof(Song));
    CHECK(KeyMode_FitToKey(&keymode_ed) == 0 && strcmp(keymode_ed.status, "Already in key") == 0,
          "keymode: FitToKey on an in-key theme says 'Already in key'");
    CHECK(keymode_ed.undoCount == 0 && !keymode_ed.dirty && memcmp(&keymode_ed.song, &before, sizeof(Song)) == 0,
          "keymode: an in-key FitToKey changes nothing and pushes no undo");

    /* Strays on the lead: one undo that restores byte for byte. */
    keymode_ShiftedTheme(t, &orig, TRACK_LEAD, 1);
    int out = 0;
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = orig.patterns[0].tone[TRACK_LEAD][st];
        if (n >= 0 && !Compose_InKey(k, n)) out++;
    }
    Editor_SetSong(&keymode_ed, &orig, "");
    memcpy(&before, &keymode_ed.song, sizeof(Song));
    int moved = KeyMode_FitToKey(&keymode_ed);
    char want[EDITOR_STATUS_LEN];
    snprintf(want, sizeof(want), out == 1 ? "Fitted 1 note" : "Fitted %d notes", out);
    CHECK(moved == out && out > 0 && strcmp(keymode_ed.status, want) == 0, "keymode: FitToKey reports 'Fitted N notes'");
    CHECK(keymode_ed.undoCount == 1 && keymode_ed.dirty, "keymode: FitToKey is exactly one undo entry and dirties the song");
    CHECK(keymode_AllInKey(&keymode_ed.song, k), "keymode: after FitToKey the song is in key");
    Editor_Apply(&keymode_ed, ED_UNDO, 0);
    CHECK(keymode_ed.undoCount == 0 && memcmp(&keymode_ed.song, &before, sizeof(Song)) == 0,
          "keymode: one Ctrl+Z restores the song byte for byte after FitToKey");

    /* A keyless import: fit to the guessed key, which is stored in the same undo. */
    static Song stray;
    keymode_RandomSong(&stray);
    ComposeKey g;
    CHECK(Compose_GuessKey(&stray, &g), "keymode: the random song has a key to guess");
    Editor_SetSong(&keymode_ed, &stray, "");
    memcpy(&before, &keymode_ed.song, sizeof(Song));
    moved = KeyMode_FitToKey(&keymode_ed);
    CHECK(moved > 0 && keymode_ed.song.keyRoot == g.root && keymode_ed.song.keyScale == g.scale && keymode_AllInKey(&keymode_ed.song, g),
          "keymode: FitToKey on a keyless song fits to the guessed key and stores it");
    Editor_Apply(&keymode_ed, ED_UNDO, 0);
    CHECK(memcmp(&keymode_ed.song, &before, sizeof(Song)) == 0 && keymode_ed.song.keyRoot == -1,
          "keymode: Ctrl+Z takes the fit and the stored key back together");

    /* Nothing to guess from. */
    Song drums;
    Song_Init(&drums, "Beat");
    drums.patterns[0].drums[0] = DRUM_SNARE;
    Editor_SetSong(&keymode_ed, &drums, "");
    CHECK(KeyMode_FitToKey(&keymode_ed) == 0 && strcmp(keymode_ed.status, "No notes to guess a key from") == 0 &&
          keymode_ed.undoCount == 0 && keymode_ed.song.keyRoot == -1,
          "keymode: FitToKey refuses a keyless drums-only song");
    CHECK(KeyMode_FitToKey(NULL) == 0, "keymode: FitToKey on NULL is 0");
}

static void keymode_test_editor_mood(void) {
    static Song orig, before;
    bool oneUndo = true, exactBack = true, roundTrip = true, wrapHome = true, status = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Music_ThemeToSong(t, &orig);
        Editor_SetSong(&keymode_ed, &orig, "");
        memcpy(&before, &keymode_ed.song, sizeof(Song));

        int to = KeyMode_NextMood((int)t->scale, 1);
        if (!KeyMode_ChangeMood(&keymode_ed, 1) || keymode_ed.undoCount != 1 || keymode_ed.song.keyScale != to) oneUndo = false;
        char key[40], want[EDITOR_STATUS_LEN];
        Song_KeyName(t->rootMidi % 12, to, key, sizeof(key));
        snprintf(want, sizeof(want), "Mood %s (%s)", KeyMode_MoodName(to), key);
        if (strcmp(keymode_ed.status, want) != 0) { status = false; printf("  keymode: status '%s', wanted '%s'\n", keymode_ed.status, want); }
        Editor_Apply(&keymode_ed, ED_UNDO, 0);
        if (memcmp(&keymode_ed.song, &before, sizeof(Song)) != 0) exactBack = false;

        /* Brighter then darker is the identity, as two edits. */
        KeyMode_ChangeMood(&keymode_ed, 1);
        KeyMode_ChangeMood(&keymode_ed, -1);
        if (memcmp(keymode_ed.song.patterns, before.patterns, sizeof(before.patterns)) != 0 ||
            keymode_ed.song.keyScale != before.keyScale || keymode_ed.song.keyRoot != before.keyRoot) roundTrip = false;

        /* Six steps visit every mood and land back on the theme. */
        Editor_SetSong(&keymode_ed, &orig, "");
        for (int s = 0; s < SONG_SCALE_COUNT; s++) KeyMode_ChangeMood(&keymode_ed, 1);
        if (memcmp(&keymode_ed.song, &before, sizeof(Song)) != 0) wrapHome = false;
    }
    CHECK(oneUndo, "keymode: ChangeMood is one undo entry and writes the new scale");
    CHECK(status, "keymode: ChangeMood reports 'Mood <mood> (<key>)'");
    CHECK(exactBack, "keymode: one Ctrl+Z restores the song byte for byte after ChangeMood");
    CHECK(roundTrip, "keymode: ChangeMood brighter then darker gives the theme back");
    CHECK(wrapHome, "keymode: six ChangeMoods round the ladder give the theme back byte for byte");

    /* A keyless song with notes changes from its guessed key and stores it. */
    Music_ThemeToSong(Music_ThemeAt(2), &orig);
    orig.keyRoot = -1;
    ComposeKey g;
    Compose_GuessKey(&orig, &g);
    Editor_SetSong(&keymode_ed, &orig, "");
    memcpy(&before, &keymode_ed.song, sizeof(Song));
    CHECK(KeyMode_ChangeMood(&keymode_ed, -1) && keymode_ed.song.keyRoot == g.root && keymode_ed.song.keyScale == KeyMode_NextMood(g.scale, -1),
          "keymode: ChangeMood on a keyless song changes from the guessed key and stores the new one");
    Editor_Apply(&keymode_ed, ED_UNDO, 0);
    CHECK(memcmp(&keymode_ed.song, &before, sizeof(Song)) == 0 && keymode_ed.song.keyRoot == -1,
          "keymode: Ctrl+Z takes the mood and the stored key back together");

    /* Strays are owned up to in the status. */
    keymode_ShiftedTheme(Music_ThemeAt(0), &orig, TRACK_ARP, 1);
    ComposeKey k0 = keymode_ThemeKey(Music_ThemeAt(0));
    int out = 0;
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = orig.patterns[0].tone[TRACK_ARP][st];
        if (n >= 0 && !Compose_InKey(k0, n)) out++;
    }
    Editor_SetSong(&keymode_ed, &orig, "");
    KeyMode_ChangeMood(&keymode_ed, 1);
    char tail[32];
    snprintf(tail, sizeof(tail), out == 1 ? ", 1 note fitted" : ", %d notes fitted", out);
    CHECK(out > 0 && strstr(keymode_ed.status, tail) != NULL && keymode_AllInKey(&keymode_ed.song, keymode_Key(k0.root, keymode_ed.song.keyScale)),
          "keymode: ChangeMood fits strays first and says how many");

    /* Refusals: no key and nothing to guess from; dir 0; NULL. */
    Song drums;
    Song_Init(&drums, "Beat");
    drums.patterns[0].drums[8] = DRUM_HAT;
    Editor_SetSong(&keymode_ed, &drums, "");
    memcpy(&before, &keymode_ed.song, sizeof(Song));
    CHECK(!KeyMode_ChangeMood(&keymode_ed, 1) && strcmp(keymode_ed.status, "No notes to guess a key from") == 0,
          "keymode: ChangeMood refuses a keyless song with no tone notes");
    CHECK(keymode_ed.undoCount == 0 && !keymode_ed.dirty && memcmp(&keymode_ed.song, &before, sizeof(Song)) == 0 && keymode_ed.song.keyRoot == -1,
          "keymode: the refused ChangeMood stores no key and pushes no undo");
    Music_ThemeToSong(Music_ThemeAt(0), &orig);
    Editor_SetSong(&keymode_ed, &orig, "");
    CHECK(!KeyMode_ChangeMood(&keymode_ed, 0) && keymode_ed.undoCount == 0, "keymode: ChangeMood with dir 0 does nothing");
    CHECK(!KeyMode_ChangeMood(&keymode_ed, SONG_SCALE_COUNT) && keymode_ed.undoCount == 0,
          "keymode: a dir that wraps back to the same scale on an in-key song is not an edit");
    CHECK(!KeyMode_ChangeMood(NULL, 1), "keymode: ChangeMood on NULL is false");

    /* A keyed song with no notes yet (a blank recipe) can still change mood. */
    Song blank;
    Song_Init(&blank, "Blank");
    blank.keyRoot = 0;
    blank.keyScale = SCALE_MINOR;
    Editor_SetSong(&keymode_ed, &blank, "");
    CHECK(KeyMode_ChangeMood(&keymode_ed, 1) && keymode_ed.song.keyScale == SCALE_HARMONIC && keymode_ed.undoCount == 1 &&
          strcmp(keymode_ed.status, "Mood mysterious (C harmonic minor)") == 0,
          "keymode: a keyed song without notes changes its key line as one undo");
}

void test_keymode_all(void) {
    keymode_test_white_index();
    keymode_test_note_for_white_key();
    keymode_test_note_for_key();
    keymode_test_moods();
    keymode_test_change_scale();
    keymode_test_fit();
    keymode_test_ensure_key();
    keymode_test_editor_fit();
    keymode_test_editor_mood();
}
