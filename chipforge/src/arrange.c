#include "arrange.h"
#include <string.h>

static int Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

/* One Grow builds several parts from one seed; each part gets its own
 * sub-seed so changing how one part draws cannot shift another's take.
 * Callers often pass consecutive seeds (R for a new take adds one), so the
 * mix is a full avalanche finaliser rather than a multiply: neighbouring
 * seeds must not give neighbouring takes. */
static uint32_t Mix(uint32_t seed, uint32_t salt) {
    uint32_t x = seed ^ (salt * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* Moves a note by octaves into the song's range. Unlike clamping it keeps
 * the pitch class, so a note that was in key or a chord tone still is. */
static int FoldIntoRange(int n) {
    while (n < NOTE_MIN) n += 12;
    while (n > NOTE_MAX) n -= 12;
    return n;
}

static bool HasOnset(const int8_t *notes, int from, int to) {
    for (int st = from; st < to; st++) if (notes[st] >= 0) return true;
    return false;
}

static int MaxNote(const int8_t *notes) {
    int best = -1;
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] > best) best = notes[st];
    return best;
}

static void ShiftOnsets(int8_t *notes, int delta) {
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] >= 0) notes[st] = (int8_t)(notes[st] + delta);
}

/* Drum hits are counted per instrument, so a step with a kick and a hat is
 * two: that is what the ear counts when a beat thins out or fills up. */
static int DrumHits(const uint8_t *drums, int from, int to) {
    int n = 0;
    for (int st = from; st < to; st++) {
        n += (drums[st] & DRUM_KICK) != 0;
        n += (drums[st] & DRUM_SNARE) != 0;
        n += (drums[st] & DRUM_HAT) != 0;
    }
    return n;
}

static bool TrackEqual(const SongPattern *a, const SongPattern *b, int track) {
    return memcmp(a->tone[track], b->tone[track], sizeof(a->tone[track])) == 0;
}

/* The candidate for degree `to` (to, or to plus or minus 7) nearest `from`,
 * so a chord change is read as the short step it sounds like (vii up to i,
 * not down a seventh). The same rule Compose_Reharmonise moves the bass by. */
static int NearestDegree(int from, int to) {
    int best = to, bestDist = 99;
    for (int cand = to - 7; cand <= to + 7; cand += 7) {
        int dist = cand > from ? cand - from : from - cand;
        if (dist < bestDist) { bestDist = dist; best = cand; }
    }
    return best;
}

static int DegreeRootPc(ComposeKey k, int degree) {
    return Mod(Compose_DegreeNote(k, Mod(k.root, 12), Mod(degree, 7)), 12);
}

/* The note nearest n that is the root of the chord on this degree (a tie
 * goes down, as Compose_SnapToKey's do), kept in the song's range. */
static int NearestChordRoot(ComposeKey k, int degree, int n) {
    int down = n - Mod(n - DegreeRootPc(k, degree), 12), up = down + 12;
    return FoldIntoRange(n - down <= up - n ? down : up);
}

/* The nearest tone of the chord on this degree, looking down first. */
static int NearestChordTone(ComposeKey k, int degree, int n) {
    for (int dist = 0; dist <= 6; dist++) {
        if (n - dist >= NOTE_MIN && Compose_IsChordTone(k, degree, n - dist)) return n - dist;
        if (n + dist <= NOTE_MAX && Compose_IsChordTone(k, degree, n + dist)) return n + dist;
    }
    return Compose_SnapToKey(k, n);
}

/* A scale degree in the register Compose_Lead writes at: degree d + 7 is the
 * chord root a tune of that leadOctave ends a phrase on. */
static int LeadNote(ComposeKey k, int tonic, int leadOctave, int degree) {
    return FoldIntoRange(Compose_DegreeNote(k, tonic, degree) + 12 * (leadOctave - 2));
}

/* ------------------------------------------------------------- the parts */

/* The rolls a fill lays over the last two beats of the bar (steps 8..15).
 * Each ends on a snare, so the fill lands and the next pattern's downbeat
 * reads as a new start. */
