/* Tests for the picker (pick.h) behind Fill (Ctrl+F), Chords (Ctrl+K), Vary
 * (Ctrl+R) and Grow (F9): Esc puts everything back byte for byte (the added
 * patterns and order entries too), Keep is exactly one undo entry that
 * lands back where the picker opened, choices rewrite only what they
 * promise, chords are read before anything is rewritten, and the band's
 * text stays inside the band. Compiled by inclusion from test_main.c. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"
#include "editor.h"
#include "pick.h"
#include "arrange.h"

/* The editor carries 25 songs of undo history: far too big for the stack. */
static Editor sPickEd;
static Pick sPick;
static Song sPickSong, sPickSnap, sPickWant;

/* ---------------------------------------------------------------- helpers */

static ComposeKey pick_ThemeKey(int theme) {
    const MusicTheme *t = Music_ThemeAt(theme);
    ComposeKey k = {t->rootMidi % 12, (int)t->scale};
    return k;
}

static ComposeChords pick_ThemeChords(int theme) {
    ComposeChords c;
    for (int b = 0; b < 4; b++) c.degree[b] = Music_ThemeAt(theme)->prog[b];
    return c;
}

static bool pick_SameChords(const ComposeChords *a, const ComposeChords *b) {
    for (int bar = 0; bar < 4; bar++) if (a->degree[bar] != b->degree[bar]) return false;
    return true;
}

/* The editor on a copy of a theme, cursor at the start, no undo history. */
static void pick_LoadTheme(int theme) {
    Music_ThemeToSong(Music_ThemeAt(theme), &sPickSong);
    Editor_SetSong(&sPickEd, &sPickSong, "");
}

static void pick_Snap(void) { memcpy(&sPickSnap, &sPickEd.song, sizeof(Song)); }
static bool pick_SongSame(void) { return memcmp(&sPickEd.song, &sPickSnap, sizeof(Song)) == 0; }

static bool pick_TrackEqual(const SongPattern *a, const SongPattern *b, int track) {
    if (track == TRACK_DRUMS) return memcmp(a->drums, b->drums, sizeof(a->drums)) == 0;
    return memcmp(a->tone[track], b->tone[track], sizeof(a->tone[track])) == 0;
}

static bool pick_OthersEqual(const SongPattern *a, const SongPattern *b, int except) {
    for (int t = 0; t < SONG_TRACKS; t++) if (t != except && !pick_TrackEqual(a, b, t)) return false;
    return true;
}

/* The note/hold/none shape of a tone track, ignoring which note. */
static bool pick_SameMask(const int8_t *a, const int8_t *b) {
    for (int st = 0; st < SONG_STEPS; st++) {
        int ca = a[st] >= 0 ? 1 : a[st], cb = b[st] >= 0 ? 1 : b[st];
        if (ca != cb) return false;
    }
    return true;
}

static bool pick_BarHasOnset(const int8_t *notes, int bar) {
    for (int i = 0; i < 16; i++) if (notes[bar * 16 + i] >= 0) return true;
    return false;
}

static bool pick_TrackInKey(ComposeKey k, const int8_t *notes) {
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = notes[st];
        if (n == NOTE_NONE || n == NOTE_HOLD) continue;
        if (n < NOTE_MIN || n > NOTE_MAX || !Compose_InKey(k, n)) return false;
    }
    return true;
}

/* The pitch class of the chord root on degree d. */
static int pick_RootPc(ComposeKey k, int d) {
    int n = Compose_DegreeNote(k, 60 + k.root, d);
    return ((n % 12) + 12) % 12;
}

/* Moves the picker until it shows `index` (Fill styles, table progressions). */
static void pick_MoveTo(int index) {
    if (sPick.index != index) Pick_Move(&sPick, &sPickEd, index - (sPick.index < 0 ? -1 : sPick.index));
}

/* ASCII, sentence case (a capital only at the start, or a note name), within
 * a glyph budget. */
static bool pick_TextOk(const char *s, int maxGlyphs, bool mustStartUpper) {
    if (!s) return false;
    int n = 0;
    for (const char *p = s; *p; p++, n++) if ((unsigned char)*p > 127) return false; /* so no em-dash either */
    if (mustStartUpper && (!s[0] || !isupper((unsigned char)s[0]))) return false;
    return n <= maxGlyphs;
}

/* A song of three patterns: the theme's own, the theme moved onto other
 * chords (so it has chords of its own), and its beat with nothing else. */
static void pick_ThreePatterns(int theme, ComposeChords *otherOut) {
    Music_ThemeToSong(Music_ThemeAt(theme), &sPickSong);
    ComposeKey k = pick_ThemeKey(theme);
    ComposeChords from = pick_ThemeChords(theme);
    int here = Compose_ProgressionFind(&from);
    ComposeChords to = Compose_ProgressionAt(here + 3)->chords;
    memcpy(&sPickSong.patterns[1], &sPickSong.patterns[0], sizeof(SongPattern));
    Compose_Reharmonise(&sPickSong.patterns[1], k, &from, &to, true);
    Song_ClearPattern(&sPickSong.patterns[2]);
    memcpy(sPickSong.patterns[2].drums, sPickSong.patterns[0].drums, sizeof(sPickSong.patterns[2].drums));
    sPickSong.patternCount = 3;
    sPickSong.orderCount = 3;
    for (int i = 0; i < 3; i++) sPickSong.order[i] = (uint8_t)i;
    Editor_SetSong(&sPickEd, &sPickSong, "");
    if (otherOut) *otherOut = to;
}

/* Every tone note of every pattern of the song in key and in range. */
static bool pick_SongInKey(const Song *s, ComposeKey k) {
    for (int p = 0; p < s->patternCount; p++)
        for (int t = 0; t < SONG_TONE_TRACKS; t++)
            if (!pick_TrackInKey(k, s->patterns[p].tone[t])) return false;
    return true;
}

/* A song of n patterns, each a copy of the theme's, played once through. */
static void pick_ManyPatterns(int theme, int n) {
    Music_ThemeToSong(Music_ThemeAt(theme), &sPickSong);
    while (sPickSong.patternCount < n) Song_DuplicatePattern(&sPickSong, 0, NULL);
    Editor_SetSong(&sPickEd, &sPickSong, "");
}

/* Moves Vary or Grow until it shows `index` (Grow may skip shapes, so this
 * steps one at a time and gives up after a lap). */
static void pick_StepTo(int index) {
    for (int i = 0; i < 8 && sPick.index != index; i++) Pick_Move(&sPick, &sPickEd, 1);
}

/* ------------------------------------------------------------------ tests */

static void pick_test_cancel_exact(void) {
    bool allSame = true, undoSame = true, dirtySame = true, closed = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        for (int tr = 0; tr < SONG_TRACKS; tr++) {
            for (int dirty = 0; dirty < 2; dirty++) {
                pick_LoadTheme(th);
                Editor_Apply(&sPickEd, ED_BPM_UP, 1); /* one undo entry, so "unchanged" means something */
                sPickEd.dirty = dirty != 0;
                sPickEd.track = tr;
                pick_Snap();
                int undo = sPickEd.undoCount;
                sPick.seed = 1234u + (uint32_t)th;
                Pick_Begin(&sPick, &sPickEd, PICK_FILL);
                Pick_Move(&sPick, &sPickEd, 1);
                Pick_Move(&sPick, &sPickEd, 2);
                Pick_Move(&sPick, &sPickEd, -1);
                Pick_Move(&sPick, &sPickEd, -4);
                Pick_Move(&sPick, &sPickEd, 1);
                Pick_Reroll(&sPick, &sPickEd);
                Pick_Reroll(&sPick, &sPickEd);
                Pick_Reroll(&sPick, &sPickEd);
                Pick_Cancel(&sPick, &sPickEd);
                if (!pick_SongSame()) allSame = false;
                if (sPickEd.undoCount != undo) undoSame = false;
                if (sPickEd.dirty != (dirty != 0)) dirtySame = false;
                if (sPick.kind != PICK_NONE) closed = false;
            }
        }
        /* Chords, through every key it offers. */
        pick_LoadTheme(th);
        pick_Snap();
        int undo = sPickEd.undoCount;
        Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
        Pick_Move(&sPick, &sPickEd, 3);
        Pick_KeyStep(&sPick, &sPickEd, 1);
        Pick_Toggle(&sPick, &sPickEd);
        Pick_KeyStep(&sPick, &sPickEd, -5);
        ComposeChords typed = {{0, 1, 2, 3}};
        Pick_SetChords(&sPick, &sPickEd, &typed);
        Pick_Move(&sPick, &sPickEd, -1);
        Pick_Cancel(&sPick, &sPickEd);
        if (!pick_SongSame()) allSame = false;
        if (sPickEd.undoCount != undo) undoSame = false;
        if (sPickEd.dirty) dirtySame = false;
        if (sPick.kind != PICK_NONE) closed = false;
    }
    CHECK(allSame, "pick: Esc after any number of choices puts the song back byte for byte");
    CHECK(undoSame, "pick: a cancelled picker leaves the undo history alone");
    CHECK(dirtySame, "pick: a cancelled picker leaves the dirty flag as it was");
    CHECK(closed, "pick: Esc closes the picker");
    CHECK(strcmp(sPickEd.status, "Put back as it was") == 0, "pick: Esc says it put things back");
}

