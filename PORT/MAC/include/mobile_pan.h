#ifndef MOBILE_PAN_H
#define MOBILE_PAN_H

#include <math.h>

/*
 * Two-finger map scrolling that follows the fingers. Finger movement, in game
 * pixels, becomes movement of the view the other way (the map moves with the
 * fingers), held until the game takes it in leptons. Lifting the fingers while
 * they are still moving keeps the view gliding and slowing down, as iOS lists do.
 */

/* Leptons per game pixel: a 24-pixel cell is 256 leptons wide. */
static const float MobilePan_LeptonsPerPixel = 256.0f / 24.0f;
/* Finger movement this recent decides how fast a flick was. */
static const unsigned long MobilePan_FlickWindowMs = 100;
/* Fingers that stopped this long before lifting don't flick. */
static const unsigned long MobilePan_FlickStillMs = 50;
/* Flicks slower than this, in game pixels per millisecond, just stop. */
static const float MobilePan_FlickMinSpeed = 0.15f;
static const float MobilePan_FlickMaxSpeed = 4.0f;
/* Glide speed kept per millisecond, and the speed where the glide ends. */
static const float MobilePan_GlideDecay = 0.995f;
static const float MobilePan_GlideStopSpeed = 0.02f;
/* Movement the game hasn't taken for this long (it was in a menu) is dropped. */
static const unsigned long MobilePan_StaleMs = 250;

enum { MobilePan_Samples = 8 };

struct MobilePanState {
	/* View movement not yet taken by the game, in game pixels. */
	float pending_x;
	float pending_y;
	unsigned long pending_tick;
	/* Total view movement of the current drag, for the flick speed. */
	float total_x;
	float total_y;
	int sample_count;
	int sample_next;
	unsigned long sample_tick[MobilePan_Samples];
	float sample_x[MobilePan_Samples];
	float sample_y[MobilePan_Samples];
	/* Glide after a flick, in game pixels per millisecond. */
	int gliding;
	float velocity_x;
	float velocity_y;
	unsigned long glide_tick;
};

static inline void MobilePan_Init(MobilePanState *pan)
{
	if (!pan) {
		return;
	}
	pan->pending_x = 0.0f;
	pan->pending_y = 0.0f;
	pan->pending_tick = 0;
	pan->total_x = 0.0f;
	pan->total_y = 0.0f;
	pan->sample_count = 0;
	pan->sample_next = 0;
	pan->gliding = 0;
	pan->velocity_x = 0.0f;
	pan->velocity_y = 0.0f;
	pan->glide_tick = 0;
}

/* A finger touched down: stop any glide and start measuring a new drag. */
static inline void MobilePan_Reset(MobilePanState *pan)
{
	if (!pan) {
		return;
	}
	pan->total_x = 0.0f;
	pan->total_y = 0.0f;
	pan->sample_count = 0;
	pan->sample_next = 0;
	pan->gliding = 0;
	pan->velocity_x = 0.0f;
	pan->velocity_y = 0.0f;
}

static inline void MobilePan_AddPending(MobilePanState *pan, float view_dx, float view_dy, unsigned long tick)
{
	pan->pending_x += view_dx;
	pan->pending_y += view_dy;
	pan->pending_tick = tick;
}

/* The fingers moved by finger_dx, finger_dy game pixels. */
static inline void MobilePan_Drag(MobilePanState *pan, float finger_dx, float finger_dy, unsigned long tick)
{
	if (!pan) {
		return;
	}
	MobilePan_AddPending(pan, -finger_dx, -finger_dy, tick);
	pan->total_x -= finger_dx;
	pan->total_y -= finger_dy;
	pan->sample_tick[pan->sample_next] = tick;
	pan->sample_x[pan->sample_next] = pan->total_x;
	pan->sample_y[pan->sample_next] = pan->total_y;
	pan->sample_next = (pan->sample_next + 1) % MobilePan_Samples;
	if (pan->sample_count < MobilePan_Samples) {
		pan->sample_count++;
	}
}

