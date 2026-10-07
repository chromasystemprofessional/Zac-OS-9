#ifndef ZACOS9_EXTENSIONS_H
#define ZACOS9_EXTENSIONS_H

#include <stdbool.h>

#include "icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One feature a set of kernel modules provides, as people know it:
 * "Bluetooth", not btusb. See lib/extensions.c. */
struct pl_extension {
	const char *key;  /* stable id, e.g. "bluetooth" */
	const char *name; /* e.g. "Bluetooth" */
	enum pl_icon_kind icon;
	const char *description;
	const char *const *modules; /* NULL-terminated; "snd_hda_codec_*" is a prefix */
};

extern const struct pl_extension pl_extensions[];
extern const int pl_n_extensions;

/* The entry a module belongs to, or NULL. '-' and '_' are the same. */
const struct pl_extension *pl_extension_for_module(const char *module);
/* The entry with this key, or NULL. */
const struct pl_extension *pl_extension_find(const char *key);

#ifdef __cplusplus
}
#endif

#endif
