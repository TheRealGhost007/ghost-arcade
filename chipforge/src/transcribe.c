/* The importer: mono samples in, a Song out (see transcribe.h for the
 * contract). The pipeline is the classic one. An STFT gives spectral flux per
 * frame (how much new energy arrived, by band); the autocorrelation of that
 * flux gives the tempo; a dynamic-programming beat tracker (Ellis 2007) lays
 * beats on the flux peaks while letting the tempo drift; then every sixteenth
 * step is asked what it contains with a harmonic-summation pitch salience and
 * the band fluxes decide the drums. Nothing here touches raylib, so the whole
 * thing runs inside the headless test suite. */
#include "transcribe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TR_WIN 1024          /* onset STFT window */
#define TR_HOP 256
#define TR_PITCH_WIN 2048    /* per-step pitch window */
#define TR_MAX_RATE 30000    /* above this the input is decimated by 2 */
#define TR_MIDI_LO 28
#define TR_MIDI_HI 95
#define TR_MIDI_N (TR_MIDI_HI - TR_MIDI_LO + 1)
#define TR_BASS_HI 51
#define TR_LEAD_LO 48
#define TR_BPM_LO 60
#define TR_BPM_HI 200
#define TR_FALLBACK_BPM 120  /* when nothing periodic is heard */
#define TR_PI 3.14159265358979323846

/* Harmonic weights for the salience and the overtones of the bass that a
 * melody candidate is suspected of being (octave, fifth, two octaves, third
 * and fifth above that). */
static const float kHarmonicWeight[5] = {1.0f, 0.6f, 0.4f, 0.3f, 0.2f};
static const int kBassOvertone[5] = {12, 19, 24, 28, 31};

static float MidiHz(int midi) { return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f); }
static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double FrameToSample(double frame) { return frame * TR_HOP + TR_WIN / 2; }

static int CmpFloat(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Sorts tmp; the caller passes a scratch copy. */
static float MedianInPlace(float *tmp, int n) {
    if (n <= 0) return 0.0f;
    qsort(tmp, (size_t)n, sizeof(float), CmpFloat);
    return (n & 1) ? tmp[n / 2] : 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

/* Median absolute deviation: a spread estimate that a few loud hits do not
 * drag upwards, which is what makes the drum thresholds work on real mixes. */
static float MadInPlace(const float *v, int n, float median, float *tmp) {
    for (int i = 0; i < n; i++) tmp[i] = fabsf(v[i] - median);
    return MedianInPlace(tmp, n);
}

/* Linear interpolation of a magnitude spectrum at a frequency. */
static float MagAt(const float *mag, int bins, float binHz, float hz) {
    float pos = hz / binHz;
    if (pos < 0.0f) return 0.0f;
    int k = (int)pos;
    if (k + 1 >= bins) return 0.0f;
    float frac = pos - (float)k;
    return mag[k] * (1.0f - frac) + mag[k + 1] * frac;
}

float Transcribe_Salience(const float *mag, int bins, float binHz, int midi) {
    if (!mag || bins < 2 || binHz <= 0.0f) return 0.0f;
    float f0 = MidiHz(midi), s = 0.0f;
    for (int h = 1; h <= 5; h++) {
        float hz = (float)h * f0;
        if (hz / binHz + 1.0f >= (float)bins) break;
        s += kHarmonicWeight[h - 1] * MagAt(mag, bins, binHz, hz);
    }
    return s;
}

/* ------------------------------------------------------------------- FFT */

typedef struct {
    int n;
    float *cosT, *sinT;  /* n/2 twiddles */
    int *rev;            /* bit-reversal permutation */
    float *win;          /* Hann */
    float winSum;
    float *re, *im;      /* work buffers */
} Fft;

static void Fft_Free(Fft *f) {
    free(f->cosT); free(f->sinT); free(f->rev); free(f->win); free(f->re); free(f->im);
    memset(f, 0, sizeof(*f));
}

static bool Fft_Init(Fft *f, int n) {
    memset(f, 0, sizeof(*f));
    f->n = n;
    f->cosT = malloc(sizeof(float) * (size_t)(n / 2));
    f->sinT = malloc(sizeof(float) * (size_t)(n / 2));
    f->rev = malloc(sizeof(int) * (size_t)n);
    f->win = malloc(sizeof(float) * (size_t)n);
    f->re = malloc(sizeof(float) * (size_t)n);
    f->im = malloc(sizeof(float) * (size_t)n);
    if (!f->cosT || !f->sinT || !f->rev || !f->win || !f->re || !f->im) { Fft_Free(f); return false; }
    int bits = 0;
    while ((1 << bits) < n) bits++;
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        f->rev[i] = r;
    }
    for (int k = 0; k < n / 2; k++) {
        double ang = -2.0 * TR_PI * (double)k / (double)n;
        f->cosT[k] = (float)cos(ang);
        f->sinT[k] = (float)sin(ang);
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        f->win[i] = (float)(0.5 - 0.5 * cos(2.0 * TR_PI * (double)i / (double)n));
        sum += f->win[i];
    }
    f->winSum = (float)sum;
    return true;
}

/* Iterative radix-2 Cooley-Tukey on the work buffers. */
static void Fft_Run(Fft *f) {
    int n = f->n;
    float *re = f->re, *im = f->im;
    for (int i = 0; i < n; i++) {
        int j = f->rev[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                float wr = f->cosT[j * step], wi = f->sinT[j * step];
                float xr = re[i + j + half], xi = im[i + j + half];
                float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
                re[i + j + half] = re[i + j] - tr;
                im[i + j + half] = im[i + j] - ti;
                re[i + j] += tr;
                im[i + j] += ti;
            }
        }
    }
}