static void pick_test_keep_one_undo(void) {
    bool pushed = true, plusOne = true, undoes = true, dirty = true, changed = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        for (int tr = 0; tr < SONG_TRACKS; tr++) {
            for (int all = 0; all < 2; all++) {
                pick_LoadTheme(th);
                sPickEd.track = tr;
                pick_Snap();
                int undo = sPickEd.undoCount;
                sPick.seed = 99u;
                Pick_Begin(&sPick, &sPickEd, PICK_FILL);
                /* The next style along always differs from the track's own
                 * (on the lead: from the theme's rhythm to a new tune). */
                Pick_Move(&sPick, &sPickEd, 1);
                Pick_Reroll(&sPick, &sPickEd);
                if (!Pick_Keep(&sPick, &sPickEd, all != 0)) pushed = false;
                if (pick_SongSame()) changed = false;
                if (sPickEd.undoCount != undo + 1) plusOne = false;
                if (!sPickEd.dirty) dirty = false;
                Editor_Apply(&sPickEd, ED_UNDO, 0);
                if (!pick_SongSame() || sPickEd.undoCount != undo) undoes = false;
            }
        }
        pick_LoadTheme(th);
        pick_Snap();
        Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
        Pick_Move(&sPick, &sPickEd, 1);
        Pick_Toggle(&sPick, &sPickEd);
        if (!Pick_Keep(&sPick, &sPickEd, false)) pushed = false;
        if (sPickEd.undoCount != 1) plusOne = false;
        Editor_Apply(&sPickEd, ED_UNDO, 0);
        if (!pick_SongSame()) undoes = false;
    }
    CHECK(pushed, "pick: Keep after a change pushes an undo entry");
    CHECK(changed, "pick: two styles on, the kept pattern differs from the theme");
    CHECK(plusOne, "pick: Keep is exactly one undo entry, however many choices were tried");
    CHECK(dirty, "pick: Keep marks the song changed");
    CHECK(undoes, "pick: one Ctrl+Z after Keep gives back the song byte for byte");

    /* Opening on a theme's bass, arp or beat starts on that very style in
     * the theme's own register, so the theme plays on unchanged. */
    bool asIs = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        for (int tr = 0; tr < SONG_TRACKS; tr++) {
            if (tr == TRACK_LEAD) continue;
            pick_LoadTheme(th);
            sPickEd.track = tr;
            pick_Snap();
            Pick_Begin(&sPick, &sPickEd, PICK_FILL);
            if (!pick_SongSame()) asIs = false;
            Pick_Cancel(&sPick, &sPickEd);
        }
    }
    CHECK(asIs, "pick: Fill opens on every theme's own bass, arp and beat, note for note");

    /* So keeping it at once changes nothing and is not an edit. */
    pick_LoadTheme(1);
    sPickEd.track = TRACK_DRUMS;
    pick_Snap();
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    CHECK(sPick.index == Music_ThemeAt(1)->drumStyle, "pick: Fill opens on the style the track already has");
    CHECK(pick_SongSame(), "pick: opening on the track's own style sounds the same");
    CHECK(!Pick_Keep(&sPick, &sPickEd, false), "pick: keeping what was there pushes nothing");
    CHECK(sPickEd.undoCount == 0 && !sPickEd.dirty, "pick: a no-op Keep is not an edit");
    CHECK(strcmp(sPickEd.status, "Nothing changed") == 0, "pick: a no-op Keep says so");
    CHECK(sPick.kind == PICK_NONE, "pick: Keep closes the picker");

    /* The status names the track and the style, and how to undo it. */
    pick_LoadTheme(0);
    sPickEd.track = TRACK_DRUMS;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    pick_MoveTo(3);
    Pick_Keep(&sPick, &sPickEd, false);
    CHECK(strcmp(sPickEd.status, "Drums: breakbeat (Ctrl+Z undoes it)") == 0, "pick: Keep's status names track and style");
    pick_LoadTheme(0);
    sPickEd.track = TRACK_BASS;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    pick_MoveTo(4);
    Pick_Keep(&sPick, &sPickEd, true);
    CHECK(strcmp(sPickEd.status, "Bass: walking, every pattern (Ctrl+Z undoes it)") == 0, "pick: Shift+Enter's status says every pattern");
}

static void pick_test_wrap_count(void) {
    bool counts = true, wraps = true;
    pick_LoadTheme(2);
    for (int tr = 0; tr < SONG_TRACKS; tr++) {
        sPickEd.track = tr;
        Pick_Begin(&sPick, &sPickEd, PICK_FILL);
        int n = Compose_StyleCount((SongTrack)tr);
        if (Pick_Count(&sPick) != n) counts = false;
        pick_MoveTo(0);
        Pick_Move(&sPick, &sPickEd, -1);
        if (sPick.index != n - 1) wraps = false;
        Pick_Move(&sPick, &sPickEd, 1);
        if (sPick.index != 0) wraps = false;
        Pick_Move(&sPick, &sPickEd, n);
        if (sPick.index != 0) wraps = false;
        Pick_Move(&sPick, &sPickEd, -n - 1);
        if (sPick.index != n - 1) wraps = false;
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(counts, "pick: Fill steps through the track's own styles");
    CHECK(wraps, "pick: Left/Right wrap both ways");
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    CHECK(Pick_Count(&sPick) == Compose_ProgressionCount(), "pick: Chords steps through the named progressions");
    pick_MoveTo(0);
    Pick_Move(&sPick, &sPickEd, -1);
    CHECK(sPick.index == Compose_ProgressionCount() - 1, "pick: Chords wraps from the first to the last");
    Pick_Cancel(&sPick, &sPickEd);
    CHECK(Pick_Count(&sPick) == 0, "pick: a closed picker has no choices");
    sPick.kind = PICK_NONE;
    Pick_Move(&sPick, &sPickEd, 1); /* nothing to do, nothing to crash */
    CHECK(sPick.kind == PICK_NONE, "pick: moving a closed picker does nothing");
}

static void pick_test_own_track(void) {
    bool previewOwn = true, otherPatterns = true, keepOwn = true, undoAll = true, eachOwnChords = true;
    for (int th = 0; th < Music_ThemeCount(); th += 3) {
        for (int tr = 0; tr < SONG_TRACKS; tr++) {
            ComposeChords other;
            pick_ThreePatterns(th, &other);
            sPickEd.pattern = 1;
            sPickEd.track = tr;
            pick_Snap();
            sPick.seed = 5u;
            Pick_Begin(&sPick, &sPickEd, PICK_FILL);
            Pick_Move(&sPick, &sPickEd, 1);
            if (!pick_OthersEqual(&sPickEd.song.patterns[1], &sPickSnap.patterns[1], tr)) previewOwn = false;
            for (int p = 0; p < SONG_MAX_PATTERNS; p++)
                if (p != 1 && memcmp(&sPickEd.song.patterns[p], &sPickSnap.patterns[p], sizeof(SongPattern)) != 0) otherPatterns = false;
            Pick_Keep(&sPick, &sPickEd, true);
            for (int p = 0; p < 3; p++)
                if (!pick_OthersEqual(&sPickEd.song.patterns[p], &sPickSnap.patterns[p], tr)) keepOwn = false;
            /* Each pattern was filled on its own chords: every bass style
             * starts each bar on its root, and the roots are the theme's,
             * the other progression's, and i VI iv V for the beat-only
             * pattern. */
            if (tr == TRACK_BASS) {
                ComposeKey k = pick_ThemeKey(th);
                ComposeChords expect[3] = {pick_ThemeChords(th), other, {{0, 5, 3, 4}}};
                for (int p = 0; p < 3; p++)
                    for (int bar = 0; bar < 4; bar++) {
                        int n = sPickEd.song.patterns[p].tone[TRACK_BASS][bar * 16];
                        if (n < 0 || (n % 12) != pick_RootPc(k, expect[p].degree[bar])) eachOwnChords = false;
                    }
            }
            Editor_Apply(&sPickEd, ED_UNDO, 0);
            if (!pick_SongSame()) undoAll = false;
        }
    }
    CHECK(previewOwn, "pick: a Fill preview rewrites only its own track");
    CHECK(otherPatterns, "pick: a Fill preview leaves every other pattern alone");
    CHECK(keepOwn, "pick: Shift+Enter rewrites only that track, in every pattern");
    CHECK(eachOwnChords, "pick: Shift+Enter fills every pattern on its own chords");
    CHECK(undoAll, "pick: one Ctrl+Z takes back a Shift+Enter over every pattern");
}

static void pick_test_chords_read_first(void) {
    bool chordsAtBegin = true, chordsKept = true, arpKept = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        ComposeKey k = pick_ThemeKey(th);
        ComposeChords prog = pick_ThemeChords(th);
        for (int tr = TRACK_BASS; tr <= TRACK_ARP; tr++) {
            for (int style = 0; style < Compose_StyleCount((SongTrack)tr); style++) {
                pick_LoadTheme(th);
                sPickEd.track = tr;
                Pick_Begin(&sPick, &sPickEd, PICK_FILL);
                if (!pick_SameChords(&sPick.chords, &prog)) chordsAtBegin = false;
                pick_MoveTo(style);
                ComposeChords after;
                Compose_ReadChords(&sPickEd.song.patterns[0], k, &after);
                if (!pick_SameChords(&after, &prog)) { if (tr == TRACK_BASS) chordsKept = false; else arpKept = false; }
                Pick_Cancel(&sPick, &sPickEd);
            }
        }
    }
    CHECK(chordsAtBegin, "pick: Fill reads the theme's chords when it opens");
    CHECK(chordsKept, "pick: every bass style keeps the chords the bass was read from");
    CHECK(arpKept, "pick: every arp style leaves the chords where they were");
}