static const uint8_t kRolls[3][8] = {
    {DRUM_KICK | DRUM_SNARE, 0, DRUM_SNARE, 0, DRUM_SNARE, DRUM_SNARE, DRUM_SNARE, DRUM_SNARE},
    {DRUM_KICK, 0, DRUM_SNARE, DRUM_KICK, DRUM_SNARE, DRUM_SNARE, DRUM_KICK | DRUM_SNARE, DRUM_SNARE},
    {DRUM_SNARE, 0, DRUM_SNARE, DRUM_SNARE, 0, DRUM_SNARE, DRUM_SNARE, DRUM_SNARE},
};

/* The last bar becomes a fill: a snare roll laid over the groove (the
 * groove is kept, so the fill grows out of it), and the tune's last beat a
 * four-note scale run into the chord root the next pattern opens on, coming
 * from the side the tune is already on so it never leaps to start the run.
 * A pattern with no tune gets no run: a lead that comes from nowhere for one
 * beat would sound like a mistake. */
static void FillLastBar(SongPattern *p, ComposeKey k, int tonic, int leadOctave, int nextDegree, uint32_t seed) {
    uint8_t *dr = p->drums + 48;
    int before = DrumHits(dr, 0, 16);
    const uint8_t *roll = kRolls[seed % 3u];
    for (int i = 0; i < 8; i++) dr[8 + i] |= roll[i];
    /* A bar that already had every hit of that roll would not change, and a
     * fill that adds nothing is not heard as one: snares go in from the end
     * backwards until it is busier than it was. */
    for (int i = 15; i >= 0 && DrumHits(dr, 0, 16) <= before; i--) dr[i] |= DRUM_SNARE;

    int8_t *lead = p->tone[TRACK_LEAD];
    if (!HasOnset(lead, 0, SONG_STEPS)) return;
    int target = LeadNote(k, tonic, leadOctave, Mod(nextDegree, 7) + 7);
    int prev = target;
    for (int st = 59; st >= 0; st--) if (lead[st] >= 0) { prev = lead[st]; break; }
    int dir = prev > target ? 1 : -1;
    for (int j = 0; j < 4; j++) lead[60 + j] = (int8_t)Compose_ScaleStep(k, target, dir * (4 - j));
}

/* A new tune on these chords at the register the old one sat in. Seeds
 * that happen to give back the old tune (a theme's own seed on its own
 * chords does) are skipped, since "a new tune" that is the old one would
 * look broken. */
static void NewLead(SongPattern *p, const SongPattern *src, ComposeKey k, int tonic, const ComposeChords *c, int leadOctave, uint32_t seed) {
    for (uint32_t i = 0; i < 16; i++) {
        Compose_Lead(p, k, tonic, c, leadOctave, seed + i * 7919u);
        if (!TrackEqual(p, src, TRACK_LEAD)) return;
    }
}

/* Bars 3-4 of the tune echo bars 1-2: the same rhythm, each note moved by
 * the scale steps between the chords (so a chord tone stays the same tone
 * of the new chord), and the last note on its bar's chord root so the
 * phrase closes. That call and response is the shape most melodies people
 * remember have. */
static void VaryAnswer(SongPattern *p, ComposeKey k, int tonic, const ComposeChords *c, int leadOctave, uint32_t seed) {
    int8_t *lead = p->tone[TRACK_LEAD];
    if (!HasOnset(lead, 0, 32)) {
        /* Nothing to echo: the call comes from a fresh tune's first half. */
        SongPattern tmp = *p;
        Compose_Lead(&tmp, k, tonic, c, leadOctave, seed);
        memcpy(lead, tmp.tone[TRACK_LEAD], 32);
    }
    for (int st = 0; st < 32; st++) {
        int bar = st / 16;
        int from = c->degree[bar];
        int shift = NearestDegree(from, c->degree[bar + 2]) - from;
        int v = lead[st];
        lead[st + 32] = (int8_t)(v >= 0 ? Compose_ScaleStep(k, v, shift) : v);
    }
    int last = -1;
    for (int st = 32; st < SONG_STEPS; st++) if (lead[st] >= 0) last = st;
    if (last < 0) return;
    lead[last] = (int8_t)NearestChordRoot(k, c->degree[last / 16], lead[last]);

    /* On a progression whose second half repeats its first (i iv i iv) the
     * echo can come out note for note, which is a repeat, not an answer:
     * the first note that is not the closing one goes up a third (still a
     * tone of a chord it was the root of), or the closing root changes
     * octave when it is the only note. */
    if (memcmp(lead, lead + 32, 32) != 0) return;
    for (int st = 32; st < last; st++) {
        if (lead[st] < 0) continue;
        int up = Compose_ScaleStep(k, lead[st], 2);
        lead[st] = (int8_t)(up != lead[st] ? up : Compose_ScaleStep(k, lead[st], -2));
        return;
    }
    lead[last] = (int8_t)(lead[last] + 12 <= NOTE_MAX ? lead[last] + 12 : lead[last] - 12);
}

