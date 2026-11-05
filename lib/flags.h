#ifndef ZACOS9_FLAGS_H
#define ZACOS9_FLAGS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Small country flags for language lists, drawn at the bitmap UI's size.
 * A locale's region picks the flag; a language with no listed region gets
 * a globe, so every row has a mark without pretending to know a country.
 */

#include "draw.h"

#define PL_FLAG_W 20
#define PL_FLAG_H 13

enum pl_flag {
	PL_FLAG_NONE,
	PL_FLAG_CANADA,
	PL_FLAG_UNITED_KINGDOM,
	PL_FLAG_UNITED_STATES,
	PL_FLAG_FRANCE,
	PL_FLAG_ITALY,
	PL_FLAG_RUSSIA,
	PL_FLAG_SPAIN,
	PL_FLAG_BRAZIL,
	PL_FLAG_PORTUGAL,
	PL_FLAG_JAPAN,
	PL_FLAG_GLOBE,
};

enum pl_flag pl_flag_for_locale(const char *locale);
void pl_flag_paint(struct pl_canvas *c, int x, int y, enum pl_flag flag);

#ifdef __cplusplus
}
#endif

#endif
