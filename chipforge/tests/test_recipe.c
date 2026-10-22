/* Tests for "Make a song" (recipe.h): every choice on the recipe screen
 * composes a song that is in key, in range and has its bass and arp in every
 * bar; a theme's recipe gives back the theme; the Length row grows the loop
 * into the arranger's song around an untouched pattern 0; rolls and steps
 * behave the way the screen relies on. Compiled by inclusion from
 * test_main.c. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"
#include "recipe.h"
#include "arrange.h"

/* ---------------------------------------------------------------- helpers */

static bool recipe_Same(const Recipe *a, const Recipe *b) {
    return a->mood == b->mood && a->speed == b->speed && a->story == b->story && a->bass == b->bass &&
           a->sparkle == b->sparkle && a->beat == b->beat && a->sound == b->sound && a->melody == b->melody &&
           a->length == b->length && a->hear == b->hear && a->tonicMidi == b->tonicMidi && a->bpm == b->bpm && a->swing == b->swing &&
           a->leadOctave == b->leadOctave && a->beatSeed == b->beatSeed;
}

/* The fields Recipe_Roll promises to change at least one of. */
static bool recipe_SameMusic(const Recipe *a, const Recipe *b) {
    return a->mood == b->mood && a->speed == b->speed && a->story == b->story && a->bass == b->bass &&
           a->sparkle == b->sparkle && a->beat == b->beat && a->sound == b->sound && a->melody == b->melody &&
           a->tonicMidi == b->tonicMidi && a->bpm == b->bpm;
}

static bool recipe_InRange(const Recipe *r) {
    return r->mood >= 0 && r->mood < RECIPE_MOODS && r->speed >= 0 && r->speed < RECIPE_SPEEDS &&
           r->story >= 0 && r->story < Compose_ProgressionCount() &&
           r->bass >= 0 && r->bass < Compose_StyleCount(TRACK_BASS) &&
           r->sparkle >= 0 && r->sparkle < Compose_StyleCount(TRACK_ARP) &&
           r->beat >= 0 && r->beat < Compose_StyleCount(TRACK_DRUMS) &&
           r->sound >= 0 && r->sound < RECIPE_SOUNDS && r->hear >= 0 && r->hear < RECIPE_HEARS &&
           r->length >= 0 && r->length < RECIPE_LENGTHS &&
           r->melody >= RECIPE_MELODY_MIN && r->melody <= RECIPE_MELODY_MAX;
}

static bool recipe_TrackEqual(const SongPattern *a, const SongPattern *b, int track) {
    if (track == TRACK_DRUMS) return memcmp(a->drums, b->drums, sizeof(a->drums)) == 0;
    return memcmp(a->tone[track], b->tone[track], sizeof(a->tone[track])) == 0;
}

/* Which tracks of pattern 0 differ, as a bit per track. */
static int recipe_ChangedTracks(const Song *a, const Song *b) {
    int bits = 0;
    for (int t = 0; t < SONG_TRACKS; t++) if (!recipe_TrackEqual(&a->patterns[0], &b->patterns[0], t)) bits |= 1 << t;
    return bits;
}

static bool recipe_SameVoices(const Song *a, const Song *b) {
    for (int t = 0; t < SONG_TONE_TRACKS; t++) if (a->wave[t] != b->wave[t]) return false;
    for (int t = 0; t < SONG_TRACKS; t++) if (a->volume[t] != b->volume[t]) return false;
    return true;
}

/* Sentence case ASCII within a glyph budget; digits-only values (the tune
 * number) pass as long as they fit. No byte above 127, so no em-dash. */
static bool recipe_Copy(const char *s, int maxGlyphs) {
    if (!s || !s[0]) return false;
    int n = 0;
    bool digits = true;
    for (const char *p = s; *p; p++, n++) {
        if ((unsigned char)*p > 127) return false;
        if (!isdigit((unsigned char)*p)) digits = false;
        if (p > s && isupper((unsigned char)*p)) return false;
    }
    if (!digits && !isupper((unsigned char)s[0])) return false;
    return n <= maxGlyphs;
}

static int recipe_FieldCount(RecipeField f) {
    switch (f) {
        case RF_MOOD: return RECIPE_MOODS;
        case RF_SPEED: return RECIPE_SPEEDS;
        case RF_STORY: return Compose_ProgressionCount();
        case RF_BASS: return Compose_StyleCount(TRACK_BASS);
        case RF_SPARKLE: return Compose_StyleCount(TRACK_ARP);
        case RF_BEAT: return Compose_StyleCount(TRACK_DRUMS);
        case RF_SOUND: return RECIPE_SOUNDS;
        case RF_MELODY: return RECIPE_MELODY_MAX - RECIPE_MELODY_MIN + 1;
        case RF_LENGTH: return RECIPE_LENGTHS;
        case RF_HEAR: return RECIPE_HEARS;
        default: return 0;
    }
}

