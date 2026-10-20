/* Tests for the arranger (arrange.h): the promise of every Vary kind, the
 * chorus and its answering progression, and Grow over every shape for every
 * game theme and a spread of recipe-style songs, including the refusals.
 * Compiled by inclusion from test_main.c. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"
#include "arrange.h"

/* ---------------------------------------------------------------- helpers */

/* One recipe-style song per named progression. */
#define ARRANGE_RECIPES 14

static int arrange_Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

static int arrange_SourceCount(void) { return Music_ThemeCount() + ARRANGE_RECIPES; }

/* The songs every test runs over: the twelve themes, then songs made the
 * way the recipe screen makes them (compose.h's writers in a roll's
 * register, tonic 40..51 with the tune at leadOctave 3), one per named
 * progression, walking every root, scale and style of every track so the
 * newer styles (walking bass, stabs, held arps, shuffle and gallop) meet the
 * arranger too. Built here rather than through recipe.c so this suite needs
 * only the arranger and the composer. */
static void arrange_Source(int i, Song *s, ComposeKey *k, char *label, size_t n) {
    int themes = Music_ThemeCount();
    if (i < themes) {
        const MusicTheme *t = Music_ThemeAt(i);
        Music_ThemeToSong(t, s);
        k->root = t->rootMidi % 12;
        k->scale = (int)t->scale;
        snprintf(label, n, "%s", t->slug);
        return;
    }
    int r = i - themes;
    /* At least 8 glyphs: Song_Parse starts from Song_Init's "Untitled" and a
     * shorter name leaves its tail behind the terminator, which a byte for
     * byte round trip would trip over. */
    Song_Init(s, "Recipe song");
    k->root = (r * 5) % 12;
    k->scale = r % SONG_SCALE_COUNT;
    s->keyRoot = k->root;
    s->keyScale = k->scale;
    s->bpm = 84 + 6 * r;
    int tonic = Compose_TonicMidi(*k, NULL);
    const ComposeChords *c = &Compose_ProgressionAt(r)->chords;
    SongPattern *p = &s->patterns[0];
    Compose_Bass(p, *k, tonic, c, r % 6);
    Compose_Arp(p, *k, tonic, c, (r + 2) % 6);
    Compose_Drums(p, (r + 4) % 6, 17u + (uint32_t)r);
    Compose_Lead(p, *k, tonic, c, 3, 100u + 37u * (uint32_t)r);
    snprintf(label, n, "recipe %d (%s)", r, Compose_ProgressionAt(r)->name);
}

static int arrange_Onsets(const int8_t *notes, int from, int to) {
    int n = 0;
    for (int st = from; st < to; st++) n += notes[st] >= 0;
    return n;
}

static int arrange_Hits(const SongPattern *p, int from, int to) {
    int n = 0;
    for (int st = from; st < to; st++) {
        n += (p->drums[st] & DRUM_KICK) != 0;
        n += (p->drums[st] & DRUM_SNARE) != 0;
        n += (p->drums[st] & DRUM_HAT) != 0;
    }
    return n;
}

static int arrange_Hats(const SongPattern *p) {
    int n = 0;
    for (int st = 0; st < SONG_STEPS; st++) n += (p->drums[st] & DRUM_HAT) != 0;
    return n;
}

static bool arrange_HasSnare(const SongPattern *p) {
    for (int st = 0; st < SONG_STEPS; st++) if (p->drums[st] & DRUM_SNARE) return true;
    return false;
}

static bool arrange_TrackEq(const SongPattern *a, const SongPattern *b, int track) {
    if (track == TRACK_DRUMS) return memcmp(a->drums, b->drums, sizeof(a->drums)) == 0;
    return memcmp(a->tone[track], b->tone[track], sizeof(a->tone[track])) == 0;
}

/* The note/hold/none shape of steps from..to of two tracks, ignoring which note. */
static bool arrange_SameMask(const int8_t *a, const int8_t *b, int n) {
    for (int st = 0; st < n; st++) {
        int ca = a[st] >= 0 ? 1 : a[st], cb = b[st] >= 0 ? 1 : b[st];
        if (ca != cb) return false;
    }
    return true;
}

/* At least one bass and four arp onsets in every bar: what a game plays when
 * calm, so no part may leave a bar of it empty. */
static bool arrange_Backed(const SongPattern *p) {
    for (int bar = 0; bar < 4; bar++) {
        if (arrange_Onsets(p->tone[TRACK_BASS], bar * 16, bar * 16 + 16) < 1) return false;
        if (arrange_Onsets(p->tone[TRACK_ARP], bar * 16, bar * 16 + 16) < 4) return false;
    }
    return true;
}

static bool arrange_InKey(const SongPattern *p, ComposeKey k) {
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        for (int st = 0; st < SONG_STEPS; st++) {
            int n = p->tone[t][st];
            if (n == NOTE_NONE || n == NOTE_HOLD) continue;
            if (n < NOTE_MIN || n > NOTE_MAX || !Compose_InKey(k, n)) return false;
        }
    }
    return true;
}

static int arrange_RootPc(ComposeKey k, int degree) {
    return arrange_Mod(Compose_DegreeNote(k, k.root, arrange_Mod(degree, 7)), 12);
}

static int arrange_LastOnset(const int8_t *notes) {
    int last = -1;
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] >= 0) last = st;
    return last;
}

static bool arrange_SameChords(const ComposeChords *a, const ComposeChords *b) {
    for (int bar = 0; bar < 4; bar++) if (arrange_Mod(a->degree[bar], 7) != arrange_Mod(b->degree[bar], 7)) return false;
    return true;
}

static ComposeChords arrange_ReadChords(const SongPattern *p, ComposeKey k) {
    ComposeChords c;
    Compose_ReadChords(p, k, &c);
    return c;
}

/* The intro's mark: no tune and no snare. */
static bool arrange_LooksLikeIntro(const SongPattern *p) {
    return arrange_Onsets(p->tone[TRACK_LEAD], 0, SONG_STEPS) == 0 && !arrange_HasSnare(p);
}

/* The outro's mark: the last bar's bass is one note held to the end. */
static bool arrange_LooksLikeOutro(const SongPattern *p) {
    const int8_t *bass = p->tone[TRACK_BASS] + 48;
    if (bass[0] < 0) return false;
    for (int i = 1; i < 16; i++) if (bass[i] != NOTE_HOLD) return false;
    return true;
}

