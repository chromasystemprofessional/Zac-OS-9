#ifndef ZACOS9_APPFILTER_H
#define ZACOS9_APPFILTER_H

/*
 * Limited accounts (Mac OS 9's Multiple Users, gap plan 5.1): the programs
 * this person may use, from malcontent's app filter, which accountsservice
 * keeps outside their home and only administrators may change. The
 * Sniffer, menu bar and Pack leave out and refuse other programs. This is
 * not a security boundary: anything run from a terminal or a script still
 * runs.
 *
 * Entries are absolute paths of programs ("/usr/bin/gimp") and Flatpak
 * refs ("app/org.gimp.GIMP/x86_64/stable"), as malcontent stores them.
 * ZacOS's own programs (zacos9-*) are always allowed.
 */

#include <gio/gdesktopappinfo.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether this person's programs are limited (read once, at first use:
 * a change shows at their next login). ZACOS9_APP_FILTER, for tests,
 * stands in for malcontent: "allow:ENTRY:ENTRY…" or "block:ENTRY…". */
bool pl_app_filter_active(void);
/* Whether the desktop entry's program may be used. */
bool pl_app_allowed(GDesktopAppInfo *app);
/* The same, for a desktop file's path; true if it can't be read. */
bool pl_app_allowed_file(const char *desktop_file);
/* The entry malcontent keeps for this program (a path or Flatpak ref),
 * or NULL. Free with g_free(). */
char *pl_app_filter_entry(GDesktopAppInfo *app);

#ifdef __cplusplus
}
#endif

#endif
