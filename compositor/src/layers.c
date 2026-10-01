#include <stdlib.h>
#include <wlr/types/wlr_output.h>

#include "server.h"

#define LAYER_SHELL_VERSION 4

/* Lay out every shell surface on an output and recompute the area left for
 * windows. Exclusive surfaces (the menu bar) go first so the others are
 * placed within what remains. */
void layers_arrange(struct plat_output *output) {
	struct plat_server *server = output->server;
	struct wlr_box full;
	wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
	struct wlr_box usable = full;

	for (int pass = 0; pass < 2; pass++) {
		bool want_exclusive = pass == 0;
		for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer >= 0; layer--) {
			struct plat_layer_surface *ls;
			wl_list_for_each(ls, &server->layer_surfaces, link) {
				struct wlr_layer_surface_v1 *s = ls->layer_surface;
				if (s->output != output->wlr_output ||
						(int)s->current.layer != layer || !s->initialized) {
					continue;
				}
				if ((s->current.exclusive_zone > 0) != want_exclusive) {
					continue;
				}
				wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
				wlr_scene_node_set_position(&ls->popup_tree->node,
					ls->scene->tree->node.x, ls->scene->tree->node.y);
			}
		}
	}
	output->usable_area = usable;
}

static void arrange_for(struct plat_layer_surface *ls) {
	struct wlr_output *wlr_output = ls->layer_surface->output;
	struct plat_output *output;
	wl_list_for_each(output, &ls->server->outputs, link) {
		if (output->wlr_output == wlr_output) {
			layers_arrange(output);
		}
	}
}

/* Keyboard focus for shell surfaces that ask for it (menus being tracked
 * use "exclusive"). NULL hands focus back to the frontmost window. */
void layers_focus(struct plat_layer_surface *layer) {
	if (!layer) {
		return;
	}
	struct plat_server *server = layer->server;
	server->focused_layer = layer;
	input_keyboard_enter(server, layer->layer_surface->surface);
}

static void unfocus(struct plat_layer_surface *ls) {
	struct plat_server *server = ls->server;
	if (server->focused_layer != ls) {
		return;
	}
	server->focused_layer = NULL;
	struct plat_view *view = server->focused_view;
	if (view && view->mapped && !view->hidden) {
		input_keyboard_enter(server, view->impl->get_surface(view));
	} else {
		wlr_seat_keyboard_clear_focus(server->seat);
	}
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct plat_layer_surface *ls = wl_container_of(listener, ls, map);
	ls->mapped = true;
	arrange_for(ls);
	if (ls->layer_surface->current.keyboard_interactive ==
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
		layers_focus(ls);
	}
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct plat_layer_surface *ls = wl_container_of(listener, ls, unmap);
	ls->mapped = false;
	unfocus(ls);
	arrange_for(ls);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct plat_layer_surface *ls = wl_container_of(listener, ls, commit);
	struct wlr_layer_surface_v1 *s = ls->layer_surface;
	if (!s->initialized) {
		return;
	}
	/* Layer changes move the surface between scene trees. */
	struct wlr_scene_tree *want = ls->server->shell_layers[s->current.layer];
	if (ls->scene->tree->node.parent != want) {
		wlr_scene_node_reparent(&ls->scene->tree->node, want);
	}
	if (s->current.committed || s->initial_commit) {
		arrange_for(ls);
	}
	if (ls->mapped) {
		bool exclusive = s->current.keyboard_interactive ==
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
		if (exclusive && ls->server->focused_layer != ls) {
			layers_focus(ls);
		} else if (!exclusive && s->current.keyboard_interactive ==
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
			unfocus(ls);
		}
	}
}

/* Popups from shell surfaces sit above everything in their layer. */
static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct plat_layer_surface *ls = wl_container_of(listener, ls, new_popup);
	struct wlr_xdg_popup *popup = data;
	popup->base->data = wlr_scene_xdg_surface_create(ls->popup_tree, popup->base);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct plat_layer_surface *ls = wl_container_of(listener, ls, destroy);
	unfocus(ls);
	wl_list_remove(&ls->link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->commit.link);
	wl_list_remove(&ls->new_popup.link);
	wl_list_remove(&ls->destroy.link);
	wlr_scene_node_destroy(&ls->popup_tree->node);
	/* The scene node goes with the layer surface; re-arrange afterwards. */
	struct wlr_output *wlr_output = ls->layer_surface->output;
	struct plat_server *server = ls->server;
	free(ls);
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == wlr_output) {
			layers_arrange(output);
		}
	}
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *s = data;

	if (!s->output) {
		struct plat_output *output =
			output_at(server, server->cursor->x, server->cursor->y);
		if (!output) {
			wlr_layer_surface_v1_destroy(s);
			return;
		}
		s->output = output->wlr_output;
	}

	struct plat_layer_surface *ls = calloc(1, sizeof(*ls));
	ls->server = server;
	ls->layer_surface = s;
	ls->scene = wlr_scene_layer_surface_v1_create(
		server->shell_layers[s->pending.layer], s);
	ls->popup_tree = wlr_scene_tree_create(server->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
	s->data = ls;
	wl_list_insert(&server->layer_surfaces, &ls->link);

	ls->map.notify = handle_map;
	wl_signal_add(&s->surface->events.map, &ls->map);
	ls->unmap.notify = handle_unmap;
	wl_signal_add(&s->surface->events.unmap, &ls->unmap);
	ls->commit.notify = handle_commit;
	wl_signal_add(&s->surface->events.commit, &ls->commit);
	ls->new_popup.notify = handle_new_popup;
	wl_signal_add(&s->events.new_popup, &ls->new_popup);
	ls->destroy.notify = handle_destroy;
	wl_signal_add(&s->events.destroy, &ls->destroy);
}

void layers_init(struct plat_server *server) {
	wl_list_init(&server->layer_surfaces);
	server->layer_shell = wlr_layer_shell_v1_create(server->display, LAYER_SHELL_VERSION);
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
}
