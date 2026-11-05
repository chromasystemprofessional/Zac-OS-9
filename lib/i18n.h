#ifndef ZACOS9_I18N_H
#define ZACOS9_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Message translation, through gettext (the catalogs are in po/). A program
 * calls zacos9_i18n_init() once, after the locale is set and before it makes
 * windows; strings marked with _() or zacos9_tr() then come back in the
 * language LC_MESSAGES or LANGUAGE names, and in English when the catalog
 * has none. N_() only marks a literal that is translated later, as a static
 * table of titles is.
 */
void zacos9_i18n_init(void);
const char *zacos9_tr(const char *msgid);
const char *zacos9_trn(const char *singular, const char *plural, unsigned long n);

#ifndef _
#define _(s) zacos9_tr(s)
#endif
#ifndef N_
#define N_(s) (s)
#endif

#ifdef __cplusplus
}
#endif

#endif
