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

static void get_geometry(struct plat_view *view, struct wlr_box *box) {
	*box = (struct wlr_box){ .width = 320, .height = 200 };
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
	.get_geometry = get_geometry,
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
	struct decor_margins m = decor_margins(DECOR_STYLE_DOCUMENT);

	/* 1920x1080 below a 20 px menu bar. */
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 1920, .height = 1060 };
	struct wlr_box frame;
	int w, h;
	assert(view_default_frame(&view, "org.gnome.TextEditor", &frame, &w, &h));
	assert(frame.x == 96 && frame.y == 20 + 32);
	/* The desktop's icon column (the rightmost 80 px) stays uncovered. */
	assert(frame.x + frame.width + DECOR_SHADOW <= 1920 - 96);
	assert(frame.y + frame.height + DECOR_SHADOW <= 1080 - 32);
	assert(w == frame.width - m.left - m.right);
	assert(h == frame.height - m.top - m.bottom);

	/* Client limits win over the default. */
	limits[2] = 800;
	limits[3] = 600;
	assert(view_default_frame(&view, "app", &frame, &w, &h));
	assert(w == 800 && h == 600);
	limits[2] = limits[3] = 0;

	/* Small screens keep proportionate strips. */
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 320, .height = 100 };
	frame = view_default_frame_in(usable);
	assert(frame.x == 40 && frame.y == 20 + 12);
	assert(frame.width > 0 && frame.height > 0);
	usable = (struct wlr_box){ .x = 0, .y = 20, .width = 1920, .height = 1060 };

	/* Left alone: the Finder, our programs' hinted windows, dialogs,
	 * fixed-size windows and fullscreen windows. */
	assert(!view_default_frame(&view, "zacos9-finder", &frame, &w, &h));
	assert(!view_default_frame(&view, "zacos9-picture", &frame, &w, &h));
	style = DECOR_STYLE_DOCUMENT;
	assert(!view_default_frame(&view, "app", &frame, &w, &h));
	style = -1;
	positioned = true;
	assert(!view_default_frame(&view, "app", &frame, &w, &h));
	positioned = false;
	parent = true;
	assert(!view_default_frame(&view, "app", &frame, &w, &h));
	parent = false;
	limits[0] = limits[2] = 400;
	limits[1] = limits[3] = 300;
	assert(!view_default_frame(&view, "app", &frame, &w, &h));
	memset(limits, 0, sizeof(limits));
	view.fullscreen = true;
	assert(!view_default_frame(&view, "app", &frame, &w, &h));
	view.fullscreen = false;

	frame_destroy(view.frame);
	wlr_cursor_destroy(cursor);
	wlr_scene_node_destroy(&scene->tree.node);
	puts("ok: new app windows open near full-screen with the desktop showing around them");
	return 0;
}
