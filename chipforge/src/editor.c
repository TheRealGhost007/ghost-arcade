/* The tracker's model. Every edit goes through Editor_Apply so that main.c
 * stays a key map, render.c a drawing routine, and the tests can drive the
 * whole thing headless. No raylib here (CORE_SOURCES builds this file). */
#include "editor.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool IsToneTrack(const Editor *ed) { return ed->track >= TRACK_BASS && ed->track < SONG_TONE_TRACKS; }

/* The order position and pattern index must always be valid: the renderer
 * and the player read them without checking. Called after anything that can
 * shrink the song (undo, pattern delete, order remove). */
static void ClampCursor(Editor *ed) {
    Song *s = &ed->song;
    if (s->patternCount < 1) {
        /* A song with no patterns cannot be edited; give it one rather than
         * letting Editor_Pattern hand out garbage. Only reachable through a
         * hand-made Song passed to Editor_SetSong. */
        s->patternCount = 1;
        Song_ClearPattern(&s->patterns[0]);
    }
    if (s->orderCount < 1) {
        s->orderCount = 1;
        s->order[0] = 0;
    }
    s->orderCount = ClampI(s->orderCount, 1, SONG_MAX_ORDER);
    s->patternCount = ClampI(s->patternCount, 1, SONG_MAX_PATTERNS);
    for (int i = 0; i < s->orderCount; i++) {
        if (s->order[i] >= s->patternCount) s->order[i] = (uint8_t)(s->patternCount - 1);
    }
    ed->pattern = ClampI(ed->pattern, 0, s->patternCount - 1);
    ed->orderPos = ClampI(ed->orderPos, 0, s->orderCount - 1);
    ed->step = ClampI(ed->step, 0, SONG_STEPS - 1);
    ed->track = ClampI(ed->track, TRACK_BASS, TRACK_DRUMS);
}

/* ---------------------------------------------------------------- undo */

/* The ring slot the next snapshot goes into. The caller fills its song, then
 * SealUndo records the cursor and advances the ring. Split in two so a commit
 * can store a song other than the live one (the picker's `before`). */
static EditorUndo *UndoSlot(Editor *ed) { return &ed->undo[ed->undoHead]; }

/* Records the cursor in the slot and advances. The ring overwrites its
 * oldest entry when full, so the last EDITOR_UNDO edits are always
 * recoverable. Every edit seals before it moves the cursor, so the snapshot
 * says where the key was pressed. */
static void SealUndo(Editor *ed) {
    EditorUndo *u = UndoSlot(ed);
    u->track = ed->track;
    u->step = ed->step;
    u->pattern = ed->pattern;
    u->orderPos = ed->orderPos;
    ed->undoHead = (ed->undoHead + 1) % EDITOR_UNDO;
    if (ed->undoCount < EDITOR_UNDO) ed->undoCount++;
}

/* Snapshots the whole live song and the cursor. memcpy rather than struct
 * assignment so the restored song is byte for byte the snapshot. */
static void PushUndo(Editor *ed) {
    memcpy(&UndoSlot(ed)->song, &ed->song, sizeof(Song));
    SealUndo(ed);
}

static bool PopUndo(Editor *ed) {
    if (ed->undoCount <= 0) return false;
    ed->undoHead = (ed->undoHead + EDITOR_UNDO - 1) % EDITOR_UNDO;
    ed->undoCount--;
    const EditorUndo *u = &ed->undo[ed->undoHead];
    memcpy(&ed->song, &u->song, sizeof(Song));
    /* Put the cursor back where the edit was made. Clamping alone would, after
     * undoing a pattern or order edit, leave the grid on a pattern the
     * highlighted order slot does not play, and notes typed next would land
     * in the wrong pattern. */
    ed->track = u->track;
    ed->step = u->step;
    ed->pattern = u->pattern;
    ed->orderPos = u->orderPos;
    ClampCursor(ed);
    return true;
}