static int recipe_FieldGet(const Recipe *r, RecipeField f) {
    switch (f) {
        case RF_MOOD: return r->mood;
        case RF_SPEED: return r->speed;
        case RF_STORY: return r->story;
        case RF_BASS: return r->bass;
        case RF_SPARKLE: return r->sparkle;
        case RF_BEAT: return r->beat;
        case RF_SOUND: return r->sound;
        case RF_MELODY: return r->melody;
        case RF_LENGTH: return r->length;
        case RF_HEAR: return r->hear;
        default: return -1;
    }
}

/* ------------------------------------------------------------------ tests */

/* The whole choice space the rows offer, the tune number swept along with
 * it. The key root turns with every song: Beat and Sound are the two
 * innermost rows and decide no pitch, so each Mood x Story x Bass x Sparkle
 * block (18 songs in a row) meets all twelve roots, the lowest and highest
 * tonic a roll can place (40 and 51) included. That covers every row
 * combination and every pitch-deciding one at every root in 54,000 songs
 * instead of 650,000 (the suite builds at -O0). */
static void recipe_test_every_choice(void) {
    static Song s; /* 16 KB: kept off the stack */
    /* Classic, Soft, Buzzy: bass, arp, lead. */
    static const SongWave kWaves[RECIPE_SOUNDS][SONG_TONE_TRACKS] = {
        {WAVE_TRI, WAVE_PULSE25, WAVE_PULSE50}, {WAVE_TRI, WAVE_TRI, WAVE_PULSE25}, {WAVE_SAW, WAVE_PULSE12, WAVE_PULSE50},
    };
    /* Which notes are in each key, asked of compose.c once rather than per note. */
    static bool inKeyTable[12][RECIPE_MOODS][128];
    for (int root = 0; root < 12; root++)
        for (int mood = 0; mood < RECIPE_MOODS; mood++) {
            Recipe k0;
            Recipe_Default(&k0);
            k0.tonicMidi = 48 + root;
            k0.mood = mood;
            ComposeKey k = Recipe_Key(&k0);
            for (int n = 0; n < 128; n++) inKeyTable[root][mood][n] = Compose_InKey(k, n);
        }
    bool inKey = true, inRange = true, bassBars = true, arpBars = true, keyed = true, shape = true, voiced = true;
    bool everyRoot = true, extremes = true;
    long songs = 0;
    int bassN = Compose_StyleCount(TRACK_BASS), arpN = Compose_StyleCount(TRACK_ARP), beatN = Compose_StyleCount(TRACK_DRUMS);
    for (int mood = 0; mood < RECIPE_MOODS; mood++)
    for (int story = 0; story < Compose_ProgressionCount(); story++)
    for (int bass = 0; bass < bassN; bass++)
    for (int sparkle = 0; sparkle < arpN; sparkle++) {
        int rootsSeen = 0, tonicLow = 99, tonicHigh = 0;
        for (int beat = 0; beat < beatN; beat++)
        for (int sound = 0; sound < RECIPE_SOUNDS; sound++) {
            int root = (int)(songs % 12);
            ComposeKey rootKey = {root, 0};
            Recipe r;
            Recipe_Default(&r);
            r.tonicMidi = Compose_TonicMidi(rootKey, NULL); /* where a roll puts this root */
            r.mood = mood;
            r.story = story;
            r.bass = bass;
            r.sparkle = sparkle;
            Recipe_Step(&r, RF_BEAT, beat - r.beat); /* through the row, so the swing follows */
            r.sound = sound;
            r.melody = RECIPE_MELODY_MIN + (int)(songs % (RECIPE_MELODY_MAX - RECIPE_MELODY_MIN + 1));
            r.beatSeed = (uint32_t)songs * 2654435761u;
            Recipe_Compose(&r, &s);
            songs++;
            rootsSeen |= 1 << root;
            if (r.tonicMidi < tonicLow) tonicLow = r.tonicMidi;
            if (r.tonicMidi > tonicHigh) tonicHigh = r.tonicMidi;

            ComposeKey k = Recipe_Key(&r);
            if (s.keyRoot != root || s.keyRoot != k.root || s.keyScale != k.scale) keyed = false;
            if (s.patternCount != 1 || s.orderCount != 1 || s.order[0] != 0 || s.bpm != r.bpm) shape = false;
            for (int t = 0; t < SONG_TONE_TRACKS; t++) if (s.wave[t] != kWaves[sound][t]) voiced = false;
            const SongPattern *p = &s.patterns[0];
            const bool *table = inKeyTable[root][mood];
            for (int t = 0; t < SONG_TONE_TRACKS; t++) {
                for (int st = 0; st < SONG_STEPS; st++) {
                    int n = p->tone[t][st];
                    if (n == NOTE_NONE || n == NOTE_HOLD) continue;
                    if (n < NOTE_MIN || n > NOTE_MAX) inRange = false;
                    else if (!table[n]) inKey = false;
                }
            }
            for (int bar = 0; bar < 4; bar++) {
                int b = 0, a = 0;
                for (int i = 0; i < 16; i++) {
                    if (p->tone[TRACK_BASS][bar * 16 + i] >= 0) b++;
                    if (p->tone[TRACK_ARP][bar * 16 + i] >= 0) a++;
                }
                if (b < 1 || p->tone[TRACK_BASS][bar * 16] < 0) bassBars = false;
                if (a < 1) arpBars = false;
            }
        }
        if (rootsSeen != 0xFFF) everyRoot = false;
        if (tonicLow != 40 || tonicHigh != 51) extremes = false;
    }
    CHECK(songs == (long)RECIPE_MOODS * Compose_ProgressionCount() * bassN * arpN * beatN * RECIPE_SOUNDS,
          "every choice: every combination of the six rows was composed");
    CHECK(everyRoot && extremes, "every choice: each pitch-deciding combination met all twelve roots, tonics 40 and 51 included");
    CHECK(inKey, "every choice: every note of every recipe is in its key");
    CHECK(inRange, "every choice: every note is within NOTE_MIN..NOTE_MAX");
    CHECK(bassBars, "every choice: the bass sounds in every bar, starting on its first step");
    CHECK(arpBars, "every choice: the arp sounds in every bar");
    CHECK(keyed, "every choice: the song carries the recipe's key");
    CHECK(shape, "every choice: one pattern, played once in the order, at the recipe's tempo");
    CHECK(voiced, "every choice: the Sound row's voices land in the song");
}

