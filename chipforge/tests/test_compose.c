/* Tests for the composer (compose.h): the theme lock that pins its port of
 * Music_ThemeToSong, the musical invariants of every style over the whole
 * key and progression space, and the key tools. Compiled by inclusion from
 * test_main.c. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"

/* ---------------------------------------------------------------- helpers */

static ComposeKey compose_ThemeKey(const MusicTheme *t) {
    ComposeKey k = {t->rootMidi % 12, (int)t->scale};
    return k;
}

static ComposeChords compose_ThemeChords(const MusicTheme *t) {
    ComposeChords c;
    for (int b = 0; b < 4; b++) c.degree[b] = t->prog[b];
    return c;
}

static bool compose_TracksEqual(const SongPattern *a, const SongPattern *b, int track) {
    if (track == TRACK_DRUMS) return memcmp(a->drums, b->drums, sizeof(a->drums)) == 0;
    return memcmp(a->tone[track], b->tone[track], sizeof(a->tone[track])) == 0;
}

/* Every track but `except` is byte-identical between the two patterns. */
static bool compose_OthersEqual(const SongPattern *a, const SongPattern *b, int except) {
    for (int t = 0; t < SONG_TRACKS; t++) if (t != except && !compose_TracksEqual(a, b, t)) return false;
    return true;
}

/* The note/hold/none shape of a tone track, ignoring which note. */
static bool compose_SameMask(const int8_t *a, const int8_t *b) {
    for (int st = 0; st < SONG_STEPS; st++) {
        int ca = a[st] >= 0 ? 1 : a[st], cb = b[st] >= 0 ? 1 : b[st];
        if (ca != cb) return false;
    }
    return true;
}

static bool compose_SentenceCase(const char *s, int maxGlyphs) {
    if (!s || !s[0] || !isupper((unsigned char)s[0])) return false;
    int n = 0;
    for (const char *p = s; *p; p++, n++) {
        if ((unsigned char)*p > 127) return false; /* ASCII only, so no em-dash can hide here */
        if (p > s && isupper((unsigned char)*p)) return false;
    }
    return n <= maxGlyphs;
}

/* "bVII" -> 6, "iii" -> 2; -1 for anything else. */
static int compose_Numeral(const char *tok) {
    if (*tok == 'b' || *tok == '#') tok++;
    static const char *const names[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    char low[8];
    size_t n = 0;
    while (tok[n] && n < sizeof(low) - 1) { low[n] = (char)tolower((unsigned char)tok[n]); n++; }
    low[n] = '\0';
    for (int d = 0; d < 7; d++) if (strcmp(low, names[d]) == 0) return d;
    return -1;
}

/* ------------------------------------------------------------------ tests */

static void compose_test_theme_lock(void) {
    bool locked = true, keyed = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song theme;
        Music_ThemeToSong(t, &theme);
        if (theme.keyRoot != t->rootMidi % 12 || theme.keyScale != (int)t->scale) keyed = false;
        SongPattern p;
        Song_ClearPattern(&p);
        ComposeKey k = compose_ThemeKey(t);
        ComposeChords c = compose_ThemeChords(t);
        Compose_Bass(&p, k, t->rootMidi, &c, t->bassStyle);
        Compose_Arp(&p, k, t->rootMidi, &c, t->arpStyle);
        Compose_Drums(&p, t->drumStyle, 0xDEADBEEFu); /* styles 0..3 ignore the seed */
        Compose_Lead(&p, k, t->rootMidi, &c, t->leadOctave, (uint32_t)t->leadSeed);
        if (memcmp(&p, &theme.patterns[0], sizeof(SongPattern)) != 0) {
            locked = false;
            printf("  theme lock broke on %s\n", t->slug);
        }
    }
    CHECK(locked, "theme lock: styles 0..3 and the lead reproduce all twelve themes byte for byte");
    CHECK(keyed, "theme lock: Music_ThemeToSong records the theme's key");
}

static void compose_test_own_track(void) {
    bool own = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song theme;
        Music_ThemeToSong(t, &theme);
        ComposeKey k = compose_ThemeKey(t);
        ComposeChords c = compose_ThemeChords(t);
        for (int style = 0; style < 6; style++) {
            SongPattern p = theme.patterns[0];
            Compose_Bass(&p, k, t->rootMidi, &c, style);
            if (!compose_OthersEqual(&p, &theme.patterns[0], TRACK_BASS)) own = false;
            p = theme.patterns[0];
            Compose_Arp(&p, k, t->rootMidi, &c, style);
            if (!compose_OthersEqual(&p, &theme.patterns[0], TRACK_ARP)) own = false;
            p = theme.patterns[0];
            Compose_Drums(&p, style, (uint32_t)style * 77u);
            if (!compose_OthersEqual(&p, &theme.patterns[0], TRACK_DRUMS)) own = false;
        }
        SongPattern p = theme.patterns[0];
        Compose_Lead(&p, k, t->rootMidi, &c, t->leadOctave, 4242u);
        if (!compose_OthersEqual(&p, &theme.patterns[0], TRACK_LEAD)) own = false;
        p = theme.patterns[0];
        Compose_LeadPitches(&p, k, t->rootMidi, &c, t->leadOctave, 4242u);
        if (!compose_OthersEqual(&p, &theme.patterns[0], TRACK_LEAD)) own = false;
    }
    CHECK(own, "writers: each one changes only its own track");
}

static void compose_test_styles_everywhere(void) {
    bool inRange = true, inKey = true, bassRoot = true, bassOnsets = true, arpOnsets = true;
    bool drumStart = true, drumAdjacent = true, drumCount = true, leadOk = true;
    int bassStyles = Compose_StyleCount(TRACK_BASS), arpStyles = Compose_StyleCount(TRACK_ARP), drumStyles = Compose_StyleCount(TRACK_DRUMS);
    /* Every tonic Compose_TonicMidi can return (36..60, not only the recipe's
     * 40..51): Fill reads it from the song, so a low arp or bass puts the
     * walking bass's approach note under NOTE_MIN. */
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            for (int tonic = 36 + root; tonic <= 60; tonic += 12) /* 36 is a C */
            for (int pi = 0; pi < Compose_ProgressionCount(); pi++) {
                const ComposeChords *c = &Compose_ProgressionAt(pi)->chords;
                for (int style = 0; style < bassStyles; style++) {
                    SongPattern p;
                    Song_ClearPattern(&p);
                    Compose_Bass(&p, k, tonic, c, style);
                    for (int bar = 0; bar < 4; bar++) {
                        int onsets = 0;
                        int first = p.tone[TRACK_BASS][bar * 16];
                        if (first < 0 || first % 12 != Compose_DegreeNote(k, tonic, c->degree[bar]) % 12) bassRoot = false;
                        for (int i = 0; i < 16; i++) {
                            int n = p.tone[TRACK_BASS][bar * 16 + i];
                            if (n < 0) continue;
                            onsets++;
                            if (n < NOTE_MIN || n > NOTE_MAX) inRange = false;
                            if (!Compose_InKey(k, n)) inKey = false;
                        }
                        if (onsets < 1) bassOnsets = false;
                    }
                }
                for (int style = 0; style < arpStyles; style++) {
                    SongPattern p;
                    Song_ClearPattern(&p);
                    Compose_Arp(&p, k, tonic, c, style);
                    for (int bar = 0; bar < 4; bar++) {
                        int onsets = 0;
                        for (int i = 0; i < 16; i++) {
                            int n = p.tone[TRACK_ARP][bar * 16 + i];
                            if (n < 0) continue;
                            onsets++;
                            if (n < NOTE_MIN || n > NOTE_MAX) inRange = false;
                            if (!Compose_InKey(k, n)) inKey = false;
                        }
                        if (onsets < 4) arpOnsets = false;
                    }
                }
                /* The lead, New tune and Fit my rhythm on its rhythm: two
                 * seeds and every octave Compose_LeadOctave can return, which
                 * Fill, Vary and Grow pass straight through, so a tune typed
                 * high puts its upper degrees past NOTE_MAX. */
                for (int oct = 1; oct <= 5; oct++) {
                    for (uint32_t seed = 1; seed <= 2; seed++) {
                        SongPattern p;
                        Song_ClearPattern(&p);
                        Compose_Lead(&p, k, tonic, c, oct, seed * 1000u + (uint32_t)pi);
                        for (int pass = 0; pass < 2; pass++) {
                            if (pass == 1) Compose_LeadPitches(&p, k, tonic, c, oct, seed);
                            int onsets = 0;
                            for (int st = 0; st < SONG_STEPS; st++) {
                                int n = p.tone[TRACK_LEAD][st];
                                if (n < 0) continue;
                                onsets++;
                                if (n < NOTE_MIN || n > NOTE_MAX) inRange = false;
                                if (!Compose_InKey(k, n)) inKey = false;
                                if (st % 4 == 0 && !Compose_IsChordTone(k, c->degree[st / 16], n)) leadOk = false;
                            }
                            if (onsets < 4) leadOk = false;
                        }
                    }
                }
            }
        }
    }
    /* Drums do not depend on the key; every style over a spread of seeds. */
    for (int style = 0; style < drumStyles; style++) {
        for (uint32_t seed = 0; seed < 64; seed++) {
            SongPattern p;
            Song_ClearPattern(&p);
            Compose_Drums(&p, style, seed * 2654435761u);
            for (int bar = 0; bar < 4; bar++) {
                if (p.drums[bar * 16] == 0) drumStart = false;
                int kicks = 0;
                for (int i = 0; i < 16; i++) if (p.drums[bar * 16 + i] & DRUM_KICK) kicks++;
                if (kicks > 6) drumCount = false;
            }
            for (int st = 1; st < SONG_STEPS; st++) if ((p.drums[st] & DRUM_KICK) && (p.drums[st - 1] & DRUM_KICK)) drumAdjacent = false;
        }
    }
    CHECK(inRange, "styles: every note of every style in every key, progression, tonic 36..60 and lead octave 1..5 is within NOTE_MIN..NOTE_MAX");
    CHECK(inKey, "styles: every note is in key, at the range ends too (an octave fold, never a clamp onto C1 or C8)");
    CHECK(bassRoot, "bass: a note on each bar's first step with the chord root's pitch class");
    CHECK(bassOnsets, "bass: at least one onset a bar");
    CHECK(arpOnsets, "arp: at least four onsets a bar");
    CHECK(leadOk, "lead: strong beats are chord tones and a tune has at least four notes");
    CHECK(drumStart, "drums: a hit on step 0 of every bar");
    CHECK(drumAdjacent, "drums: never kicks on adjacent sixteenths");
    CHECK(drumCount, "drums: at most six kicks a bar");
}

