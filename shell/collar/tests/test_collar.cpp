#include "collar.h"
#include "panelkit.h"
#include "settings.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QWindow>

static int failures;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static void settle(int duration = 80) {
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < duration) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
}

static void mouse(QWidget &widget, QEvent::Type type, QPoint point, QPoint global,
		Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	QMouseEvent event(type, point, global, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
		type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
	QApplication::sendEvent(&widget, &event);
}

static void click(QWidget &widget, QPoint point) {
	mouse(widget, QEvent::MouseButtonPress, point, widget.mapToGlobal(point));
	mouse(widget, QEvent::MouseButtonRelease, point, widget.mapToGlobal(point));
	settle();
}

static void key(QWidget &widget, int code) {
	QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
	QApplication::sendEvent(&widget, &event);
	settle();
}

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pl_setting_set("sound-theme", "none");
	Collar collar(false);
	collar.show();
	settle();
	check(collar.size() == QSize(219, 24), "reference geometry: expanded strip is 219 x 24 logical pixels");
	const QImage image = collar.grab().toImage().scaled(collar.size(),
		Qt::IgnoreAspectRatio, Qt::FastTransformation);
	check(image.pixelColor(8, 0) == QColor(0, 0, 0) && image.pixelColor(8, 1) == QColor(255, 255, 255) &&
		image.pixelColor(0, 8) == QColor(0, 0, 0) && image.pixelColor(0, 0).alpha() == 0,
		"reference geometry: angled grip outline, highlight and transparent corner");
	check(image.pixelColor(14, 12) == QColor(192, 192, 192), "reference neutral-gray strip face");
	check(image.pixelColor(31, 0) == QColor(0, 0, 0) && image.pixelColor(32, 1) == QColor(255, 255, 255) &&
		image.pixelColor(61, 12) == QColor(128, 128, 128) &&
		image.pixelColor(31, 23) == QColor(0, 0, 0), "raised module bevel and square black frame");
	check(image.pixelColor(55, 8) == QColor(0, 0, 0) &&
		image.pixelColor(58, 12) == QColor(0, 0, 0), "module has its own solid menu triangle");
	check(image.pixelColor(20, 12) == QColor(128, 128, 128) &&
		image.pixelColor(24, 12) == QColor(255, 255, 255), "unavailable scroll arrow is hollow, not a solid triangle");
	check(collar.moduleAt(QPoint(31, 12)) == Collar::Volume &&
		collar.moduleAt(QPoint(62, 12)) == Collar::Volume &&
		collar.moduleAt(QPoint(63, 12)) == Collar::Network &&
		collar.moduleAt(QPoint(0, 12)) == -1 &&
		collar.moduleAt(QPoint(35, 24)) == -1, "module hitboxes exactly match painted cells");

	auto *layer = LayerShellQt::Window::get(collar.windowHandle());
	check(layer && layer->scope() == "zacos9-collar" && layer->layer() == LayerShellQt::Window::LayerTop &&
		layer->exclusionZone() == 0 &&
		layer->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityOnDemand,
		"nonexclusive top layer; keyboard focus only on demand");
	click(collar, QPoint(212, 12));
	check(collar.collapsed() && collar.size() == QSize(17, 24), "end-tab click folds to the 17 x 24 angled grip");
	check(collar.moduleAt(QPoint(22, 12)) == -1, "collapsed strip has no module hitboxes");
	{
		Collar saved(false);
		check(saved.collapsed(), "collapsed state persists across instances");
	}
	click(collar, QPoint(6, 12));
	check(!collar.collapsed() && collar.width() == 219, "tab click unfolds all five modules");
	mouse(collar, QEvent::MouseButtonPress, QPoint(35, 12), QPoint(35, 12));
	mouse(collar, QEvent::MouseButtonRelease, QPoint(5, 12), QPoint(5, 12));
	check(!collar.collapsed(), "dragging out of a module doesn't activate the collapse cap");
	if (auto *popup = QApplication::activePopupWidget()) {
		key(*popup, Qt::Key_Escape);
	}
	collar.setVisibleModules(2);
	check(collar.width() == 123 && collar.moduleRect(Collar::Bluetooth).isEmpty(), "resize shows two modules without scaling icons");
	click(collar, QPoint(101, 12));
	check(!collar.moduleRect(Collar::Bluetooth).isEmpty() && collar.moduleRect(Collar::Volume).isEmpty(),
		"right arrow scrolls hidden modules into view");
	click(collar, QPoint(23, 12));
	check(!collar.moduleRect(Collar::Volume).isEmpty(), "left arrow scrolls back");
	collar.setVisibleModules(5);
	check(!collar.mask().contains(QPoint(0, 0)) && collar.mask().contains(QPoint(0, 8)),
		"angled grip leaves its transparent corner outside the input region");
	mouse(collar, QEvent::MouseButtonPress, QPoint(212, 12), QPoint(212, 100));
	mouse(collar, QEvent::MouseMove, QPoint(212, 12), QPoint(148, 100));
	mouse(collar, QEvent::MouseButtonRelease, QPoint(148, 12), QPoint(148, 100));
	check(!collar.collapsed() && collar.visibleModules() == 3 && collar.width() == 155,
		"dragging the square end tab resizes in measured 32-pixel module steps");
	collar.setVisibleModules(5);
	const int originalBottom = layer->margins().bottom();
	mouse(collar, QEvent::MouseButtonPress, QPoint(8, 12), QPoint(20, 200), Qt::AltModifier);
	mouse(collar, QEvent::MouseMove, QPoint(8, 12), QPoint(20, 160), Qt::AltModifier);
	mouse(collar, QEvent::MouseButtonRelease, QPoint(8, 12), QPoint(20, 160), Qt::AltModifier);
	check(layer->margins().bottom() == originalBottom + 40 && !collar.collapsed(), "Alt-drag moves vertically without folding");

	for (int module = 0; module < Collar::ModuleCount; ++module) {
		click(collar, collar.moduleRect(module).center());
		QWidget *popup = QApplication::activePopupWidget();
		check(popup && popup->isVisible() && popup->windowFlags().testFlag(Qt::Popup),
			QString("module %1 opens a real popup").arg(module));
		if (popup) {
			const QImage down = collar.grab().toImage().scaled(collar.size(),
				Qt::IgnoreAspectRatio, Qt::FastTransformation);
			const int left = collar.moduleRect(module).left();
			check(down.pixelColor(left + 1, 1) == QColor(128, 128, 128),
				"open module reverses the raised bevel");
			key(*popup, Qt::Key_Escape);
		}
		check(!QApplication::activePopupWidget(), "Escape dismisses without activating a command");
		const QImage up = collar.grab().toImage().scaled(collar.size(),
			Qt::IgnoreAspectRatio, Qt::FastTransformation);
		check(up.pixelColor(collar.moduleRect(module).left() + 1, 1) == QColor(255, 255, 255),
			"closing a menu restores the raised bevel");
	}

	bool activated = false;
	panelMenuAbove(&collar, { PopupItem("Unavailable", false) }, 0, collar.moduleRect(0),
		[&](int) { activated = true; });
	settle();
	if (auto *popup = QApplication::activePopupWidget()) {
		key(*popup, Qt::Key_Return);
		check(!activated && popup->isVisible(), "Return cannot activate a disabled initial menu selection");
		key(*popup, Qt::Key_Escape);
	} else {
		check(false, "disabled-item popup opens");
	}
	int selected = -1;
	bool closed = false;
	const std::vector<PopupItem> commands = { PopupItem("First"), PopupItem("Second") };
	panelMenuAbove(&collar, commands, -1, collar.moduleRect(0),
		[&](int index) { selected = index; }, [&] { closed = true; });
	settle(app.doubleClickInterval() + 20);
	if (auto *popup = QApplication::activePopupWidget()) {
		mouse(*popup, QEvent::MouseMove, QPoint(20, 24), popup->mapToGlobal(QPoint(20, 24)));
		mouse(*popup, QEvent::MouseButtonRelease, QPoint(20, 24), popup->mapToGlobal(QPoint(20, 24)));
		settle();
	}
	check(selected == 1 && closed, "press-drag-release selects the tracked menu item and resets the module");
	selected = -1;
	panelMenuAbove(&collar, commands, -1, collar.moduleRect(0),
		[&](int index) { selected = index; });
	settle();
	if (auto *popup = QApplication::activePopupWidget()) {
		mouse(*popup, QEvent::MouseButtonRelease, QPoint(20, 24), popup->mapToGlobal(QPoint(20, 24)));
		check(selected == -1 && popup->isVisible(), "quick opening release leaves a sticky menu");
		click(*popup, QPoint(20, 24));
	}
	check(selected == 1, "a subsequent click chooses from the sticky menu");
	key(collar, Qt::Key_Escape);
	check(collar.collapsed(), "keyboard Escape folds the strip");
	key(collar, Qt::Key_Space);
	check(!collar.collapsed(), "keyboard Space unfolds the strip");
	collar.setVisibleModules(1);
	key(collar, Qt::Key_Return);
	check(!collar.moduleRect(Collar::Power).isEmpty() && QApplication::activePopupWidget(),
		"keyboard activation scrolls a previously focused hidden module into view");
	if (auto *popup = QApplication::activePopupWidget()) {
		key(*popup, Qt::Key_Escape);
	}
	collar.setVisibleModules(5);
	if (app.arguments().contains("--save-image") && app.arguments().size() > 2) {
		check(collar.grab().save(app.arguments().last()), "save strip preview");
	}
	return failures ? 1 : 0;
}
