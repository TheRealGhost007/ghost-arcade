/* Tests for ghost-common's song model, .gsong format, library and the synth
 * (song.h, musicsynth.h). Compiled by inclusion from test_main.c, which has
 * already pointed XDG_DATA_HOME and XDG_CONFIG_HOME at a scratch folder. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "song.h"
#include "musicsynth.h"

/* ---------------------------------------------------------------- helpers */

/* Song_Write only takes a FILE, so the round trip goes through a tmpfile. */
static size_t song_WriteToText(const Song *s, char *buf, size_t size) {
    FILE *f = tmpfile();
    if (!f) return 0;
    Song_Write(s, f);
    rewind(f);
    size_t n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

/* A song that uses every feature the format has: all five waves, per-track
 * volumes, swing, holds, drums, three distinct patterns and an order of five. */
static void song_BuildFull(Song *s) {
    Song_Init(s, "Every Feature");
    s->bpm = 150;
    s->swing = 0.10f;
    s->wave[TRACK_BASS] = WAVE_SAW;
    s->wave[TRACK_ARP] = WAVE_PULSE12;
    s->wave[TRACK_LEAD] = WAVE_TRI;
    s->volume[TRACK_BASS] = 90;
    s->volume[TRACK_ARP] = 70;
    s->volume[TRACK_LEAD] = 100;
    s->volume[TRACK_DRUMS] = 55;
    int idx;
    Song_AddPattern(s, &idx);
    Song_AddPattern(s, &idx);
    for (int p = 0; p < 3; p++) {
        SongPattern *pat = &s->patterns[p];
        for (int st = 0; st < SONG_STEPS; st++) {
            if (st % 4 == 0) pat->tone[TRACK_BASS][st] = (int8_t)(NOTE_MIN + p * 5 + (st / 4) % 12);
            else if (st % 4 == 1) pat->tone[TRACK_BASS][st] = NOTE_HOLD;
            if (st % 2 == 0) pat->tone[TRACK_ARP][st] = (int8_t)(60 + (st + p) % 12);
            if (st % 8 == 0) pat->tone[TRACK_LEAD][st] = (int8_t)(NOTE_MAX - p * 7 - st / 8);
            if (st % 8 == 2) pat->tone[TRACK_LEAD][st] = NOTE_HOLD;
            uint8_t d = 0;
            if (st % 4 == 0) d |= DRUM_KICK;
            if (st % 8 == 4) d |= DRUM_SNARE;
            if (st % 2 == 1) d |= DRUM_HAT;
            if (st == 63) d = DRUM_KICK | DRUM_SNARE | DRUM_HAT;
            pat->drums[st] = d;
        }
    }
    s->orderCount = 5;
    s->order[0] = 0; s->order[1] = 1; s->order[2] = 2; s->order[3] = 1; s->order[4] = 0;
}

static bool song_PatternEmpty(const SongPattern *p) {
    for (int t = 0; t < SONG_TONE_TRACKS; t++)
        for (int st = 0; st < SONG_STEPS; st++) if (p->tone[t][st] != NOTE_NONE) return false;
    for (int st = 0; st < SONG_STEPS; st++) if (p->drums[st] != 0) return false;
    return true;
}

static bool song_OrderConsistent(const Song *s) {
    if (s->patternCount < 1 || s->patternCount > SONG_MAX_PATTERNS) return false;
    if (s->orderCount < 1 || s->orderCount > SONG_MAX_ORDER) return false;
    for (int i = 0; i < s->orderCount; i++) if (s->order[i] >= s->patternCount) return false;
    return true;
}

static bool song_FileExists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static uint32_t song_LE32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t song_LE16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Renders `frames` and reports the peak, how many frames sat near the clip
 * ceiling and whether anything moved at all. */
static void song_Measure(Synth *sy, int frames, int *peakOut, int *nearClipOut, double *rmsOut) {
    int16_t buf[1024];
    int peak = 0, nearClip = 0;
    double sq = 0.0;
    int done = 0;
    while (done < frames) {
        int chunk = frames - done > 1024 ? 1024 : frames - done;
        Synth_Render(sy, buf, chunk);
        for (int i = 0; i < chunk; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > peak) peak = v;
            if (v >= 31000) nearClip++;
            sq += (double)buf[i] * (double)buf[i];
        }
        done += chunk;
    }
    *peakOut = peak;
    *nearClipOut = nearClip;
    *rmsOut = frames > 0 ? sqrt(sq / (double)frames) : 0.0;
}

/* ------------------------------------------------------------------ tests */

