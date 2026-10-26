#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include "frame.h"
#include "pixbuf.h"
#include "server.h"

/* Clicks on the transparent parts of a frame buffer (around the shadow)
 * fall through to whatever is below. */
static bool frame_accepts_input(struct wlr_scene_buffer *buffer,
		double *sx, double *sy) {
	struct plat_frame *frame = buffer->node.data;
	int x = buffer->node.x + (int)*sx;
	int y = buffer->node.y + (int)*sy;
	return decor_hit(&frame->st, x, y) != DECOR_PART_NONE;
}

static struct wlr_scene_buffer *make_part(struct plat_frame *frame,
		struct wlr_scene_tree *parent) {
	struct wlr_scene_buffer *b = wlr_scene_buffer_create(parent, NULL);
	if (!b) {
		wlr_log(WLR_ERROR, "frame: out of memory");
		abort(); /* every window needs its frame; say so rather than crash later */
	}
	b->node.data = frame;
	b->point_accepts_input = frame_accepts_input;
	/* Integer output scaling must stay pixel-exact. */
	wlr_scene_buffer_set_filter_mode(b, WLR_SCALE_FILTER_NEAREST);
	return b;
}

struct plat_frame *frame_create(struct plat_view *view, struct wlr_scene_tree *parent) {
	struct plat_frame *frame = calloc(1, sizeof(*frame));
	if (!frame || !(frame->tree = wlr_scene_tree_create(parent))) {
		wlr_log(WLR_ERROR, "frame: out of memory");
		abort();
	}
	frame->view = view;
	frame->top = make_part(frame, frame->tree);
	frame->bottom = make_part(frame, frame->tree);
	frame->left = make_part(frame, frame->tree);
	frame->right = make_part(frame, frame->tree);
	frame->grow = make_part(frame, parent);
	frame->st.has_close = true;
	frame->st.has_collapse = true;
	frame->st.has_zoom = true;
	frame->st.has_grow = true;
	frame->dirty = true;
	return frame;
}

void frame_raise_overlay(struct plat_frame *frame) {
	wlr_scene_node_raise_to_top(&frame->grow->node);
}

void frame_destroy(struct plat_frame *frame) {
	wlr_scene_node_destroy(&frame->grow->node);
	wlr_scene_node_destroy(&frame->tree->node);
	text_destroy(frame->title);
	free(frame->title_str);
	free(frame);
}

/* Where the frame falls on its screen's pixels. At a whole-number scale
 * the scene enlarges its 1x pixels exactly (quarters 0). At 1.25, 1.5 or
 * 1.75 it is drawn at the screen's own pixels (pl_snap), which depends on
 * where it is from the screen's origin, but only up to where the pattern
 * repeats (4 / gcd(quarters, 4) pixels), so a move redraws it only now
 * and then. */
