#ifndef ZACOS9_PFONT_H
#define ZACOS9_PFONT_H

/* Load a .pfont file (a theme's bitmap font) for the text renderer. The font
 * is never freed: a theme's fonts last as long as the program. NULL if the
 * file is missing or malformed. */
struct pl_bitmap_font *pfont_load(const char *path);

#endif
