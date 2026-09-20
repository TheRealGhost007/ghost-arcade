#ifndef HOWTO_H
#define HOWTO_H

#include <stdbool.h>

/* The "How to play" card every game shows on first launch and from its menu:
 * the goal in a sentence or two, the controls, and one tip. Content lives here
 * (one place to edit); the drawing is Ui_DrawHowTo. No raylib. */
#define HOWTO_MAX_CONTROLS 9

typedef struct {
    const char *slug;
    const char *name;
    const char *goal;
    const char *controls[HOWTO_MAX_CONTROLS][2]; /* {keys, what they do}; NULL-terminated by a NULL keys */
    const char *tip;
} HowTo;

const HowTo *HowTo_ForSlug(const char *slug);
int HowTo_Count(void);
const HowTo *HowTo_At(int i);

/* Shown automatically once per game; these remember that it was. */
bool HowTo_Seen(const char *slug);
void HowTo_MarkSeen(const char *slug);

#endif