static void recipe_test_themes(void) {
    static Song theme, s;
    bool found = true, pattern = true, rest = true, labels = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Music_ThemeToSong(t, &theme);
        Recipe r;
        if (!Recipe_FromTheme(&r, t)) found = false;
        Recipe_Compose(&r, &s);
        if (memcmp(&s.patterns[0], &theme.patterns[0], sizeof(SongPattern)) != 0) {
            pattern = false;
            printf("  recipe of %s does not reproduce its pattern\n", t->slug);
        }
        if (s.bpm != theme.bpm || s.swing != theme.swing || s.keyRoot != theme.keyRoot || s.keyScale != theme.keyScale ||
            !recipe_SameVoices(&s, &theme) || s.patternCount != 1 || s.orderCount != 1)
            rest = false;
        /* The rows name what the theme is: its tempo on the Speed row, and a
         * Hear it as that plays everything. */
        char v[48], want[16];
        Recipe_FieldValue(&r, RF_SPEED, v, sizeof(v));
        snprintf(want, sizeof(want), " %d", t->bpm);
        if (!strstr(v, want) || r.hear != 2 || Recipe_Intensity(&r) != 1.0f) labels = false;
        Recipe_FieldValue(&r, RF_STORY, v, sizeof(v));
        if (strcmp(v, Compose_ProgressionAt(r.story)->name) != 0) labels = false;
    }
    CHECK(found, "themes: every theme's progression is on the Story row");
    CHECK(pattern, "themes: Recipe_FromTheme then Recipe_Compose reproduces each theme's pattern 0 byte for byte");
    CHECK(rest, "themes: and its tempo, swing, key, voices and shape");
    CHECK(labels, "themes: the Speed row shows the theme's own tempo; Hear it as starts at Intense");
    Recipe r;
    CHECK(!Recipe_FromTheme(&r, NULL) && recipe_InRange(&r), "themes: no theme gives a usable default recipe");
}