/* Hann-windowed magnitude spectrum (bins 0..n/2) of the n samples starting at
 * `start`; samples outside 0..frames read as silence so edges just fade. The
 * scale makes a full-scale sine read about 1.0 in its bin. */
static void Fft_Magnitude(Fft *f, const float *x, int frames, long start, float *mag) {
    int n = f->n;
    for (int i = 0; i < n; i++) {
        long idx = start + i;
        float v = (idx >= 0 && idx < frames) ? x[idx] : 0.0f;
        f->re[i] = v * f->win[i];
        f->im[i] = 0.0f;
    }
    Fft_Run(f);
    float scale = 2.0f / f->winSum;
    for (int k = 0; k <= n / 2; k++) mag[k] = sqrtf(f->re[k] * f->re[k] + f->im[k] * f->im[k]) * scale;
}

/* ---------------------------------------------------------------- onsets */

typedef struct {
    int frames;
    float fps;
    float *onset;             /* full-band flux, local mean removed, >= 0 */
    float *low, *mid, *high;  /* raw band fluxes: kick, body, hats */
} Onsets;

static void Onsets_Free(Onsets *o) {
    free(o->onset); free(o->low); free(o->mid); free(o->high);
    memset(o, 0, sizeof(*o));
}

static bool Onsets_Compute(const float *x, int n, int rate, Onsets *o) {
    memset(o, 0, sizeof(*o));
    int frames = (n - TR_WIN) / TR_HOP + 1;
    if (frames < 2) return false;
    const int bins = TR_WIN / 2 + 1;
    float binHz = (float)rate / (float)TR_WIN;
    Fft fft;
    if (!Fft_Init(&fft, TR_WIN)) return false;
    float *mag = malloc(sizeof(float) * (size_t)bins);
    float *prev = calloc((size_t)bins, sizeof(float));
    float *cur = malloc(sizeof(float) * (size_t)bins);
    int *band = malloc(sizeof(int) * (size_t)bins);
    o->onset = calloc((size_t)frames, sizeof(float));
    o->low = calloc((size_t)frames, sizeof(float));
    o->mid = calloc((size_t)frames, sizeof(float));
    o->high = calloc((size_t)frames, sizeof(float));
    bool ok = mag && prev && cur && band && o->onset && o->low && o->mid && o->high;
    if (ok) {
        for (int k = 0; k < bins; k++) {
            float hz = (float)k * binHz;
            band[k] = (hz >= 30.0f && hz <= 150.0f) ? 1 : (hz > 150.0f && hz <= 2500.0f) ? 2 : (hz >= 4000.0f && hz <= 10000.0f) ? 3 : 0;
        }
        o->frames = frames;
        o->fps = (float)rate / (float)TR_HOP;
        for (int f = 0; f < frames; f++) {
            Fft_Magnitude(&fft, x, n, (long)f * TR_HOP, mag);
            /* log compression so a quiet hat counts beside a loud chord */
            for (int k = 0; k < bins; k++) cur[k] = logf(1.0f + 20.0f * mag[k]);
            if (f > 0) {
                float full = 0.0f, lo = 0.0f, mi = 0.0f, hi = 0.0f;
                for (int k = 1; k < bins; k++) {
                    float d = cur[k] - prev[k];
                    if (d <= 0.0f) continue;
                    full += d;
                    if (band[k] == 1) lo += d;
                    else if (band[k] == 2) mi += d;
                    else if (band[k] == 3) hi += d;
                }
                o->onset[f] = full;
                o->low[f] = lo;
                o->mid[f] = mi;
                o->high[f] = hi;
            }
            float *t = prev; prev = cur; cur = t;
        }
        /* Remove the local mean (about a second) so a loud passage does not
         * look like one long onset, then keep only the excess. */
        double *ps = malloc(sizeof(double) * (size_t)(frames + 1));
        if (ps) {
            ps[0] = 0.0;
            for (int f = 0; f < frames; f++) ps[f + 1] = ps[f] + o->onset[f];
            int half = (int)(o->fps * 0.5f);
            if (half < 1) half = 1;
            for (int f = 0; f < frames; f++) {
                int a = f - half < 0 ? 0 : f - half;
                int b = f + half >= frames ? frames - 1 : f + half;
                float mean = (float)((ps[b + 1] - ps[a]) / (double)(b - a + 1));
                float v = o->onset[f] - mean;
                o->onset[f] = v > 0.0f ? v : 0.0f;
            }
            free(ps);
        } else {
            ok = false;
        }
    }
    free(mag); free(prev); free(cur); free(band);
    Fft_Free(&fft);
    if (!ok) Onsets_Free(o);
    return ok;
}

