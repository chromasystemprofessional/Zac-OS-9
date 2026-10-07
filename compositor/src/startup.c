/*
 * The startup screen, as a classic Mac started up: first the logo on
 * white (the boot before it is all white too, see iso/kernel-params and
 * boot/plymouth, and GRUB shows the same logo in the same place before
 * either), then, while the menu bar and the Finder start, the Welcome box
 * over the desktop pattern: "Welcome to ZacOS 9", a picture of a computer
 * (lib/welcome.c) and a progress bar, with the extensions' icons along the
 * bottom of the screen, as Mac OS 9 showed while it loaded. It lifts once
 * both shell components have put up their surfaces (or after
 * STARTUP_MAX_MS).
 *
 * At boot the splash has shown all this already: boot/zacos9-parade adds
 * each extension's icon as its kernel module loads, both to the splash and
 * to /run/zacos9/parade, and boot/zacos9-boot-handoff keeps the splash's
 * last frame up until this takes over (it writes /run/zacos9/handoff).
 * Then this skips the logo, carries on the bar from where the splash left
 * it, and keeps adding icons as more lines arrive. Without that record (a
 * later login) the loaded extensions march in as the bar fills.
 *
 * The box layout is ours; TODO: the HIG doesn't show the Mac OS 9 one.
 * ZACOS9_STARTUP=0 turns the screen off.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_output_layout.h>

#include "draw.h"
#include "extensions.h"
#include "icons.h"
#include "logo.h"
#include "patterns.h"
#include "pixbuf.h"
#include "server.h"
#include "settings.h"
#include "startup.h"
#include "text.h"
#include "welcome.h"
#include "widgets.h"

#define TICK_MS 40
#define LOGO_MS 500 /* the logo on white, before the Welcome box */
#define MAX_PARADE 64
#define HANDOFF_FRESH_S 60 /* a handoff record older than this is a past boot's */
/* The splash fills this much of the bar; it is the same in zacos9.script. */
#define SPLASH_BAR_SHARE 0.75
#define WELCOME_MIN_MS 1500 /* the Welcome box shows at least this long */
#define HANDOFF_MIN_MS 500   /* ... or this, when the splash showed it already */
#define STARTUP_MAX_MS 6000 /* from the Welcome box */
#define FILL_MS 2500 /* time to reach 90% while waiting */
#define FINISH_MS 300 /* from "ready" to a full bar */
#define HOLD_MS 250   /* the full bar stays up this long */
static struct {
	struct plat_server *server;
	bool active;
	struct wl_event_source *timer;
	long start_ms, welcome_ms, ready_ms;
	bool menubar, desktop;
	bool handoff; /* taking over from the boot splash */
	bool live;    /* the parade comes from the boot's record, as it grows */
	enum pl_icon_kind parade[MAX_PARADE];
	const char *parade_keys[MAX_PARADE];
	int n_parade;
	off_t parade_size;
	int pattern;
	struct plat_text *title, *status;
} st;

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The desktop pattern the Finder will show. */
static int chosen_pattern(void) {
	char id[128];
	return pl_pattern_find(pl_setting("pattern", id, sizeof(id)) ? id : NULL);
}

static const char *path_or(const char *env, const char *fallback) {
	const char *v = getenv(env);
	return v && *v ? v : fallback;
}

static void add_parade(const struct pl_extension *e) {
	if (!e || st.n_parade >= MAX_PARADE) {
		return;
	}
	for (int i = 0; i < st.n_parade; i++) {
		if (st.parade_keys[i] == e->key) {
			return;
		}
	}
	st.parade_keys[st.n_parade] = e->key;
	st.parade[st.n_parade++] = e->icon;
}

/* The boot's record, one extension key a line, in the order they loaded. */
static bool read_parade_record(void) {
	const char *path = path_or("ZACOS9_PARADE_FILE", "/run/zacos9/parade");
	struct stat sb;
	if (stat(path, &sb) != 0) {
		return false;
	}
	if (sb.st_size == st.parade_size) {
		return true;
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	st.parade_size = sb.st_size;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		add_parade(pl_extension_find(line));
	}
	fclose(f);
	return true;
}

/* Without a record: the extensions loaded now, oldest first (the kernel
 * lists the newest first). */
static void read_loaded_modules(void) {
	FILE *f = fopen(path_or("ZACOS9_PROC_MODULES", "/proc/modules"), "r");
	if (!f) {
		return;
	}
	const struct pl_extension *found[512];
	int n = 0;
	char line[512];
	while (n < 512 && fgets(line, sizeof(line), f)) {
		line[strcspn(line, " \n")] = 0;
		const struct pl_extension *e = pl_extension_for_module(line);
		if (e) {
			found[n++] = e;
		}
	}
	fclose(f);
	while (n > 0) {
		add_parade(found[--n]);
	}
}

/* Whether the splash handed over to us just now, this boot. */
static bool handed_off(void) {
	FILE *f = fopen(path_or("ZACOS9_HANDOFF_FILE", "/run/zacos9/handoff"), "r");
	if (!f) {
		return false;
	}
	double at = -1;
	const bool ok = fscanf(f, "%lf", &at) == 1;
	fclose(f);
	struct timespec ts;
	clock_gettime(CLOCK_BOOTTIME, &ts);
	const double now = ts.tv_sec + ts.tv_nsec / 1e9;
	return ok && at >= 0 && now - at >= 0 && now - at < HANDOFF_FRESH_S;
}

static double progress(long t) {
	const double from = st.handoff ? SPLASH_BAR_SHARE : 0;
	double p = from + (double)(t - st.welcome_ms) / FILL_MS * (0.9 - from);
	if (p > 0.9) {
		p = 0.9;
	}
	if (st.ready_ms) {
		double q = (double)(t - st.ready_ms) / FINISH_MS;
		p = p + (1.0 - p) * (q > 1 ? 1 : q);
	}
	return p;
}