static void compose_test_lead_pitches(void) {
    bool maskKept = true, strongTones = true, endsRoot = true, deterministic = true, inKey = true;
    int distinctMin = 99;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song theme;
        Music_ThemeToSong(t, &theme);
        ComposeKey k = compose_ThemeKey(t);
        ComposeChords c = compose_ThemeChords(t);
        /* A rhythm of the user's own: the theme's mask with a hold after every
         * note that has room, so holds are part of what must survive. */
        SongPattern base = theme.patterns[0];
        for (int st = 0; st + 1 < SONG_STEPS; st++) if (base.tone[TRACK_LEAD][st] >= 0 && base.tone[TRACK_LEAD][st + 1] == NOTE_NONE) base.tone[TRACK_LEAD][st + 1] = NOTE_HOLD;
        SongPattern results[8];
        for (uint32_t seed = 0; seed < 8; seed++) {
            SongPattern p = base, again = base;
            Compose_LeadPitches(&p, k, t->rootMidi, &c, t->leadOctave, seed * 31u + 7u);
            Compose_LeadPitches(&again, k, t->rootMidi, &c, t->leadOctave, seed * 31u + 7u);
            if (memcmp(&p, &again, sizeof(p)) != 0) deterministic = false;
            if (!compose_SameMask(p.tone[TRACK_LEAD], base.tone[TRACK_LEAD])) maskKept = false;
            int last = -1;
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = p.tone[TRACK_LEAD][st];
                if (n < 0) continue;
                last = st;
                if (!Compose_InKey(k, n)) inKey = false;
                if (st % 4 == 0 && !Compose_IsChordTone(k, c.degree[st / 16], n)) strongTones = false;
            }
            if (last < 0 || p.tone[TRACK_LEAD][last] % 12 != Compose_DegreeNote(k, t->rootMidi, c.degree[last / 16]) % 12) endsRoot = false;
            results[seed] = p;
        }
        int distinct = 0;
        for (int a = 0; a < 8; a++) {
            bool seen = false;
            for (int b = 0; b < a; b++) if (memcmp(&results[a], &results[b], sizeof(SongPattern)) == 0) seen = true;
            if (!seen) distinct++;
        }
        if (distinct < distinctMin) distinctMin = distinct;
    }
    CHECK(maskKept, "fit my rhythm: the note/hold/none mask is kept exactly");
    CHECK(strongTones, "fit my rhythm: every note on a step that is a multiple of 4 is a chord tone");
    CHECK(endsRoot, "fit my rhythm: the last note lands on its bar's chord root");
    CHECK(inKey, "fit my rhythm: every note is in key");
    CHECK(deterministic, "fit my rhythm: the same seed gives the same tune");
    CHECK(distinctMin >= 4, "fit my rhythm: eight seeds give at least four different tunes");

    /* An empty lead stays empty; a single note becomes the chord root. */
    SongPattern p;
    Song_ClearPattern(&p);
    ComposeKey am = {9, SCALE_MINOR};
    ComposeChords c = {{0, 5, 3, 4}};
    Compose_LeadPitches(&p, am, 45, &c, 2, 1u);
    bool empty = true;
    for (int st = 0; st < SONG_STEPS; st++) if (p.tone[TRACK_LEAD][st] != NOTE_NONE) empty = false;
    CHECK(empty, "fit my rhythm: an empty lead stays empty");
    p.tone[TRACK_LEAD][18] = 60;
    Compose_LeadPitches(&p, am, 45, &c, 2, 1u);
    CHECK(p.tone[TRACK_LEAD][18] >= 0 && p.tone[TRACK_LEAD][18] % 12 == Compose_DegreeNote(am, 45, 5) % 12, "fit my rhythm: a lone note becomes its bar's chord root");
}

static void compose_test_guess_key(void) {
    int exact = 0, transposed = 0, chordsRead = 0, tonicOk = 0, tonicExact = 0;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song s;
        Music_ThemeToSong(t, &s);
        s.keyRoot = -1; /* the guess must come from the notes */
        ComposeKey g;
        if (Compose_GuessKey(&s, &g) && g.root == t->rootMidi % 12 && g.scale == (int)t->scale) exact++;
        else printf("  guessed %s as root %d scale %d (wanted %d %d)\n", t->slug, g.root, g.scale, t->rootMidi % 12, (int)t->scale);

        Song up = s;
        for (int tr = 0; tr < SONG_TONE_TRACKS; tr++) for (int st = 0; st < SONG_STEPS; st++) if (up.patterns[0].tone[tr][st] >= 0) up.patterns[0].tone[tr][st] = (int8_t)(up.patterns[0].tone[tr][st] + 5);
        if (Compose_GuessKey(&up, &g) && g.root == (t->rootMidi + 5) % 12 && g.scale == (int)t->scale) transposed++;

        ComposeKey k = compose_ThemeKey(t);
        ComposeChords c;
        if (Compose_ReadChords(&s.patterns[0], k, &c)) {
            for (int b = 0; b < 4; b++) if (c.degree[b] == t->prog[b]) chordsRead++;
        }
        int tonic = Compose_TonicMidi(k, &s.patterns[0]);
        if (tonic >= 36 && tonic <= 60 && tonic % 12 == k.root) tonicOk++;
        /* The theme's bass sits an octave under rootMidi, so the pattern
         * places the tonic back in the theme's own register. */
        if (tonic == t->rootMidi) tonicExact++;
        else printf("  tonic for %s read as %d, theme has %d\n", t->slug, tonic, t->rootMidi);
    }
    CHECK(exact == Music_ThemeCount(), "guess key: exact on all twelve themes");
    CHECK(transposed == Music_ThemeCount(), "guess key: a theme moved up five semitones guesses root + 5");
    CHECK(chordsRead == 4 * Music_ThemeCount(), "read chords: all 48 theme bar roots recovered from the bass");
    CHECK(tonicOk == Music_ThemeCount(), "tonic: within 36..60 with the key's pitch class for every theme");
    CHECK(tonicExact == Music_ThemeCount(), "tonic: every theme's own register is recovered from its pattern");

    /* The owner's own file shape: drums only. */
    Song drums;
    Song_Init(&drums, "Beat");
    for (int st = 0; st < SONG_STEPS; st += 4) drums.patterns[0].drums[st] = DRUM_KICK | DRUM_HAT;
    ComposeKey g = {3, 3};
    CHECK(!Compose_GuessKey(&drums, &g), "guess key: false for a drums-only song");
    bool guessed = false;
    ComposeKey sk = Compose_SongKey(&drums, &guessed);
    CHECK(guessed && sk.root == 9 && sk.scale == SCALE_MINOR, "song key: a song with nothing to go by is treated as A minor, marked guessed");
    drums.keyRoot = 2;
    drums.keyScale = SCALE_MAJOR;
    sk = Compose_SongKey(&drums, &guessed);
    CHECK(!guessed && sk.root == 2 && sk.scale == SCALE_MAJOR, "song key: a stored key is used as it is");
    ComposeChords c;
    CHECK(!Compose_ReadChords(&drums.patterns[0], sk, &c) && c.degree[0] == 0 && c.degree[3] == 0, "read chords: nothing to read gives false and degree 0 everywhere");

    /* Chords from the arp alone, when there is no bass. */
    Song arpOnly;
    Music_ThemeToSong(Music_FindTheme("ghostarcade"), &arpOnly);
    memset(arpOnly.patterns[0].tone[TRACK_BASS], NOTE_NONE, SONG_STEPS);
    ComposeKey k = compose_ThemeKey(Music_FindTheme("ghostarcade"));
    bool fromArp = Compose_ReadChords(&arpOnly.patterns[0], k, &c);
    const int *prog = Music_FindTheme("ghostarcade")->prog;
    CHECK(fromArp && c.degree[0] == prog[0] && c.degree[1] == prog[1] && c.degree[2] == prog[2] && c.degree[3] == prog[3], "read chords: the arp names the chords when the bass is silent");
    /* Every arp style on every progression reads back, bass silent. */
    bool arpStyles = true;
    for (int style = 0; style < Compose_StyleCount(TRACK_ARP); style++) {
        for (int pi = 0; pi < Compose_ProgressionCount(); pi++) {
            const ComposeChords *want = &Compose_ProgressionAt(pi)->chords;
            SongPattern p;
            Song_ClearPattern(&p);
            Compose_Arp(&p, k, 45, want, style);
            ComposeChords got;
            if (!Compose_ReadChords(&p, k, &got)) arpStyles = false;
            for (int b = 0; b < 4; b++) if (got.degree[b] != want->degree[b]) arpStyles = false;
        }
    }
    CHECK(arpStyles, "read chords: every arp style on every progression reads back exactly");
    /* And every bass style. */
    bool bassStyles = true;
    for (int style = 0; style < Compose_StyleCount(TRACK_BASS); style++) {
        for (int pi = 0; pi < Compose_ProgressionCount(); pi++) {
            const ComposeChords *want = &Compose_ProgressionAt(pi)->chords;
            SongPattern p;
            Song_ClearPattern(&p);
            Compose_Bass(&p, k, 45, want, style);
            ComposeChords got;
            if (!Compose_ReadChords(&p, k, &got)) bassStyles = false;
            for (int b = 0; b < 4; b++) if (got.degree[b] != want->degree[b]) bassStyles = false;
        }
    }
    CHECK(bassStyles, "read chords: every bass style on every progression reads back exactly");

    /* Tonic without a pattern: the recipe register. */
    bool recipe = true;
    for (int root = 0; root < 12; root++) {
        ComposeKey rk = {root, SCALE_MAJOR};
        int tonic = Compose_TonicMidi(rk, NULL);
        if (tonic < 40 || tonic > 51 || tonic % 12 != root) recipe = false;
    }
    CHECK(recipe, "tonic: without a pattern the tonic is in 40..51 with the key's pitch class");
}