static void song_test_init(void) {
    Song s;
    Song_Init(&s, "Fresh");
    CHECK(strcmp(s.name, "Fresh") == 0, "init: name kept");
    CHECK(s.bpm == 120 && s.swing == 0.0f, "init: 120 bpm, no swing");
    CHECK(s.wave[TRACK_BASS] == WAVE_TRI && s.wave[TRACK_ARP] == WAVE_PULSE25 && s.wave[TRACK_LEAD] == WAVE_PULSE50, "init: the classic voices");
    bool vol = true;
    for (int i = 0; i < SONG_TRACKS; i++) if (s.volume[i] != 100) vol = false;
    CHECK(vol, "init: every track at full volume");
    CHECK(s.patternCount == 1 && s.orderCount == 1 && s.order[0] == 0, "init: one empty pattern played once");
    CHECK(song_PatternEmpty(&s.patterns[0]), "init: the pattern is silent");
    Song_Init(&s, NULL);
    CHECK(strcmp(s.name, "Untitled") == 0, "init: NULL name becomes Untitled");
    Song_Init(&s, "");
    CHECK(strcmp(s.name, "Untitled") == 0, "init: empty name becomes Untitled");
    char longName[200];
    memset(longName, 'x', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';
    Song_Init(&s, longName);
    CHECK(strlen(s.name) == SONG_NAME_LEN - 1, "init: a long name is cut to fit");

    CHECK(fabsf(Song_StepSeconds(&s) - 0.125f) < 1e-6f, "timing: a sixteenth at 120 bpm is 125 ms");
    CHECK(fabsf(Song_Seconds(&s) - 8.0f) < 1e-5f, "timing: one 64-step pattern at 120 bpm is 8 s");
    s.bpm = 60;
    s.orderCount = 2;
    CHECK(fabsf(Song_Seconds(&s) - 32.0f) < 1e-4f, "timing: seconds scale with bpm and order length");

    bool waves = true;
    for (int w = 0; w < WAVE_COUNT; w++) {
        const char *n = Song_WaveName((SongWave)w);
        if (!n || !n[0] || strcmp(n, "?") == 0) waves = false;
        for (int v = 0; v < w; v++) if (strcmp(Song_WaveName((SongWave)v), n) == 0) waves = false;
    }
    CHECK(waves, "waves: every wave has its own name");
    CHECK(strcmp(Song_WaveName((SongWave)WAVE_COUNT), "?") == 0 && strcmp(Song_WaveName((SongWave)-1), "?") == 0, "waves: out of range is a question mark");
}

static void song_test_round_trip(void) {
    Song a, b;
    song_BuildFull(&a);
    static char text[65536];
    size_t n = song_WriteToText(&a, text, sizeof(text));
    CHECK(n > 0 && strncmp(text, "GSONG 1\n", 8) == 0, "write: starts with the GSONG header");
    CHECK(strstr(text, "name Every Feature\n") != NULL, "write: name line");
    CHECK(strstr(text, "wave saw pulse12 tri\n") != NULL, "write: wave line");
    CHECK(strstr(text, "volume 90 70 100 55\n") != NULL, "write: volume line");
    CHECK(strstr(text, "order 0 1 2 1 0\n") != NULL, "write: order line");
    CHECK(strstr(text, "pattern 2\n") != NULL, "write: every pattern is written");
    CHECK(strstr(text, "63 ") != NULL && strstr(text, " KSH\n") != NULL, "write: the last step with all three drums");
    CHECK(strstr(text, "===") != NULL, "write: holds appear as ===");

    memset(&b, 0x5A, sizeof(b));
    CHECK(Song_Parse(&b, text, n), "parse: the written text is a song");
    CHECK(memcmp(&a, &b, sizeof(Song)) == 0, "round trip: parsed song equals the original byte for byte");
    CHECK(b.patternCount == 3 && b.orderCount == 5 && b.swing == 0.10f && b.bpm == 150, "round trip: counts, swing and bpm");

    /* Writing the parsed song again gives the same text: the format is stable. */
    static char text2[65536];
    size_t n2 = song_WriteToText(&b, text2, sizeof(text2));
    CHECK(n2 == n && memcmp(text, text2, n) == 0, "round trip: write is stable across a parse");

    /* Windows line endings and tabs are tolerated. */
    static char crlf[131072];
    size_t m = 0;
    for (size_t i = 0; i < n && m < sizeof(crlf) - 3; i++) {
        if (text[i] == '\n') { crlf[m++] = '\r'; crlf[m++] = '\n'; }
        else if (text[i] == ' ') crlf[m++] = '\t';
        else crlf[m++] = text[i];
    }
    CHECK(Song_Parse(&b, crlf, m) && memcmp(&a, &b, sizeof(Song)) == 0, "parse: CRLF and tabs read the same");
}

static void song_test_garbage(void) {
    Song s;
    CHECK(!Song_Parse(&s, "", 0), "parse: empty text is not a song");
    CHECK(!Song_Parse(&s, NULL, 0), "parse: NULL text is not a song");
    const char *junk = "hello world\n\x01\x02\x03\xff\n123 456 789\norder 1 2 3\npattern\n";
    CHECK(!Song_Parse(&s, junk, strlen(junk)), "parse: garbage is not a song");
    const char *headerOnly = "GSONG 1\nname Lonely\nbpm 100\n";
    CHECK(!Song_Parse(&s, headerOnly, strlen(headerOnly)), "parse: a header with no pattern is not a song");
    const char *noHeader = "name Nope\npattern 0\n00 C-4 --- --- K..\n";
    CHECK(!Song_Parse(&s, noHeader, strlen(noHeader)), "parse: patterns without the GSONG header are not a song");
    const char *stub = "GSON";
    CHECK(!Song_Parse(&s, stub, strlen(stub)), "parse: a cut-off header is not a song");
    const char *noNewline = "GSONG 1\npattern 0";
    CHECK(Song_Parse(&s, noNewline, strlen(noNewline)) && s.patternCount == 1, "parse: a final line without a newline still counts");

    /* Every prefix of a valid song: never a crash, never an inconsistent
     * song, and false until the first pattern line is complete. */
    Song full;
    song_BuildFull(&full);
    static char text[65536];
    size_t n = song_WriteToText(&full, text, sizeof(text));
    const char *firstPattern = strstr(text, "pattern 0\n");
    size_t patternEnd = (size_t)(firstPattern - text) + strlen("pattern 0");
    bool falseBefore = true, consistent = true;
    for (size_t len = 0; len <= n; len += (len < 400 ? 1 : 97)) {
        bool ok = Song_Parse(&s, text, len);
        if (len < patternEnd && ok) falseBefore = false;
        if (ok && !song_OrderConsistent(&s)) consistent = false;
    }
    CHECK(falseBefore, "parse: truncated before the first pattern is not a song");
    CHECK(consistent, "parse: every truncation that parses is a consistent song");
    CHECK(Song_Parse(&s, text, n / 2) && s.patternCount >= 1 && song_OrderConsistent(&s), "parse: half a song is still a usable song");

    /* Unknown lines are skipped, the rest kept. */
    const char *odd = "GSONG 1\nflavour mint\nname Odd\nbpm 90\n# a comment\npattern 0\n00 C-4 E-4 G-4 K.H\nwhatever\n05 D-4 --- === .S.\n";
    CHECK(Song_Parse(&s, odd, strlen(odd)), "parse: unknown lines are skipped");
    CHECK(strcmp(s.name, "Odd") == 0 && s.bpm == 90, "parse: known lines around them are kept");
    CHECK(s.patterns[0].tone[TRACK_BASS][0] == 60 && s.patterns[0].tone[TRACK_ARP][0] == 64 && s.patterns[0].tone[TRACK_LEAD][0] == 67, "parse: notes land on their tracks");
    CHECK(s.patterns[0].drums[0] == (DRUM_KICK | DRUM_HAT) && s.patterns[0].drums[5] == DRUM_SNARE, "parse: drum letters");
    CHECK(s.patterns[0].tone[TRACK_ARP][5] == NOTE_NONE && s.patterns[0].tone[TRACK_LEAD][5] == NOTE_HOLD, "parse: --- and === cells");
    CHECK(s.patterns[0].tone[TRACK_BASS][1] == NOTE_NONE, "parse: steps not listed stay empty");
}

static void song_test_clamping(void) {
    Song s;
    const char *hi = "GSONG 1\nbpm 999\nswing 0.9\nvolume 150 -5 100 1000\npattern 0\n00 C-0 C-9 G-9 KSH\n01 c#1 db8 a-0 ...\n";
    CHECK(Song_Parse(&s, hi, strlen(hi)), "clamp: the text parses");
    CHECK(s.bpm == SONG_BPM_MAX, "clamp: bpm above the maximum");
    CHECK(fabsf(s.swing - 0.3f) < 1e-6f, "clamp: swing above 0.3");
    CHECK(s.volume[0] == 100 && s.volume[1] == 0 && s.volume[2] == 100 && s.volume[3] == 100, "clamp: volumes to 0..100");
    CHECK(s.patterns[0].tone[TRACK_BASS][0] == NOTE_MIN, "clamp: a note below C1 rises to NOTE_MIN");
    CHECK(s.patterns[0].tone[TRACK_ARP][0] == NOTE_MAX && s.patterns[0].tone[TRACK_LEAD][0] == NOTE_MAX, "clamp: notes above C8 fall to NOTE_MAX");
    CHECK(s.patterns[0].tone[TRACK_BASS][1] == 25 && s.patterns[0].tone[TRACK_ARP][1] == 109 - 1, "clamp: in-range notes are untouched (C#1, Db8)");
    CHECK(s.patterns[0].tone[TRACK_LEAD][1] == NOTE_MIN, "clamp: A0 rises to NOTE_MIN");
    const char *lo = "GSONG 1\nbpm 1\nswing -2\nvolume -1 -1 -1 -1\npattern 0\n";
    CHECK(Song_Parse(&s, lo, strlen(lo)) && s.bpm == SONG_BPM_MIN && s.swing == 0.0f && s.volume[3] == 0, "clamp: bpm, swing and volume below their minimums");
    const char *badWave = "GSONG 1\nwave banana saw nothing\npattern 0\n";
    CHECK(Song_Parse(&s, badWave, strlen(badWave)) && s.wave[0] == WAVE_TRI && s.wave[1] == WAVE_SAW && s.wave[2] == WAVE_PULSE50, "clamp: an unknown wave keeps the default, a known one is taken");
    const char *badStep = "GSONG 1\npattern 0\n64 C-4 C-4 C-4 K..\n-1 C-4 C-4 C-4 K..\n99 C-4 C-4 C-4 K..\n";
    CHECK(Song_Parse(&s, badStep, strlen(badStep)) && song_PatternEmpty(&s.patterns[0]), "clamp: rows with an out-of-range step are ignored");
    const char *badPattern = "GSONG 1\npattern 64\n00 C-4 --- --- ...\npattern -3\npattern 1\n";
    CHECK(Song_Parse(&s, badPattern, strlen(badPattern)) && s.patternCount == 2, "clamp: out-of-range pattern numbers are ignored, gaps are filled");
    CHECK(song_PatternEmpty(&s.patterns[0]) && song_PatternEmpty(&s.patterns[1]), "clamp: a filled-in gap pattern is empty");
}

static void song_test_order_fixup(void) {
    Song s;
    const char *t = "GSONG 1\norder 0 5 1 7 1 63 0\npattern 0\npattern 1\n";
    CHECK(Song_Parse(&s, t, strlen(t)), "order: the text parses");
    CHECK(s.patternCount == 2 && s.orderCount == 4, "order: entries naming missing patterns are dropped");
    CHECK(s.order[0] == 0 && s.order[1] == 1 && s.order[2] == 1 && s.order[3] == 0, "order: the survivors keep their sequence");
    const char *t2 = "GSONG 1\norder 9 8 7\npattern 0\n";
    CHECK(Song_Parse(&s, t2, strlen(t2)) && s.orderCount == 1 && s.order[0] == 0, "order: an order with nothing valid becomes pattern 0 once");
    const char *t3 = "GSONG 1\npattern 0\n";
    CHECK(Song_Parse(&s, t3, strlen(t3)) && s.orderCount == 1 && s.order[0] == 0, "order: no order line means play pattern 0");
    /* An order longer than the limit is cut, never overrun. */
    static char longOrder[4096];
    int len = snprintf(longOrder, sizeof(longOrder), "GSONG 1\norder");
    for (int i = 0; i < SONG_MAX_ORDER + 40; i++) len += snprintf(longOrder + len, sizeof(longOrder) - (size_t)len, " 0");
    len += snprintf(longOrder + len, sizeof(longOrder) - (size_t)len, "\npattern 0\n");
    CHECK(Song_Parse(&s, longOrder, (size_t)len) && s.orderCount == SONG_MAX_ORDER, "order: a too-long order is cut at the limit");
}

static void song_test_note_names(void) {
    /* MIDI 12..127 round-trip exactly; 0..11 (octave -1) have no spelling in
     * three characters, so their name reads back an octave up. Songs never
     * hold them (NOTE_MIN is 24). */
    bool agree = true, lowPitchClass = true;
    for (int n = 0; n <= 127; n++) {
        char name[4];
        Song_NoteName(n, name);
        int back = Song_NoteFromName(name);
        if (strlen(name) != 3) agree = false;
        if (n >= 12 && back != n) agree = false;
        if (n < 12 && (back < 0 || back % 12 != n % 12)) lowPitchClass = false;
    }
    CHECK(agree, "notes: name and parse agree for MIDI 12..127");
    CHECK(lowPitchClass, "notes: MIDI 0..11 keep their pitch class");
    char name[4];
    Song_NoteName(60, name);
    CHECK(strcmp(name, "C-4") == 0, "notes: 60 is C-4");
    Song_NoteName(61, name);
    CHECK(strcmp(name, "C#4") == 0, "notes: 61 is C#4");
    Song_NoteName(NOTE_NONE, name);
    CHECK(strcmp(name, "---") == 0, "notes: none is ---");
    Song_NoteName(NOTE_HOLD, name);
    CHECK(strcmp(name, "===") == 0, "notes: hold is ===");
    Song_NoteName(200, name);
    CHECK(strcmp(name, "---") == 0, "notes: out of range reads as none");

    CHECK(Song_NoteFromName("Db4") == 61 && Song_NoteFromName("C#4") == 61, "notes: flats and sharps meet");
    CHECK(Song_NoteFromName("Eb3") == 51 && Song_NoteFromName("Bb2") == 46 && Song_NoteFromName("Cb4") == 59, "notes: more flats");
    CHECK(Song_NoteFromName("c#4") == 61 && Song_NoteFromName("a-4") == 69 && Song_NoteFromName("A4") == 69, "notes: lower case and no dash accepted");
    CHECK(Song_NoteFromName("---") == NOTE_NONE && Song_NoteFromName("===") == NOTE_HOLD, "notes: the two markers parse");
    CHECK(Song_NoteFromName("") == NOTE_NONE && Song_NoteFromName(NULL) == NOTE_NONE, "notes: empty and NULL are none");
    CHECK(Song_NoteFromName("H4") == NOTE_NONE && Song_NoteFromName("C") == NOTE_NONE && Song_NoteFromName("C44") == NOTE_NONE && Song_NoteFromName("C-x") == NOTE_NONE, "notes: malformed names are none");
    CHECK(Song_NoteFromName("C-0") == 12 && Song_NoteFromName("G-9") == 127, "notes: the ends of the range");
    CHECK(Song_NoteFromName("A-9") == NOTE_NONE, "notes: above 127 is none");
}

static void song_test_pattern_edits(void) {
    Song s;
    Song_Init(&s, "Edits");
    s.patterns[0].tone[TRACK_LEAD][3] = 72;
    int idx = -1;
    CHECK(Song_AddPattern(&s, &idx) && idx == 1 && s.patternCount == 2, "add: appended at index 1");
    CHECK(song_PatternEmpty(&s.patterns[1]), "add: the new pattern is empty");
    CHECK(Song_AddPattern(&s, NULL) && s.patternCount == 3, "add: NULL index is fine");
    while (s.patternCount < SONG_MAX_PATTERNS) CHECK(Song_AddPattern(&s, NULL), "add: up to the limit");
    idx = -1;
    CHECK(!Song_AddPattern(&s, &idx) && idx == -1 && s.patternCount == SONG_MAX_PATTERNS, "add: refused when full");
    CHECK(!Song_DuplicatePattern(&s, 0, &idx) && s.patternCount == SONG_MAX_PATTERNS, "dup: refused when full");

    Song_Init(&s, "Edits");
    s.patterns[0].tone[TRACK_LEAD][3] = 72;
    s.patterns[0].drums[0] = DRUM_KICK;
    CHECK(Song_DuplicatePattern(&s, 0, &idx) && idx == 1 && s.patternCount == 2, "dup: appended at index 1");
    CHECK(memcmp(&s.patterns[0], &s.patterns[1], sizeof(SongPattern)) == 0, "dup: a true copy");
    CHECK(!Song_DuplicatePattern(&s, 2, &idx) && !Song_DuplicatePattern(&s, -1, &idx) && s.patternCount == 2, "dup: out of range refused");

    /* Three distinct patterns, order 0 1 2 1 2; removing 1 shifts 2 down and
     * sends the orphans to the previous pattern. */
    Song_Init(&s, "Remove");
    Song_AddPattern(&s, NULL);
    Song_AddPattern(&s, NULL);
    for (int p = 0; p < 3; p++) s.patterns[p].tone[TRACK_BASS][0] = (int8_t)(40 + p);
    s.orderCount = 5;
    s.order[0] = 0; s.order[1] = 1; s.order[2] = 2; s.order[3] = 1; s.order[4] = 2;
    CHECK(Song_RemovePattern(&s, 1) && s.patternCount == 2, "remove: middle pattern gone");
    CHECK(s.patterns[0].tone[TRACK_BASS][0] == 40 && s.patterns[1].tone[TRACK_BASS][0] == 42, "remove: later patterns shift down");
    CHECK(s.orderCount == 5 && s.order[0] == 0 && s.order[1] == 0 && s.order[2] == 1 && s.order[3] == 0 && s.order[4] == 1, "remove: order fixed up (orphans to the previous, later ones renumbered)");
    CHECK(song_OrderConsistent(&s), "remove: order consistent");
    CHECK(Song_RemovePattern(&s, 0) && s.patternCount == 1 && s.patterns[0].tone[TRACK_BASS][0] == 42, "remove: the first pattern");
    bool allZero = true;
    for (int i = 0; i < s.orderCount; i++) if (s.order[i] != 0) allZero = false;
    CHECK(allZero && s.orderCount == 5, "remove: with one pattern left every order entry is 0");
    CHECK(!Song_RemovePattern(&s, 0) && s.patternCount == 1, "remove: refuses the last pattern");
    CHECK(!Song_RemovePattern(&s, 5) && !Song_RemovePattern(&s, -1), "remove: out of range refused");
}

static void song_test_order_edits(void) {
    Song s;
    Song_Init(&s, "Order");
    Song_AddPattern(&s, NULL);
    Song_AddPattern(&s, NULL);
    CHECK(Song_OrderInsert(&s, 1, 2) && s.orderCount == 2 && s.order[0] == 0 && s.order[1] == 2, "insert: after the first");
    CHECK(Song_OrderInsert(&s, 0, 1) && s.orderCount == 3 && s.order[0] == 1 && s.order[1] == 0 && s.order[2] == 2, "insert: at the front shifts the rest");
    CHECK(Song_OrderInsert(&s, 99, 1) && s.orderCount == 4 && s.order[3] == 1, "insert: a position past the end appends");
    CHECK(Song_OrderInsert(&s, -5, 2) && s.orderCount == 5 && s.order[0] == 2, "insert: a negative position goes first");
    CHECK(!Song_OrderInsert(&s, 1, 3) && !Song_OrderInsert(&s, 1, -1) && s.orderCount == 5, "insert: a missing pattern is refused");
    while (s.orderCount < SONG_MAX_ORDER) CHECK(Song_OrderInsert(&s, s.orderCount, 0), "insert: up to the limit");
    CHECK(!Song_OrderInsert(&s, 0, 0) && s.orderCount == SONG_MAX_ORDER, "insert: refused when full");
    CHECK(song_OrderConsistent(&s), "insert: order consistent at the limit");

    Song_Init(&s, "Order");
    Song_AddPattern(&s, NULL);
    Song_OrderInsert(&s, 1, 1);
    Song_OrderInsert(&s, 2, 0);
    CHECK(Song_OrderRemove(&s, 1) && s.orderCount == 2 && s.order[0] == 0 && s.order[1] == 0, "remove: the middle entry");
    CHECK(!Song_OrderRemove(&s, 2) && !Song_OrderRemove(&s, -1) && s.orderCount == 2, "remove: out of range refused");
    CHECK(Song_OrderRemove(&s, 0) && s.orderCount == 1, "remove: down to one");
    CHECK(!Song_OrderRemove(&s, 0) && s.orderCount == 1, "remove: refuses the last entry");

    CHECK(Song_OrderSet(&s, 0, 1) && s.order[0] == 1, "set: changes the pattern played");
    CHECK(!Song_OrderSet(&s, 0, 2) && !Song_OrderSet(&s, 0, -1) && s.order[0] == 1, "set: a missing pattern is refused");
    CHECK(!Song_OrderSet(&s, 1, 0) && !Song_OrderSet(&s, -1, 0), "set: out of range position refused");
}

static void song_test_file_names(void) {
    char out[SONG_FILE_LEN];
    Song_FileNameFor("My Tune!", out, sizeof(out));
    CHECK(strcmp(out, "my-tune.gsong") == 0, "filename: lower case, dashes, .gsong");
    Song_FileNameFor("  ..//Weird  Name__2 ", out, sizeof(out));
    CHECK(strcmp(out, "weird-name-2.gsong") == 0, "filename: separators collapse, edges trimmed");
    Song_FileNameFor("", out, sizeof(out));
    CHECK(strcmp(out, "untitled.gsong") == 0, "filename: empty is untitled");
    Song_FileNameFor("!!!", out, sizeof(out));
    CHECK(strcmp(out, "untitled.gsong") == 0, "filename: nothing usable is untitled");
    Song_FileNameFor("Skyraid", out, sizeof(out));
    CHECK(strcmp(out, "skyraid.gsong") == 0, "filename: a plain word");
    Song_FileNameFor("../../etc/passwd", out, sizeof(out));
    CHECK(strcmp(out, "etc-passwd.gsong") == 0, "filename: no path parts survive");
    char longTitle[300];
    memset(longTitle, 'a', sizeof(longTitle) - 1);
    longTitle[sizeof(longTitle) - 1] = '\0';
    Song_FileNameFor(longTitle, out, sizeof(out));
    size_t n = strlen(out);
    CHECK(n < SONG_FILE_LEN && n >= 6 && strcmp(out + n - 6, ".gsong") == 0, "filename: a long title is cut and still ends in .gsong");
    char small[12];
    Song_FileNameFor("abcdefghijklmnop", small, sizeof(small));
    n = strlen(small);
    CHECK(n < sizeof(small) && strcmp(small + n - 6, ".gsong") == 0, "filename: respects a small buffer");
}

static void song_test_library(void) {
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *xdgConfig = getenv("XDG_CONFIG_HOME");
    CHECK(xdgData && xdgData[0] && xdgConfig && xdgConfig[0], "library: the harness sandboxed XDG_DATA_HOME and XDG_CONFIG_HOME");
    if (!xdgData || !xdgData[0] || !xdgConfig || !xdgConfig[0]) return;

    char names[16][SONG_FILE_LEN];
    CHECK(Song_LibraryList(names, 16) == 0, "library: empty before anything is saved");

    char dir[700];
    CHECK(Song_LibraryDir(dir, sizeof(dir), true), "library: dir created");
    CHECK(strncmp(dir, xdgData, strlen(xdgData)) == 0 && strstr(dir, "/ghost-arcade/music") != NULL, "library: lives under XDG_DATA_HOME/ghost-arcade/music");
    CHECK(song_FileExists(dir), "library: the folder exists on disk");

    char path[800], path2[800];
    CHECK(Song_LibraryPath("my tune", path, sizeof(path), false), "library: path for a bare name");
    size_t dlen = strlen(dir);
    CHECK(strncmp(path, dir, dlen) == 0 && strcmp(path + dlen, "/my tune.gsong") == 0, "library: .gsong appended, folder prefixed");
    CHECK(Song_LibraryPath("other.gsong", path2, sizeof(path2), false) && strcmp(path2 + dlen, "/other.gsong") == 0, "library: an existing .gsong suffix is kept");
    CHECK(Song_LibraryPath("../evil", path2, sizeof(path2), false) && strcmp(path2 + dlen, "/evil.gsong") == 0, "library: slashes and leading dots are stripped");
    CHECK(Song_LibraryPath("a/b\\c", path2, sizeof(path2), false) && strcmp(path2 + dlen, "/abc.gsong") == 0, "library: both slash kinds stripped");
    CHECK(Song_LibraryPath("", path2, sizeof(path2), false) && strcmp(path2 + dlen, "/untitled.gsong") == 0, "library: an empty name is untitled");
    CHECK(!Song_LibraryPath(NULL, path2, sizeof(path2), false), "library: NULL name refused");

    Song a, b;
    song_BuildFull(&a);
    CHECK(Song_Save(&a, path), "library: save");
    CHECK(song_FileExists(path), "library: the file exists");
    char tmpPath[820];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    CHECK(!song_FileExists(tmpPath), "library: no temp file left behind");
    CHECK(Song_Load(&b, path) && memcmp(&a, &b, sizeof(Song)) == 0, "library: load gives back the saved song");
    CHECK(!Song_Load(&b, "/nonexistent/nowhere.gsong"), "library: loading a missing file fails");

    Song c;
    Song_Init(&c, "Another");
    char cPath[800];
    CHECK(Song_LibraryPath("another", cPath, sizeof(cPath), false) && Song_Save(&c, cPath), "library: a second song saved");
    /* A stray non-song and a hidden file must not show up in the listing. */
    char stray[820];
    snprintf(stray, sizeof(stray), "%s/notes.txt", dir);
    FILE *f = fopen(stray, "w");
    if (f) { fputs("not a song\n", f); fclose(f); }
    snprintf(stray, sizeof(stray), "%s/.hidden.gsong", dir);
    f = fopen(stray, "w");
    if (f) { fputs("GSONG 1\npattern 0\n", f); fclose(f); }

    int n = Song_LibraryList(names, 16);
    CHECK(n == 2, "library: two songs listed");
    CHECK(n == 2 && strcmp(names[0], "another.gsong") == 0 && strcmp(names[1], "my tune.gsong") == 0, "library: sorted by name, only .gsong files");
    CHECK(Song_LibraryList(names, 1) == 1, "library: the list respects maxNames");

    /* Assignments. */
    char got[700];
    CHECK(!Song_AssignedName("skyraid", got, sizeof(got)) && got[0] == '\0', "assign: nothing assigned at first");
    CHECK(!Song_Assigned("skyraid", got, sizeof(got)), "assign: no path either");
    CHECK(Song_Assign("skyraid", "my tune.gsong"), "assign: set");
    CHECK(Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "my tune.gsong") == 0, "assign: read back the file name");
    CHECK(Song_Assigned("skyraid", got, sizeof(got)) && strcmp(got, path) == 0, "assign: the full path of an existing file");
    char ini[800];
    snprintf(ini, sizeof(ini), "%s/ghost-arcade/music.ini", xdgConfig);
    CHECK(song_FileExists(ini), "assign: music.ini under XDG_CONFIG_HOME/ghost-arcade");

    CHECK(Song_Assign("blockfall", "another"), "assign: a second slug, suffix added for us");
    CHECK(Song_AssignedName("blockfall", got, sizeof(got)) && strcmp(got, "another.gsong") == 0, "assign: second slug reads back with .gsong");
    CHECK(Song_Assign("skyraid", "another.gsong") && Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "another.gsong") == 0, "assign: reassigning replaces");
    /* Only one line per slug after a replace. */
    int skyraidLines = 0, totalLines = 0;
    f = fopen(ini, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            totalLines++;
            if (strncmp(line, "skyraid=", 8) == 0) skyraidLines++;
        }
        fclose(f);
    }
    CHECK(skyraidLines == 1 && totalLines == 3, "assign: one line per slug plus the comment header");

    CHECK(Song_Assign("skyraid", "ghost.gsong"), "assign: a file that does not exist can be named");
    CHECK(Song_AssignedName("skyraid", got, sizeof(got)) && !Song_Assigned("skyraid", got, sizeof(got)), "assign: but Song_Assigned says it is not there");

    CHECK(Song_Assign("skyraid", NULL), "assign: clear with NULL");
    CHECK(!Song_AssignedName("skyraid", got, sizeof(got)), "assign: cleared");
    CHECK(Song_AssignedName("blockfall", got, sizeof(got)) && strcmp(got, "another.gsong") == 0, "assign: other slugs survive a clear");
    CHECK(Song_Assign("blockfall", "") && !Song_AssignedName("blockfall", got, sizeof(got)), "assign: clear with an empty name");
    CHECK(!Song_Assign("a=b", "x.gsong") && !Song_Assign("", "x.gsong") && !Song_Assign(NULL, "x.gsong"), "assign: bad slugs refused");
    CHECK(Song_Assign("coilrush", "../../escape") && Song_AssignedName("coilrush", got, sizeof(got)) && strcmp(got, "escape.gsong") == 0, "assign: the file name is sanitised on the way in");

    /* A hand-edited ini with a path in it is ignored rather than followed. */
    f = fopen(ini, "w");
    if (f) { fputs("# test\nskyraid=../outside.gsong\nblockfall=\n", f); fclose(f); }
    CHECK(!Song_AssignedName("skyraid", got, sizeof(got)), "assign: a value with a slash is not trusted");
    CHECK(!Song_AssignedName("blockfall", got, sizeof(got)), "assign: an empty value is nothing");
    CHECK(Song_Assign("skyraid", "my tune.gsong") && Song_AssignedName("skyraid", got, sizeof(got)), "assign: writing after a hand edit works");
}

