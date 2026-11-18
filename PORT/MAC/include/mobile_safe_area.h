#ifndef MOBILE_SAFE_AREA_H
#define MOBILE_SAFE_AREA_H

/*
 * Safe-area information for the game window on mobile platforms. Shared by the
 * C++ runtime and the Objective-C iOS layer, so this header stays plain C.
 */

struct SDL_Window;

/* Interface orientations, with the meaning of UIInterfaceOrientation. */
enum {
	MOBILE_ORIENTATION_UNKNOWN = 0,
	MOBILE_ORIENTATION_PORTRAIT = 1,
	MOBILE_ORIENTATION_PORTRAIT_UPSIDE_DOWN = 2,
	MOBILE_ORIENTATION_LANDSCAPE_LEFT = 3,
	MOBILE_ORIENTATION_LANDSCAPE_RIGHT = 4
};

typedef struct MobileSafeArea {
	/* Insets in window points, the units of SDL_GetWindowSize. */
	float left;
	float top;
	float right;
	float bottom;
	int orientation;
} MobileSafeArea;

/*
 * UIKit reports equal left and right safe-area insets in landscape on phones
 * with a sensor housing (Dynamic Island), even though the housing sits only on
 * the edge that is the top of the portrait-native display. Keep the inset on
 * that edge, so the game frame is not shrunk on both sides for an obstruction
 * on one side.
 *
 * Landscape-left has the top of the device on the right; landscape-right has
 * it on the left.
 */
static inline MobileSafeArea MobileSafeArea_ResolveSensorHousing(MobileSafeArea area)
{
	float difference = area.left - area.right;
	if (difference < 0.0f) {
		difference = -difference;
	}
	if (area.left > 0.0f && difference < 0.5f) {
		if (area.orientation == MOBILE_ORIENTATION_LANDSCAPE_LEFT) {
			area.left = 0.0f;
		} else if (area.orientation == MOBILE_ORIENTATION_LANDSCAPE_RIGHT) {
			area.right = 0.0f;
		}
	}
	return area;
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fills *area for the window and returns nonzero when the platform reports a
 * safe area. Implemented by the iOS layer; must be called on the main thread.
 */
int MobileSafeArea_Query(struct SDL_Window *window, MobileSafeArea *area);

#ifdef __cplusplus
}
#endif

#endif
