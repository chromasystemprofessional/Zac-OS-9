#ifndef ZACOS9_WINDOW_SOUNDS_H
#define ZACOS9_WINDOW_SOUNDS_H

#include <stdbool.h>

struct window_sound_state {
	bool moving;
};

void window_sound_move(struct window_sound_state *state);
void window_sound_finish(struct window_sound_state *state, bool committed);
void window_sound_collapse(bool collapsed);

#endif