static void compose_test_key_tools(void) {
    bool idem = true, snapInKey = true, stepBack = true, stepInKey = true, stepRises = true;
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            for (int n = NOTE_MIN; n <= NOTE_MAX; n++) {
                int s1 = Compose_SnapToKey(k, n);
                if (s1 < NOTE_MIN || s1 > NOTE_MAX || !Compose_InKey(k, s1)) snapInKey = false;
                if (Compose_SnapToKey(k, s1) != s1) idem = false;
                if (Compose_InKey(k, n) && s1 != n) idem = false;
                if (n >= 36 && n <= 96 && Compose_InKey(k, n)) {
                    int up = Compose_ScaleStep(k, n, 1);
                    if (up <= n || !Compose_InKey(k, up)) stepRises = false;
                    if (Compose_ScaleStep(k, up, -1) != n) stepBack = false;
                    if (Compose_ScaleStep(k, Compose_ScaleStep(k, n, 3), -3) != n) stepBack = false;
                }
                int far = Compose_ScaleStep(k, n, 200);
                if (far < NOTE_MIN || far > NOTE_MAX || !Compose_InKey(k, far)) stepInKey = false;
                far = Compose_ScaleStep(k, n, -200);
                if (far < NOTE_MIN || far > NOTE_MAX || !Compose_InKey(k, far)) stepInKey = false;
            }
        }
    }
    CHECK(idem, "snap to key: idempotent and leaves in-key notes alone");
    CHECK(snapInKey, "snap to key: the result is in key and in range for every note");
    CHECK(stepRises, "scale step: +1 goes up to an in-key note");
    CHECK(stepBack, "scale step: +1 then -1 (and +3 then -3) returns to the start for in-key notes");
    CHECK(stepInKey, "scale step: a huge step stops on an in-key note inside the range");
    ComposeKey cmaj = {0, SCALE_MAJOR};
    CHECK(Compose_SnapToKey(cmaj, 61) == 60 && Compose_SnapToKey(cmaj, 66) == 65, "snap to key: a tie goes down");
    CHECK(Compose_SnapToKey(cmaj, NOTE_NONE) == NOTE_NONE && Compose_SnapToKey(cmaj, NOTE_HOLD) == NOTE_HOLD && Compose_ScaleStep(cmaj, NOTE_HOLD, 2) == NOTE_HOLD, "snap and step: none and hold pass through");
    CHECK(Compose_ScaleStep(cmaj, 60, 7) == 72 && Compose_ScaleStep(cmaj, 60, -7) == 48, "scale step: seven steps is an octave");
    CHECK(Compose_ScaleStep(cmaj, 108, 1) == 108 && Compose_ScaleStep(cmaj, 24, -1) == 24, "scale step: the range ends stay put");
    CHECK(Compose_NoteDegree(cmaj, 64) == 2 && Compose_NoteDegree(cmaj, 61) == -1 && Compose_NoteDegree(cmaj, NOTE_HOLD) == -1, "note degree: E is degree 2 of C major, C# is not in it");
    CHECK(Compose_DegreeNote(cmaj, 60, 7) == 72 && Compose_DegreeNote(cmaj, 60, -1) == 59 && Compose_DegreeNote(cmaj, 60, 9) == 76, "degree note: runs past the octave both ways");

    ComposeKey am = {9, SCALE_MINOR};
    static const char *const want[7] = {"Am", "Bdim", "C", "Dm", "Em", "F", "G"};
    bool names = true;
    for (int d = 0; d < 7; d++) {
        char out[16];
        Compose_ChordName(am, d, out, sizeof(out));
        if (strcmp(out, want[d]) != 0) { names = false; printf("  chord %d of A minor named %s, wanted %s\n", d, out, want[d]); }
    }
    CHECK(names, "chord name: A minor's seven chords are Am Bdim C Dm Em F G");
    char out[16];
    ComposeKey aharm = {9, SCALE_HARMONIC};
    Compose_ChordName(aharm, 2, out, sizeof(out));
    CHECK(strcmp(out, "Caug") == 0, "chord name: the augmented chord in harmonic minor");
    Compose_ChordName(aharm, 6, out, sizeof(out));
    CHECK(strcmp(out, "G#dim") == 0, "chord name: the raised seventh is spelt with a sharp");
    Compose_ChordName(am, 7, out, sizeof(out));
    CHECK(strcmp(out, "Am") == 0, "chord name: degrees wrap");
    CHECK(Compose_IsChordTone(am, 0, 60) && Compose_IsChordTone(am, 0, 64) && Compose_IsChordTone(am, 0, 57) && !Compose_IsChordTone(am, 0, 62) && !Compose_IsChordTone(am, 0, NOTE_HOLD), "chord tone: A C E are tones of Am, D is not");
}

static void compose_test_tables(void) {
    bool styleNames = true;
    CHECK(Compose_StyleCount(TRACK_BASS) == 6 && Compose_StyleCount(TRACK_ARP) == 6 && Compose_StyleCount(TRACK_DRUMS) == 6 && Compose_StyleCount(TRACK_LEAD) == 2, "styles: six for bass, arp and drums, two for the lead");
    for (int track = 0; track < SONG_TRACKS; track++) {
        for (int s = 0; s < Compose_StyleCount((SongTrack)track); s++) {
            const char *n = Compose_StyleName((SongTrack)track, s);
            if (!compose_SentenceCase(n, 14)) { styleNames = false; printf("  style name '%s' fails the house rules\n", n ? n : "(null)"); }
            for (int o = 0; o < s; o++) if (strcmp(Compose_StyleName((SongTrack)track, o), n) == 0) styleNames = false;
        }
        if (Compose_StyleName((SongTrack)track, -1)[0] != '\0' || Compose_StyleName((SongTrack)track, 99)[0] != '\0') styleNames = false;
    }
    CHECK(styleNames, "styles: names are sentence case, distinct, at most 14 glyphs, and empty out of range");
    CHECK(strcmp(Compose_StyleName(TRACK_BASS, 0), "Steady") == 0 && strcmp(Compose_StyleName(TRACK_ARP, 0), "Up") == 0 && strcmp(Compose_StyleName(TRACK_DRUMS, 0), "Dance") == 0 && strcmp(Compose_StyleName(TRACK_LEAD, 1), "Fit my rhythm") == 0, "styles: the first names are the themes' own");

    bool progNames = true, numerals = true, degrees = true, distinct = true;
    int count = Compose_ProgressionCount();
    CHECK(count >= 10, "progressions: at least ten named progressions");
    for (int i = 0; i < count; i++) {
        const ComposeProgression *p = Compose_ProgressionAt(i);
        if (!compose_SentenceCase(p->name, 14)) { progNames = false; printf("  progression name '%s' fails the house rules\n", p->name); }
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", p->numerals);
        int n = 0;
        for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
            if (n >= 4 || compose_Numeral(tok) != p->chords.degree[n]) numerals = false;
            n++;
        }
        if (n != 4) numerals = false;
        for (int b = 0; b < 4; b++) if (p->chords.degree[b] < 0 || p->chords.degree[b] > 6) degrees = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(Compose_ProgressionAt(j)->name, p->name) == 0) distinct = false;
            if (memcmp(&Compose_ProgressionAt(j)->chords, &p->chords, sizeof(ComposeChords)) == 0) distinct = false;
        }
        if (Compose_ProgressionFind(&p->chords) != i) degrees = false;
    }
    CHECK(progNames, "progressions: names are sentence-case ASCII with no em-dash, at most 14 glyphs");
    CHECK(numerals, "progressions: the numerals agree with the degrees");
    CHECK(degrees, "progressions: degrees are 0..6 and each is found by its degrees");
    CHECK(distinct, "progressions: no two share a name or a progression");
    CHECK(Compose_ProgressionAt(count) == Compose_ProgressionAt(0) && Compose_ProgressionAt(-1) == Compose_ProgressionAt(count - 1), "progressions: ProgressionAt wraps");
    /* Every game theme's progression is in the table, so a recipe can be
     * made like any theme. */
    bool themesIn = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        ComposeChords c = compose_ThemeChords(Music_ThemeAt(i));
        if (Compose_ProgressionFind(&c) < 0) themesIn = false;
    }
    CHECK(themesIn, "progressions: every theme's progression is in the table");
    ComposeChords odd = {{6, 6, 6, 6}};
    CHECK(Compose_ProgressionFind(&odd) == -1 && Compose_ProgressionFind(NULL) == -1, "progressions: unknown degrees and NULL are not found");
}

