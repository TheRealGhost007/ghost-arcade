#include "compose.h"
#include "musicsynth.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ComposeKey.scale and the song's key line are MusicScale values; the games'
 * synth is compiled from the same header, so this holds them to one order. */
_Static_assert(SCALE_MINOR == 0 && SCALE_MAJOR == 1 && SCALE_DORIAN == 2 && SCALE_PHRYGIAN == 3 &&
               SCALE_HARMONIC == 4 && SCALE_MIXOLYDIAN == 5 && SCALE_COUNT == SONG_SCALE_COUNT,
               "ComposeKey.scale and Song.keyScale follow the MusicScale order");

/* The same table as musicsynth.c's kScales. It is duplicated rather than
 * exported so ghost-common keeps its interface; the theme-lock test catches
 * the two drifting apart. */
static const int kScales[SONG_SCALE_COUNT][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10},
    {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 5, 7, 9, 10},
};

static int Mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int ScaleOf(ComposeKey k) { return ClampI(k.scale, 0, SONG_SCALE_COUNT - 1); }
static int RootOf(ComposeKey k) { return Mod(k.root, 12); }
/* Every note a writer stores or a chord change moves is kept to the song's
 * range rather than MIDI's, so a writer at the edge of its register (a tune
 * at lead octave 4 or 5, a walking bass under a low tonic) cannot write a
 * note the editor would refuse. A stray note moves an octave back in rather
 * than stopping at the end: that keeps its pitch class, so it stays in key
 * and a chord tone stays a chord tone, where a clamp would leave C1 or C8 in
 * a key without C. The themes never leave the range, so the port stays
 * byte-exact. */
static int FoldIntoRange(int n) {
    while (n < NOTE_MIN) n += 12;
    while (n > NOTE_MAX) n -= 12;
    return n;
}
static int8_t Note8(int n) { return (int8_t)FoldIntoRange(n); }

/* The theme composer's generator, kept bit for bit so the lead of every game
 * theme comes out the same. */
static uint32_t Lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

int Compose_DegreeNote(ComposeKey k, int tonicMidi, int deg) {
    int oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    int idx = deg - oct * 7;
    return tonicMidi + kScales[ScaleOf(k)][idx] + oct * 12;
}

/* Chord tone t (0 root, 1 third, 2 fifth) of the chord rooted on degree d. */
static int ChordTone(ComposeKey k, int tonicMidi, int d, int t) { return Compose_DegreeNote(k, tonicMidi, d + t * 2); }

static int DegreePitchClass(ComposeKey k, int deg) { return Mod(RootOf(k) + kScales[ScaleOf(k)][Mod(deg, 7)], 12); }

int Compose_NoteDegree(ComposeKey k, int note) {
    if (note < 0) return -1;
    int pc = Mod(note, 12);
    for (int d = 0; d < 7; d++) if (DegreePitchClass(k, d) == pc) return d;
    return -1;
}

bool Compose_InKey(ComposeKey k, int note) { return Compose_NoteDegree(k, note) >= 0; }

bool Compose_IsChordTone(ComposeKey k, int degree, int note) {
    if (note < 0) return false;
    int pc = Mod(note, 12);
    for (int t = 0; t < 3; t++) if (DegreePitchClass(k, degree + t * 2) == pc) return true;
    return false;
}

/* ------------------------------------------------------------------- styles */

static const char *const kBassNames[6] = {"Steady", "Bouncy", "Jumping", "Driving", "Walking", "Long notes"};
static const char *const kArpNames[6] = {"Up", "Up and down", "Skipping", "Broken", "Stabs", "Held"};
static const char *const kDrumNames[6] = {"Dance", "Rock", "Laid back", "Breakbeat", "Shuffle", "Gallop"};
static const char *const kLeadNames[2] = {"New tune", "Fit my rhythm"};

int Compose_StyleCount(SongTrack track) { return track == TRACK_LEAD ? 2 : 6; }

const char *Compose_StyleName(SongTrack track, int style) {
    if (style < 0 || style >= Compose_StyleCount(track)) return "";
    switch (track) {
        case TRACK_BASS: return kBassNames[style];
        case TRACK_ARP: return kArpNames[style];
        case TRACK_LEAD: return kLeadNames[style];
        default: return kDrumNames[style];
    }
}

/* Chord degrees are 0..6; the walking bass wants the next bar's root as the
 * degree nearest the current one, so a step from vii to i goes up, not down a
 * seventh. */
static int NearestDegree(int from, int to) {
    int best = to, bestDist = 99;
    for (int cand = to - 7; cand <= to + 7; cand += 7) {
        int dist = cand > from ? cand - from : from - cand;
        if (dist < bestDist) { bestDist = dist; best = cand; }
    }
    return best;
}

