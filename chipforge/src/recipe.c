#include "recipe.h"
#include "arrange.h"
#include <stdio.h>
#include <string.h>

/* The rows' choices. Moods run from bright to dark so Left/Right feels like
 * turning a dial; each is the scale a beginner would describe that way. */
static const char *const kMoodNames[RECIPE_MOODS] = {"Happy", "Adventurous", "Dreamy", "Moody", "Spooky", "Mysterious"};
static const int kMoodScale[RECIPE_MOODS] = {SCALE_MAJOR, SCALE_MIXOLYDIAN, SCALE_DORIAN, SCALE_MINOR, SCALE_PHRYGIAN, SCALE_HARMONIC};

static const char *const kSpeedNames[RECIPE_SPEEDS] = {"Slow", "Steady", "Quick", "Fast", "Frantic"};
static const int kSpeedBpm[RECIPE_SPEEDS] = {84, 104, 124, 144, 168};

/* Bass, arp, lead. Classic is Song_Init's voices, which every theme uses, so
 * a theme's recipe needs no special case to keep its sound. */
static const char *const kSoundNames[RECIPE_SOUNDS] = {"Classic", "Soft", "Buzzy"};
static const SongWave kSoundWaves[RECIPE_SOUNDS][SONG_TONE_TRACKS] = {
    {WAVE_TRI, WAVE_PULSE25, WAVE_PULSE50},
    {WAVE_TRI, WAVE_TRI, WAVE_PULSE25},
    {WAVE_SAW, WAVE_PULSE12, WAVE_PULSE50},
};

/* Calm stops at 0.15 because the synth brings the hats in above it: that
 * level is exactly the bass and arp, the layer a game always plays. Busy
 * has hats, kick and most of the lead but no snare yet. */
static const char *const kHearNames[RECIPE_HEARS] = {"Calm", "Busy", "Intense"};
static const float kHearLevel[RECIPE_HEARS] = {0.15f, 0.45f, 1.0f};

/* Swing per beat style: the laid back and shuffle feels want the off
 * sixteenths late, the straight dance and rock beats do not. */
static const float kBeatSwing[6] = {0.00f, 0.00f, 0.10f, 0.05f, 0.15f, 0.00f};
/* A style the composer adds later plays straight until it is given a feel here. */
static float BeatSwing(int beat) { return (beat >= 0 && beat < 6) ? kBeatSwing[beat] : 0.0f; }

/* Length: the one pattern on a loop, or the arranger's three loop shapes.
 * Song with an ending is left out: it is for a WAV, and a recipe song is
 * made for a game, which loops the order from the top. */
static const char *const kLengthNames[RECIPE_LENGTHS] = {"Loop", "Short", "Medium", "Long"};
static const int kLengthShape[RECIPE_LENGTHS] = {-1, GROW_SHORT, GROW_GAME_LOOP, GROW_LONG};

static const char *const kFieldLabels[RF_COUNT] = {
    "Mood", "Speed", "Story", "Bass", "Sparkle", "Beat", "Sound", "Melody", "Length", "Hear it as",
};

static int Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* A small stateful generator: an LCG stepped, then mixed, so neighbouring
 * seeds (a roll per key press) give unrelated recipes. */
