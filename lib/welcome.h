#ifndef ZACOS9_WELCOME_H
#define ZACOS9_WELCOME_H

#include "draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The picture on the Welcome screen (compositor/src/startup.c): a modern
 * computer - a slim display on a stand, showing a ZacOS 9 desktop, with a
 * keyboard and a mouse - where Mac OS's own Welcome screen showed a
 * classic Mac. Drawn as vectors (cairo), so it is crisp at any size.
 */
#define PL_WELCOME_ART_W 176 /* its design size, in pixels at scale 1 */
#define PL_WELCOME_ART_H 132

/* Paints it with its top left at (x, y) in `c`'s coordinates, scaled by
 * `scale` (1.0 is PL_WELCOME_ART_W x PL_WELCOME_ART_H), over what is there. */
void pl_welcome_art(struct pl_canvas *c, int x, int y, double scale);

#ifdef __cplusplus
}
#endif

#endif
