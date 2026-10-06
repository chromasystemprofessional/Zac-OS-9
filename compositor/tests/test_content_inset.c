#include <assert.h>
#include <stdio.h>

#include "frame.h"
#include "server.h"

static struct wlr_box geometry;
static int style;

static struct wlr_surface *get_surface(struct plat_view *view) {
	return NULL;
}

static void get_geometry(struct plat_view *view, struct wlr_box *box) {
	*box = geometry;
}

static void get_limits(struct plat_view *view, int *min_w, int *min_h,
		int *max_w, int *max_h) {
	*min_w = *min_h = *max_w = *max_h = 0;
}

static bool has_parent(struct plat_view *view) {
	return false;
}

int platinum_shell_style_for(struct wlr_surface *surface) {
	return style;
}

static const struct plat_view_impl impl = {
	.get_surface = get_surface,
	.get_geometry = get_geometry,
	.get_size_limits = get_limits,
	.has_parent = has_parent,
};

static void check_inset(struct plat_view *view, int left, int top) {
	view_update_frame(view);
	assert(view->surface_tree->node.x == left);
	assert(view->surface_tree->node.y == top);
	if (!view->fullscreen) {
		struct decor_margins margins = decor_margins(style);
		assert(view->frame->st.width == geometry.width + margins.left + margins.right);
		assert(view->frame->st.height == geometry.height + margins.top + margins.bottom);
		assert(decor_hit(&view->frame->st, left, top) == DECOR_PART_CLIENT);
	}
}

int main(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);
	struct plat_server server = {
		.view_layer = wlr_scene_tree_create(&scene->tree),
		.dialog_layer = wlr_scene_tree_create(&scene->tree),
	};
	struct plat_view view = {0};
	view_setup(&view, &server, PLAT_VIEW_XDG, &impl);
	view_attach_surface_tree(&view, wlr_scene_tree_create(view.scene_tree));
	for (int kind = DECOR_STYLE_DOCUMENT; kind <= DECOR_STYLE_MOVABLE_MODAL; kind++) {
		style = kind;
		struct decor_margins margins = decor_margins(style);
		for (int offset = 0; offset <= 32; offset += 8) {
			geometry = (struct wlr_box){
				.x = offset, .y = offset, .width = 320, .height = 200,
			};
			check_inset(&view, margins.left, margins.top);
			view.fullscreen = true;
			check_inset(&view, 0, 0);
			view.fullscreen = false;
			check_inset(&view, margins.left, margins.top);
		}
		view.type = PLAT_VIEW_XWAYLAND;
		geometry.x = geometry.y = 0;
		check_inset(&view, margins.left, margins.top);
		view.type = PLAT_VIEW_XDG;
	}
	frame_destroy(view.frame);
	wlr_scene_node_destroy(&scene->tree.node);
	puts("ok: document/dialog content insets, nonzero xdg geometry and fullscreen restoration");
	return 0;
}
