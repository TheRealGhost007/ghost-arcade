/* The picker behind Fill (Ctrl+F), Chords (Ctrl+K), Vary (Ctrl+R) and Grow
 * (F9). Every choice is built from the snapshot taken when the picker
 * opened, never from the live song: a writer replaces its whole track,
 * Chords re-reads the chords and Grow rewrites the whole order, so building
 * on the previous choice would drift (and Esc could not come back). No
 * raylib here (CORE_SOURCES builds this file). */
#include "pick.h"
#include "arrange.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

/* What a pattern with no bass or arp to read chords from is given: i VI iv V,
 * the launcher theme's progression, so an empty pattern gets the arcade's own
 * sound rather than four bars of one chord. */
static const ComposeChords kDefaultChords = {{0, 5, 3, 4}};

static const char *const kTrackWords[SONG_TRACKS] = {"bass", "arp", "lead", "drums"};
static const char *const kTrackTitles[SONG_TRACKS] = {"Bass", "Arp", "Lead", "Drums"};

/* Whole songs being built before they replace the live one. A Song is too
 * big to want two or three of on the stack at once, and the picker is only
 * ever driven from the one UI thread, so file statics are safe. Each has
 * one owner: sWork is a song under construction (Vary's first take, a Grow
 * shape), sSwap is Pick_ReplaceSong's own. */
static Song sWork, sSwap;

static int TrackOf(int track) { return track < TRACK_BASS ? TRACK_BASS : (track > TRACK_DRUMS ? TRACK_DRUMS : track); }

static bool TrackEmpty(const SongPattern *p, int track) {
    for (int st = 0; st < SONG_STEPS; st++) {
        if (track == TRACK_DRUMS ? p->drums[st] != 0 : p->tone[track][st] >= 0) return false;
    }
    return true;
}

/* The chords a pattern is on, read from its bass, then its arp, bar by bar. */
static ComposeChords ChordsOf(const SongPattern *p, ComposeKey k) {
    ComposeChords c;
    if (!Compose_ReadChords(p, k, &c)) c = kDefaultChords;
    return c;
}

/* Fill: src with one track rewritten in a style, on src's own chords and in
 * src's own register, so a fill fits the pattern it lands in. Everything is
 * read from src before the writer runs: rewriting the bass first would
 * change what the chords and the register read as. */
static void BuildFill(const SongPattern *src, ComposeKey k, int track, int style, uint32_t seed, SongPattern *out) {
    memcpy(out, src, sizeof(*out));
    if (track == TRACK_DRUMS) {
        Compose_Drums(out, style, seed);
        return;
    }
    ComposeChords c = ChordsOf(src, k);
    int tonic = Compose_TonicMidi(k, src);
    if (track == TRACK_BASS) {
        Compose_Bass(out, k, tonic, &c, style);
    } else if (track == TRACK_ARP) {
        Compose_Arp(out, k, tonic, &c, style);
    } else {
        /* A new tune keeps the register of the one it replaces. */
        int octave = Compose_LeadOctave(k, tonic, src);
        if (style == 0) Compose_Lead(out, k, tonic, &c, octave, seed);
        else Compose_LeadPitches(out, k, tonic, &c, octave, seed);
    }
}

/* Chords: src moved from the chords it is on, read in k, to `to`. Reading in
 * k (not the song's stored key) is what lets Up/Down re-interpret a pattern
 * whose key was guessed wrong. */
static void BuildChords(const SongPattern *src, ComposeKey k, const ComposeChords *to, bool fitLead, SongPattern *out) {
    memcpy(out, src, sizeof(*out));
    ComposeChords from;
    Compose_ReadChords(src, k, &from);
    Compose_Reharmonise(out, k, &from, to, fitLead);
}

/* Vary: the source with the kind on trial applied. A fill (and only a
 * fill) runs into the chord of whatever plays after the new pattern, which
 * Begin read before anything moved. */
static void BuildVary(const Pick *pk, SongPattern *out) {
    Arrange_VaryInto(&pk->before, pk->key, pk->tonicMidi, &pk->chords, (VaryKind)pk->index, pk->seed, pk->nextDegree, out);
}

/* Whether the song has room for a Grow shape: Arrange_Grow refuses rather
 * than overflow, and the picker never offers a shape it would refuse. */
