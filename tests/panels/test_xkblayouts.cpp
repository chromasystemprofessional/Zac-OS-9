/* The Keyboard panel's layouts: every layout and variant xkb lists, by name,
 * and the ones in use (the panel's choice, else Debian's). */
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>

#include "settings.h"
#include "xkblayouts.h"

static int failures;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	qputenv("XDG_CONFIG_HOME", QFile::encodeName(dir.path()));
	QFile rules(dir.filePath("rules.xml"));
	rules.open(QIODevice::WriteOnly);
	rules.write(R"(<?xml version="1.0"?><xkbConfigRegistry><layoutList>
<layout><configItem><name>us</name><shortDescription>en</shortDescription>
<description>English (US)</description></configItem><variantList>
<variant><configItem><name>dvorak</name><description>English (Dvorak)</description></configItem></variant>
</variantList></layout>
<layout><configItem><name>de</name><description>German</description></configItem></layout>
<layout><configItem><name>br</name><description>Portuguese (Brazil)</description></configItem></layout>
</layoutList><optionList><group><configItem><name>ctrl</name><description>Ctrl position</description>
</configItem></group></optionList></xkbConfigRegistry>)");
	rules.close();
	qputenv("ZACOS9_XKB_RULES", QFile::encodeName(rules.fileName()));
	/* No extra list here: this check is the system XML's alone. */
	qputenv("ZACOS9_XKB_EXTRA", QByteArray());
	const QList<XkbLayout> layouts = xkbLayouts();
	check(layouts.size() == 4, "layouts and their variants, not options");
	check(layouts.value(0).name == "English (Dvorak)" && layouts.value(0).code == "us(dvorak)" &&
		layouts.value(1).code == "us" && layouts.value(2).code == "de" &&
		layouts.value(3).code == "br",
		"sorted by name, variants as layout(variant)");

	/* ZacOS's own list adds variants the system's XML leaves out, without
	 * repeating the ones it has (us(dvorak) here, br there). */
	QFile extra(dir.filePath("extra.xml"));
	extra.open(QIODevice::WriteOnly);
	extra.write(R"(<?xml version="1.0"?><xkbConfigRegistry><layoutList>
<layout><configItem><name>us</name><description>English (US)</description></configItem><variantList>
<variant><configItem><name>dvorak</name><description>English (Dvorak)</description></configItem></variant>
</variantList></layout>
<layout><configItem><name>br</name><description>Portuguese (Brazil)</description></configItem><variantList>
<variant><configItem><name>abnt2</name><description>Portuguese (Brazil, ABNT2)</description></configItem></variant>
</variantList></layout>
</layoutList></xkbConfigRegistry>)");
	extra.close();
	qputenv("ZACOS9_XKB_EXTRA", QFile::encodeName(extra.fileName()));
	const QList<XkbLayout> withExtra = xkbLayouts();
	check(withExtra.size() == 5, "an extra layout joins the list");
	const auto abnt2 = std::find_if(withExtra.begin(), withExtra.end(),
		[](const XkbLayout &l) { return l.code == "br(abnt2)"; });
	check(abnt2 != withExtra.end() && abnt2->name == "Portuguese (Brazil, ABNT2)",
		"the ABNT2 variant, with its name");
	check(std::count_if(withExtra.begin(), withExtra.end(),
		[](const XkbLayout &l) { return l.code == "br" || l.code == "us(dvorak)"; }) == 2,
		"a layout the system already lists is not repeated");

	QFile defaults(dir.filePath("keyboard"));
	defaults.open(QIODevice::WriteOnly);
	defaults.write("XKBLAYOUT=\"de\"\nXKBVARIANT=\"\"\n");
	defaults.close();
	qputenv("ZACOS9_KEYBOARD_DEFAULTS", QFile::encodeName(defaults.fileName()));
	check(xkbLayoutsInUse() == QStringList{ "de" }, "nothing chosen: Debian's layout");
	pl_setting_set("keyboard-layouts", "us,us(dvorak)");
	check(xkbLayoutsInUse() == (QStringList{ "us", "us(dvorak)" }), "else the panel's");

	qunsetenv("ZACOS9_XKB_RULES");
	qunsetenv("ZACOS9_XKB_EXTRA");
	if (QFile::exists("/usr/share/X11/xkb/rules/evdev.xml")) {
		const QList<XkbLayout> system = xkbLayouts();
		check(system.size() > 100, "this system's xkb lists its layouts");
		check(QFile::exists("/usr/share/X11/xkb/symbols/br") &&
			std::any_of(system.begin(), system.end(), [](const XkbLayout &l) {
				return l.code == "br(abnt2)" &&
					l.name == "Portuguese (Brazil, ABNT2)";
			}),
			"ZacOS adds Debian's ABNT2 variant to the list");
	}
	return failures ? 1 : 0;
}