static void recipe_test_determinism(void) {
    static Song a, b;
    Recipe r1, r2;
    bool same = true;
    for (uint32_t seed = 0; seed < 64; seed++) {
        Recipe_Random(&r1, seed * 7919u);
        Recipe_Random(&r2, seed * 7919u);
        if (!recipe_Same(&r1, &r2)) same = false;
        Recipe_Compose(&r1, &a);
        Recipe_Compose(&r2, &b);
        if (memcmp(&a, &b, sizeof(Song)) != 0) same = false;
    }
    CHECK(same, "determinism: a seed gives one recipe, and a recipe one song");

    /* The tune number decides the tune and nothing else. */
    bool onlyLead = true;
    int distinct = 0;
    static SongPattern leads[8];
    Recipe_Default(&r1);
    for (int m = 0; m < 8; m++) {
        r2 = r1;
        r2.melody = 100 + m * 37;
        Recipe_Compose(&r2, &a);
        Recipe_Compose(&r2, &b);
        if (memcmp(&a, &b, sizeof(Song)) != 0) same = false;
        if (m > 0) {
            static Song first;
            Recipe r0 = r1;
            r0.melody = 100;
            Recipe_Compose(&r0, &first);
            if (recipe_ChangedTracks(&first, &a) & ~(1 << TRACK_LEAD)) onlyLead = false;
        }
        bool fresh = true;
        for (int j = 0; j < distinct; j++)
            if (memcmp(leads[j].tone[TRACK_LEAD], a.patterns[0].tone[TRACK_LEAD], SONG_STEPS) == 0) fresh = false;
        if (fresh) leads[distinct++] = a.patterns[0];
    }
    CHECK(same, "determinism: one tune number always composes the same song");
    CHECK(onlyLead, "determinism: changing the tune number changes only the lead");
    CHECK(distinct >= 4, "determinism: eight tune numbers give at least four different tunes");
}

static void recipe_test_roll(void) {
    bool changed = true, kept = true, valid = true, register_ = true, tempo = true, melody = true;
    for (uint32_t seed = 0; seed < 500; seed++) {
        Recipe r, before;
        Recipe_Random(&r, seed * 2654435761u + 11u);
        r.hear = (int)(seed % RECIPE_HEARS);
        r.length = (int)(seed % RECIPE_LENGTHS);
        before = r;
        Recipe_Roll(&r, seed);
        if (recipe_SameMusic(&r, &before)) changed = false;
        if (r.hear != before.hear || r.length != before.length) kept = false;
        if (!recipe_InRange(&r)) valid = false;
        if (r.tonicMidi < 40 || r.tonicMidi > 51 || r.leadOctave != 3) register_ = false;
        char v[48], want[16];
        Recipe_FieldValue(&r, RF_SPEED, v, sizeof(v));
        snprintf(want, sizeof(want), " %d", r.bpm);
        if (!strstr(v, want)) tempo = false;

        before = r;
        Recipe_RollMelody(&r, seed);
        if (r.melody == before.melody || r.melody < RECIPE_MELODY_MIN || r.melody > RECIPE_MELODY_MAX) melody = false;
        before.melody = r.melody;
        if (!recipe_Same(&r, &before)) melody = false;
    }
    /* The roll whose first draw is the recipe already on screen must still
     * change something: Recipe_Roll(seed) starts from Recipe_Random(seed). */
    bool collision = true;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        Recipe r, before;
        Recipe_Random(&r, seed);
        before = r;
        Recipe_Roll(&r, seed);
        if (recipe_SameMusic(&r, &before)) collision = false;
    }
    /* And from a theme: a roll lands in the recipe register, not the theme's. */
    Recipe t;
    Recipe_FromTheme(&t, Music_FindTheme("moondrop")); /* root 53: above the recipe register, so the roll must move it */
    Recipe_Roll(&t, 99);
    CHECK(changed, "roll: every roll changes at least one field");
    CHECK(collision, "roll: even when the seed redraws the recipe on screen");
    CHECK(kept, "roll: Hear it as and Length are how you listen and how long, so a roll keeps them");
    CHECK(valid, "roll: every rolled field is one of its row's choices");
    CHECK(register_ && t.tonicMidi >= 40 && t.tonicMidi <= 51, "roll: the tonic lands in 40..51 with the tune an octave up");
    CHECK(tempo, "roll: the Speed row shows the tempo the song plays at");
    CHECK(melody, "roll a tune: always a new tune number in 1..999, nothing else changed");
}

