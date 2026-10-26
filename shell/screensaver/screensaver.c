/*
 * zacos9-screensaver: the screen saver. zacos9-wm starts it when a screen
 * reaches the screen saver's turn (compositor/src/idle.c); it covers every
 * screen that is in the "saver" state of $XDG_RUNTIME_DIR/zacos9-idle-
 * $WAYLAND_DISPLAY with an overlay layer surface (namespace "screensaver"),
 * drawing the saver chosen in desktop.conf ("screensaver=ID", lib/savers.c)
 * at the screen's logical size, scaled up nearest-neighbour. It hides the
 * pointer over them. Any input wakes the screens (zacos9-wm hides these
 * surfaces at once); it takes its surfaces down as the file changes and
 * exits when no screen is in the saver state. See docs/energy-saver.md.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "draw.h"
#include "fractional-scale-v1-client-protocol.h"
#include "savers.h"
#include "settings.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define FRAME_NS (1000000000L / 25) /* 25 frames a second */

struct buffer {
	struct wl_buffer *wl;
	uint32_t *px;
	size_t size;
	bool busy;
};

struct screen {
	struct wl_list link;
	uint32_t global;
	struct wl_output *output;
	char *name;
	int scale;
	bool wanted; /* in the saver state */
	/* While shown: */
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	/* 1.25, 1.5, 1.75 (5, 6, 7): drawn at the screen's own pixels; else 0. */
	int quarters;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;
	int w, h; /* logical size, once configured */
	struct pl_saver *saver;
	uint32_t *logical; /* the frame at logical size, for scale > 1 or a fraction */
	struct buffer buffers[2];
	struct wl_callback *frame; /* the last frame not yet shown */
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wp_viewporter *viewporter;
static struct wp_fractional_scale_manager_v1 *fractional_manager;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_pointer *pointer;
static struct wl_list screens; /* screen.link */
static char state_path[1024], state_name[256];
static double started;
static int saver_index;

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- buffers ------------------------------------------------------------------ */

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	((struct buffer *)data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void buffer_free(struct buffer *b) {
	if (b->wl) {
		wl_buffer_destroy(b->wl);
		munmap(b->px, b->size);
	}
	memset(b, 0, sizeof(*b));
}

static bool buffer_make(struct buffer *b, int width, int height) {
	const int stride = width * 4;
	b->size = (size_t)stride * height;
	const int fd = memfd_create("zacos9-screensaver", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)b->size) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return false;
	}
	b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->px == MAP_FAILED) {
		close(fd);
		b->px = NULL;
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	return true;
}

/* ---- drawing ------------------------------------------------------------------ */

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
	struct screen *s = data;
	wl_callback_destroy(callback);
	s->frame = NULL;
}

static const struct wl_callback_listener frame_listener = { frame_done };

/* The next frame, unless the last isn't on screen yet. */
static void draw(struct screen *s) {
	if (!s->saver || s->frame) {
		return;
	}
	struct buffer *b = !s->buffers[0].busy ? &s->buffers[0] : !s->buffers[1].busy ? &s->buffers[1] : NULL;
	if (!b) {
		return;
	}
	const int k = s->scale;
	if (s->quarters) {
		/* Each logical pixel one or two of the screen's, any text in the
		 * font for this scale: sharp. */
		struct pl_canvas c = { s->logical, s->w, 0, 0, s->w, s->h, pl_text_log_new(s->logical, s->w) };
		pl_saver_draw(s->saver, &c, now_seconds() - started);
		pl_snap_scale_canvas(&c, 0, 0, s->quarters, b->px);
		pl_text_log_free(c.log);
	} else if (k == 1) {
		struct pl_canvas c = { b->px, s->w, 0, 0, s->w, s->h, NULL };
		pl_saver_draw(s->saver, &c, now_seconds() - started);
	} else {
		/* At logical size, then each pixel k×k: sharp, like the rest. */
		struct pl_canvas c = { s->logical, s->w, 0, 0, s->w, s->h, NULL };
		pl_saver_draw(s->saver, &c, now_seconds() - started);
		for (int y = 0; y < s->h; y++) {
			uint32_t *out = b->px + (size_t)y * k * s->w * k;
			const uint32_t *in = s->logical + (size_t)y * s->w;
			for (int x = 0; x < s->w; x++) {
				for (int i = 0; i < k; i++) {
					out[x * k + i] = in[x];
				}
			}
			for (int i = 1; i < k; i++) {
				memcpy(out + (size_t)i * s->w * k, out, (size_t)s->w * k * 4);
			}
		}
	}
	b->busy = true;
	wl_surface_attach(s->surface, b->wl, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, INT32_MAX, INT32_MAX);
	s->frame = wl_surface_frame(s->surface);
	wl_callback_add_listener(s->frame, &frame_listener, s);
	wl_surface_commit(s->surface);
}