/* Song_Write only takes a FILE, so the round trip goes through a tmpfile. */
static bool arrange_RoundTrip(const Song *s) {
    static char text[1 << 18];
    FILE *f = tmpfile();
    if (!f) return false;
    Song_Write(s, f);
    rewind(f);
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    static Song back;
    memset(&back, 0x5A, sizeof(back));
    if (!Song_Parse(&back, text, n)) return false;
    return memcmp(&back, s, sizeof(Song)) == 0;
}

/* A one-pattern song holding p, with the source song's settings. */
static bool arrange_PatternRoundTrip(const Song *from, const SongPattern *p) {
    static Song s;
    s = *from;
    s.patterns[0] = *p;
    for (int i = 1; i < SONG_MAX_PATTERNS; i++) memset(&s.patterns[i], 0, sizeof(SongPattern));
    s.patternCount = 1;
    memset(s.order, 0, sizeof(s.order));
    s.orderCount = 1;
    return arrange_RoundTrip(&s);
}

/* Sentence case with room for an acronym ("WAV"): a capital first letter,
 * no other capital except in a word that is all capitals, ASCII only (so no
 * em-dash can hide), at most maxGlyphs. */
static bool arrange_SentenceCase(const char *s, int maxGlyphs) {
    if (!s || !s[0] || !isupper((unsigned char)s[0])) return false;
    int n = (int)strlen(s);
    if (n > maxGlyphs) return false;
    for (int i = 0; i < n; i++) {
        if ((unsigned char)s[i] > 127) return false;
        if (i == 0 || !isupper((unsigned char)s[i])) continue;
        int a = i, b = i;
        while (a > 0 && isalpha((unsigned char)s[a - 1])) a--;
        while (b < n && isalpha((unsigned char)s[b])) b++;
        for (int j = a; j < b; j++) if (!isupper((unsigned char)s[j])) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ names */

static void arrange_test_names(void) {
    bool names = true;
    for (int v = 0; v < VARY_COUNT; v++) {
        const char *n = Arrange_VaryName((VaryKind)v);
        if (!arrange_SentenceCase(n, 15)) { names = false; printf("  vary name %d: \"%s\"\n", v, n); }
        for (int w = 0; w < v; w++) if (strcmp(n, Arrange_VaryName((VaryKind)w)) == 0) names = false;
    }
    CHECK(names, "names: every vary kind has its own sentence-case name of at most 15 glyphs");
    CHECK(strcmp(Arrange_VaryName(VARY_FILL), "Fill at the end") == 0 && strcmp(Arrange_VaryName(VARY_ANSWER), "Answer") == 0,
          "names: Fill at the end and Answer as the picker shows them");
    CHECK(strcmp(Arrange_VaryName(VARY_COUNT), "") == 0 && strcmp(Arrange_VaryName((VaryKind)-1), "") == 0,
          "names: a kind out of range has an empty name");
    CHECK(ARRANGE_PICKER_KINDS == 5 && VARY_DROP == ARRANGE_PICKER_KINDS - 1, "names: the picker offers fill, answer, new tune, lift and drop");

    bool shapes = true;
    for (int g = 0; g < GROW_COUNT; g++) {
        const char *n = Arrange_ShapeName((GrowShape)g);
        if (!arrange_SentenceCase(n, 30)) { shapes = false; printf("  shape name %d: \"%s\"\n", g, n); }
    }
    CHECK(shapes, "names: every shape has a sentence-case name of at most 30 glyphs");
    CHECK(strcmp(Arrange_ShapeName(GROW_GAME_LOOP), "Game loop, about 1 min") == 0, "names: the default shape reads Game loop, about 1 min");
    CHECK(strcmp(Arrange_ShapeName(GROW_COUNT), "") == 0, "names: a shape out of range has an empty name");

    static const float kSeconds[GROW_COUNT] = {30.0f, 60.0f, 120.0f, 75.0f};
    static const int kNeeded[GROW_COUNT] = {2, 6, 6, 7};
    static const int kPasses[GROW_COUNT] = {4, 8, 16, 9};
    bool numbers = true;
    for (int g = 0; g < GROW_COUNT; g++) {
        if (Arrange_TargetSeconds((GrowShape)g) != kSeconds[g]) numbers = false;
        if (Arrange_PatternsNeeded((GrowShape)g) != kNeeded[g]) numbers = false;
        if (Arrange_PassCount((GrowShape)g) != kPasses[g] || kPasses[g] > SONG_MAX_ORDER) numbers = false;
    }
    CHECK(numbers, "shapes: 30, 60, 120 and 75 s from 2, 6, 6 and 7 new patterns in 4, 8, 16 and 9 passes");
    CHECK(Arrange_TargetSeconds(GROW_COUNT) == 0.0f && Arrange_PatternsNeeded((GrowShape)-1) == 0 && Arrange_PassCount(GROW_COUNT) == 0,
          "shapes: out of range reads as nothing");
}

/* ------------------------------------------------------ answer progression */

static void arrange_test_answer(void) {
    int n = Compose_ProgressionCount();
    bool valid = true, differs = true, opens = true;
    for (int i = 0; i < n; i++) {
        const ComposeChords *verse = &Compose_ProgressionAt(i)->chords;
        int a = Arrange_AnswerProgression(verse);
        if (a < 0 || a >= n) { valid = false; continue; }
        const ComposeChords *chorus = &Compose_ProgressionAt(a)->chords;
        if (arrange_SameChords(verse, chorus)) differs = false;
        int open = chorus->degree[0];
        bool away = open == 3 || open == 5;
        /* Only a verse that is itself the table's IV/VI opener may get a
         * partner that opens elsewhere, and then it must move most bars. */
        if (!away) {
            int changed = 0;
            for (int bar = 0; bar < 4; bar++) changed += chorus->degree[bar] != verse->degree[bar];
            if (open == verse->degree[0] || changed < 3) opens = false;
        }
    }
    CHECK(valid, "answer: every progression gets a partner in the table");
    CHECK(differs, "answer: the partner is never the verse's own chords");
    CHECK(opens, "answer: the partner opens on IV or VI, or else on a new chord with three bars changed");

    ComposeChords odd = {{2, 2, 2, 2}}, home = {{0, 0, 0, 0}};
    int a = Arrange_AnswerProgression(&odd);
    CHECK(a >= 0 && a < n && !arrange_SameChords(&odd, &Compose_ProgressionAt(a)->chords), "answer: chords not in the table still get a partner");
    CHECK(Arrange_AnswerProgression(NULL) == Arrange_AnswerProgression(&home), "answer: NULL reads as i i i i");
    ComposeChords wrapped = {{7, -2, 12, 4}}, plain = {{0, 5, 5, 4}};
    CHECK(Arrange_AnswerProgression(&wrapped) == Arrange_AnswerProgression(&plain), "answer: degrees are read modulo 7");
}

/* ----------------------------------------------------------------- chorus */

static void arrange_test_chorus(void) {
    static Song s;
    bool masks = true, newLead = true, readBack = true, drums = true, inKey = true, backed = true;
    bool same = true, defaults = true, alias = true, trip = true;
    for (int i = 0; i < arrange_SourceCount(); i++) {
        ComposeKey k;
        char label[64];
        arrange_Source(i, &s, &k, label, sizeof(label));
        const SongPattern *src = &s.patterns[0];
        ComposeChords from = arrange_ReadChords(src, k);
        const ComposeChords *to = &Compose_ProgressionAt(Arrange_AnswerProgression(&from))->chords;
        int tonic = Compose_TonicMidi(k, src);
        SongPattern out, again, viaNull, aliased = *src;
        Arrange_Chorus(src, k, tonic, &from, to, 42u, &out);
        Arrange_Chorus(src, k, tonic, &from, to, 42u, &again);
        Arrange_Chorus(src, k, 0, NULL, NULL, 42u, &viaNull);
        Arrange_Chorus(&aliased, k, tonic, &from, to, 42u, &aliased);
        if (!arrange_SameMask(out.tone[TRACK_BASS], src->tone[TRACK_BASS], SONG_STEPS) ||
            !arrange_SameMask(out.tone[TRACK_ARP], src->tone[TRACK_ARP], SONG_STEPS)) { masks = false; printf("  chorus masks: %s\n", label); }
        if (arrange_TrackEq(&out, src, TRACK_LEAD) || arrange_Onsets(out.tone[TRACK_LEAD], 0, SONG_STEPS) == 0) newLead = false;
        ComposeChords got = arrange_ReadChords(&out, k);
        if (!arrange_SameChords(&got, to)) { readBack = false; printf("  chorus chords: %s\n", label); }
        if (!arrange_TrackEq(&out, src, TRACK_DRUMS)) drums = false;
        if (!arrange_InKey(&out, k)) { inKey = false; printf("  chorus key: %s\n", label); }
        if (!arrange_Backed(&out)) backed = false;
        if (memcmp(&out, &again, sizeof(out)) != 0) same = false;
        if (memcmp(&out, &viaNull, sizeof(out)) != 0) defaults = false;
        if (memcmp(&out, &aliased, sizeof(out)) != 0) alias = false;
        if (!arrange_PatternRoundTrip(&s, &out)) trip = false;
    }
    CHECK(masks, "chorus: bass and arp keep their rhythm exactly");
    CHECK(newLead, "chorus: a new tune, never the old one");
    CHECK(readBack, "chorus: its chords read back as the answer progression");
    CHECK(drums, "chorus: the drums are untouched");
    CHECK(inKey, "chorus: every note in key and in range");
    CHECK(backed, "chorus: bass and arp in every bar");
    CHECK(same, "chorus: deterministic per seed");
    CHECK(defaults, "chorus: NULL chords and tonic 0 work out the same as passing them");
    CHECK(alias, "chorus: writing over the source gives the same result");
    CHECK(trip, "chorus: saves and loads back byte for byte");
}

/* ------------------------------------------------------------------- vary */

static void arrange_test_vary(void) {
    static Song s;
    bool fillKeeps = true, fillBusier = true, fillSnare = true, fillRun = true, fillInto = true;
    bool ansRhythm = true, ansDiffers = true, ansRoot = true, ansOnly = true, ansTones = true;
    bool tuneOnly = true, tuneNew = true;
    bool liftDiff = true, liftHats = true, liftDrive = true;
    bool dropHalf = true, dropLong = true;
    bool introQuiet = true, outroHome = true, turnBack = true, turnFill = true;
    bool inKey = true, backed = true, same = true, alias = true, trip = true, unknown = true;

    for (int i = 0; i < arrange_SourceCount(); i++) {
        ComposeKey k;
        char label[64];
        arrange_Source(i, &s, &k, label, sizeof(label));
        const SongPattern *src = &s.patterns[0];
        ComposeChords c = arrange_ReadChords(src, k);
        int tonic = Compose_TonicMidi(k, src);
        bool hasLead = arrange_Onsets(src->tone[TRACK_LEAD], 0, SONG_STEPS) > 0;

        for (uint32_t seed = 1; seed <= 3; seed++) {
            for (int kind = 0; kind < VARY_COUNT; kind++) {
                SongPattern out, again, aliased = *src;
                Arrange_Vary(src, k, tonic, &c, (VaryKind)kind, seed, &out);
                Arrange_Vary(src, k, tonic, &c, (VaryKind)kind, seed, &again);
                Arrange_Vary(&aliased, k, tonic, &c, (VaryKind)kind, seed, &aliased);
                if (memcmp(&out, &again, sizeof(out)) != 0) same = false;
                if (memcmp(&out, &aliased, sizeof(out)) != 0) alias = false;
                if (!arrange_InKey(&out, k)) { inKey = false; printf("  vary %s key: %s seed %u\n", Arrange_VaryName((VaryKind)kind), label, seed); }
                if (!arrange_Backed(&out)) { backed = false; printf("  vary %s backing: %s\n", Arrange_VaryName((VaryKind)kind), label); }
                if (seed == 1 && !arrange_PatternRoundTrip(&s, &out)) trip = false;

                const int8_t *lead = out.tone[TRACK_LEAD], *srcLead = src->tone[TRACK_LEAD];
                switch ((VaryKind)kind) {
                    case VARY_FILL: {
                        bool keeps = true;
                        for (int t = 0; t < SONG_TONE_TRACKS; t++) if (memcmp(out.tone[t], src->tone[t], 48) != 0) keeps = false;
                        if (memcmp(out.drums, src->drums, 48) != 0) keeps = false;
                        if (!keeps) fillKeeps = false;
                        if (arrange_Hits(&out, 48, 64) <= arrange_Hits(src, 48, 64)) fillBusier = false;
                        int lastHit = -1;
                        for (int st = 48; st < 64; st++) if (out.drums[st]) lastHit = st;
                        if (lastHit < 0 || !(out.drums[lastHit] & DRUM_SNARE)) fillSnare = false;
                        if (hasLead) {
                            /* The run fills the last beat and ends a scale step from
                             * the root of the chord the pattern starts on. */
                            int rootPc = arrange_RootPc(k, c.degree[0]);
                            bool run = true;
                            for (int st = 60; st < 64; st++) if (lead[st] < 0) run = false;
                            if (run) {
                                int up = Compose_ScaleStep(k, lead[63], 1), down = Compose_ScaleStep(k, lead[63], -1);
                                if (arrange_Mod(up, 12) != rootPc && arrange_Mod(down, 12) != rootPc) run = false;
                            }
                            if (!run) { fillRun = false; printf("  fill run: %s\n", label); }
                            /* Into another chord: the run aims at that chord's root instead. */
                            int next = arrange_Mod(c.degree[0] + 3, 7);
                            SongPattern into;
                            Arrange_VaryInto(src, k, tonic, &c, VARY_FILL, seed, next, &into);
                            int nextPc = arrange_RootPc(k, next);
                            int u = Compose_ScaleStep(k, into.tone[TRACK_LEAD][63], 1), d = Compose_ScaleStep(k, into.tone[TRACK_LEAD][63], -1);
                            if (arrange_Mod(u, 12) != nextPc && arrange_Mod(d, 12) != nextPc) fillInto = false;
                        }
                        break;
                    }
                    case VARY_ANSWER: {
                        if (!arrange_SameMask(lead + 32, lead, 32)) ansRhythm = false;
                        if (memcmp(lead + 32, lead, 32) == 0) { ansDiffers = false; printf("  answer repeats: %s\n", label); }
                        int last = arrange_LastOnset(lead);
                        if (last < 32 || arrange_Mod(lead[last], 12) != arrange_RootPc(k, c.degree[last / 16])) { ansRoot = false; printf("  answer ending: %s\n", label); }
                        for (int t = 0; t < SONG_TRACKS; t++) if (t != TRACK_LEAD && !arrange_TrackEq(&out, src, t)) ansOnly = false;
                        /* The echo moves with the chords: a strong-beat note that was
                         * a tone of its chord is a tone of the new bar's chord. */
                        for (int st = 0; st < 32; st += 4) {
                            if (st + 32 == last || lead[st] < 0) continue;
                            if (Compose_IsChordTone(k, c.degree[st / 16], lead[st]) && !Compose_IsChordTone(k, c.degree[st / 16 + 2], lead[st + 32])) ansTones = false;
                        }
                        break;
                    }
                    case VARY_NEW_TUNE:
                        for (int t = 0; t < SONG_TRACKS; t++) if (t != TRACK_LEAD && !arrange_TrackEq(&out, src, t)) tuneOnly = false;
                        if (memcmp(lead, srcLead, SONG_STEPS) == 0) tuneNew = false;
                        break;
                    case VARY_LIFT:
                        if (arrange_TrackEq(&out, src, TRACK_BASS) && arrange_TrackEq(&out, src, TRACK_ARP)) { liftDiff = false; printf("  lift same backing: %s\n", label); }
                        if (arrange_Hats(&out) < arrange_Hats(src)) liftHats = false;
                        if (arrange_Onsets(out.tone[TRACK_BASS], 0, SONG_STEPS) != SONG_STEPS) liftDrive = false;
                        break;
                    case VARY_DROP: {
                        if (2 * arrange_Hits(&out, 0, 64) > arrange_Hits(src, 0, 64)) { dropHalf = false; printf("  drop hits: %s\n", label); }
                        int holds = 0;
                        for (int st = 0; st < SONG_STEPS; st++) holds += out.tone[TRACK_BASS][st] == NOTE_HOLD;
                        /* Long bass notes, and at most one short tune note a half bar. */
                        int leadHolds = 0;
                        for (int st = 0; st < SONG_STEPS; st++) leadHolds += lead[st] == NOTE_HOLD;
                        if (holds < 32 || arrange_Onsets(lead, 0, SONG_STEPS) > 8 || leadHolds > arrange_Onsets(lead, 0, SONG_STEPS)) dropLong = false;
                        break;
                    }
                    case VARY_INTRO:
                        if (!arrange_LooksLikeIntro(&out)) introQuiet = false;
                        break;
                    case VARY_OUTRO: {
                        ComposeChords oc = arrange_ReadChords(&out, k);
                        bool home = oc.degree[3] == 0 && arrange_LooksLikeOutro(&out);
                        bool arpHeld = false;
                        for (int st = 48; st < 64; st++) if (out.tone[TRACK_ARP][st] == NOTE_HOLD) arpHeld = true;
                        if (!arpHeld) home = false;
                        if (oc.degree[2] != 3 && oc.degree[2] != 4 && oc.degree[2] != 6) home = false; /* IV, V or VII into the tonic */
                        if (hasLead) {
                            if (lead[48] < 0 || arrange_Mod(lead[48], 12) != arrange_Mod(k.root, 12)) home = false;
                            for (int st = 49; st < 64; st++) if (lead[st] != NOTE_HOLD) home = false;
                        }
                        if (!home) { outroHome = false; printf("  outro: %s\n", label); }
                        break;
                    }
                    case VARY_TURNAROUND: {
                        ComposeChords tc = arrange_ReadChords(&out, k);
                        if (tc.degree[3] != 4 && tc.degree[3] != 0) { turnBack = false; printf("  turnaround ends on %d: %s\n", tc.degree[3], label); }
                        if (arrange_Hits(&out, 48, 64) <= arrange_Hits(src, 48, 64)) turnFill = false;
                        break;
                    }
                    default: break;
                }
            }
        }
        SongPattern out;
        Arrange_Vary(src, k, tonic, &c, VARY_COUNT, 1u, &out);
        if (memcmp(&out, src, sizeof(out)) != 0) unknown = false;
    }
    CHECK(fillKeeps, "fill: bars 1 to 3 are untouched");
    CHECK(fillBusier, "fill: the last bar has more drum hits than before");
    CHECK(fillSnare, "fill: the last hit of the bar is a snare");
    CHECK(fillRun, "fill: the tune runs into the first chord's root over the last beat");
    CHECK(fillInto, "fill: given the next pattern's chord, the run aims at that one");
    CHECK(ansRhythm, "answer: bars 3 and 4 of the tune have the rhythm of bars 1 and 2");
    CHECK(ansDiffers, "answer: at least one note of the echo differs");
    CHECK(ansRoot, "answer: the last note is its bar's chord root");
    CHECK(ansOnly, "answer: only the tune changes");
    CHECK(ansTones, "answer: the echo's strong-beat chord tones move with the chords");
    CHECK(tuneOnly, "new tune: only the tune changes");
    CHECK(tuneNew, "new tune: the tune is a different one");
    CHECK(liftDiff, "lift: the bass or the arp changes, so a calm game hears it");
    CHECK(liftHats, "lift: at least as many hats");
    CHECK(liftDrive, "lift: the bass drives in sixteenths");
    CHECK(dropHalf, "drop: at most half the drum hits");
    CHECK(dropLong, "drop: long held bass notes and at most eight short tune notes");
    CHECK(introQuiet, "intro: no tune and no snare");
    CHECK(outroHome, "outro: the last bar is the tonic after IV, V or VII, every voice held");
    CHECK(turnBack, "turnaround: the last bar is on V or I");
    CHECK(turnFill, "turnaround: its last bar is a fill");
    CHECK(inKey, "vary: every note of every kind in key and in range");
    CHECK(backed, "vary: every kind keeps bass and arp in every bar");
    CHECK(same, "vary: deterministic per seed");
    CHECK(alias, "vary: writing over the source gives the same result");
    CHECK(trip, "vary: every kind saves and loads back byte for byte");
    CHECK(unknown, "vary: an unknown kind leaves the pattern as it was");

    /* NULL chords and a tonic of 0 are worked out from the pattern. */
    ComposeKey k;
    char label[64];
    arrange_Source(0, &s, &k, label, sizeof(label));
    ComposeChords c = arrange_ReadChords(&s.patterns[0], k);
    bool defaults = true;
    for (int kind = 0; kind < VARY_COUNT; kind++) {
        SongPattern a, b;
        Arrange_Vary(&s.patterns[0], k, Compose_TonicMidi(k, &s.patterns[0]), &c, (VaryKind)kind, 5u, &a);
        Arrange_Vary(&s.patterns[0], k, 0, NULL, (VaryKind)kind, 5u, &b);
        if (memcmp(&a, &b, sizeof(a)) != 0) defaults = false;
    }
    CHECK(defaults, "vary: NULL chords and tonic 0 work out the same as passing them");

    /* A theme's own seed on its own chords is the theme's own tune, so a new
     * tune or a chorus on unchanged chords asked for with it must skip it. */
    bool ownSeed = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        arrange_Source(i, &s, &k, label, sizeof(label));
        ComposeChords tc = arrange_ReadChords(&s.patterns[0], k);
        SongPattern tune, chorus;
        Arrange_Vary(&s.patterns[0], k, t->rootMidi, &tc, VARY_NEW_TUNE, (uint32_t)t->leadSeed, &tune);
        Arrange_Chorus(&s.patterns[0], k, t->rootMidi, &tc, &tc, (uint32_t)t->leadSeed, &chorus);
        if (arrange_TrackEq(&tune, &s.patterns[0], TRACK_LEAD) || arrange_TrackEq(&chorus, &s.patterns[0], TRACK_LEAD)) ownSeed = false;
    }
    CHECK(ownSeed, "new tune: the source's own seed still gives a different tune");

    /* A tune whose second half would echo as an exact repeat: i iv i iv with
     * bar 2 ending on iv's root. The answer must still change a note. */
    ComposeKey am = {9, 0};
    ComposeChords iv = {{0, 3, 0, 3}};
    SongPattern rep;
    Song_ClearPattern(&rep);
    Compose_Bass(&rep, am, 45, &iv, 0);
    Compose_Arp(&rep, am, 45, &iv, 0);
    static const int8_t kCall[32] = {
        69, NOTE_NONE, 72, NOTE_NONE, 76, NOTE_NONE, 72, NOTE_NONE, 69, NOTE_HOLD, NOTE_HOLD, NOTE_NONE, 71, NOTE_NONE, 72, NOTE_NONE,
        74, NOTE_NONE, 72, NOTE_NONE, 69, NOTE_NONE, 72, NOTE_NONE, 74, NOTE_HOLD, NOTE_HOLD, NOTE_HOLD, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    };
    memcpy(rep.tone[TRACK_LEAD], kCall, 32);
    SongPattern ans;
    Arrange_Vary(&rep, am, 45, &iv, VARY_ANSWER, 1u, &ans);
    int last = arrange_LastOnset(ans.tone[TRACK_LEAD]);
    CHECK(memcmp(ans.tone[TRACK_LEAD] + 32, ans.tone[TRACK_LEAD], 32) != 0 && arrange_SameMask(ans.tone[TRACK_LEAD] + 32, ans.tone[TRACK_LEAD], 32) &&
          last >= 32 && arrange_Mod(ans.tone[TRACK_LEAD][last], 12) == 2 && arrange_InKey(&ans, am),
          "answer: an echo that would repeat note for note still changes one and ends on the root");
}

/* ------------------------------------------------------------------- grow */

static int arrange_CountIn(const Song *s, int pattern) {
    int n = 0;
    for (int i = 0; i < s->orderCount; i++) n += s->order[i] == pattern;
    return n;
}

static void arrange_test_grow(void) {
    static Song start, s, again;
    bool ok = true, counts = true, first = true, twice = true, unchanged = true, used = true, settings = true;
    bool backed = true, inKey = true, seconds = true, introOutro = true, ending = true, turn = true;
    bool chorus = true, same = true, trip = true;
    for (int i = 0; i < arrange_SourceCount(); i++) {
        ComposeKey k;
        char label[64];
        arrange_Source(i, &start, &k, label, sizeof(label));
        ComposeChords c = arrange_ReadChords(&start.patterns[0], k);
        const ComposeChords *answer = &Compose_ProgressionAt(Arrange_AnswerProgression(&c))->chords;
        for (int g = 0; g < GROW_COUNT; g++) {
            GrowShape shape = (GrowShape)g;
            s = start;
            if (!Arrange_Grow(&s, 0, k, shape, 7u)) { ok = false; printf("  grow refused: %s shape %d\n", label, g); continue; }
            again = start;
            Arrange_Grow(&again, 0, k, shape, 7u);
            if (memcmp(&s, &again, sizeof(Song)) != 0) same = false;

            if (s.patternCount != 1 + Arrange_PatternsNeeded(shape) || s.orderCount != Arrange_PassCount(shape)) counts = false;
            int lead = shape == GROW_WITH_ENDING ? 1 : 0;
            if (s.order[lead] != 0) first = false;
            if (arrange_CountIn(&s, 0) < 2) twice = false;
            if (memcmp(&s.patterns[0], &start.patterns[0], sizeof(SongPattern)) != 0) unchanged = false;
            for (int p = 0; p < s.patternCount; p++) if (arrange_CountIn(&s, p) == 0) used = false;
            for (int o = 0; o < s.orderCount; o++) if (s.order[o] >= s.patternCount) used = false;
            if (strcmp(s.name, start.name) != 0 || s.bpm != start.bpm || s.swing != start.swing || s.keyRoot != start.keyRoot ||
                s.keyScale != start.keyScale || memcmp(s.wave, start.wave, sizeof(s.wave)) != 0 || memcmp(s.volume, start.volume, sizeof(s.volume)) != 0)
                settings = false;

            for (int p = 0; p < s.patternCount; p++) {
                if (!arrange_Backed(&s.patterns[p])) { backed = false; printf("  grow backing: %s shape %d pattern %d\n", label, g, p); }
                if (!arrange_InKey(&s.patterns[p], k)) { inKey = false; printf("  grow key: %s shape %d pattern %d\n", label, g, p); }
            }

            /* Passes scale with the tempo, so the length is judged at 120 bpm. */
            Song at120 = s;
            at120.bpm = 120;
            float secs = Song_Seconds(&at120), target = Arrange_TargetSeconds(shape);
            if (secs < target * 0.75f || secs > target * 1.25f) { seconds = false; printf("  grow length %.0f s for %.0f\n", secs, target); }

            const SongPattern *firstPart = &s.patterns[s.order[0]], *lastPart = &s.patterns[s.order[s.orderCount - 1]];
            if (shape == GROW_WITH_ENDING) {
                if (!arrange_LooksLikeIntro(firstPart) || !arrange_LooksLikeOutro(lastPart) ||
                    arrange_ReadChords(lastPart, k).degree[3] != 0) { ending = false; printf("  ending: %s\n", label); }
            } else {
                for (int o = 0; o < s.orderCount; o++) {
                    const SongPattern *p = &s.patterns[s.order[o]];
                    if (arrange_LooksLikeIntro(p) || arrange_LooksLikeOutro(p)) { introOutro = false; printf("  loop has intro or outro: %s shape %d pos %d\n", label, g, o); }
                }
                int d = arrange_ReadChords(lastPart, k).degree[3];
                if (d != 4 && d != 0) { turn = false; printf("  turnaround ends on %d: %s\n", d, label); }
            }

            if (shape == GROW_GAME_LOOP) {
                /* A A B A A' B B' T: position 5 is the chorus itself. */
                const SongPattern *b = &s.patterns[s.order[5]];
                ComposeChords bc = arrange_ReadChords(b, k);
                if (!arrange_SameMask(b->tone[TRACK_BASS], start.patterns[0].tone[TRACK_BASS], SONG_STEPS) ||
                    !arrange_SameMask(b->tone[TRACK_ARP], start.patterns[0].tone[TRACK_ARP], SONG_STEPS) ||
                    !arrange_SameChords(&bc, answer) || arrange_TrackEq(b, &start.patterns[0], TRACK_LEAD)) { chorus = false; printf("  chorus: %s\n", label); }
            }
            if (!arrange_RoundTrip(&s)) { trip = false; printf("  grow round trip: %s shape %d\n", label, g); }
        }
    }
    CHECK(ok, "grow: every shape grows every theme and recipe");
    CHECK(counts, "grow: pattern and order counts match the shape");
    CHECK(first, "grow: the source comes first (after the intro in a song with an ending)");
    CHECK(twice, "grow: the source plays at least twice");
    CHECK(unchanged, "grow: the source pattern is untouched");
    CHECK(used, "grow: every pattern is in the order and every entry points at one");
    CHECK(settings, "grow: name, tempo, swing, key, waves and volumes are left alone");
    CHECK(backed, "grow: every part has bass and arp in every bar");
    CHECK(inKey, "grow: every note of every part in key and in range");
    CHECK(seconds, "grow: the length is within 25% of the shape's target at 120 bpm");
    CHECK(introOutro, "grow: no loop shape has an intro or an outro");
    CHECK(ending, "grow: a song with an ending opens on the intro and ends on the tonic");
    CHECK(turn, "grow: a loop's last part ends on V or I, so the wrap lands");
    CHECK(chorus, "grow: the chorus keeps the backing's rhythm on the answer progression with a new tune");
    CHECK(same, "grow: deterministic per seed");
    CHECK(trip, "grow: the grown song saves and loads back byte for byte");

    /* Different seeds give different songs: the takes that use a seed (the
     * chorus tune, the fills, the lift) must not collapse onto a few. */
    enum { SEEDS = 8 };
    static Song takes[SEEDS];
    int pairs = 0, differ = 0;
    for (int i = 0; i < arrange_SourceCount(); i += 3) {
        ComposeKey k;
        char label[64];
        arrange_Source(i, &start, &k, label, sizeof(label));
        for (int g = 0; g < GROW_COUNT; g++) {
            for (int n = 0; n < SEEDS; n++) {
                takes[n] = start;
                Arrange_Grow(&takes[n], 0, k, (GrowShape)g, 1000u + (uint32_t)n);
            }
            for (int a = 0; a < SEEDS; a++) {
                for (int b = a + 1; b < SEEDS; b++) {
                    pairs++;
                    differ += memcmp(&takes[a], &takes[b], sizeof(Song)) != 0;
                }
            }
        }
    }
    CHECK(pairs > 0 && differ * 10 >= pairs * 9, "grow: different seeds give different songs in at least 90% of pairs");
    if (differ * 10 < pairs * 9) printf("  %d of %d seed pairs differ\n", differ, pairs);
}

/* --------------------------------------------------------------- refusals */

static void arrange_test_refuse(void) {
    static Song s, before;
    ComposeKey k;
    char label[64];
    bool full = true, fits = true;
    for (int g = 0; g < GROW_COUNT; g++) {
        GrowShape shape = (GrowShape)g;
        int needed = Arrange_PatternsNeeded(shape);
        /* One pattern too many: the song must come back byte for byte. */
        arrange_Source(1, &s, &k, label, sizeof(label));
        while (s.patternCount < SONG_MAX_PATTERNS - needed + 1) Song_DuplicatePattern(&s, 0, NULL);
        before = s;
        if (Arrange_Grow(&s, 0, k, shape, 3u) || memcmp(&s, &before, sizeof(Song)) != 0) full = false;
        /* Exactly enough room: it grows to the very last slot. */
        arrange_Source(1, &s, &k, label, sizeof(label));
        while (s.patternCount < SONG_MAX_PATTERNS - needed) Song_DuplicatePattern(&s, 0, NULL);
        int last = s.patternCount - 1;
        if (!Arrange_Grow(&s, last, k, shape, 3u) || s.patternCount != SONG_MAX_PATTERNS) fits = false;
    }
    CHECK(full, "refuse: a song without room for the new patterns is left byte for byte");
    CHECK(fits, "refuse: a song with exactly enough room grows to SONG_MAX_PATTERNS");

    arrange_Source(2, &s, &k, label, sizeof(label));
    before = s;
    bool bad = true;
    if (Arrange_Grow(&s, -1, k, GROW_GAME_LOOP, 1u)) bad = false;
    if (Arrange_Grow(&s, s.patternCount, k, GROW_GAME_LOOP, 1u)) bad = false;
    if (Arrange_Grow(&s, 0, k, GROW_COUNT, 1u)) bad = false;
    if (Arrange_Grow(&s, 0, k, (GrowShape)-1, 1u)) bad = false;
    if (Arrange_Grow(NULL, 0, k, GROW_GAME_LOOP, 1u)) bad = false;
    CHECK(bad && memcmp(&s, &before, sizeof(Song)) == 0, "refuse: a bad source, shape or song changes nothing");
    s.patternCount = 0;
    before = s;
    CHECK(!Arrange_Grow(&s, 0, k, GROW_SHORT, 1u) && memcmp(&s, &before, sizeof(Song)) == 0, "refuse: a song with no patterns changes nothing");

    /* The order is rewritten, not appended to, so a full order is no bar:
     * it shrinks to the pass list and the entries past it are cleared. */
    arrange_Source(2, &s, &k, label, sizeof(label));
    Song_DuplicatePattern(&s, 0, NULL);
    while (s.orderCount < SONG_MAX_ORDER) Song_OrderInsert(&s, s.orderCount, 1);
    bool grew = Arrange_Grow(&s, 0, k, GROW_GAME_LOOP, 1u);
    bool cleared = true;
    for (int i = s.orderCount; i < SONG_MAX_ORDER; i++) if (s.order[i] != 0) cleared = false;
    CHECK(grew && s.orderCount == Arrange_PassCount(GROW_GAME_LOOP) && cleared && arrange_RoundTrip(&s),
          "refuse: a full order still grows, and the stale entries are cleared");
}

/* A song that already has parts: growing one of them adds after them and
 * leaves the others alone. */
static void arrange_test_existing(void) {
    static Song s, before;
    ComposeKey k;
    char label[64];
    arrange_Source(4, &s, &k, label, sizeof(label));
    int idx;
    Song_DuplicatePattern(&s, 0, &idx);
    Compose_Drums(&s.patterns[idx], 3, 0u);
    Song_AddPattern(&s, &idx);
    Song_OrderInsert(&s, 1, 1);
    Song_OrderInsert(&s, 2, 2);
    Song_OrderInsert(&s, 3, 1);
    before = s;
    bool grew = Arrange_Grow(&s, 1, k, GROW_GAME_LOOP, 9u);
    bool kept = true;
    for (int p = 0; p < before.patternCount; p++) if (memcmp(&s.patterns[p], &before.patterns[p], sizeof(SongPattern)) != 0) kept = false;
    CHECK(grew && kept, "existing: growing pattern 1 keeps patterns 0 to 2 as they were");
    CHECK(s.patternCount == before.patternCount + Arrange_PatternsNeeded(GROW_GAME_LOOP) && s.order[0] == 1 && s.order[3] == 1,
          "existing: the new parts go after the old ones and the order starts on the source");
    bool newOnly = true;
    for (int o = 0; o < s.orderCount; o++) if (s.order[o] != 1 && s.order[o] < before.patternCount) newOnly = false;
    CHECK(newOnly, "existing: the order plays only the source and its new parts");
}

/* Hand-made and imported patterns: random notes anywhere in the range, out
 * of key, holds and rests in any order, empty tracks, any beat. The musical
 * promises above are for patterns the composer could have written; these
 * only have to come out playable: no crash, every note in range, a grow that
 * succeeds and saves back byte for byte. */
static uint32_t arrange_Lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static bool arrange_InRange(const SongPattern *p) {
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        for (int st = 0; st < SONG_STEPS; st++) {
            int n = p->tone[t][st];
            if (n != NOTE_NONE && n != NOTE_HOLD && (n < NOTE_MIN || n > NOTE_MAX)) return false;
        }
    }
    for (int st = 0; st < SONG_STEPS; st++) if (p->drums[st] & ~(DRUM_KICK | DRUM_SNARE | DRUM_HAT)) return false;
    return true;
}

