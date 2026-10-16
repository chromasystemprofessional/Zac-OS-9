#include "platinumshell.h"

#include <QGuiApplication>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QDebug>
#include <QWidget>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <wayland-client.h>

#include "platinum-shell-v1-client-protocol.h"

static wl_display *display;
static platinum_shell_v1 *shell;

struct PositionWatch {
	QPointer<QWidget> window;
	std::function<void(QPoint)> callback;
};
static QHash<wl_surface *, PositionWatch> &watches() {
	static QHash<wl_surface *, PositionWatch> w;
	return w;
}

static void handle_window_position(void *, platinum_shell_v1 *, wl_surface *surface,
		int32_t x, int32_t y) {
	auto it = watches().find(surface);
	if (it != watches().end() && it->window) {
		it->callback(QPoint(x, y));
	}
}

static uint32_t activeClient;
static QList<QPair<QPointer<QObject>, std::function<void()>>> &activeWatches() {
	static QList<QPair<QPointer<QObject>, std::function<void()>>> watches;
	return watches;
}
static void handle_active_client(void *, platinum_shell_v1 *, uint32_t pid) {
	activeClient = pid;
	const auto watches = activeWatches();
	for (const auto &watch : watches) {
		if (watch.first) {
			watch.second();
		}
	}
}
static void handle_active_gtk_menu(void *, platinum_shell_v1 *, const char *, const char *,
		const char *, const char *, const char *) {
}
static void handle_switch_app(void *, platinum_shell_v1 *, int32_t) {
}
/* Windows that show a menu on a ⌘-click of their title. */
static QHash<wl_surface *, std::function<void(bool)>> &titleMenus() {
	static QHash<wl_surface *, std::function<void(bool)>> menus;
	return menus;
}
static void handle_title_menu(void *, platinum_shell_v1 *, wl_surface *surface, uint32_t option) {
	auto it = titleMenus().find(surface);
	if (it != titleMenus().end()) {
		it.value()(option != 0);
	}
}
/* The keyboard layout in use ("us", "de(nodeadkeys)"), as zacos9-wm says. */
static QString keyboardLayout;
static QList<QPair<QPointer<QObject>, std::function<void()>>> &layoutWatches() {
	static QList<QPair<QPointer<QObject>, std::function<void()>>> watches;
	return watches;
}
static void handle_keyboard_layouts(void *, platinum_shell_v1 *, const char *layouts,
		uint32_t current) {
	const QStringList lines = QString::fromUtf8(layouts).split('\n', Qt::SkipEmptyParts);
	keyboardLayout = current < uint32_t(lines.size()) ? lines[current].section('\t', 0, 0)
		: QString();
	const auto watches = layoutWatches();
	for (const auto &watch : watches) {
		if (watch.first) {
			watch.second();
		}
	}
}
/* Windows with a proxy icon, and what dragging it does. */
static QHash<wl_surface *, std::function<void()>> &proxyDrags() {
	static QHash<wl_surface *, std::function<void()>> drags;
	return drags;
}
static void handle_proxy_drag(void *, platinum_shell_v1 *, wl_surface *surface) {
	auto it = proxyDrags().find(surface);
	if (it != proxyDrags().end() && it.value()) {
		/* Not inside Wayland's event dispatch: the drag runs its own loop. */
		std::function<void()> drag = it.value();
		QTimer::singleShot(0, drag);
	}
}
static void handle_sticky_keys(void *, platinum_shell_v1 *, uint32_t, uint32_t, uint32_t) {
}
/* Pop-up windows: where the user put each one's tab, and being dragged
 * up into an ordinary window. */