static void song_test_themes(void) {
    CHECK(Music_ThemeCount() == 8, "themes: seven games and the launcher");
    CHECK(strcmp(Music_ThemeAt(0)->slug, "ghostarcade") == 0, "themes: the launcher comes first");
    CHECK(Music_ThemeAt(Music_ThemeCount()) == Music_ThemeAt(0) && Music_ThemeAt(-1) == Music_ThemeAt(Music_ThemeCount() - 1), "themes: ThemeAt wraps");
    CHECK(strcmp(Music_FindTheme("skyraid")->slug, "skyraid") == 0, "themes: find by slug");
    CHECK(Music_FindTheme("nope") == Music_ThemeAt(0) && Music_FindTheme(NULL) == Music_ThemeAt(0), "themes: unknown falls back to the launcher theme");

    bool deterministic = true, filled = true, named = true, inRange = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song a, b;
        memset(&a, 0x11, sizeof(a));
        memset(&b, 0x22, sizeof(b));
        Music_ThemeToSong(t, &a);
        Music_ThemeToSong(t, &b);
        if (memcmp(&a, &b, sizeof(Song)) != 0) deterministic = false;
        if (strcmp(a.name, t->slug) != 0 || a.bpm != t->bpm || a.patternCount != 1 || a.orderCount != 1) named = false;
        int bass = 0, arp = 0, lead = 0, drums = 0;
        for (int st = 0; st < SONG_STEPS; st++) {
            int nb = a.patterns[0].tone[TRACK_BASS][st], na = a.patterns[0].tone[TRACK_ARP][st], nl = a.patterns[0].tone[TRACK_LEAD][st];
            if (nb >= 0) bass++;
            if (na >= 0) arp++;
            if (nl >= 0) lead++;
            if (a.patterns[0].drums[st]) drums++;
            if ((nb >= 0 && (nb < NOTE_MIN || nb > NOTE_MAX)) || (na >= 0 && (na < NOTE_MIN || na > NOTE_MAX)) || (nl >= 0 && (nl < NOTE_MIN || nl > NOTE_MAX))) inRange = false;
        }
        if (bass < 4 || arp < 8 || lead < 4 || drums < 4) filled = false;
    }
    CHECK(deterministic, "themes: ThemeToSong is deterministic");
    CHECK(named, "themes: one pattern named after the slug at the theme's tempo");
    CHECK(filled, "themes: every track has notes in every theme");
    CHECK(inRange, "themes: every note is within NOTE_MIN..NOTE_MAX");

    /* No two themes compose the same pattern. */
    bool distinct = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        for (int j = i + 1; j < Music_ThemeCount(); j++) {
            Song a, b;
            Music_ThemeToSong(Music_ThemeAt(i), &a);
            Music_ThemeToSong(Music_ThemeAt(j), &b);
            if (memcmp(&a.patterns[0], &b.patterns[0], sizeof(SongPattern)) == 0) distinct = false;
        }
    }
    CHECK(distinct, "themes: every theme sounds different");

    CHECK(fabsf(Synth_NoteHz(69) - 440.0f) < 0.01f && fabsf(Synth_NoteHz(81) - 880.0f) < 0.01f, "pitch: A4 is 440 Hz, A5 880");
    CHECK(fabsf(Synth_LoopSeconds(Music_FindTheme("blockfall")) - 64.0f * 60.0f / (140.0f * 4.0f)) < 1e-4f, "pitch: loop seconds follow the theme's tempo");
}

