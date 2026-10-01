#ifndef PLATINUM_FRAME_H
#define PLATINUM_FRAME_H

#include <wlr/types/wlr_scene.h>

#include "decor.h"

struct plat_view;

/* Scene-graph side of a window's Platinum frame. The frame tree sits below
 * the client surface; the resize-box overlay sits above it. */
struct plat_frame {
	struct plat_view *view;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *top, *bottom, *left, *right;
	struct wlr_scene_buffer *grow; /* over the client's bottom-right corner */

	struct decor_state st;
	struct plat_text *title;
	char *title_str;
	int title_max;
	bool dirty;
};

/* Creates the frame nodes as children of parent. Call frame_raise_overlay()
 * after the client surface tree has been added. */
struct plat_frame *frame_create(struct plat_view *view, struct wlr_scene_tree *parent);
void frame_raise_overlay(struct plat_frame *frame);
void frame_destroy(struct plat_frame *frame);

/* Update the frame state; re-renders only what changed. */
void frame_set_size(struct plat_frame *frame, int content_w, int content_h);
void frame_set_active(struct plat_frame *frame, bool active);
void frame_set_collapsed(struct plat_frame *frame, bool collapsed);
void frame_set_title(struct plat_frame *frame, const char *title);
void frame_set_features(struct plat_frame *frame, bool zoom, bool grow);
void frame_set_pressed(struct plat_frame *frame, enum decor_part part);
void frame_commit(struct plat_frame *frame);

/* Width and height of the frame (excluding shadow) for given content size. */
static inline int frame_outer_w(int content_w) {
	return content_w + DECOR_LEFT + DECOR_RIGHT;
}
static inline int frame_outer_h(int content_h) {
	return content_h + DECOR_TOP + DECOR_BOTTOM;
}

#endif
