#include "render.h"
#include "raylib.h"
#include "ui.h"
#include "winscale.h"
#include "song.h"
#include "browser.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- the tracker's geometry -------------------------------------------------
 * Everything is derived from these so the grid, the headings and the cursor
 * box line up whatever we tweak. Press Start 2P is an 8x8 design: a T8 glyph
 * advances 9px (8 + one font pixel of spacing), a T16 glyph 18px. */
#define HEADER_H 56
#define ROW_H 16
#define ROWS_SHOWN 30
#define GRID_Y 88                   /* under the column headings */
#define GRID_BOTTOM (GRID_Y + ROWS_SHOWN * ROW_H) /* 568 */

#define ORDER_X 16
#define ORDER_W 120

#define CENTRE_X 152
#define CENTRE_W 656
#define STEP_COL_W 40               /* "00" plus a gap */
#define TRACK_COL_W 148
#define GRID_W (STEP_COL_W + SONG_TRACKS * TRACK_COL_W) /* 632 */
#define GRID_X (CENTRE_X + (CENTRE_W - GRID_W) / 2)      /* centred in the middle column */
#define CELL_TEXT_W (3 * 16 + 2 * 2)  /* three T16 glyphs */
#define CELL_BOX_W (CELL_TEXT_W + 8)

#define HINT_X 824
#define HINT_W 120

#define STATUS_Y (WINDOW_HEIGHT - 40)
#define STATUS_HOLD 2.5f            /* seconds fully visible before the fade */
#define STATUS_FADE 0.5f

static const char *const kTrackNames[SONG_TRACKS] = {"BASS", "ARP", "LEAD", "DRUMS"};

/* The keys you reach for most; the how-to card has the rest. Each line fits
 * the 120px column at T8 (13 glyphs), and all of them fit the card's 30
 * rows. The two piano lines are swapped for their in-key wording while
 * in-key typing is on (HINT_PIANO_LOW/HIGH say where they are). */
enum { HINT_PIANO_LOW = 3, HINT_PIANO_HIGH = 4 };
static const char *const kKeyHints[] = {
    "Space play",
    "Sh+Space loop",
    "Enter cursor",
    "Z..M notes",   /* HINT_PIANO_LOW */
    "Q..P upper",   /* HINT_PIANO_HIGH */
    "' hold",
    "Del clear",
    "=/- octave",
    "[ ] bpm",
    ", . swing",
    "F5 F6 wave",
    "F7 F8 volume",
    "Tab track",
    "Ctrl+F fill",
    "Ctrl+K chords",
    "Ctrl+R vary",
    "F9 grow",
    "F10 record",
    "F3 mood",
    "F4 in key",
    "Ctrl+Sh+K fit",
    "Ctrl+N new",
    "Ctrl+D dup",
    "Ctrl+Z undo",
    "Ctrl+S save",
    "Ctrl+E export",
    "Ctrl+G assign",
    NULL,
};

static void SelectionBar(int x, int y, int w, int h, const UiTheme *t) {
    DrawRectangle(x, y, w, h, t->panel);
    DrawRectangle(x, y, 4, h, t->light);
}

/* Keeps `target` inside the middle third of a window of `shown` rows over
 * `count` rows, moving *top only when it has to so the view does not jitter. */
static void FollowRow(int *top, int target, int shown, int count) {
    int lo = shown / 3, hi = shown - shown / 3 - 1;
    if (target < *top + lo) *top = target - lo;
    if (target > *top + hi) *top = target - hi;
    if (*top > count - shown) *top = count - shown;
    if (*top < 0) *top = 0;
}

/* Scrolls just enough to keep `target` visible (lists, order column). */
static void KeepVisible(int *top, int target, int shown, int count) {
    if (target < *top) *top = target;
    if (target >= *top + shown) *top = target - shown + 1;
    if (*top > count - shown) *top = count - shown;
    if (*top < 0) *top = 0;
}

/* How many bytes are really kept when `keep` are asked for: the cut is moved
 * onto a character boundary (file names are UTF-8) so no sequence is split.
 * Keeping the head, the cut moves back; keeping the tail, forwards. Either
 * way no more bytes than asked for, so a fitting `keep` still fits. */
static size_t SnapKeep(const char *text, size_t len, size_t keep, bool keepTail) {
    if (keep >= len) return len;
    if (keepTail) {
        size_t cut = len - keep;
        while (cut < len && ((unsigned char)text[cut] & 0xC0) == 0x80) cut++;
        return len - cut;
    }
    size_t cut = keep;
    while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;
    return cut;
}

static void WriteFit(const char *text, size_t len, size_t keep, char *out, size_t outSize, bool keepTail) {
    if (keepTail) snprintf(out, outSize, "...%s", text + (len - keep));
    else snprintf(out, outSize, "%.*s...", (int)keep, text);
}

/* Copies `text` into out, shortened with "..." until it measures at most
 * maxW pixels at `size`. keepTail keeps the end (for folder paths, where the
 * last folder is what matters); otherwise the start is kept. */