static void pick_test_in_key_and_bars(void) {
    bool inKey = true, everyBar = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        ComposeKey k = pick_ThemeKey(th);
        for (int tr = TRACK_BASS; tr <= TRACK_LEAD; tr++) {
            for (int seed = 0; seed < 2; seed++) {
                pick_LoadTheme(th);
                sPickEd.track = tr;
                sPick.seed = 77u + (uint32_t)seed * 1000u;
                Pick_Begin(&sPick, &sPickEd, PICK_FILL);
                for (int i = 0; i < Pick_Count(&sPick); i++) {
                    pick_MoveTo(i);
                    const SongPattern *p = &sPickEd.song.patterns[0];
                    if (!pick_TrackInKey(k, p->tone[tr])) inKey = false;
                    if (tr != TRACK_LEAD)
                        for (int bar = 0; bar < 4; bar++)
                            if (!pick_BarHasOnset(p->tone[TRACK_BASS], bar) || !pick_BarHasOnset(p->tone[TRACK_ARP], bar)) everyBar = false;
                }
                Pick_Cancel(&sPick, &sPickEd);
            }
        }
    }
    CHECK(inKey, "pick: every note a Fill writes is in key and in range");
    CHECK(everyBar, "pick: after a bass or arp fill every bar still has both");
}

static void pick_test_fit_rhythm(void) {
    bool opensOnRhythm = true, maskKept = true, strong = true, inKey = true, others = true, rhythmSurvives = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        ComposeKey k = pick_ThemeKey(th);
        pick_LoadTheme(th);
        /* A rhythm typed by hand, pitches anyhow: some out of key, some
         * held, some far from the theme's register. */
        int8_t *lead = sPickSong.patterns[0].tone[TRACK_LEAD];
        memset(lead, NOTE_NONE, SONG_STEPS);
        for (int st = 0; st < SONG_STEPS; st += 3) lead[st] = (int8_t)(48 + (st * 7) % 30);
        for (int st = 1; st < SONG_STEPS; st += 9) lead[st] = NOTE_HOLD;
        Editor_SetSong(&sPickEd, &sPickSong, "");
        sPickEd.track = TRACK_LEAD;
        pick_Snap();
        sPick.seed = 31u;
        Pick_Begin(&sPick, &sPickEd, PICK_FILL);
        if (sPick.index != 1) opensOnRhythm = false;
        const SongPattern *p = &sPickEd.song.patterns[0];
        if (!pick_SameMask(p->tone[TRACK_LEAD], sPickSnap.patterns[0].tone[TRACK_LEAD])) maskKept = false;
        if (!pick_TrackInKey(k, p->tone[TRACK_LEAD])) inKey = false;
        if (!pick_OthersEqual(p, &sPickSnap.patterns[0], TRACK_LEAD)) others = false;
        for (int st = 0; st < SONG_STEPS; st += 4) {
            int n = p->tone[TRACK_LEAD][st];
            if (n >= 0 && !Compose_IsChordTone(k, sPick.chords.degree[st / 16], n)) strong = false;
        }
        /* Trying New tune and coming back must not hand Fit my rhythm the
         * new tune's rhythm: every choice starts from the snapshot. */
        Pick_Move(&sPick, &sPickEd, 1);
        Pick_Move(&sPick, &sPickEd, 1);
        if (!pick_SameMask(p->tone[TRACK_LEAD], sPickSnap.patterns[0].tone[TRACK_LEAD])) rhythmSurvives = false;
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(opensOnRhythm, "pick: Fill on a lead with notes opens on Fit my rhythm");
    CHECK(rhythmSurvives, "pick: Fit my rhythm after New tune still fits the rhythm typed, not the last take's");
    CHECK(maskKept, "pick: Fit my rhythm keeps every note, hold and silence where it was");
    CHECK(strong, "pick: Fit my rhythm puts the strong beats on chord tones");
    CHECK(inKey, "pick: Fit my rhythm puts every note in key");
    CHECK(others, "pick: Fit my rhythm touches only the lead");

    /* The take follows the seed: the same seed gives the same tune, and
     * rerolls give others. */
    pick_LoadTheme(4);
    sPickEd.track = TRACK_LEAD;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    pick_MoveTo(0);
    SongPattern first, again;
    sPick.seed = 500u;
    Pick_Reroll(&sPick, &sPickEd);
    memcpy(&first, &sPickEd.song.patterns[0], sizeof(first));
    int distinct = 0;
    SongPattern seen[8];
    for (int i = 0; i < 8; i++) {
        Pick_Reroll(&sPick, &sPickEd);
        memcpy(&seen[i], &sPickEd.song.patterns[0], sizeof(SongPattern));
        bool fresh = true;
        for (int j = 0; j < i; j++) if (memcmp(&seen[j], &seen[i], sizeof(SongPattern)) == 0) fresh = false;
        if (fresh) distinct++;
    }
    sPick.seed = 500u;
    Pick_Reroll(&sPick, &sPickEd);
    memcpy(&again, &sPickEd.song.patterns[0], sizeof(again));
    CHECK(memcmp(&first, &again, sizeof(first)) == 0, "pick: the same take comes back from the same seed");
    CHECK(distinct >= 4, "pick: R gives other tunes");
    Pick_Cancel(&sPick, &sPickEd);

    /* An empty lead has no rhythm to fit: it opens on New tune, and Fit my
     * rhythm says what it needs instead of silently doing nothing. */
    pick_LoadTheme(0);
    memset(sPickSong.patterns[0].tone[TRACK_LEAD], NOTE_NONE, SONG_STEPS);
    Editor_SetSong(&sPickEd, &sPickSong, "");
    sPickEd.track = TRACK_LEAD;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    CHECK(sPick.index == 0, "pick: Fill on an empty lead opens on New tune");
    CHECK(pick_BarHasOnset(sPickEd.song.patterns[0].tone[TRACK_LEAD], 0), "pick: New tune writes a tune on an empty lead");
    Pick_Move(&sPick, &sPickEd, 1);
    CHECK(strcmp(sPickEd.status, "Fit my rhythm needs notes on the lead first") == 0, "pick: Fit my rhythm on an empty lead says why nothing plays");
    Pick_Cancel(&sPick, &sPickEd);
}

/* A new tune sits where the tune it replaces sat: on every theme, New tune
 * is exactly Compose_Lead at the theme's own tonic, lead octave and chords.
 * (Compared with the writer itself rather than read back with
 * Compose_LeadOctave, which is exact for the themes' own tunes but not for
 * every take: one rockdrift take reads back an octave high.) */