/* The arp style a lift moves to from each style: always a sixteenth-note
 * one, a step busier than where it was. */
static const int kLiftArp[6] = {1, 3, 0, 1, 1, 0};
/* An octave up stops at C7: above it the pulse voices turn shrill. */
#define LIFT_CEILING 96

/* A chorus lift. Games play only bass and arp when calm, so the lift has to
 * change one of those or a calm level would not hear it at all: the bass
 * drives in sixteenths and the arp goes up an octave or to a busier style.
 * Hats double (eighths, or sixteenths when they were already eighths) and
 * the tune goes up an octave when there is room. */
static void VaryLift(SongPattern *p, const SongPattern *src, ComposeKey k, int tonic, const ComposeChords *c, uint32_t seed) {
    Compose_Bass(p, k, tonic, c, 3);

    int8_t *arp = p->tone[TRACK_ARP];
    bool octaveFits = HasOnset(arp, 0, SONG_STEPS) && MaxNote(arp) + 12 <= LIFT_CEILING;
    int style = kLiftArp[Compose_GuessStyle(src, k, TRACK_ARP)];
    if (octaveFits && (seed & 1u) == 0) ShiftOnsets(arp, 12);
    else Compose_Arp(p, k, tonic, c, style);
    /* A source that already drives and sounds like the lift's arp would
     * come out the same; the next style along always differs. */
    for (int s = 1; s < 6 && TrackEqual(p, src, TRACK_BASS) && TrackEqual(p, src, TRACK_ARP); s++)
        Compose_Arp(p, k, tonic, c, (style + s) % 6);

    bool anyDrums = false;
    for (int st = 0; st < SONG_STEPS; st++) if (src->drums[st]) anyDrums = true;
    if (anyDrums) {
        /* A pattern with no beat at all was written that way on purpose. */
        for (int bar = 0; bar < 4; bar++) {
            uint8_t *dr = p->drums + bar * 16;
            int hats = 0;
            for (int i = 0; i < 16; i++) hats += (dr[i] & DRUM_HAT) != 0;
            for (int i = 0; i < 16; i++) if (hats > 4 || i % 2 == 0) dr[i] |= DRUM_HAT;
        }
    }

    int8_t *lead = p->tone[TRACK_LEAD];
    if (HasOnset(lead, 0, SONG_STEPS) && MaxNote(lead) + 12 <= LIFT_CEILING) ShiftOnsets(lead, 12);
}

/* The half-time beat a breakdown keeps, most important hit first: kick on
 * one and snare on three of each bar, then hats on the off beats. A drop
 * keeps as many as half the source's hits allow, so it is always clearly
 * emptier than what it breaks down from. */
static const struct { uint8_t step, bit; } kHalfTime[] = {
    {0, DRUM_KICK},  {8, DRUM_SNARE},  {32, DRUM_KICK}, {40, DRUM_SNARE},
    {16, DRUM_KICK}, {24, DRUM_SNARE}, {48, DRUM_KICK}, {56, DRUM_SNARE},
    {4, DRUM_HAT},   {36, DRUM_HAT},   {20, DRUM_HAT},  {52, DRUM_HAT},
    {12, DRUM_HAT},  {44, DRUM_HAT},   {28, DRUM_HAT},  {60, DRUM_HAT},
};