static void recipe_test_steps(void) {
    static Song before, after;
    bool wraps = true, onlyField = true, effect = true;
    for (int f = 0; f < RF_COUNT; f++) {
        int n = recipe_FieldCount((RecipeField)f);
        Recipe r, start;
        Recipe_Default(&r);
        start = r;
        /* Around the whole row and back to the start, both ways. */
        for (int i = 0; i < n; i++) Recipe_Step(&r, (RecipeField)f, 1);
        if (recipe_FieldGet(&r, (RecipeField)f) != recipe_FieldGet(&start, (RecipeField)f)) wraps = false;
        for (int i = 0; i < n; i++) Recipe_Step(&r, (RecipeField)f, -1);
        if (recipe_FieldGet(&r, (RecipeField)f) != recipe_FieldGet(&start, (RecipeField)f)) wraps = false;

        /* One step: only that field moves (Speed carries its tempo, Beat its
         * swing), and the composed song changes only where that row says. */
        r = start;
        Recipe_Step(&r, (RecipeField)f, 1);
        Recipe probe = r;
        if (f == RF_SPEED) probe.bpm = start.bpm;
        if (f == RF_BEAT) probe.swing = start.swing;
        switch (f) {
            case RF_MOOD: probe.mood = start.mood; break;
            case RF_SPEED: probe.speed = start.speed; break;
            case RF_STORY: probe.story = start.story; break;
            case RF_BASS: probe.bass = start.bass; break;
            case RF_SPARKLE: probe.sparkle = start.sparkle; break;
            case RF_BEAT: probe.beat = start.beat; break;
            case RF_SOUND: probe.sound = start.sound; break;
            case RF_MELODY: probe.melody = start.melody; break;
            case RF_LENGTH: probe.length = start.length; break;
            case RF_HEAR: probe.hear = start.hear; break;
        }
        if (!recipe_Same(&probe, &start) || recipe_FieldGet(&r, (RecipeField)f) == recipe_FieldGet(&start, (RecipeField)f)) onlyField = false;

        Recipe_Compose(&start, &before);
        Recipe_Compose(&r, &after);
        int tracks = recipe_ChangedTracks(&before, &after);
        bool voices = recipe_SameVoices(&before, &after);
        bool tempoSame = before.bpm == after.bpm && before.swing == after.swing;
        bool keySame = before.keyRoot == after.keyRoot && before.keyScale == after.keyScale;
        switch (f) {
            /* A new mood or story re-pitches the tone tracks (a roots-only
             * bass can come out the same when the chord roots coincide) and
             * never touches the beat. */
            case RF_MOOD: if (keySame || !(tracks & 7) || (tracks & (1 << TRACK_DRUMS)) || !voices || !tempoSame) effect = false; break;
            case RF_SPEED: if (tracks || !voices || before.bpm == after.bpm || !keySame) effect = false; break;
            case RF_STORY: if (!(tracks & 7) || (tracks & (1 << TRACK_DRUMS)) || !voices || !tempoSame || !keySame) effect = false; break;
            case RF_BASS: if (tracks != (1 << TRACK_BASS) || !voices || !tempoSame || !keySame) effect = false; break;
            case RF_SPARKLE: if (tracks != (1 << TRACK_ARP) || !voices || !tempoSame || !keySame) effect = false; break;
            case RF_BEAT: if (tracks != (1 << TRACK_DRUMS) || !voices || before.bpm != after.bpm || !keySame) effect = false; break;
            case RF_SOUND: if (tracks || voices || !tempoSame || !keySame) effect = false; break;
            case RF_MELODY: if (tracks != (1 << TRACK_LEAD) || !voices || !tempoSame || !keySame) effect = false; break;
            /* Length grows the song around pattern 0 and changes nothing else about it. */
            case RF_LENGTH: if (tracks || !voices || !tempoSame || !keySame || after.patternCount == before.patternCount) effect = false; break;
            case RF_HEAR: if (memcmp(&before, &after, sizeof(Song)) != 0) effect = false; break;
        }
    }
    CHECK(wraps, "steps: every row wraps both ways back to where it started");
    CHECK(onlyField, "steps: a step moves only its own row (Speed sets its tempo, Beat its swing)");
    CHECK(effect, "steps: each row changes only its part of the song; Length grows it; Hear it as changes none");

    /* Beat sets the feel: the laid back beat swings, dance does not. */
    Recipe r;
    Recipe_Default(&r);
    Recipe_Step(&r, RF_BEAT, 2);
    float laidBack = r.swing;
    Recipe_Step(&r, RF_BEAT, -2);
    CHECK(laidBack > 0.0f && r.swing == 0.0f, "steps: the Beat row brings its swing with it");
}

