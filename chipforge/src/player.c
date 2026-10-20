/* Chipforge's playback: ghost-common's Synth streamed through raylib, set up
 * exactly like the games' music.c (2048-frame buffers, 44100 Hz mono 16-bit,
 * a callback on the audio thread) but with none of the mode/crossfade logic.
 *
 * Two synths share the one song pointer:
 *
 *   sSong      the song as it plays; frozen (not advanced) while stopped
 *   sPreview   only ever sounds Synth_Preview notes, so the editor can audition
 *              a key press while the song is stopped without moving it
 *
 * The preview synth is parked by setting samplesIntoStep to a huge negative
 * number: Synth_Render triggers a step only when samplesIntoStep reaches the
 * step length, and from -1e12 that takes centuries at 44.1 kHz, so it never
 * plays a note of its own. This relies on the Synth struct being public
 * (musicsynth.h) and on Synth_Render counting samples one at a time. */
#define _POSIX_C_SOURCE 199309L /* clock_gettime: the audio thread and the game thread need one monotonic clock */
#include "player.h"
#include "musicsynth.h"
#include "jam.h"
#include "raylib.h"

#include <limits.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define RATE 44100
#define CHUNK 256
#define PARKED_STEP (-1e12)

/* jam.c counts the synth's step times in this rate's samples. */
_Static_assert(RATE == JAM_RATE, "player.c and jam.c must agree on the sample rate");
/* At most one step can start inside a chunk (the click relies on it): the
 * shortest step, at SONG_BPM_MAX, is far longer than a chunk. */
_Static_assert(CHUNK * 4 < RATE * 60 / (SONG_BPM_MAX * 4), "a chunk must be shorter than the shortest step");

static AudioStream sStream;
static bool sReady = false;      /* the stream is loaded and playing */
static const Song *sSongPtr = NULL;
static Synth sSong;
static Synth sPreview;

/* Written by the game thread, read by the audio thread. A torn read of an int
 * or float here costs at worst one odd buffer. */
static volatile bool sPlaying = false;
static volatile float sGain = 0.55f;
/* The song synth's intensity (the layers a game would play); the preview
 * synth always sounds at 1.0 so a typed note is heard whatever this says. */
static volatile float sIntensity = 1.0f;
/* The metronome while recording (Player_SetClick). */
static volatile bool sClickOn = false;

/* A Play request is applied on the audio thread, so the synth is never
 * re-initialised while a render is half way through it. The handoff is a
 * seqlock: the game thread bumps sRequestSeq to odd, writes the three fields,
 * and bumps it to even; the audio thread copies the fields and uses the copy
 * only if the sequence was even before and unchanged after. So two requests
 * microseconds apart, such as Shift+Space and Enter in the same frame, can
 * never be applied as a mix of both (the odd value is what catches a copy
 * that overlaps the stores), and a request that lands mid-apply is picked up
 * on the next chunk rather than lost. Volatile on every field keeps the
 * compiler from moving the stores across the bumps; the hardware ordering is
 * the same bet music.c makes. */
typedef struct {
    int orderPos;
    int step;
    int loopPattern;
    int testBpm;        /* > 0: the Timing test's clicks instead of the song */
    int testBeats;
} PlayRequest;
static volatile PlayRequest sRequest;
static volatile unsigned sRequestSeq = 0; /* odd while the game thread is writing sRequest */
static volatile unsigned sAppliedSeq = 0; /* the (even) sequence last applied to the synth */

static bool RequestPending(void) {
    return sRequestSeq != sAppliedSeq;
}

/* The playhead as last published by the audio thread. The game thread reads
 * only these, never the synth itself, which ApplyPendingPlay re-initialises
 * (and briefly zeroes) on the audio thread. */
static volatile int sOutOrderPos = 0, sOutStep = 0, sOutPattern = 0;

/* Synth_SetPosition parks the synth one step BEFORE the requested one so that
 * the first rendered sample triggers it (step -1 before step 0). Until that
 * trigger the synth's own step is one too low, so a fresh position publishes
 * the step about to play, and Fill leaves that alone until the synth has
 * actually moved: with no audio device nothing ever renders, and this parked
 * readout is what makes Player_Step report the step Enter was pressed on. */
static int sParkedStep = INT_MIN; /* the synth's step while parked; INT_MIN once it has triggered */

static void Publish(const Synth *s, int step) {
    sOutOrderPos = s->orderPos;
    sOutStep = step;
    sOutPattern = Synth_CurrentPattern(s);
}