void Compose_Bass(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int style) {
    int8_t *bass = p->tone[TRACK_BASS];
    style = ClampI(style, 0, 5);
    memset(bass, NOTE_NONE, SONG_STEPS);
    for (int bar = 0; bar < 4; bar++) {
        int d = c->degree[bar];
        int root = ChordTone(k, tonicMidi, d, 0) - 12;
        int third = ChordTone(k, tonicMidi, d, 1) - 12;
        int fifth = ChordTone(k, tonicMidi, d, 2) - 12;
        /* Walking: the fourth beat leads into the next bar's root from the
         * scale step beside it, from below when the chord stays or rises and
         * from above when it falls. (Not the fifth when it stays: two beats on
         * the fifth would outvote the root when the bar's chord is read back.) */
        int nd = NearestDegree(d, c->degree[(bar + 1) % 4]);
        int approach = Compose_DegreeNote(k, tonicMidi, nd >= d ? nd - 1 : nd + 1) - 12;
        for (int i = 0; i < 16; i++) {
            int st = bar * 16 + i;
            bool on = false;
            int note = root;
            switch (style) {
                case 0: on = (i % 2) == 0; break;
                case 1: on = (i % 4) == 2 || i == 0; break;
                case 2: on = (i % 4) == 0 || i == 6 || i == 14; note = (i == 6 || i == 14) ? root + 12 : root; break;
                case 3: on = true; note = (i % 4 == 2) ? root + 12 : root; break;
                case 4: {
                    /* Walking: root, third, fifth, then the approach note. */
                    on = (i % 4) == 0;
                    int beat = i / 4;
                    note = beat == 0 ? root : (beat == 1 ? third : (beat == 2 ? fifth : approach));
                    break;
                }
                default:
                    /* Long notes: the root for half a bar, then the fifth,
                     * each held so the triangle sustains instead of decaying. */
                    if (i == 0) { on = true; note = root; }
                    else if (i == 8) { on = true; note = fifth; }
                    else bass[st] = NOTE_HOLD;
                    break;
            }
            if (on) bass[st] = Note8(note);
        }
    }
}

void Compose_Arp(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int style) {
    int8_t *arp = p->tone[TRACK_ARP];
    style = ClampI(style, 0, 5);
    memset(arp, NOTE_NONE, SONG_STEPS);
    for (int bar = 0; bar < 4; bar++) {
        int d = c->degree[bar];
        for (int i = 0; i < 16; i++) {
            int st = bar * 16 + i;
            int t = 0;
            bool on = true;
            switch (style) {
                case 0: t = i % 3; break;
                case 1: { static const int up[6] = {0, 1, 2, 1, 0, 1}; t = up[i % 6]; break; }
                case 2: { static const int sk[4] = {0, 2, 1, 2}; t = sk[i % 4]; on = (i % 2) == 0; break; }
                case 3: { static const int br[8] = {0, 2, 1, 2, 0, 1, 2, 1}; t = br[i % 8]; break; }
                case 4:
                    /* Stabs: a quick three-note burst on beats one and three,
                     * the fifth alone on the off sixteenth before each. */
                    if (i < 3) t = i;
                    else if (i >= 8 && i < 11) t = i - 8;
                    else if (i == 6 || i == 14) t = 2;
                    else on = false;
                    break;
                default: {
                    /* Held: one chord tone a beat, sustained. */
                    static const int held[4] = {0, 1, 2, 1};
                    if (i % 4 == 0) t = held[i / 4];
                    else { on = false; arp[st] = NOTE_HOLD; }
                    break;
                }
            }
            if (on) arp[st] = Note8(ChordTone(k, tonicMidi, d, t) + 12);
        }
    }
}

void Compose_Drums(SongPattern *p, int style, uint32_t seed) {
    style = ClampI(style, 0, 5);
    for (int bar = 0; bar < 4; bar++) {
        /* Ghost notes for the two new styles: one extra kick or snare a bar,
         * never beside another kick, chosen per bar so the four bars differ. */
        uint32_t rng = seed * 2654435761u + (uint32_t)bar * 40503u + 7919u;
        int ghostKick = -1, ghostSnare = -1;
        if (style == 4) {
            static const int kicks[4] = {-1, 10, 11, 14};
            ghostKick = kicks[Lcg(&rng) % 4];
            if (Lcg(&rng) % 3 == 0) ghostSnare = 15;
        } else if (style == 5) {
            uint32_t g = Lcg(&rng) % 3;
            if (g == 1) ghostSnare = 11;
            else if (g == 2) ghostKick = 10;
        }
        for (int i = 0; i < 16; i++) {
            uint8_t dr = 0;
            switch (style) {
                case 0: if (i % 4 == 0) dr |= DRUM_KICK; if (i % 2 == 1 || i % 4 == 2) dr |= DRUM_HAT; if (i % 8 == 4) dr |= DRUM_SNARE; break;
                case 1: if (i == 0 || i == 10) dr |= DRUM_KICK; if (i == 4 || i == 12) dr |= DRUM_SNARE; if (i % 2 == 0) dr |= DRUM_HAT; break;
                case 2: if (i == 0) dr |= DRUM_KICK; if (i == 8) dr |= DRUM_SNARE; if (i % 4 == 2) dr |= DRUM_HAT; break;
                case 3: if (i == 0 || i == 7 || i == 10) dr |= DRUM_KICK; if (i == 4 || i == 12 || i == 15) dr |= DRUM_SNARE; if (i % 2 == 1) dr |= DRUM_HAT; break;
                case 4:
                    /* Shuffle: hats on the beat and the last sixteenth before
                     * it, which reads as a swung triplet at sixteenth resolution. */
                    if (i == 0 || i == 8) dr |= DRUM_KICK;
                    if (i == 4 || i == 12) dr |= DRUM_SNARE;
                    if (i % 4 == 0 || i % 4 == 3) dr |= DRUM_HAT;
                    break;
                default:
                    /* Gallop: two quick hats into each beat, kicks on one and
                     * three with a pickup before each. */
                    if (i == 0 || i == 6 || i == 8 || i == 14) dr |= DRUM_KICK;
                    if (i == 4 || i == 12) dr |= DRUM_SNARE;
                    if (i % 4 == 2 || i % 4 == 3) dr |= DRUM_HAT;
                    break;
            }
            if (i == ghostKick) dr |= DRUM_KICK;
            if (i == ghostSnare) dr |= DRUM_SNARE;
            p->drums[bar * 16 + i] = dr;
        }
    }
}

/* The motif rules shared by the two lead writers. onsetAt says whether step i
 * of the bar gets a note (the seed's rhythm mask, or the user's own rhythm);
 * lastOnset is the pattern step of the final note, which lands on its bar's
 * chord root so the phrase settles before the loop. */