/* ------------------------------------------------- chords: reharmonising */

static int compose_Abs(int v) { return v < 0 ? -v : v; }

/* Every onset of a tone track is in key and in range. */
static bool compose_TrackInKey(ComposeKey k, const int8_t *notes) {
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = notes[st];
        if (n < 0) continue;
        if (n < NOTE_MIN || n > NOTE_MAX || !Compose_InKey(k, n)) return false;
    }
    return true;
}

static bool compose_HasOnset(const int8_t *notes) {
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] >= 0) return true;
    return false;
}

/* Whether any tone of the chord on degree d lies within `dist` semitones of a note. */
static bool compose_ChordToneWithin(ComposeKey k, int d, int note, int dist) {
    for (int x = -dist; x <= dist; x++) if (Compose_IsChordTone(k, d, note + x)) return true;
    return false;
}

static void compose_test_reharmonise_themes(void) {
    bool rhythm = true, drums = true, leadKept = true, bassRoots = true, arpTones = true, arpNear = true;
    bool inKey = true, roundTrip = true, same = true, sameRead = true, readBack = true;
    bool fitMask = true, fitScale = true, fitStrong = true, fitBound = true, fitIdem = true, fitOthers = true;
    int arpMaxMove = 0, strongExceptions = 0, pairs = 0;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song theme;
        Music_ThemeToSong(t, &theme);
        const SongPattern *src = &theme.patterns[0];
        ComposeKey k = compose_ThemeKey(t);
        ComposeChords from = compose_ThemeChords(t);

        /* The same chords change nothing, given or read from the pattern. */
        SongPattern p = *src;
        Compose_Reharmonise(&p, k, &from, &from, false);
        if (memcmp(&p, src, sizeof(p)) != 0) same = false;
        p = *src;
        Compose_Reharmonise(&p, k, NULL, &from, false);
        if (memcmp(&p, src, sizeof(p)) != 0) sameRead = false;

        for (int pi = 0; pi < Compose_ProgressionCount(); pi++) {
            const ComposeChords *to = &Compose_ProgressionAt(pi)->chords;
            pairs++;
            p = *src;
            Compose_Reharmonise(&p, k, &from, to, false);
            if (!compose_SameMask(p.tone[TRACK_BASS], src->tone[TRACK_BASS]) || !compose_SameMask(p.tone[TRACK_ARP], src->tone[TRACK_ARP])) rhythm = false;
            if (!compose_TracksEqual(&p, src, TRACK_DRUMS)) drums = false;
            if (!compose_TracksEqual(&p, src, TRACK_LEAD)) leadKept = false;
            if (!compose_TrackInKey(k, p.tone[TRACK_BASS]) || !compose_TrackInKey(k, p.tone[TRACK_ARP])) inKey = false;
            for (int st = 0; st < SONG_STEPS; st++) {
                int d = to->degree[st / 16];
                int b = p.tone[TRACK_BASS][st];
                /* A theme's bass sits on its bar root, so it must now sit on the new one. */
                if (b >= 0 && b % 12 != Compose_DegreeNote(k, t->rootMidi, d) % 12) bassRoots = false;
                int a = p.tone[TRACK_ARP][st];
                if (a >= 0) {
                    if (!Compose_IsChordTone(k, d, a)) arpTones = false;
                    int move = compose_Abs(a - src->tone[TRACK_ARP][st]);
                    if (move > 6) arpNear = false;
                    if (move > arpMaxMove) arpMaxMove = move;
                }
            }
            ComposeChords got;
            if (!Compose_ReadChords(&p, k, &got) || memcmp(&got, to, sizeof(got)) != 0) readBack = false;

            SongPattern back = p;
            Compose_Reharmonise(&back, k, to, &from, false);
            if (memcmp(&back, src, sizeof(back)) != 0) {
                roundTrip = false;
                printf("  round trip %s -> %s -> %s broke\n", t->slug, Compose_ProgressionAt(pi)->name, t->slug);
            }

            /* Fit the tune too. */
            SongPattern fit = *src;
            Compose_Reharmonise(&fit, k, &from, to, true);
            if (!compose_OthersEqual(&fit, &p, TRACK_LEAD)) fitOthers = false;
            if (!compose_SameMask(fit.tone[TRACK_LEAD], src->tone[TRACK_LEAD])) fitMask = false;
            if (!compose_TrackInKey(k, fit.tone[TRACK_LEAD])) fitScale = false;
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = fit.tone[TRACK_LEAD][st], o = src->tone[TRACK_LEAD][st];
                if (n < 0) continue;
                if (compose_Abs(n - o) > 2) fitBound = false;
                int d = to->degree[st / 16];
                if (st % 4 == 0 && !Compose_IsChordTone(k, d, n)) {
                    /* Allowed only where no chord tone was within reach. */
                    if (compose_ChordToneWithin(k, d, o, 2)) fitStrong = false;
                    else strongExceptions++;
                }
            }
            SongPattern again = fit;
            if (Compose_FitLead(&again, k, to) || memcmp(&again, &fit, sizeof(again)) != 0) fitIdem = false;
        }
    }
    CHECK(same, "reharmonise: the same chords change nothing on every theme");
    CHECK(sameRead, "reharmonise: from NULL reads the theme's own chords, so nothing changes");
    CHECK(rhythm, "reharmonise: bass and arp keep their note/hold/none masks for every theme x progression");
    CHECK(drums, "reharmonise: drums are untouched");
    CHECK(leadKept, "reharmonise: the lead is untouched without fit the tune");
    CHECK(inKey, "reharmonise: every bass and arp note stays in key and in range");
    CHECK(bassRoots, "reharmonise: a theme's bass onsets land on the new bar roots");
    CHECK(arpTones, "reharmonise: every arp onset is a tone of its bar's new chord");
    CHECK(arpNear, "reharmonise: no arp note moves more than 6 semitones");
    CHECK(readBack, "reharmonise: reading the chords back gives the new progression");
    CHECK(roundTrip, "reharmonise: theme -> any progression -> theme restores every note");
    CHECK(fitOthers, "fit the tune: bass, arp and drums are the same as without it");
    CHECK(fitMask, "fit the tune: the lead keeps its note/hold/none mask");
    CHECK(fitScale, "fit the tune: every lead note is in the scale");
    CHECK(fitStrong, "fit the tune: strong-beat notes are chord tones wherever one is within 2 semitones");
    CHECK(fitBound, "fit the tune: no note moves more than 2 semitones");
    CHECK(fitIdem, "fit the tune: a second fit moves nothing and says so");
    /* Nearly every strong beat is in reach: only harmonic minor's two
     * diminished chords leave an in-key note three semitones from both
     * neighbours (7 of the 168 pairs' strong beats when this was written). */
    CHECK(pairs == Music_ThemeCount() * Compose_ProgressionCount() && strongExceptions <= 16, "fit the tune: strong beats out of reach of a chord tone stay rare");
    if (!arpNear) printf("  an arp note moved %d semitones\n", arpMaxMove);
}

/* Patterns the recipe screen and Fill write: every bass and arp style in
 * every key, moved between progressions. */
static void compose_test_reharmonise_styles(void) {
    bool rhythm = true, inKey = true, roundTrip = true, readBack = true, arpTones = true;
    int roundTripMisses = 0, cases = 0;
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            int tonic = 48 + root; /* C3..B3: far enough from both range ends that nothing folds */
            for (int style = 0; style < 6; style++) {
                for (int pi = 0; pi < Compose_ProgressionCount(); pi += 3) {
                    const ComposeChords *from = &Compose_ProgressionAt(pi)->chords;
                    SongPattern src;
                    Song_ClearPattern(&src);
                    Compose_Bass(&src, k, tonic, from, style);
                    Compose_Arp(&src, k, tonic, from, style);
                    for (int qi = 0; qi < Compose_ProgressionCount(); qi++) {
                        const ComposeChords *to = &Compose_ProgressionAt(qi)->chords;
                        SongPattern p = src;
                        cases++;
                        Compose_Reharmonise(&p, k, from, to, false);
                        if (!compose_SameMask(p.tone[TRACK_BASS], src.tone[TRACK_BASS]) || !compose_SameMask(p.tone[TRACK_ARP], src.tone[TRACK_ARP])) rhythm = false;
                        if (!compose_TrackInKey(k, p.tone[TRACK_BASS]) || !compose_TrackInKey(k, p.tone[TRACK_ARP])) inKey = false;
                        for (int st = 0; st < SONG_STEPS; st++) {
                            int a = p.tone[TRACK_ARP][st];
                            if (a >= 0 && !Compose_IsChordTone(k, to->degree[st / 16], a)) arpTones = false;
                        }
                        ComposeChords got;
                        if (!Compose_ReadChords(&p, k, &got) || memcmp(&got, to, sizeof(got)) != 0) readBack = false;
                        Compose_Reharmonise(&p, k, to, from, false);
                        if (memcmp(&p, &src, sizeof(p)) != 0) { roundTrip = false; roundTripMisses++; }
                    }
                }
            }
        }
    }
    CHECK(rhythm, "reharmonise styles: every bass and arp style keeps its rhythm in every key");
    CHECK(inKey, "reharmonise styles: every note in key and in range");
    CHECK(arpTones, "reharmonise styles: every arp onset is a tone of the new chord");
    CHECK(readBack, "reharmonise styles: the chords read back as the new progression");
    CHECK(roundTrip, "reharmonise styles: there and back restores every style exactly");
    if (roundTripMisses) printf("  %d of %d style round trips missed\n", roundTripMisses, cases);
}

