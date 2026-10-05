#ifndef ZACOS9_STARTUP_H
#define ZACOS9_STARTUP_H

#include <stdbool.h>

struct plat_server;
struct plat_output;

/* Show the startup screen until the shell is up. */
void startup_begin(struct plat_server *server);
/* A layer surface mapped; the menu bar and desktop end the startup. */
void startup_surface_mapped(const char *layer_namespace);
bool startup_active(void);
/* Only this output's startup artwork is visible during its frame commit. */
void startup_output_frame(struct plat_output *output, bool visible);
void startup_output_destroy(struct plat_output *output);

#endif
