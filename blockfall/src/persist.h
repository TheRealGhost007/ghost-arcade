#ifndef PERSIST_H
#define PERSIST_H

#include <stdbool.h>
#include <stddef.h>

#define KEYNAME_LEN 16
/* A key slot with no key (a second key you cleared, Rotate left when Z was
 * already yours). Written as NONE, which every build reads as a key that is
 * never pressed. */
#define KEY_NONE_NAME "NONE"

/* The pad's buttons for each action (controls.h's actions, in its order:
 * Move left, Move right, Soft drop, Hard drop, Rotate right, Rotate left,
 * Hold, Pause, Restart, Rotate 180, Undo), two each, as uiinput.h's
 * PadButton, or PAD_NONE. */
#define CONTROL_ACTIONS 11
/* The Next queue (Settings > Game, 3.2.0): pieces the Next box shows. */
#define QUEUE_SHOWN_MIN 1
#define QUEUE_SHOWN_DEFAULT 5
#define QUEUE_SHOWN_LIMIT 6
#define PAD_NONE (-1)

/* Soft drop speeds (Settings > Feel): a row every 60, 30 (as it always
 * was) or 15 ms, or Instant: straight down, without locking. */
enum { SOFT_SLOW, SOFT_NORMAL, SOFT_FAST, SOFT_INSTANT, SOFT_SPEEDS };
/* The well's grid (Settings > Look). */
enum { GRID_ON, GRID_FAINT, GRID_OFF, GRID_STYLES };
/* The ghost piece (ghost_piece): hidden, an outline with a light fill (as
 * it always was), or solid. */
enum { GHOST_OFF, GHOST_OUTLINE, GHOST_SOLID, GHOST_STYLES };
#define REPEAT_DELAY_MIN 50
#define REPEAT_DELAY_MAX 300
#define REPEAT_DELAY_DEFAULT 170
#define REPEAT_SPEED_MIN 0
#define REPEAT_SPEED_MAX 100
#define REPEAT_SPEED_DEFAULT 35

/* Key bindings are stored as human-readable names (e.g. "LEFT", "A") rather
 * than raw keycodes so the config file in $XDG_CONFIG_HOME is easy for a
 * user to hand-edit. input.c maps names <-> raylib key codes. */
