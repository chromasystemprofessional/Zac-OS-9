#ifndef ZACOS9_LOGO_H
#define ZACOS9_LOGO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* The ZacOS 9 logo, 16x16 ARGB (alpha 0 or 255). */
#define PL_LOGO_SIZE 16
const uint32_t *logo_pixels(void);

#ifdef __cplusplus
}
#endif

#endif
