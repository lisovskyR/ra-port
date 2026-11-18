#include "mac_sdl.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mac_sdl_runtime.h"
#include "mobile_key_message.h"
#include "mobile_pan.h"
#include "mobile_touch_gesture.h"
#include "mobile_zoom.h"
#include "ra_aspect_viewport.h"
#include <mmsystem.h>

#if defined(RA_IOS)
#include "ios_window_scene.h"
#include "mobile_safe_area.h"
#endif

static SDL_Window *MacWindow = 0;
static SDL_Renderer *MacRenderer = 0;
static SDL_Texture *MacTexture = 0;
static uint32_t *MacFrame = 0;
static int MacFramePixels = 0;
static int MacWidth = 0;
static int MacHeight = 0;
static bool MacSDLReady = false;
static bool MacQuitRequested = false;
static bool MacFullscreen = false;
static bool MacPresenting = false;
static SDL_threadID MacMainThread = 0;
static uint32_t MacPalette[256];
static MSG MacMessageQueue[512];
static int MacMessageHead = 0;
static int MacMessageTail = 0;
static unsigned char MacKeyState[256];
static unsigned char MacToggleState[256];
static POINT MacMousePoint = {0, 0};
static bool MacUnmodifiedKeyDispatch = false;
/* Size of the frame currently held in MacFrame/MacTexture, and of the texture. */
static int MacFrameWidth = 0;
static int MacFrameHeight = 0;
static int MacTextureWidth = 0;
static int MacTextureHeight = 0;

/*
 * Part of the game page that is on screen (MacSDL_SetViewRect). On a page
 * taller than 640x400, menus use a centered 640x400 view and missions use the
 * whole page, filling the window.
 */
struct MacViewRect {
	int page_w;
	int page_h;
	SDL_Rect rect;
	bool fill;
	bool valid;
};
static MacViewRect MacView;

/* Largest stretch, in percent, accepted to fill the window with a mission page. */
static int const MAC_FILL_MAX_STRETCH_PERCENT = 3;

/* Where a frame goes on screen. Drawing uses `output`; input uses `window`. */
struct MacLayout {
	SDL_Rect source;           /* region of the frame shown, in frame pixels */
	RAAspectViewport window;   /* destination in window points */
	RAAspectViewport output;   /* destination in renderer pixels */
	bool page;                 /* the frame is the game page, which pinch zoom applies to */
};
static MacLayout MacPresentedLayout;

#if defined(RA_MOBILE_TOUCH)
extern bool InMovie;
#endif

#if defined(RA_MOBILE_TOUCH)
static MobileTouchGesture MobileTouch;
static MobilePanState MobilePan;
static MobileZoomState MobileZoom;
static MobilePinchState MobilePinch;
static bool MobileTouchCursorHidden = true;
static bool MobilePointerDragCandidate = false;
static int MobilePointerDragStartX = 0;
static int MobilePointerDragStartY = 0;
#endif

LRESULT FAR PASCAL _export Windows_Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

static uint32_t mac_argb(unsigned char red, unsigned char green, unsigned char blue)
{
	return 0xFF000000U | ((uint32_t)red << 16) | ((uint32_t)green << 8) | (uint32_t)blue;
}

static void mac_default_palette(void)
{
	for (int index = 0; index < 256; ++index) {
		MacPalette[index] = mac_argb((unsigned char)index, (unsigned char)index, (unsigned char)index);
	}
}

static DWORD mac_now_ms(void)
{
	return (DWORD)SDL_GetTicks();
}

static bool mac_fullscreen_env_requested(void)
{
	char const *value = getenv("RA_FULLSCREEN");
	if (!value || !value[0]) {
		return false;
	}
	return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0 &&
		strcmp(value, "no") != 0 && strcmp(value, "NO") != 0;
}

static LPARAM mac_pack_xy(int x, int y)
{
	return (LPARAM)(((y & 0xFFFF) << 16) | (x & 0xFFFF));
}

/*
 * Window edges the game frame must keep clear of, scaled from window points by
 * `scale`. On iOS this is the sensor-housing (Dynamic Island) edge reported by
 * UIKit. It matters on squarer screens such as both iPhone Duo displays, where
 * the 640x400 frame spans the full width. The home indicator is auto-hidden, so
 * its bottom inset stays usable.
 */
static RAViewportInsets mac_layout_insets(float scale)
{
	RAViewportInsets insets = {0, 0, 0, 0};
#if defined(RA_IOS)
	MobileSafeArea area;
	if (MacWindow && MobileSafeArea_Query(MacWindow, &area)) {
		area = MobileSafeArea_ResolveSensorHousing(area);
		insets.left = (int)(area.left * scale + 0.5f);
		insets.top = (int)(area.top * scale + 0.5f);
		insets.right = (int)(area.right * scale + 0.5f);
	}
#else
	(void)scale;
#endif
	return insets;
}

/*
 * Layout of a frame_w x frame_h frame: which part of it is shown and where.
 * Game pages show the current view rect; other frames (movies) are shown whole.
 * A mission view fills the window; everything else is fitted with bars and kept
 * clear of the safe-area insets.
 */
static MacLayout mac_layout_for(int frame_w, int frame_h)
{
	MacLayout layout;
	layout.source.x = 0;
	layout.source.y = 0;
	layout.source.w = frame_w;
	layout.source.h = frame_h;
	bool fill = false;
	layout.page = frame_w == MacWidth && frame_h == MacHeight;
	if (MacView.valid && frame_w == MacView.page_w && frame_h == MacView.page_h) {
		layout.source = MacView.rect;
		fill = MacView.fill;
	}

	int window_w = MacWidth;
	int window_h = MacHeight;
	if (MacWindow) {
		SDL_GetWindowSize(MacWindow, &window_w, &window_h);
	}
	int output_w = window_w;
	int output_h = window_h;
	if (MacRenderer) {
		SDL_GetRendererOutputSize(MacRenderer, &output_w, &output_h);
	}
	float scale = window_w > 0 ? (float)output_w / (float)window_w : 1.0f;

	if (fill) {
		layout.window = RA_CalculateFillViewport(layout.source.w, layout.source.h, window_w, window_h, MAC_FILL_MAX_STRETCH_PERCENT);
		layout.output = RA_CalculateFillViewport(layout.source.w, layout.source.h, output_w, output_h, MAC_FILL_MAX_STRETCH_PERCENT);
	} else {
		layout.window = RA_CalculateInsetAspectViewport(layout.source.w, layout.source.h, window_w, window_h, mac_layout_insets(1.0f));
		layout.output = RA_CalculateInsetAspectViewport(layout.source.w, layout.source.h, output_w, output_h, mac_layout_insets(scale));
	}
	return layout;
}

