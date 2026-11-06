/* A program with a text field: an xdg toplevel using text-input-v3.
 * Logs what the input method sends it, one line per event, so the
 * test can grep its output. */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;

static struct wl_buffer *tiny_buffer(int w, int h) {
	int stride = w * 4, size = stride * h;
	int fd = memfd_create("app-test", MFD_CLOEXEC);
	ftruncate(fd, size);
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memset(data, 0x40, size);
	munmap(data, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
		WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buf;
}
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;
static struct zwp_text_input_manager_v3 *ti_manager;
static struct zwp_text_input_v3 *text_input;

static void log_line(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

/* xdg */
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) {
	xdg_wm_base_pong(w, serial);
}
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };
static struct wl_surface *app_surface;
static int configured_once;
static void surf_config(void *d, struct xdg_surface *s, uint32_t serial) {
	xdg_surface_ack_configure(s, serial);
	if (!configured_once) {
		configured_once = 1;
		wl_surface_attach(app_surface, tiny_buffer(100, 60), 0, 0);
		wl_surface_damage(app_surface, 0, 0, 100, 60);
		wl_surface_commit(app_surface);
	}
}
static const struct xdg_surface_listener surf_listener = { .configure = surf_config };
static void top_config(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
		struct wl_array *states) {}
static void top_close(void *d, struct xdg_toplevel *t) {}
static const struct xdg_toplevel_listener top_listener = {
	.configure = top_config, .close = top_close,
};

/* keyboard: focus is what makes the text input enter */
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys) {
	log_line("APP keyboard-enter");
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface) {
	log_line("APP keyboard-leave");
}
static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t size) { close(fd); }
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state) {
	log_line("APP key keycode=%u state=%u", key, state);
}
static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
		uint32_t lat, uint32_t lock, uint32_t group) {}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
	.key = kb_key, .modifiers = kb_mods, .repeat_info = kb_repeat,
};

/* text input */
static void ti_enter(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *surface) {
	log_line("APP text-input enter");
	zwp_text_input_v3_enable(ti);
	zwp_text_input_v3_set_surrounding_text(ti, "hello ", 6, 6);
	zwp_text_input_v3_set_cursor_rectangle(ti, 12, 20, 2, 14);
	zwp_text_input_v3_commit(ti);
}
static void ti_leave(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *surface) {
	log_line("APP text-input leave");
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *ti, const char *text,
		int32_t begin, int32_t end) {
	log_line("APP preedit text=%s begin=%d end=%d", text ? text : "(null)", begin, end);
}
static void ti_commit(void *d, struct zwp_text_input_v3 *ti, const char *text) {
	log_line("APP commit text=%s", text ? text : "(null)");
}
static void ti_delete(void *d, struct zwp_text_input_v3 *ti, uint32_t before, uint32_t after) {
	log_line("APP delete before=%u after=%u", before, after);
}
static void ti_done(void *d, struct zwp_text_input_v3 *ti, uint32_t serial) {
	log_line("APP done serial=%u", serial);
}
static const struct zwp_text_input_v3_listener ti_listener = {
	.enter = ti_enter, .leave = ti_leave, .preedit_string = ti_preedit,
	.commit_string = ti_commit, .delete_surrounding_text = ti_delete, .done = ti_done,
};

static void seat_name(void *d, struct wl_seat *s, const char *name) {}
static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		struct wl_keyboard *kb = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(kb, &kb_listener, NULL);
	}
}
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps, .name = seat_name,
};

static void registry(void *d, struct wl_registry *r, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
		xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(iface, zwp_text_input_manager_v3_interface.name) == 0) {
		ti_manager = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
	}
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {
	.global = registry, .global_remove = registry_remove,
};

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) { fprintf(stderr, "no display\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(display);
	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	app_surface = surface;
	struct xdg_surface *xsurf = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xsurf, &surf_listener, NULL);
	struct xdg_toplevel *top = xdg_surface_get_toplevel(xsurf);
	xdg_toplevel_add_listener(top, &top_listener, NULL);
	xdg_toplevel_set_title(top, "ime-app-test");
	text_input = zwp_text_input_manager_v3_get_text_input(ti_manager, seat);
	zwp_text_input_v3_add_listener(text_input, &ti_listener, NULL);
	wl_surface_commit(surface);
	while (wl_display_dispatch(display) >= 0) {
	}
	return 0;
}
