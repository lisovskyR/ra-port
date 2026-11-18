#pragma once

#include <windows.h>

bool MacSDL_SetMode(int width, int height);
/*
 * Page height the game should use for a page `width` wide: taller than `height`
 * on displays squarer than 640x400 (iOS), so missions can fill the screen.
 */
int MacSDL_PreferredScreenHeight(int width, int height);
/*
 * Part of a page_width x page_height page to show. Frames of another size, such
 * as movies, are shown whole. With fill_screen the view fills the window edge to
 * edge; otherwise it is fitted with bars and kept clear of the Dynamic Island.
 */
void MacSDL_SetViewRect(int page_width, int page_height, int x, int y, int width, int height, bool fill_screen);
bool MacSDL_SetFullscreen(bool enabled);
bool MacSDL_GetFullscreen(void);
void MacSDL_Shutdown(void);
void MacSDL_SetPalette(PALETTEENTRY const *entries, int count);
void MacSDL_Present8(unsigned char const *pixels, int width, int height, int pitch);
void MacSDL_PumpEvents(void);
bool MacSDL_QuitRequested(void);
bool MacSDL_TouchCursorHidden(void);
int MacSDL_ConsumeMobilePointerDrag(int *x, int *y);
/*
 * Two-finger scrolling: view movement since the last call, in leptons (positive
 * x east, positive y south). Returns nonzero if there is any.
 */
int MacSDL_TakeMobilePan(int *dx, int *dy);
/*
 * The game's test of whether a touch at a game pixel needs the tap, drag and
 * long-press gestures. Elsewhere a touch presses the left button at once.
 */
void MacSDL_SetTouchGestureTest(int (*needs_gesture)(int x, int y));
