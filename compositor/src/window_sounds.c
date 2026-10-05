#include "window_sounds.h"
#include "settings.h"

void window_sound_move(struct window_sound_state *state) {
	if (!state->moving) {
		state->moving = true;
		pl_sound_loop_start("window-drag");
	}
}

void window_sound_finish(struct window_sound_state *state, bool committed) {
	if (!state->moving) {
		return;
	}
	state->moving = false;
	pl_sound_loop_stop();
	if (committed) {
		pl_sound_event("window-drag-end");
	}
}

void window_sound_collapse(bool collapsed) {
	pl_sound_event(collapsed ? "window-collapse" : "window-expand");
}
