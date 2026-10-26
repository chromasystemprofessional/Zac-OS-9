#ifndef PLATINUM_WLCLIENT_H
#define PLATINUM_WLCLIENT_H

/*
 * Wayland client plumbing the desktop's C programs share: the on-screen
 * keyboard, the lock screen, the screen saver and the menu bar. Each draws
 * into pixel buffers in shared memory that it hands to zacos9-wm; most keep
 * a pair, drawing into whichever the compositor has given back.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_buffer;
struct wl_shm;

struct pl_shm_buffer {
	struct wl_buffer *wl;
	uint32_t *px;          /* width x height pixels, mapped */
	size_t size;           /* in bytes */
	bool busy;             /* the compositor has it: draw into the other one */
	/* Called when the compositor gives it back (busy is false by then),
	 * if set: the lock screen redraws what changed meanwhile, the menu
	 * bar frees its one-off buffers. */
	void (*released)(struct pl_shm_buffer *b, void *data);
	void *data;
};

/* A width x height buffer of `format` (WL_SHM_FORMAT_*) in shared memory,
 * its memory file named `name` (what /proc shows). False, with b left
 * empty, if it couldn't be made. `released` and `data` are kept. */
bool pl_shm_buffer_make(struct pl_shm_buffer *b, struct wl_shm *shm, const char *name,
	int width, int height, uint32_t format);
/* Destroys it and unmaps its pixels; b is left all empty, the released
 * hook too. */
void pl_shm_buffer_free(struct pl_shm_buffer *b);
/* Of a pair, the first the compositor isn't using, or NULL if it has both. */
struct pl_shm_buffer *pl_shm_buffer_pick(struct pl_shm_buffer pair[2]);

#endif