static void song_test_render_themes(void) {
    const int rate = 22050;
    bool loud = true, clean = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        Song song;
        Music_ThemeToSong(Music_ThemeAt(i), &song);
        Synth sy;
        Synth_Init(&sy, &song, rate);
        Synth_SetIntensity(&sy, 1.0f);
        int peak, nearClip;
        double rms;
        song_Measure(&sy, rate * 2, &peak, &nearClip, &rms);
        if (peak < 2000 || rms < 300.0) loud = false;
        if (nearClip > rate * 2 / 100) clean = false;
    }
    CHECK(loud, "render: two seconds of every theme is not silent");
    CHECK(clean, "render: no theme sits at the clip ceiling for more than 1% of frames");

    /* Every wave renders without clipping or silence on its own. */
    bool wavesOk = true;
    for (int w = 0; w < WAVE_COUNT; w++) {
        Song song;
        Music_ThemeToSong(Music_ThemeAt(1), &song);
        for (int t = 0; t < SONG_TONE_TRACKS; t++) song.wave[t] = (SongWave)w;
        Synth sy;
        Synth_Init(&sy, &song, rate);
        Synth_SetIntensity(&sy, 1.0f);
        int peak, nearClip;
        double rms;
        song_Measure(&sy, rate, &peak, &nearClip, &rms);
        if (peak < 2000 || nearClip > rate / 100) wavesOk = false;
    }
    CHECK(wavesOk, "render: every wave plays cleanly");

    /* Volume 0 on every track is silence, so the volume lines really act. */
    Song quiet;
    Music_ThemeToSong(Music_ThemeAt(2), &quiet);
    for (int t = 0; t < SONG_TRACKS; t++) quiet.volume[t] = 0;
    Synth sy;
    Synth_Init(&sy, &quiet, rate);
    Synth_SetIntensity(&sy, 1.0f);
    int peak, nearClip;
    double rms;
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    CHECK(peak == 0, "render: all volumes at 0 is silence");

    /* Intensity gates the lead: below 0.35 the lead track alone is inaudible. */
    Song leadOnly;
    Song_Init(&leadOnly, "Lead");
    for (int st = 0; st < SONG_STEPS; st += 4) leadOnly.patterns[0].tone[TRACK_LEAD][st] = 72;
    leadOnly.volume[TRACK_BASS] = 0;
    Synth_Init(&sy, &leadOnly, rate);
    Synth_SetIntensity(&sy, 0.2f);
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    int quietPeak = peak;
    Synth_Init(&sy, &leadOnly, rate);
    Synth_SetIntensity(&sy, 1.0f);
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    CHECK(quietPeak == 0 && peak > 1000, "render: intensity brings the lead in");
}

static void song_test_hold(void) {
    const int rate = 44100;
    Song held, plain;
    Song_Init(&held, "Hold");
    held.patterns[0].tone[TRACK_LEAD][0] = 72;
    held.patterns[0].tone[TRACK_LEAD][1] = NOTE_HOLD;
    plain = held;
    plain.patterns[0].tone[TRACK_LEAD][1] = NOTE_NONE;

    /* 120 bpm: 5512.5 samples a step. Frame 0 triggers step 0, frame ~5512
     * step 1; stopping just short of step 2 shows the envelope a step after
     * the hold or the rest. */
    int frames = (int)(2.0 * rate * 60.0 / (120.0 * 4.0)) - 2;
    static int16_t buf[32768];
    Synth a, b;
    Synth_Init(&a, &held, rate);
    Synth_Init(&b, &plain, rate);
    Synth_Render(&a, buf, frames);
    Synth_Render(&b, buf, frames);
    CHECK(a.step == 1 && b.step == 1, "hold: both synths are on step 1");
    CHECK(a.env[TRACK_LEAD] > 0.5f, "hold: a HOLD keeps the lead envelope above 0.5 a step later");
    CHECK(b.env[TRACK_LEAD] < 0.5f, "hold: a NONE lets it decay below 0.5");
    CHECK(a.held[TRACK_LEAD] && !b.held[TRACK_LEAD], "hold: the held flag follows the step");
    CHECK(fabsf(a.freq[TRACK_LEAD] - Synth_NoteHz(72)) < 0.01f, "hold: the pitch is the note before the hold");

    /* The note is sustained from its attack because the hold is known then:
     * no decay through step 0 and no jump when step 1 arrives (that used to
     * be heard as a second attack on the arp). */
    Song arp;
    Song_Init(&arp, "Arp hold");
    arp.patterns[0].tone[TRACK_ARP][0] = 64;
    arp.patterns[0].tone[TRACK_ARP][1] = NOTE_HOLD;
    arp.patterns[0].tone[TRACK_ARP][2] = NOTE_HOLD;
    arp.patterns[0].tone[TRACK_ARP][3] = NOTE_NONE;
    int stepLen = (int)(rate * 60.0 / (120.0 * 4.0)); /* 5512 */
    Synth c;
    Synth_Init(&c, &arp, rate);
    Synth_Render(&c, buf, 1);
    CHECK(c.held[TRACK_ARP], "hold: a note followed by a hold is held from its first sample");
    Synth_Render(&c, buf, stepLen - 1);
    float before = c.env[TRACK_ARP];
    Synth_Render(&c, buf, 2);
    float after = c.env[TRACK_ARP];
    CHECK(c.step == 1 && before > 0.9f, "hold: the arp has not decayed by the end of its own step");
    CHECK(after <= before && after > 0.9f, "hold: the hold step raises nothing (no second attack)");
    Synth_Render(&c, buf, stepLen);
    CHECK(c.step == 2 && c.held[TRACK_ARP] && c.env[TRACK_ARP] > 0.9f, "hold: a hold after a hold keeps sustaining");
    for (int i = 0; i < stepLen + 2 && c.step == 2; i++) Synth_Render(&c, buf, 1);
    CHECK(c.step == 3 && !c.held[TRACK_ARP], "hold: a rest after the holds releases the note");

    /* A hold on a voice that has never sounded has nothing to hold: it is
     * ignored instead of putting a silent oscillator's constant level (a DC
     * thump) on the output. */
    Song orphan;
    Song_Init(&orphan, "Orphan hold");
    orphan.patterns[0].tone[TRACK_BASS][0] = NOTE_HOLD;
    orphan.patterns[0].tone[TRACK_LEAD][0] = NOTE_HOLD;
    Synth_Init(&c, &orphan, rate);
    Synth_SetIntensity(&c, 1.0f);
    Synth_Render(&c, buf, 4096);
    int peak = 0;
    for (int i = 0; i < 4096; i++) { int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > peak) peak = v; }
    CHECK(peak == 0, "hold: a hold on a voice that never sounded is silence");
    CHECK(!c.held[TRACK_BASS] && !c.held[TRACK_LEAD] && c.env[TRACK_BASS] == 0.0f, "hold: and sets no held flag or envelope");

    /* A hold at step 0 of the next pattern sustains a note on step 63. */
    Song across;
    Song_Init(&across, "Across");
    int second;
    Song_AddPattern(&across, &second);
    Song_OrderInsert(&across, 1, second);
    across.patterns[0].tone[TRACK_LEAD][63] = 72;
    across.patterns[1].tone[TRACK_LEAD][0] = NOTE_HOLD;
    Synth_Init(&c, &across, rate);
    Synth_SetPosition(&c, 0, 63);
    Synth_Render(&c, buf, 1);
    CHECK(c.step == 63 && c.held[TRACK_LEAD], "hold: a hold across the pattern boundary is seen from step 63");
    Synth_SetLoopPattern(&c, 0);
    Synth_SetPosition(&c, 0, 63);
    Synth_Render(&c, buf, 1);
    CHECK(c.step == 63 && !c.held[TRACK_LEAD], "hold: looping pattern 0, whose step 0 is empty, does not hold");
}

