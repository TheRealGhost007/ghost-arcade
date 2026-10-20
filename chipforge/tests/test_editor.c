/* Headless tests for the tracker model (editor.c). Compiled by inclusion from
 * test_main.c, so no main here and every helper is static with an editor_
 * prefix. An Editor carries 25 songs, so the instances live in static storage
 * rather than on the stack. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "../src/editor.h"

static Editor editor_ed;
static Song editor_snap;

static void editor_reset(void) { Editor_Init(&editor_ed); }

static int8_t editor_tone(int track, int step) {
    return editor_ed.song.patterns[editor_ed.pattern].tone[track][step];
}

static uint8_t editor_drum(int step) {
    return editor_ed.song.patterns[editor_ed.pattern].drums[step];
}

static bool editor_status_is(const char *s) { return strcmp(editor_ed.status, s) == 0; }

static void editor_go(int track, int step) {
    editor_ed.track = track;
    editor_ed.step = step;
}

/* Every index the renderer and player read without checking. */
static bool editor_invariants(const Editor *ed) {
    const Song *s = &ed->song;
    if (s->patternCount < 1 || s->patternCount > SONG_MAX_PATTERNS) return false;
    if (s->orderCount < 1 || s->orderCount > SONG_MAX_ORDER) return false;
    if (ed->pattern < 0 || ed->pattern >= s->patternCount) return false;
    if (ed->orderPos < 0 || ed->orderPos >= s->orderCount) return false;
    if (ed->step < 0 || ed->step >= SONG_STEPS) return false;
    if (ed->track < TRACK_BASS || ed->track > TRACK_DRUMS) return false;
    if (ed->octave < 1 || ed->octave > 7) return false;
    if (ed->editStep < 0 || ed->editStep > 16) return false;
    if (ed->undoCount < 0 || ed->undoCount > EDITOR_UNDO) return false;
    if (ed->undoHead < 0 || ed->undoHead >= EDITOR_UNDO) return false;
    if (s->bpm < SONG_BPM_MIN || s->bpm > SONG_BPM_MAX) return false;
    if (s->swing < 0.0f || s->swing > 0.3f + 1e-6f) return false;
    for (int i = 0; i < s->orderCount; i++) if (s->order[i] >= s->patternCount) return false;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) if (s->wave[t] < 0 || s->wave[t] >= WAVE_COUNT) return false;
    for (int t = 0; t < SONG_TRACKS; t++) if (s->volume[t] < 0 || s->volume[t] > 100) return false;
    for (int p = 0; p < s->patternCount; p++) {
        for (int t = 0; t < SONG_TONE_TRACKS; t++) for (int i = 0; i < SONG_STEPS; i++) {
            int8_t n = s->patterns[p].tone[t][i];
            if (n != NOTE_NONE && n != NOTE_HOLD && (n < NOTE_MIN || n > NOTE_MAX)) return false;
        }
        for (int i = 0; i < SONG_STEPS; i++) if (s->patterns[p].drums[i] & ~(DRUM_KICK | DRUM_SNARE | DRUM_HAT)) return false;
    }
    return true;
}

static void editor_test_init(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    CHECK(strcmp(ed->song.name, "Untitled") == 0, "init: untitled song");
    CHECK(ed->track == TRACK_BASS && ed->step == 0 && ed->pattern == 0 && ed->orderPos == 0, "init: cursor at the origin");
    CHECK(ed->octave == 4 && ed->editStep == 1, "init: octave 4, edit step 1");
    CHECK(!ed->dirty && ed->fileName[0] == '\0', "init: clean and unsaved");
    CHECK(editor_status_is("New song") && ed->statusAge == 0.0f, "init: status says new song");
    CHECK(ed->undoCount == 0, "init: empty undo ring");
    CHECK(Editor_Pattern(ed) == &ed->song.patterns[0], "init: Editor_Pattern is pattern 0");

    Editor_Tick(ed, 1.5f);
    Editor_Tick(ed, 1.5f);
    CHECK(ed->statusAge > 2.99f && ed->statusAge < 3.01f, "tick: status age accumulates");
    Editor_SetStatus(ed, "Hello");
    CHECK(editor_status_is("Hello") && ed->statusAge == 0.0f, "status: set resets the age");
    Editor_SetStatus(ed, NULL);
    CHECK(ed->status[0] == '\0', "status: NULL clears");

    /* SetSong copies, resets the cursor and forgets undo */
    Song s;
    Song_Init(&s, "Loaded");
    s.bpm = 90;
    int idx;
    Song_AddPattern(&s, &idx);
    Song_OrderInsert(&s, 1, idx);
    s.patterns[1].tone[TRACK_LEAD][5] = 60;
    ed->track = TRACK_LEAD;
    ed->step = 33;
    Editor_Apply(ed, ED_NOTE, 60);
    CHECK(ed->dirty && ed->undoCount == 1, "setsong precondition: edited");
    Editor_SetSong(ed, &s, "loaded.gsong");
    CHECK(memcmp(&ed->song, &s, sizeof(Song)) == 0, "setsong: song copied byte for byte");
    CHECK(ed->track == TRACK_BASS && ed->step == 0 && ed->pattern == 0 && ed->orderPos == 0, "setsong: cursor reset");
    CHECK(ed->undoCount == 0 && !ed->dirty, "setsong: undo forgotten, clean");
    CHECK(strcmp(ed->fileName, "loaded.gsong") == 0, "setsong: file name kept");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(editor_status_is("Nothing to undo"), "setsong: nothing to undo afterwards");
    Editor_SetSong(ed, &s, NULL);
    CHECK(ed->fileName[0] == '\0', "setsong: NULL file name becomes empty");

    /* A degenerate song (no patterns, no order) is made editable rather than trusted. */
    memset(&s, 0, sizeof(s));
    Editor_SetSong(ed, &s, NULL);
    CHECK(editor_invariants(ed), "setsong: an empty song is repaired into a valid one");
    CHECK(Editor_Pattern(ed) != NULL, "setsong: pattern pointer valid on a repaired song");
}

static void editor_test_cursor(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    CHECK(!Editor_Apply(ed, ED_UP, 0) && ed->step == 63, "cursor: up from 0 wraps to 63");
    CHECK(!Editor_Apply(ed, ED_DOWN, 0) && ed->step == 0, "cursor: down from 63 wraps to 0");
    Editor_Apply(ed, ED_DOWN, 0);
    CHECK(ed->step == 1, "cursor: down moves one");
    CHECK(!Editor_Apply(ed, ED_LEFT, 0) && ed->track == TRACK_BASS, "cursor: left stops at bass");
    for (int i = 0; i < 5; i++) Editor_Apply(ed, ED_RIGHT, 0);
    CHECK(ed->track == TRACK_DRUMS, "cursor: right stops at drums");
    Editor_Apply(ed, ED_LEFT, 0);
    CHECK(ed->track == TRACK_LEAD, "cursor: left moves one track");
    Editor_Apply(ed, ED_HOME, 0);
    CHECK(ed->step == 0, "cursor: home");
    Editor_Apply(ed, ED_PAGE_DOWN, 0);
    CHECK(ed->step == 16, "cursor: page down is a bar");
    Editor_Apply(ed, ED_PAGE_DOWN, 0);
    Editor_Apply(ed, ED_PAGE_DOWN, 0);
    Editor_Apply(ed, ED_PAGE_DOWN, 0);
    CHECK(ed->step == 63, "cursor: page down clamps at 63");
    Editor_Apply(ed, ED_PAGE_UP, 0);
    CHECK(ed->step == 47, "cursor: page up is a bar");
    Editor_Apply(ed, ED_PAGE_UP, 0);
    Editor_Apply(ed, ED_PAGE_UP, 0);
    Editor_Apply(ed, ED_PAGE_UP, 0);
    CHECK(ed->step == 0, "cursor: page up clamps at 0");
    Editor_Apply(ed, ED_END, 0);
    CHECK(ed->step == 63, "cursor: end");
    CHECK(!ed->dirty && ed->undoCount == 0, "cursor: moving never dirties or pushes undo");

    for (int i = 0; i < 10; i++) Editor_Apply(ed, ED_OCTAVE_UP, 0);
    CHECK(ed->octave == 7 && editor_status_is("Octave 7"), "octave: clamps at 7");
    for (int i = 0; i < 10; i++) Editor_Apply(ed, ED_OCTAVE_DOWN, 0);
    CHECK(ed->octave == 1 && editor_status_is("Octave 1"), "octave: clamps at 1");
    for (int i = 0; i < 20; i++) Editor_Apply(ed, ED_EDIT_STEP_UP, 0);
    CHECK(ed->editStep == 16, "edit step: clamps at 16");
    for (int i = 0; i < 20; i++) Editor_Apply(ed, ED_EDIT_STEP_DOWN, 0);
    CHECK(ed->editStep == 0, "edit step: clamps at 0");
    CHECK(!ed->dirty && ed->undoCount == 0, "octave/edit step: not part of the song");
}