static void compose_test_reharmonise_edges(void) {
    ComposeKey cmaj = {0, SCALE_MAJOR};
    ComposeChords one = {{0, 0, 0, 0}}, six = {{5, 5, 5, 5}}, four = {{3, 3, 3, 3}};

    /* A bass on the lowest C with an octave jump: the shorter way to vi (A)
     * is down past NOTE_MIN, so the bar goes up instead and keeps its jump. */
    SongPattern p;
    Song_ClearPattern(&p);
    for (int bar = 0; bar < 4; bar++) {
        p.tone[TRACK_BASS][bar * 16] = NOTE_MIN;
        p.tone[TRACK_BASS][bar * 16 + 8] = NOTE_MIN + 12;
        p.tone[TRACK_BASS][bar * 16 + 9] = NOTE_HOLD;
    }
    for (int st = 0; st < SONG_STEPS; st += 2) p.tone[TRACK_ARP][st] = (int8_t)(NOTE_MAX - (st % 6 == 0 ? 0 : (st % 6 == 2 ? 5 : 8))); /* C8 G7 E7 */
    SongPattern before = p;
    Compose_Reharmonise(&p, cmaj, &one, &six, false);
    bool low = true;
    for (int bar = 0; bar < 4; bar++) {
        int r = p.tone[TRACK_BASS][bar * 16], o = p.tone[TRACK_BASS][bar * 16 + 8];
        if (r < NOTE_MIN || r % 12 != 9 || o != r + 12 || p.tone[TRACK_BASS][bar * 16 + 9] != NOTE_HOLD) low = false;
    }
    CHECK(low, "reharmonise edges: a bass at NOTE_MIN goes the other way and keeps its octave jump on the new root");
    CHECK(compose_TrackInKey(cmaj, p.tone[TRACK_BASS]) && compose_TrackInKey(cmaj, p.tone[TRACK_ARP]) && compose_SameMask(p.tone[TRACK_ARP], before.tone[TRACK_ARP]), "reharmonise edges: an arp at NOTE_MAX stays in range and in key");
    bool top = true;
    for (int st = 0; st < SONG_STEPS; st++) if (p.tone[TRACK_ARP][st] >= 0 && !Compose_IsChordTone(cmaj, 5, p.tone[TRACK_ARP][st])) top = false;
    CHECK(top, "reharmonise edges: the folded arp notes are still tones of the new chord");
    p = before;
    Compose_Reharmonise(&p, cmaj, &one, &four, false);
    bool fourOk = true;
    for (int bar = 0; bar < 4; bar++) if (p.tone[TRACK_BASS][bar * 16] % 12 != 5) fourOk = false;
    CHECK(fourOk && compose_TrackInKey(cmaj, p.tone[TRACK_BASS]), "reharmonise edges: the shorter way up is taken when it fits");

    /* Out-of-key notes in a bar that changes come out in key; in a bar that
     * does not change they are left alone. */
    ComposeKey am = {9, SCALE_MINOR};
    Song_ClearPattern(&p);
    for (int bar = 0; bar < 4; bar++) {
        p.tone[TRACK_BASS][bar * 16] = 45;
        p.tone[TRACK_BASS][bar * 16 + 4] = 46; /* A#, outside A minor */
        p.tone[TRACK_ARP][bar * 16] = 57;
        p.tone[TRACK_ARP][bar * 16 + 2] = 61; /* C# */
        p.tone[TRACK_ARP][bar * 16 + 4] = 64;
        p.tone[TRACK_ARP][bar * 16 + 6] = 60;
    }
    before = p;
    ComposeChords amChords = {{0, 0, 0, 0}}, mixed = {{0, 5, 3, 4}};
    Compose_Reharmonise(&p, am, &amChords, &mixed, false);
    bool bar0Kept = memcmp(p.tone[TRACK_BASS], before.tone[TRACK_BASS], 16) == 0 && memcmp(p.tone[TRACK_ARP], before.tone[TRACK_ARP], 16) == 0;
    bool changedInKey = true;
    for (int st = 16; st < SONG_STEPS; st++) {
        if (p.tone[TRACK_BASS][st] >= 0 && !Compose_InKey(am, p.tone[TRACK_BASS][st])) changedInKey = false;
        if (p.tone[TRACK_ARP][st] >= 0 && !Compose_IsChordTone(am, mixed.degree[st / 16], p.tone[TRACK_ARP][st])) changedInKey = false;
    }
    CHECK(bar0Kept, "reharmonise: a bar whose chord stays is left exactly as it was, out-of-key notes included");
    CHECK(changedInKey, "reharmonise: in a bar that changes, out-of-key bass notes come out in key and every arp note on the new chord");

    /* to NULL does nothing; p NULL does not crash. */
    p = before;
    Compose_Reharmonise(&p, am, &amChords, NULL, true);
    Compose_Reharmonise(NULL, am, &amChords, &mixed, true);
    CHECK(memcmp(&p, &before, sizeof(p)) == 0, "reharmonise: no target chords leaves the pattern alone");

    /* Degrees outside 0..6 wrap rather than reading past the scale. */
    SongPattern wrapped = before, plain = before;
    ComposeChords big = {{7, 12, 10, 11}}, small = {{0, 5, 3, 4}};
    Compose_Reharmonise(&wrapped, am, &amChords, &big, false);
    Compose_Reharmonise(&plain, am, &amChords, &small, false);
    CHECK(memcmp(&wrapped, &plain, sizeof(plain)) == 0, "reharmonise: chord degrees wrap by seven");
}

static void compose_test_reharmonise_empty(void) {
    ComposeKey am = {9, SCALE_MINOR};
    ComposeChords zero = {{0, 0, 0, 0}}, hero = {{0, 4, 5, 3}};
    /* The owner's own file shape: drums only. */
    SongPattern p;
    Song_ClearPattern(&p);
    for (int st = 0; st < SONG_STEPS; st += 4) p.drums[st] = DRUM_KICK | DRUM_HAT;
    SongPattern before = p, want = p;
    Compose_Reharmonise(&p, am, &zero, &hero, false);
    int tonic = Compose_TonicMidi(am, NULL);
    Compose_Bass(&want, am, tonic, &hero, 0);
    Compose_Arp(&want, am, tonic, &hero, 0);
    CHECK(memcmp(&p, &want, sizeof(p)) == 0, "reharmonise empty: an empty bass and arp come out as style 0 on the new chords in the recipe register");
    CHECK(compose_TracksEqual(&p, &before, TRACK_DRUMS) && compose_TracksEqual(&p, &before, TRACK_LEAD), "reharmonise empty: drums and the empty lead are untouched");
    SongPattern again = p;
    Compose_Reharmonise(&again, am, &hero, &hero, false);
    CHECK(memcmp(&again, &p, sizeof(p)) == 0, "reharmonise empty: once written, the same chords change nothing");
    /* Even with the same chords an empty track is filled: Chords on an empty pattern gives a backing. */
    p = before;
    Compose_Reharmonise(&p, am, NULL, &zero, false);
    CHECK(compose_HasOnset(p.tone[TRACK_BASS]) && compose_HasOnset(p.tone[TRACK_ARP]), "reharmonise empty: the same chords still fill an empty bass and arp");

    /* An arp with no bass: the bass is written in the register the arp implies. */
    Song s;
    Music_ThemeToSong(Music_FindTheme("ghostarcade"), &s);
    const MusicTheme *t = Music_FindTheme("ghostarcade");
    ComposeKey k = compose_ThemeKey(t);
    ComposeChords from = compose_ThemeChords(t);
    p = s.patterns[0];
    memset(p.tone[TRACK_BASS], NOTE_NONE, SONG_STEPS);
    SongPattern arpOnly = p;
    Compose_Reharmonise(&p, k, &from, &hero, false);
    want = arpOnly;
    Compose_Bass(&want, k, t->rootMidi, &hero, 0);
    CHECK(memcmp(p.tone[TRACK_BASS], want.tone[TRACK_BASS], SONG_STEPS) == 0, "reharmonise empty: a missing bass is written at the theme's own register, read from the arp");
    CHECK(compose_SameMask(p.tone[TRACK_ARP], arpOnly.tone[TRACK_ARP]), "reharmonise empty: the arp beside it keeps its rhythm");
}