static void song_test_order_advance(void) {
    const int rate = 22050;
    Song song;
    Song_Init(&song, "Two");
    Song_AddPattern(&song, NULL);
    Song_OrderInsert(&song, 1, 1);
    /* Something audible in each pattern so the render is real. */
    for (int st = 0; st < SONG_STEPS; st += 8) { song.patterns[0].tone[TRACK_BASS][st] = 40; song.patterns[1].tone[TRACK_BASS][st] = 52; }
    int stepFrames = (int)(rate * 60.0 / (120.0 * 4.0)); /* 2756.25: round down, so add slack */
    static int16_t buf[4096];

    Synth sy;
    Synth_Init(&sy, &song, rate);
    Synth_Render(&sy, buf, 1);
    CHECK(Synth_CurrentPattern(&sy) == 0 && sy.step == 0 && sy.orderPos == 0, "order: the first sample starts pattern 0 at step 0");
    int sixtyFour = stepFrames * SONG_STEPS + 64;
    for (int done = 0; done < sixtyFour; done += 4096) {
        int chunk = sixtyFour - done > 4096 ? 4096 : sixtyFour - done;
        Synth_Render(&sy, buf, chunk);
    }
    CHECK(Synth_CurrentPattern(&sy) == 1 && sy.orderPos == 1, "order: after 64 steps the order advances to pattern 1");
    for (int done = 0; done < sixtyFour; done += 4096) {
        int chunk = sixtyFour - done > 4096 ? 4096 : sixtyFour - done;
        Synth_Render(&sy, buf, chunk);
    }
    CHECK(Synth_CurrentPattern(&sy) == 0 && sy.orderPos == 0, "order: and wraps back to pattern 0");

    /* Looping: the pattern never changes however long it plays. (The synth
     * parks at the previous order slot until the first step triggers, and a
     * loop never advances the slot, so orderPos is not checked here.) */
    Synth_Init(&sy, &song, rate);
    Synth_SetLoopPattern(&sy, 0);
    CHECK(Synth_CurrentPattern(&sy) == 0, "loop: the looped pattern is current at once");
    bool stayed = true;
    for (int done = 0; done < sixtyFour * 2; done += 4096) {
        int chunk = sixtyFour * 2 - done > 4096 ? 4096 : sixtyFour * 2 - done;
        Synth_Render(&sy, buf, chunk);
        if (Synth_CurrentPattern(&sy) != 0) stayed = false;
    }
    CHECK(stayed, "loop: a looped pattern never advances");
    Synth_SetLoopPattern(&sy, 1);
    CHECK(Synth_CurrentPattern(&sy) == 1, "loop: switching the looped pattern switches at once");

    /* Clearing the loop (or naming a pattern that does not exist) goes back
     * to following the order from wherever the synth is. */
    Synth_Init(&sy, &song, rate);
    Synth_Render(&sy, buf, 1);
    Synth_SetLoopPattern(&sy, 7);
    CHECK(Synth_CurrentPattern(&sy) == 0, "loop: a missing loop pattern falls back to the order");
    Synth_SetLoopPattern(&sy, 1);
    Synth_SetLoopPattern(&sy, -1);
    CHECK(Synth_CurrentPattern(&sy) == 0, "loop: -1 follows the order again");

    /* SetPosition lands where asked on the next sample. */
    Synth_Init(&sy, &song, rate);
    Synth_SetPosition(&sy, 1, 10);
    Synth_Render(&sy, buf, 1);
    CHECK(sy.orderPos == 1 && sy.step == 10 && Synth_CurrentPattern(&sy) == 1, "position: SetPosition(1, 10) triggers that step first");
    Synth_SetPosition(&sy, 5, 70);
    Synth_Render(&sy, buf, 1);
    CHECK(sy.orderPos == 1 && sy.step == 6, "position: out-of-range position and step wrap");
}

static void song_test_wav(void) {
    char dir[700];
    if (!Song_LibraryDir(dir, sizeof(dir), true)) { CHECK(false, "wav: no scratch folder"); return; }
    char path[800];
    snprintf(path, sizeof(path), "%s/export-test.wav", dir);

    Song song;
    Song_Init(&song, "Wav");
    for (int st = 0; st < SONG_STEPS; st += 4) song.patterns[0].tone[TRACK_BASS][st] = 45;
    const int rate = 22050;
    CHECK(Synth_RenderWavFile(&song, path, rate, 0.0f), "wav: rendered");

    /* 8 s of song plus the 1 s tail at 22050 Hz, 16-bit mono. */
    uint32_t frames = (uint32_t)((Song_Seconds(&song) + 1.0f) * (float)rate);
    uint32_t dataBytes = frames * 2u;
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "wav: the file exists");
    if (f) {
        unsigned char h[44];
        size_t got = fread(h, 1, 44, f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        CHECK(got == 44 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVEfmt ", 8) == 0 && memcmp(h + 36, "data", 4) == 0, "wav: RIFF/WAVE/fmt/data tags");
        CHECK(song_LE32(h + 4) == 36u + dataBytes, "wav: RIFF size is 36 + data bytes");
        CHECK(song_LE32(h + 16) == 16 && song_LE16(h + 20) == 1 && song_LE16(h + 22) == 1, "wav: 16-byte PCM mono fmt chunk");
        CHECK(song_LE32(h + 24) == (uint32_t)rate && song_LE32(h + 28) == (uint32_t)rate * 2u, "wav: sample rate and byte rate");
        CHECK(song_LE16(h + 32) == 2 && song_LE16(h + 34) == 16, "wav: block align 2, 16 bits");
        CHECK(song_LE32(h + 40) == dataBytes, "wav: data chunk size");
        CHECK(size == 44L + (long)dataBytes, "wav: file length matches the header");
        /* The first bass note starts at frame 0, so the data is not silence. */
        fseek(f, 44, SEEK_SET);
        unsigned char s[2000];
        got = fread(s, 1, sizeof(s), f);
        int peak = 0;
        for (size_t i = 0; i + 1 < got; i += 2) {
            int v = (int)(int16_t)song_LE16(s + i);
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        CHECK(peak > 1000, "wav: the data has sound in it");
        fclose(f);
    }

    CHECK(Synth_RenderWavFile(&song, path, rate, 2.0f), "wav: capped render");
    f = fopen(path, "rb");
    if (f) {
        unsigned char h[44];
        size_t got = fread(h, 1, 44, f);
        fclose(f);
        CHECK(got == 44 && song_LE32(h + 40) == (uint32_t)(2 * rate) * 2u, "wav: maxSeconds caps the data length");
    }
    char tmpPath[820];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    CHECK(!song_FileExists(tmpPath), "wav: no temp file left behind");
    CHECK(!Synth_RenderWavFile(NULL, path, rate, 0.0f) && !Synth_RenderWavFile(&song, NULL, rate, 0.0f), "wav: NULL song or path refused");
    CHECK(!Synth_RenderWavFile(&song, path, 4000, 0.0f), "wav: a silly sample rate is refused");
    CHECK(!Synth_RenderWavFile(&song, "/nonexistent-folder/x.wav", rate, 1.0f), "wav: an unwritable path fails cleanly");
    remove(path);
}

static void song_test_empty_song(void) {
    const int rate = 22050;
    Song none;
    Song_Init(&none, "Nothing");
    none.patternCount = 0;
    none.orderCount = 0;
    Synth sy;
    Synth_Init(&sy, &none, rate);
    Synth_SetIntensity(&sy, 1.0f);
    CHECK(Synth_CurrentPattern(&sy) == -1, "empty: no current pattern");
    int peak, nearClip;
    double rms;
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    CHECK(peak < 1000, "empty: a song with no patterns renders (near) silence without crashing");
    static int16_t buf[2048];
    Synth_Render(&sy, buf, 2048);
    bool steady = true;
    for (int i = 1; i < 2048; i++) if (buf[i] != buf[0]) steady = false;
    CHECK(steady, "empty: nothing moves in the output");

    /* An empty order with patterns present plays pattern 0. */
    Song noOrder;
    Song_Init(&noOrder, "No order");
    noOrder.orderCount = 0;
    noOrder.patterns[0].tone[TRACK_BASS][0] = 45;
    Synth_Init(&sy, &noOrder, rate);
    Synth_SetIntensity(&sy, 1.0f);
    CHECK(Synth_CurrentPattern(&sy) == 0, "empty: no order means pattern 0");
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    CHECK(peak > 1000, "empty: no order still plays pattern 0");

    /* A preview sounds even when the song has nothing to play. */
    Synth_Init(&sy, &none, rate);
    Synth_SetIntensity(&sy, 1.0f);
    Synth_Preview(&sy, TRACK_LEAD, 72);
    song_Measure(&sy, rate / 4, &peak, &nearClip, &rms);
    CHECK(peak > 1000 && sy.previewTrack == -1, "preview: a lead note sounds on an empty song and the request is consumed");
    Synth_Init(&sy, &none, rate);
    Synth_SetIntensity(&sy, 1.0f);
    Synth_Preview(&sy, TRACK_DRUMS, DRUM_KICK | DRUM_SNARE);
    song_Measure(&sy, rate / 4, &peak, &nearClip, &rms);
    CHECK(peak > 1000, "preview: drums sound on an empty song");

    /* A song pointer of NULL is tolerated too. */
    Synth_Init(&sy, NULL, rate);
    song_Measure(&sy, rate / 4, &peak, &nearClip, &rms);
    CHECK(peak < 1000 && Synth_CurrentPattern(&sy) == -1, "empty: a NULL song renders quietly");
}


/* Regressions found in review, 2026-10-12. */
static void song_test_review_regressions(void) {
    /* A name with a newline must not inject a directive, and outer spaces
     * must not change what reads back. */
    Song a, b;
    Song_Init(&a, "x");
    snprintf(a.name, sizeof(a.name), "  Foo\nbpm 300  ");
    a.bpm = 90;
    char path[1024];
    CHECK(Song_LibraryPath("inject.gsong", path, sizeof(path), true), "review: library path");
    CHECK(Song_Save(&a, path) && Song_Load(&b, path), "review: save and load a hostile name");
    CHECK(b.bpm == 90, "review: a newline in the name cannot set the tempo");
    CHECK(strcmp(b.name, "Foo bpm 300") == 0, "review: the name reads back as written");

    /* The library lists only names that open as themselves. */
    char dir[700], odd[800];
    CHECK(Song_LibraryDir(dir, sizeof(dir), true), "review: library dir");
    snprintf(odd, sizeof(odd), "%s/a\\b.gsong", dir);
    FILE *f = fopen(odd, "w");
    if (f) { fputs("GSONG 1\npattern 0\n", f); fclose(f); }
    char names[64][SONG_FILE_LEN];
    int n = Song_LibraryList(names, 64);
    bool listedOdd = false;
    for (int i = 0; i < n; i++) if (strchr(names[i], '\\')) listedOdd = true;
    CHECK(!listedOdd, "review: a name SanitiseFileName would rewrite is not listed");
    remove(odd);

    /* Assigning keeps every other line of a long hand-edited music.ini. */
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char ini[800];
    snprintf(ini, sizeof(ini), "%s/ghost-arcade/music.ini", cfg ? cfg : "/nonexistent");
    Song_Assign("skyraid", NULL); /* makes sure the folder exists */
    f = fopen(ini, "w");
    if (f) {
        fputs("# header\n", f);
        for (int i = 0; i < 70; i++) fprintf(f, "# note %d\n", i);
        fputs("# ", f);
        for (int i = 0; i < 600; i++) fputc('x', f); /* longer than any read buffer */
        fputs("\nskyraid=sky.gsong\nblockfall=old.gsong\n", f);
        fclose(f);
    }
    CHECK(Song_Assign("coilrush", "coil.gsong"), "review: assign into a long file");
    CHECK(Song_Assign("blockfall", "new.gsong"), "review: reassign a slug");
    char got[SONG_FILE_LEN];
    CHECK(Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "sky.gsong") == 0, "review: an assignment past line 64 survives");
    CHECK(Song_AssignedName("coilrush", got, sizeof(got)) && strcmp(got, "coil.gsong") == 0, "review: the new assignment is there");
    CHECK(Song_AssignedName("blockfall", got, sizeof(got)) && strcmp(got, "new.gsong") == 0, "review: the old line was replaced, not kept");
    int lines = 0, xLines = 0;
    f = fopen(ini, "r");
    if (f) {
        int c, prev = '\n';
        while ((c = fgetc(f)) != EOF) { if (prev == '\n' && c == 'x') xLines++; if (c == '\n') lines++; prev = c; }
        fclose(f);
    }
    CHECK(lines == 75 && xLines == 0, "review: a long comment line is kept whole, nothing dropped");
    Song_Assign("skyraid", NULL);
    Song_Assign("coilrush", NULL);
    Song_Assign("blockfall", NULL);

    /* A song with no notes is exact digital silence (the bass floor used to
     * hold an untriggered oscillator at a constant offset). */
    Song quiet;
    Song_Init(&quiet, "quiet");
    Synth sy;
    Synth_Init(&sy, &quiet, 22050);
    Synth_SetIntensity(&sy, 1.0f);
    int16_t buf[4096];
    Synth_Render(&sy, buf, 4096);
    int peak = 0;
    for (int i = 0; i < 4096; i++) { int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > peak) peak = v; }
    CHECK(peak == 0, "review: an empty song renders exact silence");

    /* SetPosition reports the position about to play, also when looping. */
    Song two;
    Song_Init(&two, "two");
    int idx;
    Song_AddPattern(&two, &idx);
    Song_OrderInsert(&two, 1, idx);
    Synth_Init(&sy, &two, 22050);
    CHECK(sy.orderPos == 0 && Synth_CurrentPattern(&sy) == 0, "review: a fresh synth is at the first position");
    Synth_SetPosition(&sy, 1, 0);
    CHECK(sy.orderPos == 1 && Synth_CurrentPattern(&sy) == 1, "review: SetPosition lands on the asked position");
}