static const char *Fit(const char *text, int size, int maxW, char *out, size_t outSize, bool keepTail) {
    if (!text) text = "";
    snprintf(out, outSize, "%s", text);
    if (maxW <= 0 || Ui_Measure(out, size) <= maxW) return out;
    size_t len = strlen(text);
    /* The width only grows with the bytes kept, so bisect for the most that
     * fit: about ten measures for a 1000-byte folder path instead of one per
     * byte, which at 60 fps with a screen of long file names cost more than
     * the whole frame. lo always fits ("..." alone is taken to); hi never. */
    size_t lo = 0, hi = len;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        WriteFit(text, len, SnapKeep(text, len, mid, keepTail), out, outSize, keepTail);
        if (Ui_Measure(out, size) <= maxW) lo = mid; else hi = mid;
    }
    WriteFit(text, len, SnapKeep(text, len, lo, keepTail), out, outSize, keepTail);
    return out;
}

/* A line of keys ("Left/Right shape    R new take    Enter keep") cut to
 * maxW at a gap between two keys, so every key shown is whole; Fit's "..."
 * only when not even the first one fits. */
static const char *FitKeys(const char *text, int size, int maxW, char *out, size_t outSize) {
    snprintf(out, outSize, "%s", text);
    if (Ui_Measure(out, size) <= maxW) return out;
    size_t keep = 0;
    for (const char *gap = strstr(text, "    "); gap; gap = strstr(gap + 4, "    ")) {
        size_t len = (size_t)(gap - text);
        if (len >= outSize) break;
        snprintf(out, outSize, "%.*s", (int)len, text);
        if (Ui_Measure(out, size) > maxW) break;
        keep = len;
    }
    if (keep == 0) return Fit(text, size, maxW, out, outSize, false);
    snprintf(out, outSize, "%.*s", (int)keep, text);
    return out;
}

void Render_ConfirmOverlay(const RenderConfirm *c) {
    const UiTheme *t = Ui_Theme();
    if (!c) return;
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(t->bg, 0.8f));
    int titleW = Ui_Measure(c->title ? c->title : "", UI_T16);
    int hintW = Ui_Measure(c->hint ? c->hint : "", UI_T8);
    int w = (titleW > hintW ? titleW : hintW) + 64;
    if (w < 360) w = 360;
    if (w > WINDOW_WIDTH - 64) w = WINDOW_WIDTH - 64;
    int h = 104, x = (WINDOW_WIDTH - w) / 2, y = (WINDOW_HEIGHT - h) / 2;
    Ui_BevelRect(x, y, w, h, t->board);
    DrawRectangle(x, y, w, 4, t->light);
    char fit[256];
    if (c->title) Ui_TextCentered(Fit(c->title, UI_T16, w - 48, fit, sizeof(fit), false), UI_T16, WINDOW_WIDTH / 2, y + 28, t->text);
    if (c->hint) Ui_TextCentered(Fit(c->hint, UI_T8, w - 48, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, y + 68, t->dim);
}

/* --- the tracker ----------------------------------------------------------- */

/* "key A minor" for a song that says its key, "key A minor?" for one whose
 * key is a guess from its notes, "" for one with no notes to guess from
 * (an empty song is in no key, and a guessed "A minor?" would only mislead). */
static void KeyTag(const Song *s, char *out, size_t n) {
    char name[32];
    out[0] = '\0';
    if (s->keyRoot >= 0 && s->keyRoot < 12) {
        Song_KeyName(s->keyRoot, s->keyScale, name, sizeof(name));
        snprintf(out, n, "key %s", name);
        return;
    }
    ComposeKey k;
    if (!Compose_GuessKey(s, &k)) return;
    Song_KeyName(k.root, k.scale, name, sizeof(name));
    snprintf(out, n, "key %s?", name);
}

