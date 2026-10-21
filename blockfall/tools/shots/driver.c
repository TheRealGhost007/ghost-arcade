/* Blockfall screenshot harness, adapted from Ghost Launcher's
 * tools/shots/driver.c. It runs the real game (src/main.c, renamed
 * blockfall_main by main_wrap.c) in a HIDDEN window with input from a script.
 * Every raylib input query is linked to a __wrap_ function here
 * (-Wl,--wrap=..., see build.sh), the audio device is never opened,
 * fullscreen only flips a flag and ghost-sync is never started. Time is the
 * script's: every frame is exactly 1/60 s, and seeds are fixed, so two runs of
 * one script draw the same pixels.
 *
 * Pictures come from winscale's logical 620x720 render texture, read just
 * before it is closed each frame, so they work although the window is never
 * shown. run.sh builds the temp HOME and checks with hyprctl that the window
 * is never mapped.
 *
 * The block wipe into and out of a run is off unless the script sets
 * SHOTS_WIPE=1 (`#! env SHOTS_WIPE=1`): the scripts written before it shoot
 * the first frames of a run as they always did.
 *
 * The Daily challenge (3.6.0) is always the same day here: the shared daily
 * code's Daily_Seed and Daily_Mode are wrapped to answer for SHOTS_DAILY
 * (YYYYMMDD, 20261020 unless set), so the twist and the pieces repeat. A
 * daily run keeps the day's seed (Game_InitDaily and a daily Game_Restart
 * are not reseeded). SHOTS_DAILYLAUNCH=1 starts the game with --daily, as
 * Ghost Launcher's D key does.
 *
 * Particles and other effects draw from raylib's GetRandomValue, which the
 * harness answers from its own sequence: it starts again at every run start
 * (Game_InitConfig, Game_Restart), so what a run looks like does not depend
 * on how long the menus before it took. How to play's demo games
 * (howtodemo.c) are passed straight through: `game` and `expect game` are
 * always the run's.
 *
 * Script, one command per line, '#' comments:
 *   wait N | tap KEY [KEY..] | hold KEY N | down KEY | up KEY
 *   pad on|off | pad tap|down|up BTN | pad hold BTN N | stick X Y
 *   mouse X Y (logical 620x720) | click [left|right|middle|back] [X Y]
 *   mdown BTN | mup BTN | wheel N (notches, positive = away from you)
 *   focus on|off | write PATH TEXT | close | quits | log TEXT
 *   seed N                    seed for the next Game_Init/InitConfig/Restart
 *   game CMD ...              edit the running Game between two frames:
 *       clear | row Y PATTERN (Y=0 bottom row; . empty, IOTSZJL, # mixed,
 *                              G garbage: Dig, 3.4.0)
 *       piece T [ROT [X [Y]]] | next T | queue T,O,S,Z,J | hold T|none
 *       queuepower I NAME [CELL]  (queued piece I, 1 to 5, carries NAME)
 *       power NAME [CELL] | nextpower NAME [CELL] | holdpower NAME [CELL]
 *       score N | level N | lines N | combo N | b2b 0|1 | elapsed S
 *       timeleft S | freeze S | slow S | best N | holdused 0|1
 *       rise S | dug N            (Dig: the next garbage row comes in S
 *                                  seconds; the rows dug so far)
 *   shot NAME
 *   tapshot KEY NAME          KEY goes down and that same frame is shot (the
 *                             frame a press acts: a clear's frame 1); it goes
 *                             up a frame later, where the next command starts
 *   expect screen NAME        MENU SETUP SETTINGS SCORES UPDATES HOWTO PLAYING
 *   expect game KEY VALUE     phase score level lines filled combo mode w h
 *                             current px py won zen hold power hidden pieces
 *                             queue (the pieces after next, '|'-joined:
 *                             the rules' five, 3.2.0) tspin (none mini
 *                             full) perfect (0/1) dug rises risein (%.2f s
 *                             to the next rise) garbage (rows holding
 *                             garbage) gaps (the garbage rows' gap columns,
 *                             bottom first, '|'-joined; 'x' a row that is
 *                             not plain garbage: Dig, 3.4.0)
 *   expect view KEY VALUE     what the play screen was handed: best unit
 *                             newbest focuslost chip sound volume card
 *                             (card is the controls card's eight rows of
 *                             keys, '|'-joined: move soft rotate rotate-left
 *                             hard hold pause restart; padcard the same for
 *                             the pad's caps)
 *                             pad (the card shows pad buttons) passed (Best
 *                             was passed) showcard intensity (the music's,
 *                             %.2f: danger.h)
 *   expect hud KEY VALUE      what the play screen last drew: place (side or
 *                             strip), cell, card (1 shown), callouts (top to
 *                             bottom, '|'-joined, or none), popup (the newest
 *                             or none), shown (the score as drawn), danger
 *                             (0/1), dangerframe (the frame's redness, %.2f),
 *                             queue (small pieces drawn after Next, by
 *                             name, '|'-joined, or none: 3.2.0),
 *                             free (rows clear above the stack), nextblocked
 *                             (the next piece's red outline), timer (Sprint's
 *                             clock as drawn, "1:12.40"), split (its split
 *                             line, "10: -1.4 s", or none), splitgold,
 *                             linesgold (0/1), risefill (Dig's Next row bar
 *                             as drawn, %.2f), risewarn (it was red, 0/1),
 *                             risedrop (px the stack was drawn low after a
 *                             rise: 3.4.0)
 *   expect feel KEY VALUE     how play moved in the last frame (feel.h):
 *                             stage (none flash zip sweep drop), rows, total
 *                             (the clear's length, %.2f s), lift (px, the most
 *                             a falling row is above its place), dx dy (px the
 *                             piece is drawn from its place), pop, fill (%.2f),
 *                             settle, bits (pixels in flight, or >0), ghost
 *                             flight slide ready squash smear impact bevel
 *                             streak quadframe (0/1), cue (none ready go),
 *                             power (none bomb-blink bomb-shock laser-charge
 *                             laser-beam thaw: the power-up show drawn last),
 *                             frost (%.2f), afterimages (the Slow's, drawn),
 *                             sweepbar (cells from the left wall, or -1.0)
 *   expect awaiting 0|1       Settings is waiting for a key or a button
 *   expect scores KEY VALUE   mode board count world period blink (the
 *                             blinking row, 0-based, or -1) oldbest
 *   expect fullscreen 0|1
 *   expect cursor arrow|hand  the pointer the game last asked for
 *   expect app KEY VALUE      main_wrap.c's drv_app: daily (on/off: the run
 *                             is a daily), dailysub (the menu item's second
 *                             line), daily:KEY (the Daily screen: twist date
 *                             datewords best rank runs streak beststreak
 *                             worldrank here world), pause (open/closed),
 *                             pausepage (main/settings), dialog (its title or
 *                             none), results (on/off), resultsdone, againchip
 *                             (the results card's "again" cap as the last frame drew it, or none), card:KEY
 *                             (title sub big newbest best rank note stat0..9),
 *                             shown (the count-up), wipe (0/1), ghost,
 *                             quickrestart, countdown, scorestab, boards
 *                             (the picker's list, '|'-joined), focus, hover, look:LABEL
 *                             (normal hover focused pressed disabled absent),
 *                             depth, under, device, hints, slide, drop, toast,
 *                             mode, board, summary, motion, bestsboard,
 *                             best:Marathon|Sprint|Ultra|Power, yourbest,
 *                             yourrank, worldbest, scroll,
 *                             skylanded, skyheight; Settings: settab, setrow
 *                             (the focused row's label, Tabs), awaiting,
 *                             capture (none, or key|pad:ACTION:1|2), note,
 *                             dialogset, key:ACTION:SLOT and pad:ACTION:SLOT
 *                             (config.ini's names; '_' for spaces), das, arr,
 *                             softdrop, grid, patterns, autopause, showhowto,
 *                             nextqueue (Settings > Game's Next queue, 3.2.0),
 *                             strip (the Feel strip's piece column);
 *                             How to play: howtopage, howtofirst, demopress,
 *                             democlears ('1,4' or none), demovariant,
 *                             democallout, democurrent, demonext, demohold
 *                             (the demo well's pieces, by name; hold none)
 *   mark sounds               then: expect sounds LIST (what played since,
 *                             "sfx:N,jingle:NAME,..."; "none" for nothing)
 *   expect pixels X Y W H below|above|white|nowhite N
 *                             in the last shot: every pixel's brightest
 *                             channel is below N / some pixel's is at least N /
 *                             some pixel has every channel at least N (white) /
 *                             none has (nowhite)
 *   expect file PATH TEXT | expect nofile PATH TEXT
 * run.sh also reads '#! after-file PATH TEXT' and '#! after-nofile PATH TEXT'
 * lines and checks them in the temp HOME once the game has exited.
 */
#define _GNU_SOURCE
#include "raylib.h"
#include "rlgl.h"
#include "ui.h"
#include "game.h"
#include "persist.h"
#include "render.h"
#include "scoresview.h"
#include "sfx.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "daily.h"

int blockfall_main(int argc, char **argv);

