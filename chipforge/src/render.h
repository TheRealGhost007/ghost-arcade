#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "editor.h"
#include "transcribe.h"
#include "compose.h"

/* Chipforge's screens. main.c owns every piece of state and hands it over as
 * const pointers; each Render_* function draws one whole frame (it owns
 * Win_BeginFrame / Win_EndFrame). The menu, settings, updates and how-to
 * screens come from ghost-common's ui.c and are not here.
 *
 * Landscape like the launcher: 960x640 logical. */
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 640

/* A small modal box over whatever screen is underneath; NULL means none. */
typedef struct {
    const char *title;  /* "Unsaved changes" */
    const char *hint;   /* "Enter saves    D discards    Esc stays" */
} RenderConfirm;

/* What the player is doing, read once per frame by main.c. */
typedef struct {
    bool playing;
    bool looping;    /* Shift+Space: one pattern repeats instead of following the order */
    int orderPos;    /* the player's position (valid while playing) */
    int step;
    int pattern;     /* the pattern the synth is on */
} RenderPlayback;

/* What the tracker screen shows beyond the editor itself, so a mode (the
 * picker, in-key typing; recording later) only fills fields instead of
 * growing Render_Editor's argument list. Zeroed means "nothing extra". */
typedef struct {
    /* The picker band, which replaces the status strip while a picker (Fill,
     * Chords, Vary, Grow) is open: pickLabel at T16 ("Fill drums  <
     * Breakbeat >"), pickInfo at T8 beside it when there is room ("on Am F
     * C G"), pickDetail at T8 under it (the keys). NULL pickLabel = the
     * ordinary strip. */
    const char *pickLabel, *pickInfo, *pickDetail;
    /* NULL-terminated lines that replace the key-hint card, whose usual keys
     * do nothing while a mode is open. */
    const char *const *hintCard;
    /* The key the step column names bar chords in. NULL = the song's own
     * (stored, else guessed); the picker passes the key it is trying, so the
     * names follow Up/Down. */
    const ComposeKey *chordKey;
    /* In-key typing (F4) is on: the header shows the "in key" tag beside the
     * song's key, and the hint card's piano lines say the rows play the
     * scale. The key itself ("key A minor", or "key A minor?" when it is a
     * guess) is read from the song and shown whether or not this is set. */
    bool scaleKeys;
    /* Recording over the loop (F10). recPass > 0 puts "REC 3" in danger in
     * the PLAYING slot and catchLabel ("1/8") on the readout line under it as
     * "catch 1/8"; the band (pickLabel/pickDetail) says what is recording.
     * countIn 4..1 replaces the band's label with the count, "4 3 2 1" at T16
     * growing a number a beat. livePass, when not NULL, is the pass being
     * played, caught onto an otherwise empty pattern: its notes, holds and
     * hits are drawn in dim over pattern livePattern while it is shown. */
    int recPass;
    int countIn;
    const char *catchLabel;
    const SongPattern *livePass;
    int livePattern;
} RenderExtras;

/* The tracker screen. x may be NULL. */
void Render_Editor(const Editor *ed, const RenderPlayback *pb, const RenderConfirm *confirm, const RenderExtras *x);

/* How visible the editor's status line is now: 1 while fresh, fading to 0
 * once it has had its time. A screen that shows the status elsewhere (the
 * assign list's note) shows it for exactly as long as the editor would. */
float Render_StatusAlpha(const Editor *ed);

/* A centred list: the song browser, audio browser, presets and assign screens. */
typedef struct {
    const char *title;          /* T16 in light */
    const char *note;           /* optional T8 line under the title */
    bool noteLit;               /* the note is news (a status line): light, not dim */
    const char *const *rows;    /* count entries */
    const char *const *right;   /* optional right-hand text per row (array or entries may be NULL) */
    const bool *highlight;      /* optional: rows drawn in light (e.g. games playing this song) */
    int count;
    int selected;
    const char *empty;          /* shown when count is 0 */
    const char *hints;          /* T8 in dim at the bottom */
    const RenderConfirm *confirm; /* optional overlay */
} RenderList;
void Render_List(const RenderList *l);

/* The import screen in its three moments: choosing options, the one frame
 * drawn while the file decodes, and the report (or the failure message). */
typedef enum { IMPORT_OPTIONS, IMPORT_LISTENING, IMPORT_REPORT, IMPORT_FAILED } ImportPhase;
typedef struct {
    ImportPhase phase;
    const char *fileName;       /* just the name, for the title */
    int tempoBpm;               /* 0 = auto */
    bool drums, chords;
    int selected;               /* 0 tempo, 1 drums, 2 chords, 3 import */
    const TranscribeReport *report; /* IMPORT_REPORT */
    const char *message;        /* IMPORT_FAILED */
    bool song;                  /* IMPORT_FAILED for a .gsong that would not open: titled as opening a song */
} RenderImport;
void Render_Import(const RenderImport *im);

/* The Timing test (Settings > Timing): 16 clicks, the first `warmup` to find
 * the beat; a line of beats with each tap marked where it fell, then the
 * result. */
typedef enum { CALIBRATE_RUNNING, CALIBRATE_DONE, CALIBRATE_FAILED, CALIBRATE_NO_SOUND } CalibratePhase;
typedef struct {
    CalibratePhase phase;
    int beats, warmup;
    double beatSeconds;
    double now;                 /* seconds since the first click; < 0 before it sounds */
    const double *taps;         /* in the same seconds */
    int tapCount;
    int resultMs;               /* CALIBRATE_DONE */
    int currentMs;              /* the setting as it stands */
} RenderCalibrate;
void Render_Calibrate(const RenderCalibrate *c);

/* The name prompt: title, the text so far (with a blinking cursor), a hint. */
void Render_Prompt(const char *title, const char *text, const char *hint);

/* The confirm box itself, drawn INSIDE a frame; the screens above call it for
 * you when they are given a RenderConfirm. */
void Render_ConfirmOverlay(const RenderConfirm *c);

#endif
