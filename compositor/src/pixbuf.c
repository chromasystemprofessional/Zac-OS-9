#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>

#include "pixbuf.h"

static void pixbuf_destroy(struct wlr_buffer *wlr_buffer) {
	struct plat_pixbuf *buf = wl_container_of(wlr_buffer, buf, base);
	free(buf->data);
	free(buf);
}

static bool pixbuf_begin_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride) {
	struct plat_pixbuf *buf = wl_container_of(wlr_buffer, buf, base);
	*data = buf->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = (size_t)buf->width * 4;
	return true;
}

static void pixbuf_end_access(struct wlr_buffer *wlr_buffer) {
}

static const struct wlr_buffer_impl pixbuf_impl = {
	.destroy = pixbuf_destroy,
	.begin_data_ptr_access = pixbuf_begin_access,
	.end_data_ptr_access = pixbuf_end_access,
};

struct plat_pixbuf *pixbuf_create(int width, int height) {
	struct plat_pixbuf *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		return NULL;
	}
	buf->data = calloc((size_t)width * height, sizeof(uint32_t));
	if (!buf->data) {
		free(buf);
		return NULL;
	}
	buf->width = width;
	buf->height = height;
	wlr_buffer_init(&buf->base, &pixbuf_impl, width, height);
	return buf;
}