static void pick_test_lead_register(void) {
    bool same = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        const MusicTheme *t = Music_ThemeAt(th);
        ComposeKey k = pick_ThemeKey(th);
        ComposeChords prog = pick_ThemeChords(th);
        for (uint32_t seed = 1; seed <= 3; seed++) {
            pick_LoadTheme(th);
            sPickEd.track = TRACK_LEAD;
            sPick.seed = seed * 7919u;
            Pick_Begin(&sPick, &sPickEd, PICK_FILL);
            pick_MoveTo(0);
            SongPattern want;
            memcpy(&want, &sPick.before, sizeof(want));
            Compose_Lead(&want, k, t->rootMidi, &prog, t->leadOctave, sPick.seed);
            if (sPick.tonicMidi != t->rootMidi || memcmp(&want, &sPickEd.song.patterns[0], sizeof(want)) != 0) same = false;
            Pick_Cancel(&sPick, &sPickEd);
        }
    }
    CHECK(same, "pick: New tune keeps the theme's register and lead octave");
}

static void pick_test_drums_only(void) {
    /* The owner's own file: a beat and nothing else, no key. */
    Song_Init(&sPickSong, "Drums only");
    Compose_Drums(&sPickSong.patterns[0], 1, 0u);
    Editor_SetSong(&sPickEd, &sPickSong, "");
    ComposeChords expect = {{0, 5, 3, 4}};
    ComposeKey aMinor = {9, SCALE_MINOR};
    bool opens = true, chords = true, inKey = true, roots = true;
    for (int tr = TRACK_BASS; tr <= TRACK_LEAD; tr++) {
        sPickEd.track = tr;
        if (!Pick_Begin(&sPick, &sPickEd, PICK_FILL)) opens = false;
        if (!pick_SameChords(&sPick.chords, &expect) || !sPick.keyGuessed) chords = false;
        if (sPick.key.root != aMinor.root || sPick.key.scale != aMinor.scale) chords = false;
        for (int i = 0; i < Pick_Count(&sPick); i++) {
            pick_MoveTo(i);
            if (!pick_TrackInKey(aMinor, sPickEd.song.patterns[0].tone[tr])) inKey = false;
        }
        pick_MoveTo(0);
        Pick_Keep(&sPick, &sPickEd, false); /* bass, then an arp over it, then a tune over both */
    }
    for (int bar = 0; bar < 4; bar++) {
        int n = sPickEd.song.patterns[0].tone[TRACK_BASS][bar * 16];
        if (n < 0 || n % 12 != pick_RootPc(aMinor, expect.degree[bar])) roots = false;
    }
    CHECK(opens, "pick: Fill opens on a song with nothing but drums");
    CHECK(chords, "pick: a pattern with no bass or arp is filled on i VI iv V in A minor");
    CHECK(inKey, "pick: every style fills a drums-only song in key");
    CHECK(roots, "pick: the bass Fill gives a drums-only song sits on i VI iv V");
    CHECK(sPickEd.undoCount == 3, "pick: three Keeps are three undo entries");
    CHECK(sPickEd.song.keyRoot == -1, "pick: Fill leaves the key line alone");

    /* No bass, but an arp: the chords are read from the arp. */
    bool fromArp = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        Music_ThemeToSong(Music_ThemeAt(th), &sPickSong);
        memset(sPickSong.patterns[0].tone[TRACK_BASS], NOTE_NONE, SONG_STEPS);
        Editor_SetSong(&sPickEd, &sPickSong, "");
        sPickEd.track = TRACK_BASS;
        Pick_Begin(&sPick, &sPickEd, PICK_FILL);
        ComposeChords prog = pick_ThemeChords(th);
        if (!pick_SameChords(&sPick.chords, &prog)) fromArp = false;
        for (int bar = 0; bar < 4; bar++) {
            int n = sPickEd.song.patterns[0].tone[TRACK_BASS][bar * 16];
            if (n < 0 || n % 12 != pick_RootPc(pick_ThemeKey(th), prog.degree[bar])) fromArp = false;
        }
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(fromArp, "pick: with no bass, the arp says what chords to fill the bass on");
}

static void pick_test_labels(void) {
    char label[PICK_TEXT_LEN], info[PICK_TEXT_LEN], detail[PICK_TEXT_LEN];
    bool labels = true, details = true, infos = true, keys = true;
    pick_LoadTheme(0);
    for (int tr = 0; tr < SONG_TRACKS; tr++) {
        sPickEd.track = tr;
        Pick_Begin(&sPick, &sPickEd, PICK_FILL);
        for (int i = 0; i < Pick_Count(&sPick); i++) {
            pick_MoveTo(i);
            Pick_Label(&sPick, label, sizeof(label));
            Pick_Info(&sPick, info, sizeof(info));
            Pick_Detail(&sPick, detail, sizeof(detail));
            if (!pick_TextOk(label, PICK_LABEL_MAX, true) || !strstr(label, Compose_StyleName((SongTrack)tr, i))) labels = false;
            if (!pick_TextOk(info, 40, false) || (tr == TRACK_DRUMS) != (info[0] == '\0')) infos = false;
            if (!pick_TextOk(detail, 100, true)) details = false;
        }
        const char *const *k = Pick_Keys(&sPick);
        for (int i = 0; k && k[i]; i++) if (i >= 12 || !pick_TextOk(k[i], 13, false)) keys = false;
        if (!k) keys = false;
        Pick_Cancel(&sPick, &sPickEd);
    }
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    for (int i = -1; i < Compose_ProgressionCount(); i++) {
        for (int root = 0; root < 12; root++) {
            for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
                for (int guessed = 0; guessed < 2; guessed++) {
                    for (int flag = 0; flag < 2; flag++) {
                        /* The label only reads these, so set them directly to
                         * reach every key without rewriting the pattern. */
                        sPick.index = i;
                        sPick.chords = i >= 0 ? Compose_ProgressionAt(i)->chords : (ComposeChords){{6, 6, 6, 6}};
                        sPick.key.root = root;
                        sPick.key.scale = scale;
                        sPick.keyGuessed = guessed != 0;
                        sPick.flag = flag != 0;
                        Pick_Label(&sPick, label, sizeof(label));
                        Pick_Info(&sPick, info, sizeof(info));
                        Pick_Detail(&sPick, detail, sizeof(detail));
                        if (!pick_TextOk(label, PICK_LABEL_MAX, true)) labels = false;
                        if (!pick_TextOk(info, 40, true)) infos = false;
                        if (!pick_TextOk(detail, 100, true)) details = false;
                    }
                }
            }
        }
    }
    const char *const *k = Pick_Keys(&sPick);
    for (int i = 0; k && k[i]; i++) if (i >= 12 || !pick_TextOk(k[i], 13, false)) keys = false;
    sPick.index = -1;
    sPick.chords = (ComposeChords){{0, 1, 2, 3}};
    Pick_Label(&sPick, label, sizeof(label));
    CHECK(strstr(label, "< 1 2 3 4 >") != NULL, "pick: chords not in the table read as their numbers");
    Pick_Cancel(&sPick, &sPickEd);
    /* Vary and Grow, every choice, on a two-digit pattern number. */
    pick_ManyPatterns(0, 12);
    sPickEd.pattern = 11;
    for (int kind = PICK_VARY; kind <= PICK_GROW; kind++) {
        Pick_Begin(&sPick, &sPickEd, (PickKind)kind);
        for (int i = 0; i < Pick_Count(&sPick) + 1; i++) {
            Pick_Label(&sPick, label, sizeof(label));
            Pick_Info(&sPick, info, sizeof(info));
            Pick_Detail(&sPick, detail, sizeof(detail));
            if (!pick_TextOk(label, PICK_LABEL_MAX, true)) labels = false;
            if (!pick_TextOk(info, 40, false) || !info[0]) infos = false;
            if (!pick_TextOk(detail, 100, true)) details = false;
            Pick_Move(&sPick, &sPickEd, 1);
        }
        const char *const *vk = Pick_Keys(&sPick);
        for (int i = 0; vk && vk[i]; i++) if (i >= 12 || !pick_TextOk(vk[i], 13, false)) keys = false;
        if (!vk) keys = false;
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(labels, "pick: every label is short sentence-case ASCII");
    CHECK(infos, "pick: every chord line beside the label is short ASCII");
    CHECK(details, "pick: every key line fits the band at T8");
    CHECK(keys, "pick: every hint-card line fits the 13-glyph card");
    Pick_Label(&sPick, label, sizeof(label));
    Pick_Detail(&sPick, detail, sizeof(detail));
    CHECK(label[0] == '\0' && detail[0] == '\0' && Pick_Keys(&sPick) == NULL, "pick: a closed picker has nothing to show");
}

