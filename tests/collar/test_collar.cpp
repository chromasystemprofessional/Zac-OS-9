#include "collar.h"
#include "panelkit.h"
#include "settings.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
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

static QImage shot(QWidget &widget) {
	return widget.grab().toImage().scaled(widget.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

static bool is(const QImage &image, int x, int y, QRgb rgb) {
	return image.pixelColor(x, y) == QColor(rgb);
}

static constexpr QRgb Black = 0x000000, White = 0xFFFFFF, Face = 0xC0C0C0, Shade = 0xA0A0A0, Shadow = 0x808080;

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	qputenv("XDG_RUNTIME_DIR", config.path().toUtf8());
	qputenv("XDG_DATA_HOME", (config.path() + "/data").toUtf8());
	qputenv("WAYLAND_DISPLAY", "test-collar");
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	{
		// Two displays, so the resolution and brightness modules repeat per device.
		QFile outputs(config.path() + "/zacos9-outputs-test-collar");
		if (!outputs.open(QIODevice::WriteOnly)) {
			return 1;
		}
		// TEST-1 lists more modes than fit on the screen in one menu.
		QByteArray data = "mirror 0\noutput TEST-1 1920x1080 scale 1 nested 0 x 0 y 0 main 1\n";
		for (int i = 0; i < 60; ++i) {
			data += QByteArray("mode ") + QByteArray::number(640 + 32 * i) + "x" + QByteArray::number(480 + 10 * i) + "@60000\n";
		}
		data += "mode 1920x1080@60000\n"
			"output TEST-2 1280x1024 scale 1 nested 0 x 1920 y 0 main 0\n"
			"mode 1280x1024@60000\nmode 1024x768@60000\n";
		outputs.write(data);
	}
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pl_setting_set("sound-theme", "none");
	Collar collar(false);
	collar.show();
	settle();
	const int count = collar.modules().size();
	check(count == 9 && collar.modules().at(4).kind == Collar::Resolution &&
		collar.modules().at(4).device == "TEST-1" && collar.modules().at(7).kind == Collar::Brightness &&
		collar.modules().at(7).device == "TEST-2" && collar.modules().last().kind == Collar::Power,
		"one resolution and one brightness module for each connected display");
	check(collar.size() == QSize(Collar::openWidth(count), 24) && collar.width() == 339,
		"open width: close box, arrow, 31 px per module, arrow and grip");
	QImage image = shot(collar);
	check(is(image, 0, 0, Black) && is(image, 0, 12, Black) && is(image, 1, 1, Face) && is(image, 2, 2, White) &&
		is(image, 13, 2, Shadow) && is(image, 12, 3, Shade) && is(image, 3, 22, Shadow) && is(image, 14, 12, Black),
		"close box sits at the left end, as in the reference");
	check(is(image, 15, 1, Shadow) && is(image, 27, 1, Face) && is(image, 15, 12, Shadow) &&
		is(image, 27, 12, White) && is(image, 15, 22, Face) && is(image, 16, 22, White) &&
		is(image, 21, 7, 0x555555) && is(image, 21, 10, 0xDDDDDD) && is(image, 28, 12, Black),
		"recessed left scroll button with a disabled hollow arrow");
	check(is(image, 29, 1, Face) && is(image, 30, 2, White) && is(image, 57, 2, Face) && is(image, 58, 2, Shadow) &&
		is(image, 30, 12, White) && is(image, 57, 12, Shade) && is(image, 58, 12, Shadow) &&
		is(image, 30, 21, Face) && is(image, 31, 21, Shade) && is(image, 30, 22, Shadow) &&
		is(image, 59, 12, Black) && is(image, 29, 0, Black) && is(image, 29, 23, Black),
		"raised 30 px module cell followed by a black divider");
	check(is(image, 51, 8, Black) && is(image, 51, 15, Black) && is(image, 54, 11, Black) && is(image, 54, 10, Face),
		"module menu triangle at its measured place");
	const int grip = collar.width() - Collar::Grip;
	check(is(image, grip - 13, 12, Shadow) && is(image, grip - 1, 12, White) && is(image, grip, 12, Black) &&
		is(image, grip + 2, 12, 0xCCCCCC) && is(image, grip + 3, 12, White) && is(image, grip + 17, 12, Black) &&
		image.pixelColor(grip + 17, 0).alpha() == 0 && is(image, grip + 9, 0, Black),
		"right scroll button then the angled grip at the right end");
	check(collar.moduleAt(QPoint(29, 12)) == 0 && collar.moduleAt(QPoint(58, 12)) == 0 &&
		collar.moduleAt(QPoint(59, 12)) == -1 && collar.moduleAt(QPoint(60, 12)) == 1 &&
		collar.moduleAt(QPoint(20, 12)) == -1, "module hitboxes exactly match painted cells");
	check(!collar.mask().contains(QPoint(collar.width() - 1, 0)) &&
		collar.mask().contains(QPoint(collar.width() - 1, 10)), "grip's angled corner is outside the input region");

	auto *layer = LayerShellQt::Window::get(collar.windowHandle());
	check(layer && layer->scope() == "zacos9-collar" && layer->layer() == LayerShellQt::Window::LayerTop &&
		layer->exclusionZone() == 0 && layer->margins().bottom() == 0 &&
		layer->anchors() == LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft |
			LayerShellQt::Window::AnchorBottom) &&
		layer->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityOnDemand,
		"flush bottom-left, nonexclusive top layer; keyboard focus only on demand");

	click(collar, QPoint(7, 12));
	check(collar.collapsed() && collar.size() == QSize(Collar::Closed, 24), "close box folds to the 18 px grip");
	image = shot(collar);
	check(is(image, 0, 0, Black) && is(image, 0, 12, Black) && is(image, 1, 12, Black) && is(image, 2, 12, 0xCCCCCC) &&
		is(image, 3, 12, White) && is(image, 17, 12, Black) && image.pixelColor(17, 0).alpha() == 0 &&
		!collar.mask().contains(QPoint(17, 0)) && collar.mask().contains(QPoint(17, 12)),
		"closed: only the grip, matching the reference");
	check(collar.moduleAt(QPoint(30, 12)) == -1, "closed strip has no module hitboxes");
	{
		Collar saved(false);
		check(saved.collapsed(), "closed state persists across instances");
	}
	click(collar, QPoint(9, 12));
	check(!collar.collapsed() && collar.width() == 339, "grip click opens every module again");
	click(collar, QPoint(grip + 9, 12));
	check(collar.collapsed(), "clicking the open strip's grip also folds it");
	click(collar, QPoint(9, 12));
	mouse(collar, QEvent::MouseButtonPress, QPoint(35, 12), QPoint(35, 12));
	if (auto *popup = QApplication::activePopupWidget()) {
		key(*popup, Qt::Key_Escape);
	}
	mouse(collar, QEvent::MouseButtonRelease, QPoint(5, 12), QPoint(5, 12));
	check(!collar.collapsed(), "dragging out of a module doesn't activate the close box");

	collar.setVisibleModules(2);
	check(collar.width() == Collar::openWidth(2) && collar.moduleRect(2).isEmpty(),
		"resize shows two modules without scaling icons");
	image = shot(collar);
	check(is(image, Collar::FirstModule + 62 + 5, 7, Black), "right arrow is solid when more modules are hidden");
	click(collar, QPoint(Collar::FirstModule + 62 + 5, 12));
	check(!collar.moduleRect(2).isEmpty() && collar.moduleRect(0).isEmpty(), "right arrow scrolls hidden modules into view");
	click(collar, QPoint(21, 12));
	check(!collar.moduleRect(0).isEmpty(), "left arrow scrolls back");
	collar.setVisibleModules(count);
	const int end = collar.width() - 9;
	mouse(collar, QEvent::MouseButtonPress, QPoint(end, 12), QPoint(end, 100));
	mouse(collar, QEvent::MouseMove, QPoint(end, 12), QPoint(end - 62, 100));
	mouse(collar, QEvent::MouseButtonRelease, QPoint(end - 62, 12), QPoint(end - 62, 100));
	check(!collar.collapsed() && collar.visibleModules() == count - 2 && collar.width() == Collar::openWidth(count - 2),
		"dragging the grip resizes in 31 px module steps");
	collar.setVisibleModules(count);

	const int screenHeight = collar.windowHandle()->screen()->size().height();
	mouse(collar, QEvent::MouseButtonPress, QPoint(40, 12), QPoint(40, 500), Qt::ControlModifier);
	check(!QApplication::activePopupWidget(), "Command-press on a module moves rather than opening it");
	mouse(collar, QEvent::MouseMove, QPoint(40, 12), QPoint(40, 460), Qt::ControlModifier);
	mouse(collar, QEvent::MouseButtonRelease, QPoint(40, 12), QPoint(40, 460), Qt::ControlModifier);
	check(layer->margins().bottom() == 40 && collar.bottomOffset() == 40 && !collar.collapsed(),
		"Command-drag moves The Collar up");
	mouse(collar, QEvent::MouseButtonPress, QPoint(grip + 9, 12), QPoint(40, 500), Qt::ControlModifier);
	mouse(collar, QEvent::MouseMove, QPoint(grip + 9, 12), QPoint(40, -100000), Qt::ControlModifier);
	mouse(collar, QEvent::MouseButtonRelease, QPoint(grip + 9, 12), QPoint(40, -100000), Qt::ControlModifier);
	check(collar.bottomOffset() == screenHeight - 24 - Collar::MenuBar && !collar.collapsed() &&
		collar.width() == 339, "Command-dragging the grip stops under the menu bar without folding or resizing");
	{
		Collar saved(false);
		check(saved.bottomOffset() == screenHeight - 24 - Collar::MenuBar, "vertical position persists");
	}
	mouse(collar, QEvent::MouseButtonPress, QPoint(7, 12), QPoint(40, 100), Qt::ControlModifier);
	mouse(collar, QEvent::MouseMove, QPoint(7, 12), QPoint(40, 300), Qt::ControlModifier);
	key(collar, Qt::Key_Escape);
	mouse(collar, QEvent::MouseButtonRelease, QPoint(7, 12), QPoint(40, 300), Qt::ControlModifier);
	check(collar.bottomOffset() == screenHeight - 44 && !collar.collapsed(),
		"Escape cancels a move without folding");
	mouse(collar, QEvent::MouseButtonPress, QPoint(7, 12), QPoint(40, 100), Qt::ControlModifier);
	mouse(collar, QEvent::MouseMove, QPoint(7, 12), QPoint(40, 100000), Qt::ControlModifier);
	mouse(collar, QEvent::MouseButtonRelease, QPoint(7, 12), QPoint(40, 100000), Qt::ControlModifier);
	check(collar.bottomOffset() == 0 && !collar.collapsed(), "Command-drag down stops flush at the screen bottom");

	QStringList labels;
	for (int module = 0; module < count; ++module) {
		labels << collar.menuLabel(module);
		click(collar, collar.moduleRect(module).center());
		QWidget *popup = QApplication::activePopupWidget();
		check(popup && popup->isVisible() && popup->windowFlags().testFlag(Qt::Popup),
			QString("module %1 (%2) opens a real popup").arg(module).arg(labels.last()));
		if (popup) {
			check(is(shot(collar), collar.moduleRect(module).left(), 1, Shadow),
				"open module recesses its cell");
			check(popup->geometry().bottom() < collar.mapToGlobal(QPoint(0, 0)).y(),
				QString("at the screen bottom the menu opens upward (module %1: menu %2-%3, strip %4)")
					.arg(module).arg(popup->geometry().top()).arg(popup->geometry().bottom())
					.arg(collar.mapToGlobal(QPoint(0, 0)).y()));
			check(popup->height() <= screenHeight - 24 - Collar::MenuBar,
				QString("menu %1 fits between The Collar and the menu bar").arg(module));
			key(*popup, Qt::Key_Escape);
		}
		check(!QApplication::activePopupWidget(), "Escape dismisses without activating a command");
		check(is(shot(collar), collar.moduleRect(module).left() + 1, 2, White), "closing a menu raises the cell again");
	}
	check(labels.mid(0, 4) == QStringList({ "Sound", "Sound Set", "Network", "Bluetooth" }) &&
		labels[4] == "Display 1: TEST-1 (Main)" && labels[5] == labels[4] &&
		labels[6] == "Display 2: TEST-2" && labels[7] == labels[6] && labels[8] == "Power",
		"every menu is headed by its device, so duplicated icons are unambiguous");
	collar.setVisibleModules(count);

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
	check(!collar.moduleRect(count - 1).isEmpty() && QApplication::activePopupWidget(),
		"keyboard activation scrolls a previously focused hidden module into view");
	if (auto *popup = QApplication::activePopupWidget()) {
		key(*popup, Qt::Key_Escape);
	}
	collar.setVisibleModules(count);
	if (app.arguments().contains("--save-image") && app.arguments().size() > 2) {
		const QString path = app.arguments().last();
		check(collar.grab().save(path), "save open strip preview");
		collar.setCollapsed(true);
		check(collar.grab().save(QString(path).replace(".png", "-closed.png")), "save closed strip preview");
		collar.setCollapsed(false);
	}
	return failures ? 1 : 0;
}