static void compose_test_dense_arp(void) {
    ComposeKey am = {9, SCALE_MINOR};
    ComposeChords zero = {{0, 0, 0, 0}}, sixes = {{5, 5, 5, 5}};
    /* A busy arp of passing notes around E (D#, E, F, F#): over F every one of
     * them heads for the F, which would make the arp one repeated note. */
    SongPattern p;
    Song_ClearPattern(&p);
    for (int st = 0; st < SONG_STEPS; st++) p.tone[TRACK_ARP][st] = (int8_t)(63 + st % 4);
    SongPattern before = p, again = p;
    Compose_Reharmonise(&p, am, &zero, &sixes, false);
    Compose_Reharmonise(&again, am, &zero, &sixes, false);
    bool bars = true;
    int count[128] = {0}, onsets = 0, most = 0;
    for (int bar = 0; bar < 4; bar++) {
        int barOnsets = 0, distinct = 0;
        bool seen[128] = {false};
        for (int i = 0; i < 16; i++) {
            int n = p.tone[TRACK_ARP][bar * 16 + i];
            if (n < 0) continue;
            barOnsets++;
            onsets++;
            if (++count[n] > most) most = count[n];
            if (!seen[n]) { seen[n] = true; distinct++; }
            if (!Compose_IsChordTone(am, 5, n)) bars = false;
        }
        /* A triad arp: four onsets or more and all three tones in every bar. */
        if (barOnsets < 4 || distinct < 3) bars = false;
    }
    /* It must be one of the writer's styles with the same rhythm (16 onsets a
     * bar: Up, Up and down or Broken), on the new chords, at the register the
     * arp's own E and F place the tonic (A2). */
    bool styled = false;
    for (int style = 0; style < Compose_StyleCount(TRACK_ARP); style++) {
        SongPattern want = before;
        Compose_Arp(&want, am, 45, &sixes, style);
        /* The arp alone: the empty bass beside it is filled with Steady, as any empty bass is. */
        if (compose_TracksEqual(&p, &want, TRACK_ARP) && compose_SameMask(want.tone[TRACK_ARP], before.tone[TRACK_ARP])) styled = true;
    }
    CHECK(most * 2 <= onsets, "dense arp: the fallback leaves no pitch with more than half the onsets");
    CHECK(bars, "dense arp: every bar has at least four onsets covering the whole new triad");
    CHECK(styled, "dense arp: the arp is rewritten as a writer style with its rhythm, on the new chords, in its own register");
    CHECK(memcmp(&p, &again, sizeof(p)) == 0, "dense arp: the fallback is deterministic");

    /* A pedal (one note all along) was already one pitch: it is re-pitched, not rewritten. */
    Song_ClearPattern(&p);
    for (int st = 0; st < SONG_STEPS; st += 2) { p.tone[TRACK_ARP][st] = 57; p.tone[TRACK_ARP][st + 1] = NOTE_HOLD; }
    before = p;
    ComposeChords g = {{6, 6, 6, 6}};
    Compose_Reharmonise(&p, am, &zero, &g, false);
    bool pedal = compose_SameMask(p.tone[TRACK_ARP], before.tone[TRACK_ARP]);
    for (int st = 0; st < SONG_STEPS; st += 2) if (p.tone[TRACK_ARP][st] != p.tone[TRACK_ARP][0] || !Compose_IsChordTone(am, 6, p.tone[TRACK_ARP][st])) pedal = false;
    CHECK(pedal, "dense arp: a pedal arp keeps its rhythm and moves as one note to a tone of the new chord");
}

static void compose_test_fit_lead(void) {
    ComposeKey am = {9, SCALE_MINOR};
    ComposeChords c = {{0, 5, 3, 4}};
    SongPattern p;
    Song_ClearPattern(&p);
    /* A chromatic run, one note a step, with holds. */
    for (int st = 0; st < SONG_STEPS; st++) p.tone[TRACK_LEAD][st] = (st % 8 == 7) ? NOTE_HOLD : (int8_t)(60 + st % 12);
    SongPattern before = p;
    bool moved = Compose_FitLead(&p, am, &c);
    bool ok = compose_SameMask(p.tone[TRACK_LEAD], before.tone[TRACK_LEAD]) && compose_TrackInKey(am, p.tone[TRACK_LEAD]);
    bool bound = true, strong = true;
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = p.tone[TRACK_LEAD][st];
        if (n < 0) continue;
        if (compose_Abs(n - before.tone[TRACK_LEAD][st]) > 2) bound = false;
        if (st % 4 == 0 && !Compose_IsChordTone(am, c.degree[st / 16], n)) strong = false;
    }
    CHECK(moved, "fit lead: a chromatic run moves and says so");
    CHECK(ok, "fit lead: mask kept and every note in the scale");
    CHECK(bound, "fit lead: no note moves more than 2 semitones");
    CHECK(strong, "fit lead: every strong-beat note is a chord tone (A minor has one in reach everywhere)");
    CHECK(compose_OthersEqual(&p, &before, TRACK_LEAD), "fit lead: only the lead changes");
    SongPattern again = p;
    CHECK(!Compose_FitLead(&again, am, &c) && memcmp(&again, &p, sizeof(p)) == 0, "fit lead: idempotent");

    /* A theme's own tune already fits its own chords. */
    bool themesFit = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song s;
        Music_ThemeToSong(t, &s);
        ComposeChords tc = compose_ThemeChords(t);
        if (Compose_FitLead(&s.patterns[0], compose_ThemeKey(t), &tc)) themesFit = false;
    }
    CHECK(themesFit, "fit lead: every theme's tune already fits its own chords, so nothing moves");

    /* Harmonic minor's diminished vii: F sits three semitones from G# and from
     * D, so on a strong beat it goes to the scale (stays F) rather than leap. */
    ComposeKey ah = {9, SCALE_HARMONIC};
    ComposeChords vii = {{6, 6, 6, 6}};
    Song_ClearPattern(&p);
    p.tone[TRACK_LEAD][0] = 65;
    p.tone[TRACK_LEAD][4] = 67; /* G, outside A harmonic minor: one semitone from G# */
    Compose_FitLead(&p, ah, &vii);
    CHECK(p.tone[TRACK_LEAD][0] == 65 && p.tone[TRACK_LEAD][4] == 68, "fit lead: out of reach stays in the scale; in reach becomes the chord tone");

    /* The ends of the range: candidates beyond them are never chosen. */
    Song_ClearPattern(&p);
    p.tone[TRACK_LEAD][0] = NOTE_MAX;
    p.tone[TRACK_LEAD][4] = NOTE_MIN;
    ComposeKey bmaj = {11, SCALE_MAJOR};
    ComposeChords bI = {{0, 0, 0, 0}};
    Compose_FitLead(&p, bmaj, &bI);
    CHECK(p.tone[TRACK_LEAD][0] >= NOTE_MIN && p.tone[TRACK_LEAD][0] <= NOTE_MAX && p.tone[TRACK_LEAD][4] >= NOTE_MIN && p.tone[TRACK_LEAD][4] <= NOTE_MAX && compose_TrackInKey(bmaj, p.tone[TRACK_LEAD]), "fit lead: notes at the range ends stay inside it and in key");
    CHECK(!Compose_FitLead(NULL, am, &c) && !Compose_FitLead(&p, am, NULL), "fit lead: NULL arguments are refused");
}

/* The ends of the range, as the editor reaches them: a tune typed high and a
 * walking bass under a low tonic used to have a note clamped onto C8 or C1,
 * outside the key. */
static void compose_test_range_ends(void) {
    /* E major, E2 on every beat, a lead typed with the octave at 7 (E7 G#7 B7
     * and B6): Fill reads lead octave 5, and the tune's upper degrees run past
     * NOTE_MAX. New tune and Fit my rhythm must fold them, keeping the key. */
    ComposeKey emaj = {4, SCALE_MAJOR};
    SongPattern typed;
    Song_ClearPattern(&typed);
    for (int st = 0; st < SONG_STEPS; st += 4) typed.tone[TRACK_BASS][st] = 40;
    static const int8_t high[8] = {100, 104, 107, 95, 100, 104, 100, 95};
    for (int st = 0; st < SONG_STEPS; st += 2) typed.tone[TRACK_LEAD][st] = high[(st / 2) % 8];
    int tonic = Compose_TonicMidi(emaj, &typed);
    int oct = Compose_LeadOctave(emaj, tonic, &typed);
    ComposeChords c;
    Compose_ReadChords(&typed, emaj, &c);
    bool highKey = true, highStrong = true;
    for (uint32_t seed = 0; seed < 40; seed++) {
        SongPattern fit = typed, tune = typed;
        Compose_LeadPitches(&fit, emaj, tonic, &c, oct, seed);
        Compose_Lead(&tune, emaj, tonic, &c, oct, seed);
        if (!compose_TrackInKey(emaj, fit.tone[TRACK_LEAD]) || !compose_TrackInKey(emaj, tune.tone[TRACK_LEAD])) highKey = false;
        for (int st = 0; st < SONG_STEPS; st += 4) {
            int a = fit.tone[TRACK_LEAD][st], b = tune.tone[TRACK_LEAD][st];
            if (a >= 0 && !Compose_IsChordTone(emaj, c.degree[st / 16], a)) highStrong = false;
            if (b >= 0 && !Compose_IsChordTone(emaj, c.degree[st / 16], b)) highStrong = false;
        }
    }
    CHECK(tonic == 52 && oct == 5, "range ends: a lead typed at octave 7 over an E2 bass reads as tonic 52, lead octave 5");
    CHECK(highKey, "range ends: New tune and Fit my rhythm at lead octave 5 stay in E major and in range (no C8)");
    CHECK(highStrong, "range ends: and their strong beats stay chord tones");

    /* C# minor with no bass and an arp on C#3 E3 G#3: the tonic reads 37, so
     * Walking's approach into a bar that stays on i is B0, under NOTE_MIN. */
    ComposeKey csm = {1, SCALE_MINOR};
    SongPattern arpOnly;
    Song_ClearPattern(&arpOnly);
    static const int8_t arp[3] = {49, 52, 56};
    for (int st = 0; st < SONG_STEPS; st++) arpOnly.tone[TRACK_ARP][st] = arp[st % 3];
    int low = Compose_TonicMidi(csm, &arpOnly);
    ComposeChords one;
    Compose_ReadChords(&arpOnly, csm, &one);
    SongPattern walk = arpOnly;
    Compose_Bass(&walk, csm, low, &one, 4);
    bool approach = true;
    for (int bar = 0; bar < 4; bar++) {
        int n = walk.tone[TRACK_BASS][bar * 16 + 12];
        if (n < NOTE_MIN || !Compose_InKey(csm, n)) approach = false;
    }
    CHECK(low == 37 && one.degree[0] == 0 && one.degree[3] == 0, "range ends: an arp on C#3 E3 G#3 reads as tonic 37 on i i i i");
    CHECK(approach && compose_TrackInKey(csm, walk.tone[TRACK_BASS]), "range ends: Walking at tonic 37 approaches each bar from in key (B1, not C1)");
    /* D major at tonic 38, i to v: the falling approach is two steps under
     * the tonic an octave down, B0 as well. */
    ComposeKey dmaj = {2, SCALE_MAJOR};
    ComposeChords iv = {{0, 4, 0, 4}};
    Song_ClearPattern(&walk);
    Compose_Bass(&walk, dmaj, 38, &iv, 4);
    CHECK(compose_TrackInKey(dmaj, walk.tone[TRACK_BASS]), "range ends: Walking at tonic 38 on i v stays in D major");
}

