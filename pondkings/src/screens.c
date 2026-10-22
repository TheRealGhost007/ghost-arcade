#include "screens.h"
#include "render.h"
#include "input.h"
#include "teams.h"
#include "prefs.h"
#include "version.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const Color kPanel = {14, 20, 48, 214};
static const Color kPanelEdge = {150, 190, 240, 255};
static const Color kText = {236, 242, 255, 255};
static const Color kDim = {150, 166, 206, 255};
static const Color kGold = {250, 206, 84, 255};
static const Color kRed = {240, 110, 110, 255};
static const Color kGreen = {120, 230, 150, 255};

static void Panel(int x, int y, int w, int h) {
    DrawRectangle(x + 4, y + 5, w, h, (Color){0, 0, 0, 80});
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 2, kPanelEdge);
}

/* A little pixel crown: the "Kings". */
static void Crown(int cx, int y, int s) {
    Color c = kGold, dark = {170, 120, 30, 255};
    DrawRectangle(cx - 6 * s, y + 4 * s, 12 * s, 4 * s, c);
    for (int k = -1; k <= 1; k++) {
        DrawTriangle((Vector2){(float)(cx + k * 4 * s - 2 * s), (float)(y + 4 * s)}, (Vector2){(float)(cx + k * 4 * s + 2 * s), (float)(y + 4 * s)},
                     (Vector2){(float)(cx + k * 4 * s), (float)(y + (k == 0 ? -2 : 0) * s)}, c);
    }
    DrawRectangle(cx - 6 * s, y + 7 * s, 12 * s, s, dark);
    DrawRectangle(cx - s, y + 5 * s, 2 * s, 2 * s, (Color){220, 60, 70, 255});
}

/* A puck, for the Journey's chances. */
static void PuckIcon(int x, int y, bool full) {
    DrawEllipse(x, y, 9, 5, full ? (Color){20, 22, 34, 255} : (Color){60, 66, 96, 255});
    if (full) DrawEllipse(x - 2, y - 2, 4, 2, (Color){90, 96, 120, 255});
}

static void Logo(int y) {
    const char *t = "POND KINGS";
    int size = 70, w = MeasureText(t, size), x = SCREEN_W / 2 - w / 2;
    for (int dx = -5; dx <= 5; dx += 5)
        for (int dy = -5; dy <= 5; dy += 5) DrawText(t, x + dx, y + dy, size, (Color){12, 16, 40, 255});
    DrawText(t, x, y, size, (Color){236, 244, 255, 255});
    DrawText(t, x, y + 3, size, (Color){170, 210, 245, 90});
    Crown(SCREEN_W / 2, y - 46, 4);
}

/* A venue behind a menu: the rinks' signs and crowds are busy, so the band
 * the title and its line sit on is darkened to keep them readable. */
static void MenuBackdrop(int venue) {
    Render_VenueBackdrop(venue);
    if (venue != VENUE_POND) {
        DrawRectangle(0, 0, SCREEN_W, 128, (Color){6, 10, 30, 196});
        DrawRectangleGradientV(0, 128, SCREEN_W, 24, (Color){6, 10, 30, 196}, (Color){6, 10, 30, 0});
    }
}

static void Title(const char *t, int y) { Render_TextCentred(t, SCREEN_W / 2, y, 40, kText); }
static void Hint(const char *text) { Render_TextCentred(text, SCREEN_W / 2, SCREEN_H - 30, 10, kDim); }

static void Item(const char *label, int cx, int y, bool sel) {
    Color c = sel ? kGold : kText;
    Render_TextCentred(label, cx, y, 20, c);
    if (sel) {
        int w = Render_TextWidth(label, 20);
        DrawTriangle((Vector2){(float)(cx - w / 2 - 22), (float)y + 2}, (Vector2){(float)(cx - w / 2 - 22), (float)y + 18}, (Vector2){(float)(cx - w / 2 - 10), (float)y + 10}, kGold);
    }
}

static void Row(const char *label, const char *value, int x, int w, int y, bool sel) {
    Color c = sel ? kGold : kText;
    Render_Text(label, x, y, 20, c);
    char v[64];
    snprintf(v, sizeof(v), "<  %s  >", value);
    Render_Text(v, x + w - Render_TextWidth(v, 20), y, 20, sel ? kGold : kDim);
}

static const char *DiffName(int d) { return d == DIFF_EASY ? "Easy" : d == DIFF_HARD ? "Hard" : "Normal"; }

/* ------------------------------------------------------------ title, menu */

void Screen_Title(void) {
    Render_Backdrop(true);
    Logo(150);
    Render_TextCentred("Pond hockey with your friends", SCREEN_W / 2, 240, 20, kGold);
    if (fmodf((float)GetTime(), 1.2f) < 0.8f) Render_TextCentred("Press A or Enter", SCREEN_W / 2, 470, 20, kText);
    Render_Text("v" POND_KINGS_VERSION, SCREEN_W - 70, SCREEN_H - 24, 10, kDim);
}

void Screen_Menu(int sel) {
    Render_Backdrop(true);
    Logo(96);
    static const char *const kItems[MENU_ITEMS] = {"Journey", "Quick game", "Shootout", "Daily pond", "Online", "Rival crews",
                                                   "How to play", "Settings", "Quit"};
    static const char *const kWhat[MENU_ITEMS] = {
        "From the pond to the arena with your crew",
        "Any venue, any teams, co-op or versus",
        "You against the goalie: three misses and you're out",
        "Today's game, the same for everyone",
        "Play a friend on their computer, by room code",
        "Share your crew; meet your friends' crews",
        "The rules and the controls",
        "Sound, fullscreen and keyboard keys",
        "Back to the arcade",
    };
    Panel(300, 186, 360, 382);
    for (int i = 0; i < MENU_ITEMS; i++) Item(kItems[i], SCREEN_W / 2, 200 + i * 38, i == sel);
    Render_TextCentred(kWhat[sel], SCREEN_W / 2, 580, 10, kGold);
    Hint("Up/Down choose   A/Enter pick   B/Esc back");
}