static void pick_test_live_memcpy(void) {
    pick_ThreePatterns(5, NULL);
    const Song *where = &sPickEd.song;
    sPickEd.pattern = 2;
    sPickEd.track = TRACK_ARP;
    pick_Snap();
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    Pick_Move(&sPick, &sPickEd, 2);
    bool onlyThat = true;
    for (int p = 0; p < SONG_MAX_PATTERNS; p++)
        if (p != 2 && memcmp(&sPickEd.song.patterns[p], &sPickSnap.patterns[p], sizeof(SongPattern)) != 0) onlyThat = false;
    CHECK(&sPickEd.song == where, "pick: the live song never moves (the player points at it)");
    CHECK(onlyThat, "pick: a preview writes only the pattern being picked for");
    CHECK(sPickEd.song.patternCount == 3 && sPickEd.song.orderCount == 3, "pick: Fill never changes the pattern or order count");
    CHECK(sPickEd.pattern == 2 && sPickEd.track == TRACK_ARP, "pick: the picker leaves the cursor where it was");
    Pick_Keep(&sPick, &sPickEd, false);
    CHECK(&sPickEd.song == where && sPickEd.pattern == 2, "pick: Keep leaves the song and the cursor in place");
    Editor_Apply(&sPickEd, ED_UNDO, 0);
    CHECK(sPickEd.pattern == 2 && sPickEd.track == TRACK_ARP, "pick: Ctrl+Z lands where the picker opened");
}

static void pick_test_chords_picker(void) {
    bool opensOnOwn = true, unchanged = true, masks = true, roots = true, drumsLead = true, fitted = true, unfit = true;
    bool leadMoved = false;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        ComposeKey k = pick_ThemeKey(th);
        ComposeChords prog = pick_ThemeChords(th);
        pick_LoadTheme(th);
        pick_Snap();
        Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
        if (sPick.index != Compose_ProgressionFind(&prog) || sPick.index < 0 || sPick.keyGuessed) opensOnOwn = false;
        if (!pick_SongSame()) unchanged = false;
        const SongPattern *before = &sPickSnap.patterns[0];
        for (int j = 0; j < Compose_ProgressionCount(); j++) {
            pick_MoveTo(j);
            const SongPattern *p = &sPickEd.song.patterns[0];
            if (!pick_SameMask(p->tone[TRACK_BASS], before->tone[TRACK_BASS]) || !pick_SameMask(p->tone[TRACK_ARP], before->tone[TRACK_ARP])) masks = false;
            if (!pick_TrackEqual(p, before, TRACK_DRUMS) || !pick_TrackEqual(p, before, TRACK_LEAD)) drumsLead = false;
            for (int bar = 0; bar < 4; bar++) {
                int n = p->tone[TRACK_BASS][bar * 16];
                if (n < 0 || n % 12 != pick_RootPc(k, sPick.chords.degree[bar])) roots = false;
            }
            Pick_Toggle(&sPick, &sPickEd);
            /* The tune is fitted to the chords on trial, exactly as the
             * composer fits it, from the tune as it was. */
            SongPattern want;
            memcpy(&want, before, sizeof(want));
            Compose_Reharmonise(&want, k, &prog, &sPick.chords, true);
            if (memcmp(&want, p, sizeof(want)) != 0) fitted = false;
            if (!pick_TrackInKey(k, p->tone[TRACK_LEAD]) || !pick_SameMask(p->tone[TRACK_LEAD], before->tone[TRACK_LEAD])) fitted = false;
            if (!pick_TrackEqual(p, before, TRACK_LEAD)) leadMoved = true;
            Pick_Toggle(&sPick, &sPickEd);
            if (!pick_TrackEqual(p, before, TRACK_LEAD)) unfit = false;
        }
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(opensOnOwn, "pick: Chords opens on the theme's own progression in its stored key");
    CHECK(unchanged, "pick: opening Chords on the chords a pattern has changes nothing");
    CHECK(masks, "pick: every progression keeps the bass and arp rhythm");
    CHECK(roots, "pick: the bass moves onto every new bar root");
    CHECK(drumsLead, "pick: Chords leaves the drums and (unless asked) the lead alone");
    CHECK(fitted, "pick: F fits the tune to the chords on trial, keeping its rhythm");
    CHECK(leadMoved, "pick: fitting the tune to new chords moves some of its notes");
    CHECK(unfit, "pick: F again gives the tune back as it was");

    /* Up/Down and back gives back the pattern; the key is then chosen, not
     * guessed. */
    pick_LoadTheme(3);
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    pick_MoveTo(2);
    SongPattern was;
    memcpy(&was, &sPickEd.song.patterns[0], sizeof(was));
    int root = sPick.key.root;
    Pick_KeyStep(&sPick, &sPickEd, 1);
    CHECK(sPick.key.root == (root + 1) % 12, "pick: Up moves the key up a semitone");
    CHECK(memcmp(&was, &sPickEd.song.patterns[0], sizeof(was)) != 0, "pick: a new key re-voices the pattern");
    Pick_KeyStep(&sPick, &sPickEd, -1);
    CHECK(memcmp(&was, &sPickEd.song.patterns[0], sizeof(was)) == 0, "pick: Up then Down gives the pattern back");
    Pick_KeyStep(&sPick, &sPickEd, -12);
    CHECK(sPick.key.root == root, "pick: the key root wraps round the octave");
    Pick_Cancel(&sPick, &sPickEd);

    /* Keep stores the key, in the same single undo entry. */
    pick_LoadTheme(6);
    sPickSong.keyRoot = -1;
    Editor_SetSong(&sPickEd, &sPickSong, "");
    pick_Snap();
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    CHECK(sPick.keyGuessed, "pick: a keyless song's key is shown as a guess");
    char label[PICK_TEXT_LEN];
    Pick_Label(&sPick, label, sizeof(label));
    CHECK(label[strlen(label) - 1] == '?', "pick: a guessed key carries a question mark");
    Pick_Move(&sPick, &sPickEd, 1);
    ComposeKey chosen = sPick.key;
    CHECK(Pick_Keep(&sPick, &sPickEd, false), "pick: Chords Keep after a change is an edit");
    CHECK(sPickEd.song.keyRoot == chosen.root && sPickEd.song.keyScale == chosen.scale, "pick: Chords Keep writes the key into the song");
    CHECK(sPickEd.undoCount == 1, "pick: the key and the chords are one undo entry");
    CHECK(strncmp(sPickEd.status, "Chords: ", 8) == 0, "pick: Chords Keep names the chords");
    Editor_Apply(&sPickEd, ED_UNDO, 0);
    CHECK(pick_SongSame() && sPickEd.song.keyRoot == -1, "pick: Ctrl+Z takes back the chords and the key together");

    /* Shift+Enter: every pattern moves onto the chords, the drums stay. */
    ComposeChords other;
    pick_ThreePatterns(9, &other);
    ComposeKey k = pick_ThemeKey(9);
    pick_Snap();
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    pick_MoveTo(Compose_ProgressionFind(&other) + 2);
    ComposeChords target = sPick.chords;
    Pick_Keep(&sPick, &sPickEd, true);
    bool drums = true, reads = true;
    for (int p = 0; p < 3; p++) {
        if (!pick_TrackEqual(&sPickEd.song.patterns[p], &sPickSnap.patterns[p], TRACK_DRUMS)) drums = false;
        ComposeChords got;
        Compose_ReadChords(&sPickEd.song.patterns[p], k, &got);
        if (!pick_SameChords(&got, &target)) reads = false;
    }
    CHECK(drums, "pick: Chords over every pattern leaves every beat alone");
    CHECK(reads, "pick: Chords over every pattern puts each one on the chords picked");
    CHECK(sPickEd.undoCount == 1, "pick: Chords over every pattern is one undo entry");
    Editor_Apply(&sPickEd, ED_UNDO, 0);
    CHECK(pick_SongSame(), "pick: one Ctrl+Z takes Chords back from every pattern");

    /* A typed chord line goes through the same picker. */
    pick_LoadTheme(0);
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    ComposeChords typed;
    char err[64];
    CHECK(Compose_ParseChordLine("1 6 4 5", sPick.key, &typed, err, sizeof(err)), "pick: the chord line parses");
    CHECK(Pick_SetChords(&sPick, &sPickEd, &typed), "pick: typed chords go into the picker");
    ComposeChords iviivv = {{0, 5, 3, 4}};
    CHECK(sPick.index == Compose_ProgressionFind(&iviivv) && sPick.index >= 0, "pick: typed chords that have a name show it");
    ComposeChords odd = {{0, 1, 2, 3}};
    Pick_SetChords(&sPick, &sPickEd, &odd);
    CHECK(sPick.index == -1, "pick: typed chords without a name are kept as typed");
    bool typedRoots = true;
    for (int bar = 0; bar < 4; bar++) {
        int n = sPickEd.song.patterns[0].tone[TRACK_BASS][bar * 16];
        if (n < 0 || n % 12 != pick_RootPc(sPick.key, odd.degree[bar])) typedRoots = false;
    }
    CHECK(typedRoots, "pick: the bass follows typed chords");
    Pick_Move(&sPick, &sPickEd, 1);
    CHECK(sPick.index == 0, "pick: Right from typed chords goes to the first named one");
    Pick_SetChords(&sPick, &sPickEd, &odd);
    Pick_Move(&sPick, &sPickEd, -1);
    CHECK(sPick.index == Compose_ProgressionCount() - 1, "pick: Left from typed chords goes to the last named one");
    Pick_Cancel(&sPick, &sPickEd);
    sPick.kind = PICK_FILL;
    CHECK(!Pick_SetChords(&sPick, &sPickEd, &odd), "pick: typed chords only go to Chords");
    sPick.kind = PICK_NONE;

    /* Chords on an empty pattern writes a backing on i VI iv V. */
    Song_Init(&sPickSong, "Empty");
    Editor_SetSong(&sPickEd, &sPickSong, "");
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    CHECK(pick_SameChords(&sPick.chords, &iviivv), "pick: Chords on an empty pattern starts on i VI iv V");
    bool backing = true;
    for (int bar = 0; bar < 4; bar++)
        if (!pick_BarHasOnset(sPickEd.song.patterns[0].tone[TRACK_BASS], bar) || !pick_BarHasOnset(sPickEd.song.patterns[0].tone[TRACK_ARP], bar)) backing = false;
    CHECK(backing, "pick: Chords on an empty pattern writes a bass and an arp in every bar");
    Pick_Cancel(&sPick, &sPickEd);
}