static void editor_test_notes(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    editor_go(TRACK_LEAD, 0);
    CHECK(Editor_Apply(ed, ED_NOTE, 60), "note: entering reports a change");
    CHECK(editor_tone(TRACK_LEAD, 0) == 60, "note: written at the cursor");
    CHECK(ed->step == 1, "note: advances by edit step 1");
    CHECK(ed->dirty && ed->undoCount == 1, "note: dirty and one undo");
    CHECK(editor_status_is("C-4"), "note: status shows the note");
    char cell[4];
    Editor_CellText(ed, TRACK_LEAD, 0, cell);
    CHECK(strcmp(cell, "C-4") == 0, "celltext: tone note name");
    Editor_CellText(ed, TRACK_LEAD, 1, cell);
    CHECK(strcmp(cell, "---") == 0, "celltext: empty tone cell");

    ed->editStep = 4;
    Editor_Apply(ed, ED_NOTE, 200);
    CHECK(editor_tone(TRACK_LEAD, 1) == NOTE_MAX && ed->step == 5, "note: clamps high and advances by 4");
    Editor_Apply(ed, ED_NOTE, -50);
    CHECK(editor_tone(TRACK_LEAD, 5) == NOTE_MIN && ed->step == 9, "note: clamps low");
    ed->step = 62;
    Editor_Apply(ed, ED_NOTE, 64);
    CHECK(ed->step == 2, "note: advance wraps within the pattern");
    ed->editStep = 0;
    ed->step = 10;
    Editor_Apply(ed, ED_NOTE, 64);
    CHECK(ed->step == 10, "note: edit step 0 stays put");
    ed->editStep = 16;
    ed->step = 48;
    Editor_Apply(ed, ED_NOTE, 64);
    CHECK(ed->step == 0, "note: edit step 16 wraps 48 to 0");
    ed->editStep = 1;

    /* hold */
    editor_go(TRACK_ARP, 3);
    CHECK(Editor_Apply(ed, ED_HOLD, 0) && editor_tone(TRACK_ARP, 3) == NOTE_HOLD && ed->step == 4, "hold: writes === and advances");
    Editor_CellText(ed, TRACK_ARP, 3, cell);
    CHECK(strcmp(cell, "===") == 0, "celltext: hold");
    editor_go(TRACK_DRUMS, 3);
    int before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_HOLD, 0) && ed->step == 3 && ed->undoCount == before, "hold: ignored on drums");

    /* clear */
    editor_go(TRACK_ARP, 3);
    CHECK(Editor_Apply(ed, ED_CLEAR, 0) && editor_tone(TRACK_ARP, 3) == NOTE_NONE && ed->step == 4, "clear: writes none and advances");
    editor_go(TRACK_LEAD, 1);
    Editor_Apply(ed, ED_CLEAR_BACK, 0);
    CHECK(editor_tone(TRACK_LEAD, 0) == NOTE_NONE && ed->step == 0, "clear back: moves up and clears without advancing");
    before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_CLEAR_BACK, 0) && ed->step == 0 && ed->undoCount == before, "clear back: nothing above step 0");

    /* drums */
    editor_go(TRACK_DRUMS, 7);
    CHECK(Editor_Apply(ed, ED_NOTE, DRUM_KICK) && editor_drum(7) == DRUM_KICK && ed->step == 7, "drums: kick toggles on, no advance");
    Editor_Apply(ed, ED_NOTE, DRUM_HAT);
    CHECK(editor_drum(7) == (DRUM_KICK | DRUM_HAT), "drums: hat adds");
    Editor_CellText(ed, TRACK_DRUMS, 7, cell);
    CHECK(strcmp(cell, "K.H") == 0, "celltext: drums K.H");
    CHECK(editor_status_is("K.H"), "drums: status shows the cell");
    Editor_Apply(ed, ED_NOTE, DRUM_KICK);
    CHECK(editor_drum(7) == DRUM_HAT, "drums: kick toggles off");
    Editor_Apply(ed, ED_NOTE, DRUM_SNARE);
    Editor_CellText(ed, TRACK_DRUMS, 7, cell);
    CHECK(strcmp(cell, ".SH") == 0, "celltext: drums .SH");
    before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_NOTE, 0) && ed->undoCount == before, "drums: a non-drum arg does nothing");
    CHECK(Editor_Apply(ed, ED_CLEAR, 0) && editor_drum(7) == 0 && ed->step == 8, "drums: clear empties the cell and advances");
    Editor_CellText(ed, TRACK_DRUMS, 7, cell);
    CHECK(strcmp(cell, "...") == 0, "celltext: empty drums");
    Editor_CellText(ed, 9, 7, cell);
    CHECK(strcmp(cell, "---") == 0, "celltext: bad track is harmless");
    Editor_CellText(ed, TRACK_BASS, 99, cell);
    CHECK(strcmp(cell, "---") == 0, "celltext: bad step is harmless");

    /* note for key */
    ed->octave = 4;
    CHECK(Editor_NoteForKey(ed, 0) == 60, "notefor: octave 4 C is MIDI 60");
    CHECK(Editor_NoteForKey(ed, 14) == 74, "notefor: offsets run into the next octave");
    ed->octave = 1;
    CHECK(Editor_NoteForKey(ed, 0) == NOTE_MIN, "notefor: octave 1 C is C1");
    CHECK(Editor_NoteForKey(ed, -5) == NOTE_MIN, "notefor: clamps low");
    ed->octave = 7;
    CHECK(Editor_NoteForKey(ed, 16) == NOTE_MAX, "notefor: clamps high");
    Song_NoteName(Editor_NoteForKey(ed, 0), cell);
    CHECK(strcmp(cell, "C-7") == 0, "notefor: agrees with Song_NoteName");
}

