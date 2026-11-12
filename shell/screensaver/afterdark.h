#ifndef ZACOS9_AFTERDARK_H
#define ZACOS9_AFTERDARK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "draw.h"

/*
 * The headless After Dark helper (emulation/afterdark): it runs the original
 * 68k modules and writes frame records to a pipe. zacos9-screensaver owns one
 * renderer per screen; the Screen Saver panel owns one for its preview.
 *
 * The helper path is found with pl_sibling_program(), so a build with cargo
 * missing simply has no helper and the saver falls back to its own art.
 */

struct ad_renderer;

struct ad_frame {
	int width, height;
	uint32_t ticks;
	const uint8_t *palette; /* 256 RGB triples */
	const uint8_t *pixels;  /* width * height, palette indices */
};

/* Whether the helper is installed, and its path. */
bool pl_afterdark_available(void);
bool pl_afterdark_program(char *out, size_t size);
/* The module title chosen in the Screen Saver panel, or false when unset. */
bool pl_afterdark_module(char *out, size_t size);
/* Where the imported modules live, for the panel's hint. */
bool pl_afterdark_library_dir(char *out, size_t size);

/* Starts the helper for `module` (a title in the library). NULL on failure. */
struct ad_renderer *ad_renderer_start(const char *module, int fps);
/* The same, running `program`; the tests use a fake helper. */
struct ad_renderer *ad_renderer_start_program(const char *program, const char *module, int fps);
/* Reads whatever the helper has produced without blocking. True when the
 * displayed frame changed. */
bool ad_renderer_take_frame(struct ad_renderer *r);
const struct ad_frame *ad_renderer_frame(const struct ad_renderer *r);
/* Draws the latest frame into (x, y, w, h), black bars and all. */
void ad_renderer_draw(const struct ad_renderer *r, struct pl_canvas *c, int x, int y, int w,
	int h);
void ad_renderer_stop(struct ad_renderer *r);

#ifdef __cplusplus
}
#endif

#endif
