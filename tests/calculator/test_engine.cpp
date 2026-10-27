/* The Calculator's arithmetic, as Mac OS's: left to right, = repeats, C and E. */
#include <cstdio>

#include "engine.h"

static int fails;

static QString keys(const char *typed) {
	CalcEngine e;
	for (const char *k = typed; *k; k++) {
		e.press(QChar(*k));
	}
	return e.display();
}

static void check(const char *typed, const char *want) {
	const QString got = keys(typed);
	const bool ok = got == QString(want);
	std::printf("%s  %-16s -> %s%s%s\n", ok ? "ok  " : "FAIL", typed, qPrintable(got),
		ok ? "" : ", wanted ", ok ? "" : want);
	fails += !ok;
}

int main() {
	check("12+30=", "42");
	check("2+3*4=", "20");          /* left to right, as you go */
	check("10/4=", "2.5");
	check("2+3==", "8");             /* = again: + 3 again */
	check("7-9=", "-2");
	check("1/0=", "Error");
	check("1/0=C5", "5");            /* C clears an error */
	check("12+34E5=", "17");         /* E clears only the entry */
	check("0.1+0.2=", "0.3");
	check("..5*2=", "1");            /* a second point does nothing */
	check("123\b4", "124");          /* Backspace takes back a digit */
	check("99999999999999", "999999999999"); /* twelve digits */
	check("999999999999*1000=", "1e+15");   /* rounded to fit */
	CalcEngine pasted;
	const bool ok = pasted.enter(" 3.5 ") && pasted.press('*') && pasted.press('2') && pasted.press('=') &&
		pasted.display() == "7" && !pasted.enter("seven");
	std::printf("%s  a pasted number counts; text doesn't\n", ok ? "ok  " : "FAIL");
	fails += !ok;
	return fails ? 1 : 0;
}