static void arrange_test_handmade(void) {
    static Song s, g;
    uint32_t rng = 2024u;
    bool range = true, grew = true, trip = true;
    for (int it = 0; it < 200; it++) {
        Song_Init(&s, "Hand made song");
        SongPattern *p = &s.patterns[0];
        int density = (int)(arrange_Lcg(&rng) % 4);
        for (int t = 0; t < SONG_TONE_TRACKS; t++) {
            bool empty = arrange_Lcg(&rng) % 6 == 0;
            for (int st = 0; st < SONG_STEPS; st++) {
                int r = (int)(arrange_Lcg(&rng) % 8);
                if (empty || r > density + 2) p->tone[t][st] = NOTE_NONE;
                else if (r == 0) p->tone[t][st] = NOTE_HOLD;
                else p->tone[t][st] = (int8_t)(NOTE_MIN + (int)(arrange_Lcg(&rng) % (NOTE_MAX - NOTE_MIN + 1)));
            }
        }
        for (int st = 0; st < SONG_STEPS; st++) p->drums[st] = (uint8_t)((int)(arrange_Lcg(&rng) % 8) < density ? arrange_Lcg(&rng) % 8 : 0);
        ComposeKey k = {(int)(arrange_Lcg(&rng) % 12), (int)(arrange_Lcg(&rng) % SONG_SCALE_COUNT)};
        for (int kind = 0; kind < VARY_COUNT; kind++) {
            SongPattern out;
            Arrange_VaryInto(p, k, 0, NULL, (VaryKind)kind, arrange_Lcg(&rng), (int)(arrange_Lcg(&rng) % 9) - 1, &out);
            if (!arrange_InRange(&out)) range = false;
        }
        SongPattern chorus;
        Arrange_Chorus(p, k, 0, NULL, NULL, arrange_Lcg(&rng), &chorus);
        if (!arrange_InRange(&chorus)) range = false;
        g = s;
        if (!Arrange_Grow(&g, 0, k, (GrowShape)(it % GROW_COUNT), arrange_Lcg(&rng))) { grew = false; continue; }
        for (int i = 0; i < g.patternCount; i++) if (!arrange_InRange(&g.patterns[i])) range = false;
        if (!arrange_RoundTrip(&g)) trip = false;
    }
    CHECK(range, "hand made: every kind, the chorus and every grown part keep every note in range");
    CHECK(grew, "hand made: a one-pattern song of any notes grows into every shape");
    CHECK(trip, "hand made: the grown songs save and load back byte for byte");
}

