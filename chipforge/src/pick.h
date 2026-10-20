#ifndef PICK_H
#define PICK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "song.h"
#include "editor.h"
#include "compose.h"

/* The picker: one way of trying generated music on the pattern being edited,
 * shared by Fill (Ctrl+F), Chords (Ctrl+K), Vary (Ctrl+R) and Grow (F9).
 * Opening it snapshots what it will rewrite (the pattern, or for Vary and
 * Grow, which add patterns, the whole song); every choice rewrites the live
 * song from that snapshot (no undo entry), so what plays is always the
 * choice on screen; Keep turns the whole session into ONE undo entry and
 * Cancel puts the snapshot back byte for byte. No raylib: main.c maps keys
 * onto these calls and render.c draws the label, so all of it is tested
 * headless.
 *
 * The live song is only ever written through Editor_ReplacePattern,
 * Pick_ReplaceSong and the Commit calls, so it keeps its address (the
 * player points at it) and a pattern is in place before any order entry
 * points at it. None of these touch the player: the caller re-points a
 * running pattern loop when patternCount changes (main.c's PickWrap) and
 * chooses where to listen from (Pick_Audition). */

typedef enum { PICK_NONE, PICK_FILL, PICK_CHORDS, PICK_VARY, PICK_GROW } PickKind;

/* Room for any label, info or detail line with its terminator. */
#define PICK_TEXT_LEN 128
/* The longest label in glyphs: "Chords  < Sad and racing >  C# harmonic minor?"
 * is 46, and the band fits 51 at T16. */
#define PICK_LABEL_MAX 46

typedef struct {
    PickKind kind;
    int pattern, track;     /* what the picker rewrites: the cursor's when it opened
                             * (Vary and Grow: the source pattern) */
    int orderPos;           /* the cursor's order position when it opened; Vary and
                             * Grow move the cursor and put it back from here */
    int index;              /* Fill: the style. Chords: the named progression, or -1
                             * for chords not in the table (found in the pattern or typed).
                             * Vary: the VaryKind. Grow: the GrowShape */
    uint32_t seed;          /* the take, for the styles that use one (a tune, ghost notes).
                             * Set it before Pick_Begin to choose the first take. */
    bool flag;              /* Chords: fit the tune to the new chords too */
    bool keyGuessed;        /* the key came from Compose_GuessKey, shown with a "?" */
    ComposeKey key;
    ComposeChords chords;   /* Fill: the chords read from `before`. Chords: the ones on trial */
    int tonicMidi;          /* the register `before` sits in (Compose_TonicMidi) */
    SongPattern before;     /* the pattern as it was when the picker opened */
    Song beforeSong;        /* the whole song before Keep: built by Keep for Shift+Enter and
                             * for Chords (which stores the key); Vary and Grow fill it at Begin */
    int added;              /* Vary: the new pattern's index. Grow: how many patterns the
                             * shape on trial added */
    int nextDegree;         /* Vary: the first chord (0..6) of what plays after the new
                             * pattern, which a fill runs into; -1 when it has none */
    float seconds;          /* Grow: how long the song on trial lasts */
} Pick;

/* Opens the picker on the cursor's pattern (and, for Fill, its track): reads
 * the song's key (stored, else guessed) and the pattern's chords from
 * `before`, snapshots, and puts the first choice into the live song at
 * once. Fill starts on the style the track already sounds like (so a beat
 * you have shows its name), New tune for an empty lead, Fit my rhythm for a
 * lead with notes. Chords starts on the pattern's own chords (the table entry
 * when they are one of the named progressions). A pattern with no bass or arp
 * to read chords from uses i VI iv V.
 *
 * Vary adds a new pattern (a variation of the cursor's, starting on Answer)
 * at the end of the song and an order entry for it right after the cursor's
 * position, as Ctrl+D does, and moves the cursor onto it. Grow rebuilds the
 * order into a whole arrangement of the cursor's pattern (Arrange_Grow,
 * starting on Game loop, or the first shape the song has room for) and moves
 * the cursor to that pattern's first entry.
 *
 * Clears the status line. False, with an editor status and nothing changed,
 * when this kind cannot open: Vary with no room for a pattern or an order
 * entry, Grow with no room for any shape or on a pattern with no bass or arp
 * to follow (an arrangement of one chord would be all it could make).
 * Opening while another picker is live puts that one back first. */
