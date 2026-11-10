/* A stand-in input method: on activate it grabs the keyboard, shows
 * a candidate popup and sends a preedit string; when a key reaches it
 * through the grab, it commits a kanji string and clears the preedit.
 * Logs everything, one line per event. */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "input-method-unstable-v2-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwp_input_method_manager_v2 *im_manager;
static struct zwp_input_method_v2 *im;
static int activated;
static uint32_t done_count; /* the commit serial: dones the compositor sent */

static void log_line(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

static struct wl_buffer *tiny_buffer(int w, int h) {
	int stride = w * 4, size = stride * h;
	int fd = memfd_create("ime-test", MFD_CLOEXEC);
	if (ftruncate(fd, size) < 0) {
		perror("ftruncate");
		exit(1);
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memset(data, 0x80, size);
	munmap(data, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
		WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buf;
}

/* the grab keyboard behaves like a wl_keyboard for events */
static void grab_keymap(void *d, struct zwp_input_method_keyboard_grab_v2 *g,
		uint32_t format, int32_t fd, uint32_t size) {
	log_line("IME grab keymap");
	close(fd);
}
static void grab_key(void *d, struct zwp_input_method_keyboard_grab_v2 *g,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	log_line("IME grab key keycode=%u state=%u", key, state);
	if (state == 1 && activated == 1) {
		activated = 2;
		zwp_input_method_v2_commit_string(im, "愛");
		zwp_input_method_v2_set_preedit_string(im, "", 0, 0);
		zwp_input_method_v2_commit(im, done_count);
		log_line("IME sent commit");
	}
}
static void grab_mods(void *d, struct zwp_input_method_keyboard_grab_v2 *g,
		uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
	log_line("IME grab modifiers");
}
static void grab_repeat(void *d, struct zwp_input_method_keyboard_grab_v2 *g,
		int32_t rate, int32_t delay) {}
static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
	.keymap = grab_keymap, .key = grab_key, .modifiers = grab_mods,
	.repeat_info = grab_repeat,
};

static void popup_rect(void *d, struct zwp_input_popup_surface_v2 *p,
		int32_t x, int32_t y, int32_t w, int32_t h) {
	log_line("IME popup rect x=%d y=%d w=%d h=%d", x, y, w, h);
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {
	.text_input_rectangle = popup_rect,
};

static void im_activate(void *d, struct zwp_input_method_v2 *i) {
	log_line("IME activate");
	if (activated) {
		return;
	}
	activated = 1; /* the preedit goes out on the done that follows */
}
static void im_deactivate(void *d, struct zwp_input_method_v2 *i) {
	log_line("IME deactivate");
}
static void im_surrounding(void *d, struct zwp_input_method_v2 *i,
		const char *text, uint32_t cursor, uint32_t anchor) {
	log_line("IME surrounding text=%s cursor=%u anchor=%u", text, cursor, anchor);
}
static void im_content_type(void *d, struct zwp_input_method_v2 *i,
		uint32_t hint, uint32_t purpose) {
	log_line("IME content-type hint=%u purpose=%u", hint, purpose);
}
static void im_cause(void *d, struct zwp_input_method_v2 *i, uint32_t cause) {
	log_line("IME text-change-cause %u", cause);
}
static void im_done(void *d, struct zwp_input_method_v2 *i) {
	done_count++;
	log_line("IME done count=%u", done_count);
	uint32_t serial = done_count;
	if (activated == 1) {
		struct zwp_input_method_keyboard_grab_v2 *grab =
			zwp_input_method_v2_grab_keyboard(im);
		zwp_input_method_keyboard_grab_v2_add_listener(grab, &grab_listener, NULL);
		/* candidate window */
		struct wl_surface *surface = wl_compositor_create_surface(compositor);
		struct zwp_input_popup_surface_v2 *popup =
			zwp_input_method_v2_get_input_popup_surface(im, surface);
		zwp_input_popup_surface_v2_add_listener(popup, &popup_listener, NULL);
		wl_surface_attach(surface, tiny_buffer(60, 20), 0, 0);
		wl_surface_damage(surface, 0, 0, 60, 20);
		wl_surface_commit(surface);
		/* first composition */
		zwp_input_method_v2_set_preedit_string(im, "あい", 0, 6);
		zwp_input_method_v2_commit(im, serial);
		log_line("IME sent preedit");
	}
}
static void im_unavailable(void *d, struct zwp_input_method_v2 *i) {
	log_line("IME unavailable");
}
static const struct zwp_input_method_v2_listener im_listener = {
	.activate = im_activate, .deactivate = im_deactivate,
	.surrounding_text = im_surrounding, .text_change_cause = im_cause,
	.content_type = im_content_type, .done = im_done,
	.unavailable = im_unavailable,
};

static void registry(void *d, struct wl_registry *r, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
	} else if (strcmp(iface, zwp_input_method_manager_v2_interface.name) == 0) {
		im_manager = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
		log_line("IME manager found");
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
	if (!im_manager) { fprintf(stderr, "no input method manager\n"); return 2; }
	im = zwp_input_method_manager_v2_get_input_method(im_manager, seat);
	zwp_input_method_v2_add_listener(im, &im_listener, NULL);
	while (wl_display_dispatch(display) >= 0) {
	}
	return 0;
}