static void WriteLeadBar(int8_t *lead, ComposeKey k, int tonicMidi, const ComposeChords *c, int leadOctave, uint32_t seed, int bar, const bool onsetAt[16], int lastOnset) {
    int d = c->degree[bar];
    uint32_t rng = seed * 2654435761u + (uint32_t)(bar % 2) * 40503u + (bar == 3 ? 977u : 0u);
    /* The theme draws its rhythm mask first; that draw is spent here even when
     * the caller brings its own rhythm, so the pitch draws that follow are
     * the theme's. */
    Lcg(&rng);
    int deg = d + 4 + (int)(Lcg(&rng) % 3);
    int lastStrong = deg;
    for (int i = 0; i < 16; i++) {
        if (!onsetAt[i]) continue;
        bool strong = (i % 4) == 0;
        if (strong) {
            int t = (int)(Lcg(&rng) % 3);
            deg = d + t * 2 + 7 * (int)(Lcg(&rng) % 2);
            lastStrong = deg;
        } else {
            static const int mv[6] = {-2, -1, 1, 1, 2, 0};
            deg += mv[Lcg(&rng) % 6];
            if (deg > lastStrong + 4) deg = lastStrong + 2;
            if (deg < lastStrong - 4) deg = lastStrong - 2;
        }
        if ((bar == 3 && i >= 12) || bar * 16 + i == lastOnset) deg = d + 7; /* the phrase ends on the chord root */
        lead[bar * 16 + i] = Note8(Compose_DegreeNote(k, tonicMidi, deg) + 12 * (leadOctave - 2));
    }
}

void Compose_Lead(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int leadOctave, uint32_t seed) {
    int8_t *lead = p->tone[TRACK_LEAD];
    memset(lead, NOTE_NONE, SONG_STEPS);
    static const uint16_t kMasks[8] = {0x9249, 0x8A8A, 0xA4A4, 0x9292, 0xAAAA, 0x8888 | 0x0505, 0xC0C0 | 0x1111, 0x9999};
    for (int bar = 0; bar < 4; bar++) {
        /* The mask is the first draw of the bar's generator, as in the theme. */
        uint32_t rng = seed * 2654435761u + (uint32_t)(bar % 2) * 40503u + (bar == 3 ? 977u : 0u);
        uint16_t mask = kMasks[Lcg(&rng) % 8];
        bool onsetAt[16];
        for (int i = 0; i < 16; i++) onsetAt[i] = ((mask >> (15 - i)) & 1) != 0;
        /* lastOnset -1: the theme's own ending rule (bar 3 from beat four) applies. */
        WriteLeadBar(lead, k, tonicMidi, c, leadOctave, seed, bar, onsetAt, -1);
    }
}

void Compose_LeadPitches(SongPattern *p, ComposeKey k, int tonicMidi, const ComposeChords *c, int leadOctave, uint32_t seed) {
    int8_t *lead = p->tone[TRACK_LEAD];
    int lastOnset = -1;
    for (int st = 0; st < SONG_STEPS; st++) if (lead[st] >= 0) lastOnset = st;
    for (int bar = 0; bar < 4; bar++) {
        bool onsetAt[16];
        for (int i = 0; i < 16; i++) onsetAt[i] = lead[bar * 16 + i] >= 0;
        WriteLeadBar(lead, k, tonicMidi, c, leadOctave, seed, bar, onsetAt, lastOnset);
    }
}

/* ----------------------------------------------------------- notes and keys */

int Compose_SnapToKey(ComposeKey k, int note) {
    if (note < 0 || Compose_InKey(k, note)) return note;
    /* Inside the song's range one neighbour is always in range (the scales
     * never leave a gap wider than three semitones), so clamp first. */
    note = ClampI(note, NOTE_MIN, NOTE_MAX);
    if (Compose_InKey(k, note)) return note;
    int down = note, up = note;
    while (down > NOTE_MIN - 12 && !Compose_InKey(k, down)) down--;
    while (up < NOTE_MAX + 12 && !Compose_InKey(k, up)) up++;
    int pick = (note - down <= up - note) ? down : up;
    if (pick < NOTE_MIN || pick > NOTE_MAX) pick = (pick == down) ? up : down;
    return pick;
}

int Compose_ScaleStep(ComposeKey k, int note, int delta) {
    if (note < 0) return note;
    int n = Compose_SnapToKey(k, note);
    int dir = delta < 0 ? -1 : 1;
    for (int steps = delta < 0 ? -delta : delta; steps > 0; steps--) {
        int next = n + dir;
        while (next >= NOTE_MIN && next <= NOTE_MAX && !Compose_InKey(k, next)) next += dir;
        if (next < NOTE_MIN || next > NOTE_MAX) break; /* the range ends here: stay on the last in-key note */
        n = next;
    }
    return n;
}

void Compose_ChordName(ComposeKey k, int degree, char *out, size_t size) {
    if (!out || size == 0) return;
    int rootPc = DegreePitchClass(k, degree);
    int third = Mod(DegreePitchClass(k, degree + 2) - rootPc, 12);
    int fifth = Mod(DegreePitchClass(k, degree + 4) - rootPc, 12);
    const char *quality = "";
    if (fifth == 6) quality = "dim";
    else if (fifth == 8) quality = "aug";
    else if (third == 3) quality = "m";
    snprintf(out, size, "%s%s", Song_KeyRootName(rootPc), quality);
}

/* The in-key degree nearest a pitch class; a tie goes to the lower note, as
 * Compose_SnapToKey does. */