bool Pick_Begin(Pick *pk, Editor *ed, PickKind kind);
/* Steps the choice, wrapping both ways, and rewrites the live song from
 * the snapshot. From the off-table chords (index -1), Right gives the first
 * named progression and Left the last. Grow skips the shapes the song has
 * no room for. */
void Pick_Move(Pick *pk, Editor *ed, int delta);
/* A new take: the next seed, same choice. Returns whether what plays
 * changed; when it did not (a style or kind with one take), the status line
 * says so. Chords ignores it and returns false without a status. */
bool Pick_Reroll(Pick *pk, Editor *ed);
/* Chords: moves the key's root by delta semitones (the scale stays) and
 * reads the pattern's chords again in that key, so a guessed key that was
 * off can be put right; the chords on trial keep their degrees. */
void Pick_KeyStep(Pick *pk, Editor *ed, int delta);
/* Chords: toggles fitting the tune (Compose_FitLead) to the chords on trial. */
void Pick_Toggle(Pick *pk, Editor *ed);
/* Chords: tries chords given directly (the typed chord line); index becomes
 * their table entry or -1. False when the picker is not on Chords. */
bool Pick_SetChords(Pick *pk, Editor *ed, const ComposeChords *c);
/* Keeps what is playing as exactly one undo entry (none when nothing ended
 * up different) and closes the picker. allPatterns applies the same choice
 * to every pattern of the song, each on its own chords (Fill) or from its
 * own chords (Chords), still as one entry; Vary and Grow ignore it. Chords
 * also writes the key into the song, in that same entry. The entry
 * remembers the cursor where the picker opened, so Ctrl+Z lands there even
 * though Vary leaves the cursor on the new pattern and Grow on the source's
 * first entry. Sets the status line ("Pattern 03 varies 00: answer", "Grew
 * into 7 patterns, about 1:04"). Returns whether an undo entry was pushed. */
bool Pick_Keep(Pick *pk, Editor *ed, bool allPatterns);
/* Puts the song back exactly as it was (Vary and Grow: the added patterns
 * and order entries go, the order comes back byte for byte) with the cursor
 * where it was, and closes the picker; no undo entry, and dirty is left
 * alone. */
void Pick_Cancel(Pick *pk, Editor *ed);

/* Where to listen from while Vary or Grow is open, for Player_Play along
 * the order: Vary from the last bar of the source (so the change is heard
 * happening), Grow from the source's first entry. False for Fill and Chords,
 * which loop their pattern instead, and when closed. */
bool Pick_Audition(const Pick *pk, const Editor *ed, int *orderPosOut, int *stepOut);

/* Replaces the live song with src, byte for byte, in an order the audio
 * thread can read through: when src has fewer patterns, the order and
 * counts that stop pointing at the extra ones land before those patterns
 * are rewritten (Editor_ReplaceSong alone writes every pattern first, so a
 * player still on an entry for a pattern that is going away would play a
 * step of whatever the slot is reset to). No undo entry, no dirty flag. */
bool Pick_ReplaceSong(Editor *ed, const Song *src);

/* How many choices Left/Right steps through: the track's style count for
 * Fill, the named progressions for Chords, the picker's five kinds for Vary,
 * the four shapes for Grow, 0 when closed. */
int Pick_Count(const Pick *pk);
/* The band's T16 line: "Fill drums  < Breakbeat >", "Chords  < Hero's
 * anthem >  A minor" (a "?" when guessed), "Vary 00 into 03  < Answer >",
 * "Grow  < Game loop, about 1 min >". Off-table chords read as their
 * numbers, "< 1 6 4 5 >". At most PICK_LABEL_MAX glyphs; "" when closed. */
void Pick_Label(const Pick *pk, char *out, size_t n);
/* What the picker works on, for beside the label: "on Am F C G" for Fill
 * and Vary (empty for drums, which follow no chords), "Am F Dm Em" for
 * Chords with ", tune fitted" while F is on, "7 patterns, about 1:04" for
 * Grow. */
void Pick_Info(const Pick *pk, char *out, size_t n);
/* The band's T8 line: the keys that work while the picker is open, short
 * enough for the band at T8. */
void Pick_Detail(const Pick *pk, char *out, size_t n);
/* The same keys as a NULL-terminated list of lines of at most 13 glyphs,
 * for the key-hint card, which otherwise lists keys the picker mutes. */
const char *const *Pick_Keys(const Pick *pk);

#endif
