/* The run: the rules (game.c) on the play screen (render.c), and what
 * happens around it (REWORK_PLAN.md, phase 5).
 *  - In play, as before: auto-repeat as Settings > Feel sets it (today's
 *    by default) and READY-GO (playstep.c), your keys and buttons for every
 *    action including Rotate left (actions.h), Left and Right held together
 *    cancel, M, - and = show the volume chip,
 *    the controls card shows the pad's buttons after pad use, NEW BEST and
 *    a soft coin the first time a run passes its best, pitched clear sounds
 *    and a dull tick for a turn that cannot be made.
 *  - Esc (or your Back key), P (or your Pause key), pad Start or pad B open
 *    the pause menu (pausemenu.h), and so does the window losing focus
 *    (unless Settings > Game > Auto pause is off). R
 *    (your Restart key) and the pad's Restart button (View as it comes)
 *    restart, asking "are you sure?" first when the run has points; Settings
 *    > Game > R restarts: At once lets the key alone restart at once, the
 *    pad's Restart button always asks. With
 *    the countdown on, play goes on after 3, 2, 1. Enter or pad A on Resume
 *    act when let go, so they never reach play as a hard drop.
 *  - The end: one sound (game over, or the cleared sting), the run recorded,
 *    then the end show and the results card (results.h). NEW BEST stamps in
 *    with the new-best jingle and confetti as the count-up ends. Play again
 *    (or your Restart key or button), Change setup, Scores (that board, the run's row
 *    blinking) and Menu (or Esc, pad B).
 *  - Every run starts with the block wipe (wipe.h), and leaving one wipes
 *    into the screen it goes to. */
#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "screens.h"
#include "blockdraw.h"
#include "hudfx.h"
#include "feel.h"
#include "comfort.h"
#include "menumodel.h"
#include "achievements.h"
#include "ghostlink.h"
#include "music.h"
#include "prefs.h"
#include "rlgl.h"
#include "runlog.h"
#include "sfx.h"
#include "tuning.h"
#include "ui.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define GHOST_GAME_SLUG RECORD_SLUG
#define CHIP_SECONDS 1.5f /* how long the volume chip shows after M, - or = */
#define HINTS_CY (WINDOW_HEIGHT - 44)

_Static_assert(ACT_KEY_SPACE == KEY_SPACE && ACT_KEY_MINUS == KEY_MINUS && ACT_KEY_EQUAL == KEY_EQUAL && ACT_KEY_M == KEY_M &&
               ACT_KEY_ESCAPE == KEY_ESCAPE && ACT_KEY_F3 == KEY_F3 && ACT_KEY_KP_SUBTRACT == KEY_KP_SUBTRACT &&
               ACT_KEY_KP_ADD == KEY_KP_ADD, "actions.h's key codes are raylib's");

static uint64_t MakeSeed(int nonce) {
    uint64_t t = (uint64_t)time(NULL);
    uint64_t addrEntropy = (uint64_t)(uintptr_t)&t;
    return (t * 2654435761u) ^ addrEntropy ^ (uint64_t)(nonce * 0x9E3779B1u);
}

/* The keyboard as raylib sees it (gamepad.h's macros are not here, so a
 * pad never answers for a key). */
static bool KeyDownRaw(int code) { return IsKeyDown(code); }
static bool KeyPressedRaw(int code) { return IsKeyPressed(code); }
static const KeyReader kKeyboard = {KeyDownRaw, KeyPressedRaw};

static void Today(char *out, size_t size) {
    time_t now = time(NULL);
    struct tm tmNow;
    if (localtime_r(&now, &tmNow) == NULL || strftime(out, size, "%Y-%m-%d", &tmNow) == 0) snprintf(out, size, "unknown");
}

/* --------------------------------------------------------- the record */

typedef struct {
    bool trophy;     /* the run earned a trophy */
    char title[64];  /* the first one it earned */
} Submitted;

#define MAX_TROPHIES 32
static int TrophiesHeld(bool had[MAX_TROPHIES]) {
    int n = Ach_CountForGame(GHOST_GAME_SLUG);
    if (n > MAX_TROPHIES) n = MAX_TROPHIES;
    char date[16];
    for (int i = 0; i < n; i++) {
        const AchDef *d = Ach_ForGame(GHOST_GAME_SLUG, i);
        had[i] = d && Ach_IsUnlocked(GHOST_GAME_SLUG, d->id, date);
    }
    return n;
}

/* Records a finished (or abandoned) run, exactly once however it ends:
 * runs.log and achievements when it counts, save.dat's classic best for the
 * classic Marathon only, and its board (runrecord.h decides all of it).
 * What it put on the board is kept for the results card and Scores. */