static void DrawHeader(const Editor *ed, const RenderPlayback *pb, const RenderExtras *x, const UiTheme *t) {
    char buf[128], fit[160];
    /* The song's key and the "in key" tag sit on the name line at a fixed
     * place, just left of the PLAYING slot (reserved even when silent, and
     * the tag's room reserved even when off), so neither jumps about as
     * playback or F4 toggles. The readout line under it is full already. The
     * name gets whatever is left and is shortened rather than run into them. */
    int tagW = Ui_Measure("in key", UI_T8);
    int tagX = WINDOW_WIDTH - 16 - Ui_Measure("PLAYING", UI_T16) - 32;
    int keyRight = tagX - tagW - 18;
    if (x && x->scaleKeys) Ui_TextRight("in key", UI_T8, tagX, 16, t->light);
    char key[48];
    KeyTag(&ed->song, key, sizeof(key));
    int nameRight = keyRight;
    if (key[0]) {
        Ui_TextRight(key, UI_T8, keyRight, 16, t->dim);
        nameRight = keyRight - Ui_Measure(key, UI_T8) - 24;
    }
    snprintf(buf, sizeof(buf), "%s%s", ed->song.name[0] ? ed->song.name : "Untitled", ed->dirty ? " *" : "");
    Ui_Text(Fit(buf, UI_T16, nameRight - 16, fit, sizeof(fit), false), 16, 12, UI_T16, t->text);

    snprintf(buf, sizeof(buf), "BPM %d   swing %.2f   octave %d   step %d   pattern %02d (%d in all)   pos %02d (%d in all)",
             ed->song.bpm, (double)ed->song.swing, ed->octave, ed->editStep,
             ed->pattern, ed->song.patternCount, ed->orderPos, ed->song.orderCount);
    Ui_Text(buf, 16, 38, UI_T8, t->dim);

    if (x && x->recPass > 0) {
        /* Recording owns the PLAYING slot, in danger like any record light,
         * with the catch grid under it at the end of the readout line. */
        char rec[16];
        snprintf(rec, sizeof(rec), "REC %d", x->recPass);
        Ui_TextRight(rec, UI_T16, WINDOW_WIDTH - 16, 12, t->danger);
        if (x->catchLabel) {
            snprintf(buf, sizeof(buf), "catch %s", x->catchLabel);
            Ui_TextRight(buf, UI_T8, WINDOW_WIDTH - 16, 38, t->dim);
        }
    } else if (pb->playing) {
        Ui_TextRight(pb->looping ? "LOOP" : "PLAYING", UI_T16, WINDOW_WIDTH - 16, 12, t->light);
    }
    DrawRectangle(0, HEADER_H - 1, WINDOW_WIDTH, 1, t->rule);
}

static void DrawOrder(const Editor *ed, const RenderPlayback *pb, const UiTheme *t) {
    static int sTop = 0;
    const Song *s = &ed->song;
    KeepVisible(&sTop, ed->orderPos, ROWS_SHOWN, s->orderCount);

    Ui_Text("ORDER", ORDER_X, HEADER_H + 8, UI_T8, t->dim);
    for (int i = 0; i < ROWS_SHOWN; i++) {
        int pos = sTop + i;
        if (pos >= s->orderCount) break;
        int y = GRID_Y + i * ROW_H;
        bool current = (pos == ed->orderPos);
        bool playingHere = pb->playing && !pb->looping && pb->orderPos == pos;
        if (current) SelectionBar(ORDER_X, y, ORDER_W, ROW_H, t);
        char buf[16];
        snprintf(buf, sizeof(buf), "%c%02d P%02d", playingHere ? '>' : ' ', pos, s->order[pos]);
        Ui_Text(buf, ORDER_X + 8, y + 4, UI_T8, current ? t->light : (playingHere ? t->text : t->dim));
    }
    if (sTop > 0) Ui_Text("more", ORDER_X + 8, GRID_Y - 12, UI_T8, t->faint);
    if (sTop + ROWS_SHOWN < s->orderCount) Ui_Text("more", ORDER_X + 8, GRID_BOTTOM + 4, UI_T8, t->faint);
}

static void DrawCellText(const char *text, int x, int y, int track, bool cursor, const UiTheme *t) {
    if (cursor) { Ui_Text(text, x, y, UI_T16, t->light); return; }
    if (track == TRACK_DRUMS) {
        /* Drum pads are three independent letters, so colour each on its own:
         * a hit in gold, the placeholder in faint. */
        char one[2] = {0, 0};
        for (int i = 0; i < 3 && text[i]; i++) {
            one[0] = text[i];
            bool hit = (text[i] != '-' && text[i] != '.' && text[i] != ' ');
            Ui_Text(one, x + i * 18, y, UI_T16, hit ? t->gold : t->faint);
        }
        return;
    }
    Color c = t->text;
    if (text[0] == '-' || text[0] == '.') c = t->faint;
    else if (text[0] == '=') c = t->dim;
    Ui_Text(text, x, y, UI_T16, c);
}

/* A drum cell while recording over it: the hits already there in gold as
 * usual, the hits of the pass being played (not written yet) in dim. */
static void DrawLiveDrums(uint8_t base, uint8_t live, int x, int y, bool cursor, const UiTheme *t) {
    static const char kLetters[3] = {'K', 'S', 'H'};
    char one[2] = {0, 0};
    for (int i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        bool had = (base & bit) != 0, played = (live & bit) != 0;
        one[0] = (had || played) ? kLetters[i] : '.';
        Color c = had ? t->gold : (played ? t->dim : t->faint);
        if (cursor) c = t->light;
        Ui_Text(one, x + i * 18, y, UI_T16, c);
    }
}

/* The text of any pattern's cell, formatted as Editor_CellText formats the
 * edited pattern's (that one only reads ed->pattern). */