static void editor_test_knobs(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    CHECK(Editor_Apply(ed, ED_BPM_UP, 10) && ed->song.bpm == 130 && editor_status_is("BPM 130"), "bpm: +10");
    CHECK(Editor_Apply(ed, ED_BPM_DOWN, 1) && ed->song.bpm == 129, "bpm: -1");
    Editor_Apply(ed, ED_BPM_UP, 1000);
    CHECK(ed->song.bpm == SONG_BPM_MAX, "bpm: clamps high");
    CHECK(!Editor_Apply(ed, ED_BPM_UP, 1), "bpm: at the limit nothing changes");
    Editor_Apply(ed, ED_BPM_DOWN, 1000);
    CHECK(ed->song.bpm == SONG_BPM_MIN, "bpm: clamps low");
    CHECK(ed->dirty, "bpm: dirties");

    editor_reset();
    Editor_Apply(ed, ED_SWING_UP, 0);
    CHECK(ed->song.swing > 0.019f && ed->song.swing < 0.021f && editor_status_is("Swing 0.02"), "swing: +0.02");
    for (int i = 0; i < 30; i++) Editor_Apply(ed, ED_SWING_UP, 0);
    CHECK(ed->song.swing > 0.299f && ed->song.swing < 0.301f, "swing: clamps at 0.3");
    for (int i = 0; i < 30; i++) Editor_Apply(ed, ED_SWING_DOWN, 0);
    CHECK(ed->song.swing == 0.0f, "swing: clamps at 0 exactly");
    Editor_Apply(ed, ED_SWING_UP, 0);
    Editor_Apply(ed, ED_SWING_UP, 0);
    Editor_Apply(ed, ED_SWING_UP, 0);
    CHECK((int)(ed->song.swing * 100.0f + 0.5f) == 6, "swing: three steps land on 0.06");

    editor_reset();
    editor_go(TRACK_BASS, 0);
    SongWave w0 = ed->song.wave[TRACK_BASS];
    CHECK(Editor_Apply(ed, ED_WAVE_NEXT, 0) && ed->song.wave[TRACK_BASS] == (w0 + 1) % WAVE_COUNT, "wave: next");
    for (int i = 0; i < WAVE_COUNT - 1; i++) Editor_Apply(ed, ED_WAVE_NEXT, 0);
    CHECK(ed->song.wave[TRACK_BASS] == w0, "wave: next cycles back round");
    Editor_Apply(ed, ED_WAVE_PREV, 0);
    CHECK(ed->song.wave[TRACK_BASS] == (w0 + WAVE_COUNT - 1) % WAVE_COUNT, "wave: prev wraps");
    CHECK(ed->song.wave[TRACK_ARP] == WAVE_PULSE25 && ed->song.wave[TRACK_LEAD] == WAVE_PULSE50, "wave: other tracks untouched");
    char buf[32];
    snprintf(buf, sizeof(buf), "Wave %s", Song_WaveName(ed->song.wave[TRACK_BASS]));
    CHECK(editor_status_is(buf), "wave: status names the wave");
    editor_go(TRACK_DRUMS, 0);
    int before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_WAVE_NEXT, 0) && ed->undoCount == before, "wave: drums have none");

    editor_reset();
    editor_go(TRACK_DRUMS, 0);
    CHECK(Editor_Apply(ed, ED_VOLUME_DOWN, 0) && ed->song.volume[TRACK_DRUMS] == 90 && editor_status_is("Volume 90"), "volume: -10 on drums");
    for (int i = 0; i < 20; i++) Editor_Apply(ed, ED_VOLUME_DOWN, 0);
    CHECK(ed->song.volume[TRACK_DRUMS] == 0, "volume: clamps at 0");
    editor_go(TRACK_ARP, 0);
    CHECK(!Editor_Apply(ed, ED_VOLUME_UP, 0) && ed->song.volume[TRACK_ARP] == 100, "volume: clamps at 100 and reports no change");
    Editor_Apply(ed, ED_VOLUME_DOWN, 0);
    CHECK(ed->song.volume[TRACK_ARP] == 90 && ed->song.volume[TRACK_BASS] == 100, "volume: per track");
}

static void editor_test_transpose(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    editor_go(TRACK_LEAD, 0);
    SongPattern *p = Editor_Pattern(ed);
    p->tone[TRACK_LEAD][0] = 60;
    p->tone[TRACK_LEAD][1] = NOTE_HOLD;
    p->tone[TRACK_LEAD][2] = NOTE_NONE;
    p->tone[TRACK_LEAD][3] = NOTE_MAX - 1;
    p->tone[TRACK_LEAD][4] = NOTE_MIN + 1;
    p->tone[TRACK_BASS][0] = 40;
    CHECK(Editor_Apply(ed, ED_TRANSPOSE_UP, 1), "transpose: reports a change");
    CHECK(p->tone[TRACK_LEAD][0] == 61 && p->tone[TRACK_LEAD][4] == NOTE_MIN + 2, "transpose: notes move a semitone");
    CHECK(p->tone[TRACK_LEAD][1] == NOTE_HOLD && p->tone[TRACK_LEAD][2] == NOTE_NONE, "transpose: holds and silence untouched");
    CHECK(p->tone[TRACK_LEAD][3] == NOTE_MAX, "transpose: up clamps at NOTE_MAX, note kept");
    CHECK(p->tone[TRACK_BASS][0] == 40, "transpose: other tracks untouched");
    CHECK(editor_status_is("Transposed up 1"), "transpose: status");
    Editor_Apply(ed, ED_TRANSPOSE_DOWN, 12);
    CHECK(p->tone[TRACK_LEAD][0] == 49 && p->tone[TRACK_LEAD][4] == NOTE_MIN && p->tone[TRACK_LEAD][3] == NOTE_MAX - 12, "transpose: down an octave, clamps at NOTE_MIN");
    for (int i = 0; i < 10; i++) Editor_Apply(ed, ED_TRANSPOSE_UP, 12);
    for (int i = 0; i < SONG_STEPS; i++) {
        int8_t n = p->tone[TRACK_LEAD][i];
        if (n >= 0) CHECK(n == NOTE_MAX, "transpose: far enough up every note is NOTE_MAX");
    }
    Editor_Apply(ed, ED_TRANSPOSE_UP, 60);
    Editor_Apply(ed, ED_TRANSPOSE_DOWN, 60);
    CHECK(p->tone[TRACK_LEAD][0] >= NOTE_MIN && p->tone[TRACK_LEAD][0] <= NOTE_MAX, "transpose: big arguments stay in range");

    /* transposing an empty track or the drums is a no-op */
    editor_go(TRACK_ARP, 0);
    int before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_TRANSPOSE_UP, 1) && ed->undoCount == before, "transpose: empty track pushes nothing");
    CHECK(editor_status_is("No notes to transpose"), "transpose: empty track says so");
    editor_go(TRACK_DRUMS, 0);
    CHECK(!Editor_Apply(ed, ED_TRANSPOSE_UP, 1), "transpose: drums ignored");
    /* only the pattern under the cursor is affected */
    editor_go(TRACK_LEAD, 0);
    Editor_Apply(ed, ED_PATTERN_DUPLICATE, 0);
    SongPattern *q = Editor_Pattern(ed);
    CHECK(q != p, "transpose setup: editing the copy");
    Editor_Apply(ed, ED_TRANSPOSE_DOWN, 1);
    CHECK(q->tone[TRACK_LEAD][0] == p->tone[TRACK_LEAD][0] - 1, "transpose: only the current pattern moves");
}