static bool GrowFits(const Song *s, int shape) {
    int added = Arrange_PatternsNeeded((GrowShape)shape);
    return added > 0 && s->patternCount + added <= SONG_MAX_PATTERNS && Arrange_PassCount((GrowShape)shape) <= SONG_MAX_ORDER;
}

/* The first order position that plays a pattern (0 when none does). */
static int FirstEntry(const Song *s, int pattern) {
    for (int i = 0; i < s->orderCount && i < SONG_MAX_ORDER; i++) if (s->order[i] == pattern) return i;
    return 0;
}

bool Pick_ReplaceSong(Editor *ed, const Song *src) {
    if (!ed || !src) return false;
    int live = ed->song.patternCount, want = src->patternCount;
    if (want < 1 || want >= live || live > SONG_MAX_PATTERNS) return Editor_ReplaceSong(ed, src);
    /* Two passes when patterns go away. First src with the doomed slots still
     * holding what they hold now: Editor_ReplaceSong lands every pattern,
     * then the order, then the counts, so once it returns nothing points at
     * those slots. Only then are they set to src's bytes, which makes the
     * end result src byte for byte (a byte-exact Esc depends on that). */
    memcpy(&sSwap, src, sizeof(Song));
    for (int i = want; i < live; i++) memcpy(&sSwap.patterns[i], &ed->song.patterns[i], sizeof(SongPattern));
    Editor_ReplaceSong(ed, &sSwap);
    for (int i = want; i < live; i++) Editor_ReplacePattern(ed, i, &src->patterns[i]);
    return true;
}

/* Grow: the snapshot arranged in the shape on trial, put live, with the
 * cursor on the source's first entry so the order column and the grid agree
 * on what is being edited. */
static void PreviewGrow(Pick *pk, Editor *ed) {
    memcpy(&sWork, &pk->beforeSong, sizeof(Song));
    /* Begin and Move only ever choose shapes with room, so this holds; if it
     * did not, the song on trial simply stays as it is. */
    if (!Arrange_Grow(&sWork, pk->pattern, pk->key, (GrowShape)pk->index, pk->seed)) return;
    pk->added = sWork.patternCount - pk->beforeSong.patternCount;
    pk->seconds = Song_Seconds(&sWork);
    Pick_ReplaceSong(ed, &sWork);
    ed->pattern = pk->pattern;
    ed->orderPos = FirstEntry(&ed->song, pk->pattern);
}

/* Puts the current choice into the live song. */
static void Preview(Pick *pk, Editor *ed) {
    SongPattern out;
    switch (pk->kind) {
        case PICK_FILL: BuildFill(&pk->before, pk->key, pk->track, pk->index, pk->seed, &out); break;
        case PICK_CHORDS: BuildChords(&pk->before, pk->key, &pk->chords, pk->flag, &out); break;
        case PICK_VARY:
            /* The new pattern's slot and order entry exist from Begin on, so
             * only its cells change from here. */
            BuildVary(pk, &out);
            Editor_ReplacePattern(ed, pk->added, &out);
            return;
        case PICK_GROW: PreviewGrow(pk, ed); return;
        default: return;
    }
    Editor_ReplacePattern(ed, pk->pattern, &out);
}

/* Closes without a status line, for Begin over a live picker. */
static void Restore(Pick *pk, Editor *ed) {
    if (pk->kind == PICK_FILL || pk->kind == PICK_CHORDS) {
        Editor_ReplacePattern(ed, pk->pattern, &pk->before);
    } else if (pk->kind == PICK_VARY || pk->kind == PICK_GROW) {
        Pick_ReplaceSong(ed, &pk->beforeSong);
        ed->pattern = pk->pattern;
        ed->orderPos = pk->orderPos;
        Editor_Pattern(ed); /* clamps, should the snapshot ever disagree */
    }
    pk->kind = PICK_NONE;
}

/* Vary's opening: room checked, the snapshot taken, the chord after the
 * new pattern read, then the song with the new pattern and its order entry
 * put live in one replace. False (with a status) leaves everything alone. */