static Submitted SubmitRun(App *a) {
    PlayState *p = &a->play;
    const Game *g = &p->game;
    Submitted out;
    memset(&out, 0, sizeof(out));
    if (p->runSubmitted) return out;
    p->runSubmitted = true;
    p->endRecorded = false;
    RunRecord r = Record_Classify(g, Tune_Modified());
    if (!r.counts) return out; /* a playtest, or a restart before anything happened */

    char logKey[GHOSTLINK_MODE_LEN];
    Record_LogKey(g->cfg.mode, g->cfg.width, g->cfg.height, logKey, sizeof(logKey));
    RunLog_Append(GHOST_GAME_SLUG, logKey, g->score, g->level);
    if (g->cfg.mode != MODE_ZEN) {
        bool had[MAX_TROPHIES];
        int n = TrophiesHeld(had);
        if (Ach_CheckRun(GHOST_GAME_SLUG, g->score, g->level) > 0) {
            out.trophy = true;
            char date[16];
            for (int i = 0; i < n && !out.title[0]; i++) {
                const AchDef *d = Ach_ForGame(GHOST_GAME_SLUG, i);
                if (d && !had[i] && Ach_IsUnlocked(GHOST_GAME_SLUG, d->id, date)) snprintf(out.title, sizeof(out.title), "%s", d->title);
            }
        }
    }

    if (r.toSave) {
        SaveData *save = &a->save;
        if (g->score > save->highScore) save->highScore = g->score;
        if (g->level > save->highLevel) save->highLevel = g->level;
        if (g->linesCleared > save->highLines) save->highLines = g->linesCleared;
        SaveData_Save(save);
    }
    if (!r.toBoard) return out;

    char date[GHOSTLINK_DATE_LEN];
    Today(date, sizeof(date));
    bool shared = false;
    Record_Submit(&r, a->username, date, &shared);
    if (shared) GhostLink_TriggerSync();
    p->endRecorded = true;
    p->endValue = r.value;
    snprintf(p->endDate, sizeof(p->endDate), "%s", date);
    return out;
}

/* ------------------------------------------------------ starting a run */

void ScrPlay_Init(App *a) {
    PlayState *p = &a->play;
    Game_Init(&p->game, MakeSeed(0), a->save.highScore, a->save.highLevel, a->save.highLines);
    p->runSubmitted = true; /* nothing to record until a run actually starts */
    p->restartNonce = 1;
    Controls_Words(&a->settings, &p->view.keys);
}

void ScrPlay_Enter(App *a, bool back) {
    PlayState *p = &a->play;
    if (back) {
        /* Back from How to play (the pause menu is still up) or Scores (the
         * results card). The run reads the keyboard itself, so the quiet
         * input main.c hands this pass does not hide the press that came
         * back here (Esc would resume the run, or leave the card): this
         * frame takes no press, as a run's first frame does. */
        p->fresh = true;
        return;
    }
    GameConfig cfg = Setup_Config(&a->settings);
    p->restartNonce++;
    p->view.unit = Record_UnitOf(cfg.mode);
    p->view.best = Record_LoadBest(cfg.mode, cfg.width, cfg.height, a->save.highScore);
    p->view.newBest = p->view.focusLost = false;
    p->view.passedBest = p->view.justPassedBest = false;
    p->view.run = (unsigned)p->restartNonce;
    p->view.showControls = !a->settings.hideControls;
    Game_InitConfig(&p->game, cfg, MakeSeed(p->restartNonce), p->view.unit == UNIT_POINTS ? p->view.best : 0, a->save.highLevel,
                    a->save.highLines);
    Menu_BoardName(p->game.cfg.width, p->game.cfg.height, p->board, sizeof(p->board));
    Ui_ClearParticles();
    p->runSubmitted = false;
    p->prevGameOver = false;
    p->fresh = true;
    p->endRecorded = false;
    PauseMenu_Close(&p->pause);
    Results_End(&p->results);
    PlayDriver_Start(&p->driver, a->settings.readyGo, false); /* READY, then GO */
    p->view.cue = p->driver.cue;
    p->view.moveRepeat = false;
    App_Wipe(a);
    Scr_Cursor(false);
}

int ScrPlay_MusicMode(const App *a) {
    GamePhase ph = a->play.game.phase;
    if (App_Top(a) != SCR_PLAY) return (Nav_Contains(&a->nav, SCR_PLAY) && ph == GS_PAUSED) ? MUSIC_PAUSED : MUSIC_MENU;
    return ph == GS_PAUSED ? MUSIC_PAUSED : (ph == GS_GAMEOVER ? MUSIC_SILENT : MUSIC_PLAY);
}

/* ------------------------------------------------------------- sound */

static void SoundKeys(App *a, const PlayInput *pi) {
    Settings *s = &a->settings;
    RenderView *v = &a->play.view;
    /* Sound keys show a chip and are saved at once. */
    if (pi->mute) {
        s->audioEnabled = !s->audioEnabled;
        Sfx_SetEnabled(s->audioEnabled);
        Music_SetEnabled(s->audioEnabled);
    }
    if (pi->volumeDown) {
        s->volumePercent -= 10;
        if (s->volumePercent < 0) s->volumePercent = 0;
        Sfx_SetVolume(s->volumePercent);
        Music_SetVolume(Prefs_Get()->musicVolume);
    }
    if (pi->volumeUp) {
        s->volumePercent += 10;
        if (s->volumePercent > 100) s->volumePercent = 100;
        Sfx_SetVolume(s->volumePercent);
        Music_SetVolume(Prefs_Get()->musicVolume);
    }
    if (pi->mute || pi->volumeDown || pi->volumeUp) {
        Settings_Save(s);
        v->chipTime = CHIP_SECONDS;
    }
    v->soundOn = s->audioEnabled;
    v->volume = s->volumePercent;
}

