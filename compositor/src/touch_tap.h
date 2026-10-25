#ifndef ZACOS9_TOUCH_TAP_H
#define ZACOS9_TOUCH_TAP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A finger as the mouse (touch.c), and a two-finger tap as a right-click.
 * The first finger's press waits a moment (TAP_WAIT_MS): a second finger
 * down in that time makes it a two-finger tap instead, a right-click
 * where the first went down once both lift, if neither moved and they
 * were quick about it. The press goes at once when the finger moves or
 * lifts, or when the moment is over. Pure: no wlroots.
 */

#define TAP_WAIT_MS 150   /* for a second finger */
#define TAP_MAX_MS 500    /* a two-finger tap is over by then */
#define TAP_SLOP 12       /* layout pixels a tapping finger may wander */

enum tap_action {
	TAP_NONE,
	TAP_PRESS,       /* left button down, at the first finger's start */
	TAP_MOVE,        /* the pointer to where the finger is */
	TAP_RELEASE,     /* left button up */
	TAP_RIGHT_CLICK, /* right button down and up, at the first finger's start */
};

/* Up to three actions an event leads to, in order. */
struct tap_actions {
	enum tap_action a[3];
	int n;
};

enum tap_phase {
	TAP_IDLE,
	TAP_WAITING, /* one finger down, no press yet */
	TAP_PRESSED, /* the first finger is the mouse, its button down */
	TAP_TWO,     /* two fingers: maybe a right-click */
};

struct tap_state {
	enum tap_phase phase;
	int32_t first, second;     /* touch ids */
	bool first_up, second_up;  /* TAP_TWO: lifted already */
	bool moved;                /* TAP_TWO: too far for a tap */
	uint32_t since;            /* ms: the first finger down */
	double x, y;               /* where it went down */
};

/* The first finger on something that takes the mouse (not an app's own
 * touch); false if a finger is already the mouse. */
bool tap_down_first(struct tap_state *s, int32_t id, uint32_t ms, double x, double y);
/* Another finger down; true if it's taken as a two-finger tap's second. */
bool tap_down_other(struct tap_state *s, int32_t id, uint32_t ms);
struct tap_actions tap_motion(struct tap_state *s, int32_t id, double x, double y);
struct tap_actions tap_up(struct tap_state *s, int32_t id, uint32_t ms);
/* TAP_WAIT_MS after the first finger went down (touch.c's timer). */
struct tap_actions tap_wait_over(struct tap_state *s);
/* Every finger taken back. */
struct tap_actions tap_cancel(struct tap_state *s);
/* Whether this finger is ours (the mouse, or a two-finger tap's). */
bool tap_owns(const struct tap_state *s, int32_t id);

#endif
