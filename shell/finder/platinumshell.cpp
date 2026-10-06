#include "platinumshell.h"

#include <QGuiApplication>
#include <QHash>
#include <QPointer>
#include <QProcess>
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

static void handle_active_client(void *, platinum_shell_v1 *, uint32_t) {
}
static const platinum_shell_v1_listener shell_listener = {
	handle_window_position, handle_active_client, nullptr
};

static void global(void *, wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
	if (std::strcmp(iface, platinum_shell_v1_interface.name) == 0) {
		shell = static_cast<platinum_shell_v1 *>(
			wl_registry_bind(reg, name, &platinum_shell_v1_interface,
				std::min(version, 3u)));
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

void platinumSetFrameStyle(QWidget *window, FrameStyle style) {
	wl_surface *surface = shell ? surfaceOf(window) : nullptr;
	if (surface) {
		platinum_shell_v1_set_window_style(shell, surface, static_cast<uint32_t>(style));
		wl_display_flush(display);
	}
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
