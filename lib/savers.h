#ifndef ZACOS9_SAVERS_H
#define ZACOS9_SAVERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "draw.h"

/*
 * The screen savers: original pixel art, drawn at the screen's logical
 * size like everything else (zacos9-screensaver scales it up, nearest-
 * neighbour). The Screen Saver panel draws the same ones in its preview.
 * The setting is "screensaver=ID" in desktop.conf; "none" turns it off.
 */

int pl_saver_count(void);
const char *pl_saver_id(int index);   /* "patterns" */
const char *pl_saver_name(int index); /* "Desktop Patterns" */
/* The index for an id; 0 (the default) if unknown. */
int pl_saver_find(const char *id);

struct pl_saver;
/* A saver for a w×h screen; `seed` varies where it starts. */
struct pl_saver *pl_saver_new(int index, int w, int h, unsigned seed);
/* Draws the whole frame `seconds` after it started, into c's w×h area
 * starting at (c->x, c->y). */
void pl_saver_draw(struct pl_saver *s, struct pl_canvas *c, double seconds);
void pl_saver_free(struct pl_saver *s);

#ifdef __cplusplus
}
#endif

#endif
