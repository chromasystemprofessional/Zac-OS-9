#ifndef ZACOS9_LOGFILTER_H
#define ZACOS9_LOGFILTER_H

#include <stdarg.h>
#include <stdio.h>
#include <wlr/util/log.h>

/*
 * wlroots' log callback, with consecutive identical messages collapsed: the
 * first is printed, the rest counted, and "previous message repeated N
 * times" follows when something else is logged. One line per pointer motion
 * ("Failed to pick cursor plane format") otherwise buries the rest of the
 * log.
 */
void logfilter_init(enum wlr_log_importance verbosity);

/* Where the lines go; NULL (the default) is stderr. */
void logfilter_set_output(FILE *out);

/* wlr_log_func_t; installed by logfilter_init(). */
void logfilter_log(enum wlr_log_importance importance, const char *fmt, va_list args);

/* Report a message still repeating (at exit, and in the test). */
void logfilter_flush(void);

#endif
