#ifndef PLATINUM_STARTUP_H
#define PLATINUM_STARTUP_H

#include <stdbool.h>

struct plat_server;

/* Show the startup screen until the shell is up. */
void startup_begin(struct plat_server *server);
/* A layer surface mapped; the menu bar and desktop end the startup. */
void startup_surface_mapped(const char *layer_namespace);
bool startup_active(void);

#endif