/* Layout of the frame on screen, or of the current mode before the first frame. */
static MacLayout mac_current_layout(void)
{
	if (MacFrameWidth > 0 && MacFrameHeight > 0) {
		return mac_layout_for(MacFrameWidth, MacFrameHeight);
	}
	return mac_layout_for(MacWidth, MacHeight);
}

/* Maps a point in window points to game coordinates; returns whether it was inside the frame. */
static bool mac_window_to_logical(MacLayout const &layout, int screen_x, int screen_y, int *logical_x, int *logical_y)
{
	return RA_MapViewportPoint(layout.window, layout.source.w, layout.source.h, screen_x, screen_y, logical_x, logical_y) != 0;
}

static bool mac_to_logical_point(int *x, int *y)
{
	if (!x || !y || !MacWindow || MacWidth <= 0 || MacHeight <= 0) {
		return false;
	}
	return mac_window_to_logical(mac_current_layout(), *x, *y, x, y);
}

int MacSDL_PreferredScreenHeight(int width, int height)
{
#if defined(RA_IOS)
	int display_w = 0;
	int display_h = 0;
	if (height == RA_PAGE_BASE_HEIGHT && RedAlertIOS_GetLandscapeDisplaySize(&display_w, &display_h)) {
		int preferred = RA_MissionPageHeight(width, display_w, display_h);
		if (preferred > height) {
			RedAlertIOS_UseFullScreenMissions();
		}
		return preferred;
	}
#else
	(void)width;
#endif
	return height;
}

void MacSDL_SetViewRect(int page_width, int page_height, int x, int y, int width, int height, bool fill_screen)
{
	if (page_width <= 0 || page_height <= 0 || width <= 0 || height <= 0 ||
			x < 0 || y < 0 || x + width > page_width || y + height > page_height) {
		MacView.valid = false;
		return;
	}
#if defined(RA_MOBILE_TOUCH)
	if (!MacView.valid || MacView.rect.x != x || MacView.rect.y != y || MacView.rect.w != width || MacView.rect.h != height) {
		MobileZoom_Init(&MobileZoom);
	}
#endif
	MacView.page_w = page_width;
	MacView.page_h = page_height;
	MacView.rect.x = x;
	MacView.rect.y = y;
	MacView.rect.w = width;
	MacView.rect.h = height;
	MacView.fill = fill_screen;
	MacView.valid = true;
}

static BOOL mac_queue_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	int next = (MacMessageTail + 1) % (int)(sizeof(MacMessageQueue) / sizeof(MacMessageQueue[0]));
	if (next == MacMessageHead) {
		return FALSE;
	}
	MSG *msg = &MacMessageQueue[MacMessageTail];
	memset(msg, 0, sizeof(*msg));
	msg->hwnd = hwnd;
	msg->message = message;
	msg->wParam = wparam;
	msg->lParam = lparam;
	msg->time = mac_now_ms();
	msg->pt = MacMousePoint;
	MacMessageTail = next;
	return TRUE;
}

static void mac_update_mouse_position(int x, int y)
{
#if defined(RA_MOBILE_TOUCH)
	MobileTouchCursorHidden = false;
#endif
	MacMousePoint.x = x;
	MacMousePoint.y = y;
}

static void mac_queue_mouse_button_with_cursor(int vk, bool down, int x, int y, bool update_cursor)
{
	MacMousePoint.x = x;
	MacMousePoint.y = y;
#if defined(RA_MOBILE_TOUCH)
	if (vk == VK_LBUTTON && down) {
		MobilePointerDragCandidate = true;
		MobilePointerDragStartX = x;
		MobilePointerDragStartY = y;
	} else if (vk != VK_LBUTTON && down) {
		MobilePointerDragCandidate = false;
	}
#endif
	if (update_cursor) {
#if defined(RA_MOBILE_TOUCH)
		MobileTouchCursorHidden = false;
#endif
	}
	MacKeyState[vk & 0xFF] = down ? 1 : 0;
	UINT message = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
	if (vk == VK_RBUTTON) {
		message = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
	} else if (vk == VK_MBUTTON) {
		message = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
	}
	mac_queue_message((HWND)(intptr_t)1, message, (WPARAM)vk, mac_pack_xy(x, y));
#if defined(RA_MOBILE_TOUCH)
	if (!update_cursor && !down) {
		MobileTouchCursorHidden = true;
	}
#endif
}

static void mac_queue_mouse_button(int vk, bool down, int x, int y)
{
	mac_queue_mouse_button_with_cursor(vk, down, x, y, true);
}

static void mac_queue_key_pulse(int vk)
{
	MacKeyState[vk & 0xFF] = 1;
	mac_queue_message((HWND)(intptr_t)1, WM_KEYDOWN, (WPARAM)vk, 0);
	MacKeyState[vk & 0xFF] = 0;
	mac_queue_message((HWND)(intptr_t)1, WM_KEYUP, (WPARAM)vk, 0);
}

static void mac_queue_unmodified_key_pulse(int vk)
{
	MacKeyState[vk & 0xFF] = 1;
	mac_queue_message((HWND)(intptr_t)1, WM_KEYDOWN, (WPARAM)vk, MOBILE_KEY_UNMODIFIED_LPARAM);
	MacKeyState[vk & 0xFF] = 0;
	mac_queue_message((HWND)(intptr_t)1, WM_KEYUP, (WPARAM)vk, MOBILE_KEY_UNMODIFIED_LPARAM);
}

