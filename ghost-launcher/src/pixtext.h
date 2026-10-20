#ifndef PIXTEXT_H
#define PIXTEXT_H

#include "raylib.h"

/* Press Start 2P, the launcher's only font. It is an 8x8 pixel design, so
 * it is drawn at 8, 16 or 24 px and nothing in between. */
void Pix_Load(const char *assetsDir);
void Pix_Unload(void);
void Pix_Text(const char *text, int x, int y, int size, Color color);
int Pix_Measure(const char *text, int size);
void Pix_TextCentered(const char *text, int size, int centerX, int y, Color color);
void Pix_TextRight(const char *text, int size, int rightX, int y, Color color);

#endif