/* A breakdown: half-time drums, the bass in long held notes, and the tune
 * thinned to one short note per half bar, each the chord tone nearest the
 * first note the tune played there. The tune's notes are eighths left to
 * decay rather than held: the long bass already sustains, and a held pulse
 * lead on top made the breakdown the loudest part of the loop (rendered,
 * about a quarter above the source by RMS) instead of the quiet one. The
 * arp keeps going: it is what holds a breakdown together. */
static void VaryDrop(SongPattern *p, const SongPattern *src, ComposeKey k, int tonic, const ComposeChords *c) {
    Compose_Bass(p, k, tonic, c, 5);

    int budget = DrumHits(src->drums, 0, SONG_STEPS) / 2;
    memset(p->drums, 0, sizeof(p->drums));
    int n = (int)(sizeof(kHalfTime) / sizeof(kHalfTime[0]));
    for (int i = 0; i < n && i < budget; i++) p->drums[kHalfTime[i].step] |= kHalfTime[i].bit;

    const int8_t *in = src->tone[TRACK_LEAD];
    int8_t *lead = p->tone[TRACK_LEAD];
    memset(lead, NOTE_NONE, sizeof(p->tone[TRACK_LEAD]));
    for (int half = 0; half < 8; half++) {
        int base = half * 8, first = -1;
        for (int i = 0; i < 8 && first < 0; i++) if (in[base + i] >= 0) first = in[base + i];
        if (first < 0) continue;
        lead[base] = (int8_t)NearestChordTone(k, c->degree[half / 2], first);
        lead[base + 1] = NOTE_HOLD;
    }
}

/* An intro: the backing alone, the beat (without its snare, which would
 * make it sound like the song proper) coming in for the second half. */
static void VaryIntro(SongPattern *p) {
    memset(p->tone[TRACK_LEAD], NOTE_NONE, sizeof(p->tone[TRACK_LEAD]));
    for (int st = 0; st < SONG_STEPS; st++) {
        if (st < 32) p->drums[st] = 0;
        else p->drums[st] &= (uint8_t)~DRUM_SNARE;
    }
}

/* An outro: the chords turned to end on the tonic (from V, IV or VII, the
 * approaches that sound final), and the last bar one held tonic chord: the
 * bass root, the arp's held chord tones, the tune's tonic, one kick. */
static void VaryOutro(SongPattern *p, ComposeKey k, int tonic, int leadOctave, const ComposeChords *c) {
    ComposeChords o = *c;
    int c2 = c->degree[2];
    o.degree[2] = (c2 == 3 || c2 == 4 || c2 == 6) ? c2 : 4;
    o.degree[3] = 0;
    bool hadLead = HasOnset(p->tone[TRACK_LEAD], 0, SONG_STEPS);
    bool hadDrums = false;
    for (int st = 0; st < SONG_STEPS; st++) if (p->drums[st]) hadDrums = true;
    Compose_Reharmonise(p, k, c, &o, true);

    int8_t *bass = p->tone[TRACK_BASS] + 48;
    bass[0] = (int8_t)NearestChordRoot(k, 0, tonic - 12);
    for (int i = 1; i < 16; i++) bass[i] = NOTE_HOLD;

    SongPattern held = *p;
    const ComposeChords home = {{0, 0, 0, 0}};
    Compose_Arp(&held, k, tonic, &home, 5);
    memcpy(p->tone[TRACK_ARP] + 48, held.tone[TRACK_ARP] + 48, 16);

    int8_t *lead = p->tone[TRACK_LEAD] + 48;
    memset(lead, NOTE_NONE, 16);
    if (hadLead) {
        lead[0] = (int8_t)LeadNote(k, tonic, leadOctave, 7);
        for (int i = 1; i < 16; i++) lead[i] = NOTE_HOLD;
    }

    memset(p->drums + 48, 0, 16);
    if (hadDrums) p->drums[48] = DRUM_KICK;
}

