#define _POSIX_C_SOURCE 200809L
#include "safefile.h"
#include "howto.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const HowTo kHowTos[] = {
    {"blockfall", "Blockfall",
     "Stack the falling pieces so complete rows vanish. Pick a mode and a board size before you start; Power mode adds bombs, lasers and more.",
     {{"Left / Right", "Slide the piece"}, {"Up", "Rotate"}, {"Down", "Soft drop"}, {"Space", "Hard drop"}, {"C", "Hold a piece"}, {"P", "Pause"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Clear several rows at once, and chain them, for far more points than one at a time."},
    {"coilrush", "Coilrush",
     "Steer the snake to the food and grow. Hit a wall (in Classic and Maze) or your own body and the run ends.",
     {{"Arrows / WASD", "Steer"}, {"P", "Pause"}, {"R", "Restart"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "After every seventh food a spirit relic appears. Phase lets you slide through your own body, so grab it when you are coiled up."},
    {"brickburst", "Brickburst",
     "Bounce the ball off your paddle to break every brick. Let the ball fall past you and you lose a ball.",
     {{"Arrows / A D / mouse", "Move the paddle"}, {"Space", "Launch the ball"}, {"P", "Pause"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Falling capsules are good: catch them for lasers, sticky paddle or a shield. Where the ball hits the paddle sets its angle."},
    {"skyraid", "Skyraid",
     "Shoot the marching formation before it reaches the ground. Shelter behind the bunkers, and hit the mystery ship for a big bonus.",
     {{"Arrows / A D", "Move"}, {"Space", "Fire"}, {"P", "Pause"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Only one shot flies at a time, so make it count. Catch the falling capsules: Rapid, Twin, Shield and Nova."},
    {"ghostmaze", "Ghostmaze",
     "You are a ghost. The priest's bell sees you if you stand in his beam. Possess things and slip between them to reach the exit mirror.",
     {{"WASD", "Move"}, {"Space", "Possess or leave a host"}, {"P", "Pause"}, {"R", "Restart the room"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "The cat fits through flaps, the armor smashes cracked walls, the servant carries keys. Each room is solvable, and the par shows the shortest way."},
    {"rockdrift", "Rockdrift",
     "Fly your ship through the asteroid field and shoot the rocks apart. Big ones break into smaller ones, and the saucers shoot back.",
     {{"Left / Right", "Turn"}, {"Up", "Thrust"}, {"Space", "Fire"}, {"Shift", "Hyperspace (risky)"}, {"P", "Pause"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "You keep drifting in the direction you last thrust. Turn around and thrust the other way to brake."},
    {"lanehop", "Lanehop",
     "Hop across five lanes of traffic and five of river to reach a burrow at the top. Fill all five to clear the level.",
     {{"Arrows / WASD", "Hop one tile"}, {"P", "Pause"}, {"R", "Restart"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Logs and turtles carry you sideways, and some turtles dive. Goodies on the middle verge: carrots, clocks and a shield charm."},
    {"crawlshot", "Crawlshot",
     "A caterpillar winds down through the toadstools. Shoot it: each hit splits it in two. Clear every segment to finish the wave.",
     {{"WASD", "Move (bottom of the field)"}, {"Space", "Fire"}, {"P", "Pause"}, {"R", "Restart"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Spiders, fleas and scorpions drop spores when shot: Pierce, Blast and Freeze."},
    {"moondrop", "Moondrop",
     "Land gently on a flat pad. Come down slowly, level and over the pad, or the ship is lost. Fuel is limited.",
     {{"Left / Right", "Rotate"}, {"Up / Space", "Thrust"}, {"P", "Pause"}, {"R", "Restart"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Smaller pads pay more. From level 3 the wind pushes you sideways; fuel pods in the sky refill you."},
    {"gemdive", "Gemdive",
     "Tunnel through the dirt, collect enough gems to open the exit, and get out before the clock runs down.",
     {{"Arrows / WASD", "Dig and move"}, {"P", "Pause"}, {"R", "Restart"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Boulders and gems fall. A rock that is already falling crushes you, so never dig straight under one. Push a boulder sideways, never up or down."},
    {"girderclimb", "Girderclimb",
     "Climb the burning tower with a grapple. Swing from ring to ring and reach the top before the fire catches you.",
     {{"Space", "Fire or let go of the grapple"}, {"Up / Down", "Reel in / let out"}, {"Left / Right", "Pump your swing"}, {"X", "Jump from a ledge"}, {"P", "Pause"}, {"Esc", "Back to the menu"}, {NULL, NULL}},
     "Letting go keeps your speed, so let go at the top of a swing. Flags are checkpoints."},
};

int HowTo_Count(void) { return (int)(sizeof(kHowTos) / sizeof(kHowTos[0])); }
const HowTo *HowTo_At(int i) { return (i >= 0 && i < HowTo_Count()) ? &kHowTos[i] : NULL; }

const HowTo *HowTo_ForSlug(const char *slug) {
    for (int i = 0; i < HowTo_Count(); i++) if (slug && strcmp(kHowTos[i].slug, slug) == 0) return &kHowTos[i];
    return NULL;
}

static bool FlagPath(const char *slug, char *out, size_t size, bool makeDirs) {
    if (!slug || !slug[0] || strchr(slug, '/')) return false;
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char dir[700];
    snprintf(dir, sizeof(dir), "%s/%s", base, slug);
    if (makeDirs) { mkdir(base, 0755); if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false; }
    snprintf(out, size, "%s/howto_seen", dir);
    return true;
}

bool HowTo_Seen(const char *slug) {
    char path[800];
    if (!FlagPath(slug, path, sizeof(path), false)) return true; /* can't remember: don't nag */
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

void HowTo_MarkSeen(const char *slug) {
    char path[800];
    if (!FlagPath(slug, path, sizeof(path), true)) return;
    FILE *f = SafeFile_Open(path);
    if (f) { fputs("1\n", f); SafeFile_Close(f); }
}