/* ----------------------------------------------------------------- tempo */

int Transcribe_EstimateBpm(const float *onset, int frames, float fps, float *confidence) {
    if (confidence) *confidence = 0.0f;
    if (!onset || frames < 2 || fps <= 0.0f) return 0;
    float lo = onset[0], hi = onset[0];
    for (int i = 1; i < frames; i++) {
        if (onset[i] < lo) lo = onset[i];
        if (onset[i] > hi) hi = onset[i];
    }
    if (hi - lo <= 1e-9f) return 0; /* flat: nothing to be periodic */

    int lagMin = (int)floorf(fps * 60.0f / (float)TR_BPM_HI);
    int lagMax = (int)ceilf(fps * 60.0f / (float)TR_BPM_LO);
    if (lagMin < 1) lagMin = 1;
    if (lagMax > frames - 1) lagMax = frames - 1;
    if (lagMax <= lagMin) return 0;
    int nl = lagMax - lagMin + 1;
    float *acf = malloc(sizeof(float) * (size_t)nl);
    if (!acf) return 0;

    /* A log-Gaussian prior an octave wide around 120 bpm settles the usual
     * "is it 70 or 140" argument the way a listener would. */
    double mean = 0.0;
    int best = 0;
    for (int li = 0; li < nl; li++) {
        int lag = lagMin + li;
        double s = 0.0;
        for (int t = 0; t + lag < frames; t++) s += (double)onset[t] * (double)onset[t + lag];
        s /= (double)(frames - lag);
        float bpm = 60.0f * fps / (float)lag;
        float l2 = log2f(bpm / 120.0f);
        acf[li] = (float)s * expf(-0.5f * l2 * l2);
        mean += acf[li];
        if (acf[li] > acf[best]) best = li;
    }
    mean /= (double)nl;
    float peak = acf[best];
    if (peak <= 0.0f) { free(acf); return 0; }

    float lag = (float)(lagMin + best);
    if (best > 0 && best < nl - 1) {
        float y0 = acf[best - 1], y1 = acf[best], y2 = acf[best + 1];
        float denom = y0 - 2.0f * y1 + y2;
        if (denom < 0.0f) lag += 0.5f * (y0 - y2) / denom;
    }
    if (confidence) {
        float ratio = mean > 0.0 ? peak / (float)mean : 0.0f;
        *confidence = ClampF((ratio - 1.0f) / 3.0f, 0.0f, 1.0f);
    }
    free(acf);
    int bpm = (int)lroundf(60.0f * fps / lag);
    return ClampI(bpm, TR_BPM_LO, TR_BPM_HI);
}

/* ---------------------------------------------------------- beat tracking */

/* Ellis-style dynamic programming: every frame scores its onset strength plus
 * the best predecessor a beat ago, minus a penalty for how far that gap strays
 * from the expected period. Returns the malloc'd beat frames in order. */
static float *TrackBeats(const float *onset, int frames, float period, bool fixedTempo, int *countOut) {
    *countOut = 0;
    int dMin = (int)lroundf(fixedTempo ? 0.9f * period : 0.5f * period);
    int dMax = (int)lroundf(fixedTempo ? 1.1f * period : 2.0f * period);
    if (dMin < 1) dMin = 1;
    if (dMax <= dMin) dMax = dMin + 1;
    double mean = 0.0;
    for (int t = 0; t < frames; t++) mean += onset[t];
    mean /= (double)frames;
    /* Scaled by the envelope level so the same drift tolerance applies to a
     * quiet recording and a loud one. */
    double alpha = 400.0 * mean;

    int nd = dMax - dMin + 1;
    double *pen = malloc(sizeof(double) * (size_t)nd);
    double *score = malloc(sizeof(double) * (size_t)frames);
    int *back = malloc(sizeof(int) * (size_t)frames);
    float *beats = NULL;
    if (pen && score && back) {
        for (int d = dMin; d <= dMax; d++) {
            double l = log((double)d / (double)period);
            pen[d - dMin] = alpha * l * l;
        }
        for (int t = 0; t < frames; t++) {
            double best = -HUGE_VAL;
            int bi = -1;
            for (int d = dMin; d <= dMax && d <= t; d++) {
                double v = score[t - d] - pen[d - dMin];
                if (v > best) { best = v; bi = t - d; }
            }
            score[t] = (double)onset[t] + (bi >= 0 ? best : 0.0);
            back[t] = bi;
        }
        int from = frames - 1 - dMax;
        if (from < 0) from = 0;
        int end = from;
        for (int t = from; t < frames; t++) if (score[t] > score[end]) end = t;
        int count = 0;
        for (int t = end; t >= 0; t = back[t]) count++;
        beats = malloc(sizeof(float) * (size_t)count);
        if (beats) {
            int i = count - 1;
            for (int t = end; t >= 0; t = back[t]) beats[i--] = (float)t;
            *countOut = count;
        }
    }
    free(pen); free(score); free(back);
    return beats;
}

/* Extends the tracked beats to cover startFrame..endFrame using the first and
 * last tracked intervals (or the period when there is only one beat). */
