#include "mobile_safe_area.h"

#include <stdio.h>

static MobileSafeArea make_area(float left, float top, float right, float bottom, int orientation)
{
	MobileSafeArea area;
	area.left = left;
	area.top = top;
	area.right = right;
	area.bottom = bottom;
	area.orientation = orientation;
	return area;
}

static int expect_area(MobileSafeArea actual, float left, float top, float right, float bottom, char const *name)
{
	if (actual.left != left || actual.top != top || actual.right != right || actual.bottom != bottom) {
		fprintf(stderr, "FAIL: %s: got (%g,%g,%g,%g) expected (%g,%g,%g,%g)\n",
			name, actual.left, actual.top, actual.right, actual.bottom, left, top, right, bottom);
		return 1;
	}
	return 0;
}

int main(void)
{
	/* UIKit reports the Dynamic Island inset on both short edges in landscape. */
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(62, 0, 62, 21, MOBILE_ORIENTATION_LANDSCAPE_LEFT)),
			0, 0, 62, 21, "landscape left keeps the right housing inset")) return 1;
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(62, 0, 62, 21, MOBILE_ORIENTATION_LANDSCAPE_RIGHT)),
			62, 0, 0, 21, "landscape right keeps the left housing inset")) return 1;

	/* Without a known landscape orientation the reported insets are kept as-is. */
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(62, 0, 62, 21, MOBILE_ORIENTATION_UNKNOWN)),
			62, 0, 62, 21, "unknown orientation keeps both insets")) return 1;
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(0, 59, 0, 34, MOBILE_ORIENTATION_PORTRAIT)),
			0, 59, 0, 34, "portrait insets are untouched")) return 1;

	/* Asymmetric insets already describe the real obstruction. */
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(10, 0, 40, 0, MOBILE_ORIENTATION_LANDSCAPE_LEFT)),
			10, 0, 40, 0, "asymmetric insets are untouched")) return 1;
	if (expect_area(MobileSafeArea_ResolveSensorHousing(make_area(0, 0, 0, 20, MOBILE_ORIENTATION_LANDSCAPE_LEFT)),
			0, 0, 0, 20, "bottom-only insets are untouched")) return 1;

	return 0;
}
