#ifndef PLATINUM_OUTLINE_H
#define PLATINUM_OUTLINE_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

/*
 * The gray dotted rectangle shown while dragging or resizing a window
 * (Mac OS 8/9 had no live window dragging by default).
 *
 * TODO: the HIG doesn't document the pattern; this 1 px 50% black/white
 * checkerboard is the classic DragGrayRgn look and should be verified.
 */
struct plat_outline {
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *edges[4];
	struct wlr_box box;
	bool visible;
};

void outline_init(struct plat_outline *outline, struct wlr_scene_tree *parent);
void outline_show(struct plat_outline *outline, struct wlr_box box);
void outline_hide(struct plat_outline *outline);

#endif
