#include "platinumshell.h"

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <cstring>
#include <wayland-client.h>

#include "platinum-shell-v1-client-protocol.h"

static wl_display *display;
static platinum_shell_v1 *shell;

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
	/* Our own queue, so we never dispatch Qt's events behind its back. */
	wl_event_queue *queue = wl_display_create_queue(display);
	auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
	wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
	wl_registry *registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	wl_registry_add_listener(registry, &registry_listener, nullptr);
	wl_display_roundtrip_queue(display, queue);
	wl_registry_destroy(registry);
}

void platinumSetFrameStyle(QWidget *window, FrameStyle style) {
	if (!shell || !window->windowHandle()) {
		return;
	}
	auto *native = QGuiApplication::platformNativeInterface();
	auto *surface = static_cast<wl_surface *>(
		native->nativeResourceForWindow("surface", window->windowHandle()));
	if (surface) {
		platinum_shell_v1_set_window_style(shell, surface, static_cast<uint32_t>(style));
		wl_display_flush(display);
	}
}