static void pick_test_open_over(void) {
    pick_LoadTheme(0);
    pick_Snap();
    CHECK(!Pick_Begin(&sPick, &sPickEd, PICK_NONE) && sPick.kind == PICK_NONE, "pick: PICK_NONE opens nothing");
    CHECK(pick_SongSame(), "pick: and changes nothing");

    /* Opening over a live picker puts the first one back. */
    sPickEd.track = TRACK_DRUMS;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    Pick_Move(&sPick, &sPickEd, 2);
    Pick_Begin(&sPick, &sPickEd, PICK_CHORDS);
    CHECK(pick_SongSame(), "pick: a picker opened over another puts the first one back first");
    CHECK(sPick.kind == PICK_CHORDS, "pick: and then opens");
    Pick_Cancel(&sPick, &sPickEd);
    CHECK(pick_SongSame(), "pick: cancelling the second leaves the song as it was");

    /* The same with the song-wide pickers, either way round. */
    Pick_Begin(&sPick, &sPickEd, PICK_GROW);
    Pick_Begin(&sPick, &sPickEd, PICK_VARY);
    CHECK(sPick.kind == PICK_VARY && sPickEd.song.patternCount == 2 && sPickEd.song.orderCount == 2,
          "pick: Vary opened over Grow starts from the song as it was, plus one pattern");
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    CHECK(sPickEd.song.patternCount == 1 && sPickEd.song.orderCount == 1, "pick: a picker opened over Vary takes its pattern away first");
    Pick_Cancel(&sPick, &sPickEd);
    CHECK(pick_SongSame(), "pick: and Esc then leaves the song exactly as it was");
}

/* ---------------------------------------------------------------- Vary, Grow */

