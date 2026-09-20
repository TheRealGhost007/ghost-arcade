/* Cave generator: builds candidate caves for each level, has the bot play
 * them, and writes a cave only when the bot's recorded solution finishes it.
 * So every cave that ships is provably solvable, and the test suite replays
 * the solution to keep it that way.
 *
 *   make caves      (writes assets/caves/01.cave .. 20.cave)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/bot.h"

static unsigned long long sState;
static unsigned Rnd(unsigned bound) {
    sState ^= sState >> 12; sState ^= sState << 25; sState ^= sState >> 27;
    return (unsigned)(((sState * 2685821657736338717ull) >> 33) % bound);
}

static const char *const kNames[20] = {
    "First Dig", "Loose Change", "Shelf Life", "Rockfall", "Glow Worms",
    "Pit Stop", "Heavy Sleeper", "The Long Drop", "Gem Trap", "Two Shafts",
    "Slow Burn", "Crumbling", "Deep Pockets", "Cold Feet", "Fools Gold",
    "Widow Maker", "Bad Ground", "Last Light", "Undermine", "Bedrock",
};

static void Carve(char m[CAVE_ROWS][CAVE_COLS + 1], int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) if (x > 0 && x < CAVE_COLS - 1 && y > 0 && y < CAVE_ROWS - 1) m[y][x] = '.';
}

static void Generate(int level, Cave *c) {
    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", kNames[(level - 1) % 20]);
    for (int y = 0; y < CAVE_ROWS; y++) {
        for (int x = 0; x < CAVE_COLS; x++) c->map[y][x] = (x == 0 || y == 0 || x == CAVE_COLS - 1 || y == CAVE_ROWS - 1) ? '#' : ':';
        c->map[y][CAVE_COLS] = '\0';
    }
    int style = (level - 1) % 5;

    /* Start pocket, top left. */
    Carve(c->map, 1, 1, 3, 2);
    c->map[1][1] = 'P';

    /* Brick shelves: gems roll off them. */
    int shelves = level >= 3 ? 1 + level / 5 : 0;
    for (int i = 0; i < shelves; i++) {
        int len = 4 + (int)Rnd(5), y = 5 + (int)Rnd(13), x = 4 + (int)Rnd(CAVE_COLS - 10);
        for (int k = 0; k < len && x + k < CAVE_COLS - 1; k++) c->map[y][x + k] = 'B';
    }

    /* Open caverns and, for some styles, a couple of vertical shafts. */
    int caverns = 1 + level / 4;
    for (int i = 0; i < caverns; i++) Carve(c->map, 6 + (int)Rnd(28), 4 + (int)Rnd(14), 3 + (int)Rnd(4), 2 + (int)Rnd(3));
    if (style == 1 || style == 4) for (int i = 0; i < 2; i++) { int x = 8 + (int)Rnd(24), y = 3 + (int)Rnd(6); Carve(c->map, x, y, 1, 6 + (int)Rnd(6)); }

    /* Exit, lower right. */
    int ex = 26 + (int)Rnd(12), ey = 13 + (int)Rnd(7);
    c->map[ey][ex] = 'X';

    /* Boulders on dirt, more of them as levels go on. */
    int perThousand = 20 + level * 7 + (style == 3 ? 40 : 0);
    if (level == 1) perThousand = 12;
    for (int y = 3; y < CAVE_ROWS - 1; y++) {
        for (int x = 1; x < CAVE_COLS - 1; x++) {
            if (c->map[y][x] == ':' && (int)Rnd(1000) < perThousand && !(x < 6 && y < 5)) c->map[y][x] = 'o';
        }
    }

    /* Gems: sparse, so the route is long; a third sit right under a boulder,
     * so getting them means dropping the rock (and not being beneath it). */
    int quota = 8 + level;
    int total = quota + 2 + level / 5, placed = 0;
    int underBoulder = total / 3;
    for (int guard = 0; guard < 6000 && placed < underBoulder; guard++) {
        int x = 1 + (int)Rnd(CAVE_COLS - 2), y = 3 + (int)Rnd(CAVE_ROWS - 5);
        if (c->map[y][x] == 'o' && c->map[y + 1][x] == ':') { c->map[y + 1][x] = '*'; placed++; }
    }
    for (int guard = 0; guard < 6000 && placed < total; guard++) {
        int x = 1 + (int)Rnd(CAVE_COLS - 2), y = 2 + (int)Rnd(CAVE_ROWS - 3);
        if (c->map[y][x] == ':' && !(x < 5 && y < 4)) { c->map[y][x] = '*'; placed++; }
    }
    c->quota = quota < placed ? quota : placed;

    /* Crawlers each get a sealed chamber of their own. */
    int crawlers = level >= 5 ? 1 + (level - 5) / 4 : 0;
    if (crawlers > 5) crawlers = 5;
    for (int i = 0; i < crawlers; i++) {
        int x = 10 + (int)Rnd(26), y = 6 + (int)Rnd(12);
        Carve(c->map, x, y, 4, 3);
        c->map[y + 1][x + 1] = (level >= 9 && (i & 1)) ? 'g' : 'f';
    }
    c->timeLimit = 999;
}

/* Returns the number of ticks the bot needed, or 0 if it failed. */
static int Solve(Cave *c) {
    static Game g;
    c->solution[0] = '\0';
    Game_Init(&g, c, 1, 0);
    g.state = ST_PLAY;
    int n = 0;
    while (n < 4500) {
        char m = Bot_Move(&g);
        Game_TickWithMove(&g, m);
        c->solution[n++] = m;
        if (!g.playerAlive) { c->solution[0] = '\0'; return 0; }
        if (g.state == ST_CLEAR) { c->solution[n] = '\0'; return n; }
    }
    c->solution[0] = '\0';
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "assets/caves";
    int levels = argc > 2 ? atoi(argv[2]) : 20;
    for (int level = 1; level <= levels; level++) {
        bool done = false;
        for (unsigned seed = 1; seed <= 600 && !done; seed++) {
            sState = (unsigned long long)level * 0x9E3779B97F4A7C15ull + seed * 0xD1B54A32D192ED03ull + 88172645463325252ull;
            for (int i = 0; i < 8; i++) Rnd(2);
            static Cave c;
            Generate(level, &c);
            int ticks = Solve(&c);
            if (ticks == 0) continue;
            double secs = ticks * TICK_SECONDS;
            int limit = (int)(secs * 2.4 + 30.0);
            limit = (limit + 4) / 5 * 5;
            c.timeLimit = limit;
            /* Prove it with the final time limit. */
            static Game g;
            Game_Init(&g, &c, 1, 0);
            g.state = ST_PLAY;
            int len = (int)strlen(c.solution);
            for (int i = 0; i < len; i++) Game_TickWithMove(&g, c.solution[i]);
            if (g.state != ST_CLEAR) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%02d.cave", dir, level);
            if (!Cave_Write(path, &c)) { fprintf(stderr, "cannot write %s\n", path); return 1; }
            printf("level %2d  %-14s seed %3u  gems %2d  %4d ticks (%3.0fs of %ds)\n", level, c.name, seed, c.quota, ticks, secs, limit);
            done = true;
        }
        if (!done) { fprintf(stderr, "level %d: no solvable cave found\n", level); return 1; }
    }
    return 0;
}