/* The pause menu's quick panel: what it shows, and a change made there,
 * saved at once (the effects volume plays a tick at its new level; the
 * music is heard at once, muffled as it is while paused). */
static QuickValues Quick(const App *a) {
    const ArcadePrefs *pr = Prefs_Get();
    QuickValues q = {(a->settings.volumePercent + 5) / 10, (pr->musicVolume + 5) / 10, a->settings.audioEnabled, a->settings.ghostPiece != 0,
                     pr->screenShake};
    return q;
}

static void ApplyQuick(App *a, int id, int value) {
    Settings *s = &a->settings;
    ArcadePrefs *pr = Prefs_Get();
    int percent = value < 0 ? 0 : (value > 10 ? 100 : value * 10);
    switch (id) {
        case PQ_EFFECTS:
            s->volumePercent = percent;
            Sfx_SetVolume(s->volumePercent);
            Music_SetVolume(pr->musicVolume);
            Settings_Save(s);
            break;
        case PQ_MUSIC:
            pr->musicVolume = percent;
            Prefs_Save();
            Music_SetVolume(pr->musicVolume);
            break;
        case PQ_SOUND:
            s->audioEnabled = value != 0;
            Sfx_SetEnabled(s->audioEnabled);
            Music_SetEnabled(s->audioEnabled);
            Settings_Save(s);
            break;
        case PQ_GHOST:
            /* Off, then on again: back to the look it had (Outline or Solid). */
            if (value) s->ghostPiece = a->ghostLook ? a->ghostLook : GHOST_OUTLINE;
            else {
                if (s->ghostPiece) a->ghostLook = s->ghostPiece;
                s->ghostPiece = GHOST_OFF;
            }
            Settings_Save(s);
            break;
        case PQ_SHAKE:
            pr->screenShake = value != 0;
            Prefs_Save();
            break;
        default: return;
    }
    Sfx_Play(SFX_MOVE);
}

/* ------------------------------------------------- pausing and leaving */

static void OpenPause(App *a, bool focusLost) {
    PlayState *p = &a->play;
    if (p->game.phase == GS_PLAYING) Game_TogglePause(&p->game);
    PauseMenu_Open(&p->pause, focusLost);
    p->view.focusLost = focusLost;
}

static void Resume(App *a) {
    PlayState *p = &a->play;
    PauseMenu_Close(&p->pause);
    if (p->game.phase == GS_PAUSED) Game_TogglePause(&p->game);
    p->view.focusLost = false;
    if (a->settings.resumeCountdown) PlayDriver_Countdown(&p->driver); /* 3, 2, 1 */
    p->view.cue = p->driver.cue;
}

/* "Restart this run?" or "Leave this run?", with what it counts for. From
 * play (your Restart key or button) the run pauses under the box. */
static void Ask(App *a, PauseAsk what, bool fromPlay) {
    PlayState *p = &a->play;
    if (p->game.phase == GS_PLAYING) Game_TogglePause(&p->game);
    char title[64], body[96], act[24];
    PauseMenu_AskWords(what, &p->game, Tune_Modified(), p->board, title, sizeof(title), body, sizeof(body), act, sizeof(act));
    PauseMenu_Ask(&p->pause, what, title, body, "", fromPlay);
    Sfx_Jingle(JINGLE_WARNING);
}

/* The same mode and board again: only GO (no READY), and the wipe. The run
 * left behind counts for what it scored. */
static void Restart(App *a) {
    PlayState *p = &a->play;
    Game *game = &p->game;
    RenderView *view = &p->view;
    Submitted s = SubmitRun(a);
    if (s.trophy) Sfx_Jingle(JINGLE_LEVEL_UP);
    p->restartNonce++;
    view->best = Record_LoadBest(game->cfg.mode, game->cfg.width, game->cfg.height, a->save.highScore);
    view->newBest = view->focusLost = false;
    view->passedBest = false;
    view->run = (unsigned)p->restartNonce;
    Game_Restart(game, MakeSeed(p->restartNonce));
    game->highScore = view->unit == UNIT_POINTS ? view->best : 0;
    p->runSubmitted = false;
    p->prevGameOver = false;
    p->endRecorded = false;
    PauseMenu_Close(&p->pause);
    Results_End(&p->results);
    PlayDriver_Start(&p->driver, a->settings.readyGo, true); /* only GO, and play at once */
    view->cue = p->driver.cue;
    App_Wipe(a);
}

/* Back to the main menu (the run counts for what it scored), wiping out. */
static void Leave(App *a) {
    PlayState *p = &a->play;
    Submitted s = SubmitRun(a);
    if (s.trophy) Sfx_Jingle(JINGLE_LEVEL_UP);
    PauseMenu_Close(&p->pause);
    Results_End(&p->results);
    App_Home(a);
    App_Wipe(a);
}