/* A turnaround: the last bar moved onto V (onto I when the loop starts on IV
 * or V, where V would not lead anywhere), the bar before onto IV or V when
 * it would otherwise sit on that same chord, the tune fitted, and a fill
 * running into the first chord. Played last in a loop, it makes the jump
 * back to the start sound like an arrival instead of a splice. */
static void VaryTurnaround(SongPattern *p, ComposeKey k, int tonic, int leadOctave, const ComposeChords *c, int nextDegree, uint32_t seed) {
    ComposeChords t = *c;
    int c0 = c->degree[0];
    int last = (c0 == 3 || c0 == 4) ? 0 : 4;
    t.degree[3] = last;
    if (t.degree[2] == last) t.degree[2] = last == 4 ? 3 : 4;
    Compose_Reharmonise(p, k, c, &t, true);
    FillLastBar(p, k, tonic, leadOctave, nextDegree, seed);
}

/* --------------------------------------------------------------- variations */

static const char *const kVaryNames[VARY_COUNT] = {
    "Fill at the end", "Answer", "New tune", "Lift", "Drop", "Intro", "Turnaround", "Outro",
};

const char *Arrange_VaryName(VaryKind v) {
    int i = (int)v;
    return (i >= 0 && i < VARY_COUNT) ? kVaryNames[i] : "";
}

void Arrange_VaryInto(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *c, VaryKind kind, uint32_t seed, int nextDegree, SongPattern *out) {
    if (!src || !out) return;
    const SongPattern in = *src; /* out may be src */
    SongPattern p = in;
    ComposeChords chords;
    if (c) chords = *c;
    else Compose_ReadChords(&in, k, &chords);
    for (int b = 0; b < 4; b++) chords.degree[b] = Mod(chords.degree[b], 7);
    int tonic = tonicMidi > 0 ? tonicMidi : Compose_TonicMidi(k, &in);
    int leadOctave = Compose_LeadOctave(k, tonic, &in);
    int next = nextDegree >= 0 ? Mod(nextDegree, 7) : chords.degree[0];

    switch (kind) {
        case VARY_FILL: FillLastBar(&p, k, tonic, leadOctave, next, seed); break;
        case VARY_ANSWER: VaryAnswer(&p, k, tonic, &chords, leadOctave, seed); break;
        case VARY_NEW_TUNE: NewLead(&p, &in, k, tonic, &chords, leadOctave, seed); break;
        case VARY_LIFT: VaryLift(&p, &in, k, tonic, &chords, seed); break;
        case VARY_DROP: VaryDrop(&p, &in, k, tonic, &chords); break;
        case VARY_INTRO: VaryIntro(&p); break;
        case VARY_TURNAROUND: VaryTurnaround(&p, k, tonic, leadOctave, &chords, next, seed); break;
        case VARY_OUTRO: VaryOutro(&p, k, tonic, leadOctave, &chords); break;
        default: break; /* an unknown kind leaves the copy as it was */
    }
    *out = p;
}

void Arrange_Vary(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *c, VaryKind kind, uint32_t seed, SongPattern *out) {
    Arrange_VaryInto(src, k, tonicMidi, c, kind, seed, -1, out);
}

void Arrange_Chorus(const SongPattern *src, ComposeKey k, int tonicMidi, const ComposeChords *from, const ComposeChords *to, uint32_t seed, SongPattern *out) {
    if (!src || !out) return;
    const SongPattern in = *src;
    ComposeChords f, t;
    if (from) f = *from;
    else Compose_ReadChords(&in, k, &f);
    if (to) t = *to;
    else t = Compose_ProgressionAt(Arrange_AnswerProgression(&f))->chords;
    int tonic = tonicMidi > 0 ? tonicMidi : Compose_TonicMidi(k, &in);
    int leadOctave = Compose_LeadOctave(k, tonic, &in);
    SongPattern p = in;
    /* The tune is written fresh, so fitting the old one first would be wasted. */
    Compose_Reharmonise(&p, k, &f, &t, false);
    NewLead(&p, &in, k, tonic, &t, leadOctave, seed);
    *out = p;
}

static int BarsChanged(const ComposeChords *a, const ComposeChords *b) {
    int n = 0;
    for (int bar = 0; bar < 4; bar++) n += Mod(a->degree[bar], 7) != Mod(b->degree[bar], 7);
    return n;
}