/* Marks the song changed after an edit that already pushed its snapshot. */
static void Touched(Editor *ed) { ed->dirty = true; }

/* ---------------------------------------------------------------- basics */

void Editor_SetStatus(Editor *ed, const char *text) {
    snprintf(ed->status, sizeof(ed->status), "%s", text ? text : "");
    ed->statusAge = 0.0f;
}

static void StatusF(Editor *ed, const char *fmt, int a) {
    char buf[EDITOR_STATUS_LEN];
    snprintf(buf, sizeof(buf), fmt, a);
    Editor_SetStatus(ed, buf);
}

void Editor_Tick(Editor *ed, float dt) {
    if (dt > 0.0f) ed->statusAge += dt;
}

void Editor_Init(Editor *ed) {
    memset(ed, 0, sizeof(*ed));
    Song_Init(&ed->song, "Untitled");
    ed->track = TRACK_BASS;
    ed->step = 0;
    ed->pattern = 0;
    ed->orderPos = 0;
    ed->octave = 4;
    ed->editStep = 1;
    ed->dirty = false;
    ed->fileName[0] = '\0';
    ed->undoCount = 0;
    ed->undoHead = 0;
    Editor_SetStatus(ed, "New song");
}

void Editor_SetSong(Editor *ed, const Song *song, const char *fileName) {
    memcpy(&ed->song, song, sizeof(Song));
    ed->track = TRACK_BASS;
    ed->step = 0;
    ed->pattern = 0;
    ed->orderPos = 0;
    ed->undoCount = 0;
    ed->undoHead = 0;
    ed->dirty = false;
    snprintf(ed->fileName, sizeof(ed->fileName), "%s", fileName ? fileName : "");
    /* Song_Parse already clamps, but a song built by hand (or an importer bug)
     * may not be; the knob actions assume these ranges. */
    Song *s = &ed->song;
    s->bpm = ClampI(s->bpm, SONG_BPM_MIN, SONG_BPM_MAX);
    if (!(s->swing >= 0.0f)) s->swing = 0.0f; /* also catches NaN */
    if (s->swing > 0.3f) s->swing = 0.3f;
    for (int t = 0; t < SONG_TONE_TRACKS; t++) if (s->wave[t] < 0 || s->wave[t] >= WAVE_COUNT) s->wave[t] = WAVE_TRI;
    for (int t = 0; t < SONG_TRACKS; t++) s->volume[t] = ClampI(s->volume[t], 0, 100);
    ClampCursor(ed);
    /* The order's first entry is what plays first, so start editing there. */
    ed->pattern = ed->song.order[0];
    Editor_SetStatus(ed, "");
}

/* ---------------------------------------------------------------- previews */

static void CopyBytes(Song *dst, const Song *src, size_t from, size_t to) {
    if (to > from) memcpy((char *)dst + from, (const char *)src + from, to - from);
}

/* Overwrites the live song in place while the audio thread may be reading
 * it. The synth bounds every index it uses by patternCount and orderCount,
 * so a count that grows before the cells behind it have landed would play
 * stale cells for a step; a count that shrinks first is harmless. Hence:
 * patterns, then the order, then everything else, then the two counts. The
 * "everything else" pass is by byte range rather than by field so a field
 * song.h grows later (a key, say) is copied wherever it lands, and so that
 * the result is byte for byte src, padding included, which the undo tests
 * and a picker's byte-exact Cancel rely on. */
_Static_assert(offsetof(Song, patternCount) < offsetof(Song, orderCount), "the two-count split below assumes patternCount comes first");
static void CopySongLive(Song *dst, const Song *src) {
    if (dst == src) return;
    memcpy(dst->patterns, src->patterns, sizeof(dst->patterns));
    memcpy(dst->order, src->order, sizeof(dst->order));
    CopyBytes(dst, src, 0, offsetof(Song, patternCount));
    CopyBytes(dst, src, offsetof(Song, patternCount) + sizeof(dst->patternCount), offsetof(Song, orderCount));
    CopyBytes(dst, src, offsetof(Song, orderCount) + sizeof(dst->orderCount), sizeof(Song));
    dst->patternCount = src->patternCount;
    dst->orderCount = src->orderCount;
}