static int NearestDegreeOfPitchClass(ComposeKey k, int pc) {
    int best = 0, bestDist = 99;
    bool bestBelow = false;
    for (int d = 0; d < 7; d++) {
        int spc = DegreePitchClass(k, d);
        int downDist = Mod(pc - spc, 12), upDist = Mod(spc - pc, 12);
        int dist = downDist < upDist ? downDist : upDist;
        bool below = downDist <= upDist;
        if (dist < bestDist || (dist == bestDist && below && !bestBelow)) { bestDist = dist; best = d; bestBelow = below; }
    }
    return best;
}

/* The tonic a track's notes imply, if they lie where the writers put them:
 * a bass note of degree d sits at tonic - 12 + scale[d], an arp note at
 * tonic + 12 + scale[d]. Each in-key note votes for the tonic that makes
 * that true and the most voted wins (a tie goes to the lower), so a bass
 * that jumps octaves for colour does not pull the register up. */
static int ImpliedTonic(ComposeKey k, const int8_t *notes, int offset) {
    int votes[128] = {0};
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = notes[st];
        int deg = Compose_NoteDegree(k, n);
        if (deg < 0) continue;
        int t = n + offset - kScales[ScaleOf(k)][deg];
        if (t >= 0 && t < 128) votes[t]++;
    }
    int best = -1;
    for (int t = 0; t < 128; t++) if (votes[t] > 0 && (best < 0 || votes[t] > votes[best])) best = t;
    return best;
}

int Compose_TonicMidi(ComposeKey k, const SongPattern *p) {
    int base = 40 + Mod(RootOf(k) - 4, 12); /* E2..D#3, where the recipes keep their tonic */
    if (!p) return base;
    int t = ImpliedTonic(k, p->tone[TRACK_BASS], 12);
    if (t < 0) t = ImpliedTonic(k, p->tone[TRACK_ARP], -12);
    if (t < 0) return base;
    while (t < 36) t += 12;
    while (t > 60) t -= 12;
    return t;
}

/* The first step of a track that sounds a note, or -1. */
static int FirstOnset(const SongPattern *p, int track) {
    for (int st = 0; st < SONG_STEPS; st++) if (p->tone[track][st] >= 0) return st;
    return -1;
}

bool Compose_GuessKey(const Song *s, ComposeKey *out) {
    if (!s || !out || s->patternCount <= 0) return false;
    /* How often each pitch class sounds (the bass counts double: it carries
     * the harmony) and how strongly each is the tonic: the very first note
     * of the song, and the bass on every bar's first beat. */
    double hist[12] = {0}, tonic[12] = {0};
    double total = 0, tonicMax = 0;
    int positions = s->orderCount > 0 ? s->orderCount : 1;
    for (int pos = 0; pos < positions; pos++) {
        int pi = s->orderCount > 0 ? s->order[pos] : 0;
        if (pi < 0 || pi >= s->patternCount) continue;
        const SongPattern *p = &s->patterns[pi];
        for (int t = 0; t < SONG_TONE_TRACKS; t++) {
            double w = t == TRACK_BASS ? 2.0 : 1.0;
            for (int st = 0; st < SONG_STEPS; st++) {
                int n = p->tone[t][st];
                if (n < 0) continue;
                hist[Mod(n, 12)] += w;
                total += w;
                if (t == TRACK_BASS && st % 16 == 0) tonic[Mod(n, 12)] += 2.0;
            }
        }
        if (pos == 0) {
            /* A song opens on its tonic far more often than not; the lowest
             * track with a note is the one to ask. */
            for (int t = 0; t < SONG_TONE_TRACKS; t++) {
                int st = FirstOnset(p, t);
                if (st >= 0) { tonic[Mod(p->tone[t][st], 12)] += 8.0; break; }
            }
        }
    }
    if (total <= 0) return false;
    for (int pc = 0; pc < 12; pc++) if (tonic[pc] > tonicMax) tonicMax = tonic[pc];
    /* Score every key: how well its scale covers the notes (notes outside it
     * cost twice what notes inside earn, so a mode that leaves a sounded note
     * out loses to one that does not), plus how strongly its root behaves as
     * the tonic. The modes of one diatonic set tie on the first term; the
     * second picks the root among them. */
    double bestScore = -1e9;
    ComposeKey best = {9, SCALE_MINOR};
    for (int root = 0; root < 12; root++) {
        for (int scale = 0; scale < SONG_SCALE_COUNT; scale++) {
            ComposeKey k = {root, scale};
            double fit = 0;
            for (int pc = 0; pc < 12; pc++) fit += Compose_NoteDegree(k, pc) >= 0 ? hist[pc] : -2.0 * hist[pc];
            double score = fit / total + (tonicMax > 0 ? 0.3 * tonic[root] / tonicMax : 0.0);
            if (score > bestScore) { bestScore = score; best = k; }
        }
    }
    *out = best;
    return true;
}

ComposeKey Compose_SongKey(const Song *s, bool *guessedOut) {
    ComposeKey k = {9, SCALE_MINOR};
    bool guessed = true;
    if (s && s->keyRoot >= 0 && s->keyRoot < 12) {
        k.root = s->keyRoot;
        k.scale = ClampI(s->keyScale, 0, SONG_SCALE_COUNT - 1);
        guessed = false;
    } else if (s) {
        Compose_GuessKey(s, &k); /* A minor stays when there is nothing to guess from */
    }
    if (guessedOut) *guessedOut = guessed;
    return k;
}

/* How strongly one bar of a track says it is on the chord of each degree:
 * every in-key note votes for the chords it is a tone of, twice as much for
 * the chord it is the root of, weighted by where it falls (the bar's first
 * beat most, the other beats more than the sixteenths between). Returns the
 * best degree, or -1 when no note in the bar is in key. */
