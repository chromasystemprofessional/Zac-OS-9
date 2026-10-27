/*
 * zacos9-controlpanel: the control panels without a program of their own,
 * one per run (`zacos9-controlpanel NAME`, Mouse when there's none): each is
 * a file of this folder that registers itself (panelregistry.h). Each
 * writes ~/.config/zacos9/desktop.conf; zacos9-wm applies the mouse,
 * keyboard, screen and sleep settings as they change (compositor/src/
 * prefs.c, idle.c). System volume goes through PulseAudio/PipeWire (pactl).
 */
#include "controlpanel.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	const QString which = argc > 1 ? QString::fromUtf8(argv[1]) : QString("mouse");
	QApplication app(argc, argv);
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	/* Each panel is its own app to the menu bar (zacos9-mouse...). */
	QGuiApplication::setDesktopFileName("zacos9-" + which);

	/* The panel named, else Mouse, as when none is. */
	const PanelInfo *panel = nullptr, *mouse = nullptr;
	for (const PanelInfo &p : panels()) {
		if (which == p.name) {
			panel = &p;
		}
		if (strcmp(p.name, "mouse") == 0) {
			mouse = &p;
		}
	}
	if (!panel) {
		panel = mouse;
	}
	QApplication::setApplicationName(panel->title);
	if (panel->alerts) {
		platinumShellInit();
	}
	const std::unique_ptr<QWidget> window = panel->make();
	window->show();
	return app.exec();
}
