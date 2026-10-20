#include "jukebox.h"
#include <math.h>
#include <string.h>

int Jukebox_Length(const Song *s) {
    int n = s ? s->orderCount * SONG_STEPS : 0;
    return n > 0 ? n : 1;
}

static int Wrap(int step, int len) { return ((step % len) + len) % len; }

/* The pattern under a step (any step: the song goes round), and the step
 * inside it; NULL for an order entry naming no pattern. */
static const SongPattern *PatternAt(const Song *s, int step, int *inPattern) {
    step = Wrap(step, Jukebox_Length(s));
    *inPattern = step % SONG_STEPS;
    if (!s || s->orderCount <= 0) return NULL;
    int pos = step / SONG_STEPS;
    if (pos >= s->orderCount || pos >= SONG_MAX_ORDER) return NULL;
    int p = s->order[pos];
    return p < s->patternCount && p < SONG_MAX_PATTERNS ? &s->patterns[p] : NULL;
}

static int RawTone(const Song *s, int track, int step) {
    int in;
    const SongPattern *p = PatternAt(s, step, &in);
    return p ? p->tone[track][in] : NOTE_NONE;
}

static bool IsNote(int tone) { return tone >= NOTE_MIN && tone <= NOTE_MAX; }

double Jukebox_Playhead(const Song *s, double seconds) {
    if (!s || s->bpm <= 0 || !(seconds > 0.0)) return 0.0;
    return fmod(seconds / (double)Song_StepSeconds(s), (double)Jukebox_Length(s));
}

/* Back through the holds to the step that decides: a note there rings on,
 * silence there (the note let go) stays silent. */
JukeNote Jukebox_NoteAt(const Song *s, int track, int step) {
    JukeNote n = {NOTE_NONE, false};
    if (track < 0 || track >= SONG_TONE_TRACKS) return n;
    int len = Jukebox_Length(s);
    for (int back = 0; back < len; back++) {
        int t = RawTone(s, track, step - back);
        if (t == NOTE_HOLD) continue;
        if (IsNote(t)) n = (JukeNote){t, back == 0};
        return n;
    }
    return n; /* holds all the way round: nothing was ever struck */
}

static void Range(JukeWindow *w, int track) {
    w->lo[track] = w->hi[track] = NOTE_NONE;
    for (int c = 0; c < JUKE_STEPS; c++) {
        int n = w->tone[track][c].note;
        if (n == NOTE_NONE) continue;
        if (w->lo[track] == NOTE_NONE || n < w->lo[track]) w->lo[track] = n;
        if (w->hi[track] == NOTE_NONE || n > w->hi[track]) w->hi[track] = n;
        w->any = true;
    }
}

void Jukebox_Window(const Song *s, double seconds, JukeWindow *w) {
    memset(w, 0, sizeof(*w));
    int len = Jukebox_Length(s);
    w->playhead = Jukebox_Playhead(s, seconds);
    w->first = Wrap((int)floor(w->playhead) - JUKE_LEAD, len);
    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
        JukeNote prev = Jukebox_NoteAt(s, t, w->first); /* a note held in from before the window */
        w->tone[t][0] = prev;
        for (int c = 1; c < JUKE_STEPS; c++) {
            int raw = RawTone(s, t, w->first + c);
            JukeNote n = {NOTE_NONE, false};
            if (raw == NOTE_HOLD) n = (JukeNote){prev.note, false};
            else if (IsNote(raw)) n = (JukeNote){raw, true};
            w->tone[t][c] = prev = n;
        }
        Range(w, t);
    }
    for (int c = 0; c < JUKE_STEPS; c++) {
        int in;
        const SongPattern *p = PatternAt(s, w->first + c, &in);
        w->drums[c] = p ? p->drums[in] : 0;
        if (w->drums[c]) w->any = true;
    }
}