struct PopupWatch {
	std::function<void(int)> tabMoved;
	std::function<void()> detached;
};
static QHash<wl_surface *, PopupWatch> &popupWatches() {
	static QHash<wl_surface *, PopupWatch> watches;
	return watches;
}
static void handle_popup_tab(void *, platinum_shell_v1 *, wl_surface *surface, int32_t x) {
	auto it = popupWatches().find(surface);
	if (it != popupWatches().end() && it->tabMoved) {
		it->tabMoved(x);
	}
}
static void handle_popup_detach(void *, platinum_shell_v1 *, wl_surface *surface) {
	auto it = popupWatches().find(surface);
	if (it != popupWatches().end() && it->detached) {
		it->detached();
	}
}
static const platinum_shell_v1_listener shell_listener = {
	handle_window_position, handle_active_client, handle_active_gtk_menu, handle_switch_app,
	handle_title_menu, handle_keyboard_layouts, handle_proxy_drag, handle_sticky_keys,
	handle_popup_tab, handle_popup_detach
};

static void global(void *, wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
	if (std::strcmp(iface, platinum_shell_v1_interface.name) == 0) {
		shell = static_cast<platinum_shell_v1 *>(
			wl_registry_bind(reg, name, &platinum_shell_v1_interface,
				std::min(version, 14u)));
		platinum_shell_v1_add_listener(shell, &shell_listener, nullptr);
	}
}

static void global_remove(void *, wl_registry *, uint32_t) {
}

static const wl_registry_listener registry_listener = { global, global_remove };

void platinumShellInit() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	if (!wayland) {
		return;
	}
	display = wayland->display();
	/* Find the global on our own queue, so we never dispatch Qt's events
	 * behind its back... */
	wl_event_queue *queue = wl_display_create_queue(display);
	auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
	wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
	wl_registry *registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	wl_registry_add_listener(registry, &registry_listener, nullptr);
	wl_display_roundtrip_queue(display, queue);
	wl_registry_destroy(registry);
	if (shell) {
		/* ...then let Qt's main-thread dispatch deliver its events. */
		wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(shell), nullptr);
	}
	wl_event_queue_destroy(queue);
}

static void flushLaunch() {
	if (wl_display_flush(display) < 0 && errno != EAGAIN) {
		qWarning() << "Could not send application launch feedback:" << strerror(errno);
	}
}

uint32_t platinumBeginLaunch(const QString &appId) {
	platinumShellInit();
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 3) {
		return 0; /* Other compositors and older ZacOS releases remain supported. */
	}
	static uint32_t nextCookie = 0;
	if (++nextCookie == 0) {
		++nextCookie;
	}
	platinum_shell_v1_begin_launch(shell, nextCookie, 0, appId.toUtf8().constData());
	flushLaunch();
	return nextCookie;
}

void platinumUpdateLaunch(uint32_t cookie, uint32_t pid) {
	if (cookie) {
		platinum_shell_v1_update_launch(shell, cookie, pid);
		flushLaunch();
	}
}

void platinumCancelLaunch(uint32_t cookie) {
	if (cookie) {
		platinum_shell_v1_cancel_launch(shell, cookie);
		flushLaunch();
	}
}

bool platinumStartApplication(const QString &program, const QStringList &args,
		const QString &directory) {
	const uint32_t cookie = platinumBeginLaunch();
	qint64 pid = 0;
	if (!QProcess::startDetached(program, args, directory, &pid)) {
		platinumCancelLaunch(cookie);
		qWarning() << "Could not launch application:" << program;
		return false;
	}
	platinumUpdateLaunch(cookie, static_cast<uint32_t>(pid));
	return true;
}

static wl_surface *surfaceOf(QWidget *window) {
	if (!window->windowHandle()) {
		return nullptr;
	}
	auto *native = QGuiApplication::platformNativeInterface();
	return static_cast<wl_surface *>(
		native->nativeResourceForWindow("surface", window->windowHandle()));
}

void platinumSetStandardSize(QWidget *window, QSize size) {
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 9) {
		return;
	}
	wl_surface *surface = surfaceOf(window);
	if (surface) {
		platinum_shell_v1_set_standard_size(shell, surface,
			static_cast<uint32_t>(std::max(0, size.width())),
			static_cast<uint32_t>(std::max(0, size.height())));
		wl_display_flush(display);
	}
}

uint32_t platinumActiveClient() {
	return activeClient;
}

