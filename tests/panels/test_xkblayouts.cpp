/* The Keyboard panel's layouts: every layout and variant xkb lists, by name,
 * and the ones in use (the panel's choice, else Debian's). */
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
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
</layoutList><optionList><group><configItem><name>ctrl</name><description>Ctrl position</description>
</configItem></group></optionList></xkbConfigRegistry>)");
	rules.close();
	qputenv("ZACOS9_XKB_RULES", QFile::encodeName(rules.fileName()));
	const QList<XkbLayout> layouts = xkbLayouts();
	check(layouts.size() == 3, "layouts and their variants, not options");
	check(layouts.value(0).name == "English (Dvorak)" && layouts.value(0).code == "us(dvorak)" &&
		layouts.value(1).code == "us" && layouts.value(2).code == "de",
		"sorted by name, variants as layout(variant)");

	QFile defaults(dir.filePath("keyboard"));
	defaults.open(QIODevice::WriteOnly);
	defaults.write("XKBLAYOUT=\"de\"\nXKBVARIANT=\"\"\n");
	defaults.close();
	qputenv("ZACOS9_KEYBOARD_DEFAULTS", QFile::encodeName(defaults.fileName()));
	check(xkbLayoutsInUse() == QStringList{ "de" }, "nothing chosen: Debian's layout");
	pl_setting_set("keyboard-layouts", "us,us(dvorak)");
	check(xkbLayoutsInUse() == (QStringList{ "us", "us(dvorak)" }), "else the panel's");

	qunsetenv("ZACOS9_XKB_RULES");
	if (QFile::exists("/usr/share/X11/xkb/rules/evdev.xml")) {
		check(xkbLayouts().size() > 100, "this system's xkb lists its layouts");
	}
	return failures ? 1 : 0;
}