int Arrange_AnswerProgression(const ComposeChords *c) {
    ComposeChords verse = {{0, 0, 0, 0}};
    if (c) for (int b = 0; b < 4; b++) verse.degree[b] = Mod(c->degree[b], 7);
    int n = Compose_ProgressionCount();
    /* A chorus that opens away from home (IV or VI) is the classic lift:
     * the ear hears the change of section before the tune even starts. */
    for (int i = 0; i < n; i++) {
        const ComposeChords *p = &Compose_ProgressionAt(i)->chords;
        int open = Mod(p->degree[0], 7);
        if ((open == 3 || open == 5) && BarsChanged(p, &verse) > 0) return i;
    }
    /* The verse itself opens there: then a partner that starts elsewhere and
     * moves most of the bars, which still sounds like somewhere new. */
    for (int i = 0; i < n; i++) {
        const ComposeChords *p = &Compose_ProgressionAt(i)->chords;
        if (Mod(p->degree[0], 7) != verse.degree[0] && BarsChanged(p, &verse) >= 3) return i;
    }
    int best = 0, bestChanged = -1;
    for (int i = 0; i < n; i++) {
        int changed = BarsChanged(&Compose_ProgressionAt(i)->chords, &verse);
        if (changed > bestChanged) { bestChanged = changed; best = i; }
    }
    return best;
}

/* -------------------------------------------------------------------- grow */

/* The parts a shape is made of. A is the source itself; the rest are built. */
typedef enum { ROLE_A, ROLE_AF, ROLE_B, ROLE_BF, ROLE_LIFT, ROLE_DROP, ROLE_TURN, ROLE_INTRO, ROLE_OUTRO, ROLE_COUNT } Role;

#define ARRANGE_MAX_ADDED 7
#define ARRANGE_MAX_PASSES 16

typedef struct {
    const char *name;
    float seconds;
    int added;
    Role parts[ARRANGE_MAX_ADDED];   /* the new patterns, in the order they are appended */
    int passes;
    Role order[ARRANGE_MAX_PASSES];
} Shape;

/* At 120 bpm a pattern lasts 8 s, so the pass counts land within 25% of
 * each target (32, 64, 128, 72 s). A fill always comes right before the
 * other family of parts (A-type into B-type and back), because that is
 * the chord it runs into; the loops end on T so the wrap back to A lands. */
static const Shape kShapes[GROW_COUNT] = {
    {"Short loop, about 30 s", 30.0f, 2, {ROLE_BF, ROLE_TURN},
     4, {ROLE_A, ROLE_BF, ROLE_A, ROLE_TURN}},
    {"Game loop, about 1 min", 60.0f, 6, {ROLE_AF, ROLE_BF, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_TURN},
     8, {ROLE_A, ROLE_AF, ROLE_BF, ROLE_A, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_TURN}},
    {"Long loop, about 2 min", 120.0f, 6, {ROLE_AF, ROLE_BF, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_TURN},
     16, {ROLE_A, ROLE_AF, ROLE_BF, ROLE_A, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_A,
          ROLE_AF, ROLE_B, ROLE_BF, ROLE_LIFT, ROLE_LIFT, ROLE_B, ROLE_BF, ROLE_TURN}},
    {"Song with an ending, for a WAV", 75.0f, 7, {ROLE_INTRO, ROLE_AF, ROLE_BF, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_OUTRO},
     9, {ROLE_INTRO, ROLE_A, ROLE_AF, ROLE_BF, ROLE_LIFT, ROLE_B, ROLE_DROP, ROLE_A, ROLE_OUTRO}},
};

/* The order is rewritten, never appended to, so the longest pass list is
 * what has to fit. */
_Static_assert(ARRANGE_MAX_PASSES <= SONG_MAX_ORDER, "every shape's pass list fits the order");
_Static_assert(ARRANGE_MAX_ADDED <= SONG_MAX_PATTERNS - 1, "a one-pattern song can always grow");