/* ------------------------------------------------------------ the end */

/* The results card's confetti: gold and the pieces' colours thrown from the
 * NEW BEST plate's edges (half as many with Reduced motion). Looks only. */
#define CONFETTI 48
typedef struct { float x, y, vx, vy, life; Color c; } Confetti;
static Confetti sConfetti[CONFETTI];

static KitRect StampRect(void) {
    KitRect c = Results_CardRect();
    float w = (float)(Kit_TextWidth("NEW BEST", 16) + 48);
    return (KitRect){c.x + (c.w - w) / 2.0f, c.y + 148.0f, w, 32.0f};
}

static void StartConfetti(void) {
    KitRect r = StampRect();
    int n = Comfort_Particles(CONFETTI);
    for (int i = 0; i < CONFETTI; i++) {
        Confetti *c = &sConfetti[i];
        c->life = 0.0f;
        if (i >= n) continue;
        bool side = i % 2 == 0;
        c->x = side ? (i % 4 == 0 ? r.x : r.x + r.w) : r.x + (float)GetRandomValue(0, (int)r.w);
        c->y = side ? r.y + (float)GetRandomValue(0, (int)r.h) : r.y;
        c->vx = (float)GetRandomValue(-170, 170);
        c->vy = -(float)GetRandomValue(60, 280);
        c->life = (float)GetRandomValue(70, 120) / 100.0f;
        c->c = i % 3 == 0 ? Render_PieceColor((PieceType)(i % PIECE_COUNT)) : Ui_Theme()->gold;
    }
}

static void DrawConfetti(float dt) {
    for (int i = 0; i < CONFETTI; i++) {
        Confetti *c = &sConfetti[i];
        if (c->life <= 0.0f) continue;
        c->life -= dt;
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        c->vy += 520.0f * dt;
        c->vx *= 1.0f - 1.5f * dt;
        if (c->life <= 0.0f) continue;
        DrawRectangle(((int)c->x / 2) * 2 - 2, ((int)c->y / 2) * 2 - 2, 4, 4, c->c); /* on the 2 px grid */
    }
}

/* The run just ended: one sound, the record, then the card's words. */
static void OnEnd(App *a) {
    PlayState *p = &a->play;
    Game *game = &p->game;
    RenderView *view = &p->view;
    bool tuned = Tune_Modified();
    RunRecord r = Record_Classify(game, tuned);
    view->newBest = r.toBoard && Record_Beats(r.value, view->best);
    long saveBest = r.toSave ? a->save.highScore : 0; /* the classic best before this run */
    Submitted s = SubmitRun(a);

    int rankHere = 0, worldRows = 0, worldRank = 0;
    if (p->endRecorded) {
        static GhostScore rows[GHOSTLINK_TOP_N];
        int n = Record_LoadBoard(r.key, false, rows, GHOSTLINK_TOP_N);
        rankHere = Results_RankHere(rows, n, a->username, r.value, p->endDate, saveBest);
        if (!r.local) {
            worldRows = Record_LoadBoard(r.key, true, rows, GHOSTLINK_TOP_N);
            worldRank = Results_WouldRank(rows, worldRows, r.value, a->username);
        }
    }
    RunFacts f = {tuned, view->best, rankHere, worldRows, worldRank, s.trophy && s.title[0] ? s.title : NULL, p->board};
    ResultsCard card;
    Results_Build(&card, game, &f);
    Results_Begin(&p->results, &card, Comfort_Motion());
    p->againChip[0] = '\0';
    for (int i = 0; i < CONFETTI; i++) sConfetti[i].life = 0.0f;
    /* One sound now: the cleared sting or the game over. A new best's
     * jingle waits for its stamp, as the count-up ends. */
    if (Record_EndSound(game->won, false) == END_SOUND_CLEARED) Sfx_Jingle(JINGLE_LEVEL_UP);
    else Sfx_Play(SFX_GAME_OVER);
}

static void UpdateResults(App *a, const RunKeys *keys, const Input *in, float dt) {
    PlayState *p = &a->play;
    ResultsStep s = Results_Update(&p->results, keys, in, dt);
    if (s.moved) Scr_SoundMove();
    if (s.tick) Sfx_PlayPitched(SFX_MOVE, s.tickPitch);
    if (s.stamp && p->results.card.newBest) {
        Sfx_Jingle(JINGLE_NEW_BEST);
        StartConfetti();
    }
    switch (s.action) {
        case RA_AGAIN: Scr_SoundConfirm(); Restart(a); return;
        case RA_SETUP:
            Scr_SoundConfirm();
            Results_End(&p->results);
            App_Home(a);
            App_Open(a, SCR_SETUP);
            App_Wipe(a);
            return;
        case RA_SCORES:
            Scr_SoundConfirm();
            ScrScores_OpenForRun(a, p->game.cfg.mode, p->game.cfg.width, p->game.cfg.height, p->endRecorded ? p->endValue : 0, p->endDate);
            return;
        case RA_MENU: Scr_SoundBack(); Leave(a); return;
        default: break;
    }
    if (Results_Done(&p->results)) {
        KitList l;
        Results_Layout(&p->results, &l);
        Scr_Cursor(Kit_WantsHand(&p->results.kit, &l));
    }
}