bool Editor_ReplacePattern(Editor *ed, int index, const SongPattern *src) {
    if (!src || index < 0 || index >= SONG_MAX_PATTERNS) return false;
    SongPattern *dst = &ed->song.patterns[index];
    if (dst != src) memcpy(dst, src, sizeof(SongPattern));
    return true;
}

bool Editor_ReplaceSong(Editor *ed, const Song *src) {
    if (!src) return false;
    CopySongLive(&ed->song, src);
    /* Only ever moves the cursor when the new song is too small for it, and
     * repairs a degenerate song exactly as Editor_SetSong would. */
    ClampCursor(ed);
    return true;
}

bool Editor_CommitPattern(Editor *ed, int index, const SongPattern *before) {
    if (!before || index < 0 || index >= SONG_MAX_PATTERNS) return false;
    if (memcmp(&ed->song.patterns[index], before, sizeof(SongPattern)) == 0) return false;
    /* The snapshot is the live song with that one pattern as it was: a
     * pattern preview touches nothing else, so this is the song before the
     * picker opened, built straight into the ring slot. */
    Song *snap = &UndoSlot(ed)->song;
    memcpy(snap, &ed->song, sizeof(Song));
    memcpy(&snap->patterns[index], before, sizeof(SongPattern));
    SealUndo(ed);
    Touched(ed);
    ClampCursor(ed);
    return true;
}

bool Editor_CommitSong(Editor *ed, const Song *before) {
    if (!before) return false;
    if (memcmp(&ed->song, before, sizeof(Song)) == 0) return false;
    memcpy(&UndoSlot(ed)->song, before, sizeof(Song));
    SealUndo(ed);
    Touched(ed);
    ClampCursor(ed);
    return true;
}

SongPattern *Editor_Pattern(Editor *ed) {
    ClampCursor(ed);
    return &ed->song.patterns[ed->pattern];
}

void Editor_CellText(const Editor *ed, int track, int step, char out[4]) {
    if (step < 0 || step >= SONG_STEPS || ed->pattern < 0 || ed->pattern >= ed->song.patternCount) {
        memcpy(out, "---", 4);
        return;
    }
    const SongPattern *p = &ed->song.patterns[ed->pattern];
    if (track >= TRACK_BASS && track < SONG_TONE_TRACKS) {
        Song_NoteName(p->tone[track][step], out);
    } else if (track == TRACK_DRUMS) {
        uint8_t d = p->drums[step];
        out[0] = (d & DRUM_KICK) ? 'K' : '.';
        out[1] = (d & DRUM_SNARE) ? 'S' : '.';
        out[2] = (d & DRUM_HAT) ? 'H' : '.';
        out[3] = '\0';
    } else {
        memcpy(out, "---", 4);
    }
}

int Editor_NoteForKey(const Editor *ed, int semitoneOffset) {
    /* Song_NoteName calls MIDI 60 "C4", so octave 4 + offset 0 must be 60. */
    return ClampI((ed->octave + 1) * 12 + semitoneOffset, NOTE_MIN, NOTE_MAX);
}

/* ---------------------------------------------------------------- edits */

static void Advance(Editor *ed) {
    ed->step = (ed->step + ed->editStep) % SONG_STEPS;
}

/* Writes one tone cell. Re-entering what is already there is not an edit: the
 * cursor still moves so typing keeps its rhythm, but nothing is pushed or
 * dirtied and the caller is told the song did not change. */