static int mac_pop_message(MSG *msg, bool remove)
{
	if (MacMessageHead == MacMessageTail) {
		return FALSE;
	}
	if (msg) {
		*msg = MacMessageQueue[MacMessageHead];
	}
	if (remove) {
		MacMessageHead = (MacMessageHead + 1) % (int)(sizeof(MacMessageQueue) / sizeof(MacMessageQueue[0]));
	}
	return TRUE;
}

static bool mac_has_pending_messages(void)
{
	return MacMessageHead != MacMessageTail;
}

#if defined(RA_MOBILE_TOUCH)
static void mobile_idle_delay(bool allow_idle_delay, bool saw_sdl_event)
{
	if (allow_idle_delay && !saw_sdl_event && !mac_has_pending_messages()) {
		SDL_Delay(1);
	}
}
#endif

static int mac_vk_from_sdl(SDL_Keycode key)
{
	if (key >= SDLK_a && key <= SDLK_z) {
		return 'A' + (int)(key - SDLK_a);
	}
	if (key >= SDLK_0 && key <= SDLK_9) {
		return '0' + (int)(key - SDLK_0);
	}
	if (key >= SDLK_F1 && key <= SDLK_F12) {
		return VK_F1 + (int)(key - SDLK_F1);
	}

	switch (key) {
		case SDLK_RETURN: return VK_RETURN;
		case SDLK_KP_ENTER: return VK_RETURN;
		case SDLK_ESCAPE: return VK_ESCAPE;
		case SDLK_SPACE: return VK_SPACE;
		case SDLK_BACKSPACE: return VK_BACK;
		case SDLK_TAB: return VK_TAB;
		case SDLK_INSERT: return VK_INSERT;
		case SDLK_DELETE: return VK_DELETE;
		case SDLK_HOME: return VK_HOME;
		case SDLK_END: return VK_END;
		case SDLK_PAGEUP: return VK_PRIOR;
		case SDLK_PAGEDOWN: return VK_NEXT;
		case SDLK_LEFT: return VK_LEFT;
		case SDLK_RIGHT: return VK_RIGHT;
		case SDLK_UP: return VK_UP;
		case SDLK_DOWN: return VK_DOWN;
		case SDLK_PAUSE: return VK_PAUSE;
		case SDLK_CAPSLOCK: return VK_CAPITAL;
		case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
		case SDLK_KP_0: return VK_NUMPAD0;
		case SDLK_KP_1: return VK_NUMPAD1;
		case SDLK_KP_2: return VK_NUMPAD2;
		case SDLK_KP_3: return VK_NUMPAD3;
		case SDLK_KP_4: return VK_NUMPAD4;
		case SDLK_KP_5: return VK_NUMPAD5;
		case SDLK_KP_6: return VK_NUMPAD6;
		case SDLK_KP_7: return VK_NUMPAD7;
		case SDLK_KP_8: return VK_NUMPAD8;
		case SDLK_KP_9: return VK_NUMPAD9;
		case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
		case SDLK_KP_PLUS: return VK_ADD;
		case SDLK_KP_MINUS: return VK_SUBTRACT;
		case SDLK_KP_PERIOD: return VK_DECIMAL;
		case SDLK_KP_DIVIDE: return VK_DIVIDE;
		case SDLK_SEMICOLON: return VK_NONE_BA;
		case SDLK_EQUALS: return VK_NONE_BB;
		case SDLK_COMMA: return VK_NONE_BC;
		case SDLK_MINUS: return VK_NONE_BD;
		case SDLK_PERIOD: return VK_NONE_BE;
		case SDLK_SLASH: return VK_NONE_BF;
		case SDLK_BACKQUOTE: return VK_NONE_C0;
		case SDLK_LEFTBRACKET: return VK_NONE_DB;
		case SDLK_BACKSLASH: return VK_NONE_DC;
		case SDLK_RIGHTBRACKET: return VK_NONE_DD;
		case SDLK_QUOTE: return VK_NONE_DE;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT: return VK_SHIFT;
		case SDLK_LCTRL:
		case SDLK_RCTRL: return VK_CONTROL;
		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_LGUI:
		case SDLK_RGUI: return VK_MENU;
		default: break;
	}
	return 0;
}

static void mac_update_modifier_state(void)
{
	SDL_Keymod mods = SDL_GetModState();
	MacKeyState[VK_SHIFT] = (mods & KMOD_SHIFT) ? 1 : 0;
	MacKeyState[VK_CONTROL] = (mods & KMOD_CTRL) ? 1 : 0;
	MacKeyState[VK_MENU] = (mods & KMOD_ALT) ? 1 : 0;
	MacToggleState[VK_CAPITAL] = (mods & KMOD_CAPS) ? 1 : 0;
	MacToggleState[VK_NUMLOCK] = (mods & KMOD_NUM) ? 1 : 0;
}

static bool mac_state_down(BYTE const *state, int key)
{
	if (state) {
		return (state[key & 0xFF] & 0x80) != 0;
	}
	return MacKeyState[key & 0xFF] != 0;
}

static bool mac_state_toggled(BYTE const *state, int key)
{
	if (state) {
		return (state[key & 0xFF] & 0x09) != 0;
	}
	return MacToggleState[key & 0xFF] != 0;
}

static int mac_write_ascii(LPWORD out, unsigned char ch)
{
	if (!out) {
		return 0;
	}
	*out = (WORD)ch;
	return 1;
}

