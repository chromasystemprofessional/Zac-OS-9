#include "i18n.h"

#include <libintl.h>
#include <locale.h>
#include <stdlib.h>

#ifndef ZACOS9_LOCALEDIR
#define ZACOS9_LOCALEDIR "/usr/share/locale"
#endif

void zacos9_i18n_init(void) {
	/* LC_MESSAGES only: numbers, dates and sorting stay as the Numbers and
	 * Text panels set them, and a program's own parsing doesn't move. */
	setlocale(LC_MESSAGES, "");
	/* A build tree points this at its compiled catalogs; installed programs
	 * use the prefix. */
	const char *dir = getenv("ZACOS9_LOCALEDIR");
	bindtextdomain("zacos9", dir && *dir ? dir : ZACOS9_LOCALEDIR);
	bind_textdomain_codeset("zacos9", "UTF-8");
}

const char *zacos9_tr(const char *msgid) {
	return dgettext("zacos9", msgid);
}

const char *zacos9_trn(const char *singular, const char *plural, unsigned long n) {
	return dngettext("zacos9", singular, plural, n);
}