static void recipe_test_copy(void) {
    bool labels = true, values = true, distinct = true;
    for (int f = 0; f < RF_COUNT; f++) {
        if (!recipe_Copy(Recipe_FieldLabel((RecipeField)f), 12)) labels = false;
        int n = recipe_FieldCount((RecipeField)f);
        if (f == RF_MELODY) n = 3; /* numbers; a few are enough */
        char seen[16][64];
        int seenN = 0;
        Recipe r;
        Recipe_Default(&r);
        for (int i = 0; i < n; i++) {
            char v[64];
            Recipe_FieldValue(&r, (RecipeField)f, v, sizeof(v));
            if (!recipe_Copy(v, 14)) { values = false; printf("  value \"%s\" on row %d\n", v, f); }
            for (int j = 0; j < seenN && j < 16; j++) if (strcmp(seen[j], v) == 0) distinct = false;
            if (seenN < 16) snprintf(seen[seenN++], sizeof(seen[0]), "%s", v);
            Recipe_Step(&r, (RecipeField)f, 1);
        }
    }
    CHECK(labels, "copy: row labels are short sentence-case ASCII");
    CHECK(values, "copy: every value fits between the arrows (14 glyphs) in sentence case, no em-dash");
    CHECK(distinct, "copy: no two choices on a row read the same");
    CHECK(strcmp(Recipe_FieldLabel(RF_HEAR), "Hear it as") == 0 && Recipe_FieldLabel(RF_COUNT)[0] == '\0', "copy: labels by row, empty past the end");

    Recipe r;
    Recipe_Default(&r);
    char note[96], v[32];
    Recipe_Note(&r, note, sizeof(note));
    CHECK(strcmp(note, "A minor at 104 bpm, about 9 s") == 0, "copy: the note line names the key, tempo and length");
    Recipe_FieldValue(&r, RF_MOOD, v, sizeof(v));
    bool moody = strcmp(v, "Moody") == 0;
    Recipe_FieldValue(&r, RF_SPEED, v, sizeof(v));
    bool steady = strcmp(v, "Steady 104") == 0;
    Recipe_FieldValue(&r, RF_STORY, v, sizeof(v));
    bool hero = strcmp(v, "Hero's anthem") == 0;
    Recipe_FieldValue(&r, RF_MELODY, v, sizeof(v));
    CHECK(moody && steady && hero && strcmp(v, "17") == 0, "copy: the default reads Moody, Steady 104, Hero's anthem, tune 17");
    bool ascii = true;
    for (uint32_t seed = 0; seed < 200; seed++) {
        Recipe_Random(&r, seed);
        Recipe_Note(&r, note, sizeof(note));
        if (!recipe_Copy(note, 48) || !strstr(note, " bpm, about ")) ascii = false;
    }
    CHECK(ascii, "copy: every note line is short ASCII in sentence case");

    /* Hear it as: calm is the layer a game always plays, then more. */
    Recipe_Default(&r);
    float lv[RECIPE_HEARS];
    for (int i = 0; i < RECIPE_HEARS; i++) { r.hear = i; lv[i] = Recipe_Intensity(&r); }
    CHECK(lv[0] <= 0.15f && lv[0] > 0.0f && lv[0] < lv[1] && lv[1] < lv[2] && lv[2] == 1.0f,
          "hear it as: calm is bass and arp only, busy more, intense everything");
}

/* A recipe with nonsense in it still composes something sane. */
static void recipe_test_sanitised(void) {
    static Song s;
    Recipe r;
    Recipe_Default(&r);
    r.mood = 99; r.speed = -4; r.story = 1000; r.bass = -1; r.sparkle = 77; r.beat = 9;
    r.sound = 5; r.hear = -3; r.tonicMidi = 200; r.bpm = 0; r.swing = 5.0f; r.leadOctave = 12;
    Recipe_Compose(&r, &s);
    ComposeKey k = Recipe_Key(&r);
    bool ok = s.bpm >= SONG_BPM_MIN && s.bpm <= SONG_BPM_MAX && s.swing <= 0.3f && s.keyScale >= 0 && s.keyScale < SONG_SCALE_COUNT;
    for (int t = 0; t < SONG_TONE_TRACKS; t++)
        for (int st = 0; st < SONG_STEPS; st++) {
            int n = s.patterns[0].tone[t][st];
            if (n >= 0 && (n < NOTE_MIN || n > NOTE_MAX)) ok = false;
        }
    char v[64];
    for (int f = 0; f < RF_COUNT; f++) { Recipe_FieldValue(&r, (RecipeField)f, v, sizeof(v)); if (!v[0]) ok = false; }
    float lv = Recipe_Intensity(&r);
    CHECK(ok && k.scale >= 0 && k.scale < SONG_SCALE_COUNT && lv >= 0.0f && lv <= 1.0f,
          "sanitised: out-of-range fields compose an in-range song and still have labels");
}


/* The Length row: Loop is the one pattern; Short, Medium and Long are the
 * arranger's loop shapes grown from it, around a pattern 0 that is exactly
 * the loop's, and the screen's words give the length the song really has. */