static void UpdatePause(App *a, const RunKeys *keys, const PlayInput *pi, const Input *in, float dt) {
    PlayState *p = &a->play;
    QuickValues q = Quick(a);
    bool viaBack = Keymap_Pressed(KA_BACK, in) || keys->back;
    PauseAction act = PauseMenu_Update(&p->pause, &q, keys, in);
    PauseMenu_Tick(&p->pause, dt);
    if (act.moved) Scr_SoundMove();
    if (act.opened) Scr_SoundConfirm();
    if (act.closed) Scr_SoundBack();
    switch (act.kind) {
        case PA_RESUME:
            if (!act.closed && !keys->pause) {
                if (viaBack) Scr_SoundBack();
                else Scr_SoundConfirm();
            }
            Resume(a);
            return;
        case PA_RESTART: {
            /* Your Restart key restarts at once when Settings says so; the
             * pad's Restart button and the menu's Restart always ask when the
             * run has points (pi keeps the key apart from the button). */
            bool atOnce = pi->restartKey && a->settings.quickRestart;
            if (PauseMenu_ShouldAsk(&p->game) && !atOnce) Ask(a, PAUSE_ASK_RESTART, false);
            else { Scr_SoundConfirm(); Restart(a); }
            return;
        }
        case PA_QUIT:
            if (PauseMenu_ShouldAsk(&p->game)) Ask(a, PAUSE_ASK_QUIT, false);
            else { Scr_SoundBack(); Leave(a); }
            return;
        case PA_DO_RESTART: Scr_SoundConfirm(); Restart(a); return;
        case PA_DO_QUIT: Scr_SoundBack(); Leave(a); return;
        case PA_HOWTO: Scr_SoundConfirm(); ScrHowTo_Open(a); return;
        case PA_SET: ApplyQuick(a, act.id, act.value); break;
        default: break;
    }
    /* Auto-repeat keeps time while paused, as it always has (the game does
     * not move: it is paused). */
    PlayIntent held = {pi->left, pi->right, pi->softDrop, false, false, false, false};
    PlayStepResult step;
    Play_Step(&p->driver, &p->game, &held, dt, Tune_Speed(), &step);
    KitList l;
    const KitState *k;
    if (ScrPlay_Kit(a, &l, &k)) Scr_Cursor(Kit_WantsHand(k, &l));
}

/* ------------------------------------------------------------- a frame */

void ScrPlay_Update(App *a, const Input *in, float dt) {
    PlayState *p = &a->play;
    Game *game = &p->game;
    RenderView *view = &p->view;
    bool quiet = p->fresh;
    p->fresh = false;

    Music_SetIntensity(fminf(1.0f, 0.40f + 0.06f * (float)game->level));
    if (view->chipTime > 0.0f) view->chipTime -= dt;
    view->justPassedBest = false;
    view->padHints = in->hintDevice == DEV_PAD;

    PlayDriver_SetHandling(&p->driver, PlayHandling_From(a->settings.repeatDelayMs, a->settings.repeatSpeedMs, a->settings.softDropSpeed));
    PlayInput pi;
    Actions_Read(&a->keys, &kKeyboard, in, quiet, &pi);
    if (pi.fps) view->showFps = !view->showFps;
    SoundKeys(a, &pi);
    RunKeys keys = {pi.pause, pi.restartKey || pi.restartPad, pi.back};

    if (p->results.on) { UpdateResults(a, &keys, in, dt); return; }
    if (p->pause.open) { UpdatePause(a, &keys, &pi, in, dt); return; }
    /* In play nothing is clicked: the arrow, even when a click on Resume,
     * Keep playing or Play again left the pointing hand up. */
    Scr_Cursor(false);

    /* The run pauses when the window loses focus (the pad stops answering
     * then, and the pieces kept falling with nobody playing), unless
     * Settings > Game > Auto pause is off. */
    if (game->phase == GS_PLAYING && !in->focused && a->settings.autoPause) {
        OpenPause(a, true);
        return;
    }
    if (game->phase == GS_PLAYING && (pi.back || pi.pause)) {
        OpenPause(a, false);
        return;
    }
    /* Your Restart key or button: "are you sure?" first when the run has
     * points. Settings > Game > R restarts: At once skips the question for
     * the key only; the pad's Restart button always asks. */
    if (keys.restart && game->phase == GS_PLAYING) {
        if (!(a->settings.quickRestart && pi.restartKey) && PauseMenu_ShouldAsk(game)) {
            Ask(a, PAUSE_ASK_RESTART, true);
            return;
        }
        Restart(a);
    }

    /* The moves and gravity, as always (or READY and the countdown holding
     * them). */
    PlayIntent intent = {pi.left, pi.right, pi.softDrop, pi.rotate, pi.hardDrop, pi.hold, pi.rotateLeft};
    PlayStepResult step;
    Play_Step(&p->driver, game, &intent, dt, Tune_Speed(), &step);
    view->moveRepeat = step.moveRepeat;
    view->cue = p->driver.cue;
    if (step.held) Sfx_Play(SFX_ROTATE); /* Hold still borrows the turn's sound (the sound pass gives it its own) */
    if (p->driver.cue.goSound) {
        p->driver.cue.goSound = false;
        Sfx_Jingle(JINGLE_GO);
    }

    bool nowGameOver = game->phase == GS_GAMEOVER;
    bool ending = nowGameOver && !p->prevGameOver;
    p->prevGameOver = nowGameOver;
    /* The first time the run passes the best it started with: a NEW BEST
     * callout and a soft coin (not on the frame the run ends: that has its
     * own one sound). */
    if (game->phase == GS_PLAYING &&
        Hud_PassesBest(game->score, view->best, view->unit, game->cfg.mode, !Tune_Modified(), view->passedBest)) {
        view->passedBest = view->justPassedBest = true;
        Sfx_Jingle(JINGLE_COIN);
    }
    if (!ending) {
        if (game->justMoved) Sfx_Play(SFX_MOVE);
        if (game->justRotated) Sfx_Play(SFX_ROTATE);
        else if (step.rotateBlocked) Sfx_PlayPitched(SFX_ROTATE, ROTATE_BLOCKED_PITCH); /* the press registered */
        if (game->justHardDropped) Sfx_Play(SFX_HARD_DROP);
        if (game->justLocked && game->lastClearCount > 0) {
            int id = game->lastClearCount >= 4 ? SFX_QUAD : SFX_CLEAR;
            float pitch = Feel_ClearPitch(game->lastClearCount, game->lastWasB2B);
            if (pitch == 1.0f) Sfx_Play(id); /* a single and a quad sound as they always have */
            else Sfx_PlayPitched(id, pitch);
        }
        if (game->lastWasLevelUp) Sfx_Play(SFX_LEVEL_UP);
    } else {
        OnEnd(a);
    }
}

