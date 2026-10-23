#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "access.h"
#include "server.h"
#include "settings.h"
#include "startup.h"
#include "snapshot.h"

static void output_snapshot_commit(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, snapshot_commit);
	const struct wlr_output_event_commit *event = data;
	if (event->state->committed & (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED |
			WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE)) {
		snapshot_output_finish(output);
	}
	if (event->state->committed & WLR_OUTPUT_STATE_BUFFER) {
		snapshot_output_commit(output, event->state->buffer);
	}
}

void output_zoom_box(int width, int height, double px, double py, double factor, struct wlr_fbox *box) {
	/* The part of the screen shown, so the point under the pointer stays
	 * under it: its corner is at p(1 - 1/factor). */
	box->width = width / factor;
	box->height = height / factor;
	box->x = px * (1 - 1 / factor);
	box->y = py * (1 - 1 / factor);
}

/* Zoom (the Accessibility panel): the frame the scene drew, magnified
 * about the pointer into another of the output's buffers. False to commit
 * as usual. */
static bool output_commit_zoomed(struct plat_output *output, struct wlr_scene_output *scene_output) {
	const double factor = access_zoom_factor();
	struct wlr_output *wlr_output = output->wlr_output;
	if (factor <= 1.0 || wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
		return false;
	}
	struct plat_server *server = output->server;
	/* Everything moves at once: draw it all. */
	wlr_damage_ring_add_whole(&scene_output->damage_ring);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool ok = wlr_scene_output_build_state(scene_output, &state, NULL) &&
		(state.committed & WLR_OUTPUT_STATE_BUFFER) && state.buffer;
	struct wlr_texture *texture = ok ? wlr_texture_from_buffer(server->renderer, state.buffer) : NULL;
	struct wlr_buffer *target = NULL;
	if (texture && wlr_output_configure_primary_swapchain(wlr_output, &state, &wlr_output->swapchain)) {
		target = wlr_swapchain_acquire(wlr_output->swapchain, NULL);
	}
	if (target) {
		double lx = server->cursor->x, ly = server->cursor->y;
		wlr_output_layout_output_coords(server->output_layout, wlr_output, &lx, &ly);
		struct wlr_fbox box;
		output_zoom_box(texture->width, texture->height, lx * wlr_output->scale,
			ly * wlr_output->scale, factor, &box);
		struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(server->renderer, target, NULL);
		if (pass) {
			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
				.texture = texture,
				.src_box = box,
				.dst_box = { .width = target->width, .height = target->height },
				.filter_mode = WLR_SCALE_FILTER_NEAREST, /* CloseView's fat pixels */
			});
			if (wlr_render_pass_submit(pass)) {
				wlr_output_state_set_buffer(&state, target);
			}
		}
		wlr_buffer_unlock(target);
	}
	if (texture) {
		wlr_texture_destroy(texture);
	}
	if (ok) {
		wlr_output_commit_state(wlr_output, &state);
	}
	wlr_output_state_finish(&state);
	return true;
}

static void output_frame(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
			output->server->scene, output->wlr_output);

	startup_output_frame(output, true);
	if (!output_commit_zoomed(output, scene_output)) {
		wlr_scene_output_commit(scene_output, NULL);
	}
	startup_output_frame(output, false);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

/* Fired when the nested backend's host window is resized. */
static void output_request_state(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	layers_arrange(output);
	view_refresh_fullscreen(output->server);
	prefs_write_outputs(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, destroy);
	struct plat_server *server = output->server;
	struct wlr_output *gone = output->wlr_output;
	startup_output_destroy(output);
	snapshot_output_finish(output);
	wl_list_remove(&output->snapshot_commit.link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
	tablet_map(server); /* off the screen that went */
	touch_map(server);
	idle_update(server);
	struct plat_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->fullscreen_output == gone) {
			view->fullscreen_output = NULL;
			if (wl_list_empty(&server->outputs)) {
				view_set_fullscreen(view, false, NULL);
			} else {
				view_set_fullscreen(view, true, NULL);
			}
		}
	}
	/* ZacOS 9's own shell (menu bar, desktop) moves to the display that is
	 * main now; anyone else's shell surfaces live on one output and go
	 * with it. */
	layers_pin_to_main(server);
	struct plat_layer_surface *ls, *tmp;
	wl_list_for_each_safe(ls, tmp, &server->layer_surfaces, link) {
		if (ls->layer_surface->output == gone) {
			wlr_layer_surface_v1_destroy(ls->layer_surface);
		}
	}
}

static void server_new_output(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_state_set_scale(&state, server->output_scale);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	wlr_xcursor_manager_load(server->cursor_mgr, server->output_scale);

	struct plat_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->snapshot_commit.notify = output_snapshot_commit;
	wl_signal_add(&wlr_output->events.commit, &output->snapshot_commit);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
	layers_arrange(output);
	/* The Monitors panel's scale and resolution, if chosen. */
	prefs_apply(server);
	/* A screen plugged in wakes the others, as input would. */
	idle_input(server, IDLE_INPUT_MOTION);
}

struct plat_output *output_main(struct plat_server *server) {
	char name[64];
	struct plat_output *output, *first = NULL;
	const bool chosen = pl_setting("main-display", name, sizeof(name));
	/* The list has the newest output first: the first one connected is last. */
	wl_list_for_each(output, &server->outputs, link) {
		first = output;
		if (chosen && strcmp(output->wlr_output->name, name) == 0) {
			return output;
		}
	}
	return first;
}

struct plat_output *output_at(struct plat_server *server, double lx, double ly) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(server->output_layout, lx, ly);
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (!wlr_output || output->wlr_output == wlr_output) {
			return output;
		}
	}
	return NULL;
}

struct wlr_box output_usable_area(struct plat_server *server, double lx, double ly) {
	struct plat_output *output = output_at(server, lx, ly);
	return output ? output->usable_area : (struct wlr_box){ 0 };
}

void output_init(struct plat_server *server) {
	wl_list_init(&server->outputs);
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
}
