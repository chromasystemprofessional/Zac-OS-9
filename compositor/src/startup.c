/*
 * The startup screen, as a classic Mac started up: first the logo on
 * white (the boot before it is all white too, see iso/kernel-params and
 * boot/plymouth), then, while the menu bar and the Finder start, a
 * "Welcome" box over the desktop pattern, with the logo and a progress
 * bar, as Mac OS 8 showed while it loaded. It lifts once both shell
 * components have put up their surfaces (or after STARTUP_MAX_MS).
 *
 * The box layout is ours; TODO: the HIG doesn't show the Mac OS 8 one.
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
#include "widgets.h"

#define TICK_MS 40
#define LOGO_MS 1500 /* the logo on white, before the Welcome box */
#define LOGO_SCALE 2
/* Extension parade: icons march left-to-right during the logo phase. */
#define EXT_ICON_SIZE   32
#define EXT_ICON_GAP    10  /* pixels between icons */
#define EXT_ICON_BOTTOM 12  /* gap from screen bottom */
#define EXT_FIRST_MS   300  /* first icon appears this many ms after start */
#define EXT_STEP_MS    140  /* each subsequent icon appears this many ms later */

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
#define BOX_W 320
#define BOX_H 168
#define BAR_W 220

static struct {
	struct plat_server *server;
	struct wlr_scene_buffer *buffer;
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

/* The logo at `scale`, top-left at (x, y). */
static void paint_logo(struct pl_canvas *c, int x, int y, int scale) {
	const int lw = PL_LOGO_SIZE * scale;
	const uint32_t *logo = logo_pixels();
	for (int ly = 0; ly < lw; ly++) {
		for (int lx = 0; lx < lw; lx++) {
			uint32_t v = logo[(ly / scale) * PL_LOGO_SIZE + lx / scale];
			if (v >> 24) {
				pl_put(c, x + lx, y + ly, v);
			}
		}
	}
}

static void draw_logo(int w, int h, long t) {
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	struct pl_canvas c = { .px = buf->data, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, C_WHITE);
	const int lw = PL_LOGO_SIZE * LOGO_SCALE;
	paint_logo(&c, (w - lw) / 2, (h - lw) / 2, LOGO_SCALE);

	/* Extension parade: icons appear one by one from left to right. */
	long elapsed = t - st.start_ms;
	int n_shown = 0;
	if (elapsed >= EXT_FIRST_MS) {
		n_shown = (int)((elapsed - EXT_FIRST_MS) / EXT_STEP_MS) + 1;
		if (n_shown > N_EXT_ICONS) n_shown = N_EXT_ICONS;
	}
	if (n_shown > 0) {
		const int stride = EXT_ICON_SIZE + EXT_ICON_GAP;
		const int total_w = N_EXT_ICONS * stride - EXT_ICON_GAP;
		const int x0 = (w - total_w) / 2;
		const int iy = h - EXT_ICON_SIZE - EXT_ICON_BOTTOM;
		for (int i = 0; i < n_shown; i++) {
			const uint32_t *px = pl_icon(ext_icons[i], EXT_ICON_SIZE);
			const int ix = x0 + i * stride;
			for (int py = 0; py < EXT_ICON_SIZE; py++) {
				for (int ipx = 0; ipx < EXT_ICON_SIZE; ipx++) {
					uint32_t v = px[py * EXT_ICON_SIZE + ipx];
					if (v >> 24) pl_put(&c, ix + ipx, iy + py, v);
				}
			}
		}
	}

	wlr_scene_buffer_set_buffer(st.buffer, &buf->base);
	wlr_buffer_drop(&buf->base);
}

static void draw(int w, int h, double fraction) {
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

	/* The logo at 3x. */
	const int scale = 3, lw = PL_LOGO_SIZE * scale;
	const int ly = by + 18;
	paint_logo(&c, bx + (BOX_W - lw) / 2, ly, scale);
	const int tw = st.title->ink_r - st.title->ink_l + 1;
	pl_text(&c, st.title, bx + (BOX_W - tw) / 2, ly + lw + 22, C_BLACK);
	const int sw = st.status->ink_r - st.status->ink_l + 1;
	pl_text(&c, st.status, bx + (BOX_W - sw) / 2, ly + lw + 42, C_BLACK);
	pl_progress_paint(&c, bx + (BOX_W - BAR_W) / 2, y1 - 30, BAR_W, fraction,
		pl_accent_current());

	wlr_scene_buffer_set_buffer(st.buffer, &buf->base);
	wlr_buffer_drop(&buf->base);
}

static void finish(void) {
	wl_event_source_remove(st.timer);
	wlr_scene_node_destroy(&st.buffer->node);
	text_destroy(st.title);
	text_destroy(st.status);
	st.buffer = NULL;
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
	struct wlr_box box;
	wlr_output_layout_get_box(st.server->output_layout, NULL, &box);
	if (box.width > 0 && box.height > 0) {
		wlr_scene_node_set_position(&st.buffer->node, box.x, box.y);
		if (welcome) {
			draw(box.width, box.height, progress(t));
		} else {
			draw_logo(box.width, box.height, t);
		}
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
	st.start_ms = now_ms();
	st.welcome_ms = st.start_ms + LOGO_MS;
	st.pattern = chosen_pattern();
	st.title = text_render_font("ZacOS 9", 1000, PL_FONT_SYSTEM);
	st.status = text_render_font("Starting Up\xe2\x80\xa6", 1000, PL_FONT_VIEWS);
	st.buffer = wlr_scene_buffer_create(server->overlay_layer, NULL);
	st.timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), tick, NULL);
	wl_event_source_timer_update(st.timer, 1);
}

void startup_surface_mapped(const char *layer_namespace) {
	if (!st.buffer || !layer_namespace) {
		return;
	}
	if (strcmp(layer_namespace, "zacos9-menubar") == 0) {
		st.menubar = true;
	} else if (strcmp(layer_namespace, "zacos9-desktop") == 0) {
		st.desktop = true;
	}
}

bool startup_active(void) {
	return st.buffer != NULL;
}
