/*
 * Mouse Tracks (the Mouse control panel, as on Mac OS PowerBooks): while
 * the pointer moves, a short trail of arrows follows it, so it's easier
 * to see on a dim or slow screen. Each copy goes again a moment later.
 */
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "pixbuf.h"
#include "server.h"

#define TRACKS 6        /* arrows in the trail, the pointer's own not counted */
#define TRACK_GAP 10    /* pixels the pointer moves between two of them */
#define TRACK_FADE_MS 50 /* each tick, the oldest goes */

static struct {
	struct plat_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *arrows[TRACKS];
	double x[TRACKS], y[TRACKS]; /* hot spots, newest first */
	int n;
	bool resting; /* no trail: the next move starts one from where it is */
	double last_x, last_y;
	struct wl_event_source *fade;
	int scale; /* of the arrow image made */
	int hot_x, hot_y, w, h;
} t;

/* The theme's arrow, at the outputs' scale, as a wlr_buffer for the trail. */
static struct wlr_buffer *arrow(void) {
	struct plat_server *server = t.server;
	const int scale = server->output_scale > 0 ? server->output_scale : 1;
	struct wlr_xcursor *cursor = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", scale);
	if (!cursor || cursor->image_count == 0) {
		return NULL;
	}
	struct wlr_xcursor_image *img = cursor->images[0];
	struct plat_pixbuf *buf = pixbuf_create((int)img->width, (int)img->height);
	if (!buf) {
		return NULL;
	}
	memcpy(buf->data, img->buffer, (size_t)img->width * img->height * 4);
	t.scale = scale;
	t.w = (int)img->width / scale;
	t.h = (int)img->height / scale;
	t.hot_x = (int)img->hotspot_x / scale;
	t.hot_y = (int)img->hotspot_y / scale;
	return &buf->base;
}

static void place(void) {
	for (int i = 0; i < TRACKS; i++) {
		struct wlr_scene_node *node = &t.arrows[i]->node;
		wlr_scene_node_set_enabled(node, i < t.n);
		if (i < t.n) {
			wlr_scene_node_set_position(node, (int)t.x[i] - t.hot_x, (int)t.y[i] - t.hot_y);
		}
	}
}

static int fade(void *data) {
	if (t.n > 0) {
		t.n--;
		place();
	}
	if (t.n > 0) {
		wl_event_source_timer_update(t.fade, TRACK_FADE_MS);
	} else {
		t.resting = true;
	}
	return 0;
}

void tracks_init(struct plat_server *server) {
	t.server = server;
	t.tree = wlr_scene_tree_create(server->overlay_layer);
	for (int i = 0; i < TRACKS; i++) {
		t.arrows[i] = wlr_scene_buffer_create(t.tree, NULL);
		wlr_scene_buffer_set_filter_mode(t.arrows[i], WLR_SCALE_FILTER_NEAREST);
		wlr_scene_node_set_enabled(&t.arrows[i]->node, false);
	}
	t.fade = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), fade, NULL);
	t.resting = true;
}

void tracks_moved(struct plat_server *server) {
	if (!server->mouse_tracks || server->cursor_mode != PLAT_CURSOR_PASSTHROUGH ||
			lock_active(server)) {
		if (t.n > 0) {
			t.n = 0;
			place();
		}
		t.resting = true;
		return;
	}
	const double x = server->cursor->x, y = server->cursor->y;
	if (t.resting) {
		t.resting = false;
		t.last_x = x;
		t.last_y = y;
		return;
	}
	const double dx = x - t.last_x, dy = y - t.last_y;
	if (dx * dx + dy * dy < TRACK_GAP * TRACK_GAP) {
		return;
	}
	const int scale = server->output_scale > 0 ? server->output_scale : 1;
	if (scale != t.scale || !t.arrows[0]->buffer) {
		struct wlr_buffer *buf = arrow();
		if (!buf) {
			return;
		}
		for (int i = 0; i < TRACKS; i++) {
			wlr_scene_buffer_set_buffer(t.arrows[i], buf);
			wlr_scene_buffer_set_dest_size(t.arrows[i], t.w, t.h);
		}
		wlr_buffer_drop(buf);
	}
	/* Where the pointer was becomes the newest of the trail. */
	memmove(t.x + 1, t.x, sizeof(t.x[0]) * (TRACKS - 1));
	memmove(t.y + 1, t.y, sizeof(t.y[0]) * (TRACKS - 1));
	t.x[0] = t.last_x;
	t.y[0] = t.last_y;
	t.last_x = x;
	t.last_y = y;
	if (t.n < TRACKS) {
		t.n++;
	}
	place();
	wl_event_source_timer_update(t.fade, TRACK_FADE_MS);
}