/* ------------------------------------------------------------ who's playing */

void Screen_Join(const JoinSlot slots[MAX_PLAYERS], int rivals, const bool deviceFree[], int deviceCount, bool teams, const char *title) {
    Render_Backdrop(true);
    Title(title ? title : "WHO'S PLAYING?", 40);
    Render_TextCentred("Each player presses their join button", SCREEN_W / 2, 92, 20, kDim);
    int cardW = 200, gap = 20, x0 = (SCREEN_W - (cardW * 4 + gap * 3)) / 2;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        int x = x0 + p * (cardW + gap), y = 140;
        Panel(x, y, cardW, 250);
        Color pc = Render_PlayerColor(p);
        DrawRectangle(x + 2, y + 2, cardW - 4, 34, (Color){pc.r, pc.g, pc.b, 200});
        char t[12];
        snprintf(t, sizeof(t), "P%d", p + 1);
        Render_TextCentred(t, x + cardW / 2, y + 8, 20, kText);
        if (!slots[p].joined) {
            Render_TextCentred("Not playing", x + cardW / 2, y + 110, 20, kDim);
            continue;
        }
        Render_TextCentred(Input_DeviceName(slots[p].device), x + cardW / 2, y + 52, 10, kText);
        int team = teams ? slots[p].team : 0;
        Color tc = Render_TeamColor(team, rivals);
        DrawRectangle(x + 20, y + 96, cardW - 40, 54, (Color){tc.r, tc.g, tc.b, 230});
        Render_TextCentred(Team_ForSide(team, rivals)->name, x + cardW / 2, y + 106, 20, kText);
        Render_TextCentred(teams ? "<   team   >" : "the crew", x + cardW / 2, y + 132, 10, kText);
        char keys[48];
        snprintf(keys, sizeof(keys), "Shoot %s  Pass %s", Input_ButtonName(slots[p].device, BTN_SHOOT, false), Input_ButtonName(slots[p].device, BTN_PASS, false));
        Render_TextCentred(keys, x + cardW / 2, y + 176, 10, kDim);
        snprintf(keys, sizeof(keys), "Check %s", Input_ButtonName(slots[p].device, BTN_CHECK, false));
        Render_TextCentred(keys, x + cardW / 2, y + 194, 10, kDim);
    }
    Panel(x0, 412, cardW * 4 + gap * 3, 110);
    Render_Text("Join with:", x0 + 18, 426, 20, kText);
    int col = 0;
    for (int d = 0; d < deviceCount; d++) {
        if (!deviceFree[d]) continue;
        char line[64];
        if (d == DEV_KEYS_LEFT) snprintf(line, sizeof(line), "Keyboard (WASD): %s", Input_KeyLabel(Input_Keys(0)[KA_SHOOT]));
        else if (d == DEV_KEYS_RIGHT) snprintf(line, sizeof(line), "Keyboard (arrows): %s", Input_KeyLabel(Input_Keys(1)[KA_SHOOT]));
        else snprintf(line, sizeof(line), "%s: A", Input_DeviceName(d));
        Render_Text(line, x0 + 18 + (col % 3) * 300, 460 + (col / 3) * 24, 10, kGold);
        col++;
    }
    if (col == 0) Render_Text("Everyone who can is in!", x0 + 18, 460, 10, kGold);
    Hint(teams ? "Left/Right: pick a team   B/Esc: drop out   Start/Enter: next" : "B/Esc: drop out   Start/Enter: next");
}

/* ------------------------------------------------------------ quick game */

void Screen_Setup(int sel, const Settings *s, const JoinSlot slots[MAX_PLAYERS]) {
    MenuBackdrop(s->venue);
    Title("QUICK GAME", 40);
    int crew = 0, rival = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!slots[p].joined) continue;
        if (slots[p].team == 0) crew++;
        else rival++;
    }
    const char *rivalName = Team_Rival(s->rivals)->name;
    char mode[96];
    if (crew && rival) snprintf(mode, sizeof(mode), "Versus: Rink Rats against %s", rivalName);
    else if (crew > 1) snprintf(mode, sizeof(mode), "Co-op: %d players on the Rink Rats", crew);
    else if (rival > 1) snprintf(mode, sizeof(mode), "Co-op: %d players on the %s", rival, rivalName);
    else snprintf(mode, sizeof(mode), "%s against the computer", crew ? "Rink Rats" : rivalName);
    Render_TextCentred(mode, SCREEN_W / 2, 94, 20, kGold);
    int x = 230, w = 500;
    Panel(x - 30, 140, w + 60, 340);
    char per[32];
    snprintf(per, sizeof(per), "%d minute%s x 2", s->periodMinutes, s->periodMinutes == 1 ? "" : "s");
    Row("Where", Game_VenueName(s->venue), x, w, 168, sel == 0);
    Row("Rivals", rivalName, x, w, 224, sel == 1);
    Row("Periods", per, x, w, 280, sel == 2);
    Row("Computer players", DiffName(s->difficulty), x, w, 336, sel == 3);
    Item("Face off!", SCREEN_W / 2, 412, sel == 4);
    static const char *const kVenue[VENUE_COUNT] = {
        "No goalies, low nets, snowbanks that swallow the puck", "Boards, goalies and slapshots, in the town's old barn",
        "The big time: the crowd, the lights, the replays",
    };
    Render_TextCentred(kVenue[s->venue % VENUE_COUNT], SCREEN_W / 2, 500, 10, kDim);
    Hint("Up/Down choose   Left/Right change   A/Enter start   B/Esc back");
}