static int mac_ascii_from_vk(UINT virt_key, BYTE const *state, LPWORD out)
{
	virt_key &= 0xFF;
	bool shift = mac_state_down(state, VK_SHIFT);
	bool caps = mac_state_toggled(state, VK_CAPITAL);

	if (virt_key >= 'A' && virt_key <= 'Z') {
		unsigned char base = (unsigned char)('a' + (virt_key - 'A'));
		if (shift ^ caps) {
			base = (unsigned char)('A' + (virt_key - 'A'));
		}
		return mac_write_ascii(out, base);
	}

	if (virt_key >= '0' && virt_key <= '9') {
		static const char shifted[] = ")!@#$%^&*(";
		unsigned char ch = shift ? (unsigned char)shifted[virt_key - '0'] : (unsigned char)virt_key;
		return mac_write_ascii(out, ch);
	}

	if (virt_key >= VK_NUMPAD0 && virt_key <= VK_NUMPAD9) {
		return mac_write_ascii(out, (unsigned char)('0' + (virt_key - VK_NUMPAD0)));
	}

	switch (virt_key) {
		case VK_SPACE: return mac_write_ascii(out, ' ');
		case VK_TAB: return mac_write_ascii(out, '\t');
		case VK_RETURN: return mac_write_ascii(out, '\r');
		case VK_BACK: return mac_write_ascii(out, '\b');
		case VK_MULTIPLY: return mac_write_ascii(out, '*');
		case VK_ADD: return mac_write_ascii(out, '+');
		case VK_SEPARATOR: return mac_write_ascii(out, ',');
		case VK_SUBTRACT: return mac_write_ascii(out, '-');
		case VK_DECIMAL: return mac_write_ascii(out, '.');
		case VK_DIVIDE: return mac_write_ascii(out, '/');
		case VK_NONE_BA: return mac_write_ascii(out, shift ? ':' : ';');
		case VK_NONE_BB: return mac_write_ascii(out, shift ? '+' : '=');
		case VK_NONE_BC: return mac_write_ascii(out, shift ? '<' : ',');
		case VK_NONE_BD: return mac_write_ascii(out, shift ? '_' : '-');
		case VK_NONE_BE: return mac_write_ascii(out, shift ? '>' : '.');
		case VK_NONE_BF: return mac_write_ascii(out, shift ? '?' : '/');
		case VK_NONE_C0: return mac_write_ascii(out, shift ? '~' : '`');
		case VK_NONE_DB: return mac_write_ascii(out, shift ? '{' : '[');
		case VK_NONE_DC: return mac_write_ascii(out, shift ? '|' : '\\');
		case VK_NONE_DD: return mac_write_ascii(out, shift ? '}' : ']');
		case VK_NONE_DE: return mac_write_ascii(out, shift ? '"' : '\'');
		default: return 0;
	}
}

static int mac_vk_from_button(unsigned char button)
{
	switch (button) {
		case SDL_BUTTON_LEFT: return VK_LBUTTON;
		case SDL_BUTTON_RIGHT: return VK_RBUTTON;
		case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
		default: return 0;
	}
}

#if defined(RA_MOBILE_TOUCH)
/*
 * Pinch zoom (magnifying the frame up to 3x) is turned off: two fingers scroll
 * the map and every quick two-finger tap is a right click. Set this to true to
 * bring zoom back.
 */
static bool MobilePinchZoomEnabled = false;

static bool mobile_zoom_applies(void)
{
	return MobilePinchZoomEnabled && !InMovie;
}

static bool mobile_logical_point(float normalized_x, float normalized_y, int *logical_x, int *logical_y)
{
	if (!MacWindow || MacWidth <= 0 || MacHeight <= 0) {
		if (logical_x) *logical_x = 0;
		if (logical_y) *logical_y = 0;
		return false;
	}

	int window_w = MacWidth;
	int window_h = MacHeight;
	SDL_GetWindowSize(MacWindow, &window_w, &window_h);
	if (window_w <= 0 || window_h <= 0) {
		if (logical_x) *logical_x = 0;
		if (logical_y) *logical_y = 0;
		return false;
	}

	int screen_x = (int)(normalized_x * (float)window_w);
	int screen_y = (int)(normalized_y * (float)window_h);
	screen_x = RA_ClampInt(screen_x, 0, window_w - 1);
	screen_y = RA_ClampInt(screen_y, 0, window_h - 1);

	MacLayout layout = mac_current_layout();
	bool inside = mac_window_to_logical(layout, screen_x, screen_y, logical_x, logical_y);
	if (layout.page && mobile_zoom_applies() && logical_x && logical_y) {
		MobileZoom_MapPoint(&MobileZoom, layout.source.w, layout.source.h, *logical_x, *logical_y, logical_x, logical_y);
	}
	return inside;
}

static void mobile_window_point(float normalized_x, float normalized_y, float *x, float *y)
{
	int window_w = MacWidth;
	int window_h = MacHeight;
	if (MacWindow) {
		SDL_GetWindowSize(MacWindow, &window_w, &window_h);
	}
	*x = normalized_x * (float)window_w;
	*y = normalized_y * (float)window_h;
}

static void mobile_emit_pan_key(void *, int vk)
{
	mac_queue_unmodified_key_pulse(vk);
}

/* Size, in game pixels, of the part of the frame on screen. */
static void mobile_logical_size(int *width, int *height)
{
	MacLayout layout = mac_current_layout();
	*width = layout.source.w;
	*height = layout.source.h;
}

static void mobile_emit_pan(float dx, float dy)
{
	int logical_w = 0;
	int logical_h = 0;
	mobile_logical_size(&logical_w, &logical_h);
	MobilePan_EmitFingerDelta(&MobilePan, dx, dy, logical_w, logical_h, mobile_emit_pan_key, 0);
}

/*
 * A two-finger move: pinching changes the magnification around the fingers'
 * midpoint, and moving the midpoint drags the magnified view. Movement the
 * view can't absorb (always, when unzoomed) scrolls the map as before.
 * Positions are in window pixels.
 */