static void editor_test_undo(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    CHECK(!Editor_Apply(ed, ED_UNDO, 0) && editor_status_is("Nothing to undo"), "undo: empty ring says so");

    memcpy(&editor_snap, &ed->song, sizeof(Song));
    editor_go(TRACK_LEAD, 10);
    Editor_Apply(ed, ED_NOTE, 67);
    CHECK(memcmp(&editor_snap, &ed->song, sizeof(Song)) != 0, "undo: the note changed the song");
    CHECK(Editor_Apply(ed, ED_UNDO, 0), "undo: reports a change");
    CHECK(memcmp(&editor_snap, &ed->song, sizeof(Song)) == 0, "undo: restores the song byte for byte");
    CHECK(editor_status_is("Undone"), "undo: status");
    CHECK(ed->undoCount == 0, "undo: ring drained");

    /* a sequence of mixed edits unwinds in order */
    Song stages[6];
    memcpy(&stages[0], &ed->song, sizeof(Song));
    Editor_Apply(ed, ED_NOTE, 60);                 memcpy(&stages[1], &ed->song, sizeof(Song));
    Editor_Apply(ed, ED_BPM_UP, 10);               memcpy(&stages[2], &ed->song, sizeof(Song));
    Editor_Apply(ed, ED_PATTERN_NEW, 0);           memcpy(&stages[3], &ed->song, sizeof(Song));
    editor_go(TRACK_DRUMS, 0);
    Editor_Apply(ed, ED_NOTE, DRUM_SNARE);         memcpy(&stages[4], &ed->song, sizeof(Song));
    Editor_Apply(ed, ED_ORDER_REMOVE, 0);          memcpy(&stages[5], &ed->song, sizeof(Song));
    CHECK(ed->undoCount == 5, "undo: five edits, five snapshots");
    for (int i = 5; i > 0; i--) {
        CHECK(memcmp(&stages[i], &ed->song, sizeof(Song)) == 0, "undo: song matches its stage before undo");
        Editor_Apply(ed, ED_UNDO, 0);
        CHECK(memcmp(&stages[i - 1], &ed->song, sizeof(Song)) == 0, "undo: each step restores the previous stage");
        CHECK(editor_invariants(ed), "undo: cursor sane after every undo");
    }
    CHECK(!Editor_Apply(ed, ED_UNDO, 0) && editor_status_is("Nothing to undo"), "undo: ring empty again");

    /* undo after a delete clamps the cursor back into range */
    editor_reset();
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    CHECK(ed->pattern == 2 && ed->orderPos == 2, "undo setup: on pattern 2 at position 2");
    Editor_Apply(ed, ED_UNDO, 0);
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->song.patternCount == 1 && ed->song.orderCount == 1, "undo: both new patterns gone");
    CHECK(ed->pattern == 0 && ed->orderPos == 0, "undo: cursor clamped to what remains");

    /* ring overflow: 30 pushes, the oldest six are lost */
    editor_reset();
    editor_go(TRACK_BASS, 0);
    ed->editStep = 1;
    for (int i = 0; i < 30; i++) Editor_Apply(ed, ED_NOTE, NOTE_MIN + i);
    CHECK(ed->undoCount == EDITOR_UNDO, "undo: ring caps at EDITOR_UNDO");
    int undone = 0;
    for (int i = 0; i < EDITOR_UNDO; i++) if (Editor_Apply(ed, ED_UNDO, 0)) undone++;
    CHECK(undone == EDITOR_UNDO, "undo: 24 undos work after 30 pushes");
    CHECK(!Editor_Apply(ed, ED_UNDO, 0) && editor_status_is("Nothing to undo"), "undo: the 25th says nothing to undo");
    /* the first six notes survive (their snapshots were overwritten), the rest are undone */
    for (int i = 0; i < 30; i++) {
        int8_t n = ed->song.patterns[0].tone[TRACK_BASS][i];
        if (i < 6) CHECK(n == NOTE_MIN + i, "undo: the six oldest edits are beyond the ring");
        else CHECK(n == NOTE_NONE, "undo: the 24 newest edits are undone");
    }

    /* cursor-only actions do not push */
    editor_reset();
    Editor_Apply(ed, ED_DOWN, 0);
    Editor_Apply(ed, ED_RIGHT, 0);
    Editor_Apply(ed, ED_PAGE_DOWN, 0);
    Editor_Apply(ed, ED_OCTAVE_UP, 0);
    Editor_Apply(ed, ED_EDIT_STEP_UP, 0);
    Editor_Apply(ed, ED_PATTERN_NEXT, 0);
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    CHECK(ed->undoCount == 0 && !ed->dirty, "undo: cursor and view actions push nothing");
}

static void editor_test_patterns(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    Editor_Pattern(ed)->tone[TRACK_BASS][0] = 36;

    CHECK(Editor_Apply(ed, ED_PATTERN_NEW, 0), "pattern new: reports a change");
    CHECK(ed->song.patternCount == 2 && ed->pattern == 1, "pattern new: appended and selected");
    CHECK(ed->song.orderCount == 2 && ed->song.order[0] == 0 && ed->song.order[1] == 1 && ed->orderPos == 1, "pattern new: inserted after the current order position");
    CHECK(Editor_Pattern(ed)->tone[TRACK_BASS][0] == NOTE_NONE, "pattern new: empty");
    CHECK(editor_status_is("Pattern 01 added"), "pattern new: status");

    Editor_Apply(ed, ED_ORDER_PREV, 0);
    CHECK(ed->orderPos == 0 && ed->pattern == 0, "order prev: back to position 0 editing pattern 0");
    CHECK(Editor_Apply(ed, ED_PATTERN_DUPLICATE, 0), "pattern dup: reports a change");
    CHECK(ed->song.patternCount == 3 && ed->pattern == 2, "pattern dup: appended as pattern 2");
    CHECK(Editor_Pattern(ed)->tone[TRACK_BASS][0] == 36, "pattern dup: copied the notes");
    CHECK(ed->song.orderCount == 3 && ed->song.order[0] == 0 && ed->song.order[1] == 2 && ed->song.order[2] == 1 && ed->orderPos == 1, "pattern dup: order is 0 2 1 and we sit at 1");
    CHECK(editor_status_is("Pattern 02 duplicated"), "pattern dup: status");

    /* next/prev cycle without touching the order */
    Editor_Apply(ed, ED_PATTERN_NEXT, 0);
    CHECK(ed->pattern == 0 && ed->orderPos == 1 && ed->song.order[1] == 2, "pattern next: wraps to 0, order untouched");
    Editor_Apply(ed, ED_PATTERN_PREV, 0);
    CHECK(ed->pattern == 2 && editor_status_is("Pattern 02"), "pattern prev: wraps back to 2");
    Editor_Apply(ed, ED_PATTERN_PREV, 0);
    CHECK(ed->pattern == 1, "pattern prev: steps down");

    /* clear track */
    editor_go(TRACK_BASS, 5);
    ed->pattern = 2;
    Editor_Pattern(ed)->tone[TRACK_LEAD][3] = 70;
    CHECK(Editor_Apply(ed, ED_PATTERN_CLEAR_TRACK, 0), "clear track: reports a change");
    CHECK(Editor_Pattern(ed)->tone[TRACK_BASS][0] == NOTE_NONE && Editor_Pattern(ed)->tone[TRACK_LEAD][3] == 70, "clear track: only the bass went");
    CHECK(ed->song.patterns[0].tone[TRACK_BASS][0] == 36, "clear track: other patterns untouched");
    CHECK(editor_status_is("Track cleared"), "clear track: status");
    editor_go(TRACK_DRUMS, 0);
    Editor_Pattern(ed)->drums[9] = DRUM_HAT;
    Editor_Apply(ed, ED_PATTERN_CLEAR_TRACK, 0);
    CHECK(Editor_Pattern(ed)->drums[9] == 0, "clear track: drums too");

    /* delete fixes the order up */
    ed->pattern = 2;
    ed->orderPos = 1;
    CHECK(Editor_Apply(ed, ED_PATTERN_DELETE, 0), "pattern delete: reports a change");
    CHECK(ed->song.patternCount == 2, "pattern delete: one fewer");
    CHECK(ed->song.orderCount == 3 && ed->song.order[0] == 0 && ed->song.order[1] == 1 && ed->song.order[2] == 1, "pattern delete: entries pointing at it fall back");
    CHECK(ed->pattern == 1 && ed->orderPos == 1, "pattern delete: pattern clamped into range");
    CHECK(editor_status_is("Pattern 02 deleted"), "pattern delete: status");
    CHECK(editor_invariants(ed), "pattern delete: invariants");
    Editor_Apply(ed, ED_PATTERN_DELETE, 0);
    CHECK(ed->song.patternCount == 1 && ed->pattern == 0, "pattern delete: down to one");
    int before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_PATTERN_DELETE, 0), "pattern delete: the last one refuses");
    CHECK(editor_status_is("The last pattern stays") && ed->undoCount == before && ed->song.patternCount == 1, "pattern delete: refusal leaves everything alone");

    /* the song fills at SONG_MAX_PATTERNS */
    editor_reset();
    int added = 0;
    for (int i = 0; i < SONG_MAX_PATTERNS + 5; i++) if (Editor_Apply(ed, ED_PATTERN_NEW, 0)) added++;
    CHECK(added == SONG_MAX_PATTERNS - 1 && ed->song.patternCount == SONG_MAX_PATTERNS, "pattern new: stops at the limit");
    CHECK(editor_status_is("The song is full"), "pattern new: full status");
    CHECK(!Editor_Apply(ed, ED_PATTERN_DUPLICATE, 0), "pattern dup: refuses when full");
    CHECK(editor_invariants(ed), "pattern new: invariants at the limit");
}

