#ifndef IOS_WINDOW_SCENE_H
#define IOS_WINDOW_SCENE_H

/*
 * Apps built with the iOS 27 SDK must adopt the UIScene lifecycle, which SDL2
 * does not. The iOS layer provides a scene delegate and this hook, called by
 * the runtime right after it creates its SDL window, to put that window into
 * the connected window scene. Plain C so the Objective-C layer can include it.
 */

struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Attaches the window's UIWindow to the app's window scene, waiting briefly
 * for the scene to connect if needed. Returns nonzero on success. Main thread.
 */
int RedAlertIOS_AttachWindowToScene(struct SDL_Window *window);

/*
 * Size, in points and in landscape (width >= height), of the display showing
 * the app's scene. Returns nonzero when known. Main thread.
 */
int RedAlertIOS_GetLandscapeDisplaySize(int *width, int *height);

/*
 * Called when missions fill the whole screen. On phones this allows only the
 * landscape side that keeps the Dynamic Island over the map (left edge) rather
 * than the build sidebar (right edge). Main thread, before the window exists.
 */
void RedAlertIOS_UseFullScreenMissions(void);

/* SDL_HINT_ORIENTATIONS value matching the allowed landscape sides. */
char const *RedAlertIOS_OrientationHint(void);

#ifdef __cplusplus
}
#endif

#endif
