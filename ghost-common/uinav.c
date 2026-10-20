#include "uinav.h"

/* A copy of Ghost Launcher's src/nav.c (see uinav.h). */

void Nav_Init(Nav *n, int root) {
    n->screens[0] = root;
    n->depth = 1;
}

int Nav_Top(const Nav *n) { return n->screens[n->depth - 1]; }

int Nav_Depth(const Nav *n) { return n->depth; }

int Nav_Under(const Nav *n) { return n->depth > 1 ? n->screens[n->depth - 2] : n->screens[0]; }

bool Nav_Contains(const Nav *n, int screen) {
    for (int i = 0; i < n->depth; i++) if (n->screens[i] == screen) return true;
    return false;
}

bool Nav_Push(Nav *n, int screen) {
    for (int i = 0; i < n->depth; i++) {
        if (n->screens[i] == screen) {
            n->depth = i + 1;
            return true;
        }
    }
    if (n->depth >= NAV_DEPTH) return false;
    n->screens[n->depth++] = screen;
    return true;
}

bool Nav_Back(Nav *n) {
    if (n->depth <= 1) return false;
    n->depth--;
    return true;
}

void Nav_Home(Nav *n) { n->depth = 1; }