/* ------------------------------------------------------------ drawing */

static void Card(KitRect c, Color bar) {
    const UiTheme *t = Ui_Theme();
    DrawRectangle((int)c.x, (int)c.y, (int)c.w, (int)c.h, t->board);
    DrawRectangleLinesEx((Rectangle){c.x, c.y, c.w, c.h}, 2.0f, t->rule);
    DrawRectangle((int)c.x, (int)c.y, (int)c.w, 4, bar);
}

/* The quick panel's rows: the label at the left (lit when the row has the
 * focus), the control at the right, the focus plate under the whole row. */
static void DrawQuick(App *a, const KitList *l, float dt, bool motion, const KitState *state) {
    PauseMenu *m = &a->play.pause;
    const UiTheme *t = Ui_Theme();
    InputDevice dev = a->input.lastDevice;
    const KitItem *f = Kit_Get(l, m->kit.focus);
    if (state && f && f->id != PQ_DONE && Kit_Look(state, f, dev) == KIT_LOOK_FOCUSED)
        KitDraw_Plate(Kit_GlideTo(&m->glide, PauseMenu_RowRect(m, f->id), dt, motion));
    else
        Kit_GlideReset(&m->glide);
    for (int i = 0; i < l->count; i++) {
        const KitItem *it = &l->items[i];
        KitLook look = Kit_Look(state, it, dev);
        int hover = Kit_HoverCell(state, it, dev), pressed = Kit_PressedCell(state, it);
        if (it->id == PQ_DONE) {
            KitDraw_Item(it, look, hover, pressed);
            continue;
        }
        KitRect row = PauseMenu_RowRect(m, it->id);
        bool focused = look == KIT_LOOK_FOCUSED;
        Ui_Text(PauseMenu_RowLabel(it->id), (int)row.x + 12, (int)(row.y + (row.h - 8) / 2), UI_T8, focused ? t->light : t->text);
        KitDraw_Item(it, focused ? KIT_LOOK_NORMAL : look, hover, pressed);
    }
}