static void recipe_test_length(void) {
    static Song loop, grown, again, parsed;
    static const int kShape[RECIPE_LENGTHS] = {-1, GROW_SHORT, GROW_GAME_LOOP, GROW_LONG};
    bool counts = true, source = true, sameSong = true, inKey = true, inRange = true, bars = true, determ = true;
    bool roundTrip = true, words = true;
    int songs = 0;
    for (int mood = 0; mood < RECIPE_MOODS; mood++)
    for (int story = 0; story < Compose_ProgressionCount(); story++)
    for (int len = 1; len < RECIPE_LENGTHS; len++) {
        Recipe r;
        Recipe_Default(&r);
        ComposeKey rootKey = {(mood * 5 + story * 7 + len) % 12, 0};
        r.tonicMidi = Compose_TonicMidi(rootKey, NULL);
        r.mood = mood;
        r.story = story;
        r.bass = (story + len) % Compose_StyleCount(TRACK_BASS);
        r.sparkle = (mood + story) % Compose_StyleCount(TRACK_ARP);
        Recipe_Step(&r, RF_BEAT, (mood + len) % Compose_StyleCount(TRACK_DRUMS));
        r.melody = 1 + (songs * 37) % RECIPE_MELODY_MAX;
        r.beatSeed = (uint32_t)songs * 2654435761u;
        Recipe_Compose(&r, &loop);
        Recipe_Step(&r, RF_LENGTH, len);
        Recipe_Compose(&r, &grown);
        Recipe_Compose(&r, &again);
        songs++;
        GrowShape g = (GrowShape)kShape[len];
        if (grown.patternCount != 1 + Arrange_PatternsNeeded(g) || grown.orderCount != Arrange_PassCount(g)) counts = false;
        if (memcmp(&grown.patterns[0], &loop.patterns[0], sizeof(SongPattern)) != 0 || grown.order[0] != 0) source = false;
        if (grown.bpm != loop.bpm || grown.swing != loop.swing || grown.keyRoot != loop.keyRoot ||
            grown.keyScale != loop.keyScale || !recipe_SameVoices(&grown, &loop) || strcmp(grown.name, loop.name) != 0)
            sameSong = false;
        if (memcmp(&grown, &again, sizeof(Song)) != 0) determ = false;
        ComposeKey k = Recipe_Key(&r);
        for (int p = 0; p < grown.patternCount; p++) {
            const SongPattern *pat = &grown.patterns[p];
            for (int t = 0; t < SONG_TONE_TRACKS; t++)
                for (int st = 0; st < SONG_STEPS; st++) {
                    int n = pat->tone[t][st];
                    if (n == NOTE_NONE || n == NOTE_HOLD) continue;
                    if (n < NOTE_MIN || n > NOTE_MAX) inRange = false;
                    else if (!Compose_InKey(k, n)) inKey = false;
                }
            /* The layer a game always plays is in every bar of every part. */
            for (int bar = 0; bar < 4; bar++) {
                int b = 0, a = 0;
                for (int i = 0; i < 16; i++) {
                    if (pat->tone[TRACK_BASS][bar * 16 + i] >= 0) b++;
                    if (pat->tone[TRACK_ARP][bar * 16 + i] >= 0) a++;
                }
                if (b < 1 || a < 1) bars = false;
            }
        }
        /* It saves and loads back as the very same song (Song_Write only
         * takes a FILE, so through a tmpfile). */
        static char text[1 << 18];
        FILE *f = tmpfile();
        if (f) {
            Song_Write(&grown, f);
            rewind(f);
            size_t n = fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
            text[n] = '\0';
            if (!Song_Parse(&parsed, text, n) || memcmp(&parsed, &grown, sizeof(Song)) != 0) roundTrip = false;
        } else {
            roundTrip = false;
        }
        /* The row and the note line say how long it is, as the song says. */
        char v[48], note[96], want[16];
        int secs = (int)(Song_Seconds(&grown) + 0.5f);
        if (secs < 60) snprintf(want, sizeof(want), "%d s", secs);
        else snprintf(want, sizeof(want), "%d:%02d", secs / 60, secs % 60);
        Recipe_FieldValue(&r, RF_LENGTH, v, sizeof(v));
        Recipe_Note(&r, note, sizeof(note));
        size_t vl = strlen(v), wl = strlen(want);
        if (vl < wl || strcmp(v + vl - wl, want) != 0 || !recipe_Copy(v, 14)) words = false;
        if (!strstr(note, want) || !recipe_Copy(note, 48)) words = false;
    }
    CHECK(songs == RECIPE_MOODS * Compose_ProgressionCount() * (RECIPE_LENGTHS - 1), "length: every mood and story at every grown length");
    CHECK(counts, "length: Short, Medium and Long are the arranger's 4, 8 and 16 passes over 3, 7 and 7 patterns");
    CHECK(source, "length: pattern 0 is the loop, untouched, and the song starts on it");
    CHECK(sameSong, "length: tempo, swing, key, voices and name stay the recipe's");
    CHECK(determ, "length: one recipe always grows into the same song");
    CHECK(inKey && inRange, "length: every note of every part is in key and in range");
    CHECK(bars, "length: every part has a bass and an arp in every bar");
    CHECK(roundTrip, "length: a grown song saves and loads back byte for byte");
    CHECK(words, "length: the row and the note line give the song's real length");

    /* Loop is today's one-pattern song, and the words at the default tempo. */
    Recipe r;
    Recipe_Default(&r);
    CHECK(r.length == 0 && strcmp(Recipe_FieldLabel(RF_LENGTH), "Length") == 0, "length: the default is Loop, on a row called Length");
    char v[48], note[96];
    const char *want[RECIPE_LENGTHS] = {"Loop 9 s", "Short 37 s", "Medium 1:14", "Long 2:28"};
    bool named = true;
    for (int len = 0; len < RECIPE_LENGTHS; len++) {
        r.length = len;
        Recipe_FieldValue(&r, RF_LENGTH, v, sizeof(v));
        if (strcmp(v, want[len]) != 0) named = false;
    }
    CHECK(named, "length: at 104 bpm the row reads Loop 9 s, Short 37 s, Medium 1:14, Long 2:28");
    r.length = 2;
    Recipe_Note(&r, note, sizeof(note));
    CHECK(strcmp(note, "A minor at 104 bpm, about 1:14") == 0, "length: the note line gives the grown length");
    r.length = 0;
    Recipe_Compose(&r, &loop);
    CHECK(loop.patternCount == 1 && loop.orderCount == 1, "length: Loop is one pattern played once");
    Recipe t;
    Recipe_FromTheme(&t, Music_ThemeAt(0));
    Recipe_Random(&r, 5u);
    CHECK(t.length == 0 && r.length == 0, "length: a theme's recipe and a random one start as a loop");
    r.length = 99;
    Recipe_Compose(&r, &grown);
    CHECK(grown.patternCount == 1 + Arrange_PatternsNeeded(GROW_LONG), "length: an out-of-range length is kept to the longest");
}