void __real_EndDrawing(void);
void __real_EndTextureMode(void);
void __real_PollInputEvents(void);
void __real_SetConfigFlags(unsigned int flags);
void __real_InitWindow(int width, int height, const char *title);
void __real_SetTargetFPS(int fps);
bool __real_IsWindowState(unsigned int flag);
RenderTexture2D __real_LoadRenderTexture(int width, int height);
void __real_Game_Init(Game *g, uint64_t seed, long highScore, int highLevel, int highLines);
void __real_Game_InitConfig(Game *g, GameConfig cfg, uint64_t seed, long highScore, int highLevel, int highLines);
void __real_Game_Restart(Game *g, uint64_t seed);
void __real_Game_InitPractice(Game *g, GameConfig cfg, PracticeRules rules, uint64_t seed);
void __real_Game_InitDaily(Game *g, GameConfig cfg, DailyRules rules, uint64_t seed);
uint64_t __real_Daily_Seed(void);
const char *__real_Daily_Mode(void);
void __real_Render_Frame(const Game *g, const RenderView *view);
void __real_ScoresView_Draw(ScoresViewInfo *v);
void __real_Wipe_Start(Wipe *w, bool motion);
void __real_Sfx_Play(int id);
void __real_Sfx_Jingle(SfxJingle j);
void __real_Sfx_PlayVaried(int id, float variance);
void __real_Sfx_PlayPitched(int id, float pitch);
const char *drv_screen(void);
void drv_app(const char *key, char *out, size_t n);
bool drv_is_demo(const Game *g);

/* ------------------------------------------------------------- the log */
static long sTick = 0;
static Game *sGame = NULL;
static bool sStarted = false; /* blockfall_main is running: drv_screen can be asked */
static const char *Screen(void) { return sStarted ? drv_screen() : "NONE"; }

static void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[shots t=%ld] ", sTick);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static const char *PhaseName(GamePhase p) {
    switch (p) {
        case GS_PLAYING: return "PLAYING";
        case GS_PAUSED: return "PAUSED";
        case GS_GAMEOVER: return "GAMEOVER";
    }
    return "?";
}

static void Where(const char *why) {
    if (sGame)
        Log("%s: screen=%s phase=%s mode=%s %dx%d score=%ld level=%d lines=%d combo=%d filled=%d cur=%d rot=%d px=%d py=%d hold=%d power=%d",
            why, Screen(), PhaseName(sGame->phase), Game_ModeName(sGame->cfg.mode), sGame->board.w, sGame->board.h, sGame->score,
            sGame->level, sGame->linesCleared, sGame->combo, Board_CountFilled(&sGame->board), (int)sGame->current, sGame->rotation,
            sGame->px, sGame->py, (int)sGame->hold, (int)sGame->curPower);
    else
        Log("%s: screen=%s (no game)", why, Screen());
}

/* ---------------------------------------------------------------- names */
typedef struct { const char *name; int key; } KeyName;
static const KeyName kKeys[] = {
    {"A", KEY_A}, {"B", KEY_B}, {"C", KEY_C}, {"D", KEY_D}, {"E", KEY_E}, {"F", KEY_F}, {"G", KEY_G}, {"H", KEY_H},
    {"I", KEY_I}, {"J", KEY_J}, {"K", KEY_K}, {"L", KEY_L}, {"M", KEY_M}, {"N", KEY_N}, {"O", KEY_O}, {"P", KEY_P},
    {"Q", KEY_Q}, {"R", KEY_R}, {"S", KEY_S}, {"T", KEY_T}, {"U", KEY_U}, {"V", KEY_V}, {"W", KEY_W}, {"X", KEY_X},
    {"Y", KEY_Y}, {"Z", KEY_Z}, {"0", KEY_ZERO}, {"1", KEY_ONE}, {"2", KEY_TWO}, {"3", KEY_THREE}, {"4", KEY_FOUR},
    {"5", KEY_FIVE}, {"6", KEY_SIX}, {"7", KEY_SEVEN}, {"8", KEY_EIGHT}, {"9", KEY_NINE},
    {"SPACE", KEY_SPACE}, {"MINUS", KEY_MINUS}, {"EQUAL", KEY_EQUAL}, {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},
    {"F11", KEY_F11}, {"ENTER", KEY_ENTER}, {"ESCAPE", KEY_ESCAPE}, {"TAB", KEY_TAB}, {"BACKSPACE", KEY_BACKSPACE},
    {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT},
    {"HOME", KEY_HOME}, {"END", KEY_END}, {"PAGE_UP", KEY_PAGE_UP}, {"PAGE_DOWN", KEY_PAGE_DOWN}, {"DELETE", KEY_DELETE},
    {"LEFT_BRACKET", KEY_LEFT_BRACKET}, {"RIGHT_BRACKET", KEY_RIGHT_BRACKET},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT}, {"LEFT_CONTROL", KEY_LEFT_CONTROL},
    {"LEFT_ALT", KEY_LEFT_ALT}, {"KP_ADD", KEY_KP_ADD}, {"KP_SUBTRACT", KEY_KP_SUBTRACT},
    {"RIGHT_CONTROL", KEY_RIGHT_CONTROL}, {"RIGHT_ALT", KEY_RIGHT_ALT}, {"COMMA", KEY_COMMA}, {"PERIOD", KEY_PERIOD},
    {"SLASH", KEY_SLASH}, {"SEMICOLON", KEY_SEMICOLON}, {"APOSTROPHE", KEY_APOSTROPHE}, {"F5", KEY_F5}, {"F12", KEY_F12},
};
#define NKEYNAMES ((int)(sizeof(kKeys) / sizeof(kKeys[0])))

static const KeyName *KeyByName(const char *s) {
    if (strcasecmp(s, "ESC") == 0) s = "ESCAPE";
    if (strcasecmp(s, "SHIFT") == 0) s = "LEFT_SHIFT";
    if (strcasecmp(s, "ALT") == 0) s = "LEFT_ALT";
    for (int i = 0; i < NKEYNAMES; i++) if (strcasecmp(kKeys[i].name, s) == 0) return &kKeys[i];
    return NULL;
}

