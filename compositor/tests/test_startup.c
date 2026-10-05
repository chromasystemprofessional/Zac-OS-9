#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>

#include "draw.h"
#include "pixbuf.h"
#include "server.h"
#include "startup.h"
#include "widgets.h"

static void dispatch_for(struct wl_event_loop *loop, int ms) {
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		wl_event_loop_dispatch(loop, 5);
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 < ms);
}

static void add_output(struct plat_server *server, struct plat_output *output,
		int width, int height, int x, int y, int scale) {
	output->server = server;
	output->wlr_output = wlr_headless_add_output(server->backend, width, height);
	assert(output->wlr_output);
	assert(wlr_output_init_render(output->wlr_output, server->allocator, server->renderer));
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	wlr_output_state_set_scale(&state, scale);
	assert(wlr_output_commit_state(output->wlr_output, &state));
	wlr_output_state_finish(&state);
	wl_list_insert(&server->outputs, &output->link);
	assert(wlr_output_layout_add(server->output_layout, output->wlr_output, x, y));
}

static struct plat_pixbuf *buffer(struct plat_output *output, int width, int height, int x, int y) {
	assert(output->startup_buffer);
	struct wlr_scene_buffer *scene = output->startup_buffer;
	assert(scene->node.x == x && scene->node.y == y);
	assert(!scene->node.enabled);
	struct plat_pixbuf *pixels = wl_container_of(scene->buffer, pixels, base);
	assert(pixels->width == width && pixels->height == height);
	return pixels;
}

static void centered_logo(struct plat_pixbuf *pixels) {
	const int x0 = (pixels->width - 64) / 2, y0 = (pixels->height - 64) / 2;
	int painted = 0;
	for (int y = 0; y < pixels->height; y++) {
		for (int x = 0; x < pixels->width; x++) {
			if (pixels->data[y * pixels->width + x] != C_WHITE) {
				assert(x >= x0 && x < x0 + 64 && y >= y0 && y < y0 + 64);
				painted++;
			}
		}
	}
	assert(painted > 100);
}

static void centered_welcome(struct plat_pixbuf *pixels) {
	const int x = (pixels->width - 320) / 2;
	/* BOX_H: well top + well height + status/bar spacing + progress height + margin. */
	const int y = (pixels->height - (42 + 156 + 26 + 10 + PL_PROGRESS_H + 24)) / 2;
	assert(pixels->data[y * pixels->width + x] == C_BLACK);
	assert(pixels->data[y * pixels->width + x + 319] == C_BLACK);
	assert(pixels->data[(y + 1) * pixels->width + x + 1] == C_WHITE);
}

int main(void) {
	struct plat_server server = {0};
	server.display = wl_display_create();
	assert(server.display);
	struct wl_event_loop *loop = wl_display_get_event_loop(server.display);
	server.backend = wlr_headless_backend_create(loop);
	server.renderer = wlr_pixman_renderer_create();
	assert(server.backend && server.renderer);
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	assert(server.allocator);
	server.scene = wlr_scene_create();
	server.overlay_layer = wlr_scene_tree_create(&server.scene->tree);
	server.output_layout = wlr_output_layout_create(server.display);
	assert(server.scene && server.overlay_layer && server.output_layout);
	wl_list_init(&server.outputs);
	assert(wlr_backend_start(server.backend));
	struct plat_output a = {0}, b = {0}, c = {0};
	add_output(&server, &a, 1280, 800, 0, 0, 1);
	add_output(&server, &b, 1920, 1080, -1920, 120, 1);
	startup_begin(&server);
	dispatch_for(loop, 30);
	assert(startup_active());
	centered_logo(buffer(&a, 1280, 800, 0, 0));
	centered_logo(buffer(&b, 1920, 1080, -1920, 120));
	puts("ok: each unequal, offset display has its own centered logo");

	/* Mirrored layouts overlap, but only the buffer for the committing output may be visible. */
	wlr_output_layout_add(server.output_layout, b.wlr_output, 0, 0);
	dispatch_for(loop, 50);
	centered_logo(buffer(&b, 1920, 1080, 0, 0));
	startup_output_frame(&a, true);
	assert(a.startup_buffer->node.enabled && !b.startup_buffer->node.enabled);
	startup_output_frame(&a, false);
	startup_output_frame(&b, true);
	assert(!a.startup_buffer->node.enabled && b.startup_buffer->node.enabled);
	startup_output_frame(&b, false);
	puts("ok: unequal mirrored displays do not show each other's artwork");

	add_output(&server, &c, 1600, 1200, 1280, -100, 2);
	dispatch_for(loop, 50);
	centered_logo(buffer(&c, 800, 600, 1280, -100));
	startup_output_destroy(&b);
	wl_list_remove(&b.link);
	wlr_output_destroy(b.wlr_output);
	assert(!b.startup_buffer);
	dispatch_for(loop, 500);
	centered_welcome(buffer(&a, 1280, 800, 0, 0));
	centered_welcome(buffer(&c, 800, 600, 1280, -100));
	puts("ok: hot-plug, hot-unplug and HiDPI keep each Welcome box centered");

	struct wlr_output_state resized;
	wlr_output_state_init(&resized);
	wlr_output_state_set_custom_mode(&resized, 2048, 1536, 60000);
	assert(wlr_output_commit_state(c.wlr_output, &resized));
	wlr_output_state_finish(&resized);
	dispatch_for(loop, 50);
	centered_welcome(buffer(&c, 1024, 768, 1280, -100));
	wlr_scene_node_destroy(&a.startup_buffer->node);
	assert(!a.startup_buffer);
	dispatch_for(loop, 50);
	centered_welcome(buffer(&a, 1280, 800, 0, 0));
	puts("ok: mode changes and scene-node destruction refresh buffers safely");

	startup_surface_mapped("zacos9-menubar");
	startup_surface_mapped("zacos9-desktop");
	dispatch_for(loop, 2100);
	assert(!startup_active() && !a.startup_buffer && !c.startup_buffer);
	puts("ok: startup completes and removes every output's overlay");
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_output_layout_destroy(server.output_layout);
	wlr_backend_destroy(server.backend);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wl_display_destroy(server.display);
	return 0;
}