void Screen_ShootoutSetup(int sel, const Settings *s) {
    MenuBackdrop(s->soVenue);
    Title("SHOOTOUT", 40);
    Render_TextCentred("Skate in alone and beat the goalie. Three misses and you're out.", SCREEN_W / 2, 100, 10, kGold);
    Render_TextCentred("He learns where you like to shoot, and gets sharper as you score.", SCREEN_W / 2, 118, 10, kDim);
    int x = 230, w = 500;
    Panel(x - 30, 180, w + 60, 200);
    Row("Where", Game_VenueName(s->soVenue), x, w, 220, sel == 0);
    Item("Start", SCREEN_W / 2, 300, sel == 1);
    Hint("Left/Right change   A/Enter start   B/Esc back");
}

/* ------------------------------------------------------------ daily */

void Screen_Daily(const DailyInfo *d, int sel) {
    Render_Backdrop(true);
    Title("DAILY POND", 40);
    Render_TextCentred(d->date, SCREEN_W / 2, 92, 20, kGold);
    Panel(200, 140, 560, 300);
    char line[96];
    snprintf(line, sizeof(line), "Today: the Rink Rats against the %s", Team_Rival(d->rival)->name);
    Render_TextCentred(line, SCREEN_W / 2, 166, 20, kText);
    snprintf(line, sizeof(line), "Today's twist: %s", Game_TwistName(d->twist));
    Render_TextCentred(line, SCREEN_W / 2, 206, 20, kGold);
    static const char *const kTwist[TWIST_COUNT] = {"", "A wind blows the puck across the pond", "The snow comes down twice as fast",
                                                    "Glassy ice: everything glides further", "Tiny nets: aim small"};
    Render_TextCentred(kTwist[d->twist % TWIST_COUNT], SCREEN_W / 2, 236, 10, kDim);
    Render_TextCentred("Two one-minute periods. Points for goals, one-timers and the win.", SCREEN_W / 2, 262, 10, kDim);
    if (d->best > 0) snprintf(line, sizeof(line), "Your best today: %ld", d->best);
    else snprintf(line, sizeof(line), "No game today yet");
    Render_TextCentred(line, SCREEN_W / 2, 298, 20, kText);
    Item("Play", SCREEN_W / 2 - 100, 370, sel == 0);
    Item("Back", SCREEN_W / 2 + 100, 370, sel == 1);
    Hint("Left/Right choose   A/Enter pick   B/Esc back");
}

/* ------------------------------------------------------------ the journey */

static void KidLine(const CrewKid *k, int x, int y, Color c) {
    char line[64];
    snprintf(line, sizeof(line), "%s  (%s)", k->name, Game_BuildName(k->build));
    Render_Text(line, x, y, 20, c);
    if (k->trait != TRAIT_NONE) {
        snprintf(line, sizeof(line), "%s: %s", Game_TraitName(k->trait), Game_TraitWords(k->trait));
        Render_Text(line, x + 16, y + 24, 10, kGold);
    }
}

void Screen_JourneyHub(const Journey *j, bool haveRun, int sel, bool confirmNew) {
    int venue = haveRun && j->active ? Journey_Venue(j) : VENUE_POND;
    MenuBackdrop(venue);
    Title("THE JOURNEY", 30);
    if (!haveRun || !j->active) {
        Panel(160, 110, 640, 330);
        static const char *const kAbout[] = {
            "The Rink Rats start on the pond, and play their way up",
            "through the town rink to the arena: three games at each.",
            "",
            "Win to go on. Lose, and it costs one of three chances:",
            "you play that game again. Run out, and the run is over.",
            "",
            "Every win lets you sign one of the kids you just beat,",
            "signature move and all. Beat a team badly and it remembers:",
            "it'll be back, sharper and rougher.",
        };
        for (unsigned k = 0; k < sizeof(kAbout) / sizeof(kAbout[0]); k++) Render_TextCentred(kAbout[k], SCREEN_W / 2, 132 + (int)k * 28, 10, kText);
        if (haveRun && j->finished) {
            char line[64];
            snprintf(line, sizeof(line), j->champion ? "Your last run: champions, %d points" : "Your last run: %d wins, %d points", j->wins, j->points);
            Render_TextCentred(line, SCREEN_W / 2, 400, 10, kGold);
        }
        Item("Start a Journey", SCREEN_W / 2, 470, sel == 0);
        Item("Back", SCREEN_W / 2, 510, sel != 0);
        Hint("A/Enter pick   B/Esc back");
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "%s: %s", Journey_StageName(j->stage), Journey_GameName(j));
    Render_TextCentred(line, SCREEN_W / 2, 80, 20, kGold);
    Panel(80, 116, 380, 330);
    Render_Text("YOUR CREW", 100, 132, 20, kText);
    for (int k = 0; k < TEAM_SIZE; k++) KidLine(&j->crew[k], 100, 176 + k * 70, kText);
    Panel(500, 116, 380, 330);
    int opp = Journey_Opponent(j);
    Render_Text("NEXT", 520, 132, 20, kText);
    snprintf(line, sizeof(line), "%s", Journey_OpponentName(j, opp));
    Render_Text(line, 520, 172, 20, opp == JOURNEY_FRIEND ? kGreen : kGold);
    Render_Text(Game_VenueName(Journey_Venue(j)), 520, 204, 10, kDim);
    if (opp >= 0 && opp < RIVAL_COUNT && j->grudge[opp] >= 2) Render_Text("They remember you. They'll be rough.", 520, 226, 10, kRed);
    if (opp == JOURNEY_FRIEND) Render_Text("A friend's crew, played their way", 520, 226, 10, kGreen);
    Render_Text("Chances", 520, 270, 10, kDim);
    for (int c = 0; c < JOURNEY_CHANCES; c++) PuckIcon(620 + c * 24, 276, c < j->chances);
    snprintf(line, sizeof(line), "Points  %d", j->points);
    Render_Text(line, 520, 304, 20, kText);
    snprintf(line, sizeof(line), "Wins %d   Goals %d - %d", j->wins, j->goalsFor, j->goalsAgainst);
    Render_Text(line, 520, 340, 10, kDim);
    if (confirmNew) {
        Render_TextCentred("Start over? This run will be lost.", SCREEN_W / 2, 470, 20, kRed);
        Item("Yes, start over", SCREEN_W / 2 - 150, 510, sel == 0);
        Item("No", SCREEN_W / 2 + 150, 510, sel != 0);
    } else {
        static const char *const kItems[HUB_ITEMS] = {"Play", "New Journey", "Back"};
        for (int i = 0; i < HUB_ITEMS; i++) Item(kItems[i], SCREEN_W / 2 - 260 + i * 260, 480, i == sel);
    }
    Hint("Left/Right choose   A/Enter pick   B/Esc back");
}

