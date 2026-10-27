#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wlr/render/pixman.h>

#include "../src/pixbuf.h"
#include "../src/snapshot.c"

static struct wlr_output test_output;

struct wlr_output *__wrap_wlr_output_from_resource(struct wl_resource *resource) {
	return &test_output;
}

void __wrap_wlr_output_layout_get_box(struct wlr_output_layout *layout,
		struct wlr_output *output, struct wlr_box *box) {
	*box = (struct wlr_box){ .width = 4, .height = 3 };
}

int main(void) {
	int sockets[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
	struct wl_display *display = wl_display_create();
	assert(display);
	struct wl_client *client = wl_client_create(display, sockets[0]);
	assert(client);
	struct plat_server server = {
		.display = display,
		.renderer = wlr_pixman_renderer_create(),
	};
	assert(server.renderer);
	wl_list_init(&server.outputs);
	struct plat_output output = {
		.server = &server,
		.wlr_output = &test_output,
	};
	wl_list_insert(&server.outputs, &output.link);
	test_output.enabled = true;
	test_output.attach_render_locks = 7;
	test_output.software_cursor_locks = 9;
	struct plat_pixbuf *buffer = pixbuf_create(4, 3);
	assert(buffer);
	for (int i = 0; i < 12; i++) {
		buffer->data[i] = 0xff3366cc;
	}
	snapshot_output_commit(&output, &buffer->base);
	const size_t locks = buffer->base.n_locks;
	assert(locks == 1);
	snapshot_output_commit(&output, &buffer->base);
	assert(buffer->base.n_locks == locks);
	struct wl_resource *manager = wl_resource_create(client,
		&zacos_snapshot_manager_v1_interface, 1, 2);
	assert(manager);
	wl_resource_set_implementation(manager, &manager_impl, &server, NULL);
	for (int i = 0; i < 8; i++) {
		capture(client, manager, 3, NULL);
		struct wl_resource *frame = wl_client_get_object(client, 3);
		assert(frame);
		wl_resource_destroy(frame);
		assert(test_output.attach_render_locks == 7);
		assert(test_output.software_cursor_locks == 9);
		assert(output.snapshot_buffer == &buffer->base);
		assert(buffer->base.n_locks == locks);
		assert(test_output.enabled);
	}
	snapshot_output_finish(&output);
	assert(!output.snapshot_buffer && buffer->base.n_locks == 0);
	capture(client, manager, 3, NULL); /* Missing displayed frame is an explicit failure. */
	wl_resource_destroy(wl_client_get_object(client, 3));
	wlr_buffer_drop(&buffer->base);
	wl_client_destroy(client);
	close(sockets[1]);
	wlr_renderer_destroy(server.renderer);
	wl_display_destroy(display);
	return 0;
}
