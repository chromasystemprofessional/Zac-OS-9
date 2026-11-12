/* The After Dark frame pipe (shell/screensaver/afterdark.c): the parent reads
 * the helper's records without blocking, keeps the last whole frame, and
 * scales it into a canvas with black bars. The test runs itself as the fake
 * helper, so it needs no module or Rust toolchain. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "afterdark.h"

static int failures;

static void check(bool ok, const char *what) {
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

static void put_u16(unsigned char *p, unsigned v) {
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_u32(unsigned char *p, unsigned v) {
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void write_all(const void *data, size_t len) {
	const unsigned char *p = data;
	while (len > 0) {
		const ssize_t n = write(STDOUT_FILENO, p, len);
		if (n <= 0) {
			_exit(1);
		}
		p += n;
		len -= (size_t)n;
	}
}

static void write_header(unsigned w, unsigned h, unsigned tick) {
	unsigned char header[12];
	put_u32(header, 0x31464441u);
	put_u16(header + 4, w);
	put_u16(header + 6, h);
	put_u32(header + 8, tick);
	write_all(header, sizeof(header));
}

static void write_palette(void) {
	unsigned char palette[256 * 3] = { 0 };
	palette[3] = 0xff; /* index 1: red */
	palette[5 * 3 + 2] = 0xff; /* index 5: blue */
	write_all(palette, sizeof(palette));
}

/* Two 4x3 frames, red at (2,1), blue at (0,2), with different tick counts
 * so the test can tell them apart. Small enough to arrive in one read. */
static void small_helper(void) {
	unsigned char pixels[4 * 3] = { 0 };
	pixels[1 * 4 + 2] = 1;
	pixels[2 * 4 + 0] = 5;
	for (unsigned tick = 7; tick <= 8; tick++) {
		write_header(4, 3, tick);
		write_palette();
		write_all(pixels, sizeof(pixels));
	}
	_exit(0);
}

/* 480x360 is larger than the pipe buffer, so the reader must take several
 * reads per frame. Pause before the last part of the first frame: a reader
 * that counts total bytes as "remaining" would call it complete early and
 * then read pixel data as the next header. */
static void large_helper(void) {
	enum { W = 480, H = 360, SPLIT = 131072 };
	unsigned char *pixels = calloc((size_t)W * H, 1);
	if (!pixels) {
		_exit(1);
	}
	pixels[0] = 1;
	pixels[(size_t)(H - 1) * W + (W - 1)] = 5;
	for (unsigned tick = 7; tick <= 8; tick++) {
		write_header(W, H, tick);
		write_palette();
		write_all(pixels, SPLIT);
		if (tick == 7) {
			struct timespec wait = { 0, 250000000 };
			nanosleep(&wait, NULL);
		}
		write_all(pixels + SPLIT, (size_t)W * H - SPLIT);
	}
	free(pixels);
	_exit(0);
}

static void fake_helper(const char *module) {
	if (strcmp(module, "large") == 0) {
		large_helper();
	}
	small_helper();
}

static const struct ad_frame *wait_frame(struct ad_renderer *r, unsigned ticks) {
	for (int i = 0; i < 400; i++) {
		ad_renderer_take_frame(r);
		const struct ad_frame *frame = ad_renderer_frame(r);
		if (frame && frame->ticks == ticks) {
			return frame;
		}
		struct timespec wait = { 0, 10000000 };
		nanosleep(&wait, NULL);
	}
	return NULL;
}

int main(int argc, char **argv) {
	if (argc > 1 && strcmp(argv[1], "--module") == 0) {
		fake_helper(argc > 2 ? argv[2] : "small");
	}

	/* The library path is the XDG one the helper and the panel share. */
	setenv("XDG_DATA_HOME", "/tmp/zacos9-afterdark-test-data", 1);
	char dir[512];
	check(pl_afterdark_library_dir(dir, sizeof(dir)) &&
		strcmp(dir, "/tmp/zacos9-afterdark-test-data/after-dark/modules") == 0,
		"the library is under XDG_DATA_HOME");

	struct ad_renderer *r = ad_renderer_start_program(argv[0], "small", 30);
	check(r != NULL, "the helper starts");
	if (r) {
		const struct ad_frame *frame = wait_frame(r, 7);
		check(frame != NULL, "a whole frame arrives");
		if (frame) {
			check(frame->width == 4 && frame->height == 3 && frame->ticks == 7,
				"the first call returns the first frame, tick 7");
			const struct ad_frame *next = wait_frame(r, 8);
			check(next != NULL, "the next call returns the second frame, tick 8");
			check(frame->palette[3] == 0xff && frame->palette[15] == 0 &&
				frame->palette[17] == 0xff,
				"its palette is the one the helper wrote");
			check(frame->pixels[1 * 4 + 2] == 1 && frame->pixels[2 * 4] == 5,
				"its palette indices are the ones the helper wrote");

			/* 4:3 in a 10x10 box: 10x7, one black row above and two
			 * below. The centre maps to source (2,1), red. */
			uint32_t pixels[10 * 10];
			struct pl_canvas c = { pixels, 10, 0, 0, 10, 10, NULL };
			ad_renderer_draw(r, &c, 0, 0, 10, 10);
			check(pixels[0] == 0xFF000000u, "the letterbox is black");
			check(pixels[5 * 10 + 5] == 0xFFFF0000u, "the centre is the red pixel");
			check(pixels[9 * 10 + 9] == 0xFF000000u,
				"the bottom bar is still black");
		}
		ad_renderer_stop(r);
	}

	/* The same two frames, but big enough that the reader gets them in
	 * several chunks. */
	r = ad_renderer_start_program(argv[0], "large", 30);
	check(r != NULL, "the large helper starts");
	if (r) {
		const struct ad_frame *frame = wait_frame(r, 7);
		check(frame != NULL, "a frame larger than the pipe arrives");
		if (frame) {
			check(frame->width == 480 && frame->height == 360,
				"it keeps the helper's size");
			check(frame->pixels[0] == 1 &&
				frame->pixels[(360 - 1) * 480 + (480 - 1)] == 5,
				"both ends of its pixels were read");
		}
		const struct ad_frame *next = wait_frame(r, 8);
		check(next != NULL, "the frame after it is read in step");
		ad_renderer_stop(r);
	}

	return failures ? 1 : 0;
}