/* Regressions fixed in the second review pass, 2026-10-12. */
static void song_test_fix_regressions(void) {
    const int rate = 44100;
    static int16_t buf[8192];

    /* The bass floor bridges the gaps of a bass line (every built-in theme
     * retriggers its bass within a second) but a bass note nothing follows
     * dies away: no more '---' that hums and no previewed note that drones. */
    Song lone;
    Song_Init(&lone, "Lone bass");
    lone.patterns[0].tone[TRACK_BASS][0] = 48;
    Synth sy;
    Synth_Init(&sy, &lone, rate);
    Synth_SetIntensity(&sy, 1.0f);
    int peak, nearClip;
    double rms;
    song_Measure(&sy, (int)(1.2 * rate), &peak, &nearClip, &rms);
    CHECK(fabsf(sy.env[TRACK_BASS] - 0.05f) < 1e-6f, "floor: 1.2 s after a bass trigger the envelope sits on the floor");
    song_Measure(&sy, (int)(2.8 * rate), &peak, &nearClip, &rms);
    CHECK(sy.env[TRACK_BASS] < 0.001f, "floor: 4 s after a lone bass note the envelope has died");
    song_Measure(&sy, rate / 2, &peak, &nearClip, &rms);
    CHECK(peak <= 1, "floor: and the output is silence, not a hum");
    /* A preview resets the window, so the previewed note sounds in full. */
    Synth_Preview(&sy, TRACK_BASS, 40);
    song_Measure(&sy, 1024, &peak, &nearClip, &rms);
    CHECK(peak > 1000 && sy.bassAge == 1024, "floor: a bass preview restarts the window");
    /* A bass line that retriggers every two steps (the slowest theme style at
     * the slowest theme tempo) never leaves the floor's window. */
    Song line;
    Song_Init(&line, "Bass line");
    line.bpm = 84;
    for (int st = 0; st < SONG_STEPS; st += 4) line.patterns[0].tone[TRACK_BASS][st] = 48;
    Synth_Init(&sy, &line, rate);
    bool floored = true;
    for (int i = 0; i < 40; i++) {
        Synth_Render(&sy, buf, 4096);
        if (sy.bassAge >= (uint32_t)(1.5 * rate)) floored = false;
    }
    CHECK(floored, "floor: a retriggering bass line stays inside the window");

    /* With swing, a jump to an odd step used to wait a whole swing delay
     * before it triggered; now any target triggers on the very next sample
     * and the step after it follows at that step's own swung length. */
    Song swung;
    Song_Init(&swung, "Swing");
    swung.bpm = 40;
    swung.swing = 0.3f;
    for (int st = 0; st < SONG_STEPS; st++) swung.patterns[0].tone[TRACK_LEAD][st] = 72;
    double sps = rate * 60.0 / (40.0 * 4.0), sd = 0.3 * sps;
    for (int target = 4; target <= 5; target++) {
        Synth_Init(&sy, &swung, rate);
        Synth_SetPosition(&sy, 0, target);
        Synth_Render(&sy, buf, 1);
        CHECK(sy.step == target, target == 4 ? "swing: an even target triggers on the first sample" : "swing: an odd target triggers on the first sample");
        int onStep = 1; /* samples rendered while the step was the target; the last render below is the one that leaves it */
        while (sy.step == target && onStep < 100000) { Synth_Render(&sy, buf, 1); onStep++; }
        onStep--;
        double want = target % 2 == 0 ? sps + sd : sps - sd;
        CHECK(fabs((double)onStep - want) <= 1.0, target == 4 ? "swing: the step after an even target has its full swung length" : "swing: the step after an odd target has its short length");
    }
    Synth_Init(&sy, &swung, rate);
    Synth_Render(&sy, buf, 1);
    CHECK(sy.step == 0, "swing: a fresh synth still starts on step 0 at once");

    /* The vibrato phase is wrapped, so the lead's vibrato runs at the same
     * rate after a quarter of an hour (a float that only grew doubled its rate
     * after six minutes and froze after nine). */
    Song vib;
    Song_Init(&vib, "Vibrato");
    Synth_Init(&sy, &vib, 8000);
    int wrapsStart = 0, wrapsLater = 0;
    double last = sy.vibPhase;
    for (int i = 0; i < 10 * 8000; i++) { Synth_Render(&sy, buf, 1); if (sy.vibPhase < last) wrapsStart++; last = sy.vibPhase; }
    for (int i = 0; i < 15 * 60 * 8000 / 4096; i++) Synth_Render(&sy, buf, 4096);
    last = sy.vibPhase;
    for (int i = 0; i < 10 * 8000; i++) { Synth_Render(&sy, buf, 1); if (sy.vibPhase < last) wrapsLater++; last = sy.vibPhase; }
    /* 55 cycles in 10 s, give or take the one a window edge can cut. */
    CHECK(wrapsStart >= 54 && wrapsStart <= 56 && wrapsLater >= 54 && wrapsLater <= 56, "vibrato: 5.5 Hz both at the start and after 15 minutes");
    CHECK(sy.vibPhase >= 0.0 && sy.vibPhase < 1.0, "vibrato: the phase stays within one cycle");

    /* 'swing nan' in a hand-edited file no longer silences the song in a game. */
    Song parsed;
    const char *nanText = "GSONG 1\nswing nan\npattern 0\n00 C-3 C-4 C-5 KSH\n";
    CHECK(Song_Parse(&parsed, nanText, strlen(nanText)) && parsed.swing == 0.0f, "parse: swing nan is rejected (0)");
    const char *negNan = "GSONG 1\nswing -nan\npattern 0\n";
    CHECK(Song_Parse(&parsed, negNan, strlen(negNan)) && parsed.swing == 0.0f, "parse: swing -nan is rejected");
    const char *infText = "GSONG 1\nswing inf\npattern 0\n";
    CHECK(Song_Parse(&parsed, infText, strlen(infText)) && parsed.swing == 0.0f, "parse: swing inf is rejected");
    const char *okText = "GSONG 1\nswing 0.2\npattern 0\n";
    CHECK(Song_Parse(&parsed, okText, strlen(okText)) && fabsf(parsed.swing - 0.2f) < 1e-6f, "parse: a finite swing is kept");
    Synth_Init(&sy, &parsed, rate);
    Song_Parse(&parsed, nanText, strlen(nanText));
    Synth_Init(&sy, &parsed, rate);
    Synth_SetIntensity(&sy, 1.0f);
    song_Measure(&sy, rate / 4, &peak, &nearClip, &rms);
    CHECK(peak > 1000 && sy.step >= 0, "parse: the song with the bad swing plays");

    /* A comment line longer than the read buffer cannot assign a song: only
     * a true line start names a slug, as Song_Assign already read it. */
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char ini[800];
    snprintf(ini, sizeof(ini), "%s/ghost-arcade/music.ini", cfg ? cfg : "/nonexistent");
    Song_Assign("gemdive", NULL); /* makes sure the folder exists */
    FILE *f = fopen(ini, "w");
    if (f) {
        fputc('#', f);
        for (int i = 0; i < 254; i++) fputc('y', f);
        fputs("gemdive=evil.gsong\n", f);
        fputc('#', f);
        for (int i = 0; i < 600; i++) fputc('z', f);
        fputs("\nskyraid=fine.gsong\n", f);
        fclose(f);
    }
    char got[SONG_FILE_LEN];
    CHECK(!Song_AssignedName("gemdive", got, sizeof(got)), "assign: the tail of an over-long comment assigns nothing");
    CHECK(Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "fine.gsong") == 0, "assign: the real line after it still reads");
    CHECK(Song_Assign("gemdive", NULL) && !Song_AssignedName("gemdive", got, sizeof(got)), "assign: reader and writer agree after a clear");
    Song_Assign("skyraid", NULL);

    /* The WAV export is one pass and a tail of decay: the order does not wrap
     * into the tail, and a hold on the last step does not sustain into it. */
    Song one;
    Song_Init(&one, "One pass");
    one.patterns[0].tone[TRACK_BASS][0] = 36;
    one.patterns[0].tone[TRACK_ARP][0] = 72;
    one.patterns[0].tone[TRACK_LEAD][0] = 84;
    one.patterns[0].tone[TRACK_LEAD][60] = 84;
    for (int st = 61; st < SONG_STEPS; st++) one.patterns[0].tone[TRACK_LEAD][st] = NOTE_HOLD;
    one.patterns[0].drums[0] = DRUM_KICK | DRUM_SNARE;
    Synth_Init(&sy, &one, rate);
    Synth_SetIntensity(&sy, 1.0f);
    sy.stopAtOrderEnd = true;
    int pass = (int)(Song_Seconds(&one) * rate); /* 352800 */
    for (int done = 0; done < pass - 10; done += 4096) { int chunk = pass - 10 - done > 4096 ? 4096 : pass - 10 - done; Synth_Render(&sy, buf, chunk); }
    CHECK(sy.step == 63 && !sy.finished && sy.held[TRACK_LEAD], "export: just before the end the last step is still playing and held");
    Synth_Render(&sy, buf, 20);
    CHECK(sy.finished && sy.step == 63 && sy.orderPos == 0, "export: at the end of the pass the synth stops instead of wrapping");
    CHECK(!sy.held[TRACK_LEAD] && sy.env[TRACK_ARP] < 0.01f, "export: nothing is held into the tail and the arp did not retrigger");
    song_Measure(&sy, rate, &peak, &nearClip, &rms);
    CHECK(sy.step == 63 && sy.finished && sy.env[TRACK_LEAD] < 0.01f, "export: the tail only decays");
    Synth_SetPosition(&sy, 0, 0);
    Synth_Render(&sy, buf, 1);
    CHECK(!sy.finished && sy.step == 0, "export: SetPosition starts a finished synth again");
    /* The same through Synth_RenderWavFile: the tail (8 s on) is only the
     * held lead dying away, far below the opening it used to replay, and the
     * file ends close to silence instead of mid-note. */
    char dir[700], path[800];
    if (Song_LibraryDir(dir, sizeof(dir), true)) {
        snprintf(path, sizeof(path), "%s/tail-test.wav", dir);
        const int wrate = 22050;
        CHECK(Synth_RenderWavFile(&one, path, wrate, 0.0f), "export: tail test rendered");
        f = fopen(path, "rb");
        int peaks[4] = {0, 0, 0, 0}; /* 50 ms windows at 0 s, 8 s, 8.5 s and the last one */
        if (f) {
            static unsigned char raw[2 * 22050];
            const long at[4] = {0, 8L * wrate, 8L * wrate + wrate / 2, 9L * wrate - wrate / 20};
            for (int w = 0; w < 4; w++) {
                fseek(f, 44 + 2L * at[w], SEEK_SET);
                size_t gotN = fread(raw, 2, wrate / 20, f);
                for (size_t i = 0; i < gotN; i++) { int v = (int)(int16_t)song_LE16(raw + 2 * i); if (v < 0) v = -v; if (v > peaks[w]) peaks[w] = v; }
            }
            fclose(f);
        }
        CHECK(peaks[0] > 5000 && peaks[1] < peaks[0] / 4, "export: the tail does not replay the opening");
        CHECK(peaks[1] > peaks[2] && peaks[2] > peaks[3] && peaks[3] < 400, "export: the tail decays to near silence by the end of the file");
        remove(path);
    }
}