/* Every tone note of every pattern is in key and in range. */
static bool recipe_SongInKey(const Song *s, ComposeKey k) {
    for (int p = 0; p < s->patternCount; p++)
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = s->patterns[p].tone[t][st];
                if (n == NOTE_NONE || n == NOTE_HOLD) continue;
                if (n < NOTE_MIN || n > NOTE_MAX || !Compose_InKey(k, n)) return false;
            }
    return true;
}

/* The top of the register a roll lands on (tonic 49..51: roots C#, D and
 * D#). A grown recipe there could have its chorus tune read an octave high,
 * which ran past NOTE_MAX and was clamped onto C8, out of key: tune 583 did
 * so in 81 of the recipes below, and so did the roll the report found. */
static void recipe_test_top_register(void) {
    static Song s;
    bool inKey = true;
    int songs = 0;
    for (int tonic = 49; tonic <= 51; tonic++)
    for (int len = 1; len < RECIPE_LENGTHS; len++)
    for (int mood = 0; mood < RECIPE_MOODS; mood++)
    for (int story = 0; story < Compose_ProgressionCount(); story++) {
        Recipe r;
        Recipe_Default(&r);
        r.tonicMidi = tonic;
        r.mood = mood;
        r.story = story;
        r.melody = 583;
        r.length = len;
        Recipe_Compose(&r, &s);
        if (!recipe_SongInKey(&s, Recipe_Key(&r))) inKey = false;
        songs++;
    }
    CHECK(songs == 3 * (RECIPE_LENGTHS - 1) * RECIPE_MOODS * Compose_ProgressionCount() && inKey,
          "top register: tune 583 grown at tonics 49..51, every mood, story and length, stays in key");

    /* R landing on D# (tonic 51), then Moody, Rockdrift, tune 293, Short. */
    Recipe r;
    Recipe_Random(&r, 120);
    int tonic = r.tonicMidi;
    Recipe_Step(&r, RF_MOOD, 3 - r.mood);
    Recipe_Step(&r, RF_STORY, 8 - r.story);
    Recipe_Step(&r, RF_MELODY, 293 - r.melody);
    Recipe_Step(&r, RF_LENGTH, 1 - r.length);
    bool rollKey = true;
    for (int len = 1; len < RECIPE_LENGTHS; len++) {
        r.length = len;
        Recipe_Compose(&r, &s);
        if (!recipe_SongInKey(&s, Recipe_Key(&r))) rollKey = false;
    }
    CHECK(tonic == 51 && strcmp(Compose_ProgressionAt(r.story)->name, "Rockdrift") == 0 && rollKey,
          "top register: the roll on D# with tune 293 stays in D# minor at every grown length");
}

void test_recipe_all(void) {
    recipe_test_every_choice();
    recipe_test_themes();
    recipe_test_determinism();
    recipe_test_roll();
    recipe_test_steps();
    recipe_test_copy();
    recipe_test_sanitised();
    recipe_test_length();
    recipe_test_top_register();
}
