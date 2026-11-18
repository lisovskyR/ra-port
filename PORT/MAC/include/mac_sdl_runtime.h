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
