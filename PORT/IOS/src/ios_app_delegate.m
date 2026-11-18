#import <UIKit/UIKit.h>
#import <objc/runtime.h>

#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#include "SDL_syswm.h"

#import "SDL_uikitappdelegate.h"

#include "ios_window_scene.h"

@interface RedAlertIOSDelegate : SDLUIKitDelegate
@end

@interface SDLUIKitDelegate (RedAlertPrivate)
- (void)postFinishLaunch;
@end

/*
 * The app's single window scene. Apps built with the iOS 27 SDK must adopt the
 * UIScene lifecycle; SDL2 still creates its UIWindows without a scene, so the
 * runtime attaches them to this scene (see RedAlertIOS_AttachWindowToScene).
 */
static UIWindowScene *RedAlertConnectedScene = nil;

/*
 * Landscape sides the app allows. Full-screen missions on a phone narrow this
 * to landscape-right, which puts the Dynamic Island (the top of the device)
 * on the left, over the map instead of the build sidebar.
 */
static UIInterfaceOrientationMask RedAlertLandscapeMask = UIInterfaceOrientationMaskLandscape;

static NSString *RedAlertGetAppDelegateClassName(id self, SEL selector)
{
	return @"RedAlertIOSDelegate";
}

static void RedAlertOrientationDebug(char const *message)
{
	if (getenv("RA_IOS_ORIENTATION_DEBUG")) {
		fprintf(stderr, "RA_IOS_ORIENTATION: %s\n", message);
		NSLog(@"RA_IOS_ORIENTATION: %s", message);
	}
}

static UIWindowScene *RedAlertFindWindowScene(void)
{
	if (RedAlertConnectedScene) {
		return RedAlertConnectedScene;
	}
	for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
		if ([scene isKindOfClass:[UIWindowScene class]] && scene.activationState != UISceneActivationStateUnattached) {
			return (UIWindowScene *)scene;
		}
	}
	return nil;
}

static void RedAlertRequestLandscapeGeometry(UIWindowScene *window_scene)
{
	if (@available(iOS 16.0, *)) {
		if (!window_scene) {
			window_scene = RedAlertFindWindowScene();
		}
		if (!window_scene) {
			RedAlertOrientationDebug("no window scene for landscape request");
			return;
		}

		UIWindowSceneGeometryPreferencesIOS *preferences =
			[[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:RedAlertLandscapeMask];
		[window_scene requestGeometryUpdateWithPreferences:preferences errorHandler:^(NSError *error) {
			(void)error;
			RedAlertOrientationDebug("landscape geometry request failed");
		}];
		[preferences release];

		for (UIWindow *scene_window in window_scene.windows) {
			[scene_window.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
		}
	} else {
		[UIViewController attemptRotationToDeviceOrientation];
	}
}

/* The game can start setting up video before UIKit connects the first scene. */
static UIWindowScene *RedAlertWaitForWindowScene(void)
{
	CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 5.0;
	while (!RedAlertFindWindowScene() && CFAbsoluteTimeGetCurrent() < deadline) {
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, TRUE);
	}
	return RedAlertFindWindowScene();
}

int RedAlertIOS_GetLandscapeDisplaySize(int *width, int *height)
{
	UIWindowScene *scene = RedAlertWaitForWindowScene();
	if (!scene || !width || !height) {
		return 0;
	}
	CGSize size = scene.screen.bounds.size;
	CGFloat long_side = size.width > size.height ? size.width : size.height;
	CGFloat short_side = size.width > size.height ? size.height : size.width;
	*width = (int)(long_side + 0.5);
	*height = (int)(short_side + 0.5);
	return *width > 0 && *height > 0;
}

void RedAlertIOS_UseFullScreenMissions(void)
{
	if ([UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPhone) {
		return;
	}
	RedAlertLandscapeMask = UIInterfaceOrientationMaskLandscapeRight;
	RedAlertRequestLandscapeGeometry(nil);
}

char const *RedAlertIOS_OrientationHint(void)
{
	return RedAlertLandscapeMask == UIInterfaceOrientationMaskLandscapeRight ? "LandscapeRight" : "LandscapeLeft LandscapeRight";
}

int RedAlertIOS_AttachWindowToScene(SDL_Window *window)
{
	if (!window) {
		return 0;
	}

	UIWindowScene *scene = RedAlertWaitForWindowScene();
	if (!scene) {
		NSLog(@"RA_IOS: no window scene connected; the game window cannot be shown");
		return 0;
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT || !info.info.uikit.window) {
		return 0;
	}

	UIWindow *uiwindow = info.info.uikit.window;
	if (uiwindow.windowScene != scene) {
		uiwindow.windowScene = scene;
	}
	/* Fill the scene: it is the folded or unfolded display on an iPhone Duo. */
	uiwindow.frame = scene.coordinateSpace.bounds;
	[uiwindow makeKeyAndVisible];
	RedAlertRequestLandscapeGeometry(scene);
	return 1;
}

@interface RedAlertSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation RedAlertSceneDelegate

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions
{
	if (![scene isKindOfClass:[UIWindowScene class]]) {
		return;
	}
	RedAlertOrientationDebug("scene connected");
	[RedAlertConnectedScene release];
	RedAlertConnectedScene = [(UIWindowScene *)scene retain];
	RedAlertRequestLandscapeGeometry(RedAlertConnectedScene);
}

- (void)sceneDidDisconnect:(UIScene *)scene
{
	if (scene == RedAlertConnectedScene) {
		[RedAlertConnectedScene release];
		RedAlertConnectedScene = nil;
	}
}

- (void)sceneDidBecomeActive:(UIScene *)scene
{
	RedAlertOrientationDebug("scene became active");
	if ([scene isKindOfClass:[UIWindowScene class]]) {
		RedAlertRequestLandscapeGeometry((UIWindowScene *)scene);
	}
}

@end

@implementation RedAlertIOSDelegate

+ (void)load
{
	Class meta_class = object_getClass([SDLUIKitDelegate class]);
	Method method = class_getClassMethod([SDLUIKitDelegate class], @selector(getAppDelegateClassName));
	char const *types = method ? method_getTypeEncoding(method) : "@@:";
	class_replaceMethod(meta_class, @selector(getAppDelegateClassName), (IMP)RedAlertGetAppDelegateClassName, types);
	RedAlertOrientationDebug("installed delegate class override");
}

- (UIInterfaceOrientationMask)application:(UIApplication *)application supportedInterfaceOrientationsForWindow:(UIWindow *)window
{
	return RedAlertLandscapeMask;
}

- (UISceneConfiguration *)application:(UIApplication *)application
	configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession
	options:(UISceneConnectionOptions *)options
{
	UISceneConfiguration *configuration =
		[UISceneConfiguration configurationWithName:@"Default Configuration" sessionRole:connectingSceneSession.role];
	configuration.delegateClass = [RedAlertSceneDelegate class];
	return configuration;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	RedAlertOrientationDebug("RedAlertIOSDelegate didFinishLaunching");
	BOOL launched = [super application:application didFinishLaunchingWithOptions:launchOptions];
	RedAlertRequestLandscapeGeometry(nil);
	return launched;
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
	RedAlertOrientationDebug("RedAlertIOSDelegate didBecomeActive");
	RedAlertRequestLandscapeGeometry(nil);
}

- (void)postFinishLaunch
{
	RedAlertOrientationDebug("RedAlertIOSDelegate postFinishLaunch");
	RedAlertRequestLandscapeGeometry(nil);
	[super postFinishLaunch];
}

@end