static int BarChordVote(const int8_t *notes, int bar, ComposeKey k) {
    int score[7] = {0};
    int firstDeg = -1;
    for (int i = 0; i < 16; i++) {
        int n = notes[bar * 16 + i];
        int deg = Compose_NoteDegree(k, n);
        if (deg < 0) continue; /* silence, a hold, or a note outside the key */
        if (firstDeg < 0) firstDeg = deg;
        int w = i == 0 ? 3 : (i % 4 == 0 ? 2 : 1);
        for (int d = 0; d < 7; d++) {
            if (deg == d) score[d] += 2 * w;
            else if (deg == Mod(d + 2, 7) || deg == Mod(d + 4, 7)) score[d] += w;
        }
    }
    if (firstDeg < 0) return -1;
    int best = firstDeg; /* a tie goes to the chord the bar opens on */
    for (int d = 0; d < 7; d++) if (score[d] > score[best]) best = d;
    return best;
}

bool Compose_ReadChords(const SongPattern *p, ComposeKey k, ComposeChords *out) {
    bool any = false;
    for (int bar = 0; bar < 4; bar++) {
        out->degree[bar] = 0;
        int d = BarChordVote(p->tone[TRACK_BASS], bar, k);
        if (d < 0) {
            /* A bass with no note in key still names a chord: its most
             * played pitch class, snapped to the nearest degree. */
            int weight[12] = {0}, bestPc = -1;
            for (int i = 0; i < 16; i++) {
                int n = p->tone[TRACK_BASS][bar * 16 + i];
                if (n >= 0) weight[Mod(n, 12)] += i == 0 ? 3 : (i % 4 == 0 ? 2 : 1);
            }
            for (int i = 0; i < 16; i++) { /* step order, so a tie goes to the earlier note */
                int n = p->tone[TRACK_BASS][bar * 16 + i];
                if (n >= 0 && (bestPc < 0 || weight[Mod(n, 12)] > weight[bestPc])) bestPc = Mod(n, 12);
            }
            if (bestPc >= 0) d = NearestDegreeOfPitchClass(k, bestPc);
        }
        if (d < 0) d = BarChordVote(p->tone[TRACK_ARP], bar, k); /* no bass: the arp decides */
        if (d >= 0) { out->degree[bar] = d; any = true; }
    }
    return any;
}

/* ------------------------------------------------------ reading a pattern */

static bool TrackHasOnset(const int8_t *notes) {
    for (int st = 0; st < SONG_STEPS; st++) if (notes[st] >= 0) return true;
    return false;
}

/* Scores every style against the track as written on these chords at this
 * tonic: matching the note/hold/silence shape of a step earns 2 and an onset
 * on the same pitch class 1 more (so Up, Up and down and Broken, which share
 * one shape, are told apart by their tones). Drums earn 1 per agreeing bit.
 * A tie goes to the lower style. */
static int GuessStyleWith(const SongPattern *p, ComposeKey k, SongTrack track, const ComposeChords *c, int tonic) {
    if (track == TRACK_LEAD) return 0;
    int best = 0, bestScore = -1;
    for (int style = 0; style < Compose_StyleCount(track); style++) {
        SongPattern w;
        memset(&w, 0, sizeof(w));
        int score = 0;
        if (track == TRACK_DRUMS) {
            Compose_Drums(&w, style, 0u);
            for (int st = 0; st < SONG_STEPS; st++) {
                int diff = (p->drums[st] ^ w.drums[st]) & (DRUM_KICK | DRUM_SNARE | DRUM_HAT);
                score += 3 - ((diff & 1) + ((diff >> 1) & 1) + ((diff >> 2) & 1));
            }
        } else {
            if (track == TRACK_BASS) Compose_Bass(&w, k, tonic, c, style);
            else Compose_Arp(&w, k, tonic, c, style);
            for (int st = 0; st < SONG_STEPS; st++) {
                int a = p->tone[track][st], b = w.tone[track][st];
                int ca = a >= 0 ? 1 : a, cb = b >= 0 ? 1 : b;
                if (ca != cb) continue;
                score += 2;
                if (a >= 0 && Mod(a, 12) == Mod(b, 12)) score += 1;
            }
        }
        if (score > bestScore) { bestScore = score; best = style; }
    }
    return best;
}

int Compose_GuessStyle(const SongPattern *p, ComposeKey k, SongTrack track) {
    int tr = (int)track;
    if (!p || tr < 0 || tr > TRACK_DRUMS || tr == TRACK_LEAD) return 0;
    if (track == TRACK_DRUMS) {
        bool any = false;
        for (int st = 0; st < SONG_STEPS; st++) if (p->drums[st]) any = true;
        if (!any) return 0;
    } else if (!TrackHasOnset(p->tone[track])) {
        return 0;
    }
    ComposeChords c;
    Compose_ReadChords(p, k, &c);
    return GuessStyleWith(p, k, track, &c, Compose_TonicMidi(k, p));
}

/* floor(a / b) for b > 0, which C's division does not give for negative a. */
static long FloorDiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int Compose_LeadOctave(ComposeKey k, int tonicMidi, const SongPattern *p) {
    (void)k;
    if (!p) return 2;
    long sum = 0;
    int n = 0, last = -1;
    for (int st = 0; st < SONG_STEPS; st++) {
        int note = p->tone[TRACK_LEAD][st];
        if (note >= 0) { sum += note; n++; last = note; }
    }
    if (n == 0) return 2;
    /* Compose_Lead and Fit my rhythm end on the bar's chord root one octave
     * above the tonic's register, 12..23 semitones above tonicMidi at
     * leadOctave 2, so the last note names the octave exactly for anything
     * they wrote. It is trusted when the tune's average agrees: at leadOctave
     * 2 Compose_Lead averages 6.4..22.8 semitones above the tonic (every key,
     * progression and 50 seeds). Averages are kept doubled and multiplied by
     * n so the sums stay in integers. */
    long octLast = 2 + FloorDiv(last - tonicMidi - 12, 12);
    long rel2 = 2 * sum - (long)n * (2L * tonicMidi + 24L * (octLast - 2));
    if (rel2 >= 12L * n && rel2 <= 46L * n) return ClampI((int)octLast, 1, 5);
    /* A hand-written tune that ends away from where it mostly sits: the
     * nearest octave by its average, centred on 14.5 (2 * 14.5 is 29). */
    long rel2Mean = 2 * sum - (long)n * (2L * tonicMidi + 29);
    return ClampI(2 + (int)FloorDiv(rel2Mean + 12L * n, 24L * n), 1, 5);
}