/* The optional key line: written only when the song knows its key, read back
 * exactly, skipped by nothing older than itself. */
static void song_test_key_line(void) {
    Song s;
    Song_Init(&s, "Keyed");
    CHECK(s.keyRoot == -1 && s.keyScale == 0, "key: a fresh song has no key");
    static char text[65536];
    size_t n = song_WriteToText(&s, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "\nkey ") == NULL, "key: no key line is written for a song without a key");

    /* Round trip for every root and scale. */
    bool trip = true, line = true;
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            Song a, b;
            song_BuildFull(&a);
            a.keyRoot = root;
            a.keyScale = scale;
            size_t m = song_WriteToText(&a, text, sizeof(text));
            char want[64], name[32];
            Song_KeyName(root, scale, name, sizeof(name));
            snprintf(want, sizeof(want), "\nkey %s\n", name);
            if (strstr(text, want) == NULL) line = false;
            memset(&b, 0x3C, sizeof(b));
            if (!Song_Parse(&b, text, m) || memcmp(&a, &b, sizeof(Song)) != 0) trip = false;
        }
    }
    CHECK(line, "key: the line reads 'key <root> <scale>' using the key names");
    CHECK(trip, "key: write then parse is byte-exact for every root and scale");

    /* Specific spellings. */
    Song a;
    song_BuildFull(&a);
    a.keyRoot = 9;
    a.keyScale = 0;
    size_t m = song_WriteToText(&a, text, sizeof(text));
    CHECK(strstr(text, "\nkey A minor\n") != NULL, "key: A minor is written as 'key A minor'");
    char *keyLine = strstr(text, "\nkey A minor\n");
    char *orderLine = strstr(text, "\norder ");
    CHECK(keyLine && orderLine && keyLine < orderLine, "key: the key line comes before the order (with the other header lines)");
    a.keyRoot = 1;
    a.keyScale = 4;
    m = song_WriteToText(&a, text, sizeof(text));
    CHECK(strstr(text, "\nkey C# harmonic minor\n") != NULL, "key: C# harmonic minor spells its two-word scale");
    Song b;
    CHECK(Song_Parse(&b, text, m) && b.keyRoot == 1 && b.keyScale == 4, "key: the two-word scale reads back");

    /* Older files, odd spellings and junk. */
    const char *old = "GSONG 1\nname Old\nbpm 100\npattern 0\n00 C-2 --- --- K..\n";
    CHECK(Song_Parse(&b, old, strlen(old)) && b.keyRoot == -1 && b.keyScale == 0, "key: a file without the line has no key");
    const char *flat = "GSONG 1\nkey Db major\npattern 0\n";
    CHECK(Song_Parse(&b, flat, strlen(flat)) && b.keyRoot == 1 && b.keyScale == 1, "key: a flat root is read");
    const char *lower = "GSONG 1\nkey a MINOR\npattern 0\n";
    CHECK(Song_Parse(&b, lower, strlen(lower)) && b.keyRoot == 9 && b.keyScale == 0, "key: case does not matter");
    const char *oneWord = "GSONG 1\nkey E harmonic\npattern 0\n";
    CHECK(Song_Parse(&b, oneWord, strlen(oneWord)) && b.keyRoot == 4 && b.keyScale == 4, "key: 'harmonic' alone is harmonic minor");
    const char *tabs = "GSONG 1\nkey\tG\tmixolydian\t\r\npattern 0\n";
    CHECK(Song_Parse(&b, tabs, strlen(tabs)) && b.keyRoot == 7 && b.keyScale == 5, "key: tabs and a CR are tolerated");
    const char *noScale = "GSONG 1\nkey F\npattern 0\n";
    CHECK(Song_Parse(&b, noScale, strlen(noScale)) && b.keyRoot == 5 && b.keyScale == 0, "key: a root alone is that root's minor");
    const char *badScale = "GSONG 1\nkey F lydian\npattern 0\n";
    CHECK(Song_Parse(&b, badScale, strlen(badScale)) && b.keyRoot == 5 && b.keyScale == 0, "key: an unknown scale falls back to minor");
    const char *badRoot = "GSONG 1\nkey H minor\npattern 0\n";
    CHECK(Song_Parse(&b, badRoot, strlen(badRoot)) && b.keyRoot == -1, "key: an unknown root leaves the song without a key");
    const char *bare = "GSONG 1\nkey\npattern 0\n";
    CHECK(Song_Parse(&b, bare, strlen(bare)) && b.keyRoot == -1, "key: a bare key line is ignored");
    const char *octave = "GSONG 1\nkey A4 minor\npattern 0\n";
    CHECK(Song_Parse(&b, octave, strlen(octave)) && b.keyRoot == -1, "key: a note name with an octave is not a root");

    /* A song whose key fields are out of range still writes a loadable file. */
    song_BuildFull(&a);
    a.keyRoot = 12;
    m = song_WriteToText(&a, text, sizeof(text));
    CHECK(strstr(text, "\nkey ") == NULL, "key: a root out of range is not written");
    a.keyRoot = 11;
    a.keyScale = 99;
    m = song_WriteToText(&a, text, sizeof(text));
    CHECK(strstr(text, "\nkey B minor\n") != NULL && Song_Parse(&b, text, m) && b.keyRoot == 11 && b.keyScale == 0, "key: a scale out of range is written as minor");

    /* The names. */
    bool names = true;
    for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
        const char *sn = Song_ScaleName(scale);
        if (!sn || !sn[0] || strcmp(sn, "?") == 0 || Song_ScaleFromName(sn) != scale) names = false;
        for (int o = 0; o < scale; o++) if (strcmp(Song_ScaleName(o), sn) == 0) names = false;
    }
    for (int root = 0; root < 12; root++) {
        const char *rn = Song_KeyRootName(root);
        if (!rn || !rn[0] || strcmp(rn, "?") == 0 || Song_KeyRootFromName(rn) != root) names = false;
        char nn[4];
        Song_NoteName(root + 60, nn);
        /* The root names are the note names without the octave and the dash. */
        if (rn[0] != nn[0] || (nn[1] == '#' && strcmp(rn + 1, "#") != 0) || (nn[1] == '-' && rn[1] != '\0')) names = false;
    }
    CHECK(names, "key names: every scale and root has its own name that reads back, matching the note names");
    CHECK(strcmp(Song_ScaleName(0), "minor") == 0 && strcmp(Song_ScaleName(1), "major") == 0 && strcmp(Song_ScaleName(4), "harmonic minor") == 0 && strcmp(Song_ScaleName(5), "mixolydian") == 0, "key names: the MusicScale order minor, major, dorian, phrygian, harmonic minor, mixolydian");
    CHECK(strcmp(Song_ScaleName(-1), "?") == 0 && strcmp(Song_ScaleName(SONG_SCALE_COUNT), "?") == 0 && strcmp(Song_KeyRootName(-1), "?") == 0 && strcmp(Song_KeyRootName(12), "?") == 0, "key names: out of range is a question mark");
    CHECK(Song_ScaleFromName("Harmonic Minor") == 4 && Song_ScaleFromName("harmonic") == 4 && Song_ScaleFromName("MAJOR") == 1 && Song_ScaleFromName("lydian") == -1 && Song_ScaleFromName("") == -1 && Song_ScaleFromName(NULL) == -1, "key names: scale reading is case-insensitive and refuses what it does not know");
    CHECK(Song_KeyRootFromName("Db") == 1 && Song_KeyRootFromName("c#") == 1 && Song_KeyRootFromName("Cb") == 11 && Song_KeyRootFromName("B#") == 0 && Song_KeyRootFromName("E#") == 5 && Song_KeyRootFromName("H") == -1 && Song_KeyRootFromName("A4") == -1 && Song_KeyRootFromName("") == -1 && Song_KeyRootFromName(NULL) == -1, "key names: roots accept sharps, flats and any case, nothing else");
    char kn[32];
    Song_KeyName(9, 0, kn, sizeof(kn));
    CHECK(strcmp(kn, "A minor") == 0, "key name: A minor");
    Song_KeyName(1, 4, kn, sizeof(kn));
    CHECK(strcmp(kn, "C# harmonic minor") == 0, "key name: C# harmonic minor");
    Song_KeyName(-1, 0, kn, sizeof(kn));
    CHECK(kn[0] == '\0', "key name: an unknown key is an empty string");
    Song_KeyName(5, 42, kn, sizeof(kn));
    CHECK(strcmp(kn, "F minor") == 0, "key name: a scale out of range is shown as minor");
    char tiny[4];
    Song_KeyName(9, 0, tiny, sizeof(tiny));
    CHECK(strcmp(tiny, "A m") == 0, "key name: a short buffer is cut, not overrun");

    /* The themes carry their key. */
    bool themed = true;
    for (int i = 0; i < Music_ThemeCount(); i++) {
        const MusicTheme *t = Music_ThemeAt(i);
        Song ts;
        Music_ThemeToSong(t, &ts);
        if (ts.keyRoot != t->rootMidi % 12 || ts.keyScale != (int)t->scale) themed = false;
        size_t tn = song_WriteToText(&ts, text, sizeof(text));
        Song back;
        if (!Song_Parse(&back, text, tn) || memcmp(&ts, &back, sizeof(Song)) != 0) themed = false;
    }
    CHECK(themed, "key: every theme's song carries the theme's key and round-trips with it");
}

