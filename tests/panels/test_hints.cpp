/* Hint Bubbles in panels: PanelHost answers a tooltip event with the hint
 * of the control under the pointer (panelkit.cpp). */
#include <QApplication>
#include <QElapsedTimer>
#include <QHelpEvent>
#include <QToolTip>
#include <QWidget>
#include <cstdio>

#include "panelkit.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static bool tip(QWidget *w, QPoint p) {
	QHelpEvent e(QEvent::ToolTip, p, w->mapToGlobal(p));
	QApplication::sendEvent(w, &e);
	return QToolTip::isVisible();
}

int main(int argc, char **argv) {
	qputenv("XDG_CONFIG_HOME", "/nonexistent-zacos9-test");
	QApplication app(argc, argv);
	QWidget w;
	w.resize(300, 200);
	PanelHost host(&w);
	PanelCheckbox box("Mute", QPoint(10, 10));
	box.hint = "Turns all sound off.";
	host.checks.push_back(&box);
	PanelButton button("Select…", QRect(10, 100, 76, 20));
	host.buttons.push_back(&button); /* no hint */
	const QRect list(200, 10, 50, 50);
	host.hintAreas.push_back({ &list, "A list." });
	w.show();

	CHECK(host.hintAt(QPoint(14, 14)) == "Turns all sound off.");
	CHECK(host.hintAt(QPoint(220, 30)) == "A list.");
	CHECK(host.hintAt(QPoint(20, 110)).isEmpty());
	CHECK(host.hintAt(QPoint(150, 150)).isEmpty());

	CHECK(tip(&w, QPoint(14, 14)));
	CHECK(QToolTip::text() == "Turns all sound off.");
	tip(&w, QPoint(150, 150)); /* nothing there: the bubble goes */
	QElapsedTimer t;
	t.start();
	while (QToolTip::isVisible() && t.elapsed() < 2000) {
		app.processEvents(QEventLoop::AllEvents, 50);
	}
	CHECK(!QToolTip::isVisible());
	std::puts("ok: panel controls show their Hint Bubbles");
	return 0;
}