/* ---- showing and hiding ---------------------------------------------------- */

static void hide(struct screen *s) {
	if (!s->surface) {
		return;
	}
	if (s->frame) {
		wl_callback_destroy(s->frame);
		s->frame = NULL;
	}
	zwlr_layer_surface_v1_destroy(s->layer);
	if (s->fractional) {
		wp_fractional_scale_v1_destroy(s->fractional);
		wp_viewport_destroy(s->viewport);
		s->fractional = NULL;
		s->viewport = NULL;
	}
	wl_surface_destroy(s->surface);
	s->surface = NULL;
	s->layer = NULL;
	buffer_free(&s->buffers[0]);
	buffer_free(&s->buffers[1]);
	pl_saver_free(s->saver);
	s->saver = NULL;
	free(s->logical);
	s->logical = NULL;
}

static void start(struct screen *s);

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial,
		uint32_t w, uint32_t h) {
	struct screen *s = data;
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	if ((int)w == s->w && (int)h == s->h && s->saver) {
		return;
	}
	s->w = (int)w;
	s->h = (int)h;
	start(s);
}

/* Buffers at the screen's scale (or its own pixels at a fraction) and a
 * new saver for its size. */
static void start(struct screen *s) {
	buffer_free(&s->buffers[0]);
	buffer_free(&s->buffers[1]);
	pl_saver_free(s->saver);
	s->saver = NULL;
	free(s->logical);
	const bool scaled = s->scale > 1 || s->quarters;
	const int bw = s->quarters ? pl_snap(s->w, s->quarters) : s->w * s->scale;
	const int bh = s->quarters ? pl_snap(s->h, s->quarters) : s->h * s->scale;
	s->logical = scaled ? malloc((size_t)s->w * s->h * 4) : NULL;
	if (!buffer_make(&s->buffers[0], bw, bh) || !buffer_make(&s->buffers[1], bw, bh) ||
			(scaled && !s->logical)) {
		fprintf(stderr, "zacos9-screensaver: no memory for %s\n", s->name);
		return;
	}
	s->saver = pl_saver_new(saver_index, s->w, s->h, (unsigned)(now_seconds() * 1000) ^ s->global);
	wl_surface_set_buffer_scale(s->surface, s->quarters ? 1 : s->scale);
	if (s->viewport) {
		wp_viewport_set_destination(s->viewport, s->quarters ? s->w : -1, s->quarters ? s->h : -1);
	}
	draw(s);
}

/* 1.25, 1.5, 1.75 (in 120ths): the screen's own pixels from now on. */
static void preferred_scale(void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
	struct screen *s = data;
	const int quarters = (int)((scale + 15) / 30);
	const int q = quarters % 4 ? quarters : 0;
	if (q == s->quarters) {
		return;
	}
	s->quarters = q;
	if (s->w > 0) {
		start(s);
	}
}

static const struct wp_fractional_scale_v1_listener fractional_listener = { preferred_scale };

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	hide(data);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = { layer_configure, layer_closed };

static void show(struct screen *s) {
	if (s->surface || !s->name) {
		return;
	}
	s->surface = wl_compositor_create_surface(compositor);
	s->quarters = 0;
	if (viewporter && fractional_manager) {
		s->viewport = wp_viewporter_get_viewport(viewporter, s->surface);
		s->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(fractional_manager,
			s->surface);
		wp_fractional_scale_v1_add_listener(s->fractional, &fractional_listener, s);
	}
	s->layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, s->surface, s->output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "screensaver");
	zwlr_layer_surface_v1_add_listener(s->layer, &layer_listener, s);
	zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(s->layer, -1); /* over the menu bar too */
	zwlr_layer_surface_v1_set_keyboard_interactivity(s->layer,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	s->w = s->h = 0;
	wl_surface_commit(s->surface);
}

/* Which screens are in the saver state now; false if none is. */
static bool read_states(void) {
	struct screen *s;
	wl_list_for_each(s, &screens, link) {
		s->wanted = false;
	}
	FILE *f = fopen(state_path, "r");
	if (!f) {
		return false;
	}
	bool any = false;
	char line[512], name[256], state[32];
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "output %255s %31s", name, state) != 2 || strcmp(state, "saver") != 0) {
			continue;
		}
		wl_list_for_each(s, &screens, link) {
			if (s->name && strcmp(s->name, name) == 0) {
				s->wanted = any = true;
			}
		}
	}
	fclose(f);
	return any;
}