static void DrawPause(App *a, float dt) {
    PauseMenu *m = &a->play.pause;
    const UiTheme *t = Ui_Theme();
    bool motion = Comfort_Motion();
    InputDevice dev = a->input.lastDevice, hints = a->input.hintDevice;
    bool asking = m->dialog.open;
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(t->bg, 0.82f * PauseMenu_Dim(m, motion)));

    KitRect c = PauseMenu_Card(m);
    int cx = (int)(c.x + c.w / 2);
    QuickValues q = Quick(a);
    KitList l;
    PauseMenu_Layout(m, &q, &l);
    const KitState *state = asking ? NULL : &m->kit; /* under the box everything is at rest */
    rlPushMatrix();
    rlTranslatef(0.0f, (float)PauseMenu_SlideOffset(m, motion), 0.0f);
    Card(c, t->light);
    if (m->page == PAUSE_MAIN) {
        Ui_TextCentered("PAUSED", UI_T32, cx, (int)c.y + 24, t->text);
        if (m->focusLost) Ui_TextCentered("The window lost focus", UI_T8, cx, (int)c.y + 68, t->gold);
        const KitItem *f = Kit_Get(&l, m->kit.focus);
        KitRect plate = Kit_GlideTo(&m->glide, f ? f->r : l.items[0].r, dt, motion);
        KitView kv = {&l, state, dev, hints, &plate};
        KitDraw_List(&kv);
    } else {
        Ui_TextCentered("Settings", UI_T16, cx, (int)c.y + 24, t->text);
        DrawQuick(a, &l, dt, motion, state);
    }
    rlPopMatrix();

    if (asking) {
        KitList dl;
        Kit_DialogLayout(&m->dialog, &dl);
        KitView dv = {&dl, &m->dlgKit, Kit_DialogDevice(&m->dlgKit, &dl, dev), hints, NULL};
        KitDraw_Dialog(&m->dialog, &dv);
        KitHint h[3] = {{2, {KA_UI_LEFT, KA_UI_RIGHT}, "choose"}, {1, {KA_UI_PRESS}, "select"}, {1, {KA_BACK}, "keep playing"}};
        KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, hints);
    } else if (m->page == PAUSE_MAIN) {
        KitHint h[3] = {{2, {KA_UI_UP, KA_UI_DOWN}, "choose"}, {1, {KA_UI_PRESS}, "select"}, {1, {KA_BACK}, "resume"}};
        KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, hints);
    } else {
        KitHint h[3] = {{2, {KA_UI_UP, KA_UI_DOWN}, "choose"}, {2, {KA_UI_LEFT, KA_UI_RIGHT}, "change"}, {1, {KA_BACK}, "back"}};
        KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, hints);
    }
}

/* The "again" chip under the results card: your Restart key, or the pad's
 * Restart button (the first of its two, as the controls card names them)
 * once the pad is in use. False, and out empty, when there is no chip: the
 * pad has no Restart button, or a swap made Esc Restart (here Esc is Menu). */
bool ScrPlay_AgainCap(const App *a, char *out, size_t n) {
    bool pad = a->input.hintDevice == DEV_PAD;
    const char *src = pad ? a->play.view.keys.padRestart : a->play.view.keys.restart;
    snprintf(out, n, "%s", src);
    if (pad) { /* "View B": the first button only */
        char *sp = strchr(out, ' ');
        if (sp) *sp = '\0';
    }
    if (!out[0] || (!pad && strcmp(out, "Esc") == 0)) { out[0] = '\0'; return false; }
    return true;
}

/* The hint line under the results card: the kit's chips, then the "again"
 * chip (ScrPlay_AgainCap) when there is one. What it drew is kept in
 * play.againChip, so the shots harness checks the chip as drawn, not the
 * helper alone. */
static void DrawResultsHints(App *a, bool done) {
    InputDevice d = a->input.hintDevice;
    bool pad = d == DEV_PAD;
    char again[24];
    KitHint h[3] = {{2, {KA_UI_LEFT, KA_UI_RIGHT}, "choose"}, {1, {KA_UI_PRESS}, done ? "select" : "skip"}, {1, {KA_BACK}, "menu"}};
    if (!ScrPlay_AgainCap(a, again, sizeof(again))) {
        a->play.againChip[0] = '\0';
        KitDraw_Hints(h, 3, WINDOW_WIDTH / 2, HINTS_CY, d);
        return;
    }
    snprintf(a->play.againChip, sizeof(a->play.againChip), "%s", again);
    int againW = Kit_CapWidth(again, pad) + 6 + Kit_TextWidth("again", 8);
    int total = Kit_HintsWidth(h, 3, d) + KIT_HINT_GAP + againW;
    int x = WINDOW_WIDTH / 2 - total / 2;
    KitDraw_Hints(h, 3, x + Kit_HintsWidth(h, 3, d) / 2, HINTS_CY, d);
    x += Kit_HintsWidth(h, 3, d) + KIT_HINT_GAP;
    KitDraw_Cap(again, pad, x, HINTS_CY);
    Ui_Text("again", x + Kit_CapWidth(again, pad) + 6, HINTS_CY - 4, UI_T8, Ui_Theme()->dim);
}

