#include "ra_aspect_viewport.h"

#include <stdio.h>

static int expect_int(int actual, int expected, char const *message)
{
	if (actual != expected) {
		fprintf(stderr, "FAIL: %s: got %d expected %d\n", message, actual, expected);
		return 1;
	}
	return 0;
}

static RAViewportInsets make_insets(int left, int top, int right, int bottom)
{
	RAViewportInsets insets;
	insets.left = left;
	insets.top = top;
	insets.right = right;
	insets.bottom = bottom;
	return insets;
}

static int expect_rect(RAAspectViewport actual, int x, int y, int w, int h, char const *name)
{
	if (actual.x != x || actual.y != y || actual.w != w || actual.h != h) {
		fprintf(stderr, "FAIL: %s: got (%d,%d %dx%d) expected (%d,%d %dx%d)\n",
			name, actual.x, actual.y, actual.w, actual.h, x, y, w, h);
		return 1;
	}
	return 0;
}

/*
 * iPhone Duo geometry in points, landscape: the folded outer display is
 * 678x466 and the unfolded inner display is 951x669. Both are squarer than
 * the 640x400 game frame, so the frame spans the full width and the sensor
 * housing on a short edge would cover the sidebar or the map.
 */
static int inset_viewport_cases(void)
{
	RAViewportInsets none = make_insets(0, 0, 0, 0);
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 678, 466, none), 0, 21, 678, 423, "duo outer without insets")) return 1;
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 951, 669, none), 0, 37, 951, 594, "duo inner without insets")) return 1;

	/* Housing on the right: the frame shrinks just enough and hugs the left edge. */
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 678, 466, make_insets(0, 0, 62, 0)), 0, 40, 616, 385, "duo outer housing right")) return 1;
	/* Housing on the left: same size, pushed clear of the left edge. */
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 678, 466, make_insets(62, 0, 0, 0)), 62, 40, 616, 385, "duo outer housing left")) return 1;

	/* Wide phones already pillarbox past the housing, so nothing moves. */
	RAAspectViewport plain = RA_CalculateAspectViewport(640, 400, 852, 393);
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 852, 393, make_insets(59, 0, 0, 0)), plain.x, plain.y, plain.w, plain.h, "wide phone housing left")) return 1;
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 852, 393, make_insets(0, 0, 59, 0)), plain.x, plain.y, plain.w, plain.h, "wide phone housing right")) return 1;

	/* Symmetric insets keep the frame centered. */
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 678, 466, make_insets(31, 0, 31, 0)), 31, 40, 616, 385, "symmetric insets")) return 1;

	/* Bottom-only inset on a width-limited screen: room to spare, so it stays centered. */
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 951, 669, make_insets(0, 0, 0, 20)), 0, 37, 951, 594, "inner bottom inset fits")) return 1;

	/* Insets that would leave no room are ignored instead of collapsing the frame. */
	if (expect_rect(RA_CalculateInsetAspectViewport(640, 400, 678, 466, make_insets(400, 0, 400, 0)), 0, 21, 678, 423, "oversized insets ignored")) return 1;
	return 0;
}

/*
 * Full-screen missions: the iPhone Duo's displays are squarer than 640x400, so
 * the page grows to 448 lines and is drawn edge to edge.
 */
static int mission_page_cases(void)
{
	if (expect_int(RA_MissionPageHeight(640, 678, 466), 448, "duo outer page height")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 951, 669), 448, "duo inner page height")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 1194, 834), 448, "11-inch tablet page height")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 1024, 768), 472, "4:3 tablet page height")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 874, 402), 400, "wide phone keeps 400")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 640, 400), 400, "640x400 display keeps 400")) return 1;
	if (expect_int(RA_MissionPageHeight(640, 0, 466), 400, "unknown display keeps 400")) return 1;

	/* A 448-line page fills both Duo displays; the stretch is under 2 percent. */
	if (expect_rect(RA_CalculateFillViewport(640, 448, 678, 466, 3), 0, 0, 678, 466, "duo outer fills")) return 1;
	if (expect_rect(RA_CalculateFillViewport(640, 448, 951, 669, 3), 0, 0, 951, 669, "duo inner fills")) return 1;
	if (expect_rect(RA_CalculateFillViewport(640, 448, 2034, 1398, 3), 0, 0, 2034, 1398, "duo outer pixels fill")) return 1;
	/* A 640x400 menu would stretch about 10 percent, so it keeps its bars. */
	if (expect_rect(RA_CalculateFillViewport(640, 400, 678, 466, 3), 0, 21, 678, 423, "menu frame is not stretched")) return 1;
	if (expect_rect(RA_CalculateFillViewport(640, 448, 0, 466, 3), 0, 0, 0, 0, "empty target")) return 1;
	return 0;
}

int main(void)
{
	RAAspectViewport tablet = RA_CalculateAspectViewport(640, 400, 2550, 1490);
	if (expect_int(tablet.x, 83, "wide tablet viewport x")) return 1;
	if (expect_int(tablet.y, 0, "wide tablet viewport y")) return 1;
	if (expect_int(tablet.w, 2384, "wide tablet viewport width")) return 1;
	if (expect_int(tablet.h, 1490, "wide tablet viewport height")) return 1;

	RAAspectViewport phone = RA_CalculateAspectViewport(640, 400, 1920, 1080);
	if (expect_int(phone.x, 96, "wide phone viewport x")) return 1;
	if (expect_int(phone.y, 0, "wide phone viewport y")) return 1;
	if (expect_int(phone.w, 1728, "wide phone viewport width")) return 1;
	if (expect_int(phone.h, 1080, "wide phone viewport height")) return 1;

	RAAspectViewport tall = RA_CalculateAspectViewport(640, 400, 1200, 1000);
	if (expect_int(tall.x, 0, "tall viewport x")) return 1;
	if (expect_int(tall.y, 125, "tall viewport y")) return 1;
	if (expect_int(tall.w, 1200, "tall viewport width")) return 1;
	if (expect_int(tall.h, 750, "tall viewport height")) return 1;

	int logical_x = -1;
	int logical_y = -1;
	if (!RA_MapViewportPoint(tablet, 640, 400, 83, 0, &logical_x, &logical_y)) {
		fprintf(stderr, "FAIL: tablet top-left should map inside viewport\n");
		return 1;
	}
	if (expect_int(logical_x, 0, "tablet top-left logical x")) return 1;
	if (expect_int(logical_y, 0, "tablet top-left logical y")) return 1;

	if (!RA_MapViewportPoint(tablet, 640, 400, 2466, 1489, &logical_x, &logical_y)) {
		fprintf(stderr, "FAIL: tablet bottom-right should map inside viewport\n");
		return 1;
	}
	if (expect_int(logical_x, 639, "tablet bottom-right logical x")) return 1;
	if (expect_int(logical_y, 399, "tablet bottom-right logical y")) return 1;

	if (RA_MapViewportPoint(tablet, 640, 400, 82, 100, &logical_x, &logical_y)) {
		fprintf(stderr, "FAIL: left pillarbox should report outside viewport\n");
		return 1;
	}
	if (expect_int(logical_x, 0, "left pillarbox clamps to logical x")) return 1;
	if (expect_int(logical_y, 26, "left pillarbox preserves logical y")) return 1;

	if (inset_viewport_cases()) return 1;
	if (mission_page_cases()) return 1;

	return 0;
}
