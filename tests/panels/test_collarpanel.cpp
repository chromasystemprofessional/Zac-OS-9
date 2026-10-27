/*
 * The Collar control panel: its settings, the hot key recorder, and its
 * layout against measurements of the Control Strip panel.
 */
#include <QApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstring>

#include "collarpanel.h"
#include "settings.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static QString setting(const char *key) {
	char value[64];
	return pl_setting(key, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

static void click(PanelRadios &r, int i) {
	const QPoint p = r.buttons[i].pos + QPoint(4, 4);
	r.press(p);
	r.release(p);
}

int main(int argc, char *argv[]) {
	QTemporaryDir home;
	qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
	QApplication app(argc, argv);
	using namespace collarhotkey;

	check(fromKey(Qt::Key_F8, Qt::NoModifier) == "F8", "a function key works alone");
	check(fromKey(Qt::Key_K, Qt::NoModifier).isEmpty() &&
		fromKey(Qt::Key_K, Qt::ShiftModifier).isEmpty(), "a letter needs a modifier");
	check(fromKey(Qt::Key_K, Qt::ControlModifier) == "command+k", "Qt's Control is the command key");
	check(fromKey(Qt::Key_F5, Qt::AltModifier | Qt::ShiftModifier) == "option+shift+F5",
		"option and shift, in order");
	check(fromKey(Qt::Key_Shift, Qt::ShiftModifier).isEmpty(), "a modifier alone isn't a hot key");
	check(display("command+F8") == "command + F8" && display("command+option+k") == "command + option + K",
		"hot keys read as the Control Strip showed them");

	CollarPanel panel;
	check(panel.width() == 295 && panel.height() == 248, "the panel is the Control Strip panel's size");
	check(panel.visibility().selected == 0 && !panel.defineButton().enabled,
		"Show Collar by default; no hot key to define");
	check(panel.font().selected == 0, "module menus in the system font by default");

	Pixels px(CollarPanel::W, CollarPanel::H);
	panel.render(&px.c);
	const QImage &img = px.img;
	auto at = [&](int x, int y) { return QColor(img.pixel(x, y)); };
	const QColor white(Qt::white), line(0x88, 0x88, 0x88), black(Qt::black);
	check(at(2, 2) == white && at(262, 100) == white, "white, as the original");
	check(at(10, 10) == line && at(282, 150) == line && at(10, 160) == line && at(282, 236) == line &&
		at(15, 10) == line, "group boxes where the original has them");
	check(at(20, 10) == white, "group titles cut their box's top line");
	check(at(39, 117) == black && at(251, 139) == black && at(45, 117) == black,
		"the current hot key box");
	check(at(49, 117) == white, "its title cuts the box");
	bool preview = false;
	for (int x = 202; x < 202 + 63; x++) {
		preview |= at(x, 31 + 12) != white;
	}
	check(preview && at(201, 43) == white && at(202 + 63, 43) == white, "the Collar's preview");
	bool radio = false;
	for (int x = 22; x < 34; x++) {
		radio |= at(x, 26 + 6) != white && at(x, 66 + 6) != white;
	}
	check(radio, "three radio buttons");
	if (qEnvironmentVariableIsSet("ZACOS9_COLLARPANEL_PNG")) {
		img.save(qEnvironmentVariable("ZACOS9_COLLARPANEL_PNG"));
	}

	click(panel.visibility(), 1);
	check(setting("collar-visibility") == "hide", "Hide Collar is saved");
	click(panel.visibility(), 2);
	check(setting("collar-visibility") == "hotkey" && panel.defineButton().enabled,
		"Hot key to show/hide is saved and the hot key can be defined");

	QString offered;
	panel.askHotKey = [&](const QString &current) {
		offered = current;
		return QString("command+option+k");
	};
	panel.defineButton().clicked();
	check(offered == "command+F8", "the recorder starts from the current hot key");
	check(setting("collar-hotkey") == "command+option+k" && current() == "command+option+k",
		"a new hot key is saved");
	panel.askHotKey = [](const QString &) { return QString(); };
	panel.defineButton().clicked();
	check(setting("collar-hotkey") == "command+option+k", "Cancel keeps the hot key");
	panel.askHotKey = [](const QString &) { return QString(Default); };
	panel.defineButton().clicked();
	check(setting("collar-hotkey").isEmpty() && current() == "command+F8",
		"back to the default clears the setting");

	click(panel.visibility(), 0);
	check(setting("collar-visibility").isEmpty() && !panel.defineButton().enabled,
		"Show Collar is the default again");

	panel.font().chosen(1);
	check(setting("collar-menu-font") == "views", "the views font is saved");
	panel.font().chosen(0);
	check(setting("collar-menu-font").isEmpty(), "the system font is the default");

	HotKeyDialog dialog("command+F8");
	dialog.press(Qt::Key_J, Qt::NoModifier);
	check(dialog.chosen() == "command+F8", "the recorder ignores keys that can't be hot keys");
	dialog.press(Qt::Key_F9, Qt::ControlModifier | Qt::ShiftModifier);
	check(dialog.chosen() == "command+shift+F9", "the recorder takes the key pressed");

	return fails ? 1 : 0;
}