static bool BeginVary(Pick *pk, Editor *ed) {
    const Song *s = &ed->song;
    if (s->patternCount >= SONG_MAX_PATTERNS) {
        Editor_SetStatus(ed, "The song is full: no room for a variation");
        return false;
    }
    if (s->orderCount >= SONG_MAX_ORDER) {
        Editor_SetStatus(ed, "The order is full: no room for a variation");
        return false;
    }
    memcpy(&pk->beforeSong, s, sizeof(Song));
    pk->added = s->patternCount;
    /* What follows the new entry is what followed the cursor's position
     * (wrapping to the top, as the order does), read before anything moves. */
    int next = s->order[(pk->orderPos + 1) % s->orderCount];
    if (next < 0 || next >= s->patternCount) next = 0;
    ComposeChords nc;
    pk->nextDegree = Compose_ReadChords(&s->patterns[next], pk->key, &nc) ? Mod(nc.degree[0], 7) : -1;
    /* Answer first: the kind a second pattern most often is, and the one
     * that makes a loop sound like a phrase with an end. */
    pk->index = VARY_ANSWER;

    memcpy(&sWork, &pk->beforeSong, sizeof(Song));
    BuildVary(pk, &sWork.patterns[pk->added]);
    sWork.patternCount++;
    Song_OrderInsert(&sWork, pk->orderPos + 1, pk->added); /* room checked above */
    Pick_ReplaceSong(ed, &sWork);
    ed->pattern = pk->added;
    ed->orderPos = pk->orderPos + 1;
    return true;
}

/* Grow's opening: a pattern to follow and room for a shape, else a status
 * and nothing changed. Starts on Game loop, the shape a game wants. */
static bool BeginGrow(Pick *pk, Editor *ed, bool chordsFound) {
    /* A pattern with no bass or arp reads as one chord throughout, and an
     * arrangement of that is a minute of one chord: better to say what
     * would help. */
    if (!chordsFound) {
        Editor_SetStatus(ed, "Nothing to grow from: give it a bass or arp first (Ctrl+K)");
        return false;
    }
    int shape = -1;
    if (GrowFits(&ed->song, GROW_GAME_LOOP)) shape = GROW_GAME_LOOP;
    for (int g = 0; shape < 0 && g < GROW_COUNT; g++) if (GrowFits(&ed->song, g)) shape = g;
    if (shape < 0) {
        char msg[EDITOR_STATUS_LEN];
        snprintf(msg, sizeof(msg), "The song is full: Grow needs %d free patterns", Arrange_PatternsNeeded(GROW_SHORT));
        Editor_SetStatus(ed, msg);
        return false;
    }
    memcpy(&pk->beforeSong, &ed->song, sizeof(Song));
    pk->index = shape;
    pk->nextDegree = -1;
    return true;
}

bool Pick_Begin(Pick *pk, Editor *ed, PickKind kind) {
    if (!pk || !ed) return false;
    if (pk->kind != PICK_NONE) Restore(pk, ed);
    if (kind != PICK_FILL && kind != PICK_CHORDS && kind != PICK_VARY && kind != PICK_GROW) return false;
    Editor_Pattern(ed); /* clamps the cursor, so pattern and track are valid indices */
    uint32_t seed = pk->seed;
    memset(pk, 0, sizeof(*pk));
    pk->seed = seed;
    pk->pattern = ed->pattern;
    pk->orderPos = ed->orderPos;
    pk->track = TrackOf(ed->track);
    memcpy(&pk->before, &ed->song.patterns[pk->pattern], sizeof(SongPattern));
    pk->key = Compose_SongKey(&ed->song, &pk->keyGuessed);
    pk->tonicMidi = Compose_TonicMidi(pk->key, &pk->before);
    bool found = Compose_ReadChords(&pk->before, pk->key, &pk->chords);
    if (!found) pk->chords = kDefaultChords;

    if (kind == PICK_FILL) {
        /* Start on what is most likely wanted: the style the track already
         * sounds like (so the band names the beat you have and Left/Right
         * move away from it), a new tune for an empty lead, and the user's
         * own rhythm for a lead that has notes. */
        if (pk->track == TRACK_LEAD) pk->index = TrackEmpty(&pk->before, TRACK_LEAD) ? 0 : 1;
        else pk->index = Compose_GuessStyle(&pk->before, pk->key, (SongTrack)pk->track);
    } else if (kind == PICK_CHORDS) {
        pk->index = Compose_ProgressionFind(&pk->chords);
        if (pk->index < 0 && !found) {
            pk->index = 0;
            pk->chords = Compose_ProgressionAt(0)->chords;
        }
    } else if (kind == PICK_VARY) {
        if (!BeginVary(pk, ed)) return false; /* pk is zeroed, so it stays closed */
    } else {
        if (!BeginGrow(pk, ed, found)) return false;
    }
    pk->kind = kind;
    /* Vary put its first take live while opening (the new slot and order
     * entry had to land together); the others get theirs here. */
    if (kind != PICK_VARY) Preview(pk, ed);
    /* The band shows a fresh status beside its label; one left over from
     * before the picker ("Opened ...") would read as the picker's own. */
    Editor_SetStatus(ed, "");
    return true;
}

