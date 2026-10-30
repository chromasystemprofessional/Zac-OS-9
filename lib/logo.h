#ifndef ZACOS9_LOGO_H
#define ZACOS9_LOGO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Menu-bar logo: 20×15 ARGB with alpha, exactly MBAR_ICON_W × MBAR_ICON_H. */
#define PL_LOGO_W 20
#define PL_LOGO_H 15
const uint32_t *logo_pixels(void);
/* The menu-bar logo from the active theme's menu-icon.png when it supplies
 * one, else the built-in Happy Zac. Same 20×15 ARGB layout. */
const uint32_t *logo_pixels_themed(void);

/* High-quality logo: 64×64 ARGB with full per-pixel alpha, for startup/installer. */
#define PL_LOGO_SIZE_HQ 64
const uint32_t *logo_pixels_hq(void);

#ifdef __cplusplus
}
#endif

#endif
