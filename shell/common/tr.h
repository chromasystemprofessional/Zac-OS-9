#pragma once

/*
 * QString from the message catalog (lib/i18n.h). Use tr9() where a QString
 * is wanted and _() where a const char* is. It is not called tr(): every
 * QWidget already has QObject::tr, which would win inside its member
 * functions and quietly return English.
 */
#include <QString>

#include "i18n.h"

inline QString tr9(const char *s) {
	return QString::fromUtf8(zacos9_tr(s));
}

inline QString tr9(const char *singular, const char *plural, unsigned long n) {
	return QString::fromUtf8(zacos9_trn(singular, plural, n));
}
