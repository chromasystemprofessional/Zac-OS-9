#include "secondarydesktop.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QFile>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <map>
#include <memory>

#include "patterns.h"
#include "custompatterns.h"
#include "pixels.h"

namespace {

QString settingsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9";
}

/* zacos9-wm names the main display in the screens file it publishes. */
QString outputsFile() {
	return qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
		qEnvironmentVariable("WAYLAND_DISPLAY");
}

QString mainDisplayName() {
	QFile f(outputsFile());
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : f.readAll().split('\n')) {
			const QList<QByteArray> w = line.split(' ');
			if (w.value(0) == "output" && w.value(12) == "1") {
				return QString::fromUtf8(w.value(1));
			}
		}
	}
	return QString();
}

std::map<QScreen *, std::unique_ptr<SecondaryDesktop>> &desktops() {
	static std::map<QScreen *, std::unique_ptr<SecondaryDesktop>> map;
	return map;
}

} // namespace

SecondaryDesktop::SecondaryDesktop(QScreen *screen) : m_screen(screen) {
	watchDesktopPatterns(this, [this] { loadPattern(); update(); });
	watchDesktopWallpaper(this, [this] { update(); });
	setAttribute(Qt::WA_OpaquePaintEvent);
	setFocusPolicy(Qt::NoFocus);
	loadPattern();
	winId();
	windowHandle()->setScreen(screen);
	if (auto *lw = LayerShellQt::Window::get(windowHandle())) {
		lw->setLayer(LayerShellQt::Window::LayerBottom);
		lw->setAnchors(LayerShellQt::Window::Anchors(
			LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
			LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
		lw->setExclusiveZone(-1);
		lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
		/* Not "zacos9-...": the compositor keeps ZacOS 9's own shell on the main
		 * display, and this one stays where it was put. */
		lw->setScope("desktop-extra");
	}
	resize(screen->size());
}

void SecondaryDesktop::loadPattern() {
	QSettings settings(settingsDir() + "/desktop.conf", QSettings::IniFormat);
	const QByteArray id = settings.value("pattern").toString().toUtf8();
	m_pattern = desktopPatternFind(QString::fromUtf8(id));
}

void SecondaryDesktop::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	desktopBackgroundFill(&px.c, width(), height());
	QPainter p(this);
	px.blit(p);
}

void SecondaryDesktop::keepInStep() {
	static bool started = false;
	if (!started) {
		started = true;
		/* A screen comes or goes, or the screens file changes (the main display
		 * was moved, a resolution changed) or the pattern was chosen again. */
		auto *timer = new QTimer;
		timer->setSingleShot(true);
		timer->setInterval(300);
		QObject::connect(timer, &QTimer::timeout, [] { keepInStep(); });
		auto again = [timer] { timer->start(); };
		QObject::connect(qApp, &QGuiApplication::screenAdded, again);
		QObject::connect(qApp, &QGuiApplication::screenRemoved, again);
		auto *watcher = new QFileSystemWatcher;
		watcher->addPath(qEnvironmentVariable("XDG_RUNTIME_DIR"));
		watcher->addPath(settingsDir());
		QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, again);
	}
	const QString main = mainDisplayName();
	auto &map = desktops();
	/* Gone screens, and the one that is main now. */
	for (auto it = map.begin(); it != map.end();) {
		const QList<QScreen *> screens = QGuiApplication::screens();
		if (!screens.contains(it->first) || it->first->name() == main) {
			it = map.erase(it);
		} else {
			++it;
		}
	}
	if (main.isEmpty() || QGuiApplication::screens().size() < 2) {
		return;
	}
	for (QScreen *screen : QGuiApplication::screens()) {
		if (screen->name() == main) {
			continue;
		}
		auto it = map.find(screen);
		if (it == map.end()) {
			auto d = std::make_unique<SecondaryDesktop>(screen);
			d->show();
			map[screen] = std::move(d);
		} else {
			it->second->loadPattern();
			it->second->resize(screen->size());
			it->second->update();
		}
	}
}