static uint32_t Next(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    uint32_t x = *s;
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static int Pick(uint32_t *s, int n) { return n > 0 ? (int)(Next(s) % (uint32_t)n) : 0; }

/* Indices kept in range, so a recipe from anywhere (a stale struct, a future
 * theme) composes instead of reading past a table. The register is kept
 * where every style stays in key and in range: a walking bass reaches 16
 * semitones under the tonic, so 40 is the floor above NOTE_MIN (24); the
 * themes sit at 41..55 and the tune at their two lead octaves, which keeps
 * the top of the tune under NOTE_MAX up to a tonic of 60. */
static Recipe Sane(const Recipe *in) {
    Recipe r = *in;
    r.mood = ClampI(r.mood, 0, RECIPE_MOODS - 1);
    r.speed = ClampI(r.speed, 0, RECIPE_SPEEDS - 1);
    r.story = ClampI(r.story, 0, Compose_ProgressionCount() - 1);
    r.bass = ClampI(r.bass, 0, Compose_StyleCount(TRACK_BASS) - 1);
    r.sparkle = ClampI(r.sparkle, 0, Compose_StyleCount(TRACK_ARP) - 1);
    r.beat = ClampI(r.beat, 0, Compose_StyleCount(TRACK_DRUMS) - 1);
    r.sound = ClampI(r.sound, 0, RECIPE_SOUNDS - 1);
    r.length = ClampI(r.length, 0, RECIPE_LENGTHS - 1);
    r.hear = ClampI(r.hear, 0, RECIPE_HEARS - 1);
    r.tonicMidi = ClampI(r.tonicMidi, 40, 60);
    r.bpm = ClampI(r.bpm, SONG_BPM_MIN, SONG_BPM_MAX);
    if (!(r.swing >= 0.0f)) r.swing = 0.0f; /* also catches NaN */
    if (r.swing > 0.3f) r.swing = 0.3f;
    r.leadOctave = ClampI(r.leadOctave, 2, 3);
    return r;
}

ComposeKey Recipe_Key(const Recipe *r) {
    ComposeKey k = {Mod(r->tonicMidi, 12), kMoodScale[ClampI(r->mood, 0, RECIPE_MOODS - 1)]};
    return k;
}

float Recipe_Intensity(const Recipe *r) { return kHearLevel[ClampI(r->hear, 0, RECIPE_HEARS - 1)]; }

/* How many 64-step passes the song plays before it loops: one for Loop,
 * the shape's pass count otherwise. */
static int Passes(const Recipe *r) {
    int shape = kLengthShape[ClampI(r->length, 0, RECIPE_LENGTHS - 1)];
    return shape < 0 ? 1 : Arrange_PassCount((GrowShape)shape);
}

/* "9 s" or "1:14": the song's length at its tempo, one pass being 64
 * sixteenths (960 / bpm seconds), as Song_Seconds counts it. */
static void LengthText(const Recipe *r, char *out, size_t size) {
    int secs = (int)(960.0f * (float)Passes(r) / (float)r->bpm + 0.5f);
    if (secs < 60) snprintf(out, size, "%d s", secs);
    else snprintf(out, size, "%d:%02d", secs / 60, secs % 60);
}

void Recipe_Default(Recipe *r) {
    memset(r, 0, sizeof(*r));
    r->mood = 3;    /* Moody */
    r->speed = 1;   /* Steady */
    r->story = 0;   /* Hero's anthem */
    r->bass = 0;
    r->sparkle = 0;
    r->beat = 0;
    r->sound = 0;
    r->melody = 17;
    r->length = 0;  /* Loop: one pattern, the quickest thing to hear change */
    r->hear = 2;    /* Intense: the song as written */
    ComposeKey a = {9, SCALE_MINOR};
    r->tonicMidi = Compose_TonicMidi(a, NULL);
    r->bpm = kSpeedBpm[r->speed];
    r->swing = BeatSwing(r->beat);
    r->leadOctave = 3;
    r->beatSeed = 0;
}

void Recipe_Random(Recipe *r, uint32_t seed) {
    uint32_t s = seed;
    memset(r, 0, sizeof(*r));
    r->mood = Pick(&s, RECIPE_MOODS);
    r->speed = Pick(&s, RECIPE_SPEEDS);
    r->story = Pick(&s, Compose_ProgressionCount());
    r->bass = Pick(&s, Compose_StyleCount(TRACK_BASS));
    r->sparkle = Pick(&s, Compose_StyleCount(TRACK_ARP));
    r->beat = Pick(&s, Compose_StyleCount(TRACK_DRUMS));
    r->sound = Pick(&s, RECIPE_SOUNDS);
    r->melody = RECIPE_MELODY_MIN + Pick(&s, RECIPE_MELODY_MAX - RECIPE_MELODY_MIN + 1);
    r->hear = 2;
    /* Any of the twelve roots, placed where the composer keeps a recipe's
     * tonic (40..51): low enough for a bass, and every choice stays in range. */
    ComposeKey k = {Pick(&s, 12), kMoodScale[r->mood]};
    r->tonicMidi = Compose_TonicMidi(k, NULL);
    r->bpm = kSpeedBpm[r->speed];
    r->swing = BeatSwing(r->beat);
    r->leadOctave = 3;
    r->beatSeed = Next(&s);
}

/* The fields that make a different song (the beat's ghost-note seed only
 * matters for two styles, and swing follows the beat). */
static bool SameMusic(const Recipe *a, const Recipe *b) {
    return a->mood == b->mood && a->speed == b->speed && a->story == b->story && a->bass == b->bass &&
           a->sparkle == b->sparkle && a->beat == b->beat && a->sound == b->sound && a->melody == b->melody &&
           a->tonicMidi == b->tonicMidi && a->bpm == b->bpm;
}

void Recipe_Roll(Recipe *r, uint32_t seed) {
    Recipe old = *r;
    /* Attempt 0 is Recipe_Random(seed) itself, so the result is what the
     * seed says unless that happens to be the recipe already on screen, in
     * which case "roll" pressing and nothing changing would read as broken. */
    for (uint32_t attempt = 0; attempt < 8; attempt++) {
        Recipe_Random(r, seed + attempt * 0x9E3779B9u);
        if (!SameMusic(r, &old)) break;
    }
    if (SameMusic(r, &old)) Recipe_Step(r, RF_MELODY, 1);
    r->length = old.length;
    r->hear = old.hear;
}

void Recipe_RollMelody(Recipe *r, uint32_t seed) {
    uint32_t s = seed;
    int span = RECIPE_MELODY_MAX - RECIPE_MELODY_MIN + 1;
    for (int attempt = 0; attempt < 8; attempt++) {
        int m = RECIPE_MELODY_MIN + Pick(&s, span);
        if (m != r->melody) { r->melody = m; return; }
    }
    Recipe_Step(r, RF_MELODY, 1);
}

bool Recipe_FromTheme(Recipe *r, const MusicTheme *t) {
    Recipe_Default(r);
    if (!t) return false;
    for (int m = 0; m < RECIPE_MOODS; m++) if (kMoodScale[m] == (int)t->scale) r->mood = m;
    /* The theme keeps its own tempo; the Speed row names the nearest speed
     * ("Steady 96") until it is changed. A tie goes to the slower name. */
    r->bpm = t->bpm;
    int best = 0;
    for (int i = 1; i < RECIPE_SPEEDS; i++) {
        int d = kSpeedBpm[i] - t->bpm, bd = kSpeedBpm[best] - t->bpm;
        if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = i;
    }
    r->speed = best;
    ComposeChords c;
    for (int b = 0; b < 4; b++) c.degree[b] = t->prog[b];
    int story = Compose_ProgressionFind(&c);
    r->story = story >= 0 ? story : 0;
    r->bass = ClampI(t->bassStyle, 0, Compose_StyleCount(TRACK_BASS) - 1);
    r->sparkle = ClampI(t->arpStyle, 0, Compose_StyleCount(TRACK_ARP) - 1);
    r->beat = ClampI(t->drumStyle, 0, Compose_StyleCount(TRACK_DRUMS) - 1);
    r->sound = 0;
    /* The theme's own seed, even if a future one lies outside the row's
     * 1..999: reproducing the theme matters more, and a step wraps it back. */
    r->melody = t->leadSeed;
    r->hear = 2;
    r->tonicMidi = t->rootMidi;
    r->swing = t->swing;
    r->leadOctave = t->leadOctave;
    r->beatSeed = 0; /* styles 0..3, the themes' range, have no ghost notes */
    return story >= 0;
}

void Recipe_Step(Recipe *r, RecipeField f, int delta) {
    switch (f) {
        case RF_MOOD: r->mood = Mod(r->mood + delta, RECIPE_MOODS); break;
        case RF_SPEED:
            r->speed = Mod(r->speed + delta, RECIPE_SPEEDS);
            r->bpm = kSpeedBpm[r->speed];
            break;
        case RF_STORY: r->story = Mod(r->story + delta, Compose_ProgressionCount()); break;
        case RF_BASS: r->bass = Mod(r->bass + delta, Compose_StyleCount(TRACK_BASS)); break;
        case RF_SPARKLE: r->sparkle = Mod(r->sparkle + delta, Compose_StyleCount(TRACK_ARP)); break;
        case RF_BEAT:
            r->beat = Mod(r->beat + delta, Compose_StyleCount(TRACK_DRUMS));
            r->swing = BeatSwing(r->beat);
            break;
        case RF_SOUND: r->sound = Mod(r->sound + delta, RECIPE_SOUNDS); break;
        case RF_MELODY: {
            int span = RECIPE_MELODY_MAX - RECIPE_MELODY_MIN + 1;
            r->melody = RECIPE_MELODY_MIN + Mod(r->melody - RECIPE_MELODY_MIN + delta, span);
            break;
        }
        case RF_LENGTH: r->length = Mod(r->length + delta, RECIPE_LENGTHS); break;
        case RF_HEAR: r->hear = Mod(r->hear + delta, RECIPE_HEARS); break;
        default: break;
    }
}

const char *Recipe_FieldLabel(RecipeField f) { return (f >= 0 && f < RF_COUNT) ? kFieldLabels[f] : ""; }

void Recipe_FieldValue(const Recipe *in, RecipeField f, char *out, size_t size) {
    if (!out || size == 0) return;
    Recipe r = Sane(in);
    switch (f) {
        case RF_MOOD: snprintf(out, size, "%s", kMoodNames[r.mood]); break;
        case RF_SPEED: snprintf(out, size, "%s %d", kSpeedNames[r.speed], r.bpm); break;
        case RF_STORY: snprintf(out, size, "%s", Compose_ProgressionAt(r.story)->name); break;
        case RF_BASS: snprintf(out, size, "%s", Compose_StyleName(TRACK_BASS, r.bass)); break;
        case RF_SPARKLE: snprintf(out, size, "%s", Compose_StyleName(TRACK_ARP, r.sparkle)); break;
        case RF_BEAT: snprintf(out, size, "%s", Compose_StyleName(TRACK_DRUMS, r.beat)); break;
        case RF_SOUND: snprintf(out, size, "%s", kSoundNames[r.sound]); break;
        case RF_MELODY: snprintf(out, size, "%d", in->melody); break;
        case RF_LENGTH: {
            char len[16];
            LengthText(&r, len, sizeof(len));
            snprintf(out, size, "%s %s", kLengthNames[r.length], len);
            break;
        }
        case RF_HEAR: snprintf(out, size, "%s", kHearNames[r.hear]); break;
        default: out[0] = '\0'; break;
    }
}

void Recipe_Note(const Recipe *in, char *out, size_t size) {
    if (!out || size == 0) return;
    Recipe r = Sane(in);
    ComposeKey k = Recipe_Key(&r);
    char key[32], len[16];
    Song_KeyName(k.root, k.scale, key, sizeof(key));
    LengthText(&r, len, sizeof(len));
    snprintf(out, size, "%s at %d bpm, about %s", key, r.bpm, len);
}

void Recipe_Compose(const Recipe *in, Song *out) {
    Recipe r = Sane(in);
    Song_Init(out, "New song");
    out->bpm = r.bpm;
    out->swing = r.swing;
    ComposeKey k = Recipe_Key(&r);
    out->keyRoot = k.root;
    out->keyScale = k.scale;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) out->wave[t] = kSoundWaves[r.sound][t];
    const ComposeChords *c = &Compose_ProgressionAt(r.story)->chords;
    SongPattern *p = &out->patterns[0];
    Compose_Bass(p, k, r.tonicMidi, c, r.bass);
    Compose_Arp(p, k, r.tonicMidi, c, r.sparkle);
    Compose_Drums(p, r.beat, r.beatSeed);
    Compose_Lead(p, k, r.tonicMidi, c, r.leadOctave, (uint32_t)r.melody);
    /* The arrangement's takes (the chorus tune, the fills' rolls) follow the
     * tune number, so a new tune gives a new chorus too, while every other
     * row keeps the same arrangement of a different loop. The recipe song is
     * one in-key pattern with a bass and arp in every bar, which is exactly
     * what Arrange_Grow builds well from, and 1 + 7 patterns always fit. */
    int shape = kLengthShape[r.length];
    if (shape >= 0) Arrange_Grow(out, 0, k, (GrowShape)shape, (uint32_t)r.melody * 2654435761u ^ r.beatSeed);
}