/* ------------------------------------------------------ changing the chords */

/* The signed distance between two pitch classes, -5..6. */
static int PcDelta(int from, int to) {
    int d = Mod(to - from, 12);
    return d > 6 ? d - 12 : d;
}

/* Voice leading from the triad on degree a to the triad on degree b: tone t
 * (0 root, 1 third, 2 fifth) of a moves by off[t] semitones. Each triad
 * rotation is tried and the one with the least total movement wins (then the
 * smallest largest move, then the first rotation), so common tones stay and
 * the others step to a near tone instead of all leaping in parallel. The
 * pair is always worked out from the lower degree and inverted for the other
 * direction: a tie-break cannot then pick differently on the way back, so
 * A -> B -> A is exact. */
static void ChordMoves(ComposeKey k, int a, int b, int off[3]) {
    a = Mod(a, 7);
    b = Mod(b, 7);
    int lo = a < b ? a : b, hi = a < b ? b : a;
    int bestRot = 0, bestOff[3] = {0, 0, 0}, bestCost = 999, bestMax = 999;
    for (int r = 0; r < 3; r++) {
        int o[3], cost = 0, mx = 0;
        for (int t = 0; t < 3; t++) {
            o[t] = PcDelta(DegreePitchClass(k, lo + 2 * t), DegreePitchClass(k, hi + 2 * ((t + r) % 3)));
            int m = o[t] < 0 ? -o[t] : o[t];
            cost += m;
            if (m > mx) mx = m;
        }
        if (cost < bestCost || (cost == bestCost && mx < bestMax)) {
            bestCost = cost; bestMax = mx; bestRot = r;
            for (int t = 0; t < 3; t++) bestOff[t] = o[t];
        }
    }
    for (int t = 0; t < 3; t++) {
        if (a <= b) off[t] = bestOff[t];                 /* tone t of lo goes to tone t + r of hi */
        else off[t] = -bestOff[Mod(t - bestRot, 3)];      /* tone t of hi came from tone t - r of lo */
    }
}

/* A note as a count of scale steps from the key's tonic at MIDI pitch class
 * 0..11 (in-key notes only), so moving by steps is plain addition. */
static int AbsDegree(ComposeKey k, int note) {
    int deg = Compose_NoteDegree(k, note);
    int oct = (note - RootOf(k) - kScales[ScaleOf(k)][deg]) / 12; /* exact: note is in key and above the root */
    return oct * 7 + deg;
}

static int BassRemap(ComposeKey k, int note, int steps) {
    int n = Compose_SnapToKey(k, note);
    return FoldIntoRange(Compose_DegreeNote(k, RootOf(k), AbsDegree(k, n) + steps));
}

/* The nearest tone of the chord on degree d, looking down first so a tie
 * goes down as in Compose_SnapToKey. A triad never leaves a gap wider than
 * six semitones, so the search always ends. */
static int NearestChordTone(ComposeKey k, int d, int note) {
    for (int dist = 0; dist <= 6; dist++) {
        if (Compose_IsChordTone(k, d, note - dist)) return note - dist;
        if (Compose_IsChordTone(k, d, note + dist)) return note + dist;
    }
    return note;
}

static int ArpRemap(ComposeKey k, int note, int from, int to, const int off[3]) {
    int pc = Mod(note, 12);
    for (int t = 0; t < 3; t++) if (DegreePitchClass(k, from + 2 * t) == pc) return FoldIntoRange(note + off[t]);
    return FoldIntoRange(NearestChordTone(k, to, note));
}

/* How many onsets the most played pitch of a track has, and how many onsets
 * there are in all. */
static int MostCommonPitch(const int8_t *notes, int *onsets) {
    int count[128] = {0}, best = 0;
    *onsets = 0;
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = notes[st];
        if (n < 0 || n > 127) continue;
        (*onsets)++;
        if (++count[n] > best) best = count[n];
    }
    return best;
}

/* The tonic the arp's own notes imply (it may sit apart from the bass),
 * kept to the same 36..60 Compose_TonicMidi uses; the pattern's tonic when
 * the arp has nothing in key. */
static int ArpTonic(ComposeKey k, const SongPattern *p) {
    int t = ImpliedTonic(k, p->tone[TRACK_ARP], -12);
    if (t < 0) return Compose_TonicMidi(k, p);
    while (t < 36) t += 12;
    while (t > 60) t -= 12;
    return t;
}