/* Right after Synth_Init or Synth_SetPosition: s->step is -1..62, so the step
 * about to play is s->step + 1. */
static void PublishParked(const Synth *s) {
    sParkedStep = s->step;
    Publish(s, s->step + 1);
}

/* --- the clock the recording follows ---------------------------------------
 * Audio-thread state: which request is playing and how many frames of it
 * (song or click test) have been rendered. After each callback that rendered
 * any, the thread publishes (generation, the frame its callback began with,
 * the wall time it was called at) under a seqlock like the Play request's,
 * the reader retrying while the sequence is odd or moved. */
static unsigned sPlayGen = 0;
static long long sPlayFrames = 0;
static volatile unsigned sClockSeq = 0;
static volatile unsigned sPubGen = 0;
static volatile long long sPubFrame = 0;
static volatile double sPubAt = 0.0;
static volatile bool sPubValid = false;

static double NowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void PublishClock(long long firstFrame, double at) {
    sClockSeq++;
    sPubGen = sPlayGen;
    sPubFrame = firstFrame;
    sPubAt = at;
    sPubValid = true;
    sClockSeq++;
}

/* --- the click ---------------------------------------------------------------
 * A short sine blip mixed after the synths: its own voice, so a click never
 * steals the preview voice from a note being played along. Audio thread only. */
static float sTickEnv = 0.0f;
static double sTickPhase = 0.0;
static double sTickHz = 0.0;

/* The Timing test's state (audio thread): frames between clicks, how many. */
static bool sTestMode = false;
static double sTestPeriod = 0.0;
static int sTestBeats = 0, sTestBeat = 0;

static void StartTick(bool accent) {
    sTickEnv = 1.0f;
    sTickPhase = 0.0;
    sTickHz = accent ? 1568.0 : 1175.0; /* G6 on the bar, D6 on the other beats: above every track's register */
}

static void MixTick(short *out, int from, int to, float gain) {
    for (int i = from; i < to && sTickEnv > 0.002f; i++) {
        sTickPhase += sTickHz / (double)RATE;
        if (sTickPhase >= 1.0) sTickPhase -= 1.0;
        float v = sinf((float)(6.283185307179586 * sTickPhase)) * sTickEnv * 0.35f * gain;
        sTickEnv *= 0.9990f; /* about 15 ms to half: a tick, not a tone */
        int x = out[i] + (int)(v * 32000.0f);
        if (x > 32767) x = 32767;
        if (x < -32768) x = -32768;
        out[i] = (short)x;
    }
}

static void ParkPreview(void) {
    Synth_Init(&sPreview, sSongPtr, RATE);
    Synth_SetIntensity(&sPreview, 1.0f);
    sPreview.samplesIntoStep = PARKED_STEP;
}

/* Re-initialising drops any ringing envelope from before the jump, so a stale
 * note never carries over into the new start position. Returns without
 * applying when the game thread was mid-write; the caller tries again on the
 * next chunk, which keeps the audio callback from ever spinning. */
static void ApplyPendingPlay(void) {
    unsigned seq = sRequestSeq;
    if (seq & 1u) return; /* the game thread is writing */
    PlayRequest r;
    r.orderPos = sRequest.orderPos;
    r.step = sRequest.step;
    r.loopPattern = sRequest.loopPattern;
    r.testBpm = sRequest.testBpm;
    r.testBeats = sRequest.testBeats;
    if (seq != sRequestSeq) return; /* it wrote while we copied */

    Synth_Init(&sSong, sSongPtr, RATE);
    Synth_SetIntensity(&sSong, sIntensity);
    Synth_SetLoopPattern(&sSong, r.loopPattern);
    Synth_SetPosition(&sSong, r.orderPos, r.step);
    PublishParked(&sSong);
    sTestMode = r.testBpm > 0;
    if (sTestMode) {
        sTestPeriod = (double)RATE * 60.0 / (double)r.testBpm;
        sTestBeats = r.testBeats;
        sTestBeat = 0;
    }
    /* A new generation of the clock: frame 0 is the next one rendered. */
    sPlayGen = seq;
    sPlayFrames = 0;
    sAppliedSeq = seq;
}

/* The editor changes the tempo while the song plays; the synth only derives
 * samplesPerStep in Synth_Init, so refresh it before every chunk. */
static void RefreshTempo(Synth *s) {
    int bpm = (s->song && s->song->bpm > 0) ? s->song->bpm : 120;
    s->samplesPerStep = (double)RATE * 60.0 / ((double)bpm * 4.0);
}