int Pick_Count(const Pick *pk) {
    if (!pk) return 0;
    if (pk->kind == PICK_FILL) return Compose_StyleCount((SongTrack)pk->track);
    if (pk->kind == PICK_CHORDS) return Compose_ProgressionCount();
    if (pk->kind == PICK_VARY) return ARRANGE_PICKER_KINDS;
    if (pk->kind == PICK_GROW) return GROW_COUNT;
    return 0;
}

void Pick_Move(Pick *pk, Editor *ed, int delta) {
    int count = Pick_Count(pk);
    if (count <= 0 || delta == 0) return;
    if (pk->kind == PICK_GROW) {
        /* One shape at a time, stepping over the ones the song has no room
         * for (Begin made sure at least one fits). */
        int dir = delta > 0 ? 1 : -1, steps = delta > 0 ? delta : -delta;
        for (int i = 0; i < steps; i++) {
            int tries = 0;
            do pk->index = Mod(pk->index + dir, count);
            while (!GrowFits(&pk->beforeSong, pk->index) && ++tries < count);
        }
        Preview(pk, ed);
        return;
    }
    /* Off-table chords sit before the first entry and after the last. */
    if (pk->kind == PICK_CHORDS && pk->index < 0) pk->index = delta > 0 ? delta - 1 : delta;
    else pk->index += delta;
    pk->index = Mod(pk->index, count);
    if (pk->kind == PICK_CHORDS) pk->chords = Compose_ProgressionAt(pk->index)->chords;
    if (pk->kind == PICK_FILL && pk->track == TRACK_LEAD && pk->index == 1 && TrackEmpty(&pk->before, TRACK_LEAD))
        Editor_SetStatus(ed, "Fit my rhythm needs notes on the lead first");
    Preview(pk, ed);
}

bool Pick_Reroll(Pick *pk, Editor *ed) {
    if (!pk || !ed || pk->kind == PICK_NONE || pk->kind == PICK_CHORDS) return false;
    /* The patterns a new take can touch: the one being filled, Vary's new
     * one, or the ones Grow added (a take keeps the shape, so the same slots). */
    int lo = pk->pattern, n = 1;
    if (pk->kind == PICK_VARY) lo = pk->added;
    if (pk->kind == PICK_GROW) { lo = pk->beforeSong.patternCount; n = pk->added; }
    SongPattern was[8];
    if (n > 8) n = 8; /* Grow adds at most 7 */
    if (n < 1 || lo < 0 || lo + n > SONG_MAX_PATTERNS) n = 0;
    if (n > 0) memcpy(was, &ed->song.patterns[lo], (size_t)n * sizeof(SongPattern));
    pk->seed = pk->seed * 1664525u + 1013904223u;
    Preview(pk, ed);
    if (n > 0 && memcmp(was, &ed->song.patterns[lo], (size_t)n * sizeof(SongPattern)) != 0) return true;
    /* Only some choices have other takes (a tune, ghost notes, a fill's roll);
     * say so rather than let R look broken. */
    const char *what = pk->kind == PICK_VARY ? "kind" : (pk->kind == PICK_GROW ? "shape" : "style");
    char msg[EDITOR_STATUS_LEN];
    snprintf(msg, sizeof(msg), "This %s has one take. Left/Right for another", what);
    Editor_SetStatus(ed, msg);
    return false;
}

void Pick_KeyStep(Pick *pk, Editor *ed, int delta) {
    if (!pk || pk->kind != PICK_CHORDS || delta == 0) return;
    pk->key.root = Mod(pk->key.root + delta, 12);
    pk->keyGuessed = false; /* chosen now, so no longer a guess */
    pk->tonicMidi = Compose_TonicMidi(pk->key, &pk->before);
    Preview(pk, ed);
}

