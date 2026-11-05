#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct plat_text;

/* Whether the string has a character from an Arabic block. */
bool pl_has_arabic(const char *utf8);

/* Shape a line that contains Arabic: HarfBuzz joins the letters, FreeType
 * rasterises the glyphs, and the runs are placed in visual order. NULL when
 * no Arabic face is installed, so the caller can try its old fallback. */
struct plat_text *pl_shape_arabic(const char *utf8, int height, int baseline,
	int pixels, bool bold);

#ifdef __cplusplus
}
#endif