/* Where in this chunk a click starts (-1 for none) and whether it is the
 * higher one. While the song plays: on the frame its next step fires, when
 * that step is a beat. In the Timing test: on the test's own beats. */
static int ClickInChunk(int chunk, bool *accent) {
    if (sTestMode) {
        if (sTestBeat >= sTestBeats) return -1;
        long long due = (long long)ceil((double)sTestBeat * sTestPeriod) - sPlayFrames;
        if (due >= chunk) return -1;
        *accent = (sTestBeat % 4) == 0;
        sTestBeat++;
        return due < 0 ? 0 : (int)due;
    }
    if (!sClickOn) return -1;
    int due = Jam_FramesToTrigger(&sSong);
    if (due >= chunk) return -1;
    int next = (sSong.step + 1) % SONG_STEPS; /* the step that fires; -1 parked becomes 0 */
    if (next % 4 != 0) return -1;
    *accent = (next % 16) == 0;
    return due;
}

static void Fill(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    unsigned int done = 0;
    /* When this burst was asked for: its first frame starts playing a fixed
     * device latency later, which the Timing test measures. */
    double calledAt = NowSeconds();
    long long lastAdvancedEnd = -1; /* offset just past the last chunk that moved the clock on */
    while (done < frames) {
        unsigned int chunk = frames - done > CHUNK ? CHUNK : frames - done;
        if (RequestPending()) ApplyPendingPlay();

        float gain = sGain;
        sSong.gain = gain;
        sPreview.gain = gain;
        /* Like the gain, picked up here so the change lands between chunks
         * rather than mid-render; Synth_SetIntensity clamps to 0..1. */
        Synth_SetIntensity(&sSong, sIntensity);
        RefreshTempo(&sSong);
        RefreshTempo(&sPreview);

        /* Worked out before rendering, from the synth's state as it enters
         * the chunk, so the click lands on the sample its step starts on. */
        bool playing = sPlaying, accent = false;
        int clickAt = playing ? ClickInChunk((int)chunk, &accent) : -1;
        if (playing && !sTestMode) Synth_Render(&sSong, out + done, (int)chunk);
        else memset(out + done, 0, chunk * sizeof(short));
        /* A request applied in a chunk that is not rendered (Play then Stop
         * before the callback ran, or sPlaying not yet visible) leaves the
         * synth parked one step short of the readout PublishParked set, so
         * publish the real position only once the synth has moved. */
        if (sSong.step != sParkedStep) {
            sParkedStep = INT_MIN;
            Publish(&sSong, sSong.step);
        }

        /* The preview synth is always rendered (it is silent once its
         * envelopes have died) and mixed on top with a hard clip. */
        int16_t pv[CHUNK];
        Synth_Render(&sPreview, pv, (int)chunk);
        for (unsigned int i = 0; i < chunk; i++) {
            int v = out[done + i] + pv[i];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[done + i] = (short)v;
        }

        /* The tail of the last click, then the new one from its frame. */
        MixTick(out + done, 0, clickAt >= 0 ? clickAt : (int)chunk, gain);
        if (clickAt >= 0) {
            StartTick(accent);
            MixTick(out + done, clickAt, (int)chunk, gain);
        }

        if (playing) {
            sPlayFrames += chunk;
            lastAdvancedEnd = (long long)(done + chunk);
        }
        done += chunk;
    }
    /* Measured from the last chunk that played: if the Play landed mid-burst
     * (or a chunk sat silent before sPlaying showed), the burst's first frame
     * is that many frames BEFORE the Play's frame 0, a negative frame. */
    if (lastAdvancedEnd >= 0) PublishClock(sPlayFrames - lastAdvancedEnd, calledAt);
}

void Player_Init(const Song *song) {
    sSongPtr = song;
    sPlaying = false;
    sAppliedSeq = sRequestSeq; /* nothing pending */
    Synth_Init(&sSong, song, RATE);
    Synth_SetIntensity(&sSong, sIntensity);
    PublishParked(&sSong); /* reads as position 0, step 0 until the first Play */
    ParkPreview();
    sReady = false;

    if (!IsAudioDeviceReady()) return;
    SetAudioStreamBufferSizeDefault(2048);
    sStream = LoadAudioStream(RATE, 16, 1);
    if (!IsAudioStreamValid(sStream)) return;
    SetAudioStreamCallback(sStream, Fill);
    PlayAudioStream(sStream);
    sReady = true;
}

void Player_Shutdown(void) {
    sPlaying = false;
    if (!sReady) return;
    StopAudioStream(sStream);
    UnloadAudioStream(sStream);
    sReady = false;
}