/* The fingers lifted: glide on if they were moving fast enough. */
static inline void MobilePan_Release(MobilePanState *pan, unsigned long tick)
{
	if (!pan) {
		return;
	}
	pan->gliding = 0;
	if (pan->sample_count < 2) {
		return;
	}
	int newest = (pan->sample_next + MobilePan_Samples - 1) % MobilePan_Samples;
	if ((unsigned long)(tick - pan->sample_tick[newest]) > MobilePan_FlickStillMs) {
		return;
	}
	int oldest = newest;
	for (int count = 1; count < pan->sample_count; ++count) {
		int index = (newest + MobilePan_Samples - count) % MobilePan_Samples;
		if ((unsigned long)(tick - pan->sample_tick[index]) > MobilePan_FlickWindowMs) {
			break;
		}
		oldest = index;
	}
	unsigned long elapsed = (unsigned long)(pan->sample_tick[newest] - pan->sample_tick[oldest]);
	if (oldest == newest || elapsed == 0) {
		return;
	}
	float vx = (pan->sample_x[newest] - pan->sample_x[oldest]) / (float)elapsed;
	float vy = (pan->sample_y[newest] - pan->sample_y[oldest]) / (float)elapsed;
	float speed = sqrtf(vx * vx + vy * vy);
	if (speed < MobilePan_FlickMinSpeed) {
		return;
	}
	if (speed > MobilePan_FlickMaxSpeed) {
		vx *= MobilePan_FlickMaxSpeed / speed;
		vy *= MobilePan_FlickMaxSpeed / speed;
	}
	pan->gliding = 1;
	pan->velocity_x = vx;
	pan->velocity_y = vy;
	pan->glide_tick = tick;
}

/* Advances a glide to tick. Call often; returns nonzero while gliding. */
static inline int MobilePan_Step(MobilePanState *pan, unsigned long tick)
{
	if (!pan || !pan->gliding) {
		return 0;
	}
	unsigned long elapsed = (unsigned long)(tick - pan->glide_tick);
	if (elapsed == 0) {
		return 1;
	}
	pan->glide_tick = tick;
	/* Distance covered while the speed decays over the elapsed time. */
	float keep = powf(MobilePan_GlideDecay, (float)elapsed);
	float travel = (1.0f - keep) / (1.0f - MobilePan_GlideDecay);
	MobilePan_AddPending(pan, pan->velocity_x * travel, pan->velocity_y * travel, tick);
	pan->velocity_x *= keep;
	pan->velocity_y *= keep;
	if (sqrtf(pan->velocity_x * pan->velocity_x + pan->velocity_y * pan->velocity_y) < MobilePan_GlideStopSpeed) {
		pan->gliding = 0;
	}
	return pan->gliding;
}

static inline int MobilePan_TakeAxis(float *pending, int *leptons)
{
	float exact = *pending * MobilePan_LeptonsPerPixel;
	int whole = (int)exact;
	*leptons = whole;
	*pending -= (float)whole / MobilePan_LeptonsPerPixel;
	return whole != 0;
}

/*
 * Hands the game the view movement in whole leptons (positive x is east,
 * positive y is south), keeping the remainder. Returns nonzero if it moved.
 */
static inline int MobilePan_Take(MobilePanState *pan, unsigned long tick, int *dx, int *dy)
{
	*dx = 0;
	*dy = 0;
	if (!pan) {
		return 0;
	}
	if (!pan->gliding && (unsigned long)(tick - pan->pending_tick) > MobilePan_StaleMs) {
		pan->pending_x = 0.0f;
		pan->pending_y = 0.0f;
		return 0;
	}
	int moved_x = MobilePan_TakeAxis(&pan->pending_x, dx);
	int moved_y = MobilePan_TakeAxis(&pan->pending_y, dy);
	return moved_x || moved_y;
}

/* SDL_MULTIGESTURE reports where the fingers are, not how far they moved. */
static inline void MobilePan_IgnoreMultiGestureCenter(MobilePanState *, float, float)
{
}

#endif