static float *ExtendBeats(const float *tracked, int nTracked, float period, double startFrame, double endFrame, int *countOut) {
    *countOut = 0;
    float first, last, ivFirst, ivLast;
    if (nTracked >= 2) {
        first = tracked[0]; last = tracked[nTracked - 1];
        ivFirst = tracked[1] - tracked[0];
        ivLast = tracked[nTracked - 1] - tracked[nTracked - 2];
    } else {
        first = last = nTracked == 1 ? tracked[0] : 0.0f;
        ivFirst = ivLast = period;
    }
    if (ivFirst < 1.0f) ivFirst = period > 1.0f ? period : 1.0f;
    if (ivLast < 1.0f) ivLast = period > 1.0f ? period : 1.0f;
    int before = first > startFrame ? (int)ceil((first - startFrame) / ivFirst) : 0;
    int after = last < endFrame ? (int)ceil((endFrame - last) / ivLast) : 0;
    int total = before + (nTracked > 0 ? nTracked : 1) + after;
    if (total < 2) total = 2;
    float *beats = malloc(sizeof(float) * (size_t)total);
    if (!beats) return NULL;
    int n = 0;
    for (int i = before; i >= 1; i--) beats[n++] = first - (float)i * ivFirst;
    if (nTracked > 0) for (int i = 0; i < nTracked; i++) beats[n++] = tracked[i];
    else beats[n++] = first;
    for (int i = 1; i <= after; i++) beats[n++] = last + (float)i * ivLast;
    while (n < total) beats[n] = beats[n - 1] + ivLast, n++;
    *countOut = n;
    return beats;
}

/* ------------------------------------------------------------------ steps */

typedef struct {
    int n;
    float *frame;                     /* n+1 boundaries in STFT frame units */
    float *low, *mid, *high, *onset;  /* per-step peak flux by band */
    float *rms;
    float *sal;                       /* n x TR_MIDI_N salience vectors */
    int *bass, *lead;                 /* MIDI or NOTE_NONE per step */
    bool *gate;                       /* loud enough to carry a note */
} Steps;

static void Steps_Free(Steps *s) {
    free(s->frame); free(s->low); free(s->mid); free(s->high); free(s->onset);
    free(s->rms); free(s->sal); free(s->bass); free(s->lead); free(s->gate);
    memset(s, 0, sizeof(*s));
}

static bool Steps_Alloc(Steps *s, int n) {
    memset(s, 0, sizeof(*s));
    s->n = n;
    s->frame = malloc(sizeof(float) * (size_t)(n + 1));
    s->low = calloc((size_t)n, sizeof(float));
    s->mid = calloc((size_t)n, sizeof(float));
    s->high = calloc((size_t)n, sizeof(float));
    s->onset = calloc((size_t)n, sizeof(float));
    s->rms = calloc((size_t)n, sizeof(float));
    s->sal = calloc((size_t)n * TR_MIDI_N, sizeof(float));
    s->bass = malloc(sizeof(int) * (size_t)n);
    s->lead = malloc(sizeof(int) * (size_t)n);
    s->gate = calloc((size_t)n, sizeof(bool));
    if (!s->frame || !s->low || !s->mid || !s->high || !s->onset || !s->rms || !s->sal || !s->bass || !s->lead || !s->gate) {
        Steps_Free(s);
        return false;
    }
    for (int i = 0; i < n; i++) s->bass[i] = s->lead[i] = NOTE_NONE;
    return true;
}

/* The STFT frames nearer to step i's grid line than to any other: half a step
 * either side, which is how a tracker quantises a hit. The grid runs past the
 * last frame (ExtendBeats finishes the final beat), so a window that lies
 * wholly outside the analysed frames gets an empty range (f0 > f1) and the
 * step stays silent; clamping it would hand every trailing step the final
 * frame's hit again. A window that only partly overlaps is clamped. */
static void StepFramesNearest(const Steps *s, int i, int frames, int *f0, int *f1) {
    float before = i > 0 ? s->frame[i - 1] : 2.0f * s->frame[0] - s->frame[1];
    int a = (int)ceilf(0.5f * (before + s->frame[i]));
    int b = (int)ceilf(0.5f * (s->frame[i] + s->frame[i + 1])) - 1;
    if (b < a) a = b = (int)lroundf(s->frame[i]);
    if (a > frames - 1 || b < 0) { *f0 = 1; *f1 = 0; return; }
    *f0 = ClampI(a, 0, frames - 1);
    *f1 = ClampI(b, 0, frames - 1);
}

/* Raises *best to v[f] when frame f is a local maximum of v. */
static void PeakMax(const float *v, int frames, int f, float *best) {
    if (f > 0 && v[f] < v[f - 1]) return;
    if (f + 1 < frames && v[f] <= v[f + 1]) return;
    if (v[f] > *best) *best = v[f];
}