static void placement(struct plat_frame *frame, int *quarters, int *ux, int *uy) {
	*quarters = *ux = *uy = 0;
	struct plat_server *server = frame->view ? frame->view->server : NULL;
	int lx, ly;
	if (!server || !wlr_scene_node_coords(&frame->tree->node, &lx, &ly)) {
		return;
	}
	struct plat_output *output = output_at(server, lx + frame->st.width / 2.0, ly + DECOR_TOP / 2.0);
	if (!output) {
		return;
	}
	const int q = (int)(output->wlr_output->scale * 4 + 0.5f);
	if (q % 4 == 0) {
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
	const int period = q % 2 ? 4 : 2;
	*quarters = q;
	*ux = ((lx - box.x) % period + period) % period;
	*uy = ((ly - box.y) % period + period) % period;
}

/* Render one frame-local rectangle of the decoration into a scene buffer. */
static void render_part(struct plat_frame *frame, struct wlr_scene_buffer *node,
		int x, int y, int w, int h) {
	if (w <= 0 || h <= 0) {
		wlr_scene_node_set_enabled(&node->node, false);
		return;
	}
	struct plat_pixbuf *buf = pixbuf_create(w, h);
	if (!buf) {
		return;
	}
	const int q = frame->quarters, ux = frame->ux, uy = frame->uy;
	struct pl_canvas canvas = {
		.px = buf->data, .stride = w, .x = x, .y = y, .width = w, .height = h,
		.log = q ? pl_text_log_new(buf->data, w) : NULL,
	};
	decor_paint(&canvas, &frame->st);
	if (q) {
		/* The screen's pixels, shown one for one: each 1x pixel one or
		 * two of them, the title in the font for this scale, and the
		 * stripes drawn even for it. */
		const int dw = pl_snap(ux + x + w, q) - pl_snap(ux + x, q);
		const int dh = pl_snap(uy + y + h, q) - pl_snap(uy + y, q);
		struct plat_pixbuf *big = pixbuf_create(dw, dh);
		if (big) {
			pl_snap_scale_canvas(&canvas, ux + x, uy + y, q, big->data);
			decor_paint_stripes_scaled(big->data, dw, dh, &frame->st, x, y, ux, uy, q);
			wlr_buffer_drop(&buf->base);
			buf = big;
		}
		pl_text_log_free(canvas.log);
	}
	wlr_scene_buffer_set_buffer(node, &buf->base);
	wlr_scene_buffer_set_dest_size(node, w, h);
	wlr_scene_node_set_position(&node->node, x, y);
	wlr_scene_node_set_enabled(&node->node, true);
	wlr_buffer_drop(&buf->base);
}

static void update_title_text(struct plat_frame *frame) {
	int max = decor_title_max_width(&frame->st);
	if (frame->title && max == frame->title_max) {
		return;
	}
	text_destroy(frame->title);
	frame->title = text_render(frame->title_str ? frame->title_str : "", max);
	frame->title_max = max;
	frame->st.title = frame->title;
}

void frame_commit(struct plat_frame *frame) {
	int quarters, ux, uy;
	placement(frame, &quarters, &ux, &uy);
	if (quarters != frame->quarters || ux != frame->ux || uy != frame->uy) {
		frame->quarters = quarters;
		frame->ux = ux;
		frame->uy = uy;
		frame->dirty = true;
	}
	if (!frame->dirty || frame->st.width <= 0) {
		return;
	}
	frame->dirty = false;
	update_title_text(frame);

	const struct decor_state *st = &frame->st;
	const int W = st->width, S = DECOR_SHADOW;
	if (st->collapsed) {
		/* A pop-up's tab sits on the screen's edge: no shadow below it. */
		const bool popup = st->style == DECOR_STYLE_POPUP;
		render_part(frame, frame->top, 0, 0, W + S, decor_frame_height(st) + (popup ? 0 : S));
		wlr_scene_node_set_enabled(&frame->bottom->node, false);
		wlr_scene_node_set_enabled(&frame->left->node, false);
		wlr_scene_node_set_enabled(&frame->right->node, false);
		wlr_scene_node_set_enabled(&frame->grow->node, false);
		return;
	}

	const struct decor_margins m = decor_margins(st->style);
	const int H = st->height, mid = H - m.top - m.bottom;
	render_part(frame, frame->top, 0, 0, W + S, m.top);
	render_part(frame, frame->bottom, 0, H - m.bottom, W + S, m.bottom + S);
	render_part(frame, frame->left, 0, m.top, m.left, mid);
	render_part(frame, frame->right, W - m.right, m.top, m.right + S, mid);
	if (st->has_grow && st->style == DECOR_STYLE_DOCUMENT) {
		/* Only the part over the client; the frame parts paint the rest. */
		int g = DECOR_GROW_INSET - DECOR_RIGHT;
		render_part(frame, frame->grow, W - DECOR_GROW_INSET,
			H - DECOR_GROW_INSET, g, g);
	} else {
		wlr_scene_node_set_enabled(&frame->grow->node, false);
	}
}

void frame_set_size(struct plat_frame *frame, int content_w, int content_h) {
	int w = frame_outer_w(frame, content_w), h = frame_outer_h(frame, content_h);
	if (w != frame->st.width || h != frame->st.height) {
		bool width_changed = w != frame->st.width;
		frame->st.width = w;
		frame->st.height = h;
		if (width_changed) {
			frame->title_max = -1; /* re-fit the title */
		}
		frame->dirty = true;
	}
}

void frame_set_active(struct plat_frame *frame, bool active) {
	if (frame->st.active != active) {
		frame->st.active = active;
		frame->st.pressed = DECOR_PART_NONE;
		frame->dirty = true;
	}
}

void frame_set_collapsed(struct plat_frame *frame, bool collapsed) {
	if (frame->st.collapsed != collapsed) {
		frame->st.collapsed = collapsed;
		frame->dirty = true;
	}
}

void frame_set_proxy(struct plat_frame *frame, int proxy) {
	if (frame->st.proxy == proxy) {
		return;
	}
	frame->st.proxy = proxy;
	frame->title_max = -1; /* the title's room changed */
	frame->dirty = true;
}

void frame_set_title(struct plat_frame *frame, const char *title) {
	title = title ? title : "";
	if (frame->title_str && strcmp(frame->title_str, title) == 0) {
		return;
	}
	free(frame->title_str);
	frame->title_str = strdup(title);
	frame->title_max = -1;
	frame->dirty = true;
}

void frame_set_features(struct plat_frame *frame, bool zoom, bool grow) {
	if (frame->st.style != DECOR_STYLE_DOCUMENT) {
		zoom = grow = false;
	}
	if (frame->st.has_zoom != zoom || frame->st.has_grow != grow) {
		frame->st.has_zoom = zoom;
		frame->st.has_grow = grow;
		frame->title_max = -1;
		frame->dirty = true;
	}
}

void frame_set_style(struct plat_frame *frame, enum decor_style style) {
	if (frame->st.style == style) {
		return;
	}
	frame->st.style = style;
	const bool doc = style == DECOR_STYLE_DOCUMENT;
	frame->st.has_close = frame->st.has_collapse = doc;
	if (!doc) {
		frame->st.has_zoom = frame->st.has_grow = false;
	}
	frame->st.width = 0; /* margins changed: recompute on the next size */
	frame->title_max = -1;
	frame->dirty = true;
}

void frame_set_pressed(struct plat_frame *frame, enum decor_part part) {
	if (frame->st.pressed != part) {
		frame->st.pressed = part;
		frame->dirty = true;
	}
}

void frame_set_tab_x(struct plat_frame *frame, int tab_x) {
	if (frame->st.tab_x != tab_x) {
		frame->st.tab_x = tab_x;
		frame->dirty = true;
	}
}