static void mobile_handle_pinch(float scale, float mid_x, float mid_y, float mid_dx, float mid_dy)
{
	int window_w = MacWidth;
	int window_h = MacHeight;
	SDL_GetWindowSize(MacWindow, &window_w, &window_h);
	/* Zoom works in the coordinates of the part of the page on screen (the current view). */
	MacLayout layout = mac_current_layout();
	RAAspectViewport viewport = layout.window;
	int logical_w = layout.source.w;
	int logical_h = layout.source.h;
	if (viewport.w <= 0 || viewport.h <= 0 || window_w <= 0 || window_h <= 0 || logical_w <= 0 || logical_h <= 0) {
		return;
	}
	float logical_per_px_x = (float)logical_w / (float)viewport.w;
	float logical_per_px_y = (float)logical_h / (float)viewport.h;

	if (layout.page && mobile_zoom_applies()) {
		float focus_x = (mid_x - (float)viewport.x) * logical_per_px_x;
		float focus_y = (mid_y - (float)viewport.y) * logical_per_px_y;
		MobileZoom_Pinch(&MobileZoom, scale / MobileZoom.scale, focus_x, focus_y, logical_w, logical_h);
	}

	float rest_x = 0.0f;
	float rest_y = 0.0f;
	MobileZoom_Pan(&MobileZoom, mid_dx * logical_per_px_x, mid_dy * logical_per_px_y, logical_w, logical_h, &rest_x, &rest_y);

	/* Each finger used to scroll the map on its own, so the pair scrolled twice as far as the midpoint moves. */
	mobile_emit_pan(2.0f * rest_x / logical_per_px_x / (float)window_w, 2.0f * rest_y / logical_per_px_y / (float)window_h);
}

static void mobile_queue_touch_output(MobileTouchGestureOutput const *out)
{
	if (!out) {
		return;
	}
	for (int index = 0; index < out->count; ++index) {
		MobileTouchGestureEvent const *touch_event = &out->events[index];
		switch (touch_event->type) {
			case MOBILE_TOUCH_MOUSE_MOVE:
				mac_update_mouse_position(touch_event->x, touch_event->y);
				break;
			case MOBILE_TOUCH_LEFT_DOWN:
				mac_queue_mouse_button_with_cursor(VK_LBUTTON, true, touch_event->x, touch_event->y, touch_event->update_cursor != 0);
				break;
			case MOBILE_TOUCH_LEFT_UP:
				mac_queue_mouse_button_with_cursor(VK_LBUTTON, false, touch_event->x, touch_event->y, touch_event->update_cursor != 0);
				break;
			case MOBILE_TOUCH_RIGHT_DOWN:
				mac_queue_mouse_button_with_cursor(VK_RBUTTON, true, touch_event->x, touch_event->y, touch_event->update_cursor != 0);
				break;
			case MOBILE_TOUCH_RIGHT_UP:
				mac_queue_mouse_button_with_cursor(VK_RBUTTON, false, touch_event->x, touch_event->y, touch_event->update_cursor != 0);
				break;
		}
	}
}

static void mobile_maybe_send_long_press(void)
{
	MobileTouchGestureOutput out;
	MobileTouchGesture_Update(&MobileTouch, mac_now_ms(), &out);
	mobile_queue_touch_output(&out);
}
#endif

static void mac_destroy_video_objects(void)
{
	if (MacTexture) {
		SDL_DestroyTexture(MacTexture);
		MacTexture = 0;
	}
	MacTextureWidth = 0;
	MacTextureHeight = 0;
	if (MacRenderer) {
		SDL_DestroyRenderer(MacRenderer);
		MacRenderer = 0;
	}
	if (MacWindow) {
		SDL_DestroyWindow(MacWindow);
		MacWindow = 0;
	}
}

