#define _GNU_SOURCE
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include "server.h"
#include "snapshot.h"
#include "zacos-snapshot-v1-protocol.h"

void snapshot_output_commit(struct plat_output *output, struct wlr_buffer *buffer) {
	if (output->snapshot_buffer == buffer) {
		return;
	}
	struct wlr_buffer *old = output->snapshot_buffer;
	output->snapshot_buffer = buffer ? wlr_buffer_lock(buffer) : NULL;
	if (old) {
		wlr_buffer_unlock(old);
	}
}

void snapshot_output_finish(struct plat_output *output) {
	snapshot_output_commit(output, NULL);
}

static void destroy_resource(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zacos_snapshot_frame_v1_interface frame_impl = {
	.destroy = destroy_resource,
};

static void fail(struct wl_resource *frame, const char *message) {
	wlr_log(WLR_ERROR, "Screen Snapshot: %s", message);
	zacos_snapshot_frame_v1_send_failed(frame, message);
}

static void capture(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *output_resource) {
	struct wl_resource *frame = wl_resource_create(client,
		&zacos_snapshot_frame_v1_interface, 1, id);
	if (!frame) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(frame, &frame_impl, NULL, NULL);
	struct plat_server *server = wl_resource_get_user_data(resource);
	struct wlr_output *wlr_output = wlr_output_from_resource(output_resource);
	struct plat_output *output = NULL, *candidate;
	wl_list_for_each(candidate, &server->outputs, link) {
		if (candidate->wlr_output == wlr_output) {
			output = candidate;
			break;
		}
	}
	if (!output || !wlr_output->enabled || !output->snapshot_buffer) {
		fail(frame, "No displayed frame is available on this output.");
		return;
	}
	struct wlr_buffer *buffer = output->snapshot_buffer;
	if (buffer->width <= 0 || buffer->height <= 0 ||
			(uint64_t)buffer->width * buffer->height > 32u * 1024u * 1024u) {
		fail(frame, "Display exceeds the 32-megapixel screenshot limit.");
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
	if (box.width <= 0 || box.height <= 0) {
		fail(frame, "Display has no valid layout area.");
		return;
	}
	const size_t stride = (size_t)buffer->width * 4;
	const size_t size = stride * buffer->height;
	int fd = memfd_create("zacos9-snapshot", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0 || ftruncate(fd, (off_t)size) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		fail(frame, "Could not allocate screenshot storage.");
		return;
	}
	void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		fail(frame, "Could not map screenshot storage.");
		return;
	}
	struct wlr_texture *texture = wlr_texture_from_buffer(server->renderer, buffer);
	bool copied = false;
	if (texture) {
		copied = wlr_texture_read_pixels(texture,
			&(struct wlr_texture_read_pixels_options){
				.data = pixels,
				.format = DRM_FORMAT_ARGB8888,
				.stride = (uint32_t)stride,
			});
		wlr_texture_destroy(texture);
	}
	munmap(pixels, size);
	if (!copied) {
		close(fd);
		fail(frame, "The renderer could not read the displayed frame.");
		return;
	}
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW |
			F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
		close(fd);
		fail(frame, "Could not seal the screenshot image.");
		return;
	}
	zacos_snapshot_frame_v1_send_image(frame, fd, buffer->width, buffer->height,
		stride, wlr_output->transform, box.x, box.y, box.width, box.height);
	close(fd);
}

static const struct zacos_snapshot_manager_v1_interface manager_impl = {
	.capture = capture,
	.destroy = destroy_resource,
};

static void bind_manager(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(client,
		&zacos_snapshot_manager_v1_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

void snapshot_init(struct plat_server *server) {
	if (!wl_global_create(server->display, &zacos_snapshot_manager_v1_interface,
			1, server, bind_manager)) {
		wlr_log(WLR_ERROR, "Could not create the native Screen Snapshot interface.");
	}
}