static void editor_test_order(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    /* order 0 1 2, at position 2 */
    CHECK(ed->orderPos == 2 && ed->song.orderCount == 3, "order setup");
    CHECK(!Editor_Apply(ed, ED_ORDER_NEXT, 0) && ed->orderPos == 2, "order next: clamps at the end");
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    CHECK(ed->orderPos == 1 && ed->pattern == 1 && editor_status_is("Position 01"), "order prev: pattern follows");
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    CHECK(ed->orderPos == 0 && ed->pattern == 0, "order prev: clamps at 0");
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    CHECK(ed->orderPos == 1 && ed->pattern == 1, "order next: moves and follows");

    CHECK(Editor_Apply(ed, ED_ORDER_SET_UP, 0) && ed->song.order[1] == 2 && ed->pattern == 2, "order set up: +1");
    CHECK(editor_status_is("Position plays pattern 02"), "order set: status");
    Editor_Apply(ed, ED_ORDER_SET_UP, 0);
    CHECK(ed->song.order[1] == 0 && ed->pattern == 0, "order set up: wraps over patternCount");
    Editor_Apply(ed, ED_ORDER_SET_DOWN, 0);
    CHECK(ed->song.order[1] == 2 && ed->pattern == 2, "order set down: wraps the other way");
    Editor_Apply(ed, ED_ORDER_SET_DOWN, 0);
    CHECK(ed->song.order[1] == 1 && ed->pattern == 1, "order set down: -1");

    CHECK(Editor_Apply(ed, ED_ORDER_INSERT, 0), "order insert: reports a change");
    CHECK(ed->song.orderCount == 4 && ed->song.order[2] == 1 && ed->song.order[3] == 2 && ed->orderPos == 2 && ed->pattern == 1, "order insert: repeat after the position, cursor moves onto it");
    CHECK(editor_status_is("Pattern 01 repeated"), "order insert: status");

    CHECK(Editor_Apply(ed, ED_ORDER_REMOVE, 0), "order remove: reports a change");
    CHECK(ed->song.orderCount == 3 && ed->song.order[2] == 2 && ed->orderPos == 2 && ed->pattern == 2, "order remove: the next entry slides in and the pattern follows");
    CHECK(editor_status_is("Order entry removed"), "order remove: status");
    Editor_Apply(ed, ED_ORDER_REMOVE, 0);
    CHECK(ed->song.orderCount == 2 && ed->orderPos == 1 && ed->pattern == 1, "order remove: removing the last position clamps back");
    Editor_Apply(ed, ED_ORDER_REMOVE, 0);
    CHECK(ed->song.orderCount == 1 && ed->orderPos == 0 && ed->pattern == 0, "order remove: down to one entry");
    int before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_ORDER_REMOVE, 0) && ed->song.orderCount == 1 && ed->undoCount == before, "order remove: the last entry refuses");
    CHECK(editor_status_is("The order needs one entry"), "order remove: refusal status");

    /* a single-pattern song cannot change what a position plays */
    editor_reset();
    before = ed->undoCount;
    CHECK(!Editor_Apply(ed, ED_ORDER_SET_UP, 0) && ed->undoCount == before, "order set: nothing to cycle with one pattern");

    /* the order fills at SONG_MAX_ORDER */
    int inserted = 0;
    for (int i = 0; i < SONG_MAX_ORDER + 5; i++) if (Editor_Apply(ed, ED_ORDER_INSERT, 0)) inserted++;
    CHECK(inserted == SONG_MAX_ORDER - 1 && ed->song.orderCount == SONG_MAX_ORDER, "order insert: stops at the limit");
    CHECK(editor_status_is("The order is full"), "order insert: full status");
    CHECK(ed->orderPos == SONG_MAX_ORDER - 1, "order insert: cursor rode along to the end");
    /* a new pattern still gets made when the order is full; it just is not placed */
    CHECK(Editor_Apply(ed, ED_PATTERN_NEW, 0) && ed->song.patternCount == 2 && ed->song.orderCount == SONG_MAX_ORDER && ed->pattern == 1, "pattern new: works with a full order");
    CHECK(editor_invariants(ed), "order: invariants at the limit");
}

/* A plain LCG: deterministic across platforms, good enough to shake the model. */
static uint32_t editor_rngState = 12345u;
static uint32_t editor_rand(void) {
    editor_rngState = editor_rngState * 1664525u + 1013904223u;
    return editor_rngState >> 8;
}

static void editor_test_fuzz(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    int failures = 0;
    int changes = 0;
    static const int drumBits[3] = {DRUM_KICK, DRUM_SNARE, DRUM_HAT};
    for (int i = 0; i < 5000; i++) {
        EditorAction a = (EditorAction)(editor_rand() % ED_ACTION_COUNT);
        int arg;
        switch (editor_rand() % 4) {
            case 0: arg = (int)(editor_rand() % 256) - 64; break;          /* around the MIDI range */
            case 1: arg = drumBits[editor_rand() % 3]; break;              /* a drum pad */
            case 2: arg = (int)(editor_rand() % 2000000) - 1000000; break; /* wild */
            default: arg = (int)(editor_rand() % 13); break;               /* 0..12 */
        }
        if (Editor_Apply(ed, a, arg)) changes++;
        if (!editor_invariants(ed)) {
            if (failures == 0) printf("fuzz: invariants broken after action %d (arg %d) at iteration %d\n", (int)a, arg, i);
            failures++;
        }
        if ((i % 7) == 0) Editor_Tick(ed, 0.016f);
        /* Editor_Pattern and CellText must be callable at any moment */
        SongPattern *p = Editor_Pattern(ed);
        char cell[4];
        Editor_CellText(ed, ed->track, ed->step, cell);
        if (p != &ed->song.patterns[ed->pattern] || cell[3] != '\0') failures++;
    }
    CHECK(failures == 0, "fuzz: 5000 random actions keep every index in range");
    CHECK(changes > 500, "fuzz: the random walk did edit the song");
    CHECK(ed->dirty, "fuzz: dirty after edits");
    CHECK(strlen(ed->status) < EDITOR_STATUS_LEN, "fuzz: status stays terminated");
}

/* Regression: undo puts the cursor back on the pattern, order position, track
 * and step the edit was made from, not wherever the edit left it, so the grid
 * always shows the pattern the highlighted slot plays after Ctrl+Z. */