static void pick_test_vary(void) {
    bool opened = true, placed = true, others = true, cursorOn = true, answerFirst = true, firstTake = true;
    bool intoNext = true, inKey = true, cancelExact = true, cancelUndo = true, cancelCursor = true, cancelDirty = true;
    int discriminating = 0;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        ComposeChords other;
        pick_ThreePatterns(th, &other); /* order 0 1 2; 1 is on other chords, 2 has drums only */
        ComposeKey k = pick_ThemeKey(th);
        sPickEd.pattern = 0;
        sPickEd.orderPos = 0;
        pick_Snap();
        int undo = sPickEd.undoCount;
        sPick.seed = 4242u + (uint32_t)th;
        if (!Pick_Begin(&sPick, &sPickEd, PICK_VARY) || sPick.kind != PICK_VARY) opened = false;
        const Song *s = &sPickEd.song;
        /* One new pattern at the end, one new entry right after the source's. */
        if (s->patternCount != 4 || s->orderCount != 4) opened = false;
        if (s->order[0] != 0 || s->order[1] != 3 || s->order[2] != 1 || s->order[3] != 2) placed = false;
        for (int p = 0; p < 3; p++)
            if (memcmp(&s->patterns[p], &sPickSnap.patterns[p], sizeof(SongPattern)) != 0) others = false;
        if (sPickEd.pattern != 3 || sPickEd.orderPos != 1 || sPick.added != 3) cursorOn = false;
        if (sPick.index != VARY_ANSWER) answerFirst = false;

        /* Each kind is the arranger's, built from the snapshot on the
         * source's chords, a fill running into the next entry's first chord. */
        const SongPattern *src = &sPickSnap.patterns[0];
        ComposeChords c;
        Compose_ReadChords(src, k, &c);
        int tonic = Compose_TonicMidi(k, src);
        SongPattern want;
        Arrange_VaryInto(src, k, tonic, &c, VARY_ANSWER, sPick.seed, other.degree[0], &want);
        if (memcmp(&want, &s->patterns[3], sizeof(want)) != 0) firstTake = false;
        for (int kind = 0; kind < ARRANGE_PICKER_KINDS; kind++) {
            pick_StepTo(kind);
            Arrange_VaryInto(src, k, tonic, &c, (VaryKind)kind, sPick.seed, other.degree[0], &want);
            if (memcmp(&want, &s->patterns[3], sizeof(want)) != 0) intoNext = false;
            if (kind == VARY_FILL) {
                SongPattern own;
                Arrange_Vary(src, k, tonic, &c, VARY_FILL, sPick.seed, &own);
                if (memcmp(&own, &want, sizeof(own)) != 0) discriminating++;
            }
            if (!pick_SongInKey(s, k)) inKey = false;
        }
        /* Every kind both ways, three takes, then Esc. */
        Pick_Move(&sPick, &sPickEd, 3);
        Pick_Move(&sPick, &sPickEd, -7);
        Pick_Reroll(&sPick, &sPickEd);
        Pick_Reroll(&sPick, &sPickEd);
        Pick_Move(&sPick, &sPickEd, 1);
        Pick_Reroll(&sPick, &sPickEd);
        Pick_Cancel(&sPick, &sPickEd);
        if (!pick_SongSame()) cancelExact = false;
        if (sPickEd.undoCount != undo) cancelUndo = false;
        if (sPickEd.pattern != 0 || sPickEd.orderPos != 0 || sPick.kind != PICK_NONE) cancelCursor = false;
        if (sPickEd.dirty) cancelDirty = false;
    }
    CHECK(opened, "vary: opens with exactly one more pattern and one more order entry");
    CHECK(placed, "vary: the new entry goes right after the source's position, as Ctrl+D places a copy");
    CHECK(others, "vary: every existing pattern is left as it was");
    CHECK(cursorOn, "vary: the cursor moves onto the new pattern");
    CHECK(answerFirst, "vary: opens on Answer");
    CHECK(firstTake, "vary: the first take is the arranger's answer on the source's chords");
    CHECK(intoNext, "vary: every kind is the arranger's, a fill aimed at the next entry's first chord");
    CHECK(discriminating > 0, "vary: (and the next entry's chord really changes some fills)");
    CHECK(inKey, "vary: every kind stays in the song's key and in range");
    CHECK(cancelExact, "vary: Esc takes the pattern and its entry away, the song back byte for byte");
    CHECK(cancelUndo, "vary: a cancelled Vary leaves the undo history alone");
    CHECK(cancelCursor, "vary: Esc puts the cursor back on the source and closes");
    CHECK(cancelDirty, "vary: a cancelled Vary does not mark the song changed");

    /* Keep: one entry, the cursor stays on what was made, Ctrl+Z goes back
     * to the source in one step. Shift+Enter keeps the same thing. */
    bool keepOne = true, keepDirty = true, keepCursor = true, undoExact = true, undoCursor = true, shiftSame = true;
    for (int th = 0; th < Music_ThemeCount(); th += 2) {
        for (int all = 0; all < 2; all++) {
            pick_ThreePatterns(th, NULL);
            sPickEd.pattern = 1;
            sPickEd.orderPos = 1;
            pick_Snap();
            sPick.seed = 99u;
            Pick_Begin(&sPick, &sPickEd, PICK_VARY);
            Pick_Move(&sPick, &sPickEd, 2);
            if (!Pick_Keep(&sPick, &sPickEd, all != 0)) keepOne = false;
            if (all) { if (memcmp(&sPickEd.song, &sPickWant, sizeof(Song)) != 0) shiftSame = false; }
            else memcpy(&sPickWant, &sPickEd.song, sizeof(Song));
            if (sPickEd.undoCount != 1) keepOne = false;
            if (!sPickEd.dirty) keepDirty = false;
            if (sPickEd.pattern != 3 || sPickEd.orderPos != 2) keepCursor = false;
            Editor_Apply(&sPickEd, ED_UNDO, 0);
            if (!pick_SongSame() || sPickEd.undoCount != 0) undoExact = false;
            if (sPickEd.pattern != 1 || sPickEd.orderPos != 1) undoCursor = false;
        }
    }
    CHECK(keepOne, "vary: Keep is exactly one undo entry");
    CHECK(keepDirty, "vary: Keep marks the song changed");
    CHECK(keepCursor, "vary: after Keep the cursor is on the new pattern, ready to edit it");
    CHECK(undoExact, "vary: one Ctrl+Z takes the variation and its entry back, byte for byte");
    CHECK(undoCursor, "vary: and lands on the source where the picker opened");
    CHECK(shiftSame, "vary: Shift+Enter keeps exactly what Enter keeps");

    /* The words: the label names source, new pattern and kind; Keep says so. */
    pick_LoadTheme(0);
    sPick.seed = 5u;
    Pick_Begin(&sPick, &sPickEd, PICK_VARY);
    char label[PICK_TEXT_LEN];
    Pick_Label(&sPick, label, sizeof(label));
    CHECK(strcmp(label, "Vary 00 into 01  < Answer >") == 0, "vary: the label reads Vary 00 into 01  < Answer >");
    CHECK(Pick_Count(&sPick) == ARRANGE_PICKER_KINDS, "vary: Left/Right step through the picker's five kinds");
    Pick_Move(&sPick, &sPickEd, -2);
    CHECK(sPick.index == ARRANGE_PICKER_KINDS - 1, "vary: and wrap");
    /* A one-pattern song: what follows the new pattern is the source again. */
    ComposeChords own;
    Compose_ReadChords(&sPick.before, pick_ThemeKey(0), &own);
    CHECK(sPick.nextDegree == own.degree[0], "vary: in a one-pattern song the fill runs back into the source");
    CHECK(sPickEd.song.order[0] == 0 && sPickEd.song.order[1] == 1, "vary: a one-pattern song becomes 00 then 01");
    Pick_Keep(&sPick, &sPickEd, false);
    CHECK(strcmp(sPickEd.status, "Pattern 01 varies 00: drop") == 0, "vary: Keep says which pattern varies which, and how");

    /* Reroll: a new take where the kind has one, a status where it does not. */
    bool rerollHonest = true;
    int changedTakes = 0;
    for (int kind = 0; kind < ARRANGE_PICKER_KINDS; kind++) {
        pick_LoadTheme(4);
        sPick.seed = 31u;
        Pick_Begin(&sPick, &sPickEd, PICK_VARY);
        pick_StepTo(kind);
        for (int r = 0; r < 4; r++) {
            SongPattern was;
            memcpy(&was, &sPickEd.song.patterns[1], sizeof(was));
            Editor_SetStatus(&sPickEd, "");
            bool changed = Pick_Reroll(&sPick, &sPickEd);
            bool differs = memcmp(&was, &sPickEd.song.patterns[1], sizeof(was)) != 0;
            if (changed != differs) rerollHonest = false;
            if (!changed && strcmp(sPickEd.status, "This kind has one take. Left/Right for another") != 0) rerollHonest = false;
            if (changed) changedTakes++;
        }
        Pick_Cancel(&sPick, &sPickEd);
    }
    CHECK(rerollHonest, "vary: R reports a new take only when the pattern changed, and says when there is none");
    CHECK(changedTakes > 0, "vary: some kinds have other takes");

    /* Where to listen from: the source's last bar, or the new pattern
     * itself when the cursor's slot does not play the source. */
    pick_ThreePatterns(2, NULL);
    sPickEd.pattern = 2;
    sPickEd.orderPos = 2;
    Pick_Begin(&sPick, &sPickEd, PICK_VARY);
    int pos = -1, step = -1;
    CHECK(Pick_Audition(&sPick, &sPickEd, &pos, &step) && pos == 2 && step == SONG_STEPS - 16, "vary: auditions from the source's last bar");
    Pick_Cancel(&sPick, &sPickEd);
    sPickEd.pattern = 1;
    sPickEd.orderPos = 2; /* this slot plays pattern 2, not the cursor's 1 */
    Pick_Begin(&sPick, &sPickEd, PICK_VARY);
    CHECK(Pick_Audition(&sPick, &sPickEd, &pos, &step) && pos == 3 && step == 0, "vary: or from the new pattern when there is no lead-in");
    Pick_Cancel(&sPick, &sPickEd);
    sPickEd.track = TRACK_BASS;
    Pick_Begin(&sPick, &sPickEd, PICK_FILL);
    CHECK(!Pick_Audition(&sPick, &sPickEd, &pos, &step), "pick: Fill loops its pattern instead of auditioning along the order");
    Pick_Cancel(&sPick, &sPickEd);

    /* No room: refused, nothing changed, a reason given. */
    pick_ManyPatterns(1, SONG_MAX_PATTERNS);
    pick_Snap();
    Editor_SetStatus(&sPickEd, "");
    CHECK(!Pick_Begin(&sPick, &sPickEd, PICK_VARY) && sPick.kind == PICK_NONE, "vary: refuses when the song has no room for a pattern");
    CHECK(pick_SongSame() && sPickEd.status[0] != '\0', "vary: and says so, changing nothing");
    pick_LoadTheme(1);
    for (int i = 1; i < SONG_MAX_ORDER; i++) Editor_Apply(&sPickEd, ED_ORDER_INSERT, 0);
    pick_Snap();
    Editor_SetStatus(&sPickEd, "");
    CHECK(sPickEd.song.orderCount == SONG_MAX_ORDER, "vary: (an order filled to the brim)");
    CHECK(!Pick_Begin(&sPick, &sPickEd, PICK_VARY) && pick_SongSame() && sPickEd.status[0] != '\0', "vary: refuses when the order is full too");
}

