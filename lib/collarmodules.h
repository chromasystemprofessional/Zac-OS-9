#ifndef ZACOS9_COLLARMODULES_H
#define ZACOS9_COLLARMODULES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "icons.h"

/*
 * The Collar modules, for the Collar Modules folder and disabling them: the
 * installed desktop entries with X-ZacOS9-CollarModule in Categories=.
 * A new module needs only its desktop entry (and its icon in
 * pl_icon_for_program()); one marked NoDisplay=true is left out.
 */

struct pl_collar_module {
	char *id;      /* the desktop file's ID: "zacos9-volume-collar.desktop" */
	char *name;    /* Name=: "Volume" */
	char *program; /* Exec='s program: "zacos9-collarmodule" */
	char *args;    /* the rest of Exec=, after a space: " volume", or "" */
	enum pl_icon_kind icon;
};

/* By name, ignoring case, as the Mac's folder was. Free with
 * pl_collar_modules_free(). */
struct pl_collar_module *pl_collar_modules(size_t *count);
void pl_collar_modules_free(struct pl_collar_module *modules, size_t count);

/* The modules the user has turned off: the system installed the module, and
 * the user's own copy of its entry hides it. */
struct pl_collar_module *pl_collar_modules_disabled(size_t *count);

/* Turns a module off or on, as dragging it into or out of Collar Modules
 * (Disabled) did: off copies the system's entry into the user's data folder
 * marked NoDisplay=true; on removes that copy again. */
bool pl_collar_module_set_enabled(const char *id, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