void Screen_Sign(const Journey *j, int step, int theirs, int sel) {
    Render_Backdrop(true);
    Title("SIGN A PLAYER", 30);
    CrewKid options[TEAM_SIZE];
    Journey_SignOptions(j, options);
    char line[96];
    if (step == 0) {
        snprintf(line, sizeof(line), "You beat the %s. One of them can join your crew.", Journey_OpponentName(j, j->lastOpponent));
        Render_TextCentred(line, SCREEN_W / 2, 84, 10, kGold);
        for (int k = 0; k < TEAM_SIZE; k++) {
            Panel(110, 120 + k * 110, 740, 96);
            KidLine(&options[k], 140, 140 + k * 110, sel == k ? kGold : kText);
            if (sel == k) DrawRectangleLinesEx((Rectangle){110, (float)(120 + k * 110), 740, 96}, 3, kGold);
        }
        Item("Keep my crew as it is", SCREEN_W / 2, 470, sel == TEAM_SIZE);
    } else {
        snprintf(line, sizeof(line), "%s joins. Who makes room?", options[theirs].name);
        Render_TextCentred(line, SCREEN_W / 2, 84, 10, kGold);
        for (int k = 0; k < TEAM_SIZE; k++) {
            Panel(110, 120 + k * 110, 740, 96);
            KidLine(&j->crew[k], 140, 140 + k * 110, sel == k ? kGold : kText);
            if (sel == k) DrawRectangleLinesEx((Rectangle){110, (float)(120 + k * 110), 740, 96}, 3, kGold);
        }
        Item("Back", SCREEN_W / 2, 470, sel == TEAM_SIZE);
    }
    Hint("Up/Down choose   A/Enter pick");
}

void Screen_JourneyResult(const Journey *j, const JourneyResult *r, const Game *g, const RenderInfo *ri) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){6, 10, 30, 160});
    Panel(180, 100, 600, 400);
    Title(r->won ? "A WIN!" : "A LOSS", 124);
    char line[96];
    snprintf(line, sizeof(line), "Rink Rats %d  -  %d %s", g->score[0], g->score[1], ri && ri->rivalName ? ri->rivalName : Team_Rival(ri ? ri->rivals : 0)->name);
    Render_TextCentred(line, SCREEN_W / 2, 186, 20, kText);
    snprintf(line, sizeof(line), "+%ld points   (%d in all)", r->points, j->points);
    Render_TextCentred(line, SCREEN_W / 2, 226, 20, kGold);
    if (r->champion) Render_TextCentred("You won the arena playoffs!", SCREEN_W / 2, 270, 20, kGreen);
    else if (r->stageCleared) {
        snprintf(line, sizeof(line), "On to %s!", Journey_StageName(j->stage));
        Render_TextCentred(line, SCREEN_W / 2, 270, 20, kGreen);
    } else if (r->out) Render_TextCentred("That was your last chance.", SCREEN_W / 2, 270, 20, kRed);
    else if (!r->won) {
        snprintf(line, sizeof(line), "%d chance%s left: you'll play them again.", j->chances, j->chances == 1 ? "" : "s");
        Render_TextCentred(line, SCREEN_W / 2, 270, 20, kRed);
    }
    for (int c = 0; c < JOURNEY_CHANCES; c++) PuckIcon(SCREEN_W / 2 - 24 + c * 24, 320, c < j->chances);
    Item("Continue", SCREEN_W / 2, 420, true);
}

void Screen_JourneyEnd(const Journey *j, const RunScore *rs) {
    MenuBackdrop(j->champion ? VENUE_ARENA : VENUE_POND);
    Panel(160, 90, 640, 440);
    if (j->champion) {
        Crown(SCREEN_W / 2, 112, 5);
        Title("THE POND KINGS!", 170);
        Render_TextCentred("From the pond to the arena: champions.", SCREEN_W / 2, 224, 20, kGold);
    } else {
        Title("THE RUN ENDS HERE", 130);
        char line[64];
        snprintf(line, sizeof(line), "You got as far as %s.", Journey_StageName(j->stage));
        Render_TextCentred(line, SCREEN_W / 2, 196, 20, kText);
    }
    char line[96];
    snprintf(line, sizeof(line), "%d points   %d wins   goals %d - %d", j->points, j->wins, j->goalsFor, j->goalsAgainst);
    Render_TextCentred(line, SCREEN_W / 2, 280, 20, kText);
    if (rs && rs->rank > 0) {
        snprintf(line, sizeof(line), rs->newBest ? "A new best! #%d on your board" : "#%d on your board", rs->rank);
        Render_TextCentred(line, SCREEN_W / 2, 320, 20, kGold);
    }
    if (rs && rs->toast[0]) {
        snprintf(line, sizeof(line), "Trophy: %s", rs->toast);
        Render_TextCentred(line, SCREEN_W / 2, 360, 20, kGreen);
    }
    Item("Menu", SCREEN_W / 2, 460, true);
}

