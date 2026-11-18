#include "mobile_pan.h"

#include <assert.h>
#include <stdlib.h>

static int take_x(MobilePanState *pan, unsigned long tick)
{
	int dx = 0;
	int dy = 0;
	MobilePan_Take(pan, tick, &dx, &dy);
	return dx;
}

static int take_y(MobilePanState *pan, unsigned long tick)
{
	int dx = 0;
	int dy = 0;
	MobilePan_Take(pan, tick, &dx, &dy);
	return dy;
}

static void test_map_follows_fingers_one_to_one(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);

	/* Fingers moving right by a cell drag the map right: the view moves a cell west. */
	MobilePan_Drag(&pan, 24.0f, 0.0f, 1000);
	assert(take_x(&pan, 1000) == -256);

	/* Fingers moving up show what is south. */
	MobilePan_Drag(&pan, 0.0f, -12.0f, 1010);
	assert(take_y(&pan, 1010) == 128);
}

static void test_small_moves_add_up_without_drift(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	int total = 0;
	for (int step = 0; step < 240; ++step) {
		MobilePan_Drag(&pan, -0.5f, 0.0f, 2000 + step);
		total += take_x(&pan, 2000 + step);
	}
	/* 120 pixels is five cells. */
	assert(abs(total - 5 * 256) <= 1);
}

static void test_flick_glides_on_and_stops(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	for (int step = 0; step < 6; ++step) {
		MobilePan_Drag(&pan, -16.0f, 0.0f, 3000 + step * 10);
	}
	take_x(&pan, 3050);
	MobilePan_Release(&pan, 3055);
	assert(pan.gliding);

	int glide = 0;
	unsigned long tick = 3055;
	for (int step = 0; step < 400 && pan.gliding; ++step) {
		tick += 16;
		MobilePan_Step(&pan, tick);
		glide += take_x(&pan, tick);
	}
	assert(!pan.gliding);
	/* It keeps going the same way (east) a good distance, but not forever. */
	assert(glide > 256);
	assert(glide < 256 * 40);
}

static void test_fingers_that_stopped_do_not_flick(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	for (int step = 0; step < 6; ++step) {
		MobilePan_Drag(&pan, -16.0f, 0.0f, 4000 + step * 10);
	}
	MobilePan_Release(&pan, 4050 + MobilePan_FlickStillMs + 1);
	assert(!pan.gliding);

	MobilePan_Init(&pan);
	MobilePan_Drag(&pan, -1.0f, 0.0f, 5000);
	MobilePan_Drag(&pan, -1.0f, 0.0f, 5040);
	MobilePan_Release(&pan, 5045);
	assert(!pan.gliding);
}

static void test_new_touch_stops_glide(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	MobilePan_Drag(&pan, 20.0f, 0.0f, 6000);
	MobilePan_Drag(&pan, 20.0f, 0.0f, 6010);
	MobilePan_Release(&pan, 6012);
	assert(pan.gliding);
	MobilePan_Reset(&pan);
	assert(!pan.gliding);
	assert(!MobilePan_Step(&pan, 6100));
}

static void test_movement_not_taken_in_time_is_dropped(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	MobilePan_Drag(&pan, 48.0f, 0.0f, 7000);
	assert(take_x(&pan, 7000 + MobilePan_StaleMs + 1) == 0);
	assert(take_x(&pan, 7001) == 0);
}

static void test_multigesture_center_coordinates_are_not_pan_deltas(void)
{
	MobilePanState pan;
	MobilePan_Init(&pan);
	MobilePan_IgnoreMultiGestureCenter(&pan, 0.5f, 0.5f);
	assert(take_x(&pan, 0) == 0);
}

int main(void)
{
	test_map_follows_fingers_one_to_one();
	test_small_moves_add_up_without_drift();
	test_flick_glides_on_and_stops();
	test_fingers_that_stopped_do_not_flick();
	test_new_touch_stops_glide();
	test_movement_not_taken_in_time_is_dropped();
	test_multigesture_center_coordinates_are_not_pan_deltas();
	return 0;
}
