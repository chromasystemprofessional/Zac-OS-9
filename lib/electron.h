#ifndef ZACOS9_ELECTRON_H
#define ZACOS9_ELECTRON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/*
 * Electron apps (VS Code, ...) and the global menu. Electron gives its menus
 * to the menu bar (com.canonical.AppMenu.Registrar) only when it runs under
 * X11, so the desktop starts Electron apps there, drawing at the screen's
 * scale; zacos9-wm shows such windows at full resolution (xwayland.c).
 * See docs/app-integration.md.
 */

/* Whether `program` (a path, or a name on $PATH) is an Electron app: an
 * Electron build keeps its app in resources/app.asar or resources/app next
 * to the binary (or one folder up, beside a launcher script in bin/). */
bool pl_electron_app(const char *program);

/* VS Code and its relatives show the menu bar their menus only with
 * "window.titleBarStyle": "native" (their default title bar draws the menus
 * itself). For a program that is one of them: where its settings.json is. */
bool pl_code_settings_path(const char *program, char *out, size_t size);

/* Sets "window.titleBarStyle": "native" in that settings.json, unless the
 * person chose a title bar style there already (either way). True if it
 * wrote it. pl_electron_command calls these for every launch. */
bool pl_code_use_native_title_bar(const char *settings);

/* The screen's scale for X11 programs, from zacos9-wm's outputs file: the
 * whole-number scale, 1 at 1.25 and 1.5 or if unknown. */
int pl_screen_scale(void);

/* For a command line (a desktop entry's Exec, or a shell command, either
 * optionally starting with "exec") whose program is an Electron app: a
 * copy with the X11 and scale arguments after the program, to be freed by
 * the caller. NULL if the program isn't an Electron app. */
char *pl_electron_command(const char *command);

#ifdef __cplusplus
}
#endif

#endif