bool MacSDL_SetFullscreen(bool enabled)
{
	MacFullscreen = enabled;
	if (!MacWindow) {
		return true;
	}
	if (SDL_SetWindowFullscreen(MacWindow, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
		return false;
	}
	MacFullscreen = enabled;
	return true;
}

bool MacSDL_GetFullscreen(void)
{
	return MacFullscreen;
}

bool MacSDL_SetMode(int width, int height)
{
	if (width <= 0 || height <= 0) {
		return false;
	}

	if (!MacSDLReady) {
		SDL_SetMainReady();
#if defined(RA_MOBILE_TOUCH)
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#endif
		SDL_SetHint(SDL_HINT_RENDER_LOGICAL_SIZE_MODE, "letterbox");
#if defined(RA_IOS) || defined(RA_ANDROID)
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#if defined(RA_IOS)
		/* Phones with full-screen missions allow only the side that keeps the Dynamic Island over the map. */
		SDL_SetHint(SDL_HINT_ORIENTATIONS, RedAlertIOS_OrientationHint());
		SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
#endif
#if defined(RA_MOBILE_TOUCH)
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
		SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
		if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
			return false;
		}
		MacSDLReady = true;
		MacMainThread = SDL_ThreadID();
		mac_update_modifier_state();
		mac_default_palette();
#if defined(RA_MOBILE_TOUCH)
		MobileTouchGesture_Init(&MobileTouch);
		MobilePan_Init(&MobilePan);
		MobileZoom_Init(&MobileZoom);
		MobilePinch_Init(&MobilePinch);
#endif
	}

	if (MacWindow && MacWidth == width && MacHeight == height) {
		if (mac_fullscreen_env_requested() && !MacFullscreen) {
			MacSDL_SetFullscreen(true);
		}
		return true;
	}

#if defined(RA_IOS)
	/*
	 * The window always fills the scene on iOS, so a new mode (a movie's frame
	 * size, say) only changes the logical size. Keep the window, renderer, and
	 * GL view; MacSDL_Present8 resizes the texture to each frame.
	 */
	if (MacWindow) {
		MacWidth = width;
		MacHeight = height;
		return true;
	}
#endif

	mac_destroy_video_objects();
	MacWidth = width;
	MacHeight = height;
	MacFrameWidth = 0;
	MacFrameHeight = 0;
	memset(&MacPresentedLayout, 0, sizeof(MacPresentedLayout));

	if (mac_fullscreen_env_requested()) {
		MacFullscreen = true;
	}
	Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#if defined(RA_MOBILE_TOUCH)
	window_flags |= SDL_WINDOW_BORDERLESS;
#endif
#if defined(RA_IOS)
	window_flags &= ~SDL_WINDOW_RESIZABLE;
	/*
	 * Render at the display's native pixel density. Without this the GL drawable
	 * is one pixel per point, so the 640x400 frame is shrunk below its own size
	 * (dropping rows and columns) and then upscaled by the compositor.
	 */
	window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
	if (MacFullscreen) {
		window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	MacWindow = SDL_CreateWindow("Command & Conquer: Red Alert", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, window_flags);
	if (!MacWindow) {
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
#if defined(RA_IOS)
	/* Must happen before the renderer creates its GL view, so it gets the scene's size. */
	RedAlertIOS_AttachWindowToScene(MacWindow);
#endif

	Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
	MacRenderer = SDL_CreateRenderer(MacWindow, -1, renderer_flags);
	if (!MacRenderer) {
		MacRenderer = SDL_CreateRenderer(MacWindow, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!MacRenderer) {
		mac_destroy_video_objects();
		return false;
	}

	SDL_RendererInfo renderer_info;
	if (SDL_GetRendererInfo(MacRenderer, &renderer_info) == 0) {
		SDL_Log("Red Alert renderer=%s flags=0x%08x", renderer_info.name ? renderer_info.name : "(unknown)", renderer_info.flags);
	}
	SDL_SetRenderDrawColor(MacRenderer, 0, 0, 0, 255);
	MacTexture = SDL_CreateTexture(MacRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	MacTextureWidth = MacTexture ? width : 0;
	MacTextureHeight = MacTexture ? height : 0;
	return MacTexture != 0;
}

void MacSDL_Shutdown(void)
{
	if (MacFrame) {
		free(MacFrame);
		MacFrame = 0;
		MacFramePixels = 0;
	}
	mac_destroy_video_objects();
	if (MacSDLReady) {
		SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO);
		MacSDLReady = false;
		MacMainThread = 0;
	}
}

void MacSDL_SetPalette(PALETTEENTRY const *entries, int count)
{
	if (!entries) {
		return;
	}
	if (count > 256) {
		count = 256;
	}
	for (int index = 0; index < count; ++index) {
		MacPalette[index] = mac_argb(entries[index].peRed, entries[index].peGreen, entries[index].peBlue);
	}
}

static bool mac_same_viewport(RAAspectViewport a, RAAspectViewport b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool mac_same_layout(MacLayout const &a, MacLayout const &b)
{
	return a.source.x == b.source.x && a.source.y == b.source.y && a.source.w == b.source.w && a.source.h == b.source.h &&
		mac_same_viewport(a.output, b.output);
}

static bool mac_layout_debug(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		enabled = getenv("RA_IOS_LAYOUT_DEBUG") != 0 ? 1 : 0;
	}
	return enabled != 0;
}

/* Draws the frame held in MacTexture into the layout for the current window, view, and safe area. */
static void mac_draw_frame(void)
{
	if (!MacRenderer || !MacTexture || MacFrameWidth <= 0 || MacFrameHeight <= 0) {
		return;
	}
	MacLayout layout = mac_layout_for(MacFrameWidth, MacFrameHeight);
	if (layout.output.w <= 0 || layout.output.h <= 0) {
		return;
	}
	SDL_Rect destination;
	destination.x = layout.output.x;
	destination.y = layout.output.y;
	destination.w = layout.output.w;
	destination.h = layout.output.h;
	SDL_Rect source = layout.source;
#if defined(RA_MOBILE_TOUCH)
	if (layout.page && mobile_zoom_applies() && MobileZoom_IsZoomed(&MobileZoom)) {
		MobileZoomRect zoomed = MobileZoom_SourceRect(&MobileZoom, layout.source.w, layout.source.h);
		source.x = layout.source.x + zoomed.x;
		source.y = layout.source.y + zoomed.y;
		source.w = zoomed.w;
		source.h = zoomed.h;
	}
#endif
	SDL_SetRenderDrawColor(MacRenderer, 0, 0, 0, 255);
	SDL_RenderClear(MacRenderer);
	SDL_RenderCopy(MacRenderer, MacTexture, &source, &destination);
	SDL_RenderPresent(MacRenderer);

	if (mac_layout_debug() && !mac_same_layout(layout, MacPresentedLayout)) {
		int output_w = 0;
		int output_h = 0;
		SDL_GetRendererOutputSize(MacRenderer, &output_w, &output_h);
		SDL_Log("RA_LAYOUT: output=%dx%d frame=%dx%d source=(%d,%d %dx%d) viewport=(%d,%d %dx%d)",
			output_w, output_h, MacFrameWidth, MacFrameHeight,
			layout.source.x, layout.source.y, layout.source.w, layout.source.h,
			layout.output.x, layout.output.y, layout.output.w, layout.output.h);
	}
	MacPresentedLayout = layout;
}

/*
 * Folding or unfolding an iPhone Duo, rotating to the other landscape side, or
 * returning from the background changes the window size or the safe area while
 * the game may be sitting in an idle menu that draws nothing new. Redraw the
 * last frame right away when its layout no longer matches.
 */
static void mac_refresh_layout(bool force)
{
	if (!MacRenderer || !MacTexture || MacFrameWidth <= 0 || MacFrameHeight <= 0) {
		return;
	}
	if (force || !mac_same_layout(mac_layout_for(MacFrameWidth, MacFrameHeight), MacPresentedLayout)) {
		mac_draw_frame();
	}
}

static void mac_sdl_pump_events(bool allow_idle_delay)
{
	if (!MacSDLReady) {
		return;
	}
	if (MacMainThread && SDL_ThreadID() != MacMainThread) {
		return;
	}
	MacMM_PumpTimers();
#if defined(RA_MOBILE_TOUCH)
	mobile_maybe_send_long_press();
#endif
	SDL_Event event;
	bool saw_sdl_event = false;
	bool force_redraw = false;
	while (SDL_PollEvent(&event)) {
		saw_sdl_event = true;
		switch (event.type) {
			case SDL_QUIT:
				MacQuitRequested = true;
				mac_queue_message((HWND)(intptr_t)1, WM_DESTROY, 0, 0);
				break;

			case SDL_MOUSEMOTION: {
				int x = event.motion.x;
				int y = event.motion.y;
				mac_to_logical_point(&x, &y);
				mac_update_mouse_position(x, y);
				break;
			}

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				int vk = mac_vk_from_button(event.button.button);
				if (!vk) {
					break;
				}
				int x = event.button.x;
				int y = event.button.y;
				mac_to_logical_point(&x, &y);
				mac_queue_mouse_button(vk, event.type == SDL_MOUSEBUTTONDOWN, x, y);
				break;
			}

#if defined(RA_MOBILE_TOUCH)
			case SDL_FINGERDOWN: {
				int x = 0;
				int y = 0;
				bool inside = mobile_logical_point(event.tfinger.x, event.tfinger.y, &x, &y);
				if (mac_layout_debug()) {
					SDL_Log("RA_TOUCH: normalized=(%.3f,%.3f) game=(%d,%d) inside=%d", event.tfinger.x, event.tfinger.y, x, y, inside ? 1 : 0);
				}
				if (!inside) {
					break;
				}
				float window_x = 0.0f;
				float window_y = 0.0f;
				mobile_window_point(event.tfinger.x, event.tfinger.y, &window_x, &window_y);
				MobilePinch_Down(&MobilePinch, (long long)event.tfinger.fingerId, window_x, window_y, mac_now_ms(), MobileZoom.scale);
				MobileTouchGestureOutput out;
				MobileTouchGesture_Begin(&MobileTouch, (long long)event.tfinger.fingerId, x, y, mac_now_ms(), &out);
				if (MobileTouch.secondary_active && MobileTouch.secondary_finger == (long long)event.tfinger.fingerId) {
					MobilePan_Reset(&MobilePan);
				}
				mobile_queue_touch_output(&out);
				break;
			}

			case SDL_FINGERMOTION: {
				int x = 0;
				int y = 0;
				mobile_logical_point(event.tfinger.x, event.tfinger.y, &x, &y);
				float window_x = 0.0f;
				float window_y = 0.0f;
				float scale = MobileZoom.scale;
				float mid_x = 0.0f;
				float mid_y = 0.0f;
				float mid_dx = 0.0f;
				float mid_dy = 0.0f;
				mobile_window_point(event.tfinger.x, event.tfinger.y, &window_x, &window_y);
				bool pinch_moved = MobilePinch_Move(&MobilePinch, (long long)event.tfinger.fingerId, window_x, window_y,
					&scale, &mid_x, &mid_y, &mid_dx, &mid_dy) != 0;
				if (MobileTouchGesture_IsPanFinger(&MobileTouch, (long long)event.tfinger.fingerId)) {
					if (pinch_moved) {
						mobile_handle_pinch(scale, mid_x, mid_y, mid_dx, mid_dy);
					} else {
						mobile_emit_pan(event.tfinger.dx, event.tfinger.dy);
					}
					break;
				}
				MobileTouchGestureOutput out;
				MobileTouchGesture_Move(&MobileTouch, (long long)event.tfinger.fingerId, x, y, &out);
				mobile_queue_touch_output(&out);
				break;
			}

			case SDL_FINGERUP: {
				int x = 0;
				int y = 0;
				if (MobilePinch_BothActive(&MobilePinch)) {
					unsigned long now = mac_now_ms();
					int tap_fingers = MobilePinch_EndTapFingers(&MobilePinch, now);
					if (MobilePinchZoomEnabled && MobilePinch_RecordTap(&MobilePinch, now, tap_fingers)) {
						/* A two-finger double tap shows the whole frame again. */
						MobileZoom_Init(&MobileZoom);
					} else if (tap_fingers == 2 && MobileTouch.primary_active) {
						/* A quick two-finger tap is a right click, which deselects units. */
						MobileTouchGestureOutput out;
						MobileTouchGesture_ClearOutput(&out);
						MobileTouchGesture_Add(&out, MOBILE_TOUCH_RIGHT_DOWN, MobileTouch.last_x, MobileTouch.last_y, 0);
						MobileTouchGesture_Add(&out, MOBILE_TOUCH_RIGHT_UP, MobileTouch.last_x, MobileTouch.last_y, 0);
						mobile_queue_touch_output(&out);
					} else {
						MobileZoom_SnapIfNearlyUnzoomed(&MobileZoom);
					}
				}
				if (MobilePinch.max_down == 1) MobilePinch.had_tap = 0;
				MobilePinch_Up(&MobilePinch, (long long)event.tfinger.fingerId);
				mobile_logical_point(event.tfinger.x, event.tfinger.y, &x, &y);
				if (InMovie && MobileTouchGesture_IsTapEnd(&MobileTouch, (long long)event.tfinger.fingerId, x, y)) {
					MobileTouchGesture_Init(&MobileTouch);
					mac_queue_key_pulse(VK_ESCAPE);
					break;
				}
				MobileTouchGestureOutput out;
				MobileTouchGesture_End(&MobileTouch, (long long)event.tfinger.fingerId, x, y, &out);
				mobile_queue_touch_output(&out);
				break;
			}

			case SDL_MULTIGESTURE:
				if (MobileTouch.secondary_active) {
					int logical_w = 0;
					int logical_h = 0;
					mobile_logical_size(&logical_w, &logical_h);
					MobilePan_IgnoreMultiGestureCenter(&MobilePan, event.mgesture.x, event.mgesture.y, logical_w, logical_h, mobile_emit_pan_key, 0);
				}
				break;
#endif

			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				if (event.type == SDL_KEYDOWN && !event.key.repeat &&
						(event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) &&
						(event.key.keysym.mod & (KMOD_GUI | KMOD_ALT))) {
					MacSDL_SetFullscreen(!MacFullscreen);
					break;
				}
				int vk = mac_vk_from_sdl(event.key.keysym.sym);
				if (!vk) {
					break;
				}
				MacKeyState[vk & 0xFF] = (event.type == SDL_KEYDOWN) ? 1 : 0;
				mac_update_modifier_state();
				mac_queue_message((HWND)(intptr_t)1, event.type == SDL_KEYDOWN ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk, 0);
				break;
			}

			case SDL_APP_DIDENTERFOREGROUND:
			case SDL_RENDER_TARGETS_RESET:
				force_redraw = true;
				break;

			default:
				break;
		}
	}
	if (saw_sdl_event) {
		mac_refresh_layout(force_redraw);
	}
#if defined(RA_MOBILE_TOUCH)
	mobile_idle_delay(allow_idle_delay, saw_sdl_event);
#endif
}

void MacSDL_PumpEvents(void)
{
	mac_sdl_pump_events(true);
}

bool MacSDL_QuitRequested(void)
{
	return MacQuitRequested;
}

bool MacSDL_TouchCursorHidden(void)
{
#if defined(RA_MOBILE_TOUCH)
	return MobileTouchCursorHidden;
#else
	return false;
#endif
}

int MacSDL_ConsumeMobilePointerDrag(int *x, int *y)
{
#if defined(RA_MOBILE_TOUCH)
	if (!MobilePointerDragCandidate) {
		return 0;
	}
	MobilePointerDragCandidate = false;
	if (x) {
		*x = MobilePointerDragStartX;
	}
	if (y) {
		*y = MobilePointerDragStartY;
	}
	return 1;
#else
	(void)x;
	(void)y;
	return 0;
#endif
}

void MacSDL_Present8(unsigned char const *pixels, int width, int height, int pitch)
{
	if (!pixels || width <= 0 || height <= 0) {
		return;
	}
	if (MacMainThread && SDL_ThreadID() != MacMainThread) {
		return;
	}
	if (MacPresenting) {
		return;
	}

	MacPresenting = true;
	mac_sdl_pump_events(false);
	if (!MacRenderer) {
		MacPresenting = false;
		return;
	}
	if (!MacTexture || width != MacTextureWidth || height != MacTextureHeight) {
		/* Movies and the game page can differ in size; keep the texture matched to the frame. */
		if (MacTexture) {
			SDL_DestroyTexture(MacTexture);
		}
		MacTexture = SDL_CreateTexture(MacRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
		MacTextureWidth = MacTexture ? width : 0;
		MacTextureHeight = MacTexture ? height : 0;
		if (!MacTexture) {
			MacPresenting = false;
			return;
		}
	}

	int needed = width * height;
	if (needed > MacFramePixels) {
		uint32_t *new_frame = (uint32_t *)realloc(MacFrame, (size_t)needed * sizeof(uint32_t));
		if (!new_frame) {
			MacPresenting = false;
			return;
		}
		MacFrame = new_frame;
		MacFramePixels = needed;
	}

	for (int y = 0; y < height; ++y) {
		unsigned char const *src = pixels + (y * pitch);
		uint32_t *dst = MacFrame + (y * width);
		for (int x = 0; x < width; ++x) {
			dst[x] = MacPalette[src[x]];
		}
	}

	SDL_UpdateTexture(MacTexture, 0, MacFrame, width * (int)sizeof(uint32_t));
	MacFrameWidth = width;
	MacFrameHeight = height;
	mac_draw_frame();
	MacPresenting = false;
}

extern "C" BOOL MacWin_PeekMessage(MSG *msg, HWND, UINT, UINT, UINT remove)
{
	mac_sdl_pump_events(true);
	return mac_pop_message(msg, (remove & PM_REMOVE) != 0);
}

extern "C" BOOL MacWin_GetMessage(MSG *msg, HWND, UINT, UINT)
{
	mac_sdl_pump_events(true);
	if (!mac_pop_message(msg, true)) {
		return FALSE;
	}
	return msg && msg->message == WM_QUIT ? FALSE : TRUE;
}

extern "C" BOOL MacWin_TranslateMessage(MSG const *)
{
	return TRUE;
}

extern "C" LRESULT MacWin_DispatchMessage(MSG const *msg)
{
	if (!msg) {
		return 0;
	}
	bool previous = MacUnmodifiedKeyDispatch;
	MacUnmodifiedKeyDispatch = MobileKeyMessage_IsUnmodified(msg->message, msg->lParam) != 0;
	LRESULT result = Windows_Procedure(msg->hwnd, msg->message, msg->wParam, msg->lParam);
	MacUnmodifiedKeyDispatch = previous;
	return result;
}

extern "C" BOOL MacWin_PostMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	return mac_queue_message(hwnd, message, wparam, lparam);
}

extern "C" void MacWin_PostQuitMessage(int code)
{
	mac_queue_message((HWND)(intptr_t)1, WM_QUIT, (WPARAM)code, 0);
}

extern "C" short MacWin_GetAsyncKeyState(int key)
{
	key &= 0xFF;
	return MacKeyState[key] ? (short)0x8000 : 0;
}

extern "C" short MacWin_GetKeyState(int key)
{
	key &= 0xFF;
	short state = MacKeyState[key] ? (short)0x8000 : 0;
	if (MacToggleState[key]) {
		state |= (short)0x0009;
	}
	return MobileKeyMessage_FilterKeyState(MacUnmodifiedKeyDispatch, key, state);
}

extern "C" int MacWin_ToAscii(UINT virt_key, UINT, BYTE const *key_state, LPWORD trans_key, UINT)
{
	if (mac_state_down(key_state, VK_CONTROL) || mac_state_down(key_state, VK_MENU)) {
		return 0;
	}
	return mac_ascii_from_vk(virt_key, key_state, trans_key);
}

extern "C" BOOL MacWin_GetCursorPos(LPPOINT point)
{
	if (point) {
		*point = MacMousePoint;
	}
	return TRUE;
}