typedef struct {
    char left[KEYNAME_LEN];
    char left2[KEYNAME_LEN];
    char right[KEYNAME_LEN];
    char right2[KEYNAME_LEN];
    char softDrop[KEYNAME_LEN];
    char softDrop2[KEYNAME_LEN];
    char rotate[KEYNAME_LEN];
    char rotate2[KEYNAME_LEN];
    char hardDrop[KEYNAME_LEN];
    char hold[KEYNAME_LEN];
    char pause[KEYNAME_LEN];
    char restart[KEYNAME_LEN];
    char quit[KEYNAME_LEN];    /* the Pause row's second key (it was "Back to menu") */
    /* 3.0.0: Rotate left (Z), and second keys for Hard drop, Rotate left,
     * Hold and Restart (none at first). A config.ini from before has none of
     * these lines: Rotate left takes Z unless one of your keys already is Z. */
    char hardDrop2[KEYNAME_LEN];
    char rotateLeft[KEYNAME_LEN];
    char rotateLeft2[KEYNAME_LEN];
    char hold2[KEYNAME_LEN];
    char restart2[KEYNAME_LEN];
    /* 3.2.0: Rotate 180 (a half turn in one press), no key until you set
     * one (rotate_half, rotate_half2; the pad's pad_rotate_half). */
    char rotateHalf[KEYNAME_LEN];
    char rotateHalf2[KEYNAME_LEN];
    /* 3.5.0: Undo, the Practice room's key (undo, undo2; the pad's
     * pad_undo): U and RB as it comes. A config.ini from before has no
     * undo line: Undo takes U unless one of your keys already is U (then
     * it starts with none), and RB unless a button of yours already is. */
    char undo[KEYNAME_LEN];
    char undo2[KEYNAME_LEN];
    /* 3.0.0: the pad's buttons, two per action (pad_left, pad_left2, ...).
     * The defaults are the buttons the pad always had, plus X for Rotate
     * left and LB as Hold's second. */
    int pad[CONTROL_ACTIONS][2];
    /* 3.0.0, Settings > Feel: auto-repeat (das_ms, 50-300, 170 as it always
     * was), repeat speed (arr_ms, 0-100, 35; 0 goes straight to the wall)
     * and the soft drop's speed (soft_drop_speed). */
    int repeatDelayMs;
    int repeatSpeedMs;
    int softDropSpeed;
    /* 3.0.0, Settings > Look: the grid (grid) and patterns on the blocks for
     * colour-blind players (block_patterns). */
    int grid;
    bool blockPatterns;
    /* 3.0.0, Settings > Game: the run pauses when the window loses focus
     * (auto_pause, on as since 2.1.0), and How to play opens on the next
     * launch (show_howto, cleared once it has). */
    bool autoPause;
    bool showHowTo;
    /* 3.2.0, Settings > Game: pieces the Next box shows (next_queue, 1 to
     * 6, 5 as it comes): the next one large and the rest small. The rules
     * always draw six ahead, so it changes nothing about the pieces. */
    int nextQueue;
    /* 3.5.0, the Practice room's options (practice_level, practice_gravity,
     * practice_lock, practice_hold, practice_garbage, practice_hints), as
     * you last set them. Missing lines are the defaults: level 1, gravity
     * on, the lock on, Hold once per piece, no garbage, hints on. */
    int practiceLevel;
    bool practiceGravity;
    bool practiceLock;
    bool practiceInfiniteHold;
    int practiceGarbage;
    bool practiceHints;
    int volumePercent; /* 0-100 */
    bool audioEnabled;
    /* The last play setup, so "Start game" opens on what you played last. */
    int setupMode;    /* GameMode */
    int setupPreset;  /* index into the board presets in main.c; the last one is Custom */
    int setupWidth, setupHeight; /* used when the preset is Custom */
    /* Motion: Full (false) or Reduced (true): slides, glides and drops
     * become plain cuts and particles are halved (comfort.h). Added in
     * 2.2.0 as reduced_motion; an older build ignores the line. */
    bool reducedMotion;
    /* The controls card in play is hidden (Settings, "Controls card"). Added
     * in 2.3.0 as hide_controls; an older build ignores the line. */
    bool hideControls;
    /* Piece motion: Smooth (false) or Snap (true): a tap and a gravity step
     * glide, or the piece moves cell by cell. Added in 2.4.0 as piece_snap;
     * an older build ignores the line. */
    bool pieceSnap;
    /* READY then GO at the start of a run (only GO after a restart). Added
     * in 2.4.0 as ready_go; missing means On. */
    bool readyGo;
    /* Your Restart key alone restarts at once (true), or asks "are you
     * sure?" first when the run has points (false, the default); the pad's
     * Restart button always asks. Added in 2.5.0 as quick_restart; an older
     * build ignores the line. */
    bool quickRestart;
    /* 3, 2, 1 over the board after a pause before play goes on. Added in
     * 2.5.0 as resume_countdown; off by default. */
    bool resumeCountdown;
    /* The ghost piece: GHOST_OFF, GHOST_OUTLINE (the default) or GHOST_SOLID
     * (3.0.0). Any value but 0 shows it (2.5.0 reads Solid as shown), and a
     * value this build does not know is kept as it is and drawn as the
     * outline. Added in 2.5.0 as ghost_piece. */
    int ghostPiece;
} Settings;

typedef struct {
    long highScore;
    int highLevel;
    int highLines;
} SaveData;

void Settings_Default(Settings *s);
/* Loads from $XDG_CONFIG_HOME/blockfall/config.ini, falling back to
 * defaults for any missing/invalid fields. Returns true if a file was
 * found and read. */
bool Settings_Load(Settings *s);
bool Settings_Save(const Settings *s);

void SaveData_Default(SaveData *d);
/* Loads from $XDG_DATA_HOME/blockfall/save.dat. Returns true if a file
 * was found and read. */
bool SaveData_Load(SaveData *d);
bool SaveData_Save(const SaveData *d);

/* $XDG_DATA_HOME/blockfall/<filename> (made if missing), for Blockfall's
 * other files such as this machine's own score table. */
bool Persist_DataFile(const char *filename, char *out, size_t outSize);

/* config.ini's pad button names ("A", "LB", "MENU", "UP"...), NONE for
 * none. Unknown names read as PAD_NONE. */
const char *Persist_PadName(int button);
int Persist_PadFromName(const char *name);
/* "slow" "normal" "fast" "instant"; "on" "faint" "off". */
const char *Persist_SoftName(int speed);
const char *Persist_GridName(int grid);

#endif