void Compose_Reharmonise(SongPattern *p, ComposeKey k, const ComposeChords *from, const ComposeChords *to, bool fitLead) {
    if (!p || !to) return;
    ComposeChords read;
    if (!from) {
        Compose_ReadChords(p, k, &read);
        from = &read;
    }
    /* Everything is read from the pattern as it came in: once the bass has
     * moved, its notes would place the tonic (and so the register of any
     * track written fresh) somewhere else. */
    const SongPattern before = *p;
    int tonic = Compose_TonicMidi(k, &before);
    int8_t *bass = p->tone[TRACK_BASS], *arp = p->tone[TRACK_ARP];

    if (!TrackHasOnset(before.tone[TRACK_BASS])) {
        Compose_Bass(p, k, tonic, to, 0);
    } else {
        for (int bar = 0; bar < 4; bar++) {
            int a = Mod(from->degree[bar], 7), b = Mod(to->degree[bar], 7);
            if (a == b) continue;
            int steps = NearestDegree(a, b) - a; /* -3..3: the root takes the shorter way */
            /* Near the ends of the range the shorter way can run out; then
             * the whole bar goes the other way, so its octave jumps stay
             * octave jumps instead of single notes folding back. */
            bool fits = true;
            for (int i = 0; i < 16; i++) {
                int n = bass[bar * 16 + i];
                if (n < 0) continue;
                int m = Compose_DegreeNote(k, RootOf(k), AbsDegree(k, Compose_SnapToKey(k, n)) + steps);
                if (m < NOTE_MIN || m > NOTE_MAX) fits = false;
            }
            if (!fits) steps += steps < 0 ? 7 : -7;
            for (int i = 0; i < 16; i++) {
                int st = bar * 16 + i;
                if (bass[st] >= 0) bass[st] = (int8_t)BassRemap(k, bass[st], steps);
            }
        }
    }

    if (!TrackHasOnset(before.tone[TRACK_ARP])) {
        Compose_Arp(p, k, tonic, to, 0);
    } else {
        for (int bar = 0; bar < 4; bar++) {
            int a = Mod(from->degree[bar], 7), b = Mod(to->degree[bar], 7);
            if (a == b) continue;
            int off[3];
            ChordMoves(k, a, b, off);
            for (int i = 0; i < 16; i++) {
                int st = bar * 16 + i;
                if (arp[st] >= 0) arp[st] = (int8_t)ArpRemap(k, arp[st], a, b, off);
            }
        }
        /* Notes that are not tones of the old chord all head for the nearest
         * new tone, so a busy arp of passing notes can fold onto one pitch
         * and stop sounding like an arp. Only a collapse the remap caused
         * counts: an arp that was already a pedal on one note stays one. */
        int oldOnsets, newOnsets;
        int oldMost = MostCommonPitch(before.tone[TRACK_ARP], &oldOnsets);
        int newMost = MostCommonPitch(arp, &newOnsets);
        if (newOnsets > 0 && newMost * 2 > newOnsets && oldMost * 2 <= oldOnsets) {
            int arpTonic = ArpTonic(k, &before);
            int style = GuessStyleWith(&before, k, TRACK_ARP, from, arpTonic);
            Compose_Arp(p, k, arpTonic, to, style);
        }
    }

    if (fitLead) Compose_FitLead(p, k, to);
}

bool Compose_FitLead(SongPattern *p, ComposeKey k, const ComposeChords *c) {
    if (!p || !c) return false;
    int8_t *lead = p->tone[TRACK_LEAD];
    bool moved = false;
    for (int st = 0; st < SONG_STEPS; st++) {
        int n = lead[st];
        if (n < 0) continue;
        int d = Mod(c->degree[st / 16], 7);
        int fit = -1;
        if (st % 4 == 0) {
            /* The same strong beats the lead writers put chord tones on. Two
             * semitones reach a chord tone from anywhere but the middle of a
             * diminished chord's six-semitone gap. A note there that is in
             * key (harmonic minor's) stays put; one that is not (the other
             * modes') reaches three, to the chord tone: a semitone onto the
             * scale would leave it two from a chord tone, and fitting the
             * same chords again would then move it a second time. */
            int reach = Compose_InKey(k, n) ? 2 : 3;
            for (int dist = 0; dist <= reach && fit < 0; dist++) {
                int down = n - dist, up = n + dist;
                if (down >= NOTE_MIN && Compose_IsChordTone(k, d, down)) fit = down;
                else if (up <= NOTE_MAX && Compose_IsChordTone(k, d, up)) fit = up;
            }
        }
        /* Off the beat, or no chord tone within reach: the scale. Its gaps
         * are at most three semitones, so this moves a note one at most, or
         * two where harmonic minor's wide gap straddles an end of the range. */
        if (fit < 0) fit = Compose_SnapToKey(k, n);
        if (fit != n) { lead[st] = (int8_t)fit; moved = true; }
    }
    return moved;
}

/* ------------------------------------------------------------ chord lines */

static void SetErr(char *err, size_t size, const char *msg) {
    if (err && size > 0) snprintf(err, size, "%s", msg);
}

static bool IsSeparator(char ch) { return ch == ' ' || ch == '\t' || ch == ',' || ch == '-' || ch == '|'; }