static void editor_test_undo_cursor(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    /* order 0 1 2, back at position 0 editing pattern 0 */
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    CHECK(ed->orderPos == 0 && ed->pattern == 0 && ed->song.orderCount == 3, "undo cursor setup: order 0 1 2 at position 0");

    /* Ctrl+N then Ctrl+Z */
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    CHECK(ed->orderPos == 1 && ed->pattern == 3, "undo cursor setup: new pattern moved to position 1, pattern 3");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->orderPos == 0 && ed->pattern == 0, "undo cursor: Ctrl+N undone returns to position 0, pattern 0");
    CHECK(ed->pattern == ed->song.order[ed->orderPos], "undo cursor: the grid shows what the slot plays");

    /* Shift+Delete at position 1 then Ctrl+Z */
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    CHECK(ed->orderPos == 1 && ed->pattern == 1, "undo cursor setup: position 1, pattern 1");
    Editor_Apply(ed, ED_ORDER_REMOVE, 0);
    CHECK(ed->pattern == 2, "undo cursor setup: removal slid pattern 2 under the cursor");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->orderPos == 1 && ed->pattern == 1 && ed->song.order[1] == 1, "undo cursor: order remove undone returns to pattern 1 at position 1");

    /* Ctrl+Shift+Up then Ctrl+Z */
    Editor_Apply(ed, ED_ORDER_SET_UP, 0);
    CHECK(ed->pattern == 2 && ed->song.order[1] == 2, "undo cursor setup: slot 1 now plays pattern 2");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->pattern == 1 && ed->song.order[1] == 1, "undo cursor: order set undone returns the grid to the slot's pattern");

    /* a note: the cursor returns to the track and step it was typed on */
    editor_go(TRACK_LEAD, 10);
    Editor_Apply(ed, ED_NOTE, 67);
    Editor_Apply(ed, ED_RIGHT, 0);
    Editor_Apply(ed, ED_DOWN, 0);
    CHECK(ed->track == TRACK_DRUMS && ed->step == 12, "undo cursor setup: moved away after the note");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->track == TRACK_LEAD && ed->step == 10, "undo cursor: note undone puts the cursor back on its step");
    CHECK(editor_tone(TRACK_LEAD, 10) == NOTE_NONE, "undo cursor: the note is gone");

    /* backspace: undo lands where the key was pressed, not on the cleared row */
    editor_go(TRACK_LEAD, 20);
    Editor_Apply(ed, ED_NOTE, 60);
    CHECK(ed->step == 21, "undo cursor setup: at step 21 after the note");
    Editor_Apply(ed, ED_CLEAR_BACK, 0);
    CHECK(ed->step == 20 && editor_tone(TRACK_LEAD, 20) == NOTE_NONE, "undo cursor setup: backspace cleared step 20");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->step == 21 && editor_tone(TRACK_LEAD, 20) == 60, "undo cursor: backspace undone returns to step 21");

    /* a pattern delete: undo returns to the deleted pattern's slot */
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    CHECK(ed->orderPos == 2 && ed->pattern == 2, "undo cursor setup: position 2, pattern 2");
    Editor_Apply(ed, ED_PATTERN_DELETE, 0);
    CHECK(ed->song.patternCount == 2 && ed->pattern == 1, "undo cursor setup: deleting pattern 2 leaves the slot on pattern 1");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->song.patternCount == 3 && ed->orderPos == 2 && ed->pattern == 2, "undo cursor: pattern delete undone returns to pattern 2 at position 2");
    CHECK(editor_invariants(ed), "undo cursor: invariants hold throughout");
}

/* Regression: edits that change nothing push no snapshot, leave the song
 * clean and return false, so a held Backspace over blank rows cannot push
 * real edits out of the undo ring and a freshly opened song does not turn
 * dirty when Delete walks over empty cells. */
static void editor_test_noop_edits(void) {
    editor_reset();
    Editor *ed = &editor_ed;

    /* Delete on an empty cell still walks down; Backspace still walks up */
    editor_go(TRACK_LEAD, 5);
    CHECK(!Editor_Apply(ed, ED_CLEAR, 0), "noop clear: an empty cell reports no change");
    CHECK(ed->step == 6 && ed->undoCount == 0 && !ed->dirty, "noop clear: cursor advanced, nothing pushed, still clean");
    CHECK(!Editor_Apply(ed, ED_CLEAR_BACK, 0), "noop clear back: an empty cell reports no change");
    CHECK(ed->step == 5 && ed->undoCount == 0 && !ed->dirty, "noop clear back: cursor moved up, nothing pushed, still clean");
    editor_go(TRACK_DRUMS, 5);
    CHECK(!Editor_Apply(ed, ED_CLEAR, 0) && ed->step == 6 && ed->undoCount == 0 && !ed->dirty, "noop clear: an empty drum cell too");

    /* hold on a held cell, the same note on the same cell */
    editor_go(TRACK_ARP, 3);
    CHECK(Editor_Apply(ed, ED_HOLD, 0) && ed->undoCount == 1, "noop setup: the first hold is an edit");
    editor_go(TRACK_ARP, 3);
    CHECK(!Editor_Apply(ed, ED_HOLD, 0), "noop hold: holding a held cell reports no change");
    CHECK(ed->step == 4 && ed->undoCount == 1, "noop hold: cursor advanced, nothing pushed");
    editor_go(TRACK_LEAD, 0);
    Editor_Apply(ed, ED_NOTE, 60);
    editor_go(TRACK_LEAD, 0);
    CHECK(!Editor_Apply(ed, ED_NOTE, 60) && ed->step == 1 && ed->undoCount == 2, "noop note: the same note again advances but pushes nothing");
    CHECK(Editor_Apply(ed, ED_NOTE, 62) && ed->undoCount == 3, "noop note: a different note is still an edit");
    editor_go(TRACK_DRUMS, 9);
    Editor_Apply(ed, ED_NOTE, DRUM_KICK);
    CHECK(Editor_Apply(ed, ED_NOTE, DRUM_KICK) && editor_drum(9) == 0 && ed->undoCount == 5, "noop: a drum pad always toggles, so it is never a no-op");

    /* clear track on an empty track */
    editor_reset();
    editor_go(TRACK_BASS, 0);
    CHECK(!Editor_Apply(ed, ED_PATTERN_CLEAR_TRACK, 0), "noop clear track: an empty track reports no change");
    CHECK(ed->undoCount == 0 && !ed->dirty && editor_status_is("Track already empty"), "noop clear track: nothing pushed, clean, says so");
    editor_go(TRACK_DRUMS, 0);
    CHECK(!Editor_Apply(ed, ED_PATTERN_CLEAR_TRACK, 0) && ed->undoCount == 0 && !ed->dirty, "noop clear track: empty drums too");
    editor_go(TRACK_LEAD, 7);
    Editor_Pattern(ed)->tone[TRACK_LEAD][7] = NOTE_HOLD;
    CHECK(Editor_Apply(ed, ED_PATTERN_CLEAR_TRACK, 0) && ed->undoCount == 1, "noop clear track: a lone hold is content, so clearing is an edit");

    /* transpose when every note is already pinned at the limit */
    editor_reset();
    editor_go(TRACK_LEAD, 0);
    Editor_Pattern(ed)->tone[TRACK_LEAD][0] = NOTE_MAX;
    Editor_Pattern(ed)->tone[TRACK_LEAD][1] = NOTE_HOLD;
    Editor_Pattern(ed)->tone[TRACK_LEAD][2] = NOTE_MAX;
    CHECK(!Editor_Apply(ed, ED_TRANSPOSE_UP, 1), "noop transpose: all at NOTE_MAX reports no change");
    CHECK(ed->undoCount == 0 && !ed->dirty && editor_status_is("Notes already at the limit"), "noop transpose: nothing pushed, clean, says so");
    CHECK(Editor_Apply(ed, ED_TRANSPOSE_DOWN, 1) && ed->undoCount == 1, "noop transpose: moving down from the top is an edit");
    Editor_Pattern(ed)->tone[TRACK_LEAD][2] = NOTE_MIN;
    CHECK(Editor_Apply(ed, ED_TRANSPOSE_UP, 1) && ed->undoCount == 2, "noop transpose: one movable note is enough");
    Editor_Pattern(ed)->tone[TRACK_LEAD][0] = NOTE_MIN;
    Editor_Pattern(ed)->tone[TRACK_LEAD][2] = NOTE_MIN;
    CHECK(!Editor_Apply(ed, ED_TRANSPOSE_DOWN, 12) && ed->undoCount == 2 && editor_status_is("Notes already at the limit"), "noop transpose: all at NOTE_MIN reports no change");

    /* the reviewer's scenario: one real edit, then 30 backspaces over blank rows */
    editor_reset();
    editor_go(TRACK_LEAD, 0);
    Editor_Apply(ed, ED_NOTE, 60);
    editor_go(TRACK_LEAD, 40);
    int changed = 0;
    for (int i = 0; i < 30; i++) if (Editor_Apply(ed, ED_CLEAR_BACK, 0)) changed++;
    CHECK(changed == 0 && ed->step == 10 && ed->undoCount == 1, "noop: 30 backspaces over blank rows walked up and pushed nothing");
    CHECK(Editor_Apply(ed, ED_UNDO, 0) && editor_tone(TRACK_LEAD, 0) == NOTE_NONE, "noop: Ctrl+Z still undoes the real edit");
    CHECK(!Editor_Apply(ed, ED_UNDO, 0), "noop: and the ring is then empty");

    /* a loaded song stays clean when Delete walks over empty cells */
    Song s;
    Song_Init(&s, "Loaded");
    Editor_SetSong(ed, &s, "loaded.gsong");
    editor_go(TRACK_ARP, 0);
    for (int i = 0; i < 5; i++) Editor_Apply(ed, ED_CLEAR, 0);
    CHECK(!ed->dirty && ed->undoCount == 0 && ed->step == 5, "noop: Delete over a loaded song's empty cells leaves it clean");
}