/* Picks the strongest candidate in sal[lo..hi] (indices into a TR_MIDI_N
 * vector) that is a real note: clearly above the range's median and with an
 * actual fundamental, so a melody's harmonics cannot assemble a phantom bass
 * an octave below. Returns MIDI or NOTE_NONE.
 *
 * prev is the voice's last pitch (or NOTE_NONE): a melody mostly moves by
 * steps and small leaps, so a candidate within `near` semitones of it is
 * favoured and one more than an octave away is discounted when choosing
 * between near-equal peaks. Only the choice is biased; whether the winner is
 * loud enough to count as a note at all is still judged on its raw salience,
 * so continuity never conjures a note out of noise. */
static int PickNote(const float *sal, const float *fund, int lo, int hi, int prev, int near, float *tmp) {
    int n = hi - lo + 1;
    memcpy(tmp, sal + lo, sizeof(float) * (size_t)n);
    float median = MedianInPlace(tmp, n);
    int best = -1;
    float bestV = 0.0f;
    for (int j = lo; j <= hi; j++) {
        if (sal[j] <= 0.0f || fund[j] < 0.15f * sal[j]) continue;
        float v = sal[j];
        if (prev != NOTE_NONE) {
            int gap = abs(j + TR_MIDI_LO - prev);
            if (gap <= near) v *= 1.35f;
            else if (gap > 12) v *= 0.8f;
        }
        if (v > bestV) { bestV = v; best = j; }
    }
    if (best < 0 || sal[best] <= 2.5f * median) return NOTE_NONE;
    return best + TR_MIDI_LO;
}

/* Width-3 median filter where NOTE_NONE is just another value: a lone
 * outlier between two equal neighbours takes their value. */
static void SmoothPitches(int *seq, int n) {
    if (n < 3) return;
    int *copy = malloc(sizeof(int) * (size_t)n);
    if (!copy) return;
    memcpy(copy, seq, sizeof(int) * (size_t)n);
    for (int i = 1; i < n - 1; i++) {
        if (copy[i - 1] == copy[i + 1] && copy[i] != copy[i - 1]) seq[i] = copy[i - 1];
    }
    free(copy);
}

/* A pitch that shows up for one step and is gone the next is a flicker of
 * the analysis (a passing harmonic, a vocal slide), not a note a tracker
 * would write. A change of pitch has to last two steps, or land on a clear
 * onset, to start a new note; otherwise the step keeps the note already
 * sounding (or stays silent if none is). Silence is never invented or
 * removed. */
static void StabilisePitches(int *seq, const bool *restrike, int n) {
    int sounding = NOTE_NONE;
    for (int i = 0; i < n; i++) {
        int p = seq[i];
        if (p == NOTE_NONE) { sounding = NOTE_NONE; continue; }
        if (p == sounding) continue;
        if (restrike[i] || (i + 1 < n && seq[i + 1] == p)) { sounding = p; continue; }
        seq[i] = sounding;
    }
}

/* The major or minor triad that best explains a bar's chroma (energy per
 * pitch class). Each of the 24 templates scores its root in full, its third
 * and fifth at 0.8, and is charged 0.3 for the energy in the nine classes it
 * does not own, so a melody passing through three adjacent semitones no longer
 * outranks the chord actually sounding underneath. Writes the chord tones as
 * pitch classes 0..11 in ascending order. */
static void BestTriad(const float pc[12], int triad[3]) {
    float total = 0.0f;
    for (int c = 0; c < 12; c++) total += pc[c];
    int bestRoot = 0, bestThird = 4;
    float bestScore = -HUGE_VALF;
    for (int root = 0; root < 12; root++) {
        for (int minor = 0; minor < 2; minor++) {
            int third = (root + (minor ? 3 : 4)) % 12, fifth = (root + 7) % 12;
            float in = pc[root] + pc[third] + pc[fifth];
            float score = pc[root] + 0.8f * (pc[third] + pc[fifth]) - 0.3f * (total - in);
            if (score > bestScore) { bestScore = score; bestRoot = root; bestThird = third; }
        }
    }
    triad[0] = bestRoot; triad[1] = bestThird; triad[2] = (bestRoot + 7) % 12;
    for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (triad[b] < triad[a]) { int t = triad[a]; triad[a] = triad[b]; triad[b] = t; }
}

/* ------------------------------------------------------------------- run */

void Transcribe_DefaultOptions(TranscribeOptions *o) {
    memset(o, 0, sizeof(*o));
}

static int8_t *ToneCell(Song *s, int track, int step) {
    return &s->patterns[s->order[step / SONG_STEPS]].tone[track][step % SONG_STEPS];
}

static uint8_t *DrumCell(Song *s, int step) {
    return &s->patterns[s->order[step / SONG_STEPS]].drums[step % SONG_STEPS];
}

/* Writes one tone track: a NOTE where the pitch changes or an onset clearly
 * restrikes it, a HOLD while it rings on, NONE in silence. */
static int WriteToneTrack(Song *out, int track, const int *seq, const bool *restrike, int n, int pad, int written) {
    int notes = 0;
    for (int i = 0; i < n && pad + i < written; i++) {
        int p = seq[i];
        int8_t v;
        if (p == NOTE_NONE) v = NOTE_NONE;
        else if (i == 0 || seq[i - 1] != p || restrike[i]) { v = (int8_t)p; notes++; }
        else v = NOTE_HOLD;
        *ToneCell(out, track, pad + i) = v;
    }
    return notes;
}