static bool WriteTone(Editor *ed, int8_t value, bool advance) {
    SongPattern *p = Editor_Pattern(ed);
    if (p->tone[ed->track][ed->step] == value) {
        if (advance) Advance(ed);
        return false;
    }
    PushUndo(ed);
    p->tone[ed->track][ed->step] = value;
    Touched(ed);
    if (advance) Advance(ed);
    return true;
}

static bool ApplyNote(Editor *ed, int arg) {
    SongPattern *p = Editor_Pattern(ed);
    if (ed->track == TRACK_DRUMS) {
        int bit = arg & (DRUM_KICK | DRUM_SNARE | DRUM_HAT);
        if (bit == 0) return false; /* not a drum: nothing sensible to toggle */
        PushUndo(ed);
        p->drums[ed->step] ^= (uint8_t)bit;
        Touched(ed);
        char cell[4];
        Editor_CellText(ed, TRACK_DRUMS, ed->step, cell);
        Editor_SetStatus(ed, cell);
        return true;
    }
    int note = ClampI(arg, NOTE_MIN, NOTE_MAX);
    char name[4];
    Song_NoteName(note, name);
    Editor_SetStatus(ed, name);
    return WriteTone(ed, (int8_t)note, true);
}

static bool ApplyClear(Editor *ed, bool back) {
    int step = ed->step;
    if (back) {
        if (step <= 0) return false; /* nothing above the first step to clear */
        step--;
    }
    SongPattern *p = Editor_Pattern(ed);
    bool empty = ed->track == TRACK_DRUMS ? p->drums[step] == 0 : p->tone[ed->track][step] == NOTE_NONE;
    if (empty) {
        /* Nothing to clear. The cursor still walks so Delete and a held
         * Backspace keep moving over blank rows, but Backspace auto-repeats
         * and must not fill the undo ring with snapshots that change nothing,
         * pushing real edits out; nor may it dirty a freshly opened song. */
        ed->step = step;
        if (!back) Advance(ed);
        return false;
    }
    PushUndo(ed); /* before the cursor moves, so undo lands where the key was pressed */
    ed->step = step;
    if (ed->track == TRACK_DRUMS) p->drums[step] = 0;
    else p->tone[ed->track][step] = NOTE_NONE;
    Touched(ed);
    if (!back) Advance(ed);
    return true;
}

static bool ApplyTranspose(Editor *ed, int semitones) {
    if (!IsToneTrack(ed) || semitones == 0) return false;
    SongPattern *p = Editor_Pattern(ed);
    int8_t *row = p->tone[ed->track];
    bool any = false, moves = false;
    for (int i = 0; i < SONG_STEPS && !moves; i++) {
        if (row[i] < 0) continue; /* holds and silence have no pitch */
        any = true;
        if (ClampI(row[i] + semitones, NOTE_MIN, NOTE_MAX) != row[i]) moves = true;
    }
    if (!any) {
        Editor_SetStatus(ed, "No notes to transpose");
        return false;
    }
    if (!moves) {
        /* Every note is already pinned at the limit, so the clamp below would
         * change nothing: no snapshot, no dirty flag. */
        Editor_SetStatus(ed, "Notes already at the limit");
        return false;
    }
    PushUndo(ed);
    for (int i = 0; i < SONG_STEPS; i++) {
        if (row[i] < 0) continue; /* holds and silence have no pitch */
        row[i] = (int8_t)ClampI(row[i] + semitones, NOTE_MIN, NOTE_MAX);
    }
    Touched(ed);
    StatusF(ed, semitones > 0 ? "Transposed up %d" : "Transposed down %d", semitones > 0 ? semitones : -semitones);
    return true;
}