static const struct { const char *name; int button; } kPadNames[] = {
    {"A", GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {"B", GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {"X", GAMEPAD_BUTTON_RIGHT_FACE_LEFT},
    {"Y", GAMEPAD_BUTTON_RIGHT_FACE_UP}, {"LB", GAMEPAD_BUTTON_LEFT_TRIGGER_1}, {"RB", GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
    {"LT", GAMEPAD_BUTTON_LEFT_TRIGGER_2}, {"RT", GAMEPAD_BUTTON_RIGHT_TRIGGER_2}, {"VIEW", GAMEPAD_BUTTON_MIDDLE_LEFT},
    {"MENU", GAMEPAD_BUTTON_MIDDLE_RIGHT}, {"UP", GAMEPAD_BUTTON_LEFT_FACE_UP}, {"DOWN", GAMEPAD_BUTTON_LEFT_FACE_DOWN},
    {"LEFT", GAMEPAD_BUTTON_LEFT_FACE_LEFT}, {"RIGHT", GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
};

static int MouseByName(const char *s) {
    static const struct { const char *name; int button; } k[] = {
        {"left", MOUSE_BUTTON_LEFT}, {"right", MOUSE_BUTTON_RIGHT}, {"middle", MOUSE_BUTTON_MIDDLE},
        {"back", MOUSE_BUTTON_SIDE}, {"forward", MOUSE_BUTTON_EXTRA},
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (strcasecmp(s, k[i].name) == 0) return k[i].button;
    return -1;
}

static int PadByName(const char *s) {
    for (size_t i = 0; i < sizeof(kPadNames) / sizeof(kPadNames[0]); i++)
        if (strcasecmp(s, kPadNames[i].name) == 0) return kPadNames[i].button;
    return -1;
}

/* ------------------------------------------------------- device state */
#define NK 512
#define NPAD 32
static unsigned char sKey[NK], sKeyPrev[NK];
static int sKeyQueue[64], sKeyQN, sKeyQI;
static bool sPadOn = false;
static unsigned char sPad[NPAD], sPadPrev[NPAD];
static float sAxis[8];
static bool sFocused = true;
#define NMOUSE 8
static Vector2 sMouse = {310.0f, 360.0f}, sMousePrev = {310.0f, 360.0f};
static unsigned char sMouseBtn[NMOUSE], sMouseBtnPrev[NMOUSE];
static float sWheel = 0.0f;
static bool sBorderless = false;
static bool sCloseFlag = false;
static bool sVerbose = false;
static int sCursor = 0; /* the pointer the game last asked raylib for (expect cursor) */
static uint64_t sSeed = 20261014;
static int sInits = 0;

/* -------------------------------------------------------------- events */
typedef enum {
    EV_KEYDOWN, EV_KEYUP, EV_PADON, EV_PADOFF, EV_PADDOWN, EV_PADUP, EV_STICK, EV_FOCUS, EV_WRITE, EV_SHOT, EV_EXPECT,
    EV_LOG, EV_CLOSE, EV_SEED, EV_GAME, EV_MARK, EV_MOVE, EV_MDOWN, EV_MUP, EV_WHEEL,
} EvType;
typedef struct { long tick; int order; EvType type; int arg; float x, y; char text[512]; } Event;
static Event *sEv = NULL;
static int sEvN = 0, sEvCap = 0, sEvI = 0;
static long sT = 3;
static long sEndTick = 0;
static int sExpectFails = 0, sExpectsAsked = 0, sExpectsRun = 0, sShotsAsked = 0, sShotsWritten = 0, sGameFails = 0;
static bool sMustQuit = false;

static Event *AddEv(long tick, EvType type, int arg, const char *text) {
    if (sEvN == sEvCap) {
        sEvCap = sEvCap ? sEvCap * 2 : 256;
        sEv = realloc(sEv, (size_t)sEvCap * sizeof(Event));
        if (!sEv) { perror("realloc"); exit(2); }
    }
    Event *e = &sEv[sEvN];
    memset(e, 0, sizeof(*e));
    e->tick = tick;
    e->order = sEvN++;
    e->type = type;
    e->arg = arg;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    return e;
}

static int EvCmp(const void *a, const void *b) {
    const Event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;
}

static void Die(int line, const char *what, const char *arg) {
    fprintf(stderr, "script line %d: %s%s%s\n", line, what, arg ? ": " : "", arg ? arg : "");
    exit(2);
}

static void Tap(const KeyName **keys, int n, int holdTicks) {
    for (int i = 0; i < n - 1; i++) AddEv(sT, EV_KEYDOWN, keys[i]->key, NULL);
    long t = sT + (n > 1 ? 1 : 0);
    AddEv(t, EV_KEYDOWN, keys[n - 1]->key, NULL);
    AddEv(t + holdTicks, EV_KEYUP, keys[n - 1]->key, NULL);
    for (int i = 0; i < n - 1; i++) AddEv(t + holdTicks + 1, EV_KEYUP, keys[i]->key, NULL);
    sT = t + holdTicks + (n > 1 ? 2 : 1);
}

static void ParseKeys(char *rest, int line, bool hold) {
    const KeyName *keys[8];
    int n = 0, holdTicks = 1;
    for (char *tok = strtok(rest, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (hold && n > 0 && isdigit((unsigned char)tok[0])) { holdTicks = atoi(tok); break; }
        const KeyName *k = KeyByName(tok);
        if (!k) Die(line, "unknown key", tok);
        if (n < 8) keys[n++] = k;
    }
    if (n == 0) Die(line, "no key", NULL);
    if (holdTicks < 1) holdTicks = 1;
    Tap(keys, n, holdTicks);
}

static void ParsePad(char *rest, int line) {
    char verb[16] = "", btn[16] = "";
    int ticks = 1;
    if (sscanf(rest, "%15s %15s %d", verb, btn, &ticks) < 1) Die(line, "pad what?", NULL);
    if (strcmp(verb, "on") == 0) { AddEv(sT++, EV_PADON, 0, NULL); return; }
    if (strcmp(verb, "off") == 0) { AddEv(sT++, EV_PADOFF, 0, NULL); return; }
    int b = PadByName(btn);
    if (b < 0) Die(line, "unknown pad button", btn);
    if (strcmp(verb, "down") == 0) { AddEv(sT++, EV_PADDOWN, b, NULL); return; }
    if (strcmp(verb, "up") == 0) { AddEv(sT++, EV_PADUP, b, NULL); return; }
    if (strcmp(verb, "tap") == 0) ticks = 1;
    else if (strcmp(verb, "hold") != 0) Die(line, "pad tap|hold|down|up|on|off", verb);
    if (ticks < 1) ticks = 1;
    AddEv(sT, EV_PADDOWN, b, NULL);
    AddEv(sT + ticks, EV_PADUP, b, NULL);
    sT += ticks + 1;
}

/* click [BTN] [X Y]: the pointer moves there (if given), the button goes
 * down a frame later and comes up the frame after. */
static void ParseClick(char *rest, int line) {
    char a[16] = "", b[16] = "", c[16] = "";
    int n = sscanf(rest, "%15s %15s %15s", a, b, c);
    int button = MOUSE_BUTTON_LEFT;
    const char *xs = NULL, *ys = NULL;
    if (n >= 1 && !(isdigit((unsigned char)a[0]) || a[0] == '-')) {
        button = MouseByName(a);
        if (button < 0) Die(line, "unknown mouse button", a);
        if (n >= 3) { xs = b; ys = c; }
    } else if (n >= 2) { xs = a; ys = b; }
    if (xs) {
        Event *e = AddEv(sT++, EV_MOVE, 0, NULL);
        e->x = (float)atof(xs);
        e->y = (float)atof(ys);
    }
    AddEv(sT++, EV_MDOWN, button, NULL);
    AddEv(sT++, EV_MUP, button, NULL);
}

static void ParseLine(char *p, int line) {
    while (*p == ' ' || *p == '\t') p++;
    p[strcspn(p, "\r\n")] = '\0';
    if (*p == '\0' || *p == '#') return;
    char cmd[32];
    int used = 0;
    if (sscanf(p, "%31s%n", cmd, &used) != 1) return;
    char *rest = p + used;
    while (*rest == ' ' || *rest == '\t') rest++;
    float x = 0.0f, y = 0.0f;

    if (strcmp(cmd, "wait") == 0) sT += atol(rest);
    else if (strcmp(cmd, "tap") == 0) ParseKeys(rest, line, false);
    else if (strcmp(cmd, "hold") == 0) ParseKeys(rest, line, true);
    else if (strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0) {
        const KeyName *k = KeyByName(rest);
        if (!k) Die(line, "unknown key", rest);
        AddEv(sT++, cmd[0] == 'd' ? EV_KEYDOWN : EV_KEYUP, k->key, NULL);
    } else if (strcmp(cmd, "pad") == 0) ParsePad(rest, line);
    else if (strcmp(cmd, "stick") == 0) {
        if (sscanf(rest, "%f %f", &x, &y) != 2) Die(line, "stick X Y", NULL);
        Event *e = AddEv(sT++, EV_STICK, 0, NULL); e->x = x; e->y = y;
    } else if (strcmp(cmd, "mouse") == 0) {
        if (sscanf(rest, "%f %f", &x, &y) != 2) Die(line, "mouse X Y", NULL);
        Event *e = AddEv(sT++, EV_MOVE, 0, NULL); e->x = x; e->y = y;
    } else if (strcmp(cmd, "click") == 0) ParseClick(rest, line);
    else if (strcmp(cmd, "mdown") == 0 || strcmp(cmd, "mup") == 0) {
        int b = MouseByName(rest[0] ? rest : "left");
        if (b < 0) Die(line, "unknown mouse button", rest);
        AddEv(sT++, cmd[1] == 'd' ? EV_MDOWN : EV_MUP, b, NULL);
    } else if (strcmp(cmd, "wheel") == 0) { Event *e = AddEv(sT++, EV_WHEEL, 0, NULL); e->y = (float)atof(rest); }
    else if (strcmp(cmd, "focus") == 0) AddEv(sT++, EV_FOCUS, strcmp(rest, "on") == 0, NULL);
    else if (strcmp(cmd, "write") == 0) AddEv(sT, EV_WRITE, line, rest);
    else if (strcmp(cmd, "quits") == 0) sMustQuit = true;
    else if (strcmp(cmd, "shot") == 0) { AddEv(sT, EV_SHOT, line, rest); sShotsAsked++; sT++; }
    else if (strcmp(cmd, "tapshot") == 0) {
        char key[32] = "";
        int n = 0;
        if (sscanf(rest, "%31s %n", key, &n) < 1 || !rest[n]) Die(line, "tapshot KEY NAME", NULL);
        const KeyName *k = KeyByName(key);
        if (!k) Die(line, "unknown key", key);
        AddEv(sT, EV_KEYDOWN, k->key, NULL);
        AddEv(sT, EV_SHOT, line, rest + n);
        sShotsAsked++;
        AddEv(sT + 1, EV_KEYUP, k->key, NULL);
        sT++;
    }
    else if (strcmp(cmd, "expect") == 0) { AddEv(sT, EV_EXPECT, line, rest); sExpectsAsked++; }
    else if (strcmp(cmd, "log") == 0) AddEv(sT, EV_LOG, line, rest);
    else if (strcmp(cmd, "close") == 0) AddEv(sT++, EV_CLOSE, 0, NULL);
    else if (strcmp(cmd, "seed") == 0) AddEv(sT, EV_SEED, atoi(rest), NULL);
    else if (strcmp(cmd, "game") == 0) AddEv(sT, EV_GAME, line, rest);
    else if (strcmp(cmd, "mark") == 0) AddEv(sT, EV_MARK, line, rest);
    else Die(line, "unknown command", cmd);
}

static void LoadScript(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof(line), f)) ParseLine(line, ++n);
    fclose(f);
    qsort(sEv, (size_t)sEvN, sizeof(Event), EvCmp);
    sEndTick = sT + 5;
    Log("script %s: %d events, %ld frames", path, sEvN, sEndTick);
}

/* ------------------------------------------------------- script actions */
static void Unescape(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < size; p++) {
        if (p[0] == '\\' && p[1] == 'n') { out[o++] = '\n'; p++; }
        else out[o++] = *p;
    }
    out[o] = '\0';
}

static bool HomePath(const char *rel, char *out, size_t size) {
    const char *home = getenv("HOME");
    if (!home || !home[0] || rel[0] == '/' || strstr(rel, "..")) return false;
    snprintf(out, size, "%s/%s", home, rel);
    return true;
}

static void MakeParents(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
}

static void SplitFirst(const char *text, char *first, size_t firstSize, const char **rest) {
    size_t n = strcspn(text, " \t");
    snprintf(first, firstSize, "%.*s", (int)n, text);
    *rest = text + n;
    while (**rest == ' ' || **rest == '\t') (*rest)++;
}

static void DoWrite(const Event *e) {
    char rel[300], path[800], body[1024];
    const char *rest;
    SplitFirst(e->text, rel, sizeof(rel), &rest);
    if (!HomePath(rel, path, sizeof(path))) { Log("WRITE REFUSED (line %d): %s", e->arg, rel); sExpectFails++; return; }
    Unescape(rest, body, sizeof(body));
    MakeParents(path);
    FILE *f = fopen(path, "w");
    if (!f) { Log("WRITE FAILED (line %d): %s: %s", e->arg, path, strerror(errno)); sExpectFails++; return; }
    fputs(body, f);
    fclose(f);
    Log("wrote %s", rel);
}

static bool FileHas(const char *rel, const char *want) {
    char path[800], buf[16384];
    if (!HomePath(rel, path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, want) != NULL;
}

static int PieceByName(const char *s) {
    static const char *const k = "IOTSZJL";
    if (!s || !s[0]) return -1;
    const char *p = strchr(k, toupper((unsigned char)s[0]));
    return p ? (int)(p - k) : -1;
}

static PowerUp PowerByName(const char *s) {
    for (int p = PU_BOMB; p < PU_COUNT; p++) if (strcasecmp(s, Game_PowerName((PowerUp)p)) == 0) return (PowerUp)p;
    return PU_NONE;
}

static float FallFor(int level) {
    float v = 1.0f - (float)(level - 1) * 0.07f;
    return v < 0.08f ? 0.08f : v;
}

/* game CMD ...: edits the running game between two frames. */
static void DoGame(const Event *e) {
    Game *g = sGame;
    if (!g) { Log("GAME FAIL (line %d): no game yet", e->arg); sGameFails++; return; }
    char cmd[32], a[64] = "", b[64] = "", c[64] = "", d[64] = "";
    const char *rest;
    SplitFirst(e->text, cmd, sizeof(cmd), &rest);
    int n = sscanf(rest, "%63s %63s %63s %63s", a, b, c, d);
    if (strcmp(cmd, "clear") == 0) {
        memset(g->board.cells, CELL_EMPTY, sizeof(g->board.cells));
    } else if (strcmp(cmd, "row") == 0 && n >= 1) {
        int y = atoi(a);
        if (y < 0 || y >= g->board.h) { sGameFails++; Log("GAME FAIL (line %d): row %d is not on a %d-row board", e->arg, y, g->board.h); return; }
        int row = BOARD_HIDDEN + g->board.h - 1 - y;
        const char *pat = n >= 2 ? b : "";
        for (int x = 0; x < g->board.w; x++) {
            char ch = x < (int)strlen(pat) ? pat[x] : '.';
            int t = ch == '#' ? (x * 3 + y) % PIECE_COUNT : (ch == 'G' || ch == 'g' ? (int)CELL_GARBAGE : PieceByName((char[]){ch, 0}));
            g->board.cells[row][x] = (int8_t)(ch == '.' ? CELL_EMPTY : (t < 0 ? CELL_EMPTY : t));
        }
    } else if (strcmp(cmd, "piece") == 0 && n >= 1) {
        int t = PieceByName(a);
        if (t < 0) { sGameFails++; Log("GAME FAIL (line %d): piece %s", e->arg, a); return; }
        g->current = (PieceType)t;
        g->rotation = n >= 2 ? atoi(b) : 0;
        g->px = n >= 3 ? atoi(c) : (g->board.w - 4) / 2;
        g->py = n >= 4 ? atoi(d) : BOARD_HIDDEN;
        g->fallTimer = g->lockTimer = 0.0f;
        g->lockResets = 0;
        g->lowestRow = g->py;
        g->grounded = false;
        if (!Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py)) { sGameFails++; Log("GAME FAIL (line %d): piece does not fit", e->arg); }
    } else if (strcmp(cmd, "next") == 0 && n >= 1) g->next = g->queue[0] = (PieceType)PieceByName(a);
    else if (strcmp(cmd, "queuepower") == 0 && n >= 2) { /* queuepower I NAME [CELL]: a queued piece's power-up (3.2.0) */
        int i = atoi(a);
        if (i >= 1 && i < QUEUE_MAX) { g->queuePower[i] = PowerByName(b); g->queueCell[i] = n >= 3 ? atoi(c) : 0; }
    }
    else if (strcmp(cmd, "queue") == 0 && n >= 1) { /* queue T,O,S,...: the pieces after next (3.2.0) */
        int i = 1;
        for (char *tok = strtok(a, ","); tok && i < QUEUE_MAX; tok = strtok(NULL, ","), i++) {
            int t = PieceByName(tok);
            if (t >= 0) g->queue[i] = (PieceType)t;
        }
    }
    else if (strcmp(cmd, "hold") == 0 && n >= 1) g->hold = strcmp(a, "none") == 0 ? (PieceType)-1 : (PieceType)PieceByName(a);
    else if (strcmp(cmd, "power") == 0 && n >= 1) { g->curPower = PowerByName(a); g->curPowerCell = n >= 2 ? atoi(b) : 0; }
    else if (strcmp(cmd, "nextpower") == 0 && n >= 1) { g->nextPower = PowerByName(a); g->nextPowerCell = n >= 2 ? atoi(b) : 0; }
    else if (strcmp(cmd, "holdpower") == 0 && n >= 1) { g->holdPower = PowerByName(a); g->holdPowerCell = n >= 2 ? atoi(b) : 0; }
    else if (strcmp(cmd, "score") == 0) g->score = atol(a);
    else if (strcmp(cmd, "best") == 0) g->highScore = atol(a);
    else if (strcmp(cmd, "level") == 0) { g->level = atoi(a); g->fallInterval = FallFor(g->level); }
    else if (strcmp(cmd, "lines") == 0) g->linesCleared = atoi(a);
    else if (strcmp(cmd, "combo") == 0) g->combo = atoi(a);
    else if (strcmp(cmd, "b2b") == 0) g->backToBack = atoi(a) != 0;
    else if (strcmp(cmd, "elapsed") == 0) g->elapsed = (float)atof(a);
    else if (strcmp(cmd, "timeleft") == 0) g->timeLeft = (float)atof(a);
    else if (strcmp(cmd, "freeze") == 0) g->freezeTimer = (float)atof(a);
    else if (strcmp(cmd, "slow") == 0) g->slowTimer = (float)atof(a);
    else if (strcmp(cmd, "holdused") == 0) g->holdUsed = atoi(a) != 0;
    else if (strcmp(cmd, "dug") == 0) g->dug = atoi(a);
    else if (strcmp(cmd, "rise") == 0) { /* Dig: the next row in S seconds */
        float s = (float)atof(a);
        if (g->digInterval <= 0.0f) g->digInterval = Game_DigInterval(g->elapsed);
        g->digTimer = g->digInterval - s;
        if (g->digTimer < 0.0f) g->digTimer = 0.0f;
    }
    else { sGameFails++; Log("GAME FAIL (line %d): unknown game command %s", e->arg, e->text); return; }
    if (sVerbose) Log("game %s", e->text);
}

static void GameValue(const char *key, char *out, size_t n) {
    const Game *g = sGame;
    if (!g) { snprintf(out, n, "nogame"); return; }
    if (strcmp(key, "phase") == 0) snprintf(out, n, "%s", PhaseName(g->phase));
    else if (strcmp(key, "score") == 0) snprintf(out, n, "%ld", g->score);
    else if (strcmp(key, "level") == 0) snprintf(out, n, "%d", g->level);
    else if (strcmp(key, "lines") == 0) snprintf(out, n, "%d", g->linesCleared);
    else if (strcmp(key, "filled") == 0) snprintf(out, n, "%d", Board_CountFilled(&g->board));
    else if (strcmp(key, "combo") == 0) snprintf(out, n, "%d", g->combo);
    else if (strcmp(key, "mode") == 0) snprintf(out, n, "%s", Game_ModeName(g->cfg.mode));
    else if (strcmp(key, "w") == 0) snprintf(out, n, "%d", g->board.w);
    else if (strcmp(key, "h") == 0) snprintf(out, n, "%d", g->board.h);
    else if (strcmp(key, "current") == 0) snprintf(out, n, "%s", Tetromino_Name(g->current));
    else if (strcmp(key, "px") == 0) snprintf(out, n, "%d", g->px);
    else if (strcmp(key, "py") == 0) snprintf(out, n, "%d", g->py);
    else if (strcmp(key, "won") == 0) snprintf(out, n, "%d", (int)g->won);
    else if (strcmp(key, "zen") == 0) snprintf(out, n, "%d", g->zenResets);
    else if (strcmp(key, "hold") == 0) snprintf(out, n, "%s", (int)g->hold < 0 ? "none" : Tetromino_Name(g->hold));
    else if (strcmp(key, "power") == 0) snprintf(out, n, "%s", g->curPower == PU_NONE ? "none" : Game_PowerName(g->curPower));
    else if (strcmp(key, "pieces") == 0) snprintf(out, n, "%d", g->piecesLocked);
    else if (strcmp(key, "rotation") == 0) snprintf(out, n, "%d", g->rotation);
    else if (strcmp(key, "elapsed") == 0) snprintf(out, n, "%.2f", g->elapsed);
    else if (strcmp(key, "timeleft") == 0) snprintf(out, n, "%.2f", g->timeLeft);
    else if (strcmp(key, "next") == 0) snprintf(out, n, "%s", Tetromino_Name(g->next));
    else if (strcmp(key, "queue") == 0) {
        out[0] = '\0';
        for (int i = 1; i < QUEUE_MAX; i++) {
            size_t used = strlen(out);
            snprintf(out + used, n - used, "%s%s", i > 1 ? "|" : "", Tetromino_Name(g->queue[i]));
        }
    }
    else if (strcmp(key, "tspin") == 0) snprintf(out, n, "%s", g->lastTSpin == TSPIN_FULL ? "full" : (g->lastTSpin == TSPIN_MINI ? "mini" : "none"));
    else if (strcmp(key, "perfect") == 0) snprintf(out, n, "%d", (int)g->lastPerfectClear);
    else if (strcmp(key, "dug") == 0) snprintf(out, n, "%d", g->dug);
    else if (strcmp(key, "rises") == 0) snprintf(out, n, "%d", g->digRises);
    else if (strcmp(key, "risein") == 0) snprintf(out, n, "%.2f", Game_DigSecondsLeft(g));
    else if (strcmp(key, "garbage") == 0) snprintf(out, n, "%d", Game_GarbageRows(&g->board));
    else if (strcmp(key, "gaps") == 0) { /* the garbage rows' gap columns from the floor up */
        out[0] = '\0';
        int total = Board_TotalH(&g->board);
        for (int row = total - 1; row >= BOARD_HIDDEN && Board_RowHasGarbage(&g->board, row); row--) {
            size_t used = strlen(out);
            int gap = Game_GarbageGap(&g->board, row);
            if (gap >= 0) snprintf(out + used, n - used, "%s%d", used ? "|" : "", gap);
            else snprintf(out + used, n - used, "%sx", used ? "|" : "");
        }
        if (!out[0]) snprintf(out, n, "none");
    }
    else if (strcmp(key, "hidden") == 0) { /* blocks in the rows above the well */
        int c = 0;
        for (int y = 0; y < BOARD_HIDDEN; y++) for (int x = 0; x < g->board.w; x++) if (g->board.cells[y][x] != CELL_EMPTY) c++;
        snprintf(out, n, "%d", c);
    }
    else snprintf(out, n, "?unknown %.40s", key);
}

/* What main.c last handed the play screen, the Settings screen and Scores. */
static RenderView sView;
static bool sHaveView = false;
static char sScoresMode[48] = "", sScoresBoard[48] = "";
static int sScoresCount = -1, sScoresPeriod = 0, sScoresBlink = -1;
static long sScoresOldBest = 0;
static bool sScoresWorld = false;

static void ViewValue(const char *key, char *out, size_t n) {
    const RenderView *v = &sView;
    if (!sHaveView) snprintf(out, n, "noview");
    else if (strcmp(key, "best") == 0) snprintf(out, n, "%ld", v->best);
    else if (strcmp(key, "unit") == 0) snprintf(out, n, "%s", v->unit == UNIT_TIME ? "time" : "points");
    else if (strcmp(key, "newbest") == 0) snprintf(out, n, "%d", (int)v->newBest);
    else if (strcmp(key, "focuslost") == 0) snprintf(out, n, "%d", (int)v->focusLost);
    else if (strcmp(key, "chip") == 0) snprintf(out, n, "%d", (int)(v->chipTime > 0.0f));
    else if (strcmp(key, "sound") == 0) snprintf(out, n, "%d", (int)v->soundOn);
    else if (strcmp(key, "volume") == 0) snprintf(out, n, "%d", v->volume);
    else if (strcmp(key, "card") == 0) {
        const ControlWords *k = &v->keys;
        snprintf(out, n, "%s|%s|%s|%s|%s|%s|%s|%s", k->move, k->softDrop, k->rotate, k->rotateLeft, k->hardDrop, k->hold, k->pause, k->restart);
    }
    else if (strcmp(key, "padcard") == 0) {
        const ControlWords *k = &v->keys;
        snprintf(out, n, "%s|%s|%s|%s|%s|%s|%s|%s", k->padMove, k->padSoft, k->padRotate, k->padRotateLeft, k->padHard, k->padHold, k->padPause, k->padRestart);
    }
    else if (strcmp(key, "pad") == 0) snprintf(out, n, "%d", (int)v->padHints);
    else if (strcmp(key, "passed") == 0) snprintf(out, n, "%d", (int)v->passedBest);
    else if (strcmp(key, "showcard") == 0) snprintf(out, n, "%d", (int)v->showControls);
    else if (strcmp(key, "intensity") == 0) snprintf(out, n, "%.2f", v->intensity);
    else snprintf(out, n, "?unknown %.40s", key);
}

/* What the play screen last drew (Render_Inspect, read after each frame). */
static RenderInspect sInspect;
static void HudValue(const char *key, char *out, size_t n) {
    const RenderInspect *r = &sInspect;
    if (!sHaveView) snprintf(out, n, "noview");
    else if (strcmp(key, "place") == 0) snprintf(out, n, "%s", r->layout.place == PLACE_STRIP ? "strip" : "side");
    else if (strcmp(key, "cell") == 0) snprintf(out, n, "%d", r->layout.cell);
    else if (strcmp(key, "card") == 0) snprintf(out, n, "%d", (int)r->layout.showCard);
    else if (strcmp(key, "shown") == 0) snprintf(out, n, "%ld", r->shownScore);
    else if (strcmp(key, "popup") == 0) snprintf(out, n, "%s", r->popups ? r->popup : "none");
    else if (strcmp(key, "danger") == 0) snprintf(out, n, "%d", (int)r->dangerOn);
    else if (strcmp(key, "dangerframe") == 0) snprintf(out, n, "%.2f", r->dangerFrame);
    else if (strcmp(key, "free") == 0) snprintf(out, n, "%d", r->freeRows);
    else if (strcmp(key, "nextblocked") == 0) snprintf(out, n, "%d", (int)r->nextBlocked);
    else if (strcmp(key, "timer") == 0) snprintf(out, n, "%s", r->timer[0] ? r->timer : "none");
    else if (strcmp(key, "split") == 0) snprintf(out, n, "%s", r->split[0] ? r->split : "none");
    else if (strcmp(key, "splitgold") == 0) snprintf(out, n, "%d", (int)r->splitGold);
    else if (strcmp(key, "linesgold") == 0) snprintf(out, n, "%d", (int)r->linesGold);
    else if (strcmp(key, "risefill") == 0) snprintf(out, n, "%.2f", r->riseFill);
    else if (strcmp(key, "risewarn") == 0) snprintf(out, n, "%d", (int)r->riseWarn);
    else if (strcmp(key, "risedrop") == 0) snprintf(out, n, "%d", r->riseDrop);
    else if (strcmp(key, "queue") == 0) snprintf(out, n, "%s", r->queue > 0 ? r->queuePieces : "none");
    else if (strcmp(key, "callouts") == 0) {
        if (!r->callouts) { snprintf(out, n, "none"); return; }
        out[0] = '\0';
        for (int i = 0; i < r->callouts; i++) {
            size_t used = strlen(out);
            snprintf(out + used, n - used, "%s%s", i ? "|" : "", r->callout[i]);
        }
    }
    else snprintf(out, n, "?unknown %.40s", key);
}

/* How play moved in the last frame (feel.h, read through Render_Inspect). */
static void FeelValue(const char *key, char *out, size_t n) {
    const RenderInspect *r = &sInspect;
    static const char *const kStages[] = {"none", "flash", "zip", "sweep", "drop"};
    static const char *const kCues[] = {"none", "ready", "go", "count"};
    if (!sHaveView) snprintf(out, n, "noview");
    else if (strcmp(key, "stage") == 0) snprintf(out, n, "%s", r->clearStage >= 0 && r->clearStage < 5 ? kStages[r->clearStage] : "?");
    else if (strcmp(key, "power") == 0) snprintf(out, n, "%s", r->power);
    else if (strcmp(key, "frost") == 0) snprintf(out, n, "%.2f", r->frost);
    else if (strcmp(key, "afterimages") == 0) snprintf(out, n, "%d", r->afterimages);
    else if (strcmp(key, "sweepbar") == 0) snprintf(out, n, "%.1f", r->sweepBar);
    else if (strcmp(key, "rows") == 0) snprintf(out, n, "%d", r->clearRows);
    else if (strcmp(key, "total") == 0) snprintf(out, n, "%.2f", r->clearTotal);
    else if (strcmp(key, "lift") == 0) snprintf(out, n, "%d", r->lift);
    else if (strcmp(key, "dx") == 0) snprintf(out, n, "%d", r->pieceDX);
    else if (strcmp(key, "dy") == 0) snprintf(out, n, "%d", r->pieceDY);
    else if (strcmp(key, "pop") == 0) snprintf(out, n, "%d", r->pop);
    else if (strcmp(key, "fill") == 0) snprintf(out, n, "%.2f", r->fill);
    else if (strcmp(key, "settle") == 0) snprintf(out, n, "%d", r->settle);
    else if (strcmp(key, "bits") == 0) snprintf(out, n, "%d", r->bits);
    else if (strcmp(key, "ghost") == 0) snprintf(out, n, "%d", (int)r->ghost);
    else if (strcmp(key, "flight") == 0) snprintf(out, n, "%d", (int)r->flight);
    else if (strcmp(key, "slide") == 0) snprintf(out, n, "%d", (int)r->slide);
    else if (strcmp(key, "ready") == 0) snprintf(out, n, "%d", (int)r->holdReady);
    else if (strcmp(key, "squash") == 0) snprintf(out, n, "%d", (int)r->squash);
    else if (strcmp(key, "smear") == 0) snprintf(out, n, "%d", (int)r->smear);
    else if (strcmp(key, "impact") == 0) snprintf(out, n, "%d", (int)r->impact);
    else if (strcmp(key, "bevel") == 0) snprintf(out, n, "%d", (int)r->bevel);
    else if (strcmp(key, "streak") == 0) snprintf(out, n, "%d", (int)r->streak);
    else if (strcmp(key, "quadframe") == 0) snprintf(out, n, "%d", (int)r->quadFrame);
    else if (strcmp(key, "cue") == 0) snprintf(out, n, "%s", sView.cue.stage >= 0 && sView.cue.stage < 4 ? kCues[sView.cue.stage] : "?");
    else snprintf(out, n, "?unknown %.40s", key);
}

/* Sounds played since `mark sounds`. */
static char sSounds[2048] = "";
static void NoteSound(const char *what) {
    size_t used = strlen(sSounds);
    snprintf(sSounds + used, sizeof(sSounds) - used, "%s%s", used ? "," : "", what);
    if (sVerbose) Log("sound %s", what);
}
static const char *JingleName(SfxJingle j) {
    static const char *const k[JINGLE_COUNT] = {"READY", "GO", "GAME_OVER", "NEW_BEST", "LEVEL_UP", "COIN", "CONFIRM", "BACK", "WARNING"};
    return (j >= 0 && j < JINGLE_COUNT) ? k[j] : "?";
}

/* The last shot's pixels, for `expect pixels`. */
static unsigned char *sLastShot = NULL;
static int sLastW = 0, sLastH = 0;

static bool PixelsCheck(const char *args, char *why, size_t n) {
    int x, y, w, h, limit;
    char dir[16];
    if (sscanf(args, "%d %d %d %d %15s %d", &x, &y, &w, &h, dir, &limit) != 6) { snprintf(why, n, "pixels X Y W H below|above|white|nowhite N"); return false; }
    if (!sLastShot) { snprintf(why, n, "no shot yet"); return false; }
    int brightest = 0, whites = 0;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || yy < 0 || xx >= sLastW || yy >= sLastH) continue;
            /* rlReadScreenPixels gives rows top-down already */
            const unsigned char *p = sLastShot + ((size_t)yy * (size_t)sLastW + (size_t)xx) * 4;
            int hi = p[0] > p[1] ? p[0] : p[1], lo = p[0] < p[1] ? p[0] : p[1];
            if (p[2] > hi) hi = p[2];
            if (p[2] < lo) lo = p[2];
            if (hi > brightest) brightest = hi;
            if (lo >= limit) whites++; /* white-ish: every channel at least N */
        }
    snprintf(why, n, "brightest channel %d, %d pixel(s) with every channel >= %d", brightest, whites, limit);
    if (strcmp(dir, "below") == 0) return brightest < limit;
    if (strcmp(dir, "above") == 0) return brightest >= limit;
    if (strcmp(dir, "white") == 0) return whites > 0;
    if (strcmp(dir, "nowhite") == 0) return whites == 0;
    return false;
}

static void DoExpect(const Event *e) {
    sExpectsRun++;
    char what[32], arg[300], text[600];
    const char *rest;
    SplitFirst(e->text, what, sizeof(what), &rest);
    bool ok;
    if (strcmp(what, "screen") == 0) ok = strcmp(Screen(), rest) == 0;
    else if (strcmp(what, "game") == 0) {
        char got[100];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        GameValue(arg, got, sizeof(got));
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("game %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "file") == 0 || strcmp(what, "nofile") == 0) {
        SplitFirst(rest, arg, sizeof(arg), &rest);
        Unescape(rest, text, sizeof(text));
        ok = what[0] == 'f' ? FileHas(arg, text) : !FileHas(arg, text);
    } else if (strcmp(what, "view") == 0) {
        char got[160];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        ViewValue(arg, got, sizeof(got));
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("view %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "hud") == 0) {
        char got[160];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        HudValue(arg, got, sizeof(got));
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("hud %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "feel") == 0) {
        char got[160];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        FeelValue(arg, got, sizeof(got));
        /* bits >0: some pixels are in flight */
        if (strcmp(arg, "bits") == 0 && strcmp(rest, ">0") == 0) ok = atoi(got) > 0;
        else ok = strcmp(got, rest) == 0;
        if (!ok) Log("feel %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "awaiting") == 0) {
        char got[16];
        drv_app("awaiting", got, sizeof(got));
        ok = atoi(rest) == atoi(got);
        if (!ok) Log("awaiting is %s", got);
    } else if (strcmp(what, "fullscreen") == 0) {
        ok = atoi(rest) == (int)sBorderless;
    } else if (strcmp(what, "cursor") == 0) {
        const char *want = rest;
        const char *got = sCursor == MOUSE_CURSOR_POINTING_HAND ? "hand" : (sCursor == MOUSE_CURSOR_DEFAULT ? "arrow" : "other");
        ok = strcmp(got, want) == 0;
        if (!ok) Log("cursor is '%s', not '%s'", got, want);
    } else if (strcmp(what, "app") == 0) {
        char got[160];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        drv_app(arg, got, sizeof(got));
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("app %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "scores") == 0) {
        char got[96];
        SplitFirst(rest, arg, sizeof(arg), &rest);
        if (strcmp(arg, "mode") == 0) snprintf(got, sizeof(got), "%s", sScoresMode);
        else if (strcmp(arg, "board") == 0) snprintf(got, sizeof(got), "%s", sScoresBoard);
        else if (strcmp(arg, "count") == 0) snprintf(got, sizeof(got), "%d", sScoresCount);
        else if (strcmp(arg, "world") == 0) snprintf(got, sizeof(got), "%d", (int)sScoresWorld);
        else if (strcmp(arg, "period") == 0) snprintf(got, sizeof(got), "%d", sScoresPeriod);
        else if (strcmp(arg, "blink") == 0) snprintf(got, sizeof(got), "%d", sScoresBlink);
        else if (strcmp(arg, "oldbest") == 0) snprintf(got, sizeof(got), "%ld", sScoresOldBest);
        else snprintf(got, sizeof(got), "?unknown %.40s", arg);
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("scores %s is '%s', not '%s'", arg, got, rest);
    } else if (strcmp(what, "sounds") == 0) {
        const char *got = sSounds[0] ? sSounds : "none";
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("sounds were '%s', not '%s'", got, rest);
    } else if (strcmp(what, "pixels") == 0) {
        char why[96];
        ok = PixelsCheck(rest, why, sizeof(why));
        Log("pixels %s: %s", rest, why);
    } else {
        Log("EXPECT FAIL (line %d): unknown check %s", e->arg, what);
        sExpectFails++;
        return;
    }
    char why[700];
    snprintf(why, sizeof(why), "%s (line %d: %s)", ok ? "expect ok" : "EXPECT FAIL", e->arg, e->text);
    if (!ok) sExpectFails++;
    Where(why);
}

static char sShotName[512] = "";
static const char *sShotDir = ".";

static void ApplyEvent(const Event *e) {
    if (sVerbose) Log("event %d arg %d %s", (int)e->type, e->arg, e->text);
    switch (e->type) {
        case EV_KEYDOWN: if (e->arg > 0 && e->arg < NK) sKey[e->arg] = 1; if (sKeyQN < 64) sKeyQueue[sKeyQN++] = e->arg; break;
        case EV_KEYUP: if (e->arg > 0 && e->arg < NK) sKey[e->arg] = 0; break;
        case EV_PADON: sPadOn = true; break;
        case EV_PADOFF: sPadOn = false; memset(sPad, 0, sizeof(sPad)); memset(sAxis, 0, sizeof(sAxis)); break;
        case EV_PADDOWN: if (e->arg >= 0 && e->arg < NPAD) sPad[e->arg] = 1; break;
        case EV_PADUP: if (e->arg >= 0 && e->arg < NPAD) sPad[e->arg] = 0; break;
        case EV_STICK: sAxis[GAMEPAD_AXIS_LEFT_X] = e->x; sAxis[GAMEPAD_AXIS_LEFT_Y] = e->y; break;
        case EV_FOCUS: sFocused = e->arg != 0; break;
        case EV_WRITE: DoWrite(e); break;
        case EV_SHOT: snprintf(sShotName, sizeof(sShotName), "%s", e->text); break;
        case EV_EXPECT: DoExpect(e); break;
        case EV_LOG: { char why[600]; snprintf(why, sizeof(why), "log (line %d) %s", e->arg, e->text); Where(why); break; }
        case EV_CLOSE: sCloseFlag = true; Where("close requested"); break;
        case EV_SEED: sSeed = (uint64_t)e->arg; sInits = 0; break;
        case EV_GAME: DoGame(e); break;
        case EV_MARK: sSounds[0] = '\0'; break;
        case EV_MOVE: sMouse = (Vector2){e->x, e->y}; break;
        case EV_MDOWN: if (e->arg >= 0 && e->arg < NMOUSE) sMouseBtn[e->arg] = 1; break;
        case EV_MUP: if (e->arg >= 0 && e->arg < NMOUSE) sMouseBtn[e->arg] = 0; break;
        case EV_WHEEL: sWheel += e->y; break;
    }
}

/* ------------------------------------------------------ the clock moves */
static int sHoldMs = 400;
static bool sClosing = false;
static long sCloseAt = 0;

static void Advance(void) {
    sTick++;
    sCloseFlag = false;
    memcpy(sKeyPrev, sKey, sizeof(sKey));
    memcpy(sPadPrev, sPad, sizeof(sPad));
    memcpy(sMouseBtnPrev, sMouseBtn, sizeof(sMouseBtn));
    sMousePrev = sMouse;
    sWheel = 0.0f;
    sKeyQN = sKeyQI = 0;
    while (sEvI < sEvN && sEv[sEvI].tick <= sTick) ApplyEvent(&sEv[sEvI++]);

    if (sTick == 1 && sHoldMs > 0) usleep((useconds_t)sHoldMs * 1000u);
    if (!sClosing && sTick >= sEndTick) {
        sClosing = true;
        sCloseAt = sTick;
        sCloseFlag = true;
        Where("end of script, closing");
    }
    if (sClosing && sTick - sCloseAt > 0 && (sTick - sCloseAt) % 60 == 0) sCloseFlag = true;
    if (sClosing && sTick - sCloseAt > 600) {
        Where("HANG: still open 600 frames after the close request");
        fflush(stderr);
        _exit(4);
    }
}

/* ------------------------------------------------------------ the wraps */
bool __wrap_IsKeyPressed(int key) { return key > 0 && key < NK && sKey[key] && !sKeyPrev[key]; }
bool __wrap_IsKeyPressedRepeat(int key) { (void)key; return false; }
bool __wrap_IsKeyDown(int key) { return key > 0 && key < NK && sKey[key]; }
bool __wrap_IsKeyReleased(int key) { return key > 0 && key < NK && !sKey[key] && sKeyPrev[key]; }
bool __wrap_IsKeyUp(int key) { return !__wrap_IsKeyDown(key); }
int __wrap_GetKeyPressed(void) { return sKeyQI < sKeyQN ? sKeyQueue[sKeyQI++] : 0; }
int __wrap_GetCharPressed(void) { return 0; }

bool __wrap_IsGamepadAvailable(int gamepad) { return sPadOn && gamepad == 0; }
const char *__wrap_GetGamepadName(int gamepad) { return (sPadOn && gamepad == 0) ? "Xbox Wireless Controller" : ""; }
static bool PadOk(int gamepad, int button) { return sPadOn && gamepad == 0 && button >= 0 && button < NPAD; }
bool __wrap_IsGamepadButtonDown(int gamepad, int button) { return PadOk(gamepad, button) && sPad[button]; }
bool __wrap_IsGamepadButtonPressed(int gamepad, int button) { return PadOk(gamepad, button) && sPad[button] && !sPadPrev[button]; }
float __wrap_GetGamepadAxisMovement(int gamepad, int axis) { return (sPadOn && gamepad == 0 && axis >= 0 && axis < 8) ? sAxis[axis] : 0.0f; }

/* The pointer, already in logical coordinates (winscale's mapping is the
 * identity in a hidden window of the logical size). */
Vector2 __wrap_GetMousePosition(void) { return sMouse; }
int __wrap_GetMouseX(void) { return (int)sMouse.x; }
int __wrap_GetMouseY(void) { return (int)sMouse.y; }
Vector2 __wrap_GetMouseDelta(void) { return (Vector2){sMouse.x - sMousePrev.x, sMouse.y - sMousePrev.y}; }
static bool MouseOk(int button) { return button >= 0 && button < NMOUSE; }
bool __wrap_IsMouseButtonDown(int button) { return MouseOk(button) && sMouseBtn[button]; }
bool __wrap_IsMouseButtonUp(int button) { return !__wrap_IsMouseButtonDown(button); }
bool __wrap_IsMouseButtonPressed(int button) { return MouseOk(button) && sMouseBtn[button] && !sMouseBtnPrev[button]; }
bool __wrap_IsMouseButtonReleased(int button) { return MouseOk(button) && !sMouseBtn[button] && sMouseBtnPrev[button]; }
float __wrap_GetMouseWheelMove(void) { return sWheel; }
Vector2 __wrap_GetMouseWheelMoveV(void) { return (Vector2){0.0f, sWheel}; }
void __wrap_SetMousePosition(int x, int y) { sMouse = (Vector2){(float)x, (float)y}; }
void __wrap_SetMouseCursor(int cursor) {
    sCursor = cursor;
    if (sVerbose) Log("cursor %d", cursor);
}

bool __wrap_WindowShouldClose(void) { return sCloseFlag; }
bool __wrap_IsWindowFocused(void) { return sFocused; }

void __wrap_InitWindow(int width, int height, const char *title) {
    static char unique[96];
    (void)title;
    snprintf(unique, sizeof(unique), "blockfall-shots-%d", (int)getpid());
    __real_InitWindow(width, height, unique);
}

void __wrap_SetConfigFlags(unsigned int flags) {
    flags &= ~(unsigned)FLAG_VSYNC_HINT;
    __real_SetConfigFlags(flags | FLAG_WINDOW_HIDDEN);
}
void __wrap_SetTargetFPS(int fps) { (void)fps; __real_SetTargetFPS(0); }

void __wrap_ToggleBorderlessWindowed(void) { sBorderless = !sBorderless; Log("fullscreen %s", sBorderless ? "on" : "off"); }
void __wrap_ToggleFullscreen(void) { __wrap_ToggleBorderlessWindowed(); }
bool __wrap_IsWindowState(unsigned int flag) {
    if (flag == FLAG_BORDERLESS_WINDOWED_MODE || flag == FLAG_FULLSCREEN_MODE) return sBorderless;
    return __real_IsWindowState(flag);
}

double __wrap_GetTime(void) { return (double)sTick / 60.0; }
float __wrap_GetFrameTime(void) { return 1.0f / 60.0f; }

#define RAND_SEED 0x243F6A8885A308D3ull
static unsigned long long sRandState = RAND_SEED;
int __wrap_GetRandomValue(int min, int max) {
    if (min > max) { int t = min; min = max; max = t; }
    sRandState ^= sRandState << 13; sRandState ^= sRandState >> 7; sRandState ^= sRandState << 17;
    unsigned long long span = (unsigned long long)((long long)max - (long long)min + 1);
    return (int)((long long)min + (long long)((sRandState >> 11) % span));
}

static int sLogicalW = 0, sLogicalH = 0;
static unsigned int sLogicalFbo = 0;
RenderTexture2D __wrap_LoadRenderTexture(int width, int height) {
    RenderTexture2D rt = __real_LoadRenderTexture(width, height);
    if (!sLogicalW) { sLogicalW = width; sLogicalH = height; sLogicalFbo = rt.id; }
    return rt;
}

void __wrap_EndTextureMode(void) {
    if (sShotName[0] && sLogicalW > 0 && rlGetActiveFramebuffer() == sLogicalFbo) {
        rlDrawRenderBatchActive();
        unsigned char *px = rlReadScreenPixels(sLogicalW, sLogicalH);
        Image img = {px, sLogicalW, sLogicalH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        char path[800];
        snprintf(path, sizeof(path), "%s/%s.png", sShotDir, sShotName);
        bool ok = px && ExportImage(img, path);
        if (ok) sShotsWritten++;
        if (px) { /* kept for `expect pixels` */
            size_t bytes = (size_t)sLogicalW * (size_t)sLogicalH * 4;
            unsigned char *copy = realloc(sLastShot, bytes);
            if (copy) { memcpy(copy, px, bytes); sLastShot = copy; sLastW = sLogicalW; sLastH = sLogicalH; }
        }
        char why[900];
        snprintf(why, sizeof(why), "%s %s", ok ? "shot" : "SHOT FAILED", path);
        Where(why);
        MemFree(px);
        sShotName[0] = '\0';
    }
    __real_EndTextureMode();
}

static bool sInRender = false; /* the play screen is drawing this frame */
void __wrap_EndDrawing(void) {
    /* What the play screen drew, read before the clock moves on, so an
     * `expect hud`/`expect feel` after a shot checks that very frame. */
    if (sInRender) Render_Inspect(&sInspect);
    __real_EndDrawing();
    Advance();
}
void __wrap_PollInputEvents(void) {
    __real_PollInputEvents();
    Advance();
}

/* No audio device, ever. */
void __wrap_InitAudioDevice(void) { }
void __wrap_CloseAudioDevice(void) { }
bool __wrap_IsAudioDeviceReady(void) { return false; }
Sound __wrap_LoadSound(const char *f) { (void)f; return (Sound){0}; }
Sound __wrap_LoadSoundFromWave(Wave w) { (void)w; return (Sound){0}; }
Sound __wrap_LoadSoundAlias(Sound s) { (void)s; return (Sound){0}; }
void __wrap_UnloadSound(Sound s) { (void)s; }
void __wrap_UnloadSoundAlias(Sound s) { (void)s; }
void __wrap_PlaySound(Sound s) { (void)s; }
void __wrap_SetSoundVolume(Sound s, float v) { (void)s; (void)v; }
void __wrap_SetSoundPitch(Sound s, float v) { (void)s; (void)v; }
AudioStream __wrap_LoadAudioStream(unsigned int r, unsigned int s, unsigned int c) { (void)r; (void)s; (void)c; return (AudioStream){0}; }
bool __wrap_IsAudioStreamValid(AudioStream s) { (void)s; return false; }
void __wrap_UnloadAudioStream(AudioStream s) { (void)s; }
void __wrap_PlayAudioStream(AudioStream s) { (void)s; }
void __wrap_StopAudioStream(AudioStream s) { (void)s; }
void __wrap_SetAudioStreamBufferSizeDefault(int n) { (void)n; }
void __wrap_SetAudioStreamCallback(AudioStream s, AudioCallback cb) { (void)s; (void)cb; }

/* The background online sync is never started. */
void __wrap_GhostLink_TriggerSync(void) { if (sVerbose) Log("ghost-sync not started (harness)"); }

/* The game and which screen is drawn, seen through the calls main.c makes. */
static uint64_t NextSeed(void) { return sSeed + (uint64_t)(sInits++) * 7919u; }
void __wrap_Game_Init(Game *g, uint64_t seed, long hs, int hl, int hln) {
    (void)seed;
    sGame = g;
    __real_Game_Init(g, NextSeed(), hs, hl, hln);
}
void __wrap_Game_InitConfig(Game *g, GameConfig cfg, uint64_t seed, long hs, int hl, int hln) {
    /* How to play's demos run games of their own: left as they are, so the
     * run's game, its seeds and its effects stay the script's. */
    if (drv_is_demo(g)) { __real_Game_InitConfig(g, cfg, seed, hs, hl, hln); return; }
    (void)seed;
    sGame = g;
    __real_Game_InitConfig(g, cfg, NextSeed(), hs, hl, hln);
    sRandState = RAND_SEED; /* a run's effects do not depend on the menus before it */
    Where("Game_InitConfig");
}
/* A Practice room run (3.5.0): seeded like any other. */
void __wrap_Game_InitPractice(Game *g, GameConfig cfg, PracticeRules rules, uint64_t seed) {
    (void)seed;
    sGame = g;
    __real_Game_InitPractice(g, cfg, rules, NextSeed());
    sRandState = RAND_SEED;
    Where("Game_InitPractice");
}
void __wrap_Game_Restart(Game *g, uint64_t seed) {
    sGame = g;
    /* A daily (3.6.0) restarts on the day's seed: the pieces are the point. */
    __real_Game_Restart(g, g->daily.on ? seed : NextSeed());
    sRandState = RAND_SEED;
    Where("Game_Restart");
}
/* A Daily challenge run (3.6.0): the day's seed as it is (the wrapped
 * Daily_Seed below fixes the day), the effects' sequence starting over. */
void __wrap_Game_InitDaily(Game *g, GameConfig cfg, DailyRules rules, uint64_t seed) {
    sGame = g;
    __real_Game_InitDaily(g, cfg, rules, seed);
    sRandState = RAND_SEED;
    Where("Game_InitDaily");
}
/* The shared daily code answers for SHOTS_DAILY (YYYYMMDD), 20261020 by
 * default, whatever the clock says. */
static void ShotsDay(int *y, int *m, int *d) {
    const char *s = getenv("SHOTS_DAILY");
    int date = s && s[0] ? atoi(s) : 20261020;
    if (date < 19700101) date = 20261020;
    *y = date / 10000;
    *m = (date / 100) % 100;
    *d = date % 100;
}
uint64_t __wrap_Daily_Seed(void) {
    int y, m, d;
    ShotsDay(&y, &m, &d);
    return Daily_SeedForDate(y, m, d);
}
const char *__wrap_Daily_Mode(void) {
    static char buf[24];
    int y, m, d;
    ShotsDay(&y, &m, &d);
    Daily_ModeForDate(y, m, d, buf, sizeof(buf));
    return buf;
}
void __wrap_Render_Frame(const Game *g, const RenderView *v) {
    sView = *v;
    sHaveView = true;
    sInRender = true;
    __real_Render_Frame(g, v);
    sInRender = false;
}
void __wrap_ScoresView_Draw(ScoresViewInfo *v) {
    snprintf(sScoresMode, sizeof(sScoresMode), "%s", v->modeLabel);
    snprintf(sScoresBoard, sizeof(sScoresBoard), "%s", v->boardLabel);
    sScoresCount = v->count;
    sScoresWorld = v->world;
    sScoresPeriod = (int)v->period;
    sScoresOldBest = v->oldBest;
    __real_ScoresView_Draw(v);
    sScoresBlink = v->blinkRow;
}
/* The wipe is the script's choice (SHOTS_WIPE=1); off, every screen change
 * is drawn as it was before the wipe existed. */
void __wrap_Wipe_Start(Wipe *w, bool motion) {
    __real_Wipe_Start(w, motion && getenv("SHOTS_WIPE") != NULL);
}
void __wrap_Sfx_Play(int id) {
    char what[24];
    snprintf(what, sizeof(what), "sfx:%d", id);
    NoteSound(what);
    __real_Sfx_Play(id);
}
void __wrap_Sfx_PlayVaried(int id, float variance) {
    char what[24];
    snprintf(what, sizeof(what), "tick:%d", id);
    NoteSound(what);
    __real_Sfx_PlayVaried(id, variance);
}
void __wrap_Sfx_PlayPitched(int id, float pitch) {
    char what[24];
    snprintf(what, sizeof(what), "pitched:%d", id);
    NoteSound(what);
    __real_Sfx_PlayPitched(id, pitch);
}
void __wrap_Sfx_Jingle(SfxJingle j) {
    char what[32];
    snprintf(what, sizeof(what), "jingle:%s", JingleName(j));
    NoteSound(what);
    __real_Sfx_Jingle(j);
}

/* --------------------------------------------------------------- main */
int main(void) {
    sVerbose = getenv("SHOTS_VERBOSE") != NULL;
    if (getenv("SHOTS_OUT")) sShotDir = getenv("SHOTS_OUT");
    if (getenv("SHOTS_HOLD_MS")) sHoldMs = atoi(getenv("SHOTS_HOLD_MS"));
    const char *script = getenv("SHOTS_SCRIPT");
    if (!script) { fprintf(stderr, "SHOTS_SCRIPT is not set (see run.sh)\n"); return 2; }
    const char *home = getenv("HOME"), *real = getenv("SHOTS_REAL_HOME");
    const char *xd = getenv("XDG_DATA_HOME"), *xc = getenv("XDG_CONFIG_HOME");
    if (!home || !xd || !xc || (real && strcmp(home, real) == 0) || !strstr(home, "bf-shots.") || !strstr(xd, "bf-shots.") ||
        !strstr(xc, "bf-shots.")) {
        fprintf(stderr, "refusing to run: HOME and XDG_*_HOME must point at a run.sh temp folder\n");
        return 2;
    }
    LoadScript(script);
    SetTraceLogLevel(LOG_WARNING);
    sStarted = true;
    /* As Ghost Launcher starts it: plain, or with --daily (SHOTS_DAILYLAUNCH=1). */
    char *gameArgv[3] = {"blockfall", NULL, NULL};
    int gameArgc = 1;
    if (getenv("SHOTS_DAILYLAUNCH")) gameArgv[gameArgc++] = "--daily";
    int rc = blockfall_main(gameArgc, gameArgv);
    sStarted = false; /* its App is still there, but the game is gone */
    bool quitEarly = !sClosing;
    sGame = NULL; /* it lived on blockfall_main's stack, which is gone now */
    Where("blockfall exited");
    Log("summary: rc=%d frames=%ld shots=%d/%d expects=%d/%d expect_failures=%d game_failures=%d quit=%s", rc, sTick, sShotsWritten,
        sShotsAsked, sExpectsRun, sExpectsAsked, sExpectFails, sGameFails, quitEarly ? "early" : "at the end");
    if (rc != 0) return rc;
    if (sExpectFails || sGameFails || sExpectsRun != sExpectsAsked) return 3;
    if (sShotsWritten != sShotsAsked) return 5;
    if (quitEarly != sMustQuit) {
        Log("QUIT MISMATCH: blockfall %s, the script %s", quitEarly ? "quit early" : "stayed open to the end",
            sMustQuit ? "says it quits" : "does not say `quits`");
        return 6;
    }
    return 0;
}
