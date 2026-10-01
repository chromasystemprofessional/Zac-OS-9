#include "platinumshell.h"

#include <QGuiApplication>
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <cstring>
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

static const platinum_shell_v1_listener shell_listener = { handle_window_position };

static void global(void *, wl_registry *reg, uint32_t name, const char *iface, uint32_t) {
	if (std::strcmp(iface, platinum_shell_v1_interface.name) == 0) {
		shell = static_cast<platinum_shell_v1 *>(
			wl_registry_bind(reg, name, &platinum_shell_v1_interface, 1));
	}
}

static void global_remove(void *, wl_registry *, uint32_t) {
}

static const wl_registry_listener registry_listener = { global, global_remove };

void platinumShellInit() {
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
	wl_event_queue_destroy(queue);
	if (shell) {
		/* ...then let Qt's main-thread dispatch deliver its events. */
		wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(shell), nullptr);
		platinum_shell_v1_add_listener(shell, &shell_listener, nullptr);
	}
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
