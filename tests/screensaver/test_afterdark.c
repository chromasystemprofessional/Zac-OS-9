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

/* The fake helper. A 4x3 frame, red at (2,1), blue at (0,2). */
static void fake_helper(void) {
	unsigned char header[12];
	put_u32(header, 0x31464441u);
	put_u16(header + 4, 4);
	put_u16(header + 6, 3);
	put_u32(header + 8, 7);
	write_all(header, sizeof(header));
	unsigned char palette[256 * 3] = { 0 };
	palette[3] = 0xff; /* index 1: red */
	palette[5 * 3 + 2] = 0xff; /* index 5: blue */
	write_all(palette, sizeof(palette));
	unsigned char pixels[4 * 3] = { 0 };
	pixels[1 * 4 + 2] = 1;
	pixels[2 * 4 + 0] = 5;
	write_all(pixels, sizeof(pixels));
	_exit(0);
}

int main(int argc, char **argv) {
	if (argc > 1 && strcmp(argv[1], "--module") == 0) {
		fake_helper();
	}

	/* The library path is the XDG one the helper and the panel share. */
	setenv("XDG_DATA_HOME", "/tmp/zacos9-afterdark-test-data", 1);
	char dir[512];
	check(pl_afterdark_library_dir(dir, sizeof(dir)) &&
		strcmp(dir, "/tmp/zacos9-afterdark-test-data/after-dark/modules") == 0,
		"the library is under XDG_DATA_HOME");

	struct ad_renderer *r = ad_renderer_start_program(argv[0], "test", 30);
	check(r != NULL, "the helper starts");
	if (r) {
		const struct ad_frame *frame = NULL;
		for (int i = 0; i < 200 && !frame; i++) {
			ad_renderer_take_frame(r);
			frame = ad_renderer_frame(r);
			if (!frame) {
				struct timespec wait = { 0, 10000000 };
				nanosleep(&wait, NULL);
			}
		}
		check(frame != NULL, "a whole frame arrives");
		if (frame) {
			check(frame->width == 4 && frame->height == 3 && frame->ticks == 7,
				"its header says 4x3, tick 7");
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

	return failures ? 1 : 0;
}
