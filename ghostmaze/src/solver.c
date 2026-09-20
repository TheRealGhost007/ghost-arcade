#include "game.h"
#include <stdlib.h>
#include <string.h>

/* Breadth-first search over the whole game state. Everything about a tick is
 * deterministic, so the first state that reaches the exit gives a shortest
 * solution. The same search proves each shipped level can be finished. */

typedef struct {
    Game g;
    int parent;
    uint8_t act;
} Node;

static uint64_t Hash(const Game *g) {
    uint64_t h = 1469598103934665603ull;
#define MIX(v) do { h ^= (uint64_t)(uint8_t)(v); h *= 1099511628211ull; } while (0)
    MIX(g->gx); MIX(g->gy); MIX(g->possessed); MIX(g->possessCooldown); MIX(g->ticks % 6);
    for (int i = 0; i < g->hostCount; i++) { MIX(g->hosts[i].x); MIX(g->hosts[i].y); MIX(g->hosts[i].hasKey); }
    for (int i = 0; i < g->crateCount; i++) { MIX(g->crates[i].x); MIX(g->crates[i].y); }
    for (int i = 0; i < g->keyCount; i++) MIX(g->keys[i].taken);
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) MIX(g->tile[y][x]);
    for (int i = 0; i < g->priestCount; i++) { MIX(g->priests[i].x); MIX(g->priests[i].y); MIX(g->priests[i].dir); MIX(g->priests[i].toward); MIX(g->priests[i].wait); }
#undef MIX
    return h ? h : 1;
}

#define NODE_CAP 300000
#define SET_SIZE (1u << 21)

int Solver_Solve(int level, int maxTicks, char *out, int outCap) {
    static const char kChars[6] = {'.', 'U', 'D', 'L', 'R', 'P'};
    Node *nodes = (Node *)malloc(sizeof(Node) * NODE_CAP);
    uint64_t *seen = (uint64_t *)calloc(SET_SIZE, sizeof(uint64_t));
    if (!nodes || !seen) { free(nodes); free(seen); return -1; }

    int count = 0, head = 0, result = -1, goal = -1;
    memset(&nodes[0].g, 0, sizeof(Game));
    Game_LoadLevel(&nodes[0].g, level);
    nodes[0].g.phase = GS_PLAYING;
    nodes[0].g.state = ST_PLAY;
    nodes[0].parent = -1;
    nodes[0].act = 0;
    count = 1;
    { uint64_t h = Hash(&nodes[0].g); seen[h & (SET_SIZE - 1)] = h; }

    while (head < count && goal < 0) {
        if (nodes[head].g.ticks >= maxTicks) { head++; continue; }
        for (int a = 0; a < 6 && goal < 0; a++) {
            if (count >= NODE_CAP) break;
            Node *n = &nodes[count];
            n->g = nodes[head].g;
            Game_Tick(&n->g, (Action)a);
            if (n->g.state == ST_DYING) continue;
            if (n->g.state == ST_CLEAR) { n->parent = head; n->act = (uint8_t)a; goal = count++; break; }
            uint64_t h = Hash(&n->g);
            uint32_t slot = (uint32_t)(h & (SET_SIZE - 1));
            bool dup = false;
            while (seen[slot]) { if (seen[slot] == h) { dup = true; break; } slot = (slot + 1) & (SET_SIZE - 1); }
            if (dup) continue;
            seen[slot] = h;
            n->parent = head;
            n->act = (uint8_t)a;
            count++;
        }
        head++;
    }

    if (goal >= 0) {
        int len = 0;
        for (int i = goal; nodes[i].parent >= 0; i = nodes[i].parent) len++;
        result = len;
        if (out && outCap > len) {
            out[len] = '\0';
            int k = len;
            for (int i = goal; nodes[i].parent >= 0; i = nodes[i].parent) out[--k] = kChars[nodes[i].act];
        }
    }
    free(nodes);
    free(seen);
    return result;
}