void Screen_ShootoutResult(const Game *g, const RunScore *rs, int sel) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){6, 10, 30, 160});
    Panel(200, 110, 560, 380);
    Title("SHOOTOUT OVER", 134);
    char line[64];
    snprintf(line, sizeof(line), "%d goal%s", g->score[0], g->score[0] == 1 ? "" : "s");
    Render_TextCentred(line, SCREEN_W / 2, 200, 40, kGold);
    snprintf(line, sizeof(line), "Best streak %d   Tries %d", g->soBestStreak, g->soAttempts);
    Render_TextCentred(line, SCREEN_W / 2, 260, 20, kText);
    if (rs && rs->rank > 0) {
        snprintf(line, sizeof(line), rs->newBest ? "A new best! #%d on your board" : "#%d on your board", rs->rank);
        Render_TextCentred(line, SCREEN_W / 2, 300, 20, kGold);
    }
    if (rs && rs->toast[0]) {
        snprintf(line, sizeof(line), "Trophy: %s", rs->toast);
        Render_TextCentred(line, SCREEN_W / 2, 336, 10, kGreen);
    }
    Item("Again", SCREEN_W / 2 - 120, 420, sel == 0);
    Item("Menu", SCREEN_W / 2 + 120, 420, sel == 1);
}

void Screen_DailyResult(const Game *g, const DailyInfo *d, const RunScore *rs, int sel) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){6, 10, 30, 160});
    Panel(200, 110, 560, 380);
    Title("DAILY POND", 134);
    char line[96];
    snprintf(line, sizeof(line), "Rink Rats %d  -  %d %s", g->score[0], g->score[1], Team_Rival(d->rival)->name);
    Render_TextCentred(line, SCREEN_W / 2, 192, 20, kText);
    snprintf(line, sizeof(line), "%ld points", rs ? rs->score : 0L);
    Render_TextCentred(line, SCREEN_W / 2, 236, 40, kGold);
    if (rs && rs->rank > 0) {
        snprintf(line, sizeof(line), rs->newBest ? "A new best today! #%d" : "#%d today", rs->rank);
        Render_TextCentred(line, SCREEN_W / 2, 300, 20, kGold);
    }
    Render_TextCentred(d->date, SCREEN_W / 2, 336, 10, kDim);
    Item("Again", SCREEN_W / 2 - 120, 420, sel == 0);
    Item("Menu", SCREEN_W / 2 + 120, 420, sel == 1);
}

/* ------------------------------------------------------------ how to play */

void Screen_HowTo(int page) {
    Render_Backdrop(true);
    Title("HOW TO PLAY", 34);
    Panel(60, 90, 840, 500);
    int x = 90, y = 110;
    if (page == 0) {
        static const char *const kRules[] = {
            "Three on three. Skate with the stick or the keys; pass, shoot, and check the puck carrier.",
            "Press Shoot just before a pass reaches you for a one-timer: the hardest shot there is.",
            "Check with nobody close by to switch to the skater nearest the puck.",
            "Faceoffs: press Pass the moment the puck drops. Too early, and you wait.",
            "",
            "THE POND: no goalies, low narrow nets, wrist shots only. Hard pucks bury themselves in the",
            "snowbanks, snow piles up and slows everything, and between periods everyone shovels.",
            "",
            "THE TOWN RINK AND THE ARENA: boards bounce the puck, goalies stand in the nets, and",
            "slapshots are allowed: hold Shoot to wind up, let go to fire. A goalie who holds the puck",
            "stops play. The arena shows every goal again on the big screen.",
            "",
            "Friends can join any time: press A on a free pad. Hold View for a second to drop out.",
        };
        for (unsigned k = 0; k < sizeof(kRules) / sizeof(kRules[0]); k++) Render_Text(kRules[k], x, y + (int)k * 26, 10, kText);
    } else if (page == 1) {
        Render_Text("CONTROLS", x, y, 20, kGold);
        y += 34;
        static const char *const kHead[4] = {"", "Shoot", "Pass", "Check / switch"};
        int cols[4] = {x, x + 230, x + 420, x + 600};
        for (int c = 0; c < 4; c++) Render_Text(kHead[c], cols[c], y, 10, kDim);
        const int *L = Input_Keys(0), *R = Input_Keys(1);
        char l1[16], l2[16], l3[16], r1[16], r2[16], r3[16];
        snprintf(l1, sizeof(l1), "%s", Input_KeyLabel(L[KA_SHOOT]));
        snprintf(l2, sizeof(l2), "%s", Input_KeyLabel(L[KA_PASS]));
        snprintf(l3, sizeof(l3), "%s", Input_KeyLabel(L[KA_CHECK]));
        snprintf(r1, sizeof(r1), "%s", Input_KeyLabel(R[KA_SHOOT]));
        snprintf(r2, sizeof(r2), "%s", Input_KeyLabel(R[KA_PASS]));
        snprintf(r3, sizeof(r3), "%s", Input_KeyLabel(R[KA_CHECK]));
        const char *rows[4][4] = {
            {"Pad", "X (or RT)", "A", "B"},
            {"Keyboard WASD side", l1, l2, l3},
            {"Keyboard arrows side", r1, r2, r3},
            {"One keyboard: also", "Z or J", "X or K", "C or L"},
        };
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) Render_Text(rows[r][c], cols[c], y + 24 + r * 24, 10, c == 0 ? kGold : kText);
        Render_Text("Pause: Start or Esc.   Fullscreen: F11.   Change the keyboard keys in Settings.", x, y + 140, 10, kDim);
    } else {
        static const char *const kModes[] = {
            "JOURNEY: the Rink Rats from the pond to the arena. Win to go on; lose and it costs a chance.",
            "  Sign a kid from each team you beat. Beat a team badly and it comes back for you.",
            "",
            "QUICK GAME: any venue, any rivals. Everyone on the Rink Rats is co-op; players on both teams is versus.",
            "",
            "SHOOTOUT: you, the puck and a goalie who learns. Three misses and you're out.",
            "",
            "DAILY POND: one game a day with a twist, the same for everyone. Your best goes on the board.",
            "",
            "ONLINE: one of you hosts and reads out a room code; the other types it in. Rollback keeps",
            "  it smooth: only button presses cross the internet.",
            "",
            "RIVAL CREWS: share your crew's code. Friends who add it meet your crew in their Journey's",
            "  final, played the way you play.",
        };
        for (unsigned k = 0; k < sizeof(kModes) / sizeof(kModes[0]); k++) Render_Text(kModes[k], x, y + (int)k * 26, 10, kText);
    }
    char pg[16];
    snprintf(pg, sizeof(pg), "%d / %d", page + 1, HOWTO_PAGES);
    Render_TextCentred(pg, SCREEN_W / 2, 560, 10, kDim);
    Hint("Left/Right: page   B/Esc back");
}