static bool ApplyPatternNew(Editor *ed, bool duplicate) {
    Song *s = &ed->song;
    if (s->patternCount >= SONG_MAX_PATTERNS) {
        Editor_SetStatus(ed, "The song is full");
        return false;
    }
    PushUndo(ed);
    int idx = 0;
    bool ok = duplicate ? Song_DuplicatePattern(s, ed->pattern, &idx) : Song_AddPattern(s, &idx);
    if (!ok) {
        /* Cannot happen after the check above, but never leave a stray snapshot. */
        PopUndo(ed);
        Editor_SetStatus(ed, "The song is full");
        return false;
    }
    /* The order may be full; the pattern still exists, it just is not placed. */
    if (Song_OrderInsert(s, ed->orderPos + 1, idx)) ed->orderPos++;
    ed->pattern = idx;
    Touched(ed);
    StatusF(ed, duplicate ? "Pattern %02d duplicated" : "Pattern %02d added", idx);
    return true;
}

static bool ApplyPatternDelete(Editor *ed) {
    Song *s = &ed->song;
    if (s->patternCount <= 1) {
        Editor_SetStatus(ed, "The last pattern stays");
        return false;
    }
    PushUndo(ed);
    int removed = ed->pattern;
    if (!Song_RemovePattern(s, removed)) {
        PopUndo(ed);
        Editor_SetStatus(ed, "The last pattern stays");
        return false;
    }
    /* Song_RemovePattern has repointed the order (slots that played the
     * deleted pattern fall back to the previous one, later indexes shift
     * down), so the index under the cursor now names a different pattern.
     * Edit what the current slot plays: that is what the order list
     * highlights and what Space plays from here. */
    ed->orderPos = ClampI(ed->orderPos, 0, s->orderCount - 1);
    ed->pattern = s->order[ed->orderPos];
    Touched(ed);
    StatusF(ed, "Pattern %02d deleted", removed);
    return true;
}

static bool ApplyClearTrack(Editor *ed) {
    SongPattern *p = Editor_Pattern(ed);
    /* An empty track (a lone hold counts as content) has nothing to clear:
     * no snapshot, no dirty flag, and the caller learns nothing changed. */
    bool empty = true;
    for (int i = 0; i < SONG_STEPS && empty; i++) {
        if (ed->track == TRACK_DRUMS ? p->drums[i] != 0 : p->tone[ed->track][i] != NOTE_NONE) empty = false;
    }
    if (empty) {
        Editor_SetStatus(ed, "Track already empty");
        return false;
    }
    PushUndo(ed);
    if (ed->track == TRACK_DRUMS) memset(p->drums, 0, sizeof(p->drums));
    else memset(p->tone[ed->track], NOTE_NONE, sizeof(p->tone[ed->track]));
    Touched(ed);
    Editor_SetStatus(ed, "Track cleared");
    return true;
}

static bool ApplyOrderSet(Editor *ed, int delta) {
    Song *s = &ed->song;
    ClampCursor(ed);
    int cur = s->order[ed->orderPos];
    int next = (cur + delta + s->patternCount) % s->patternCount;
    if (next == cur) return false; /* a one-pattern song has nowhere to go */
    PushUndo(ed);
    Song_OrderSet(s, ed->orderPos, next);
    ed->pattern = next;
    Touched(ed);
    StatusF(ed, "Position plays pattern %02d", next);
    return true;
}

static bool ApplyOrderInsert(Editor *ed) {
    Song *s = &ed->song;
    ClampCursor(ed);
    if (s->orderCount >= SONG_MAX_ORDER) {
        Editor_SetStatus(ed, "The order is full");
        return false;
    }
    PushUndo(ed);
    if (!Song_OrderInsert(s, ed->orderPos + 1, ed->pattern)) {
        PopUndo(ed);
        Editor_SetStatus(ed, "The order is full");
        return false;
    }
    ed->orderPos++;
    Touched(ed);
    StatusF(ed, "Pattern %02d repeated", ed->pattern);
    return true;
}

