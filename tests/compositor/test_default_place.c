#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "frame.h"
#include "server.h"

static struct wlr_box usable;
static int style = -1;
static bool positioned, parent;
static int limits[4];

static struct wlr_surface *get_surface(struct plat_view *view) {
	return NULL;
}

static void get_limits(struct plat_view *view, int *min_w, int *min_h,
		int *max_w, int *max_h) {
	*min_w = limits[0];
	*min_h = limits[1];
	*max_w = limits[2];
	*max_h = limits[3];
}

static bool has_parent(struct plat_view *view) {
	return parent;
}

int platinum_shell_style_for(struct wlr_surface *surface) {
	return style;
}

bool platinum_shell_position_for(struct wlr_surface *surface, int *x, int *y) {
	*x = *y = 0;
	return positioned;
}

struct wlr_box output_usable_area(struct plat_server *server, double lx, double ly) {
	return usable;
}

static const struct plat_view_impl impl = {
	.get_surface = get_surface,
	.get_size_limits = get_limits,
	.has_parent = has_parent,
};

int main(void) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_cursor *cursor = wlr_cursor_create();
	struct plat_server server = {
		.view_layer = wlr_scene_tree_create(&scene->tree),
		.dialog_layer = wlr_scene_tree_create(&scene->tree),
		.cursor = cursor,
	};
	struct plat_view view = {0};
	view_setup(&view, &server, PLAT_VIEW_XDG, &impl);

	/* 1920x1080 below a 20 px menu bar. */
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 1920, .height = 1060 };
	int x, y;
	assert(view_default_place(&view, "org.gnome.TextEditor", &x, &y));
	assert(x == 96 && y == 20 + 32);

	/* Small screens keep proportionate strips. */
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 320, .height = 100 };
	assert(view_default_place(&view, "app", &x, &y));
	assert(x == 40 && y == 20 + 12);
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 1920, .height = 1060 };

	/* Left alone: our programs that place their own windows, hinted
	 * windows, windows with a parent, fixed-size windows and fullscreen
	 * windows. */
	assert(!view_default_place(&view, "zacos9-finder", &x, &y));
	assert(!view_default_place(&view, "zacos9-picture", &x, &y));
	style = DECOR_STYLE_DOCUMENT;
	assert(!view_default_place(&view, "app", &x, &y));
	style = -1;
	positioned = true;
	assert(!view_default_place(&view, "app", &x, &y));
	positioned = false;
	parent = true;
	assert(!view_default_place(&view, "app", &x, &y));
	parent = false;
	limits[0] = limits[2] = 400;
	limits[1] = limits[3] = 300;
	assert(!view_default_place(&view, "app", &x, &y));
	memset(limits, 0, sizeof(limits));
	view.fullscreen = true;
	assert(!view_default_place(&view, "app", &x, &y));
	view.fullscreen = false;

	frame_destroy(view.frame);
	wlr_cursor_destroy(cursor);
	wlr_scene_node_destroy(&scene->tree.node);
	puts("ok: new app windows open inside the desktop strip");
	return 0;
}
