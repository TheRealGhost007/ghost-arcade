#include "pixtext.h"
#include <stdio.h>

static Font sFont;
static bool sLoaded = false;

static float Spacing(float size) { return size * 0.125f; } /* one font pixel */

void Pix_Load(const char *assetsDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/fonts/PressStart2P-Regular.ttf", assetsDir);
    if (!FileExists(path)) return;
    sFont = LoadFontEx(path, 64, NULL, 0);
    if (sFont.texture.id == 0) return;
    SetTextureFilter(sFont.texture, TEXTURE_FILTER_POINT);
    sLoaded = true;
}

void Pix_Unload(void) {
    if (sLoaded) UnloadFont(sFont);
    sLoaded = false;
}

void Pix_Text(const char *text, int x, int y, int size, Color color) {
    Font font = sLoaded ? sFont : GetFontDefault();
    DrawTextEx(font, text, (Vector2){(float)x, (float)y}, (float)size, Spacing((float)size), color);
}

int Pix_Measure(const char *text, int size) {
    Font font = sLoaded ? sFont : GetFontDefault();
    return (int)MeasureTextEx(font, text, (float)size, Spacing((float)size)).x;
}

void Pix_TextCentered(const char *text, int size, int centerX, int y, Color color) {
    Pix_Text(text, centerX - Pix_Measure(text, size) / 2, y, size, color);
}

void Pix_TextRight(const char *text, int size, int rightX, int y, Color color) {
    Pix_Text(text, rightX - Pix_Measure(text, size), y, size, color);
}