/* Fit the tune twice on the same chords: every key, every chord degree and
 * every note of the range, on a strong beat and off it. The second fit must
 * move nothing, which a diminished chord's gap used to break: B on ii of C
 * minor went to A# and then, fitted again, to G#. Reach counts only notes
 * inside the range, as FitLead never picks one beyond it. */
static bool compose_ChordToneInReach(ComposeKey k, int d, int note, int dist) {
    for (int x = -dist; x <= dist; x++) {
        int m = note + x;
        if (m >= NOTE_MIN && m <= NOTE_MAX && Compose_IsChordTone(k, d, m)) return true;
    }
    return false;
}

static void compose_test_fit_twice(void) {
    bool idem = true, inKey = true, strong = true, bound = true, wide = true;
    int cases = 0;
    for (int root = 0; root < 12; root++)
    for (int scale = 0; scale < SONG_SCALE_COUNT; scale++)
    for (int d = 0; d < 7; d++) {
        ComposeKey k = {root, scale};
        ComposeChords c = {{d, d, d, d}};
        for (int n = NOTE_MIN; n <= NOTE_MAX; n++) {
            SongPattern p;
            Song_ClearPattern(&p);
            p.tone[TRACK_LEAD][0] = (int8_t)n; /* a strong beat */
            p.tone[TRACK_LEAD][2] = (int8_t)n; /* off the beat */
            Compose_FitLead(&p, k, &c);
            SongPattern again = p;
            if (Compose_FitLead(&again, k, &c) || memcmp(&again, &p, sizeof(p)) != 0) idem = false;
            if (!compose_TrackInKey(k, p.tone[TRACK_LEAD])) inKey = false;
            int on = p.tone[TRACK_LEAD][0], off = p.tone[TRACK_LEAD][2];
            /* A strong beat is a chord tone unless none is in reach (2, or 3
             * from out of key); then it goes to the scale, so an in-key note
             * stays. Only the range ends leave an out-of-key note out of reach. */
            int reach = Compose_InKey(k, n) ? 2 : 3;
            if (!Compose_IsChordTone(k, d, on) && (compose_ChordToneInReach(k, d, n, reach) || on != Compose_SnapToKey(k, n))) strong = false;
            if (off != Compose_SnapToKey(k, n)) bound = false;
            int move = compose_Abs(on - n);
            if (move > 3) bound = false;
            /* Three only for an out-of-key note midway in a diminished gap. */
            if (move == 3 && (Compose_InKey(k, n) || compose_ChordToneInReach(k, d, n, 2))) wide = false;
            cases++;
        }
    }
    CHECK(cases == 12 * SONG_SCALE_COUNT * 7 * (NOTE_MAX - NOTE_MIN + 1), "fit twice: every key, degree and note of the range");
    CHECK(idem, "fit twice: a second fit on the same chords moves nothing and says so");
    CHECK(inKey, "fit twice: every fitted note is in key and in range");
    CHECK(strong, "fit twice: a strong beat lands on a chord tone, or with none in reach goes to the scale");
    CHECK(bound && wide, "fit twice: off the beat a note goes to the scale; on it, it moves 2 at most, 3 only from out of key with no chord tone within 2");

    /* The report's case: C minor, a bass that reads as ii (D dim), B4 on beat one. */
    ComposeKey cm = {0, SCALE_MINOR};
    ComposeChords ii = {{1, 1, 1, 1}};
    SongPattern p;
    Song_ClearPattern(&p);
    p.tone[TRACK_LEAD][0] = 71;
    bool first = Compose_FitLead(&p, cm, &ii);
    int once = p.tone[TRACK_LEAD][0];
    bool second = Compose_FitLead(&p, cm, &ii);
    CHECK(first && once == 68 && !second && p.tone[TRACK_LEAD][0] == 68, "fit twice: B4 on ii of C minor goes straight to the chord's G#4 and stays there");
}

static bool compose_MessageOk(const char *msg) {
    if (!msg || !msg[0] || !isupper((unsigned char)msg[0]) || strlen(msg) > 40) return false;
    for (const char *p = msg; *p; p++) if ((unsigned char)*p > 127) return false;
    return true;
}

static bool compose_Parses(const char *text, ComposeKey k, int a, int b, int c, int d) {
    ComposeChords out = {{-1, -1, -1, -1}};
    char err[64] = "stale";
    if (!Compose_ParseChordLine(text, k, &out, err, sizeof(err))) { printf("  '%s' refused: %s\n", text, err); return false; }
    if (err[0] != '\0') return false;
    return out.degree[0] == a && out.degree[1] == b && out.degree[2] == c && out.degree[3] == d;
}

static bool compose_Refuses(const char *text, ComposeKey k, const char *want) {
    ComposeChords out = {{1, 2, 3, 4}};
    char err[64] = "";
    if (Compose_ParseChordLine(text, k, &out, err, sizeof(err))) return false;
    if (out.degree[0] != 1 || out.degree[1] != 2 || out.degree[2] != 3 || out.degree[3] != 4) return false; /* untouched */
    if (!compose_MessageOk(err)) { printf("  message '%s' fails the house rules\n", err); return false; }
    if (want && strcmp(err, want) != 0) { printf("  '%s' said '%s', wanted '%s'\n", text ? text : "(null)", err, want); return false; }
    return true;
}

static void compose_test_chord_line(void) {
    ComposeKey am = {9, SCALE_MINOR}, cmaj = {0, SCALE_MAJOR}, fmaj = {5, SCALE_MAJOR};
    CHECK(compose_Parses("1 6 4 5", am, 0, 5, 3, 4), "chord line: 1 6 4 5 is i VI iv v");
    CHECK(compose_Parses("Am F C G", am, 0, 5, 2, 6), "chord line: Am F C G in A minor");
    CHECK(compose_Parses("Am F Dm Em", am, 0, 5, 3, 4), "chord line: chord names give the same degrees as 1 6 4 5");
    CHECK(compose_Parses("1 5", am, 0, 4, 0, 4), "chord line: two chords repeat as 1 5 1 5");
    CHECK(compose_Parses("1 6 4", am, 0, 5, 3, 0), "chord line: three chords repeat from the first");
    CHECK(compose_Parses("4", am, 3, 3, 3, 3), "chord line: one chord fills every bar");
    CHECK(compose_Parses("i VI iv v", am, 0, 5, 3, 4) && compose_Parses("I bVII IV I", cmaj, 0, 6, 3, 0), "chord line: Roman numerals, with the table's flats");
    CHECK(compose_Parses("viio III+ V7 ii", cmaj, 6, 2, 4, 1), "chord line: numerals with quality marks");
    CHECK(compose_Parses("1,6,4,5", am, 0, 5, 3, 4) && compose_Parses("1-6-4-5", am, 0, 5, 3, 4) && compose_Parses("  1 | 6\t4  5 ", am, 0, 5, 3, 4), "chord line: commas, dashes, bars and extra spaces separate");
    CHECK(compose_Parses("am f c g", am, 0, 5, 2, 6), "chord line: lower-case names");
    CHECK(compose_Parses("G7 Bdim Am7 Cmaj7", cmaj, 4, 6, 5, 0), "chord line: sevenths and dim are read as the key's triads");
    CHECK(compose_Parses("A F C G", am, 0, 5, 2, 6), "chord line: A in A minor means Am");
    CHECK(compose_Parses("Bb C F", fmaj, 3, 4, 0, 3), "chord line: flats in a flat key");
    CHECK(compose_Refuses("Bb", am, "Bb is not in A minor"), "chord line: a root outside the key names it as typed");
    CHECK(compose_Refuses("F#m", am, "F# is not in A minor"), "chord line: a sharp root outside the key");
    CHECK(compose_Refuses("8", am, "Use 1 to 7 for chords") && compose_Refuses("0", am, "Use 1 to 7 for chords") && compose_Refuses("15", am, "Use 1 to 7 for chords"), "chord line: digits outside 1..7 are refused");
    CHECK(compose_Refuses("X", am, "Not a chord: X") && compose_Refuses("Hm", am, NULL) && compose_Refuses("Amazing", am, "Not a chord: Amazing"), "chord line: words that are not chords are refused");
    CHECK(compose_Refuses("1 6 supercalifragilistic", am, "Not a chord: supercal"), "chord line: a long word is shortened in the message");
    CHECK(compose_Refuses("", am, "Type 1 to 4 chords, like 1 6 4 5") && compose_Refuses("  , ", am, NULL) && compose_Refuses(NULL, am, NULL), "chord line: nothing typed is refused");
    CHECK(compose_Refuses("1 2 3 4 5", am, "Up to 4 chords, one a bar") && compose_Refuses("Am F C G Am", am, NULL), "chord line: five chords are refused");
    CHECK(compose_Refuses("1 6 4 9", am, NULL), "chord line: one bad chord refuses the line and changes nothing");

    /* No err buffer, a tiny one, and no output are all safe. */
    ComposeChords out;
    char tiny[4];
    CHECK(!Compose_ParseChordLine("8", am, &out, NULL, 0) && !Compose_ParseChordLine("8", am, &out, tiny, sizeof(tiny)) && strlen(tiny) == 3, "chord line: a missing or tiny message buffer is safe");
    CHECK(Compose_ParseChordLine("1 5", am, NULL, NULL, 0), "chord line: no output still validates");

    /* Every chord name the editor shows parses back to its degree. */
    bool names = true;
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            for (int d = 0; d < 7; d++) {
                char name[16];
                Compose_ChordName(k, d, name, sizeof(name));
                ComposeChords got;
                if (!Compose_ParseChordLine(name, k, &got, NULL, 0) || got.degree[0] != d || got.degree[3] != d) { names = false; printf("  %s did not read back as degree %d\n", name, d); }
            }
        }
    }
    CHECK(names, "chord line: every Compose_ChordName in every key reads back as its degree");
    /* And every progression's numerals read back as its degrees. */
    bool numerals = true;
    for (int i = 0; i < Compose_ProgressionCount(); i++) {
        const ComposeProgression *pr = Compose_ProgressionAt(i);
        ComposeChords got;
        if (!Compose_ParseChordLine(pr->numerals, am, &got, NULL, 0) || memcmp(&got, &pr->chords, sizeof(got)) != 0) numerals = false;
    }
    CHECK(numerals, "chord line: every progression's numerals read back as its chords");
}