/* ------------------------------------------------------------ settings */

void Screen_Settings(int sel, const Settings *s) {
    Render_Backdrop(true);
    Title("SETTINGS", 50);
    int x = 230, w = 500;
    Panel(x - 30, 120, w + 60, 380);
    char v[16];
    snprintf(v, sizeof(v), "%d%%", s->sfxVolume);
    Row("Effects volume", v, x, w, 150, sel == 0);
    snprintf(v, sizeof(v), "%d%%", Prefs_Get()->musicVolume);
    Row("Music volume", v, x, w, 206, sel == 1);
    Row("Sound", s->sound ? "On" : "Off", x, w, 262, sel == 2);
    Row("Fullscreen", Prefs_Get()->fullscreen ? "On" : "Off", x, w, 318, sel == 3);
    Item("Keyboard keys", SCREEN_W / 2, 382, sel == 4);
    Item("Back", SCREEN_W / 2, 434, sel == 5);
    Render_TextCentred("Music volume and fullscreen are shared with every Ghost Arcade game.", SCREEN_W / 2, 520, 10, kDim);
    Hint("Up/Down choose   Left/Right change   B/Esc back");
}

void Screen_Keys(int row, int col, const Settings *s, bool waiting) {
    Render_Backdrop(true);
    Title("KEYBOARD KEYS", 40);
    Panel(140, 100, 680, 420);
    static const char *const kAct[KA_COUNT] = {"Up", "Down", "Left", "Right", "Shoot", "Pass", "Check"};
    Render_Text("WASD side", 400, 120, 20, kDim);
    Render_Text("Arrows side", 600, 120, 20, kDim);
    for (int a = 0; a < KA_COUNT; a++) {
        int y = 160 + a * 40;
        Render_Text(kAct[a], 180, y, 20, kText);
        for (int h = 0; h < 2; h++) {
            bool sel = row == a && col == h;
            const char *label = sel && waiting ? "press a key" : Input_KeyLabel(s->keys[h][a]);
            Render_Text(label, 400 + h * 200, y, 20, sel ? kGold : kText);
            if (sel) DrawRectangleLines(392 + h * 200, y - 6, 180, 32, kGold);
        }
    }
    Item("Reset to the usual keys", SCREEN_W / 2, 456, row == KA_COUNT);
    Hint(waiting ? "Press the new key   Esc keeps the old one" : "Arrows choose   Enter changes   Esc back");
}

/* ------------------------------------------------------------ in game */

void Screen_Pause(int sel, bool online) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){6, 10, 30, 150});
    Panel(330, 190, 300, 230);
    Render_TextCentred(online ? "MENU" : "PAUSED", SCREEN_W / 2, 210, 30, kText);
    static const char *const kItems[PAUSE_ITEMS] = {"Resume", "Restart", "Quit to menu"};
    static const char *const kOnline[PAUSE_ITEMS] = {"Back to the game", "", "Leave the game"};
    for (int i = 0; i < PAUSE_ITEMS; i++) {
        const char *label = online ? kOnline[i] : kItems[i];
        if (!label[0]) continue;
        Item(label, SCREEN_W / 2, 270 + i * 44, i == sel);
    }
    Hint(online ? "The game keeps going online while this is open." : "A free pad can join with A.   A pad holding View drops out.");
}

