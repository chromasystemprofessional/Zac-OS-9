#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>

#include "draw.h"
#include "pixbuf.h"
#include "server.h"
#include "startup.h"
#include "welcome.h"
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

/* Happy Zac, 32 px times the display's scale, in the middle of white. */
static void centered_logo(struct plat_pixbuf *pixels, int scale) {
	assert(pl_welcome_scale(pixels->width, pixels->height) == scale);
	const int size = 32 * scale;
	const int x0 = (pixels->width - size) / 2, y0 = (pixels->height - size) / 2;
	int painted = 0;
	for (int y = 0; y < pixels->height; y++) {
		for (int x = 0; x < pixels->width; x++) {
			if (pixels->data[y * pixels->width + x] != C_WHITE) {
				assert(x >= x0 && x < x0 + size && y >= y0 && y < y0 + size);
				painted++;
			}
		}
	}
	assert(painted > size * size / 4);
}

/* The 1984 box: 448 x 126 times the scale, where it was on the 512 x 342
 * screen centred on this one; a black line, white inside, a black shadow. */
static void centered_welcome(struct plat_pixbuf *pixels, int scale) {
	const int w = pixels->width, h = pixels->height, s = scale;
	assert(pl_welcome_scale(w, h) == s);
	const int x = (w - 512 * s) / 2 + 32 * s, y = (h - 342 * s) / 2 + 64 * s;
	const int x1 = x + 448 * s - 1, y1 = y + 126 * s - 1;
	assert(pixels->data[y * w + x] == C_BLACK);
	assert(pixels->data[y * w + x1] == C_BLACK);
	assert(pixels->data[y1 * w + x] == C_BLACK);
	assert(pixels->data[(y + s) * w + x + s] == C_WHITE);
	assert(pixels->data[(y + s - 1) * w + x + s] == C_BLACK);
	assert(pixels->data[(y1 + 2 * s) * w + x1 + 2 * s] == C_BLACK);
	assert(pixels->data[(y1 + 2 * s + 1) * w + x1 + 2 * s + 1] != C_BLACK ||
		pixels->data[(y1 + 2 * s + 1) * w + x1] != C_BLACK);
	/* The logo is in, at its corner, in colour. */
	int colour = 0;
	for (int py = y + 25 * s; py < y + 57 * s; py++) {
		for (int px = x + 24 * s; px < x + 56 * s; px++) {
			const uint32_t v = pixels->data[py * w + px];
			const int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
			colour += abs(r - g) > 40 || abs(g - b) > 40;
		}
	}
	assert(colour > 20 * s * s);
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
	centered_logo(buffer(&a, 1280, 800, 0, 0), 2);
	centered_logo(buffer(&b, 1920, 1080, -1920, 120), 3);
	puts("ok: each unequal, offset display has its own centered logo");

	/* Mirrored layouts overlap, but only the buffer for the committing output may be visible. */
	wlr_output_layout_add(server.output_layout, b.wlr_output, 0, 0);
	dispatch_for(loop, 50);
	centered_logo(buffer(&b, 1920, 1080, 0, 0), 3);
	startup_output_frame(&a, true);
	assert(a.startup_buffer->node.enabled && !b.startup_buffer->node.enabled);
	startup_output_frame(&a, false);
	startup_output_frame(&b, true);
	assert(!a.startup_buffer->node.enabled && b.startup_buffer->node.enabled);
	startup_output_frame(&b, false);
	puts("ok: unequal mirrored displays do not show each other's artwork");

	add_output(&server, &c, 1600, 1200, 1280, -100, 2);
	dispatch_for(loop, 50);
	centered_logo(buffer(&c, 800, 600, 1280, -100), 1);
	startup_output_destroy(&b);
	wl_list_remove(&b.link);
	wlr_output_destroy(b.wlr_output);
	assert(!b.startup_buffer);
	dispatch_for(loop, 500);
	centered_welcome(buffer(&a, 1280, 800, 0, 0), 2);
	centered_welcome(buffer(&c, 800, 600, 1280, -100), 1);
	puts("ok: hot-plug, hot-unplug and HiDPI keep each Welcome box centered");

	struct wlr_output_state resized;
	wlr_output_state_init(&resized);
	wlr_output_state_set_custom_mode(&resized, 2048, 1536, 60000);
	assert(wlr_output_commit_state(c.wlr_output, &resized));
	wlr_output_state_finish(&resized);
	dispatch_for(loop, 50);
	centered_welcome(buffer(&c, 1024, 768, 1280, -100), 2);
	wlr_scene_node_destroy(&a.startup_buffer->node);
	assert(!a.startup_buffer);
	dispatch_for(loop, 50);
	centered_welcome(buffer(&a, 1280, 800, 0, 0), 2);
	puts("ok: mode changes and scene-node destruction refresh buffers safely");

	startup_surface_mapped("zacos9-menubar");
	startup_surface_mapped("zacos9-desktop");
	dispatch_for(loop, 2100);
	assert(!startup_active() && !a.startup_buffer && !c.startup_buffer);
	puts("ok: startup completes and removes every output's overlay");

	/* Taking over from the boot splash: the Welcome box at once, and the
	 * boot's own parade, live. */
	char dir[] = "/tmp/zacos9-startup-XXXXXX", handoff[64], parade[64];
	assert(mkdtemp(dir));
	snprintf(handoff, sizeof(handoff), "%s/handoff", dir);
	snprintf(parade, sizeof(parade), "%s/parade", dir);
	struct timespec boot;
	clock_gettime(CLOCK_BOOTTIME, &boot);
	FILE *f = fopen(handoff, "w");
	fprintf(f, "%.2f\n", boot.tv_sec + boot.tv_nsec / 1e9 - 1);
	fclose(f);
	f = fopen(parade, "w");
	fputs("intel-graphics\nbuilt-in-sound\n", f);
	fclose(f);
	setenv("ZACOS9_HANDOFF_FILE", handoff, 1);
	setenv("ZACOS9_PARADE_FILE", parade, 1);
	setenv("ZACOS9_PROC_MODULES", "/nonexistent", 1);
	startup_begin(&server);
	dispatch_for(loop, 30);
	struct plat_pixbuf *p = buffer(&a, 1280, 800, 0, 0);
	centered_welcome(p, 2);
	puts("ok: after the splash, the Welcome box is up at once, without Happy Zac first");

	/* Icon slots: 0 and 1 drawn, 2 not yet; then the next to load appears. */
	uint32_t slot2[32 * 32];
	const int sy = 800 - 44;
	for (int y = 0; y < 32; y++) {
		for (int x = 0; x < 32; x++) {
			slot2[y * 32 + x] = p->data[(sy + y) * 1280 + 96 + x];
		}
	}
	f = fopen(parade, "a");
	fputs("bluetooth\n", f);
	fclose(f);
	dispatch_for(loop, 60);
	p = buffer(&a, 1280, 800, 0, 0);
	int changed = 0;
	for (int y = 0; y < 32; y++) {
		for (int x = 0; x < 32; x++) {
			changed += slot2[y * 32 + x] != p->data[(sy + y) * 1280 + 96 + x];
		}
	}
	assert(changed > 50);
	puts("ok: extensions join the parade as the boot records them");

	startup_surface_mapped("zacos9-menubar");
	startup_surface_mapped("zacos9-desktop");
	dispatch_for(loop, 1300);
	assert(!startup_active());
	unlink(handoff);
	unlink(parade);
	rmdir(dir);
	puts("ok: after the splash, startup ends soon after the shell is up");
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_output_layout_destroy(server.output_layout);
	wlr_backend_destroy(server.backend);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wl_display_destroy(server.display);
	return 0;
}