/* Writes exactly n bytes, NULs included, for the music.ini checks below. */
static void song_PutBytes(const char *path, const char *bytes, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(bytes, 1, n, f);
    fclose(f);
}

/* Regressions fixed in the third review pass, 2026-10-13: hand-edited files
 * read the way the next save writes them. */
static void song_test_tidy_regressions(void) {
    /* The key line reads its scale word by word: tabs or a doubled space
     * inside "harmonic minor", or a note after the scale, used to turn the
     * key into plain minor, which the next save then wrote for good. */
    Song b;
    static char text[65536];
    const char *tabbed = "GSONG 1\nkey\tC#\tharmonic\tminor\npattern 0\n";
    CHECK(Song_Parse(&b, tabbed, strlen(tabbed)) && b.keyRoot == 1 && b.keyScale == 4, "key: tab-separated harmonic minor reads as harmonic minor");
    size_t m = song_WriteToText(&b, text, sizeof(text));
    CHECK(m > 0 && strstr(text, "\nkey C# harmonic minor\n") != NULL, "key: and saves as harmonic minor, not minor");
    const char *doubled = "GSONG 1\nkey C# harmonic  minor \r\npattern 0\n";
    CHECK(Song_Parse(&b, doubled, strlen(doubled)) && b.keyRoot == 1 && b.keyScale == 4, "key: a doubled space inside harmonic minor reads the same");
    const char *noted = "GSONG 1\nkey A major # chorus key\npattern 0\n";
    CHECK(Song_Parse(&b, noted, strlen(noted)) && b.keyRoot == 9 && b.keyScale == 1, "key: text after the scale is ignored, as on the other lines");
    const char *notedTwo = "GSONG 1\nkey E harmonic minor # verse\npattern 0\n";
    CHECK(Song_Parse(&b, notedTwo, strlen(notedTwo)) && b.keyRoot == 4 && b.keyScale == 4, "key: and after the two-word scale");
    const char *harmonicNoted = "GSONG 1\nkey D harmonic # verse\npattern 0\n";
    CHECK(Song_Parse(&b, harmonicNoted, strlen(harmonicNoted)) && b.keyRoot == 2 && b.keyScale == 4, "key: 'harmonic' alone before a note is still harmonic minor");
    const char *rootNoted = "GSONG 1\nkey G # no scale yet\npattern 0\n";
    CHECK(Song_Parse(&b, rootNoted, strlen(rootNoted)) && b.keyRoot == 7 && b.keyScale == 0, "key: a root and a note is that root's minor");

    /* A NUL byte in music.ini hid its line's newline, so the next line was
     * read as that line's tail: a clear could assign a song, and assigning
     * one game could reassign another. */
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char ini[800];
    snprintf(ini, sizeof(ini), "%s/ghost-arcade/music.ini", cfg ? cfg : "/nonexistent");
    Song_Assign("skyraid", NULL); /* makes sure the folder exists */
    char got[SONG_FILE_LEN];
    static const char nulA[] = "\0\nskyraid=a.gsong\n";
    song_PutBytes(ini, nulA, sizeof(nulA) - 1);
    CHECK(Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "a.gsong") == 0, "assign: the line after a NUL byte is a line of its own");
    CHECK(Song_Assign("skyraid", NULL) && !Song_AssignedName("skyraid", got, sizeof(got)), "assign: and a clear clears it instead of assigning it");
    static const char nulB[] = "\0\ngemdive=r.gsong\n";
    song_PutBytes(ini, nulB, sizeof(nulB) - 1);
    bool gemBefore = Song_AssignedName("gemdive", got, sizeof(got)) && strcmp(got, "r.gsong") == 0;
    CHECK(Song_Assign("blockfall", "x.gsong"), "assign: a set into a file with a NUL byte");
    bool gemAfter = Song_AssignedName("gemdive", got, sizeof(got)) && strcmp(got, "r.gsong") == 0;
    CHECK(gemBefore && gemAfter, "assign: giving one game a song leaves the game after a NUL line as it was");
    static const char nulC[] = "coilrush=old\0x\nskyraid=s.gsong\n";
    song_PutBytes(ini, nulC, sizeof(nulC) - 1);
    CHECK(Song_Assign("skyraid", "new.gsong") && Song_AssignedName("skyraid", got, sizeof(got)) && strcmp(got, "new.gsong") == 0, "assign: reassigning past a line with a NUL in it");
    CHECK(Song_AssignedName("coilrush", got, sizeof(got)) && strcmp(got, "old") == 0, "assign: the line with the NUL keeps its own value");
    static const char wantC[] = "coilrush=old\0x\nskyraid=new.gsong\n";
    char back[128];
    size_t backLen = 0;
    FILE *f = fopen(ini, "rb");
    if (f) { backLen = fread(back, 1, sizeof(back), f); fclose(f); }
    CHECK(backLen == sizeof(wantC) - 1 && memcmp(back, wantC, backLen) == 0, "assign: a kept line is written back byte for byte, newline and all");
    Song_Assign("coilrush", NULL);
    Song_Assign("skyraid", NULL);
    Song_Assign("blockfall", NULL);
    Song_Assign("gemdive", NULL);

    /* Song_Parse tidies a name and a swing the way Song_Write writes them,
     * so a loaded song is exactly what the next save writes back. */
    struct { const char *file, *name; } names[] = {
        {"GSONG 1\nname My tune\t\npattern 0\n", "My tune"},
        {"GSONG 1\nname My tune\x7f\npattern 0\n", "My tune"},
        {"GSONG 1\nname \x01\npattern 0\n", "Untitled"},
        {"GSONG 1\nname \x01\t\x02 Lead\x03\r\npattern 0\n", "Lead"},
        {"GSONG 1\nname 01234567890123456789012345678901234567 xyz\npattern 0\n", "01234567890123456789012345678901234567"},
    };
    bool named = true, nameTrip = true;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        Song a;
        if (!Song_Parse(&a, names[i].file, strlen(names[i].file)) || strcmp(a.name, names[i].name) != 0) { named = false; continue; }
        size_t n = song_WriteToText(&a, text, sizeof(text));
        memset(&b, 0x3C, sizeof(b));
        if (!Song_Parse(&b, text, n) || memcmp(&a, &b, sizeof(Song)) != 0) nameTrip = false;
    }
    CHECK(named, "parse: a trailing tab, DEL, control bytes and a cut on a space are tidied like Song_Write does");
    CHECK(nameTrip, "parse: and a save then reopen gives the same song byte for byte");
    const char *swing3 = "GSONG 1\nswing 0.125\npattern 0\n";
    Song sw;
    CHECK(Song_Parse(&sw, swing3, strlen(swing3)) && fabsf(sw.swing - 0.13f) < 1e-6f, "parse: a swing with three decimals is kept to hundredths");
    size_t sn = song_WriteToText(&sw, text, sizeof(text));
    CHECK(Song_Parse(&b, text, sn) && b.swing == sw.swing, "parse: and a save then reopen keeps that swing exactly");
    const char *negZero = "GSONG 1\nswing -0\npattern 0\n";
    CHECK(Song_Parse(&sw, negZero, strlen(negZero)) && sw.swing == 0.0f && !signbit(sw.swing), "parse: swing -0 is plain 0");
    bool grid = true;
    for (int k = 0; k <= 30; k++) {
        char line[64];
        snprintf(line, sizeof(line), "GSONG 1\nswing %.2f\npattern 0\n", (double)((float)k / 100.0f));
        if (!Song_Parse(&sw, line, strlen(line)) || sw.swing != (float)k / 100.0f) grid = false;
    }
    CHECK(grid, "parse: every swing the app makes (0.00 to 0.30) reads back exactly as before");
}

/* Debug check 2026-10-13: a full library keeps the first songs by name,
 * whatever order the filesystem returns them in. */
static void song_test_library_cap(void) {
    char path[1024];
    /* Created in reverse so readdir order is unlikely to match name order. */
    for (int i = 39; i >= 0; i--) {
        char name[32];
        snprintf(name, sizeof(name), "cap%02d.gsong", i);
        Song s;
        Song_Init(&s, name);
        if (Song_LibraryPath(name, path, sizeof(path), true)) Song_Save(&s, path);
    }
    char names[16][SONG_FILE_LEN];
    int n = Song_LibraryList(names, 16);
    CHECK(n == 16, "library cap: exactly maxNames come back from a fuller library");
    bool firstByName = n > 0;
    char prev[SONG_FILE_LEN] = "";
    for (int i = 0; i < n; i++) {
        if (strcmp(prev, names[i]) >= 0) firstByName = false;
        snprintf(prev, sizeof(prev), "%s", names[i]);
    }
    /* Everything else in the sandbox library sorts after or before "cap";
     * what matters is that the 16 are the 16 smallest names overall. */
    char all[256][SONG_FILE_LEN];
    int total = Song_LibraryList(all, 256);
    for (int i = 0; i < n && i < total; i++) if (strcmp(all[i], names[i]) != 0) firstByName = false;
    CHECK(firstByName, "library cap: the kept songs are the first ones by name, in order");
    CHECK(Song_LibraryList(names, 0) == 0, "library cap: room for none lists none");
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof(name), "cap%02d.gsong", i);
        if (Song_LibraryPath(name, path, sizeof(path), false)) remove(path);
    }
}

void test_song_all(void) {
    song_test_library_cap();
    song_test_init();
    song_test_round_trip();
    song_test_garbage();
    song_test_clamping();
    song_test_order_fixup();
    song_test_note_names();
    song_test_pattern_edits();
    song_test_order_edits();
    song_test_file_names();
    song_test_library();
    song_test_themes();
    song_test_render_themes();
    song_test_hold();
    song_test_order_advance();
    song_test_wav();
    song_test_empty_song();
    song_test_review_regressions();
    song_test_fix_regressions();
    song_test_key_line();
    song_test_tidy_regressions();
}
