#include "idle_policy.h"

/* The screen saver only shows before the display sleeps. */
static bool saver_shows(const struct idle_timeouts *t) {
	return t->saver_ms > 0 && (t->display_ms <= 0 || t->saver_ms < t->display_ms);
}

enum idle_state idle_output_state(const struct idle_timeouts *t, long idle_ms, bool kept) {
	if (kept) {
		return IDLE_AWAKE;
	}
	if (t->display_ms > 0 && idle_ms >= t->display_ms) {
		return IDLE_ASLEEP;
	}
	if (saver_shows(t) && idle_ms >= t->saver_ms) {
		return IDLE_SAVER;
	}
	return IDLE_AWAKE;
}

long idle_output_next(const struct idle_timeouts *t, long idle_ms, bool kept) {
	if (kept) {
		return -1;
	}
	if (saver_shows(t) && idle_ms < t->saver_ms) {
		return t->saver_ms - idle_ms;
	}
	if (t->display_ms > 0 && idle_ms < t->display_ms) {
		return t->display_ms - idle_ms;
	}
	return -1;
}

bool idle_system_due(const struct idle_timeouts *t, long idle_ms, bool kept) {
	return !kept && t->system_ms > 0 && idle_ms >= t->system_ms;
}

long idle_system_next(const struct idle_timeouts *t, long idle_ms, bool kept) {
	if (kept || t->system_ms <= 0) {
		return -1;
	}
	return idle_ms < t->system_ms ? t->system_ms - idle_ms : 0;
}
