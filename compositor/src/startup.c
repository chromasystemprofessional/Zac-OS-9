/*
 * The startup screen, as a classic Mac started up: first the logo on
 * white (the boot before it is all white too, see iso/kernel-params and
 * boot/plymouth, and GRUB shows the same logo in the same place before
 * either), then, while the menu bar and the Finder start, the Welcome box
 * over the desktop pattern: "Welcome to ZacOS 9", a picture of a computer
 * (lib/welcome.c) and a progress bar, with the extensions' icons marching
 * in along the bottom of the screen as it fills, as Mac OS 9 showed while
 * it loaded. It lifts once both shell components have put up their
 * surfaces (or after STARTUP_MAX_MS).
 *
 * The box layout is ours; TODO: the HIG doesn't show the Mac OS 9 one.
 * ZACOS9_STARTUP=0 turns the screen off.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_output_layout.h>

#include "draw.h"
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
/* The extensions' icons, along the bottom of the screen: one more each
 * time the progress bar passes another seventh. */
#define EXT_ICON_SIZE   32
#define EXT_ICON_GAP    8   /* pixels between icons */
#define EXT_ICON_BOTTOM 12  /* gap from the screen's bottom */
#define EXT_ICON_LEFT   16  /* gap from the screen's left */

static const enum pl_icon_kind ext_icons[] = {
	PL_ICON_EXT_OPENTRANSPORT,
	PL_ICON_EXT_APPLETALK,
	PL_ICON_SHARED_FOLDER,
	PL_ICON_EXT_BLUETOOTH,
	PL_ICON_EXT_AUDIO,
	PL_ICON_EXT_PRINTMONITOR,
};
#define N_EXT_ICONS ((int)(sizeof(ext_icons) / sizeof(ext_icons[0])))
#define WELCOME_MIN_MS 1500 /* the Welcome box shows at least this long */
#define STARTUP_MAX_MS 6000 /* from the Welcome box */
#define FILL_MS 2500 /* time to reach 90% while waiting */
#define FINISH_MS 300 /* from "ready" to a full bar */
#define HOLD_MS 250   /* the full bar stays up this long */
/* The Welcome box: the title, the picture in a white well, the status
 * line, the bar. */
#define BOX_W 320
#define WELL_W 204
#define WELL_H 156
#define WELL_TOP 42
#define BAR_W 220
#define BOX_H (WELL_TOP + WELL_H + 26 + 10 + PL_PROGRESS_H + 24)

static struct {
	struct plat_server *server;
	bool active;
	struct wl_event_source *timer;
	long start_ms, welcome_ms, ready_ms;
	bool menubar, desktop;
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

static double progress(long t) {
	double p = (double)(t - st.welcome_ms) / FILL_MS * 0.9;
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

/* The extensions' icons, left to right along the bottom. */
static void paint_extensions(struct pl_canvas *c, int h, double fraction) {
	int shown = (int)(fraction * (N_EXT_ICONS + 1));
	if (shown > N_EXT_ICONS) {
		shown = N_EXT_ICONS;
	}
	const int y = h - EXT_ICON_SIZE - EXT_ICON_BOTTOM;
	for (int i = 0; i < shown; i++) {
		const uint32_t *px = pl_icon(ext_icons[i], EXT_ICON_SIZE);
		const int x = EXT_ICON_LEFT + i * (EXT_ICON_SIZE + EXT_ICON_GAP);
		for (int py = 0; py < EXT_ICON_SIZE; py++) {
			for (int ix = 0; ix < EXT_ICON_SIZE; ix++) {
				const uint32_t v = px[py * EXT_ICON_SIZE + ix];
				if (v >> 24) {
					pl_put(c, x + ix, y + py, v);
				}
			}
		}
	}
}

static void draw(struct wlr_scene_buffer *buffer, int w, int h, double fraction) {
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	struct pl_canvas c = { .px = buf->data, .stride = w, .width = w, .height = h };
	pl_pattern_fill(&c, st.pattern, 0, 0, w - 1, h - 1);

	/* A raised Platinum box with a one-pixel shadow. */
	const int bx = (w - BOX_W) / 2, by = (h - BOX_H) / 2;
	const int x1 = bx + BOX_W - 1, y1 = by + BOX_H - 1;
	pl_fill(&c, bx + 2, by + 2, x1 + 2, y1 + 2, GRAY(0x2));
	pl_fill(&c, bx, by, x1, y1, GRAY(0xD));
	pl_outline(&c, bx, by, x1, y1, C_BLACK);
	pl_hline(&c, bx + 1, x1 - 1, by + 1, C_WHITE);
	pl_vline(&c, bx + 1, by + 1, y1 - 1, C_WHITE);
	pl_hline(&c, bx + 2, x1 - 1, y1 - 1, GRAY(0x9));
	pl_vline(&c, x1 - 1, by + 2, y1 - 1, GRAY(0x9));

	/* "Welcome to ZacOS 9". */
	const int tw = st.title->ink_r - st.title->ink_l + 1;
	pl_text(&c, st.title, bx + (BOX_W - tw) / 2, by + 28, C_BLACK);

	/* The computer, in a white well. */
	const int wx = bx + (BOX_W - WELL_W) / 2, wy = by + WELL_TOP;
	pl_outline(&c, wx - 1, wy - 1, wx + WELL_W, wy + WELL_H, GRAY(0x6));
	pl_fill(&c, wx, wy, wx + WELL_W - 1, wy + WELL_H - 1, C_WHITE);
	pl_welcome_art(&c, wx + (WELL_W - PL_WELCOME_ART_W) / 2,
		wy + (WELL_H - PL_WELCOME_ART_H) / 2, 1.0);

	/* What it is doing, and how far it has got. */
	const int sw = st.status->ink_r - st.status->ink_l + 1;
	pl_text(&c, st.status, bx + (BOX_W - sw) / 2, wy + WELL_H + 24, C_BLACK);
	pl_progress_paint(&c, bx + (BOX_W - BAR_W) / 2, wy + WELL_H + 34, BAR_W, fraction,
		pl_accent_current());

	paint_extensions(&c, h, fraction);

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
	if (welcome && !st.ready_ms && t - st.welcome_ms >= WELCOME_MIN_MS &&
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
	st.welcome_ms = st.start_ms + LOGO_MS;
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
