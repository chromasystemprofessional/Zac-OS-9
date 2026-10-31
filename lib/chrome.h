#pragma once

/*
 * Theme image skins (the bundle's chrome PNG files). Missing files
 * keep the Platinum drawing: every loader returns NULL then.
 */

#include <stdbool.h>
#include <stdint.h>

struct pl_canvas;

/* The pixels of one bundle file, loaded once per theme. */
const uint32_t *chrome_file(const char *path, int *w, int *h);

/* Tile a skin over a rectangle, wrapping in both directions. Pure. */
void chrome_tile(struct pl_canvas *c, const uint32_t *px, int sw, int sh,
	int x0, int y0, int x1, int y1);

/* Fill a rectangle with a skin when the theme has one, else a flat colour. */
void chrome_fill(struct pl_canvas *c, int x0, int y0, int x1, int y1,
	const char *path, uint32_t fallback);