void Player_Play(int orderPos, int step, int loopPattern) {
    sRequestSeq++; /* odd: writing */
    sRequest.orderPos = orderPos;
    sRequest.step = step;
    sRequest.loopPattern = loopPattern;
    sRequest.testBpm = 0;
    sRequest.testBeats = 0;
    sRequestSeq++; /* even: complete */
    /* Without a stream there is no audio thread to pick the request up, so
     * apply it here; the position readouts still make sense headless. */
    if (!sReady) ApplyPendingPlay();
    sPlaying = true;
}

void Player_Stop(void) {
    sPlaying = false;
}

bool Player_IsPlaying(void) {
    return sPlaying;
}

/* Until the audio thread has taken a pending request the synth still holds
 * the old position, so report the requested one instead. Only the game thread
 * writes sRequest, so reading it from here needs no sequence check. */
int Player_OrderPos(void) {
    if (RequestPending()) return sRequest.orderPos;
    return sOutOrderPos;
}

int Player_Step(void) {
    if (RequestPending()) return sRequest.step;
    return sOutStep;
}

int Player_Pattern(void) {
    if (RequestPending()) {
        if (sRequest.loopPattern >= 0) return sRequest.loopPattern;
        if (!sSongPtr || sSongPtr->patternCount <= 0) return -1;
        if (sSongPtr->orderCount <= 0) return 0;
        int oc = sSongPtr->orderCount;
        int pos = ((sRequest.orderPos % oc) + oc) % oc;
        int p = sSongPtr->order[pos];
        return p < sSongPtr->patternCount ? p : 0;
    }
    return sOutPattern;
}

void Player_Preview(int track, int note) {
    if (track < 0 || track >= SONG_TRACKS) return;
    /* The synth keeps one pending preview, taken only when the audio thread
     * next renders: a callback renders its whole buffer at once, so that can
     * be tens of milliseconds away. Pads pressed together (one call per pad)
     * or in frames just apart would each overwrite the last and only the
     * final drum would sound, so a drum preview still waiting gains the new
     * bits instead. If the audio thread takes it while this merges, a drum
     * that just fired starts again a chunk later: a flam, never a lost hit. */
    if (track == TRACK_DRUMS && sPreview.previewTrack == TRACK_DRUMS) note |= sPreview.previewNote;
    Synth_Preview(&sPreview, track, note);
}

void Player_SetVolume(int percent0to100) {
    if (percent0to100 < 0) percent0to100 = 0;
    if (percent0to100 > 100) percent0to100 = 100;
    /* 0.55 is music.c's BaseGain: the same headroom the games leave. */
    sGain = (float)percent0to100 / 100.0f * 0.55f;
}

void Player_SetIntensity(float intensity0to1) {
    if (intensity0to1 < 0.0f) intensity0to1 = 0.0f;
    else if (!(intensity0to1 <= 1.0f)) intensity0to1 = 1.0f; /* above 1, or NaN, which no comparison catches */
    sIntensity = intensity0to1;
}

PlayerClock Player_Clock(void) {
    PlayerClock c = {false, 0, 0.0, 0.0};
    bool valid = false;
    long long frame = 0;
    /* The audio thread holds the sequence odd for a few stores; a reader
     * that keeps colliding just keeps last frame's view (valid stays false
     * for this frame, which costs nothing). */
    for (int tries = 0; tries < 64; tries++) {
        unsigned seq = sClockSeq;
        if (seq & 1u) continue;
        unsigned gen = sPubGen;
        frame = sPubFrame;
        double at = sPubAt;
        valid = sPubValid;
        if (seq != sClockSeq) { valid = false; continue; }
        c.gen = gen;
        c.at = at;
        break;
    }
    c.seconds = (double)frame / (double)RATE;
    /* Only the game thread writes sRequestSeq, so this is the latest Play. */
    c.current = valid && c.gen == sRequestSeq;
    return c;
}

double Player_Now(void) {
    return NowSeconds();
}

void Player_SetClick(bool on) {
    sClickOn = on;
}

void Player_ClickTest(int bpm, int beats) {
    if (bpm < 1) bpm = 1;
    sRequestSeq++;
    sRequest.orderPos = 0;
    sRequest.step = 0;
    sRequest.loopPattern = -1;
    sRequest.testBpm = bpm;
    sRequest.testBeats = beats;
    sRequestSeq++;
    if (!sReady) ApplyPendingPlay();
    sPlaying = true;
}