/* The HQ logo (64×64), top-left at (x, y), blended with real alpha. */
static void paint_logo(struct pl_canvas *c, int x, int y) {
	pl_image_blend(c, x, y, logo_pixels_hq(), PL_LOGO_SIZE_HQ, PL_LOGO_SIZE_HQ, false);
}

static void draw_logo(struct wlr_scene_buffer *buffer, int w, int h) {
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	struct pl_canvas c = { .px = buf->data, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, C_WHITE);
	const int lw = PL_LOGO_SIZE_HQ;
	paint_logo(&c, (w - lw) / 2, (h - lw) / 2);

	wlr_scene_buffer_set_buffer(buffer, &buf->base);
	wlr_buffer_drop(&buf->base);
}

static void draw(struct wlr_scene_buffer *buffer, int w, int h, double fraction) {
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	struct pl_canvas c = { .px = buf->data, .stride = w, .width = w, .height = h };
	pl_pattern_fill(&c, st.pattern, 0, 0, w - 1, h - 1);
	int bx, by;
	pl_welcome_box_origin(w, h, &bx, &by);
	pl_welcome_box_paint(&c, bx, by, st.title, st.status, fraction, pl_accent_current());

	/* Live from the boot's record, or one more each time the bar passes
	 * another step. */
	int shown = st.n_parade;
	if (!st.live) {
		shown = (int)(fraction * (st.n_parade + 1));
		if (shown > st.n_parade) {
			shown = st.n_parade;
		}
	}
	pl_parade_paint(&c, w, h, st.parade, shown);

	wlr_scene_buffer_set_buffer(buffer, &buf->base);
	wlr_buffer_drop(&buf->base);
}

static void finish(void) {
	wl_event_source_remove(st.timer);
	struct plat_output *output;
	wl_list_for_each(output, &st.server->outputs, link) {
		startup_output_destroy(output);
	}
	text_destroy(st.title);
	text_destroy(st.status);
	st.active = false;
}

void startup_output_frame(struct plat_output *output, bool visible) {
	if (output->startup_buffer) {
		wlr_scene_node_set_enabled(&output->startup_buffer->node, visible);
	}
}

void startup_output_destroy(struct plat_output *output) {
	if (output->startup_buffer) {
		wlr_scene_node_destroy(&output->startup_buffer->node);
	}
}

static void buffer_destroyed(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, startup_buffer_destroy);
	wl_list_remove(&output->startup_buffer_destroy.link);
	output->startup_buffer = NULL;
}

static int tick(void *data) {
	const long t = now_ms();
	const bool welcome = t >= st.welcome_ms;
	if (st.live) {
		read_parade_record();
	}
	const long min_ms = st.handoff ? HANDOFF_MIN_MS : WELCOME_MIN_MS;
	if (welcome && !st.ready_ms && t - st.welcome_ms >= min_ms &&
			((st.menubar && st.desktop) || t - st.welcome_ms > STARTUP_MAX_MS)) {
		st.ready_ms = t;
	}
	if (st.ready_ms && t - st.ready_ms > FINISH_MS + HOLD_MS) {
		finish();
		return 0;
	}
	struct plat_output *output;
	wl_list_for_each(output, &st.server->outputs, link) {
		struct wlr_box box;
		wlr_output_layout_get_box(st.server->output_layout, output->wlr_output, &box);
		if (box.width <= 0 || box.height <= 0) {
			startup_output_destroy(output);
			continue;
		}
		if (!output->startup_buffer) {
			output->startup_buffer = wlr_scene_buffer_create(st.server->overlay_layer, NULL);
			output->startup_buffer_destroy.notify = buffer_destroyed;
			wl_signal_add(&output->startup_buffer->node.events.destroy,
				&output->startup_buffer_destroy);
			wlr_scene_node_set_enabled(&output->startup_buffer->node, false);
		}
		struct wlr_scene_buffer *buffer = output->startup_buffer;
		wlr_scene_node_set_position(&buffer->node, box.x, box.y);
		if (welcome) {
			draw(buffer, box.width, box.height, progress(t));
		} else {
			draw_logo(buffer, box.width, box.height);
		}
		wlr_output_schedule_frame(output->wlr_output);
	}
	wl_event_source_timer_update(st.timer, TICK_MS);
	return 0;
}

void startup_begin(struct plat_server *server) {
	const char *env = getenv("ZACOS9_STARTUP");
	if (env && strcmp(env, "0") == 0) {
		return;
	}
	st.server = server;
	st.active = true;
	st.start_ms = now_ms();
	st.ready_ms = 0;
	st.menubar = st.desktop = false;
	st.handoff = handed_off();
	st.welcome_ms = st.start_ms + (st.handoff ? 0 : LOGO_MS);
	st.n_parade = 0;
	st.parade_size = -1;
	st.live = st.handoff && read_parade_record();
	if (!st.live) {
		read_loaded_modules();
	}
	st.pattern = chosen_pattern();
	st.title = text_render_font("Welcome to ZacOS 9", 1000, PL_FONT_SYSTEM);
	st.status = text_render_font("Starting Up\xe2\x80\xa6", 1000, PL_FONT_VIEWS);
	st.timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), tick, NULL);
	wl_event_source_timer_update(st.timer, 1);
}

void startup_surface_mapped(const char *layer_namespace) {
	if (!st.active || !layer_namespace) {
		return;
	}
	if (strcmp(layer_namespace, "zacos9-menubar") == 0) {
		st.menubar = true;
	} else if (strcmp(layer_namespace, "zacos9-desktop") == 0) {
		st.desktop = true;
	}
}

bool startup_active(void) {
	return st.active;
}
