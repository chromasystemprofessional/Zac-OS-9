#ifndef ZACOS9_LOGO_H
#define ZACOS9_LOGO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Menu-bar logo: 16×16 ARGB, exactly MBAR_ICON_SIZE. */
#define PL_LOGO_SIZE 16
const uint32_t *logo_pixels(void);

/* High-quality logo: 64×64 ARGB with full per-pixel alpha, for startup/installer. */
#define PL_LOGO_SIZE_HQ 64
const uint32_t *logo_pixels_hq(void);

#ifdef __cplusplus
}
#endif

#endif