void Screen_Results(const Game *g, const RenderInfo *ri, int sel) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){6, 10, 30, 160});
    int rivals = ri ? ri->rivals : 0;
    int winner = g->winner < 0 ? 0 : g->winner;
    const char *winName = winner == 0 ? Team_Crew()->name : (ri && ri->rivalName ? ri->rivalName : Team_Rival(rivals)->name);
    char title[64];
    snprintf(title, sizeof(title), "The %s win!", winName);
    Panel(150, 60, 660, 520);
    Crown(SCREEN_W / 2, 74, 3);
    Render_TextCentred(title, SCREEN_W / 2, 110, 40, Render_TeamColor(winner, rivals));
    char score[64];
    snprintf(score, sizeof(score), "%s %d  -  %d %s", Team_Crew()->name, g->score[0], g->score[1], ri && ri->rivalName ? ri->rivalName : Team_Rival(rivals)->name);
    Render_TextCentred(score, SCREEN_W / 2, 164, 20, kText);
    if (g->winner >= 0 && g->period > g->setup.periods) Render_TextCentred("in sudden death", SCREEN_W / 2, 192, 10, kGold);
    int y = 226;
    Render_Text("Skater", 200, y, 10, kDim);
    Render_Text("Goals", 480, y, 10, kDim);
    Render_Text("Assists", 560, y, 10, kDim);
    Render_Text("Shots", 660, y, 10, kDim);
    for (int i = 0; i < SKATERS; i++) {
        int row = y + 26 + i * 30;
        const Skater *s = &g->sk[i];
        DrawRectangle(200, row + 2, 10, 10, Render_TeamColor(s->team, rivals));
        char name[64];
        snprintf(name, sizeof(name), "%s  (%s)", Render_SkaterNameIn(g, i, ri), Game_BuildName(s->build));
        Render_Text(name, 220, row, 10, s->player >= 0 ? Render_PlayerColor(s->player) : kText);
        char n[8];
        snprintf(n, sizeof(n), "%d", s->goals);
        Render_Text(n, 490, row, 10, kText);
        snprintf(n, sizeof(n), "%d", s->assists);
        Render_Text(n, 580, row, 10, kText);
        snprintf(n, sizeof(n), "%d", s->shots);
        Render_Text(n, 670, row, 10, kText);
    }
    if (g->gk[0].present || g->gk[1].present) {
        char sv[64];
        snprintf(sv, sizeof(sv), "Saves: %s %d, %s %d", Team_Crew()->goalie, g->gk[0].saves, Team_Rival(rivals)->goalie, g->gk[1].saves);
        Render_TextCentred(sv, SCREEN_W / 2, 446, 10, kDim);
    }
    static const char *const kItems[RESULT_ITEMS] = {"Play again", "Menu"};
    for (int i = 0; i < RESULT_ITEMS; i++) Item(kItems[i], SCREEN_W / 2 - 120 + i * 240, 500, i == sel);
}

/* ------------------------------------------------------------ online and the rival crews */

/* Centred text wrapped to width; returns the y under it. */
static int Wrapped(const char *text, int cx, int y, int width, int size, Color c) {
    char line[160] = "";
    size_t len = 0;
    const char *p = text;
    while (*p) {
        size_t word = strcspn(p, " ");
        size_t wl = word < 100 ? word : 100;
        char tryLine[264];
        size_t tl = len;
        memcpy(tryLine, line, len);
        if (len) tryLine[tl++] = ' ';
        memcpy(tryLine + tl, p, wl);
        tl += wl;
        tryLine[tl] = '\0';
        if (len && (Render_TextWidth(tryLine, size) > width || tl >= sizeof(line))) {
            Render_TextCentred(line, cx, y, size, c);
            y += size + 8;
            memcpy(line, p, wl);
            len = wl;
            line[len] = '\0';
        } else if (tl < sizeof(line)) {
            memcpy(line, tryLine, tl + 1);
            len = tl;
        }
        p += word;
        while (*p == ' ') p++;
    }
    if (len) {
        Render_TextCentred(line, cx, y, size, c);
        y += size + 8;
    }
    return y;
}

static void Spinner(int cx, int y) {
    float t = (float)GetTime();
    for (int k = 0; k < 3; k++) {
        float phase = fmodf(t * 2.4f - k * 0.35f, 2.0f);
        float lift = phase < 1.0f ? sinf(phase * PI) * 6.0f : 0.0f;
        DrawEllipse(cx + (k - 1) * 22, y - (int)lift, 8, 5, (Color){20, 22, 34, 255});
        DrawEllipse(cx + (k - 1) * 22 - 2, y - (int)lift - 2, 3, 2, (Color){90, 96, 120, 255});
    }
}

static void CodeBoxes(const char *code, int len, int y, bool typing) {
    int box = 50, gap = 10, total = len * box + (len - 1) * gap, x0 = SCREEN_W / 2 - total / 2;
    int filled = code ? (int)strlen(code) : 0;
    for (int i = 0; i < len; i++) {
        int x = x0 + i * (box + gap);
        bool cursor = typing && i == filled && fmodf((float)GetTime(), 1.0f) < 0.6f;
        DrawRectangle(x, y, box, 60, (Color){8, 12, 30, 230});
        DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)box, 60}, 2, cursor || (typing && i == filled) ? kGold : kPanelEdge);
        if (i < filled) {
            char ch[2] = {code[i], 0};
            Render_TextCentred(ch, x + box / 2, y + 12, 40, kText);
        }
    }
}

static void CodeGrid(int sel, int y) {
    const char *alpha = CODE_ALPHABET;
    int n = (int)strlen(alpha), cols = CODE_GRID_COLS, cw = 46, ch = 32, x0 = SCREEN_W / 2 - cols * cw / 2;
    for (int i = 0; i < n; i++) {
        int x = x0 + (i % cols) * cw, yy = y + (i / cols) * ch;
        if (i == sel) DrawRectangle(x + 3, yy + 2, cw - 6, ch - 4, (Color){250, 206, 84, 70});
        char c[2] = {alpha[i], 0};
        Render_TextCentred(c, x + cw / 2, yy + 7, 20, i == sel ? kGold : kDim);
    }
}