/* Regression: deleting the pattern under the cursor leaves the grid on the
 * pattern the highlighted order slot plays, not on whichever pattern slid
 * into the deleted index. */
static void editor_test_delete_sync(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    /* order 0 1 2, position 1 editing pattern 1 */
    CHECK(ed->orderPos == 1 && ed->pattern == 1, "delete sync setup: position 1 editing pattern 1");
    ed->song.patterns[2].tone[TRACK_BASS][0] = 40; /* mark the old pattern 2 */
    CHECK(Editor_Apply(ed, ED_PATTERN_DELETE, 0), "delete sync: reports a change");
    CHECK(ed->song.orderCount == 3 && ed->song.order[0] == 0 && ed->song.order[1] == 0 && ed->song.order[2] == 1, "delete sync: order is 0 0 1");
    CHECK(ed->orderPos == 1 && ed->pattern == 0, "delete sync: the grid follows the slot to pattern 0");
    CHECK(ed->pattern == ed->song.order[ed->orderPos], "delete sync: pattern is what the slot plays");
    CHECK(ed->song.patterns[1].tone[TRACK_BASS][0] == 40, "delete sync: the old pattern 2 is now pattern 1, not on screen");
    CHECK(editor_status_is("Pattern 01 deleted"), "delete sync: status names the deleted index");

    /* viewing a pattern the slot does not play: deleting it returns to the slot's pattern */
    editor_reset();
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    Editor_Apply(ed, ED_PATTERN_NEXT, 0);
    Editor_Apply(ed, ED_PATTERN_NEXT, 0);
    CHECK(ed->orderPos == 0 && ed->pattern == 2, "delete sync setup: position 0 viewing pattern 2");
    Editor_Apply(ed, ED_PATTERN_DELETE, 0);
    CHECK(ed->song.patternCount == 2 && ed->orderPos == 0 && ed->pattern == ed->song.order[0], "delete sync: the grid returns to the slot's pattern");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(ed->song.patternCount == 3 && ed->orderPos == 0 && ed->pattern == 2, "delete sync: undo returns to viewing pattern 2");
    CHECK(editor_invariants(ed), "delete sync: invariants");
}

/* The generators' preview writes: Replace rewrites the live song with no undo
 * entry and no cursor move (a picker trying styles), Commit pushes the one
 * snapshot the picker took when it opened, so the whole session is one
 * Ctrl+Z. Songs live in static storage: an Editor already carries 25. */
static Song editor_before, editor_other;
static SongPattern editor_pat;