void platinumOnActiveClient(QObject *owner, std::function<void()> changed) {
	activeWatches().append({ owner, std::move(changed) });
}

QString platinumKeyboardLayout() {
	return keyboardLayout;
}

void platinumOnKeyboardLayout(QObject *owner, std::function<void()> changed) {
	layoutWatches().append({ owner, std::move(changed) });
}

void platinumSetProxyIcon(QWidget *window, int iconKind, std::function<void()> dragged) {
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 12) {
		return;
	}
	wl_surface *surface = surfaceOf(window);
	if (!surface) {
		return;
	}
	proxyDrags().insert(surface, std::move(dragged));
	QObject::connect(window, &QObject::destroyed, [surface] { proxyDrags().remove(surface); });
	platinum_shell_v1_set_proxy_icon(shell, surface, static_cast<uint32_t>(iconKind + 1));
	wl_display_flush(display);
}

void platinumActivateApp(const QString &appId) {
	platinumShellInit();
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 8) {
		return;
	}
	platinum_shell_v1_activate_app(shell, appId.toUtf8().constData());
	wl_display_flush(display);
}

bool platinumCloseFrontAppWindow(const QString &appId) {
	platinumShellInit();
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) <
			PLATINUM_SHELL_V1_CLOSE_APP_WINDOW_SINCE_VERSION) {
		return false;
	}
	platinum_shell_v1_close_app_window(shell, appId.toUtf8().constData());
	wl_display_flush(display);
	return true;
}

void platinumOnTitleMenu(QWidget *window, std::function<void(bool)> callback) {
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 10) {
		return;
	}
	wl_surface *surface = surfaceOf(window);
	if (!surface) {
		return;
	}
	titleMenus().insert(surface, std::move(callback));
	QObject::connect(window, &QObject::destroyed, [surface] { titleMenus().remove(surface); });
	platinum_shell_v1_enable_title_menu(shell, surface);
	wl_display_flush(display);
}

void platinumSetFrameStyle(QWidget *window, FrameStyle style) {
	wl_surface *surface = shell ? surfaceOf(window) : nullptr;
	if (surface) {
		platinum_shell_v1_set_window_style(shell, surface, static_cast<uint32_t>(style));
		wl_display_flush(display);
	}
}

bool platinumHasPopupWindows() {
	return shell && wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) >= 14;
}

void platinumSetPopupTab(QWidget *window, int x) {
	wl_surface *surface = platinumHasPopupWindows() ? surfaceOf(window) : nullptr;
	if (surface) {
		platinum_shell_v1_set_popup_tab(shell, surface, x);
		wl_display_flush(display);
	}
}

void platinumOnPopup(QWidget *window, std::function<void(int)> tabMoved,
		std::function<void()> detached) {
	wl_surface *surface = platinumHasPopupWindows() ? surfaceOf(window) : nullptr;
	if (!surface) {
		return;
	}
	popupWatches().insert(surface, { std::move(tabMoved), std::move(detached) });
	QObject::connect(window, &QObject::destroyed, [surface] { popupWatches().remove(surface); });
}

void platinumSetWindowPosition(QWidget *window, QPoint pos) {
	wl_surface *surface = shell ? surfaceOf(window) : nullptr;
	if (surface) {
		platinum_shell_v1_set_window_position(shell, surface, pos.x(), pos.y());
		wl_display_flush(display);
	}
}

void platinumOnWindowPosition(QWidget *window, std::function<void(QPoint)> callback) {
	if (wl_surface *surface = shell ? surfaceOf(window) : nullptr) {
		watches().insert(surface, { window, std::move(callback) });
		/* Surfaces are reused after a window goes; forget it then. */
		QObject::connect(window, &QObject::destroyed, [surface] { watches().remove(surface); });
	}
}

bool platinumForceQuit(const QString &appId) {
	if (!shell || wl_proxy_get_version(reinterpret_cast<wl_proxy *>(shell)) < 7) {
		return false;
	}
	platinum_shell_v1_force_quit(shell, appId.toUtf8().constData());
	wl_display_flush(display);
	return true;
}
