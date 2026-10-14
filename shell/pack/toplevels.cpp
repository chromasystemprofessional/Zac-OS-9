#include "toplevels.h"

#include <QGuiApplication>
#include <QHash>
#include <QtGui/qguiapplication_platform.h>
#include <algorithm>
#include <cstring>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace {

struct Window {
	zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
	WindowInfo info, pending;
};

wl_display *display = nullptr;
zwlr_foreign_toplevel_manager_v1 *manager = nullptr;
std::vector<Window *> all;
std::function<void()> onChange;

void title(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *t) {
	static_cast<Window *>(data)->pending.title = QString::fromUtf8(t);
}
void appId(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *id) {
	static_cast<Window *>(data)->pending.appId = QString::fromUtf8(id);
}
void outputEnter(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void outputLeave(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void state(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *states) {
	auto *w = static_cast<Window *>(data);
	w->pending.active = w->pending.minimized = false;
	const uint32_t *s;
	for (s = static_cast<const uint32_t *>(states->data);
			reinterpret_cast<const char *>(s) < static_cast<const char *>(states->data) + states->size; s++) {
		w->pending.active |= *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
		w->pending.minimized |= *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
}
void done(void *data, zwlr_foreign_toplevel_handle_v1 *) {
	auto *w = static_cast<Window *>(data);
	w->info = w->pending;
	if (onChange) {
		onChange();
	}
}
void closed(void *data, zwlr_foreign_toplevel_handle_v1 *h) {
	auto *w = static_cast<Window *>(data);
	all.erase(std::remove(all.begin(), all.end(), w), all.end());
	zwlr_foreign_toplevel_handle_v1_destroy(h);
	delete w;
	if (onChange) {
		onChange();
	}
}
void parent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {}

const zwlr_foreign_toplevel_handle_v1_listener handleListener = {
	title, appId, outputEnter, outputLeave, state, done, closed, parent,
};

void toplevel(void *, zwlr_foreign_toplevel_manager_v1 *, zwlr_foreign_toplevel_handle_v1 *h) {
	auto *w = new Window;
	w->handle = h;
	all.push_back(w);
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &handleListener, w);
}
void finished(void *, zwlr_foreign_toplevel_manager_v1 *) {}
const zwlr_foreign_toplevel_manager_v1_listener managerListener = { toplevel, finished };

void global(void *, wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
	if (std::strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		manager = static_cast<zwlr_foreign_toplevel_manager_v1 *>(wl_registry_bind(reg, name,
			&zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3u)));
		zwlr_foreign_toplevel_manager_v1_add_listener(manager, &managerListener, nullptr);
	}
}
void globalRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener = { global, globalRemove };

} // namespace

void Toplevels::start(std::function<void()> changed) {
	onChange = std::move(changed);
	auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	if (!wayland || manager) {
		return;
	}
	display = wayland->display();
	/* Found on a queue of our own, then handed to Qt's dispatch. */
	wl_event_queue *queue = wl_display_create_queue(display);
	auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
	wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
	wl_registry *registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	wl_registry_add_listener(registry, &registryListener, nullptr);
	wl_display_roundtrip_queue(display, queue);
	/* Again: the windows already open, and all they say about themselves,
	 * before the queue goes. */
	wl_display_roundtrip_queue(display, queue);
	wl_registry_destroy(registry);
	if (manager) {
		wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(manager), nullptr);
		for (Window *w : all) {
			wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(w->handle), nullptr);
		}
	}
	wl_event_queue_destroy(queue);
	wl_display_flush(display);
}

std::vector<WindowInfo> Toplevels::windows() {
	std::vector<WindowInfo> out;
	for (const Window *w : all) {
		if (!w->info.appId.isEmpty()) {
			out.push_back(w->info);
		}
	}
	return out;
}

void Toplevels::hide(const QString &appId) {
	for (Window *w : all) {
		if (w->info.appId == appId) {
			zwlr_foreign_toplevel_handle_v1_set_minimized(w->handle);
		}
	}
	wl_display_flush(display);
}
