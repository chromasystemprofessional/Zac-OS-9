#pragma once

#include <stdbool.h>

#include "afp.h"

/* Serves the session's open volume at `mountpoint` through FUSE until it
 * is unmounted; `source` names it ("afp://server/volume"). Goes into the
 * background once mounted unless `foreground`. fuse_main's result. */
int afpfs_run(struct afp *session, const char *mountpoint, const char *source, bool foreground);
