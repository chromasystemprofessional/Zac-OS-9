#ifndef ZACOS9_CONTROLPANELS_H
#define ZACOS9_CONTROLPANELS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "icons.h"

/*
 * The control panels, for the Control Panels folder and the Zac menu: the
 * installed desktop entries with X-ZacOS9-ControlPanel in Categories=,
 * the Mac's Control Panels folder as files. A new panel needs only its
 * desktop entry (and its icon in pl_icon_for_program()); one marked
 * NoDisplay=true is left out.
 */

struct pl_control_panel {
	char *id;      /* the desktop file's ID: "zacos9-energysaver.desktop" */
	char *name;    /* Name=: "Nap Time" */
	char *program; /* Exec='s program: "zacos9-controlpanel" */
	char *args;    /* the rest of Exec=, after a space: " energysaver", or "" */
	enum pl_icon_kind icon;
};

/* By name, ignoring case, as the Mac's folder was. Free with
 * pl_control_panels_free(). */
struct pl_control_panel *pl_control_panels(size_t *count);
void pl_control_panels_free(struct pl_control_panel *panels, size_t count);

/* The panels the user has turned off: the system installed the panel, and
 * the user's own copy of its entry hides it (NoDisplay=true, as turning one
 * off below writes, or Hidden=true by hand). By name too, with the name,
 * program and icon the panel has when it is on. */
struct pl_control_panel *pl_control_panels_disabled(size_t *count);

/* Turns a panel off or on, as dragging it into or out of Control Panels
 * (Disabled) did: off copies the system's entry into the user's data folder
 * marked NoDisplay=true, which the reader above leaves out; on removes that
 * copy again. False if the system doesn't install that panel, or the copy
 * can't be written. */
bool pl_control_panel_set_enabled(const char *id, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
