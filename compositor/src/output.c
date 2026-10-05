#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "server.h"
#include "settings.h"
#include "startup.h"

static void output_frame(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
			output->server->scene, output->wlr_output);

	startup_output_frame(output, true);
	wlr_scene_output_commit(scene_output, NULL);
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
	prefs_write_outputs(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct plat_output *output = wl_container_of(listener, output, destroy);
	struct plat_server *server = output->server;
	struct wlr_output *gone = output->wlr_output;
	startup_output_destroy(output);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
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
	/* Whole-number scales only: 1-bit-era pixel art must stay sharp. */
	wlr_output_state_set_scale(&state, server->output_scale);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	wlr_xcursor_manager_load(server->cursor_mgr, server->output_scale);

	struct plat_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
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