static bool ApplyOrderRemove(Editor *ed) {
    Song *s = &ed->song;
    ClampCursor(ed);
    if (s->orderCount <= 1) {
        Editor_SetStatus(ed, "The order needs one entry");
        return false;
    }
    PushUndo(ed);
    if (!Song_OrderRemove(s, ed->orderPos)) {
        PopUndo(ed);
        Editor_SetStatus(ed, "The order needs one entry");
        return false;
    }
    ed->orderPos = ClampI(ed->orderPos, 0, s->orderCount - 1);
    ed->pattern = s->order[ed->orderPos];
    Touched(ed);
    Editor_SetStatus(ed, "Order entry removed");
    return true;
}

/* Song-level knobs: each pushes a snapshot only when the value really moves,
 * so leaning on a key at the limit does not flood the undo ring. */
static bool ApplyBpm(Editor *ed, int delta) {
    int next = ClampI(ed->song.bpm + delta, SONG_BPM_MIN, SONG_BPM_MAX);
    if (next == ed->song.bpm) {
        StatusF(ed, "BPM %d", next);
        return false;
    }
    PushUndo(ed);
    ed->song.bpm = next;
    Touched(ed);
    StatusF(ed, "BPM %d", next);
    return true;
}

static bool ApplySwing(Editor *ed, int direction) {
    /* Work in hundredths so repeated steps land on clean 0.02 multiples. */
    int cur = (int)lroundf(ed->song.swing * 100.0f);
    int next = ClampI(cur + 2 * direction, 0, 30);
    char buf[EDITOR_STATUS_LEN];
    snprintf(buf, sizeof(buf), "Swing %.2f", next / 100.0);
    Editor_SetStatus(ed, buf);
    if (next == cur) return false;
    PushUndo(ed);
    ed->song.swing = (float)next / 100.0f;
    Touched(ed);
    return true;
}

static bool ApplyWave(Editor *ed, int direction) {
    if (!IsToneTrack(ed)) {
        Editor_SetStatus(ed, "Drums have no wave");
        return false;
    }
    PushUndo(ed);
    int w = ((int)ed->song.wave[ed->track] + direction + WAVE_COUNT) % WAVE_COUNT;
    ed->song.wave[ed->track] = (SongWave)w;
    Touched(ed);
    char buf[EDITOR_STATUS_LEN];
    snprintf(buf, sizeof(buf), "Wave %s", Song_WaveName((SongWave)w));
    Editor_SetStatus(ed, buf);
    return true;
}

static bool ApplyVolume(Editor *ed, int delta) {
    int cur = ed->song.volume[ed->track];
    int next = ClampI(cur + delta, 0, 100);
    StatusF(ed, "Volume %d", next);
    if (next == cur) return false;
    PushUndo(ed);
    ed->song.volume[ed->track] = next;
    Touched(ed);
    return true;
}