void Pick_Toggle(Pick *pk, Editor *ed) {
    if (!pk || pk->kind != PICK_CHORDS) return;
    pk->flag = !pk->flag;
    Preview(pk, ed);
}

bool Pick_SetChords(Pick *pk, Editor *ed, const ComposeChords *c) {
    if (!pk || !c || pk->kind != PICK_CHORDS) return false;
    for (int b = 0; b < 4; b++) pk->chords.degree[b] = Mod(c->degree[b], 7);
    pk->index = Compose_ProgressionFind(&pk->chords);
    Preview(pk, ed);
    return true;
}

/* "Up and down" -> "up and down": style names are sentence case, so only the
 * first letter is a capital, and mid-sentence it is not. */
static void LowerFirst(const char *in, char *out, size_t n) {
    snprintf(out, n, "%s", in ? in : "");
    if (out[0]) out[0] = (char)tolower((unsigned char)out[0]);
}

/* The chords on trial as the label shows them: the table's name, or their
 * numbers when they are not in it ("1 6 4 5", the chord line's own syntax). */
static void ChordsValue(const Pick *pk, char *out, size_t n) {
    if (pk->index >= 0) {
        snprintf(out, n, "%s", Compose_ProgressionAt(pk->index)->name);
        return;
    }
    snprintf(out, n, "%d %d %d %d", Mod(pk->chords.degree[0], 7) + 1, Mod(pk->chords.degree[1], 7) + 1,
             Mod(pk->chords.degree[2], 7) + 1, Mod(pk->chords.degree[3], 7) + 1);
}

static void ChordNames(ComposeKey k, const ComposeChords *c, char *out, size_t n) {
    if (n == 0) return;
    out[0] = '\0';
    for (int b = 0; b < 4; b++) {
        char name[16];
        Compose_ChordName(k, c->degree[b], name, sizeof(name));
        size_t len = strlen(out);
        snprintf(out + len, n - len, "%s%s", b ? " " : "", name);
    }
}

/* "1:04" or "32 s": how long a song lasts, as the status and band say it. */
static void FormatLength(float seconds, char *out, size_t n) {
    int secs = (int)(seconds + 0.5f);
    if (secs < 60) snprintf(out, n, "%d s", secs);
    else snprintf(out, n, "%d:%02d", secs / 60, secs % 60);
}

/* Vary and Grow keep the whole song, as one entry whose cursor is where the
 * picker opened (the commit records the cursor as it stands), then leave the
 * cursor on what was made: the new pattern, or the source's first entry. */
static bool KeepSong(Pick *pk, Editor *ed) {
    int madePattern = ed->pattern, madePos = ed->orderPos;
    ed->pattern = pk->pattern;
    ed->orderPos = pk->orderPos;
    bool pushed = Editor_CommitSong(ed, &pk->beforeSong);
    if (pushed) {
        ed->pattern = madePattern;
        ed->orderPos = madePos;
    }
    Editor_Pattern(ed); /* clamps */
    /* Wider than the status line so the format can never cut a word; the
     * longest real one ("Grew into 8 patterns, about 3:03. 56 others left
     * out of the order") fits the editor's line. */
    char msg[128];
    if (!pushed) {
        snprintf(msg, sizeof(msg), "Nothing changed");
    } else if (pk->kind == PICK_VARY) {
        char kind[32];
        LowerFirst(Arrange_VaryName((VaryKind)pk->index), kind, sizeof(kind));
        snprintf(msg, sizeof(msg), "Pattern %02d varies %02d: %s", pk->added, pk->pattern, kind);
    } else {
        /* The patterns the arrangement plays: the source and what was added.
         * Any others the song had are kept but no longer in the order, which
         * is worth saying since they stop being heard. */
        char len[16];
        FormatLength(Song_Seconds(&ed->song), len, sizeof(len));
        int left = pk->beforeSong.patternCount - 1;
        if (left > 0) snprintf(msg, sizeof(msg), "Grew into %d patterns, about %s. %d other%s left out of the order", pk->added + 1, len, left, left == 1 ? "" : "s");
        else snprintf(msg, sizeof(msg), "Grew into %d patterns, about %s", pk->added + 1, len);
    }
    Editor_SetStatus(ed, msg);
    pk->kind = PICK_NONE;
    return pushed;
}