static void apply_states(void) {
	struct screen *s;
	wl_list_for_each(s, &screens, link) {
		if (s->wanted) {
			show(s);
		} else {
			hide(s);
		}
	}
}

/* ---- Wayland ------------------------------------------------------------------ */

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
		int32_t ph, int32_t subpixel, const char *make, const char *model, int32_t transform) {
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
		int32_t refresh) {
}

static void output_done(void *data, struct wl_output *o) {
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
	((struct screen *)data)->scale = factor > 0 ? factor : 1;
}

static void output_name(void *data, struct wl_output *o, const char *name) {
	struct screen *s = data;
	free(s->name);
	s->name = strdup(name);
}

static void output_description(void *data, struct wl_output *o, const char *description) {
}

static const struct wl_output_listener output_listener = {
	output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	wl_pointer_set_cursor(p, serial, NULL, 0, 0); /* no pointer over the saver */
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface) {
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
		uint32_t button, uint32_t state) {
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
	.button = pointer_button, .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
}

static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *iface,
		uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	} else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		fractional_manager = wl_registry_bind(registry, name,
			&wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		struct wl_seat *seat = wl_registry_bind(registry, name, &wl_seat_interface, 2);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && version >= 4) {
		struct screen *s = calloc(1, sizeof(*s));
		s->global = name;
		s->scale = 1;
		s->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(s->output, &output_listener, s);
		wl_list_insert(&screens, &s->link);
	}
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	struct screen *s, *tmp;
	wl_list_for_each_safe(s, tmp, &screens, link) {
		if (s->global == name) {
			hide(s);
			wl_output_release(s->output);
			wl_list_remove(&s->link);
			free(s->name);
			free(s);
		}
	}
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

int main(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *wayland = getenv("WAYLAND_DISPLAY");
	if (!runtime || !wayland) {
		fprintf(stderr, "zacos9-screensaver: no XDG_RUNTIME_DIR or WAYLAND_DISPLAY\n");
		return 1;
	}
	snprintf(state_name, sizeof(state_name), "zacos9-idle-%s", wayland);
	snprintf(state_path, sizeof(state_path), "%s/%s", runtime, state_name);
	char id[64] = "";
	pl_setting("screensaver", id, sizeof(id));
	saver_index = pl_saver_find(id);
	started = now_seconds();

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "zacos9-screensaver: no Wayland display\n");
		return 1;
	}
	wl_list_init(&screens);
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display); /* the outputs' names */
	if (!compositor || !shm || !layer_shell) {
		fprintf(stderr, "zacos9-screensaver: the compositor lacks layer-shell\n");
		return 1;
	}

	/* The idle file is replaced (renamed into place) on every change. */
	const int inotify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	inotify_add_watch(inotify, runtime, IN_MOVED_TO | IN_CLOSE_WRITE);
	const int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	const struct itimerspec every = { { 0, FRAME_NS }, { 0, FRAME_NS } };
	timerfd_settime(timer, 0, &every, NULL);

	if (!read_states()) {
		return 0; /* woken already */
	}
	apply_states();
	for (;;) {
		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);
		struct pollfd fds[3] = {
			{ wl_display_get_fd(display), POLLIN, 0 }, { inotify, POLLIN, 0 }, { timer, POLLIN, 0 },
		};
		if (poll(fds, 3, -1) < 0 && errno != EINTR) {
			wl_display_cancel_read(display);
			return 1;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0) {
				return 0; /* zacos9-wm has gone */
			}
		} else {
			wl_display_cancel_read(display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP) || wl_display_dispatch_pending(display) < 0) {
			return 0;
		}
		if (fds[1].revents & POLLIN) {
			char events[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
			bool ours = false;
			ssize_t n;
			while ((n = read(inotify, events, sizeof(events))) > 0) {
				for (char *p = events; p < events + n;) {
					const struct inotify_event *e = (const struct inotify_event *)p;
					ours |= e->len && strcmp(e->name, state_name) == 0;
					p += sizeof(*e) + e->len;
				}
			}
			if (ours) {
				if (!read_states()) {
					return 0; /* every screen woke, or went to sleep */
				}
				apply_states();
			}
		}
		if (fds[2].revents & POLLIN) {
			uint64_t ticks;
			if (read(timer, &ticks, sizeof(ticks)) < 0) {
				ticks = 0;
			}
			struct screen *s;
			wl_list_for_each(s, &screens, link) {
				draw(s);
			}
		}
	}
}
