#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include "song.h"

/* The tracker's model: a song, a cursor and the edits you can make. No raylib,
 * so every edit is testable; main.c maps keys to EditorActions and render.c
 * draws the state. The song being edited may be playing at the same time (the
 * player holds a pointer to ed->song), so edits never move it in memory. */

#define EDITOR_UNDO 24
#define EDITOR_STATUS_LEN 72

typedef enum {
    ED_UP, ED_DOWN, ED_LEFT, ED_RIGHT,          /* cursor; up/down wrap within the pattern */
    ED_PAGE_UP, ED_PAGE_DOWN,                   /* a bar (16 steps) */
    ED_HOME, ED_END,                            /* step 0 / step 63 */
    ED_NOTE,                                    /* arg = MIDI note; on the drums track arg is a DRUM_ bit to toggle */
    ED_HOLD,                                    /* tone tracks: NOTE_HOLD at the cursor */
    ED_CLEAR,                                   /* clear the cell at the cursor */
    ED_CLEAR_BACK,                              /* move up one and clear (backspace) */
    ED_OCTAVE_UP, ED_OCTAVE_DOWN,
    ED_EDIT_STEP_UP, ED_EDIT_STEP_DOWN,         /* how far the cursor advances after entering a note: 0..16 */
    ED_BPM_UP, ED_BPM_DOWN,                     /* arg = amount (1 or 10) */
    ED_SWING_UP, ED_SWING_DOWN,                 /* 0.02 a step */
    ED_WAVE_NEXT, ED_WAVE_PREV,                 /* current tone track */
    ED_VOLUME_UP, ED_VOLUME_DOWN,               /* current track, 10 a step */
    ED_TRANSPOSE_UP, ED_TRANSPOSE_DOWN,         /* current tone track in this pattern, arg semitones (1 or 12) */
    ED_PATTERN_NEXT, ED_PATTERN_PREV,           /* edit another pattern (does not touch the order) */
    ED_PATTERN_NEW,                             /* empty pattern appended to the song and to the order after the current position */
    ED_PATTERN_DUPLICATE,                       /* copy of the current pattern, likewise */
    ED_PATTERN_DELETE,                          /* refuses the last one */
    ED_PATTERN_CLEAR_TRACK,                     /* current track in this pattern */
    ED_ORDER_NEXT, ED_ORDER_PREV,               /* move along the order; the edited pattern follows */
    ED_ORDER_SET_UP, ED_ORDER_SET_DOWN,         /* change which pattern this order position plays */
    ED_ORDER_INSERT,                            /* repeat the current pattern after this position */
    ED_ORDER_REMOVE,                            /* refuses the last entry */
    ED_UNDO,
    ED_ACTION_COUNT
} EditorAction;

/* One undo step: the song as it was before an edit, plus where the cursor was
 * when the edit was made, so Ctrl+Z puts the user back on the pattern, order
 * position and step the edit came from rather than wherever it left them. */
typedef struct {
    Song song;
    int track, step, pattern, orderPos;
} EditorUndo;

typedef struct {
    Song song;
    int track;          /* TRACK_BASS..TRACK_DRUMS */
    int step;           /* 0..SONG_STEPS-1 */
    int pattern;        /* the pattern being edited */
    int orderPos;       /* position in the order whose pattern is being edited (kept in sync where possible) */
    int octave;         /* 1..7; piano keys enter octave*12+offset, default 4 */
    int editStep;       /* 0..16, default 1 */
    bool dirty;         /* unsaved changes */
    char fileName[SONG_FILE_LEN]; /* library file it was loaded from / saved to, "" if never saved */
    char status[EDITOR_STATUS_LEN]; /* one line for the renderer: "Pattern 3 added", "Nothing to undo" */
    float statusAge;    /* seconds since status was set */

    EditorUndo undo[EDITOR_UNDO];
    int undoCount, undoHead;
} Editor;

void Editor_Init(Editor *ed);
/* Replaces the song (and resets the cursor and undo history). */
void Editor_SetSong(Editor *ed, const Song *song, const char *fileName);

/* Preview writes for the generators (Fill, Chords, Grow, the recipe screen):
 * a picker snapshots what it is about to rewrite, Replaces the live song as
 * many times as the user tries styles, then either Commits once or Replaces
 * the snapshot back. The live song keeps its address (the player points at
 * it) and its cells are written before its counts, so the audio thread never
 * plays a pattern whose data has not landed yet.
 *
 * Replace: no undo entry, no dirty flag, the cursor is left where it is (only
 * clamped when the new song is too small for it). index is 0..SONG_MAX_PATTERNS-1
 * and may lie beyond patternCount, so a slot can be filled before a count
 * grows. Returns false and changes nothing for a NULL src or a bad index.
 *
 * Commit: pushes exactly ONE undo entry holding `before` (the pattern as it
 * was when the picker opened, or the whole song for generators that add
 * patterns), so a whole preview session undoes in one Ctrl+Z; marks the song
 * dirty and clamps the cursor. The entry remembers the cursor as it stands at
 * the commit, which is where the picker opened as long as the caller did not
 * move it. Returns false and pushes nothing when the song already equals
 * `before` (a Keep that changed nothing is not an edit) or the arguments are
 * bad. Callers set their own status line. */
bool Editor_ReplacePattern(Editor *ed, int index, const SongPattern *src);
bool Editor_ReplaceSong(Editor *ed, const Song *src);
bool Editor_CommitPattern(Editor *ed, int index, const SongPattern *before);
bool Editor_CommitSong(Editor *ed, const Song *before);
/* Applies one action; arg is used by the actions that say so. Returns true
 * if the song changed. */
bool Editor_Apply(Editor *ed, EditorAction a, int arg);
/* The current pattern (never NULL). */
SongPattern *Editor_Pattern(Editor *ed);
/* The cell under the cursor as text: a note name, or "KSH"-style drums. */
void Editor_CellText(const Editor *ed, int track, int step, char out[4]);
/* Note a piano key would enter right now (octave applied, clamped). */
int Editor_NoteForKey(const Editor *ed, int semitoneOffset);
void Editor_SetStatus(Editor *ed, const char *text);
void Editor_Tick(Editor *ed, float dt);

#endif
