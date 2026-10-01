#ifndef PLATINUM_PIXBUF_H
#define PLATINUM_PIXBUF_H

#include <stdint.h>
#include <wlr/interfaces/wlr_buffer.h>

/* A CPU-side ARGB8888 (premultiplied) buffer usable as a wlr_buffer. */
struct plat_pixbuf {
	struct wlr_buffer base;
	uint32_t *data; /* width*height pixels, row-major, zero-initialised */
	int width, height;
};

struct plat_pixbuf *pixbuf_create(int width, int height);

#endif