static void compose_test_guess_style(void) {
    bool themes = true, writers = true, octave = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song s;
        Music_ThemeToSong(t, &s);
        ComposeKey k = compose_ThemeKey(t);
        int b = Compose_GuessStyle(&s.patterns[0], k, TRACK_BASS), a = Compose_GuessStyle(&s.patterns[0], k, TRACK_ARP), d = Compose_GuessStyle(&s.patterns[0], k, TRACK_DRUMS);
        if (b != t->bassStyle || a != t->arpStyle || d != t->drumStyle) { themes = false; printf("  %s guessed bass %d arp %d drums %d\n", t->slug, b, a, d); }
        int oct = Compose_LeadOctave(k, t->rootMidi, &s.patterns[0]);
        if (oct != t->leadOctave) { octave = false; printf("  %s lead octave read as %d, theme has %d\n", t->slug, oct, t->leadOctave); }
    }
    CHECK(themes, "guess style: every theme's bass, arp and drum style is recovered");
    CHECK(octave, "lead octave: every theme's lead octave is recovered");
    for (int root = 0; root < 12; root += 5) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            int tonic = Compose_TonicMidi(k, NULL);
            for (int pi = 0; pi < Compose_ProgressionCount(); pi++) {
                const ComposeChords *c = &Compose_ProgressionAt(pi)->chords;
                for (int style = 0; style < 6; style++) {
                    SongPattern p;
                    Song_ClearPattern(&p);
                    Compose_Bass(&p, k, tonic, c, style);
                    Compose_Arp(&p, k, tonic, c, style);
                    Compose_Drums(&p, style, 0u);
                    if (Compose_GuessStyle(&p, k, TRACK_BASS) != style || Compose_GuessStyle(&p, k, TRACK_ARP) != style || Compose_GuessStyle(&p, k, TRACK_DRUMS) != style) writers = false;
                }
            }
        }
    }
    CHECK(writers, "guess style: every style a writer puts down is read back, in several keys and every progression");
    bool seeded = true;
    for (int style = 0; style < Compose_StyleCount(TRACK_DRUMS); style++) {
        for (uint32_t seed = 0; seed < 64; seed++) {
            SongPattern p;
            Song_ClearPattern(&p);
            Compose_Drums(&p, style, seed * 2654435761u);
            if (Compose_GuessStyle(&p, (ComposeKey){9, SCALE_MINOR}, TRACK_DRUMS) != style) seeded = false;
        }
    }
    CHECK(seeded, "guess style: drum styles are read back whatever ghost notes the seed adds");
    SongPattern empty;
    Song_ClearPattern(&empty);
    ComposeKey am = {9, SCALE_MINOR};
    CHECK(Compose_GuessStyle(&empty, am, TRACK_BASS) == 0 && Compose_GuessStyle(&empty, am, TRACK_ARP) == 0 && Compose_GuessStyle(&empty, am, TRACK_DRUMS) == 0 && Compose_GuessStyle(&empty, am, TRACK_LEAD) == 0 && Compose_GuessStyle(NULL, am, TRACK_BASS) == 0, "guess style: empty tracks, the lead and NULL give style 0");
    CHECK(Compose_LeadOctave(am, 45, &empty) == 2 && Compose_LeadOctave(am, 45, NULL) == 2, "lead octave: an empty lead gives 2");
    bool lead = true;
    for (int oct = 1; oct <= 4; oct++) {
        for (uint32_t seed = 1; seed <= 8; seed++) {
            SongPattern p;
            Song_ClearPattern(&p);
            ComposeChords c = {{0, 5, 3, 4}};
            Compose_Lead(&p, am, 45, &c, oct, seed * 977u);
            if (Compose_LeadOctave(am, 45, &p) != oct) lead = false;
        }
    }
    CHECK(lead, "lead octave: a tune written at octaves 1..4 reads back at its octave");
    /* A hand-written tune around C4 that drops to the low tonic at the end:
     * its average, not its last note, places it. */
    SongPattern hand;
    Song_ClearPattern(&hand);
    for (int st = 0; st < 32; st += 4) hand.tone[TRACK_LEAD][st] = (int8_t)(59 + (st / 4) % 4);
    hand.tone[TRACK_LEAD][40] = 45;
    CHECK(Compose_LeadOctave(am, 45, &hand) == 2, "lead octave: a tune that ends far below where it sits is placed by its average");
    SongPattern fit = hand;
    ComposeChords fc = {{0, 5, 3, 4}};
    Compose_LeadPitches(&fit, am, 45, &fc, 3, 5u);
    CHECK(Compose_LeadOctave(am, 45, &fit) == 3, "lead octave: a fit my rhythm tune reads back at the octave it was fitted at");
}

/* What the arranger's chorus does: new chords keeping the rhythm, then a new
 * tune on them with a fresh seed. */
static void compose_test_chorus_recipe(void) {
    bool masks = true, newLead = true, chords = true, tuneFits = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song s;
        Music_ThemeToSong(t, &s);
        ComposeKey k = compose_ThemeKey(t);
        ComposeChords from = compose_ThemeChords(t);
        const ComposeChords *to = &Compose_ProgressionAt(Compose_ProgressionFind(&from) + 1)->chords;
        SongPattern p = s.patterns[0];
        Compose_Reharmonise(&p, k, &from, to, false);
        int tonic = Compose_TonicMidi(k, &s.patterns[0]);
        int oct = Compose_LeadOctave(k, tonic, &s.patterns[0]);
        Compose_Lead(&p, k, tonic, to, oct, (uint32_t)t->leadSeed + 1u);
        if (!compose_SameMask(p.tone[TRACK_BASS], s.patterns[0].tone[TRACK_BASS]) || !compose_SameMask(p.tone[TRACK_ARP], s.patterns[0].tone[TRACK_ARP])) masks = false;
        if (compose_TracksEqual(&p, &s.patterns[0], TRACK_LEAD)) newLead = false;
        ComposeChords got;
        if (!Compose_ReadChords(&p, k, &got) || memcmp(&got, to, sizeof(got)) != 0) chords = false;
        for (int st = 0; st < SONG_STEPS; st += 4) {
            int n = p.tone[TRACK_LEAD][st];
            if (n >= 0 && !Compose_IsChordTone(k, to->degree[st / 16], n)) tuneFits = false;
        }
    }
    CHECK(masks, "chorus: bass and arp keep their rhythm on the new chords");
    CHECK(newLead, "chorus: the tune is a new one");
    CHECK(chords, "chorus: its chords read back as the new progression");
    CHECK(tuneFits, "chorus: the new tune's strong beats sit on the new chords");
}

void test_compose_all(void) {
    compose_test_theme_lock();
    compose_test_own_track();
    compose_test_styles_everywhere();
    compose_test_lead_pitches();
    compose_test_guess_key();
    compose_test_key_tools();
    compose_test_tables();
    compose_test_reharmonise_themes();
    compose_test_reharmonise_styles();
    compose_test_reharmonise_edges();
    compose_test_reharmonise_empty();
    compose_test_dense_arp();
    compose_test_fit_lead();
    compose_test_range_ends();
    compose_test_fit_twice();
    compose_test_chord_line();
    compose_test_guess_style();
    compose_test_chorus_recipe();
}
