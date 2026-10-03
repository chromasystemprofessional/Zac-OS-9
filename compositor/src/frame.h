#ifndef ZACOS9_FRAME_H
#define ZACOS9_FRAME_H

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
/* Document window or dialog; dialogs have no boxes and no resize box. */
void frame_set_style(struct plat_frame *frame, enum decor_style style);
void frame_set_pressed(struct plat_frame *frame, enum decor_part part);
void frame_commit(struct plat_frame *frame);

/* Width and height of the frame (excluding shadow) for given content size. */
static inline int frame_outer_w(const struct plat_frame *frame, int content_w) {
	struct decor_margins m = decor_margins(frame->st.style);
	return content_w + m.left + m.right;
}
static inline int frame_outer_h(const struct plat_frame *frame, int content_h) {
	struct decor_margins m = decor_margins(frame->st.style);
	return content_h + m.top + m.bottom;
}

#endif