static const Shape *ShapeAt(GrowShape g) {
    int i = (int)g;
    return (i >= 0 && i < GROW_COUNT) ? &kShapes[i] : NULL;
}

const char *Arrange_ShapeName(GrowShape g) { const Shape *s = ShapeAt(g); return s ? s->name : ""; }
float Arrange_TargetSeconds(GrowShape g) { const Shape *s = ShapeAt(g); return s ? s->seconds : 0.0f; }
int Arrange_PatternsNeeded(GrowShape g) { const Shape *s = ShapeAt(g); return s ? s->added : 0; }
int Arrange_PassCount(GrowShape g) { const Shape *s = ShapeAt(g); return s ? s->passes : 0; }

bool Arrange_Grow(Song *s, int sourcePattern, ComposeKey k, GrowShape shape, uint32_t seed) {
    const Shape *sh = ShapeAt(shape);
    if (!s || !sh) return false;
    if (s->patternCount < 1 || s->patternCount > SONG_MAX_PATTERNS) return false;
    if (sourcePattern < 0 || sourcePattern >= s->patternCount) return false;
    if (s->patternCount + sh->added > SONG_MAX_PATTERNS || sh->passes > SONG_MAX_ORDER) return false;

    /* Everything is built from a copy before the song is touched, so a
     * refusal above is the only way out and the song is either grown whole
     * or left exactly as it was. */
    const SongPattern a = s->patterns[sourcePattern];
    ComposeChords c;
    Compose_ReadChords(&a, k, &c);
    for (int bar = 0; bar < 4; bar++) c.degree[bar] = Mod(c.degree[bar], 7);
    int tonic = Compose_TonicMidi(k, &a);
    const ComposeChords *answer = &Compose_ProgressionAt(Arrange_AnswerProgression(&c))->chords;
    SongPattern b;
    Arrange_Chorus(&a, k, tonic, &c, answer, Mix(seed, 1), &b);

    SongPattern built[ARRANGE_MAX_ADDED];
    int index[ROLE_COUNT];
    for (int r = 0; r < ROLE_COUNT; r++) index[r] = sourcePattern;
    for (int i = 0; i < sh->added; i++) {
        SongPattern *out = &built[i];
        switch (sh->parts[i]) {
            case ROLE_AF: Arrange_VaryInto(&a, k, tonic, &c, VARY_FILL, Mix(seed, 2), answer->degree[0], out); break;
            case ROLE_B: *out = b; break;
            case ROLE_BF: Arrange_VaryInto(&b, k, tonic, answer, VARY_FILL, Mix(seed, 3), c.degree[0], out); break;
            case ROLE_LIFT: Arrange_Vary(&a, k, tonic, &c, VARY_LIFT, Mix(seed, 4), out); break;
            case ROLE_DROP: Arrange_Vary(&b, k, tonic, answer, VARY_DROP, Mix(seed, 5), out); break;
            case ROLE_TURN: Arrange_VaryInto(&a, k, tonic, &c, VARY_TURNAROUND, Mix(seed, 6), c.degree[0], out); break;
            case ROLE_INTRO: Arrange_Vary(&a, k, tonic, &c, VARY_INTRO, Mix(seed, 7), out); break;
            case ROLE_OUTRO: Arrange_Vary(&a, k, tonic, &c, VARY_OUTRO, Mix(seed, 8), out); break;
            default: *out = a; break;
        }
        index[sh->parts[i]] = s->patternCount + i;
    }

    int base = s->patternCount;
    for (int i = 0; i < sh->added; i++) s->patterns[base + i] = built[i];
    s->patternCount = base + sh->added;
    for (int i = 0; i < sh->passes; i++) s->order[i] = (uint8_t)index[sh->order[i]];
    int oldOrder = s->orderCount;
    s->orderCount = sh->passes;
    /* Entries past the new end are cleared (after the count, so a player
     * never reads one as live), the way a parsed song has them: then the
     * grown song saves and loads back byte for byte, and the editor's
     * "did anything change" memcmp sees only real changes. */
    for (int i = sh->passes; i < oldOrder && i < SONG_MAX_ORDER; i++) s->order[i] = 0;
    return true;
}