static void DrawResults(App *a, float dt) {
    Results *r = &a->play.results;
    if (!Results_CardShown(r)) return;
    const UiTheme *t = Ui_Theme();
    const ResultsCard *c = &r->card;
    bool motion = Comfort_Motion(), done = Results_Done(r);
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(t->bg, END_DIM * Results_ScreenDim(r)));

    KitRect k = Results_CardRect();
    int cx = (int)(k.x + k.w / 2), top = (int)k.y;
    Color tc = c->kind == END_TOPOUT ? t->danger : (c->kind == END_SPRINT_CLEAR ? t->light : t->gold);
    rlPushMatrix();
    rlTranslatef(0.0f, -(float)Results_CardDrop(r), 0.0f);
    Card(k, tc);
    Ui_TextCentered(c->title, UI_T32, cx, top + 24, tc);
    Ui_TextCentered(c->sub, UI_T8, cx, top + 66, t->dim);

    char big[32];
    long shown = Results_Shown(r);
    if (c->timeValue) Record_FormatTime(shown, big, sizeof(big));
    else Menu_Group(shown, big, sizeof(big));
    int size = Kit_TextWidth(big, UI_T48) <= (int)k.w - 32 ? UI_T48 : UI_T32;
    Ui_TextCentered(big, size, cx, top + 90 + (size == UI_T32 ? 8 : 0), t->text);

    if (c->newBest && r->counted) {
        /* NEW BEST stamps in: it drops 6 px and flashes for two frames
         * (a gold tint, never white, with Reduced flashing). */
        float since = r->t - Results_DoneAt(r);
        KitRect p = StampRect();
        int drop = motion && since < 0.1f ? (int)(6.0f * (1.0f - since / 0.1f) + 0.5f) : 0;
        DrawRectangle((int)p.x, (int)p.y - drop, (int)p.w, (int)p.h, t->gold);
        Ui_TextCentered("NEW BEST", UI_T16, cx, (int)p.y + 8 - drop, t->bg);
        if (since < 2.0f / 60.0f) DrawRectangle((int)p.x, (int)p.y - drop, (int)p.w, (int)p.h, Comfort_Calm() ? Fade(t->text, 0.25f) : Fade(WHITE, 0.7f));
        Ui_TextCentered(c->bestLine, UI_T8, cx, top + 188, t->dim);
    } else if (!c->newBest) {
        Ui_TextCentered(c->bestLine, UI_T8, cx, top + 168, t->dim); /* in the middle of the stamp's place */
    }
    if (c->rankLine[0]) Ui_TextCentered(c->rankLine, UI_T8, cx, top + 206, t->text);
    if (c->note[0]) Ui_TextCentered(c->note, UI_T8, cx, top + 224, c->noteGold ? t->gold : t->faint);
    DrawRectangle((int)k.x + 24, top + 244, (int)k.w - 48, 2, t->rule);

    for (int i = 0; i < RESULTS_STATS; i++) {
        KitRect s = Results_StatRect(i);
        Ui_Text(c->statLabel[i], (int)s.x, (int)s.y + 6, UI_T8, t->dim);
        /* 16 px when it fits beside its label, else 8 (a long run's clock) */
        int room = (int)s.w - Kit_TextWidth(c->statLabel[i], UI_T8) - 8;
        bool big = Kit_TextWidth(c->statValue[i], UI_T16) <= room;
        Ui_TextRight(c->statValue[i], big ? UI_T16 : UI_T8, (int)(s.x + s.w), (int)s.y + (big ? 0 : 6), t->text);
    }

    KitList l;
    Results_Layout(r, &l);
    KitRect plate = {0};
    if (done) {
        const KitItem *f = Kit_Get(&l, r->kit.focus);
        plate = Kit_GlideTo(&r->glide, f ? f->r : l.items[0].r, dt, motion);
    }
    KitView kv = {&l, done ? &r->kit : NULL, a->input.lastDevice, a->input.hintDevice, done ? &plate : NULL};
    KitDraw_List(&kv);
    DrawConfetti(dt);
    rlPopMatrix();
    DrawResultsHints(a, done);
}

static void DrawOverlay(void *ctx) {
    App *a = ctx;
    float dt = GetFrameTime();
    if (a->play.results.on) DrawResults(a, dt);
    else if (a->play.pause.open) DrawPause(a, dt);
}

bool ScrPlay_Kit(const App *a, KitList *l, const KitState **state) {
    const PlayState *p = &a->play;
    if (p->results.on) {
        Results_Layout(&p->results, l);
        *state = &p->results.kit;
        return true;
    }
    if (p->pause.open) {
        if (p->pause.dialog.open) {
            Kit_DialogLayout(&p->pause.dialog, l);
            *state = &p->pause.dlgKit;
            return true;
        }
        QuickValues q = Quick(a);
        PauseMenu_Layout(&p->pause, &q, l);
        *state = &p->pause.kit;
        return true;
    }
    Kit_Clear(l, 0);
    *state = NULL;
    return false;
}

void ScrPlay_Draw(App *a, float dt) {
    (void)dt;
    PlayState *p = &a->play;
    int gp = a->settings.ghostPiece;
    p->view.ghost = gp == GHOST_OFF ? GHOST_OFF : (gp == GHOST_SOLID ? GHOST_SOLID : GHOST_OUTLINE); /* a look this build does not know: the outline */
    p->view.grid = a->settings.grid;
    p->view.end = p->results.on ? &p->results : NULL;
    p->view.overlay = (p->results.on || p->pause.open) ? DrawOverlay : NULL;
    p->view.overlayCtx = a;
    p->view.wipe = &a->wipe;
    /* Render first: it starts its flashes and particles off the same
     * one-frame events the sounds just used. */
    Render_Frame(&p->game, &p->view);
    Game_ConsumeFrameFlags(&p->game);
}

void ScrPlay_Shutdown(App *a) {
    /* A run still going when the game closes counts for what it scored. */
    SubmitRun(a);
}
