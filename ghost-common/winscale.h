#ifndef WINSCALE_H
#define WINSCALE_H

#include <stdbool.h>

/* Every game draws at a fixed logical size into an off-screen texture; this
 * module blows that up to whatever the window is (integer scale when it fits
 * cleanly, smooth otherwise, always letterboxed) and owns F11 / Alt+Enter
 * fullscreen. Mouse positions are remapped so games keep reading logical
 * coordinates. Use Win_BeginFrame / Win_EndFrame where a game would call
 * BeginDrawing / EndDrawing. */
void Win_Init(int logicalW, int logicalH); /* after InitWindow */
void Win_Shutdown(void);                   /* before CloseWindow */
void Win_Update(void);                     /* once per frame, top of the loop */
void Win_BeginFrame(void);
void Win_EndFrame(void);
/* Drawn on top of everything, inside the logical frame (the F2 panel). */
void Win_SetOverlay(void (*draw)(void));
bool Win_IsFullscreen(void);
int Win_LogicalWidth(void);

#endif
