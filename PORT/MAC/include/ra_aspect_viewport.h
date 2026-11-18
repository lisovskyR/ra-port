#ifndef RA_ASPECT_VIEWPORT_H
#define RA_ASPECT_VIEWPORT_H

struct RAAspectViewport {
	int x;
	int y;
	int w;
	int h;
};

static inline int RA_ClampInt(int value, int min_value, int max_value)
{
	if (value < min_value) return min_value;
	if (value > max_value) return max_value;
	return value;
}

static inline RAAspectViewport RA_CalculateAspectViewport(int source_w, int source_h, int target_w, int target_h)
{
	RAAspectViewport viewport = {0, 0, 0, 0};
	if (source_w <= 0 || source_h <= 0 || target_w <= 0 || target_h <= 0) {
		return viewport;
	}

	if ((long long)target_w * source_h > (long long)target_h * source_w) {
		viewport.h = target_h;
		viewport.w = (int)(((long long)source_w * target_h) / source_h);
		if (viewport.w < 1) viewport.w = 1;
		viewport.x = (target_w - viewport.w) / 2;
		viewport.y = 0;
	} else {
		viewport.w = target_w;
		viewport.h = (int)(((long long)source_h * target_w) / source_w);
		if (viewport.h < 1) viewport.h = 1;
		viewport.x = 0;
		viewport.y = (target_h - viewport.h) / 2;
	}
	return viewport;
}

/*
 * Edge insets (in the same units as the target size) that the game frame must
 * stay out of: sensor housings such as the Dynamic Island, camera cutouts.
 */
struct RAViewportInsets {
	int left;
	int top;
	int right;
	int bottom;
};

/*
 * Largest source-aspect frame that keeps out of the insets. The frame stays
 * centered on the whole target whenever that does not put it inside an inset,
 * so screens with enough room keep symmetric letterbox bars; otherwise it is
 * pushed just far enough to clear the obstruction.
 */
static inline RAAspectViewport RA_CalculateInsetAspectViewport(
	int source_w,
	int source_h,
	int target_w,
	int target_h,
	RAViewportInsets insets)
{
	RAAspectViewport viewport = {0, 0, 0, 0};
	if (source_w <= 0 || source_h <= 0 || target_w <= 0 || target_h <= 0) {
		return viewport;
	}

	int left = insets.left > 0 ? insets.left : 0;
	int top = insets.top > 0 ? insets.top : 0;
	int right = insets.right > 0 ? insets.right : 0;
	int bottom = insets.bottom > 0 ? insets.bottom : 0;
	if (left + right >= target_w) {
		left = 0;
		right = 0;
	}
	if (top + bottom >= target_h) {
		top = 0;
		bottom = 0;
	}

	viewport = RA_CalculateAspectViewport(source_w, source_h, target_w - left - right, target_h - top - bottom);
	viewport.x = RA_ClampInt((target_w - viewport.w) / 2, left, target_w - right - viewport.w);
	viewport.y = RA_ClampInt((target_h - viewport.h) / 2, top, target_h - bottom - viewport.h);
	return viewport;
}

/*
 * Fills the whole target when the aspect ratios differ by no more than
 * max_stretch_percent, a stretch too small to notice; otherwise fits the
 * source with bars like RA_CalculateAspectViewport.
 */
static inline RAAspectViewport RA_CalculateFillViewport(
	int source_w,
	int source_h,
	int target_w,
	int target_h,
	int max_stretch_percent)
{
	RAAspectViewport viewport = {0, 0, 0, 0};
	if (source_w <= 0 || source_h <= 0 || target_w <= 0 || target_h <= 0) {
		return viewport;
	}

	long long source_shape = (long long)source_w * target_h;
	long long target_shape = (long long)target_w * source_h;
	long long larger = source_shape > target_shape ? source_shape : target_shape;
	long long smaller = source_shape > target_shape ? target_shape : source_shape;
	if ((larger - smaller) * 100 <= smaller * max_stretch_percent) {
		viewport.w = target_w;
		viewport.h = target_h;
		return viewport;
	}
	return RA_CalculateAspectViewport(source_w, source_h, target_w, target_h);
}

/*
 * Missions draw a 16-pixel tab bar and then whole 24-pixel map cell rows, so a
 * taller game page grows from the original 400 lines in 24-line steps.
 */
enum {
	RA_PAGE_BASE_HEIGHT = 400,
	RA_PAGE_ROW_HEIGHT = 24,
	RA_PAGE_MAX_HEIGHT = 472
};

/*
 * Page height that best matches a landscape display of display_w x display_h
 * for a page page_w wide. Displays squarer than 640x400 get extra map rows
 * instead of letterbox bars; wider displays keep 400 lines.
 */
static inline int RA_MissionPageHeight(int page_w, int display_w, int display_h)
{
	if (page_w <= 0 || display_w <= 0 || display_h <= 0) {
		return RA_PAGE_BASE_HEIGHT;
	}

	/* Compare candidate heights scaled by display_w against page_w * display_h. */
	long long ideal = (long long)page_w * display_h;
	int best = RA_PAGE_BASE_HEIGHT;
	long long best_error = (long long)best * display_w - ideal;
	if (best_error < 0) best_error = -best_error;
	for (int height = RA_PAGE_BASE_HEIGHT + RA_PAGE_ROW_HEIGHT; height <= RA_PAGE_MAX_HEIGHT; height += RA_PAGE_ROW_HEIGHT) {
		long long error = (long long)height * display_w - ideal;
		if (error < 0) error = -error;
		if (error < best_error) {
			best = height;
			best_error = error;
		}
	}
	return best;
}

static inline int RA_MapViewportPoint(
	RAAspectViewport viewport,
	int logical_w,
	int logical_h,
	int screen_x,
	int screen_y,
	int *logical_x,
	int *logical_y)
{
	if (viewport.w <= 0 || viewport.h <= 0 || logical_w <= 0 || logical_h <= 0) {
		if (logical_x) *logical_x = 0;
		if (logical_y) *logical_y = 0;
		return 0;
	}

	int right = viewport.x + viewport.w;
	int bottom = viewport.y + viewport.h;
	int inside = screen_x >= viewport.x && screen_x < right && screen_y >= viewport.y && screen_y < bottom;

	int clamped_x = RA_ClampInt(screen_x, viewport.x, right - 1);
	int clamped_y = RA_ClampInt(screen_y, viewport.y, bottom - 1);
	int mapped_x = (int)(((long long)(clamped_x - viewport.x) * logical_w) / viewport.w);
	int mapped_y = (int)(((long long)(clamped_y - viewport.y) * logical_h) / viewport.h);

	if (mapped_x >= logical_w) mapped_x = logical_w - 1;
	if (mapped_y >= logical_h) mapped_y = logical_h - 1;
	if (logical_x) *logical_x = mapped_x;
	if (logical_y) *logical_y = mapped_y;
	return inside;
}

#endif