bool Pick_Keep(Pick *pk, Editor *ed, bool allPatterns) {
    if (!pk || !ed || pk->kind == PICK_NONE) return false;
    if (pk->kind == PICK_VARY || pk->kind == PICK_GROW) return KeepSong(pk, ed);
    if (pk->kind != PICK_FILL && pk->kind != PICK_CHORDS) return false;
    bool pushed;
    bool hadNotes = !TrackEmpty(&pk->before, pk->track);
    if (pk->kind == PICK_FILL && !allPatterns) {
        pushed = Editor_CommitPattern(ed, pk->pattern, &pk->before);
    } else {
        /* The song before the session: the live one with the snapshot back
         * in place, since the preview touched nothing else. */
        memcpy(&pk->beforeSong, &ed->song, sizeof(Song));
        memcpy(&pk->beforeSong.patterns[pk->pattern], &pk->before, sizeof(SongPattern));
        if (allPatterns) {
            /* Each pattern from its own snapshot, so every one is filled on
             * its own chords (Fill) or moved from its own chords (Chords). */
            for (int i = 0; i < ed->song.patternCount; i++) {
                const SongPattern *src = &pk->beforeSong.patterns[i];
                SongPattern out;
                if (pk->kind == PICK_FILL) BuildFill(src, pk->key, pk->track, pk->index, pk->seed, &out);
                else BuildChords(src, pk->key, &pk->chords, pk->flag, &out);
                if (!TrackEmpty(src, pk->track)) hadNotes = true;
                Editor_ReplacePattern(ed, i, &out);
            }
        }
        /* Chords was worked out in this key, so the song now carries it, and
         * the in-key tools and the next Chords start from it. The synth never
         * reads the key, so writing it into the live song is safe. */
        if (pk->kind == PICK_CHORDS) {
            ed->song.keyRoot = Mod(pk->key.root, 12);
            ed->song.keyScale = pk->key.scale;
        }
        pushed = Editor_CommitSong(ed, &pk->beforeSong);
    }

    /* Wider than the status line so the format cannot truncate mid-name;
     * the longest real one ("Chords: Sad and racing in C# harmonic minor,
     * every pattern") fits the editor's line. */
    char msg[160];
    if (!pushed) {
        snprintf(msg, sizeof(msg), "Nothing changed");
    } else if (pk->kind == PICK_FILL) {
        char style[32];
        LowerFirst(Compose_StyleName((SongTrack)pk->track, pk->index), style, sizeof(style));
        /* Fill replaces what was on the track, which may have been typed by
         * hand, so say how to get it back. */
        snprintf(msg, sizeof(msg), "%s: %s%s%s", kTrackTitles[pk->track], style, allPatterns ? ", every pattern" : "",
                 hadNotes ? " (Ctrl+Z undoes it)" : "");
    } else {
        char value[32], key[32];
        ChordsValue(pk, value, sizeof(value));
        Song_KeyName(Mod(pk->key.root, 12), pk->key.scale, key, sizeof(key));
        snprintf(msg, sizeof(msg), "Chords: %s in %s%s", value, key, allPatterns ? ", every pattern" : "");
    }
    Editor_SetStatus(ed, msg);
    pk->kind = PICK_NONE;
    return pushed;
}

void Pick_Cancel(Pick *pk, Editor *ed) {
    if (!pk || !ed || pk->kind == PICK_NONE) return;
    Restore(pk, ed);
    Editor_SetStatus(ed, "Put back as it was");
}

bool Pick_Audition(const Pick *pk, const Editor *ed, int *orderPosOut, int *stepOut) {
    if (!pk || !ed || ed->song.orderCount < 1) return false;
    int pos, step;
    if (pk->kind == PICK_VARY) {
        /* The source's last bar, then the new pattern: the change is heard
         * as it happens. That lead-in only exists when the entry before the
         * new one plays the source (the cursor can be on a pattern the order
         * slot does not play); otherwise start on the new pattern itself. */
        pos = pk->orderPos;
        step = SONG_STEPS - 16;
        if (pos < 0 || pos >= ed->song.orderCount || ed->song.order[pos] != pk->pattern) {
            pos = pk->orderPos + 1;
            step = 0;
        }
    } else if (pk->kind == PICK_GROW) {
        /* From the top: the source in the loop shapes, the intro in the one
         * with an ending, which is part of what is being chosen. */
        pos = 0;
        step = 0;
    } else {
        return false;
    }
    if (pos >= ed->song.orderCount) pos = ed->song.orderCount - 1;
    if (pos < 0) pos = 0;
    if (orderPosOut) *orderPosOut = pos;
    if (stepOut) *stepOut = step;
    return true;
}

