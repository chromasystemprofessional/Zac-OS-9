#include <wlr/types/wlr_buffer.h>

#include "outline.h"
#include "pixbuf.h"

void outline_init(struct plat_outline *outline, struct wlr_scene_tree *parent) {
	outline->tree = wlr_scene_tree_create(parent);
	for (int i = 0; i < 4; i++) {
		outline->edges[i] = wlr_scene_buffer_create(outline->tree, NULL);
		wlr_scene_buffer_set_filter_mode(outline->edges[i], WLR_SCALE_FILTER_NEAREST);
	}
	wlr_scene_node_set_enabled(&outline->tree->node, false);
}

/* One edge strip; the checkerboard phase is taken from the outline's corner
 * so the four edges meet seamlessly. */
static void set_edge(struct wlr_scene_buffer *node, int x, int y, int w, int h) {
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	if (!buf) {
		return;
	}
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			bool dark = ((x + i) + (y + j)) % 2 == 0;
			buf->data[j * w + i] = dark ? 0xFF000000u : 0xFFFFFFFFu;
		}
	}
	wlr_scene_buffer_set_buffer(node, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_node_set_position(&node->node, x, y);
}

void outline_show(struct plat_outline *outline, struct wlr_box box) {
	if (box.width < 2 || box.height < 2) {
		return;
	}
	if (outline->visible && wlr_box_equal(&box, &outline->box)) {
		return;
	}
	/* Only rebuild buffers when the size changes; moves just reposition. */
	wlr_scene_node_set_position(&outline->tree->node, box.x, box.y);
	bool same_size = outline->visible && box.width == outline->box.width &&
		box.height == outline->box.height;
	if (!same_size) {
		int w = box.width, h = box.height;
		set_edge(outline->edges[0], 0, 0, w, 1);
		set_edge(outline->edges[1], 0, h - 1, w, 1);
		set_edge(outline->edges[2], 0, 1, 1, h - 2);
		set_edge(outline->edges[3], w - 1, 1, 1, h - 2);
	}
	outline->box = box;
	outline->visible = true;
	wlr_scene_node_set_enabled(&outline->tree->node, true);
}

void outline_hide(struct plat_outline *outline) {
	outline->visible = false;
	wlr_scene_node_set_enabled(&outline->tree->node, false);
}
