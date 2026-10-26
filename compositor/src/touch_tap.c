#include "touch_tap.h"

static void add(struct tap_actions *out, enum tap_action a) {
	if (out->n < 3) {
		out->a[out->n++] = a;
	}
}

static bool far(const struct tap_state *s, double x, double y) {
	const double dx = x - s->x, dy = y - s->y;
	return dx * dx + dy * dy > TAP_SLOP * TAP_SLOP;
}

bool tap_down_first(struct tap_state *s, int32_t id, uint32_t ms, double x, double y) {
	if (s->phase != TAP_IDLE) {
		return false;
	}
	*s = (struct tap_state){ .phase = TAP_WAITING, .first = id, .second = -1, .since = ms, .x = x, .y = y };
	return true;
}

bool tap_down_other(struct tap_state *s, int32_t id, uint32_t ms) {
	if (s->phase != TAP_WAITING || id == s->first || ms - s->since > TAP_WAIT_MS) {
		return false; /* pressed already, or a third finger: it rests */
	}
	s->phase = TAP_TWO;
	s->second = id;
	return true;
}

bool tap_owns(const struct tap_state *s, int32_t id) {
	if (s->phase == TAP_IDLE) {
		return false;
	}
	return id == s->first || (s->phase == TAP_TWO && id == s->second);
}

struct tap_actions tap_motion(struct tap_state *s, int32_t id, double x, double y) {
	struct tap_actions out = { 0 };
	if (!tap_owns(s, id)) {
		return out;
	}
	switch (s->phase) {
	case TAP_WAITING:
		if (far(s, x, y)) {
			/* A drag: pressed where it began, then along. */
			s->phase = TAP_PRESSED;
			add(&out, TAP_PRESS);
			add(&out, TAP_MOVE);
		}
		break;
	case TAP_PRESSED:
		add(&out, TAP_MOVE);
		break;
	case TAP_TWO:
		/* The second finger's own start isn't kept: it gets the same
		 * slop from the first's, which is close by on a tap. */
		if (id == s->first && far(s, x, y)) {
			s->moved = true;
		}
		break;
	case TAP_IDLE:
		break;
	}
	return out;
}

struct tap_actions tap_up(struct tap_state *s, int32_t id, uint32_t ms) {
	struct tap_actions out = { 0 };
	if (!tap_owns(s, id)) {
		return out;
	}
	switch (s->phase) {
	case TAP_WAITING:
		/* A tap: a click. */
		add(&out, TAP_PRESS);
		add(&out, TAP_RELEASE);
		s->phase = TAP_IDLE;
		break;
	case TAP_PRESSED:
		add(&out, TAP_RELEASE);
		s->phase = TAP_IDLE;
		break;
	case TAP_TWO:
		if (id == s->first) {
			s->first_up = true;
		} else {
			s->second_up = true;
		}
		if (s->first_up && s->second_up) {
			if (!s->moved && ms - s->since <= TAP_MAX_MS) {
				add(&out, TAP_RIGHT_CLICK);
			}
			s->phase = TAP_IDLE;
		}
		break;
	case TAP_IDLE:
		break;
	}
	return out;
}

struct tap_actions tap_wait_over(struct tap_state *s) {
	struct tap_actions out = { 0 };
	if (s->phase == TAP_WAITING) {
		/* Held still: the press (a menu, a long press on an icon). */
		s->phase = TAP_PRESSED;
		add(&out, TAP_PRESS);
	}
	return out;
}

struct tap_actions tap_cancel(struct tap_state *s) {
	struct tap_actions out = { 0 };
	if (s->phase == TAP_PRESSED) {
		add(&out, TAP_RELEASE);
	}
	s->phase = TAP_IDLE;
	return out;
}
