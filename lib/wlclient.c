#define _GNU_SOURCE
#include "wlclient.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct pl_shm_buffer *b = data;
	b->busy = false;
	if (b->released) {
		b->released(b, b->data);
	}
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

bool pl_shm_buffer_make(struct pl_shm_buffer *b, struct wl_shm *shm, const char *name,
		int width, int height, uint32_t format) {
	const int stride = width * 4;
	b->wl = NULL;
	b->px = NULL;
	b->busy = false;
	b->size = (size_t)stride * height;
	const int fd = memfd_create(name, MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)b->size) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return false;
	}
	b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->px == MAP_FAILED) {
		close(fd);
		b->px = NULL;
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	return true;
}

void pl_shm_buffer_free(struct pl_shm_buffer *b) {
	if (b->wl) {
		wl_buffer_destroy(b->wl);
		munmap(b->px, b->size);
	}
	memset(b, 0, sizeof(*b));
}

struct pl_shm_buffer *pl_shm_buffer_pick(struct pl_shm_buffer pair[2]) {
	return !pair[0].busy ? &pair[0] : !pair[1].busy ? &pair[1] : NULL;
}