bool Transcribe_Run(const float *mono, int frames, int sampleRate, const TranscribeOptions *opt, Song *out, TranscribeReport *rep) {
    TranscribeOptions defaults;
    TranscribeReport local;
    if (!rep) rep = &local;
    memset(rep, 0, sizeof(*rep));
    if (!opt) { Transcribe_DefaultOptions(&defaults); opt = &defaults; }
    Song_Init(out, "Imported");
    out->swing = 0.0f;
    rep->bpm = out->bpm;
    rep->patterns = out->patternCount;
    if (!mono || frames <= 0 || sampleRate <= 0) return false;

    /* Analysis near 22 kHz is plenty for anything a chip can play back, and it
     * halves the work on a 44.1 or 48 kHz file. */
    const float *x = mono;
    float *owned = NULL;
    int n = frames, rate = sampleRate;
    while (rate > TR_MAX_RATE && n >= 2) {
        int n2 = n / 2;
        float *d = malloc(sizeof(float) * (size_t)n2);
        if (!d) { free(owned); return false; }
        for (int i = 0; i < n2; i++) d[i] = 0.5f * (x[2 * i] + x[2 * i + 1]);
        free(owned);
        owned = d; x = d; n = n2; rate /= 2;
    }
    rep->seconds = (float)n / (float)rate;
    if (n < rate) { free(owned); return false; }

    /* Remove the DC offset. A cheap mic or a tape or vinyl transfer sits the
     * whole recording a little off zero, and the note gates below compare
     * each step's RMS with 2% of the track's: an offset of a few hundredths
     * passes that on its own, so a silent intro would be gated on and the arp
     * pass would fit chords to the noise floor. The STFT does not care (a
     * constant has no flux), so only the gates see the difference. */
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += (double)x[i];
    float mean = (float)(sum / (double)n);
    if (mean != 0.0f && isfinite(mean)) {
        if (!owned) {
            owned = malloc(sizeof(float) * (size_t)n);
            if (!owned) return false;
            memcpy(owned, x, sizeof(float) * (size_t)n);
            x = owned;
        }
        for (int i = 0; i < n; i++) owned[i] -= mean;
    }

    bool ok = false;
    Onsets on; memset(&on, 0, sizeof(on));
    Steps st; memset(&st, 0, sizeof(st));
    Fft pitchFft; memset(&pitchFft, 0, sizeof(pitchFft));
    float *tracked = NULL, *beats = NULL, *mag = NULL, *tmp = NULL;
    bool *restrike = NULL;
    int nTracked = 0, nBeats = 0;

    if (!Onsets_Compute(x, n, rate, &on)) goto done;

    /* ---- tempo and beats ---- */
    float confidence = 0.0f;
    int estBpm = Transcribe_EstimateBpm(on.onset, on.frames, on.fps, &confidence);
    bool fixedTempo = opt->bpm > 0;
    int gridBpm = fixedTempo ? ClampI(opt->bpm, SONG_BPM_MIN, SONG_BPM_MAX) : (estBpm > 0 ? estBpm : TR_FALLBACK_BPM);
    float period = on.fps * 60.0f / (float)gridBpm;
    rep->bpmConfidence = confidence;

    bool periodic = fixedTempo || estBpm > 0;
    if (periodic) {
        tracked = TrackBeats(on.onset, on.frames, period, fixedTempo, &nTracked);
        if (!tracked) goto done;
    }
    double startFrame = -(double)(TR_WIN / 2) / TR_HOP;  /* sample 0 */
    double endFrame = ((double)n - TR_WIN / 2) / TR_HOP;  /* the last sample */
    beats = ExtendBeats(tracked, nTracked, period, startFrame, endFrame, &nBeats);
    if (!beats || nBeats < 2) goto done;

    if (fixedTempo) {
        out->bpm = gridBpm;
    } else if (nTracked >= 2) {
        /* The mean tracked interval, not the median: beats sit on whole frames,
         * so a median would be quantised to a frame (about 3% at 120 bpm). */
        float meanInterval = (tracked[nTracked - 1] - tracked[0]) / (float)(nTracked - 1);
        out->bpm = ClampI((int)lroundf(60.0f * on.fps / meanInterval), SONG_BPM_MIN, SONG_BPM_MAX);
    } else {
        out->bpm = ClampI(gridBpm, SONG_BPM_MIN, SONG_BPM_MAX);
    }

    /* ---- the sixteenth grid ---- */
    int nSteps = 4 * (nBeats - 1);
    if (!Steps_Alloc(&st, nSteps)) goto done;
    for (int k = 0; k < nBeats - 1; k++) {
        float iv = (beats[k + 1] - beats[k]) * 0.25f;
        for (int j = 0; j < 4; j++) st.frame[4 * k + j] = j == 0 ? beats[k] : beats[k] + (float)j * iv;
    }
    st.frame[nSteps] = beats[nBeats - 1];

    double sumSq = 0.0;
    for (int i = 0; i < n; i++) sumSq += (double)x[i] * (double)x[i];
    float trackRms = (float)sqrt(sumSq / (double)n);

    for (int i = 0; i < nSteps; i++) {
        int f0, f1;
        StepFramesNearest(&st, i, on.frames, &f0, &f1);
        for (int f = f0; f <= f1; f++) {
            /* Only a flux peak marks a hit, and it goes to the nearest grid
             * line: the frames leading up to a peak are the onset sliding into
             * the window and a low kick can peak a frame ahead of the full-band
             * onset the beat sits on, so plain containment would hand the hit
             * to the step before. */
            PeakMax(on.low, on.frames, f, &st.low[i]);
            PeakMax(on.mid, on.frames, f, &st.mid[i]);
            PeakMax(on.high, on.frames, f, &st.high[i]);
            PeakMax(on.onset, on.frames, f, &st.onset[i]);
        }
        long s0 = ClampI((int)lround(FrameToSample(st.frame[i])), 0, n);
        long s1 = ClampI((int)lround(FrameToSample(st.frame[i + 1])), 0, n);
        if (s1 > s0) {
            double acc = 0.0;
            for (long s = s0; s < s1; s++) acc += (double)x[s] * (double)x[s];
            st.rms[i] = (float)sqrt(acc / (double)(s1 - s0));
        }
        st.gate[i] = st.rms[i] > 0.02f * trackRms;
    }

    /* ---- pitch per step ---- */
    const int pitchBins = TR_PITCH_WIN / 2 + 1;
    float pitchBinHz = (float)rate / (float)TR_PITCH_WIN;
    if (!Fft_Init(&pitchFft, TR_PITCH_WIN)) goto done;
    mag = malloc(sizeof(float) * (size_t)pitchBins);
    tmp = malloc(sizeof(float) * (size_t)(TR_MIDI_N > nSteps ? TR_MIDI_N : nSteps));
    if (!mag || !tmp) goto done;
    {
        float fund[TR_MIDI_N], penalised[TR_MIDI_N];
        int prevBass = NOTE_NONE, prevLead = NOTE_NONE; /* the last pitch each voice sang */
        for (int i = 0; i < nSteps; i++) {
            double centre = FrameToSample(0.5 * ((double)st.frame[i] + (double)st.frame[i + 1]));
            Fft_Magnitude(&pitchFft, x, n, lround(centre) - TR_PITCH_WIN / 2, mag);
            float *sal = st.sal + (size_t)i * TR_MIDI_N;
            for (int j = 0; j < TR_MIDI_N; j++) {
                sal[j] = Transcribe_Salience(mag, pitchBins, pitchBinHz, TR_MIDI_LO + j);
                fund[j] = MagAt(mag, pitchBins, pitchBinHz, MidiHz(TR_MIDI_LO + j));
            }
            if (!st.gate[i]) continue;
            st.bass[i] = PickNote(sal, fund, 0, TR_BASS_HI - TR_MIDI_LO, prevBass, 5, tmp);
            /* A bass overtone is a poor melody candidate: dampen those so the
             * bass does not also sing an octave up. */
            memcpy(penalised, sal, sizeof(penalised));
            if (st.bass[i] != NOTE_NONE) {
                for (int h = 0; h < 5; h++) {
                    int m = st.bass[i] + kBassOvertone[h];
                    if (m >= TR_LEAD_LO && m <= TR_MIDI_HI) penalised[m - TR_MIDI_LO] *= 0.6f;
                }
            }
            st.lead[i] = PickNote(penalised, fund, TR_LEAD_LO - TR_MIDI_LO, TR_MIDI_N - 1, prevLead, 7, tmp);
            if (st.bass[i] != NOTE_NONE) prevBass = st.bass[i];
            if (st.lead[i] != NOTE_NONE) prevLead = st.lead[i];
        }
    }
    SmoothPitches(st.bass, nSteps);
    SmoothPitches(st.lead, nSteps);

    /* A step whose onset stands clearly out of the crowd restrikes a repeated
     * note instead of holding it. */
    restrike = calloc((size_t)nSteps, sizeof(bool));
    if (!restrike) goto done;
    {
        double m = 0.0, v = 0.0;
        for (int i = 0; i < nSteps; i++) m += st.onset[i];
        m /= nSteps;
        for (int i = 0; i < nSteps; i++) v += ((double)st.onset[i] - m) * ((double)st.onset[i] - m);
        float thr = (float)(m + 1.5 * sqrt(v / nSteps));
        for (int i = 0; i < nSteps; i++) restrike[i] = st.onset[i] > thr && thr > 0.0f;
    }
    /* The melody is the busiest voice and the one a flicker hurts most; the
     * bass already moves slowly and keeps its raw sequence. */
    StabilisePitches(st.lead, restrike, nSteps);

    /* ---- downbeat: the beat phase that carries the most kick ---- */
    int pad = 0;
    {
        double phase[4] = {0, 0, 0, 0};
        for (int k = 0; k < nBeats - 1; k++) phase[k % 4] += st.low[4 * k];
        int bestK = 0;
        for (int k = 1; k < 4; k++) if (phase[k] > phase[bestK]) bestK = k;
        pad = ((4 - bestK) % 4) * 4;
    }

    /* ---- patterns ---- */
    int maxPat = opt->maxPatterns > 0 ? ClampI(opt->maxPatterns, 1, SONG_MAX_PATTERNS) : SONG_MAX_PATTERNS;
    int totalSteps = pad + nSteps;
    int needed = (totalSteps + SONG_STEPS - 1) / SONG_STEPS;
    if (needed < 1) needed = 1;
    rep->truncated = needed > maxPat;
    int wantPat = needed > maxPat ? maxPat : needed;
    for (int p = 1; p < wantPat; p++) {
        int idx;
        if (!Song_AddPattern(out, &idx) || !Song_OrderInsert(out, out->orderCount, idx)) break;
    }
    int written = totalSteps < out->patternCount * SONG_STEPS ? totalSteps : out->patternCount * SONG_STEPS;

    /* ---- tone tracks ---- */
    rep->bassNotes = WriteToneTrack(out, TRACK_BASS, st.bass, restrike, nSteps, pad, written);
    rep->leadNotes = WriteToneTrack(out, TRACK_LEAD, st.lead, restrike, nSteps, pad, written);

    /* ---- chords as an arpeggio, one triad per bar ---- */
    if (!opt->noArp) {
        for (int bar = 0; bar * 16 < written; bar++) {
            float pc[12] = {0};
            bool heard = false;
            for (int S = bar * 16; S < bar * 16 + 16 && S < written; S++) {
                int i = S - pad;
                if (i < 0 || i >= nSteps || !st.gate[i]) continue;
                heard = true;
                const float *sal = st.sal + (size_t)i * TR_MIDI_N;
                for (int j = 0; j < TR_MIDI_N; j++) pc[(TR_MIDI_LO + j) % 12] += sal[j];
            }
            if (!heard) continue;
            int triad[3];
            BestTriad(pc, triad);
            for (int S = bar * 16; S < bar * 16 + 16 && S < written; S++) *ToneCell(out, TRACK_ARP, S) = (int8_t)(60 + triad[(S - bar * 16) % 3]);
        }
    }

    /* ---- drums from the band fluxes ---- */
    if (!opt->noDrums) {
        float medLow, medMid, medHigh, madLow, madMid, madHigh, maxLow = 0.0f, maxMid = 0.0f, maxHigh = 0.0f;
        memcpy(tmp, st.low, sizeof(float) * (size_t)nSteps); medLow = MedianInPlace(tmp, nSteps);
        madLow = MadInPlace(st.low, nSteps, medLow, tmp);
        memcpy(tmp, st.mid, sizeof(float) * (size_t)nSteps); medMid = MedianInPlace(tmp, nSteps);
        madMid = MadInPlace(st.mid, nSteps, medMid, tmp);
        memcpy(tmp, st.high, sizeof(float) * (size_t)nSteps); medHigh = MedianInPlace(tmp, nSteps);
        madHigh = MadInPlace(st.high, nSteps, medHigh, tmp);
        for (int i = 0; i < nSteps; i++) {
            if (st.low[i] > maxLow) maxLow = st.low[i];
            if (st.mid[i] > maxMid) maxMid = st.mid[i];
            if (st.high[i] > maxHigh) maxHigh = st.high[i];
        }
        /* The MAD rule finds hits on a busy mix; the floor keeps a near-silent
         * band (where median and MAD are both about zero) from firing on noise. */
        float thrLow = fmaxf(medLow + 1.5f * madLow, fmaxf(0.1f * maxLow, 1e-3f));
        float thrMid = fmaxf(medMid + 1.5f * madMid, fmaxf(0.1f * maxMid, 1e-3f));
        float thrHigh = fmaxf(medHigh + 1.0f * madHigh, fmaxf(0.1f * maxHigh, 1e-3f));
        float raisedHigh = fmaxf(medHigh + 0.5f * madHigh, fmaxf(0.05f * maxHigh, 1e-3f));
        for (int i = 0; i < nSteps && pad + i < written; i++) {
            uint8_t bits = 0;
            bool kick = st.low[i] > thrLow;
            bool snare = st.mid[i] > thrMid && st.high[i] > raisedHigh;
            bool hat = st.high[i] > thrHigh;
            /* A kick alone also bumps the mid band; only a clearly louder mid
             * hit on the same step is a real kick-plus-snare. */
            if (kick && snare && st.mid[i] < 1.2f * thrMid) snare = false;
            if (kick) { bits |= DRUM_KICK; rep->kicks++; }
            if (snare) { bits |= DRUM_SNARE; rep->snares++; }
            if (hat) { bits |= DRUM_HAT; rep->hats++; }
            *DrumCell(out, pad + i) = bits;
        }
    }

    rep->bpm = out->bpm;
    rep->steps = written;
    rep->patterns = out->patternCount;
    ok = true;

done:
    free(restrike); free(tmp); free(mag); free(beats); free(tracked);
    Fft_Free(&pitchFft);
    Steps_Free(&st);
    Onsets_Free(&on);
    free(owned);
    if (!ok) {
        /* Leave a clean song behind whatever went wrong half way. */
        Song_Init(out, "Imported");
        rep->bpm = out->bpm;
        rep->patterns = 1;
    }
    return ok;
}