void Pick_Label(const Pick *pk, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = '\0';
    if (!pk) return;
    if (pk->kind == PICK_FILL) {
        snprintf(out, n, "Fill %s  < %s >", kTrackWords[pk->track], Compose_StyleName((SongTrack)pk->track, pk->index));
    } else if (pk->kind == PICK_CHORDS) {
        char value[32], key[32];
        ChordsValue(pk, value, sizeof(value));
        Song_KeyName(Mod(pk->key.root, 12), pk->key.scale, key, sizeof(key));
        snprintf(out, n, "Chords  < %s >  %s%s", value, key, pk->keyGuessed ? "?" : "");
    } else if (pk->kind == PICK_VARY) {
        snprintf(out, n, "Vary %02d into %02d  < %s >", pk->pattern, pk->added, Arrange_VaryName((VaryKind)pk->index));
    } else if (pk->kind == PICK_GROW) {
        snprintf(out, n, "Grow  < %s >", Arrange_ShapeName((GrowShape)pk->index));
    }
}

void Pick_Info(const Pick *pk, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = '\0';
    if (!pk) return;
    char names[64];
    if ((pk->kind == PICK_FILL && pk->track != TRACK_DRUMS) || pk->kind == PICK_VARY) {
        ChordNames(pk->key, &pk->chords, names, sizeof(names));
        snprintf(out, n, "on %s", names);
    } else if (pk->kind == PICK_CHORDS) {
        ChordNames(pk->key, &pk->chords, names, sizeof(names));
        snprintf(out, n, "%s%s", names, pk->flag ? ", tune fitted" : "");
    } else if (pk->kind == PICK_GROW) {
        /* The shape's name says "about 1 min" at 120 bpm; this is the real
         * length at the song's own tempo. */
        char len[16];
        FormatLength(pk->seconds, len, sizeof(len));
        snprintf(out, n, "%d patterns, about %s", pk->added + 1, len);
    }
}

void Pick_Detail(const Pick *pk, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = '\0';
    if (!pk) return;
    if (pk->kind == PICK_FILL) {
        snprintf(out, n, "Left/Right style    R new take    Enter keep    Shift+Enter every pattern    Esc put it back");
    } else if (pk->kind == PICK_CHORDS) {
        /* F's hint says what pressing it would do next. Shift+Enter is on
         * the hint card only: with it this line would not fit the band. */
        snprintf(out, n, "Left/Right chords    Up/Down key    F %s the tune    / type them    Enter keep    Esc put it back",
                 pk->flag ? "leave" : "fit");
    } else if (pk->kind == PICK_VARY) {
        snprintf(out, n, "Left/Right kind    R new take    Enter keep    Esc put it back    Space hears it again");
    } else if (pk->kind == PICK_GROW) {
        snprintf(out, n, "Left/Right shape    R new take    Enter keep    Esc put it back    Space plays it from the top");
    }
}

const char *const *Pick_Keys(const Pick *pk) {
    static const char *const kFillKeys[] = {
        "< > style", "R new take", "Enter keep", "Sh+Enter all", "Esc put back", "Space loop", NULL,
    };
    static const char *const kChordKeys[] = {
        "< > chords", "Up Dn key", "F fit tune", "/ type them", "Enter keep", "Sh+Enter all", "Esc put back", "Space loop", NULL,
    };
    static const char *const kVaryKeys[] = {
        "< > kind", "R new take", "Enter keep", "Esc put back", "Space replay", NULL,
    };
    static const char *const kGrowKeys[] = {
        "< > shape", "R new take", "Enter keep", "Esc put back", "Space replay", NULL,
    };
    if (!pk) return NULL;
    if (pk->kind == PICK_FILL) return kFillKeys;
    if (pk->kind == PICK_CHORDS) return kChordKeys;
    if (pk->kind == PICK_VARY) return kVaryKeys;
    if (pk->kind == PICK_GROW) return kGrowKeys;
    return NULL;
}