static void CellText(const Song *s, int pattern, int track, int step, char out[4]) {
    if (pattern < 0 || pattern >= s->patternCount || step < 0 || step >= SONG_STEPS) { memcpy(out, "---", 4); return; }
    const SongPattern *p = &s->patterns[pattern];
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

/* Whether a bar of a pattern has a bass or arp note: only those bars get a
 * chord name, since a bar of drums alone is on no chord at all. */
static bool BarHasHarmony(const SongPattern *p, int bar) {
    for (int i = 0; i < 16; i++) {
        int st = bar * 16 + i;
        if (p->tone[TRACK_BASS][st] >= 0 || p->tone[TRACK_ARP][st] >= 0) return true;
    }
    return false;
}

static void DrawPatternGrid(const Editor *ed, const RenderPlayback *pb, const RenderExtras *x, const UiTheme *t) {
    static int sTop = 0;
    const Song *s = &ed->song;
    /* Playing through the order, the grid shows the pattern the synth is on
     * and follows its step, even once the order has carried it past the
     * cursor's pattern; the cursor stays put (and is drawn only while its
     * pattern is the one shown) and the grid comes back to it when playback
     * stops. A pattern loop is different: it was started from the cursor's
     * pattern, and moving the cursor to a neighbour while it repeats is how
     * you prepare that neighbour, so the loop never drags the grid back. */
    bool following = pb->playing && !pb->looping && pb->pattern >= 0 && pb->pattern < s->patternCount;
    int shownPattern = following ? pb->pattern : ed->pattern;
    bool cursorHere = (shownPattern == ed->pattern);
    bool playheadHere = pb->playing && pb->pattern == shownPattern;
    FollowRow(&sTop, playheadHere ? pb->step : ed->step, ROWS_SHOWN, SONG_STEPS);

    /* The chord each bar of the shown pattern is on, named beside its step
     * number, so every screen says what chord you are on. Read once a frame
     * (four bars, and a key guess over the order when the song has none):
     * cheap next to drawing the grid. */
    const SongPattern *shown = &s->patterns[shownPattern];
    /* The pass being recorded, over the pattern it will be written into. */
    const SongPattern *live = (x && x->livePass && x->livePattern == shownPattern) ? x->livePass : NULL;
    ComposeKey chordKey = (x && x->chordKey) ? *x->chordKey : Compose_SongKey(s, NULL);
    ComposeChords barChords;
    bool haveChords = Compose_ReadChords(shown, chordKey, &barChords);

    /* Column headings: name, wave and volume. The current track in text, the
     * others dim, so you can tell where Tab has you without hunting for the cursor. */
    for (int tr = 0; tr < SONG_TRACKS; tr++) {
        /* Two lines: the name, then wave and volume, so no heading can run
         * into its neighbour however long the wave name is. */
        char buf[32];
        if (tr < SONG_TONE_TRACKS) snprintf(buf, sizeof(buf), "%s %d", Song_WaveName(s->wave[tr]), s->volume[tr]);
        else snprintf(buf, sizeof(buf), "vol %d", s->volume[tr]);
        int cx = GRID_X + STEP_COL_W + tr * TRACK_COL_W + TRACK_COL_W / 2;
        Ui_TextCentered(kTrackNames[tr], UI_T8, cx, HEADER_H + 4, tr == ed->track ? t->text : t->dim);
        Ui_TextCentered(buf, UI_T8, cx, HEADER_H + 16, tr == ed->track ? t->dim : t->faint);
    }
    DrawRectangle(GRID_X, GRID_Y - 4, GRID_W, 1, t->rule);

    for (int i = 0; i < ROWS_SHOWN; i++) {
        int step = sTop + i;
        if (step >= SONG_STEPS) break;
        int y = GRID_Y + i * ROW_H;
        if (step % 4 == 0) DrawRectangle(GRID_X, y, GRID_W, ROW_H, t->board);
        if (step % 16 == 0) DrawRectangle(GRID_X, y, GRID_W, 1, t->rule);
        /* The one bold element on the screen: the playhead. */
        if (playheadHere && pb->step == step) DrawRectangle(GRID_X, y, GRID_W, ROW_H, Fade(t->light, 0.25f));

        char num[8];
        snprintf(num, sizeof(num), "%02d", step);
        Ui_Text(num, GRID_X + 4, y + 4, UI_T8, step % 16 == 0 ? t->text : t->dim);
        /* "16 F": the longest name ("C#dim") still ends short of the bass
         * cell, which sits 84px into the grid. */
        if (step % 16 == 0 && haveChords && BarHasHarmony(shown, step / 16)) {
            char chord[16];
            Compose_ChordName(chordKey, barChords.degree[step / 16], chord, sizeof(chord));
            Ui_Text(chord, GRID_X + 4 + Ui_Measure(num, UI_T8) + 9, y + 4, UI_T8, t->dim);
        }

        for (int tr = 0; tr < SONG_TRACKS; tr++) {
            int colX = GRID_X + STEP_COL_W + tr * TRACK_COL_W;
            int boxX = colX + (TRACK_COL_W - CELL_BOX_W) / 2;
            bool cursor = cursorHere && tr == ed->track && step == ed->step;
            if (cursor) DrawRectangle(boxX, y, CELL_BOX_W, ROW_H, t->panel);
            if (live && tr < SONG_TONE_TRACKS && live->tone[tr][step] != NOTE_NONE) {
                /* Not written yet: dim, where it will land. */
                char note[4];
                Song_NoteName(live->tone[tr][step], note);
                Ui_Text(note, boxX + 4, y, UI_T16, cursor ? t->light : t->dim);
                continue;
            }
            if (live && tr == TRACK_DRUMS && live->drums[step]) {
                DrawLiveDrums(shown->drums[step], live->drums[step], boxX + 4, y, cursor, t);
                continue;
            }
            char cell[4];
            CellText(s, shownPattern, tr, step, cell);
            DrawCellText(cell, boxX + 4, y, tr, cursor, t);
        }
    }
    DrawRectangle(GRID_X, GRID_BOTTOM + 2, GRID_W, 1, t->rule);
    /* Under the headings there is no free row, so the "more above" mark sits
     * at the left of the heading row, over the step numbers; the line below
     * it names the pattern on screen when it is not the one being edited (the
     * header's "pattern" field keeps describing the cursor). */
    if (sTop > 0) Ui_Text("more", GRID_X, HEADER_H + 4, UI_T8, t->faint);
    if (!cursorHere) {
        char tag[8];
        snprintf(tag, sizeof(tag), "P%02d", shownPattern);
        Ui_Text(tag, GRID_X, HEADER_H + 16, UI_T8, t->light);
    }
    if (sTop + ROWS_SHOWN < SONG_STEPS) Ui_TextCentered("more", UI_T8, GRID_X + GRID_W / 2, GRID_BOTTOM + 6, t->faint);
}

static void DrawHints(const RenderExtras *x, const UiTheme *t) {
    const char *const *lines = (x && x->hintCard) ? x->hintCard : kKeyHints;
    /* In key, the piano rows play the scale, so their lines say so. */
    bool inKey = lines == kKeyHints && x && x->scaleKeys;
    Ui_Text("KEYS", HINT_X, HEADER_H + 8, UI_T8, t->dim);
    int y = GRID_Y;
    for (int i = 0; lines[i]; i++, y += ROW_H) {
        if (y + ROW_H > GRID_BOTTOM) break;
        const char *line = lines[i];
        if (inKey && i == HINT_PIANO_LOW) line = "Z..M in key";
        if (inKey && i == HINT_PIANO_HIGH) line = "Q..P up";
        Ui_Text(line, HINT_X, y + 4, UI_T8, t->dim);
    }
}

/* How visible the status line is: full for STATUS_HOLD seconds, then fading;
 * 0 once it has gone (or there is none). */
float Render_StatusAlpha(const Editor *ed) {
    if (!ed->status[0] || ed->statusAge >= STATUS_HOLD + STATUS_FADE) return 0.0f;
    return ed->statusAge < STATUS_HOLD ? 1.0f : 1.0f - (ed->statusAge - STATUS_HOLD) / STATUS_FADE;
}

/* From the left edge, not the grid's: the order column ends above the rule,
 * and from CENTRE_X only 67 glyphs fit, fewer than the 71 a status holds. */
static void DrawStatus(const Editor *ed, const UiTheme *t) {
    DrawRectangle(0, STATUS_Y - 12, WINDOW_WIDTH, 1, t->rule);
    float a = Render_StatusAlpha(ed);
    if (a > 0.0f) {
        int hintX = WINDOW_WIDTH - 16 - Ui_Measure("F1 help    Esc menu", UI_T8);
        char fit[EDITOR_STATUS_LEN + 4];
        Ui_Text(Fit(ed->status, UI_T8, hintX - 32, fit, sizeof(fit), false), 16, STATUS_Y, UI_T8, Fade(t->light, a));
    }
    Ui_TextRight("F1 help    Esc menu", UI_T8, WINDOW_WIDTH - 16, STATUS_Y, t->dim);
}

/* The picker band in place of the status strip, in the house selection look
 * (panel plus a 4px light bar): what is being tried at T16, the keys at T8
 * under it. Beside the label goes what just happened (a fresh status line,
 * such as why Fit my rhythm is silent) or else what the choice gives (the
 * chords followed, a grown song's length). When the label leaves too little
 * room, that goes at the end of the keys' line instead and the keys give way:
 * the hint card lists them too, and this changes with every choice. */
static void DrawPickBand(const Editor *ed, const RenderExtras *x, const UiTheme *t) {
    int y = STATUS_Y - 11, h = WINDOW_HEIGHT - y;
    DrawRectangle(0, STATUS_Y - 12, WINDOW_WIDTH, 1, t->rule);
    SelectionBar(0, y, WINDOW_WIDTH, h, t);
    char fit[256];
    /* The count-in shows in place of the label, a number more each beat:
     * "4", "4 3", "4 3 2", "4 3 2 1", then the first downbeat. */
    char count[16] = "";
    const char *label = x->pickLabel;
    if (x->countIn > 0) {
        for (int n = 4; n >= x->countIn && n >= 1; n--) {
            size_t len = strlen(count);
            snprintf(count + len, sizeof(count) - len, "%s%d", n == 4 ? "" : " ", n);
        }
        label = count;
    }
    Fit(label, UI_T16, WINDOW_WIDTH - 32, fit, sizeof(fit), false);
    Ui_Text(fit, 16, y + 7, UI_T16, t->light);

    int room = WINDOW_WIDTH - 16 - (16 + Ui_Measure(fit, UI_T16) + 32);
    float a = Render_StatusAlpha(ed);
    const char *side = a > 0.0f ? ed->status : x->pickInfo;
    Color sideColor = a > 0.0f ? Fade(t->light, a) : t->dim;
    int detailW = WINDOW_WIDTH - 32;
    if (side && side[0]) {
        char sideFit[160]; /* a status line or a chord line, with room for "..." */
        if (Ui_Measure(side, UI_T8) <= room) {
            Ui_TextRight(side, UI_T8, WINDOW_WIDTH - 16, y + 11, sideColor);
        } else {
            Fit(side, UI_T8, WINDOW_WIDTH - 32, sideFit, sizeof(sideFit), false);
            Ui_TextRight(sideFit, UI_T8, WINDOW_WIDTH - 16, y + 33, sideColor);
            detailW -= Ui_Measure(sideFit, UI_T8) + 32;
        }
    }
    /* Six T8 glyphs at least, or it is only dots. */
    if (x->pickDetail && detailW >= 6 * 9) Ui_Text(FitKeys(x->pickDetail, UI_T8, detailW, fit, sizeof(fit)), 16, y + 33, UI_T8, t->dim);
}

void Render_Editor(const Editor *ed, const RenderPlayback *pb, const RenderConfirm *confirm, const RenderExtras *x) {
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    DrawHeader(ed, pb, x, t);
    DrawOrder(ed, pb, t);
    DrawPatternGrid(ed, pb, x, t);
    DrawHints(x, t);
    if (x && x->pickLabel) DrawPickBand(ed, x, t);
    else DrawStatus(ed, t);
    if (confirm) Render_ConfirmOverlay(confirm);
    Win_EndFrame();
}

/* --- lists ----------------------------------------------------------------- */

#define LIST_ROW_H 28
#define LIST_X 120
#define LIST_W (WINDOW_WIDTH - 2 * LIST_X)

void Render_List(const RenderList *l) {
    static int sTop = 0;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);

    char fit[BROWSER_PATH_LEN + 8];
    /* Titles can be folder paths: keep their tail. */
    Ui_TextCentered(Fit(l->title, UI_T16, WINDOW_WIDTH - 32, fit, sizeof(fit), true), UI_T16, WINDOW_WIDTH / 2, 40, t->light);
    int top = 96;
    if (l->note) { Ui_TextCentered(Fit(l->note, UI_T8, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, 76, l->noteLit ? t->light : t->dim); top = 112; }
    int bottom = WINDOW_HEIGHT - 64;
    int shown = (bottom - top) / LIST_ROW_H;
    KeepVisible(&sTop, l->selected, shown, l->count);

    if (l->count == 0 && l->empty) Ui_TextCentered(l->empty, UI_T8, WINDOW_WIDTH / 2, top + 40, t->dim);

    for (int i = 0; i < shown; i++) {
        int row = sTop + i;
        if (row >= l->count) break;
        int y = top + i * LIST_ROW_H;
        bool sel = (row == l->selected);
        bool hot = l->highlight && l->highlight[row];
        if (sel) SelectionBar(LIST_X, y, LIST_W, LIST_ROW_H, t);
        /* The right-hand note gets at most half the row; the name gets what
         * is left, and both are shortened rather than run into each other. */
        const char *r = l->right ? l->right[row] : NULL;
        int rightEdge = LIST_X + LIST_W - 12, nameMax = LIST_W - 28;
        if (r && r[0]) {
            char rfit[160];
            Fit(r, UI_T8, LIST_W / 2, rfit, sizeof(rfit), false);
            Ui_TextRight(rfit, UI_T8, rightEdge, y + 10, hot ? t->light : t->dim);
            nameMax = rightEdge - Ui_Measure(rfit, UI_T8) - 16 - (LIST_X + 16);
        }
        Ui_Text(Fit(l->rows[row], UI_T16, nameMax, fit, sizeof(fit), false), LIST_X + 16, y + 6, UI_T16, (sel || hot) ? t->light : t->text);
    }
    if (sTop > 0) Ui_TextCentered("more above", UI_T8, WINDOW_WIDTH / 2, top - 16, t->faint);
    if (sTop + shown < l->count) Ui_TextCentered("more below", UI_T8, WINDOW_WIDTH / 2, bottom + 4, t->faint);

    if (l->hints) Ui_TextCentered(l->hints, UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
    if (l->confirm) Render_ConfirmOverlay(l->confirm);
    Win_EndFrame();
}

/* --- import ---------------------------------------------------------------- */

void Render_Import(const RenderImport *im) {
    const UiTheme *t = Ui_Theme();
    const char *name = im->fileName ? im->fileName : "";
    char buf[160];
    Win_BeginFrame();
    ClearBackground(t->bg);
    Ui_TextCentered(im->song ? "Open a song" : "Import a recording", UI_T16, WINDOW_WIDTH / 2, 40, t->light);
    char fit[BROWSER_PATH_LEN + 8];
    Ui_TextCentered(Fit(name, UI_T8, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, 76, t->dim);

    if (im->phase == IMPORT_LISTENING) {
        /* The file name is already on the line above, so this one stays short. */
        snprintf(buf, sizeof(buf), "Listening...");
        Ui_TextCentered(buf, UI_T16, WINDOW_WIDTH / 2, 300, t->text);
        Ui_TextCentered("This takes a second or two", UI_T8, WINDOW_WIDTH / 2, 340, t->dim);
        Win_EndFrame();
        return;
    }

    if (im->phase == IMPORT_OPTIONS) {
        const char *labels[4] = {"Tempo", "Drums", "Chords", "Import"};
        char values[4][24];
        if (im->tempoBpm > 0) snprintf(values[0], sizeof(values[0]), "< %d >", im->tempoBpm);
        else snprintf(values[0], sizeof(values[0]), "< auto >");
        snprintf(values[1], sizeof(values[1]), "%s", im->drums ? "on" : "off");
        snprintf(values[2], sizeof(values[2]), "%s", im->chords ? "on" : "off");
        values[3][0] = '\0';
        int y = 200;
        for (int i = 0; i < 4; i++, y += 48) {
            bool sel = (i == im->selected);
            if (sel) SelectionBar(LIST_X, y - 12, LIST_W, 40, t);
            Ui_Text(labels[i], LIST_X + 24, y, UI_T16, sel ? t->light : t->text);
            if (values[i][0]) Ui_TextRight(values[i], UI_T16, LIST_X + LIST_W - 24, y, sel ? t->light : t->text);
        }
        Ui_TextCentered("Auto tempo listens for the beat; set one when it guesses wrong", UI_T8, WINDOW_WIDTH / 2, y + 8, t->dim);
        Ui_TextCentered("Up/Down choose    Left/Right adjust    Enter select    Esc back", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
        Win_EndFrame();
        return;
    }

    if (im->phase == IMPORT_FAILED) {
        Ui_TextCentered(im->song ? "Could not open this song" : "Could not import this file", UI_T16, WINDOW_WIDTH / 2, 260, t->text);
        if (im->message) Ui_TextCentered(Fit(im->message, UI_T8, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, 300, t->dim);
        Ui_TextCentered("Esc back", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
        Win_EndFrame();
        return;
    }

    /* IMPORT_REPORT */
    const TranscribeReport *r = im->report;
    Ui_TextCentered("Here is what the chip heard", UI_T16, WINDOW_WIDTH / 2, 160, t->text);
    if (r) {
        int y = 220;
        int mins = (int)(r->seconds / 60.0f), secs = (int)fmodf(r->seconds, 60.0f);
        snprintf(buf, sizeof(buf), "Tempo %d bpm, confidence %d%%", r->bpm, (int)(r->bpmConfidence * 100.0f + 0.5f));
        Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, y, t->text); y += 24;
        snprintf(buf, sizeof(buf), "Length %d:%02d, %d steps", mins, secs, r->steps);
        Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, y, t->text); y += 24;
        snprintf(buf, sizeof(buf), "%d pattern%s%s", r->patterns, r->patterns == 1 ? "" : "s", r->truncated ? ", truncated: the song ran out of patterns before the end" : "");
        Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, y, r->truncated ? t->gold : t->text); y += 24;
        snprintf(buf, sizeof(buf), "Notes found: lead %d, bass %d", r->leadNotes, r->bassNotes);
        Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, y, t->text); y += 24;
        snprintf(buf, sizeof(buf), "Drums: %d kicks, %d snares, %d hats", r->kicks, r->snares, r->hats);
        Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, y, t->text); y += 40;
        Ui_TextCentered("It is a cover by a very literal chip: expect to tidy it up in the editor", UI_T8, WINDOW_WIDTH / 2, y, t->dim);
    }
    Ui_TextCentered("Enter opens it in the editor    Esc back to the options", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
    Win_EndFrame();
}

/* --- the Timing test -------------------------------------------------------- */

#define CAL_LINE_X 120
#define CAL_LINE_W 720
#define CAL_LINE_Y 260

static int CalX(const RenderCalibrate *c, double beats) {
    double span = c->beats > 1 ? (double)(c->beats - 1) : 1.0;
    double x = CAL_LINE_X + beats / span * CAL_LINE_W;
    if (x < CAL_LINE_X - 24) x = CAL_LINE_X - 24;
    if (x > CAL_LINE_X + CAL_LINE_W + 24) x = CAL_LINE_X + CAL_LINE_W + 24;
    return (int)x;
}

void Render_Calibrate(const RenderCalibrate *c) {
    const UiTheme *t = Ui_Theme();
    char buf[128];
    Win_BeginFrame();
    ClearBackground(t->bg);
    Ui_TextCentered("Timing test", UI_T16, WINDOW_WIDTH / 2, 40, t->light);
    Ui_TextCentered("Tap Space with each click. The first four are to find the beat.", UI_T8, WINDOW_WIDTH / 2, 76, t->dim);
    snprintf(buf, sizeof(buf), "Timing now %d ms", c->currentMs);
    Ui_TextCentered(buf, UI_T8, WINDOW_WIDTH / 2, 96, t->faint);

    /* The beats along a line: warm-up ones fainter, each one lighting as it
     * sounds and the newest flashing; the taps under the line where they
     * fell, so a steady lag shows as a steady gap. */
    DrawRectangle(CAL_LINE_X - 24, CAL_LINE_Y + 7, CAL_LINE_W + 48, 1, t->rule);
    double beat = c->beatSeconds > 0.0 ? c->beatSeconds : 1.0;
    for (int k = 0; k < c->beats; k++) {
        double at = k * beat;
        bool sounded = c->now >= at, fresh = sounded && c->now < at + 0.12;
        Color col = !sounded ? t->faint : (k < c->warmup ? t->dim : t->text);
        if (fresh) col = t->light;
        int bx = CalX(c, (double)k);
        DrawRectangle(bx - 2, CAL_LINE_Y, 4, 16, col);
    }
    for (int i = 0; i < c->tapCount; i++) {
        int tx = CalX(c, c->taps[i] / beat);
        DrawRectangle(tx - 2, CAL_LINE_Y + 22, 4, 10, t->gold);
    }
    int warmEnd = CalX(c, (double)c->warmup - 0.5);
    Ui_TextCentered("find the beat", UI_T8, (CAL_LINE_X + warmEnd) / 2, CAL_LINE_Y + 44, t->faint);
    Ui_TextCentered("these count", UI_T8, (warmEnd + CAL_LINE_X + CAL_LINE_W) / 2, CAL_LINE_Y + 44, t->faint);

    const char *big = "", *small = "", *hints = "Space tap    Esc back";
    switch (c->phase) {
        case CALIBRATE_RUNNING:
            big = c->now < 0.0 ? "Listen..." : "Tap Space with the clicks";
            snprintf(buf, sizeof(buf), "%d tap%s", c->tapCount, c->tapCount == 1 ? "" : "s");
            small = c->now < 0.0 ? "" : buf;
            break;
        case CALIBRATE_DONE:
            snprintf(buf, sizeof(buf), "Your timing is %d ms. Enter keeps it", c->resultMs);
            big = buf;
            small = "What you play along with is caught that much earlier";
            hints = "Enter keep    Space try again    Esc back";
            break;
        case CALIBRATE_FAILED:
            big = "Not enough taps to tell";
            small = "Tap once with each click from the fifth on, then it can tell";
            hints = "Space try again    Esc back";
            break;
        case CALIBRATE_NO_SOUND:
            big = "No sound is playing";
            small = "The test needs the clicks to be heard: check the sound, then try again";
            hints = "Space try again    Esc back";
            break;
    }
    char fit[160];
    Ui_TextCentered(Fit(big, UI_T16, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T16, WINDOW_WIDTH / 2, 380,
                    c->phase == CALIBRATE_DONE ? t->light : t->text);
    if (small[0]) Ui_TextCentered(Fit(small, UI_T8, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, 416, t->dim);
    Ui_TextCentered(hints, UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
    Win_EndFrame();
}

/* --- prompt ---------------------------------------------------------------- */

void Render_Prompt(const char *title, const char *text, const char *hint) {
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Ui_TextCentered(title ? title : "", UI_T16, WINDOW_WIDTH / 2, 200, t->light);

    /* Wide enough for a full SONG_NAME_LEN-1 name plus the cursor at T16. */
    int boxW = 768, boxX = (WINDOW_WIDTH - boxW) / 2, boxY = 280;
    Ui_BevelRect(boxX, boxY, boxW, 48, t->board);
    char shown[SONG_NAME_LEN + 2];
    /* A blinking underscore marks the insertion point; GetTime keeps it steady
     * whatever the frame rate. */
    bool blink = ((int)(GetTime() * 2.0) & 1) == 0;
    snprintf(shown, sizeof(shown), "%s%s", text ? text : "", blink ? "_" : " ");
    Ui_Text(shown, boxX + 16, boxY + 16, UI_T16, t->text);

    char fit[160];
    if (hint) Ui_TextCentered(Fit(hint, UI_T8, WINDOW_WIDTH - 32, fit, sizeof(fit), false), UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, t->dim);
    Win_EndFrame();
}