void Screen_Net(const NetView *v) {
    if (v->backdrop >= 0 && v->backdrop < VENUE_COUNT) MenuBackdrop(v->backdrop);
    else Render_Backdrop(true);
    Title(v->title ? v->title : "ONLINE", 36);
    Panel(130, 100, 700, 470);
    int y = 124;
    for (int i = 0; i < v->lineCount && i < NETVIEW_LINES; i++) {
        if (!v->lines[i]) continue;
        Color c = v->error ? (i == 0 ? kRed : kText) : (i == 0 ? kGold : kText);
        y = Wrapped(v->lines[i], SCREEN_W / 2, y, 640, i == 0 ? 20 : 20, c) + (i == 0 ? 6 : 0);
    }
    if (v->lineCount > 0) y += 10;
    int rowX = 190, rowW = 580;
    for (int r = 0; r < v->rowCount && r < NETVIEW_ROWS; r++) {
        Row(v->rowLabel[r], v->rowValue[r], rowX, rowW, y, v->sel == r);
        y += 46;
    }
    if (v->code && v->codeLen > 0) {
        CodeBoxes(v->code, v->codeLen, y, v->typing);
        y += 76;
        if (v->typing) {
            CodeGrid(v->gridSel, y);
            y += ((int)strlen(CODE_ALPHABET) / CODE_GRID_COLS) * 32 + 10;
        }
    }
    int items = v->itemCount < NETVIEW_ITEMS ? v->itemCount : NETVIEW_ITEMS;
    if (items > 0) {
        int iy = y > 470 ? y : 470;
        if (iy > 520) iy = 520;
        for (int i = 0; i < items; i++) {
            int cx = SCREEN_W / 2 + (int)((i - (items - 1) / 2.0f) * 220);
            Item(v->items[i], cx, iy, v->sel == v->rowCount + i);
        }
    }
    if (v->status && v->status[0]) {
        int sy = 586;
        if (v->busy) {
            Spinner(SCREEN_W / 2 - Render_TextWidth(v->status, 20) / 2 - 50, sy + 10);
        }
        Render_TextCentred(v->status, SCREEN_W / 2 + (v->busy ? 20 : 0), sy, 20, v->error && !v->lineCount ? kRed : kDim);
    }
    Hint(v->hint ? v->hint : "Up/Down choose   A/Enter pick   B/Esc back");
}

static void CrewBlock(const CrewCard *c, int x, int y, Color nameColour) {
    Render_Text(c->name, x, y, 20, nameColour);
    for (int k = 0; k < TEAM_SIZE; k++) KidLine(&c->kids[k], x + 10, y + 34 + k * 46, kText);
}

void Screen_Crews(const CrewsView *v) {
    Render_Backdrop(true);
    Title("RIVAL CREWS", 30);
    Render_TextCentred("Share your crew by code. Friends' crews can turn up in your Journey's final,", SCREEN_W / 2, 78, 10, kDim);
    Render_TextCentred("played by the computer the way their owner plays.", SCREEN_W / 2, 92, 10, kDim);

    Panel(30, 116, 440, 330);
    Render_Text("YOUR CREW", 50, 130, 20, kGold);
    if (v->mine) CrewBlock(v->mine, 50, 164, kText);
    Render_Text(v->myStyle ? v->myStyle : "", 50, 334, 10, kDim);
    if (v->myCode && v->myCode[0]) {
        Render_Text("Code:", 50, 366, 20, kText);
        Render_Text(v->myCode, 130, 360, 30, kGold);
        Render_Text("Friends add your crew with this.", 50, 404, 10, kDim);
    } else {
        Render_Text("Not shared yet.", 50, 372, 20, kDim);
    }

    Panel(490, 116, 440, 330);
    Render_Text("FRIENDS' CREWS", 510, 130, 20, kGold);
    int friendBase = v->itemCount;
    if (v->friendCount == 0) {
        Render_Text("None yet. Add one with", 510, 180, 20, kDim);
        Render_Text("a friend's crew code.", 510, 206, 20, kDim);
    }
    for (int i = 0; i < v->friendCount && i < CREWS_VIEW_MAX; i++) {
        int y = 162 + i * 34;
        bool sel = v->sel == friendBase + i;
        if (sel) DrawTriangle((Vector2){500, (float)y + 2}, (Vector2){500, (float)y + 18}, (Vector2){510, (float)y + 10}, kGold);
        Render_Text(v->friends[i].name, 516, y, 20, sel ? kGold : kText);
        /* its play style under the name, cut to the panel */
        char words[96];
        snprintf(words, sizeof(words), "%s", v->removing == i ? "Remove this crew?  A yes, B no" : (v->friendStyle[i] ? v->friendStyle[i] : ""));
        while (words[0] && Render_TextWidth(words, 10) > 396) words[strlen(words) - 1] = '\0';
        Render_Text(words, 532, y + 21, 10, v->removing == i ? kRed : kDim);
    }

    int n = v->itemCount + 1;
    for (int i = 0; i < n; i++) {
        int cx = SCREEN_W * (i + 1) / (n + 1);
        const char *label = i < v->itemCount ? v->items[i] : "Back";
        int selIndex = i < v->itemCount ? i : v->itemCount + v->friendCount;
        Item(label, cx, 476, v->sel == selIndex);
    }
    if (v->status && v->status[0]) {
        if (v->busy) Spinner(SCREEN_W / 2 - Render_TextWidth(v->status, 20) / 2 - 50, 540);
        Render_TextCentred(v->status, SCREEN_W / 2 + (v->busy ? 20 : 0), 530, 20, v->error ? kRed : kDim);
    }
    Hint("Left/Right or Up/Down choose   A/Enter pick   B/Esc back");
}
