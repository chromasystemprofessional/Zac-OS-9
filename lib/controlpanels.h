#ifndef ZACOS9_CONTROLPANELS_H
#define ZACOS9_CONTROLPANELS_H

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
