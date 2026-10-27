#include <assert.h>
#include <string.h>

#include "window_sounds.h"

static int starts, stops, events;
static const char *last_event;

void pl_sound_loop_start(const char *event) {
	assert(strcmp(event, "window-drag") == 0);
	++starts;
}

void pl_sound_loop_stop(void) {
	++stops;
}

void pl_sound_event(const char *event) {
	++events;
	last_event = event;
}

int main(void) {
	struct window_sound_state state = {0};
	window_sound_finish(&state, true);
	assert(starts == 0 && stops == 0 && events == 0);
	for (int i = 0; i < 100; ++i) {
		window_sound_move(&state);
	}
	assert(state.moving && starts == 1);
	window_sound_finish(&state, true);
	assert(!state.moving && stops == 1 && events == 1);
	assert(strcmp(last_event, "window-drag-end") == 0);
	window_sound_finish(&state, true);
	assert(stops == 1 && events == 1);
	window_sound_move(&state);
	window_sound_finish(&state, false);
	assert(starts == 2 && stops == 2 && events == 1 && !state.moving);
	window_sound_collapse(true);
	assert(strcmp(last_event, "window-collapse") == 0);
	window_sound_collapse(false);
	assert(strcmp(last_event, "window-expand") == 0);
	assert(events == 3);
	return 0;
}
