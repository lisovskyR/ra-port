#import <UIKit/UIKit.h>

#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_syswm.h"

#include "mobile_safe_area.h"

/*
 * Safe-area queries for the game window. The game draws one fixed-size frame,
 * so the C++ runtime only needs the obstruction insets and the interface
 * orientation to place it. This matters most on squarer screens such as the
 * iPhone Duo's folded and unfolded displays, where the frame spans the full
 * width and the Dynamic Island would otherwise cover the sidebar.
 */

static int MobileOrientationFromUIKit(UIInterfaceOrientation orientation)
{
	switch (orientation) {
		case UIInterfaceOrientationPortrait: return MOBILE_ORIENTATION_PORTRAIT;
		case UIInterfaceOrientationPortraitUpsideDown: return MOBILE_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
		case UIInterfaceOrientationLandscapeLeft: return MOBILE_ORIENTATION_LANDSCAPE_LEFT;
		case UIInterfaceOrientationLandscapeRight: return MOBILE_ORIENTATION_LANDSCAPE_RIGHT;
		default: return MOBILE_ORIENTATION_UNKNOWN;
	}
}

static UIInterfaceOrientation RedAlertSceneOrientation(UIWindow *window)
{
	UIWindowScene *scene = window.windowScene;
	if (!scene) {
		return UIInterfaceOrientationUnknown;
	}
	if (@available(iOS 16.0, *)) {
		return scene.effectiveGeometry.interfaceOrientation;
	}
	return scene.interfaceOrientation;
}

static void RedAlertLogLayoutIfChanged(UIWindow *window, MobileSafeArea const *area)
{
	static int enabled = -1;
	static MobileSafeArea last;
	static CGSize last_size;
	if (enabled < 0) {
		enabled = getenv("RA_IOS_LAYOUT_DEBUG") != NULL;
		memset(&last, 0, sizeof(last));
		last.orientation = -1;
		last_size = CGSizeZero;
	}
	if (!enabled) {
		return;
	}

	CGSize size = window.bounds.size;
	if (memcmp(&last, area, sizeof(last)) == 0 && CGSizeEqualToSize(size, last_size)) {
		return;
	}
	last = *area;
	last_size = size;

	UIScreen *screen = window.windowScene ? window.windowScene.screen : nil;
	NSLog(@"RA_IOS_LAYOUT: window=%.0fx%.0f screen=%.0fx%.0f@%.0f safe(l=%.1f t=%.1f r=%.1f b=%.1f) orientation=%d",
		size.width, size.height,
		screen ? screen.bounds.size.width : 0.0, screen ? screen.bounds.size.height : 0.0, screen ? screen.scale : 0.0,
		area->left, area->top, area->right, area->bottom, area->orientation);
	for (UIWindow *scene_window in window.windowScene.windows) {
		UIView *root = scene_window.rootViewController.view;
		NSLog(@"RA_IOS_LAYOUT:   window %@ %p level=%.0f hidden=%d alpha=%.2f key=%d frame=%@ root=%@ rootFrame=%@ scale=%.1f",
			NSStringFromClass([scene_window class]), scene_window, scene_window.windowLevel, scene_window.hidden, scene_window.alpha,
			scene_window.isKeyWindow, NSStringFromCGRect(scene_window.frame),
			root ? NSStringFromClass([root class]) : @"-", root ? NSStringFromCGRect(root.frame) : @"-",
			root ? root.contentScaleFactor : 0.0);
	}
}

int MobileSafeArea_Query(SDL_Window *window, MobileSafeArea *area)
{
	if (!window || !area) {
		return 0;
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT || !info.info.uikit.window) {
		return 0;
	}

	UIWindow *uiwindow = info.info.uikit.window;
	UIEdgeInsets insets = uiwindow.safeAreaInsets;
	area->left = (float)insets.left;
	area->top = (float)insets.top;
	area->right = (float)insets.right;
	area->bottom = (float)insets.bottom;
	area->orientation = MobileOrientationFromUIKit(RedAlertSceneOrientation(uiwindow));

	RedAlertLogLayoutIfChanged(uiwindow, area);
	return 1;
}