bool Editor_Apply(Editor *ed, EditorAction a, int arg) {
    ClampCursor(ed);
    Song *s = &ed->song;
    switch (a) {
        /* cursor */
        case ED_UP: ed->step = (ed->step + SONG_STEPS - 1) % SONG_STEPS; return false;
        case ED_DOWN: ed->step = (ed->step + 1) % SONG_STEPS; return false;
        case ED_LEFT: ed->track = ClampI(ed->track - 1, TRACK_BASS, TRACK_DRUMS); return false;
        case ED_RIGHT: ed->track = ClampI(ed->track + 1, TRACK_BASS, TRACK_DRUMS); return false;
        case ED_PAGE_UP: ed->step = ClampI(ed->step - 16, 0, SONG_STEPS - 1); return false;
        case ED_PAGE_DOWN: ed->step = ClampI(ed->step + 16, 0, SONG_STEPS - 1); return false;
        case ED_HOME: ed->step = 0; return false;
        case ED_END: ed->step = SONG_STEPS - 1; return false;

        /* cells */
        case ED_NOTE: return ApplyNote(ed, arg);
        case ED_HOLD:
            if (!IsToneTrack(ed)) return false; /* drums cannot ring on */
            return WriteTone(ed, NOTE_HOLD, true);
        case ED_CLEAR: return ApplyClear(ed, false);
        case ED_CLEAR_BACK: return ApplyClear(ed, true);

        /* entry settings (not part of the song, so no undo and not dirty) */
        case ED_OCTAVE_UP: ed->octave = ClampI(ed->octave + 1, 1, 7); StatusF(ed, "Octave %d", ed->octave); return false;
        case ED_OCTAVE_DOWN: ed->octave = ClampI(ed->octave - 1, 1, 7); StatusF(ed, "Octave %d", ed->octave); return false;
        case ED_EDIT_STEP_UP: ed->editStep = ClampI(ed->editStep + 1, 0, 16); StatusF(ed, "Edit step %d", ed->editStep); return false;
        case ED_EDIT_STEP_DOWN: ed->editStep = ClampI(ed->editStep - 1, 0, 16); StatusF(ed, "Edit step %d", ed->editStep); return false;

        /* song knobs */
        case ED_BPM_UP: return ApplyBpm(ed, arg > 0 ? arg : 1);
        case ED_BPM_DOWN: return ApplyBpm(ed, -(arg > 0 ? arg : 1));
        case ED_SWING_UP: return ApplySwing(ed, 1);
        case ED_SWING_DOWN: return ApplySwing(ed, -1);
        case ED_WAVE_NEXT: return ApplyWave(ed, 1);
        case ED_WAVE_PREV: return ApplyWave(ed, -1);
        case ED_VOLUME_UP: return ApplyVolume(ed, 10);
        case ED_VOLUME_DOWN: return ApplyVolume(ed, -10);
        case ED_TRANSPOSE_UP: return ApplyTranspose(ed, arg > 0 ? arg : 1);
        case ED_TRANSPOSE_DOWN: return ApplyTranspose(ed, -(arg > 0 ? arg : 1));

        /* patterns */
        case ED_PATTERN_NEXT:
            ed->pattern = (ed->pattern + 1) % s->patternCount;
            StatusF(ed, "Pattern %02d", ed->pattern);
            return false;
        case ED_PATTERN_PREV:
            ed->pattern = (ed->pattern + s->patternCount - 1) % s->patternCount;
            StatusF(ed, "Pattern %02d", ed->pattern);
            return false;
        case ED_PATTERN_NEW: return ApplyPatternNew(ed, false);
        case ED_PATTERN_DUPLICATE: return ApplyPatternNew(ed, true);
        case ED_PATTERN_DELETE: return ApplyPatternDelete(ed);
        case ED_PATTERN_CLEAR_TRACK: return ApplyClearTrack(ed);

        /* order */
        case ED_ORDER_NEXT:
            ed->orderPos = ClampI(ed->orderPos + 1, 0, s->orderCount - 1);
            ed->pattern = s->order[ed->orderPos];
            StatusF(ed, "Position %02d", ed->orderPos);
            return false;
        case ED_ORDER_PREV:
            ed->orderPos = ClampI(ed->orderPos - 1, 0, s->orderCount - 1);
            ed->pattern = s->order[ed->orderPos];
            StatusF(ed, "Position %02d", ed->orderPos);
            return false;
        case ED_ORDER_SET_UP: return ApplyOrderSet(ed, 1);
        case ED_ORDER_SET_DOWN: return ApplyOrderSet(ed, -1);
        case ED_ORDER_INSERT: return ApplyOrderInsert(ed);
        case ED_ORDER_REMOVE: return ApplyOrderRemove(ed);

        case ED_UNDO:
            if (PopUndo(ed)) {
                ed->dirty = true; /* the song moved away from what is on disk again */
                Editor_SetStatus(ed, "Undone");
                return true;
            }
            Editor_SetStatus(ed, "Nothing to undo");
            return false;

        case ED_ACTION_COUNT:
        default:
            return false;
    }
}