static void editor_test_commit(void) {
    editor_reset();
    Editor *ed = &editor_ed;
    /* order 0 1 2, position 1 editing pattern 1, cursor on the lead at step 9 */
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_PATTERN_NEW, 0);
    Editor_Apply(ed, ED_ORDER_PREV, 0);
    editor_go(TRACK_LEAD, 9);
    Editor_Pattern(ed)->tone[TRACK_LEAD][9] = 60;
    ed->dirty = false;
    int undos = ed->undoCount, head = ed->undoHead;
    memcpy(&editor_before, &ed->song, sizeof(Song));
    SongPattern before1 = ed->song.patterns[1];
    Song *live = &ed->song;

    /* Replace pattern: byte-exact write, nothing else moves */
    Song_ClearPattern(&editor_pat);
    editor_pat.tone[TRACK_BASS][0] = 40;
    editor_pat.drums[4] = DRUM_KICK | DRUM_HAT;
    CHECK(Editor_ReplacePattern(ed, 1, &editor_pat), "replace pattern: accepted");
    CHECK(memcmp(&ed->song.patterns[1], &editor_pat, sizeof(SongPattern)) == 0, "replace pattern: pattern is src byte for byte");
    CHECK(memcmp(&ed->song.patterns[0], &editor_before.patterns[0], sizeof(SongPattern)) == 0
          && memcmp(&ed->song.patterns[2], &editor_before.patterns[2], sizeof(SongPattern)) == 0, "replace pattern: other patterns untouched");
    CHECK(ed->undoCount == undos && ed->undoHead == head, "replace pattern: no undo entry");
    CHECK(ed->track == TRACK_LEAD && ed->step == 9 && ed->pattern == 1 && ed->orderPos == 1, "replace pattern: cursor unchanged");
    CHECK(!ed->dirty, "replace pattern: dirty flag left alone");
    for (int i = 0; i < 5; i++) {
        editor_pat.tone[TRACK_ARP][i] = (int8_t)(50 + i);
        Editor_ReplacePattern(ed, 1, &editor_pat);
    }
    CHECK(ed->undoCount == undos, "replace pattern: five previews, still no undo entry");

    /* Commit pattern: one entry; undo is the song before the first preview, cursor included */
    CHECK(Editor_CommitPattern(ed, 1, &before1), "commit pattern: accepted");
    CHECK(ed->undoCount == undos + 1, "commit pattern: exactly one undo entry");
    CHECK(ed->dirty, "commit pattern: dirty");
    CHECK(memcmp(&ed->song.patterns[1], &editor_pat, sizeof(SongPattern)) == 0, "commit pattern: keeps the previewed pattern");
    Editor_Apply(ed, ED_RIGHT, 0);
    Editor_Apply(ed, ED_DOWN, 0);
    CHECK(Editor_Apply(ed, ED_UNDO, 0), "commit pattern: undo reports a change");
    CHECK(memcmp(&ed->song, &editor_before, sizeof(Song)) == 0, "commit pattern: one Ctrl+Z restores the song before the previews byte for byte");
    CHECK(ed->track == TRACK_LEAD && ed->step == 9 && ed->pattern == 1 && ed->orderPos == 1, "commit pattern: undo restores the cursor the picker opened on");
    CHECK(ed->undoCount == undos, "commit pattern: the entry is consumed");
    CHECK(&ed->song == live, "commit pattern: the song never moved");

    /* Commit of an unchanged pattern is not an edit */
    ed->dirty = false;
    CHECK(!Editor_CommitPattern(ed, 1, &ed->song.patterns[1]), "commit pattern: identical pattern pushes nothing");
    CHECK(ed->undoCount == undos && !ed->dirty, "commit pattern: no entry, still clean");

    /* Bad arguments change nothing */
    memcpy(&editor_other, &ed->song, sizeof(Song));
    CHECK(!Editor_ReplacePattern(ed, -1, &editor_pat) && !Editor_ReplacePattern(ed, SONG_MAX_PATTERNS, &editor_pat)
          && !Editor_ReplacePattern(ed, 1, NULL), "replace pattern: bad index or NULL refused");
    CHECK(!Editor_CommitPattern(ed, 99, &editor_pat) && !Editor_CommitPattern(ed, 1, NULL) && !Editor_ReplaceSong(ed, NULL)
          && !Editor_CommitSong(ed, NULL), "commit: bad index or NULL refused");
    CHECK(memcmp(&ed->song, &editor_other, sizeof(Song)) == 0 && ed->undoCount == undos && !ed->dirty, "bad arguments: song, undo and dirty untouched");
    /* a slot beyond patternCount may be filled before a count grows */
    CHECK(Editor_ReplacePattern(ed, 5, &editor_pat) && ed->song.patternCount == 3
          && memcmp(&ed->song.patterns[5], &editor_pat, sizeof(SongPattern)) == 0, "replace pattern: a slot past patternCount is writable");
    CHECK(editor_invariants(ed), "replace pattern: invariants");

    /* Replace song, same shape: byte-exact, cursor and undo untouched */
    memcpy(&editor_before, &ed->song, sizeof(Song));
    memcpy(&editor_other, &ed->song, sizeof(Song));
    editor_other.bpm = 150;
    editor_other.swing = 0.1f;
    editor_other.wave[TRACK_LEAD] = WAVE_SAW;
    editor_other.volume[TRACK_DRUMS] = 70;
    snprintf(editor_other.name, sizeof(editor_other.name), "Replaced");
    editor_other.patterns[2].tone[TRACK_BASS][16] = 45;
    editor_other.order[2] = 0;
    undos = ed->undoCount;
    CHECK(Editor_ReplaceSong(ed, &editor_other), "replace song: accepted");
    CHECK(memcmp(&ed->song, &editor_other, sizeof(Song)) == 0, "replace song: song is src byte for byte");
    CHECK(&ed->song == live, "replace song: the song kept its address");
    CHECK(ed->undoCount == undos, "replace song: no undo entry");
    CHECK(ed->track == TRACK_LEAD && ed->step == 9 && ed->pattern == 1 && ed->orderPos == 1, "replace song: cursor unchanged");
    CHECK(!ed->dirty, "replace song: dirty flag left alone");
    CHECK(Editor_ReplaceSong(ed, &ed->song) && memcmp(&ed->song, &editor_other, sizeof(Song)) == 0, "replace song: replacing with itself is harmless");

    /* Commit song: one entry, undo restores the song and cursor the picker opened on */
    CHECK(Editor_CommitSong(ed, &editor_before), "commit song: accepted");
    CHECK(ed->undoCount == undos + 1 && ed->dirty, "commit song: exactly one undo entry, dirty");
    CHECK(memcmp(&ed->song, &editor_other, sizeof(Song)) == 0, "commit song: keeps the previewed song");
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    Editor_Apply(ed, ED_END, 0);
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(memcmp(&ed->song, &editor_before, sizeof(Song)) == 0, "commit song: one Ctrl+Z restores the song before byte for byte");
    CHECK(ed->track == TRACK_LEAD && ed->step == 9 && ed->pattern == 1 && ed->orderPos == 1, "commit song: undo restores the cursor");
    ed->dirty = false;
    CHECK(!Editor_CommitSong(ed, &ed->song) && ed->undoCount == undos && !ed->dirty, "commit song: identical song pushes nothing");

    /* Fewer patterns: the cursor is clamped, order entries stay in range */
    Editor_Apply(ed, ED_ORDER_NEXT, 0);
    CHECK(ed->orderPos == 2 && ed->pattern == 2, "shrink setup: at position 2 on pattern 2");
    Song_Init(&editor_other, "Small");
    editor_other.patterns[0].tone[TRACK_BASS][0] = 36;
    memcpy(&editor_before, &ed->song, sizeof(Song));
    CHECK(Editor_ReplaceSong(ed, &editor_other), "shrink: accepted");
    CHECK(memcmp(&ed->song, &editor_other, sizeof(Song)) == 0, "shrink: song is src byte for byte");
    CHECK(ed->pattern == 0 && ed->orderPos == 0, "shrink: pattern and order position clamped into the smaller song");
    CHECK(ed->track == TRACK_LEAD && ed->step == 9, "shrink: track and step kept");
    CHECK(editor_invariants(ed), "shrink: invariants");
    undos = ed->undoCount;
    CHECK(Editor_CommitSong(ed, &editor_before) && ed->undoCount == undos + 1, "shrink commit: one entry");
    Editor_Apply(ed, ED_UNDO, 0);
    CHECK(memcmp(&ed->song, &editor_before, sizeof(Song)) == 0, "shrink commit: undo brings the big song back");
    /* The entry holds the cursor as it stood at the commit, which the shrink
     * had already clamped; a caller that wants the picker's opening cursor
     * back puts it there before committing. */
    CHECK(ed->orderPos == 0 && ed->pattern == 0 && editor_invariants(ed), "shrink commit: undo returns to the clamped cursor the commit saw");

    /* A commit after the cursor has outgrown the song clamps it (Grow's Cancel path). */
    ed->pattern = 2;
    ed->orderPos = 2;
    Editor_ReplaceSong(ed, &editor_other);
    ed->pattern = 7; /* a caller that moved onto a pattern the song does not have */
    ed->orderPos = 9;
    CHECK(Editor_CommitSong(ed, &editor_before) && ed->pattern == 0 && ed->orderPos == 0, "commit song: clamps a cursor the song cannot hold");
    CHECK(editor_invariants(ed), "commit song: invariants after clamp");

    /* A degenerate song (order pointing past its patterns, no patterns) is repaired like SetSong does. */
    memset(&editor_other, 0, sizeof(Song));
    editor_other.bpm = 120;
    Song_ClearPattern(&editor_other.patterns[0]);
    Song_ClearPattern(&editor_other.patterns[1]);
    editor_other.patternCount = 2;
    editor_other.orderCount = 3;
    editor_other.order[0] = 0;
    editor_other.order[1] = 7;
    editor_other.order[2] = 1;
    Editor_ReplaceSong(ed, &editor_other);
    CHECK(ed->song.patternCount == 2 && ed->song.orderCount == 3, "repair: counts kept");
    CHECK(ed->song.order[1] < ed->song.patternCount, "repair: order entries stay below patternCount");
    CHECK(editor_invariants(ed), "repair: invariants");
    memset(&editor_other, 0, sizeof(Song));
    editor_other.bpm = 120;
    Editor_ReplaceSong(ed, &editor_other);
    CHECK(ed->song.patternCount == 1 && ed->song.orderCount == 1 && Editor_Pattern(ed) != NULL, "repair: an empty song gets one pattern and one order entry");
    CHECK(editor_invariants(ed), "repair: invariants on an empty song");

    /* Many commits still respect the ring. */
    editor_reset();
    for (int i = 0; i < EDITOR_UNDO + 6; i++) {
        memcpy(&editor_before, &ed->song, sizeof(Song));
        Song_ClearPattern(&editor_pat);
        editor_pat.tone[TRACK_LEAD][0] = (int8_t)(NOTE_MIN + i);
        Editor_ReplacePattern(ed, 0, &editor_pat);
        Editor_CommitPattern(ed, 0, &editor_before.patterns[0]);
    }
    CHECK(ed->undoCount == EDITOR_UNDO, "commit: the ring caps at EDITOR_UNDO");
    int undone = 0;
    while (Editor_Apply(ed, ED_UNDO, 0)) undone++;
    CHECK(undone == EDITOR_UNDO, "commit: every ring entry undoes");
    CHECK(ed->song.patterns[0].tone[TRACK_LEAD][0] == NOTE_MIN + 5, "commit: the oldest six commits are beyond the ring");
}

void test_editor_all(void) {
    editor_test_init();
    editor_test_commit();
    editor_test_cursor();
    editor_test_notes();
    editor_test_knobs();
    editor_test_transpose();
    editor_test_undo();
    editor_test_patterns();
    editor_test_order();
    editor_test_undo_cursor();
    editor_test_noop_edits();
    editor_test_delete_sync();
    editor_test_fuzz();
}