static void pick_test_grow(void) {
    bool opensLoop = true, asArranged = true, cursorAt = true, cancelExact = true, cancelUndo = true, cancelCursor = true;
    bool everyShape = true, inKey = true;
    for (int th = 0; th < Music_ThemeCount(); th++) {
        pick_LoadTheme(th);
        ComposeKey k = pick_ThemeKey(th);
        Editor_Apply(&sPickEd, ED_SWING_UP, 0); /* one undo entry, so "unchanged" means something */
        sPickEd.dirty = (th & 1) != 0;
        pick_Snap();
        int undo = sPickEd.undoCount;
        bool dirty = sPickEd.dirty;
        sPick.seed = 777u + (uint32_t)th;
        if (!Pick_Begin(&sPick, &sPickEd, PICK_GROW) || sPick.index != GROW_GAME_LOOP) opensLoop = false;
        for (int lap = 0; lap < 2; lap++) {
            for (int g = 0; g < GROW_COUNT; g++) {
                pick_StepTo(g);
                memcpy(&sPickWant, &sPickSnap, sizeof(Song));
                Arrange_Grow(&sPickWant, 0, k, (GrowShape)g, sPick.seed);
                if (memcmp(&sPickWant, &sPickEd.song, sizeof(Song)) != 0) {
                    if (lap) everyShape = false;
                    else asArranged = false;
                }
                if (sPickEd.pattern != 0 || sPickEd.orderPos != (g == GROW_WITH_ENDING ? 1 : 0)) cursorAt = false;
                if (!pick_SongInKey(&sPickEd.song, k)) inKey = false;
            }
            Pick_Reroll(&sPick, &sPickEd); /* the second lap on another take */
        }
        Pick_Move(&sPick, &sPickEd, -3);
        Pick_Cancel(&sPick, &sPickEd);
        if (!pick_SongSame()) cancelExact = false;
        if (sPickEd.undoCount != undo || sPickEd.dirty != dirty) cancelUndo = false;
        if (sPickEd.pattern != 0 || sPickEd.orderPos != 0) cancelCursor = false;
    }
    CHECK(opensLoop, "grow: opens on Game loop");
    CHECK(asArranged, "grow: every shape is exactly the arranger's on the snapshot");
    CHECK(everyShape, "grow: and so is every shape on a new take, going round both ways");
    CHECK(cursorAt, "grow: the cursor sits on the source's first entry");
    CHECK(inKey, "grow: every pattern of every shape stays in key and in range");
    CHECK(cancelExact, "grow: Esc takes every added pattern away and gives the order back byte for byte");
    CHECK(cancelUndo, "grow: a cancelled Grow leaves the undo history and the dirty flag alone");
    CHECK(cancelCursor, "grow: Esc puts the cursor back");

    /* Keep: one entry and the status the plan promises; Ctrl+Z goes back. */
    pick_LoadTheme(3);
    sPickSong.bpm = 120; /* 8 s a pattern, so Game loop is 64 s */
    Editor_SetSong(&sPickEd, &sPickSong, "");
    pick_Snap();
    sPick.seed = 1u;
    Pick_Begin(&sPick, &sPickEd, PICK_GROW);
    char text[PICK_TEXT_LEN];
    Pick_Label(&sPick, text, sizeof(text));
    CHECK(strcmp(text, "Grow  < Game loop, about 1 min >") == 0, "grow: the label names the shape");
    Pick_Info(&sPick, text, sizeof(text));
    CHECK(strcmp(text, "7 patterns, about 1:04") == 0, "grow: beside it, the real size and length");
    CHECK(Pick_Count(&sPick) == GROW_COUNT, "grow: Left/Right step through the four shapes");
    int pos = -1, step = -1;
    CHECK(Pick_Audition(&sPick, &sPickEd, &pos, &step) && pos == 0 && step == 0, "grow: auditions from the top");
    CHECK(Pick_Keep(&sPick, &sPickEd, true), "grow: Keep is an edit (Shift+Enter too)");
    CHECK(sPickEd.undoCount == 1 && sPickEd.dirty, "grow: one undo entry, song changed");
    CHECK(strcmp(sPickEd.status, "Grew into 7 patterns, about 1:04") == 0, "grow: Keep says how big and how long");
    CHECK(sPickEd.song.patternCount == 7 && sPickEd.song.orderCount == 8, "grow: Game loop is 7 patterns over 8 entries");
    Editor_Apply(&sPickEd, ED_UNDO, 0);
    CHECK(pick_SongSame() && sPickEd.pattern == 0 && sPickEd.orderPos == 0, "grow: one Ctrl+Z gives the loop back");

    /* A pattern in the middle of a song: its own arrangement, the others
     * kept but out of the order, Ctrl+Z back to where the picker opened. */
    pick_ThreePatterns(6, NULL);
    sPickEd.pattern = 1;
    sPickEd.orderPos = 1;
    pick_Snap();
    sPick.seed = 3u;
    Pick_Begin(&sPick, &sPickEd, PICK_GROW);
    bool kept = memcmp(&sPickEd.song.patterns[0], &sPickSnap.patterns[0], sizeof(SongPattern)) == 0 &&
                memcmp(&sPickEd.song.patterns[2], &sPickSnap.patterns[2], sizeof(SongPattern)) == 0 &&
                memcmp(&sPickEd.song.patterns[1], &sPickSnap.patterns[1], sizeof(SongPattern)) == 0;
    CHECK(kept, "grow: every existing pattern stays as it was, the source included");
    CHECK(sPickEd.song.order[0] == 1 && sPickEd.pattern == 1 && sPickEd.orderPos == 0, "grow: the arrangement opens on the source");
    Pick_Keep(&sPick, &sPickEd, false);
    CHECK(strstr(sPickEd.status, "2 others left out of the order") != NULL, "grow: Keep says when other patterns drop out of the order");
    Editor_Apply(&sPickEd, ED_UNDO, 0);
    CHECK(pick_SongSame() && sPickEd.pattern == 1 && sPickEd.orderPos == 1, "grow: Ctrl+Z lands where the picker opened");

    /* A keyless song grows in its guessed key. */
    pick_LoadTheme(8);
    sPickSong.keyRoot = -1;
    Editor_SetSong(&sPickEd, &sPickSong, "");
    pick_Snap();
    sPick.seed = 8u;
    Pick_Begin(&sPick, &sPickEd, PICK_GROW);
    ComposeKey guess;
    Compose_GuessKey(&sPickSnap, &guess);
    memcpy(&sPickWant, &sPickSnap, sizeof(Song));
    Arrange_Grow(&sPickWant, 0, guess, GROW_GAME_LOOP, sPick.seed);
    CHECK(memcmp(&sPickWant, &sPickEd.song, sizeof(Song)) == 0 && sPickEd.song.keyRoot == -1, "grow: a keyless song grows in its guessed key, which is not stored");
    Pick_Cancel(&sPick, &sPickEd);

    /* Nothing to follow: refused with a reason. */
    Song_Init(&sPickSong, "Drums only");
    sPickSong.patterns[0].drums[0] = DRUM_KICK;
    Editor_SetSong(&sPickEd, &sPickSong, "");
    pick_Snap();
    Editor_SetStatus(&sPickEd, "");
    CHECK(!Pick_Begin(&sPick, &sPickEd, PICK_GROW) && sPick.kind == PICK_NONE, "grow: refuses a pattern with no bass or arp to follow");
    CHECK(pick_SongSame() && strstr(sPickEd.status, "bass or arp") != NULL, "grow: and says what would help, changing nothing");

    /* Room: only the shapes that fit are offered, none at all refuses. */
    pick_ManyPatterns(2, SONG_MAX_PATTERNS - 1);
    pick_Snap();
    Editor_SetStatus(&sPickEd, "");
    CHECK(!Pick_Begin(&sPick, &sPickEd, PICK_GROW) && pick_SongSame() && sPickEd.status[0] != '\0', "grow: refuses when no shape fits");
    pick_ManyPatterns(2, SONG_MAX_PATTERNS - 4); /* room for Short (2) only */
    CHECK(Pick_Begin(&sPick, &sPickEd, PICK_GROW) && sPick.index == GROW_SHORT, "grow: opens on the shape that fits when Game loop does not");
    Pick_Move(&sPick, &sPickEd, 1);
    bool onlyShort = sPick.index == GROW_SHORT;
    Pick_Move(&sPick, &sPickEd, -3);
    onlyShort = onlyShort && sPick.index == GROW_SHORT;
    CHECK(onlyShort, "grow: Left/Right skip the shapes with no room");
    Pick_Cancel(&sPick, &sPickEd);
    pick_ManyPatterns(2, SONG_MAX_PATTERNS - 6); /* Short, Game loop, Long; not the 7 of the ending */
    Pick_Begin(&sPick, &sPickEd, PICK_GROW);
    int seen[GROW_COUNT] = {0};
    for (int i = 0; i < 8; i++) { seen[sPick.index]++; Pick_Move(&sPick, &sPickEd, 1); }
    CHECK(seen[GROW_SHORT] && seen[GROW_GAME_LOOP] && seen[GROW_LONG] && !seen[GROW_WITH_ENDING], "grow: every shape that fits is offered, and only those");
    Pick_Cancel(&sPick, &sPickEd);
}

/* The live replace lands exactly the song asked for, growing or shrinking,
 * without the song moving. */
static void pick_test_replace_song(void) {
    pick_LoadTheme(5);
    const Song *where = &sPickEd.song;
    memcpy(&sPickWant, &sPickEd.song, sizeof(Song));
    Arrange_Grow(&sPickWant, 0, pick_ThemeKey(5), GROW_WITH_ENDING, 12u);
    pick_Snap();
    CHECK(Pick_ReplaceSong(&sPickEd, &sPickWant) && memcmp(&sPickEd.song, &sPickWant, sizeof(Song)) == 0, "replace: a bigger song lands byte for byte");
    CHECK(Pick_ReplaceSong(&sPickEd, &sPickSnap) && pick_SongSame(), "replace: so does a smaller one, its old slots reset to the target's bytes");
    CHECK(&sPickEd.song == where, "replace: the live song never moves");
    CHECK(!Pick_ReplaceSong(&sPickEd, NULL) && !Pick_ReplaceSong(NULL, &sPickSnap), "replace: refuses NULL");
}

void test_pick_all(void) {
    Editor_Init(&sPickEd);
    memset(&sPick, 0, sizeof(sPick));
    pick_test_cancel_exact();
    pick_test_keep_one_undo();
    pick_test_wrap_count();
    pick_test_own_track();
    pick_test_chords_read_first();
    pick_test_in_key_and_bars();
    pick_test_fit_rhythm();
    pick_test_lead_register();
    pick_test_drums_only();
    pick_test_labels();
    pick_test_live_memcpy();
    pick_test_chords_picker();
    pick_test_open_over();
    pick_test_vary();
    pick_test_grow();
    pick_test_replace_song();
}