/* Tunes that sit high or low in their octave. A recipe tune written at
 * leadOctave 3 averages 18..35 semitones over the tonic as a rule, but a few
 * in a thousand sit above or below that, and Compose_LeadOctave reads those
 * by their average, as 4 or 2, so the chorus is written an octave from the
 * one the verse was. At a tonic of 49 or more a chorus an octave up reaches
 * past C8: the writers once clamped it there, and a D major recipe (Hero's
 * anthem, melody 583) grew a C8 into its chorus and saved it. Every root
 * whose rolled tonic is 49..51, every scale and story and melodies 1..999,
 * built as the recipe screen builds them: each tune that sits off its
 * octave is grown into a game loop that must stay in key, with its chorus
 * still near the verse. Reading such a tune at the octave it was written at
 * instead puts the chorus about 11 semitones off the verse on average, and
 * capping the octave so nothing could clamp about 9; read by its average it
 * is about 3 off, as close as any other recipe's chorus. */
static double arrange_LeadMean(const int8_t *notes) {
    double sum = 0;
    int n = 0;
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] >= 0) { sum += notes[st]; n++; }
    return n ? sum / n : 0.0;
}

static void arrange_test_off_octave(void) {
    static Song s;
    int found[2] = {0, 0};
    double offSum[2] = {0, 0};
    bool inKey = true, example = false;
    SongPattern lead;
    Song_ClearPattern(&lead);
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            int tonic = Compose_TonicMidi(k, NULL);
            if (tonic < 49) continue;
            for (int story = 0; story < Compose_ProgressionCount(); story++) {
                const ComposeChords *c = &Compose_ProgressionAt(story)->chords;
                for (uint32_t melody = 1; melody <= 999; melody++) {
                    Compose_Lead(&lead, k, tonic, c, 3, melody);
                    double sits = arrange_LeadMean(lead.tone[TRACK_LEAD]) - tonic;
                    if (sits >= 18.0 && sits <= 35.0) continue;
                    int high = sits > 35.0;
                    Song_Init(&s, "Recipe song");
                    SongPattern *p = &s.patterns[0];
                    *p = lead;
                    Compose_Bass(p, k, tonic, c, 0);
                    Compose_Arp(p, k, tonic, c, 0);
                    Compose_Drums(p, 0, 0u);
                    if (!Arrange_Grow(&s, 0, k, GROW_GAME_LOOP, melody * 2654435761u)) { inKey = false; continue; }
                    found[high]++;
                    if (root == 2 && scale == SCALE_MAJOR && story == 0 && melody == 583 && high) example = true;
                    for (int i = 0; i < s.patternCount; i++) {
                        if (!arrange_InKey(&s.patterns[i], k)) {
                            if (inKey) printf("  off octave: root %d scale %d story %d melody %u, pattern %d out of key\n", root, scale, story, melody, i);
                            inKey = false;
                        }
                    }
                    /* Game loop appends Af Bf A' B B' T: pattern 4 is the chorus. */
                    double off = arrange_LeadMean(s.patterns[4].tone[TRACK_LEAD]) - arrange_LeadMean(p->tone[TRACK_LEAD]);
                    offSum[high] += off < 0 ? -off : off;
                }
            }
        }
    }
    CHECK(found[0] >= 50 && found[1] >= 50 && example, "off octave: the sweep meets tunes sitting low and high, the D major melody 583 among them");
    CHECK(inKey, "off octave: a chorus at a high tonic keeps every note in key and in range");
    bool near = true;
    for (int h = 0; h < 2; h++) {
        if (found[h] > 0 && offSum[h] / found[h] <= 5.0) continue;
        near = false;
        printf("  chorus of tunes sitting %s: %.2f semitones off the verse on average over %d songs\n", h ? "high" : "low", found[h] ? offSum[h] / found[h] : 0.0, found[h]);
    }
    CHECK(near, "off octave: the chorus sits near the verse it answers");
}

void test_arrange_all(void) {
    arrange_test_names();
    arrange_test_answer();
    arrange_test_chorus();
    arrange_test_vary();
    arrange_test_grow();
    arrange_test_refuse();
    arrange_test_existing();
    arrange_test_handmade();
    arrange_test_off_octave();
}