/* "ii" -> 1, "VII" -> 6: the letters of a Roman numeral, any case; -1 if not. */
static int NumeralDegree(const char *s, size_t len) {
    static const char *const names[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    if (len == 0 || len > 3) return -1;
    char low[4];
    for (size_t i = 0; i < len; i++) low[i] = (char)tolower((unsigned char)s[i]);
    low[len] = '\0';
    for (int d = 0; d < 7; d++) if (strcmp(low, names[d]) == 0) return d;
    return -1;
}

/* Whether the letters after a chord's root are a quality we know: nothing
 * (major), m, min, maj, dim, o, aug or +, each with an optional 7. They are
 * read and then let go: the chord used is the key's own triad. */
static bool KnownQuality(const char *s, size_t len) {
    static const char *const qualities[] = {"", "m", "min", "maj", "dim", "o", "aug", "+"};
    char low[8];
    if (len >= sizeof(low)) return false;
    for (size_t i = 0; i < len; i++) low[i] = (char)tolower((unsigned char)s[i]);
    low[len] = '\0';
    if (len > 0 && low[len - 1] == '7') low[--len] = '\0';
    for (size_t q = 0; q < sizeof(qualities) / sizeof(qualities[0]); q++) if (strcmp(low, qualities[q]) == 0) return true;
    return false;
}

/* One token of a chord line as a degree 0..6, or -1 with err written. */
static int ChordTokenDegree(const char *tok, size_t len, ComposeKey k, char *err, size_t errSize) {
    char msg[64];
    bool digits = true;
    for (size_t i = 0; i < len; i++) if (!isdigit((unsigned char)tok[i])) digits = false;
    if (digits) {
        if (len == 1 && tok[0] >= '1' && tok[0] <= '7') return tok[0] - '1';
        SetErr(err, errSize, "Use 1 to 7 for chords");
        return -1;
    }
    /* A chord name: a root letter, an optional sharp or flat, a quality. */
    static const int kLetter[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    int letter = toupper((unsigned char)tok[0]);
    if (letter >= 'A' && letter <= 'G') {
        int pc = kLetter[letter - 'A'];
        size_t rootLen = 1;
        if (len > 1 && tok[1] == '#') { pc++; rootLen = 2; }
        else if (len > 1 && tok[1] == 'b') { pc--; rootLen = 2; }
        if (KnownQuality(tok + rootLen, len - rootLen)) {
            for (int d = 0; d < 7; d++) if (DegreePitchClass(k, d) == Mod(pc, 12)) return d;
            char key[32];
            Song_KeyName(RootOf(k), ScaleOf(k), key, sizeof(key));
            /* Echo the root as typed ("Bb", not "A#"), capitalised. */
            snprintf(msg, sizeof(msg), "%c%s is not in %s", (char)letter, rootLen == 2 ? (tok[1] == '#' ? "#" : "b") : "", key);
            SetErr(err, errSize, msg);
            return -1;
        }
    }
    /* A Roman numeral, perhaps with the flat or sharp the table's numerals
     * carry ("bVII") and a quality mark ("viio", "III+", "V7"); the degree
     * is all that matters. */
    size_t start = (tok[0] == 'b' || tok[0] == '#') ? 1 : 0, end = len;
    if (end > start + 1 && (tok[end - 1] == 'o' || tok[end - 1] == '+' || tok[end - 1] == '7')) end--;
    int d = NumeralDegree(tok + start, end - start);
    if (d >= 0) return d;
    snprintf(msg, sizeof(msg), "Not a chord: %.*s", (int)(len > 8 ? 8 : len), tok);
    SetErr(err, errSize, msg);
    return -1;
}

bool Compose_ParseChordLine(const char *text, ComposeKey k, ComposeChords *out, char *err, size_t errSize) {
    int deg[4], n = 0;
    const char *p = text ? text : "";
    for (;;) {
        while (*p && IsSeparator(*p)) p++;
        if (!*p) break;
        const char *tok = p;
        while (*p && !IsSeparator(*p)) p++;
        if (n == 4) { SetErr(err, errSize, "Up to 4 chords, one a bar"); return false; }
        int d = ChordTokenDegree(tok, (size_t)(p - tok), k, err, errSize);
        if (d < 0) return false;
        deg[n++] = d;
    }
    if (n == 0) { SetErr(err, errSize, "Type 1 to 4 chords, like 1 6 4 5"); return false; }
    if (out) for (int b = 0; b < 4; b++) out->degree[b] = deg[b % n];
    SetErr(err, errSize, "");
    return true;
}

/* ----------------------------------------------------------- progressions */

/* The six the recipe names, then the game themes' own (the ones not already
 * above), named after their games so the owner can ask for "the chords
 * Rockdrift has"; four more came from games that have left the arcade, and
 * Pond Kings' jig keeps to its own world, so those are named for their mood.
 * Numerals follow the mood each is written for. */
static const ComposeProgression kProgressions[] = {
    {"Hero's anthem",    "I V vi IV",    {{0, 4, 5, 3}}},  /* also Brickburst */
    {"Last level",       "i VI VII i",   {{0, 5, 6, 0}}},
    {"Sad and racing",   "i VI III VII", {{0, 5, 2, 6}}},  /* also Blockfall */
    {"Spooky stairs",    "i bII i vii",  {{0, 1, 0, 6}}},
    {"Lazy groove",      "i iv i iv",    {{0, 3, 0, 3}}},
    {"Royal road",       "IV V iii vi",  {{3, 4, 2, 5}}},
    {"Ghost arcade",     "i VI iv v",    {{0, 5, 3, 4}}},  /* also Skyraid */
    {"Chase loop",       "i IV v IV",    {{0, 3, 4, 3}}},
    {"Rockdrift",        "i VII VI VII", {{0, 6, 5, 6}}},
    {"Open road",        "I bVII IV I",  {{0, 6, 3, 0}}},
    {"Crawlshot",        "i iv V i",     {{0, 3, 4, 0}}},
    {"Moondrop",         "i v ii v",     {{0, 4, 1, 4}}},
    {"Going under",      "i III VI v",   {{0, 2, 5, 4}}},
    {"Climbing up",      "I IV bVII v",  {{0, 3, 6, 4}}},
    {"Pond jig",         "I IV I V",     {{0, 3, 0, 4}}},  /* Pond Kings' own */
};
#define PROGRESSION_N ((int)(sizeof(kProgressions) / sizeof(kProgressions[0])))

int Compose_ProgressionCount(void) { return PROGRESSION_N; }

const ComposeProgression *Compose_ProgressionAt(int i) { return &kProgressions[Mod(i, PROGRESSION_N)]; }

int Compose_ProgressionFind(const ComposeChords *c) {
    if (!c) return -1;
    for (int i = 0; i < PROGRESSION_N; i++) {
        bool same = true;
        for (int b = 0; b < 4; b++) if (kProgressions[i].chords.degree[b] != c->degree[b]) same = false;
        if (same) return i;
    }
    return -1;
}
