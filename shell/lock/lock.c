/*
 * zacos9-lock: the lock screen ("Ask for my password to wake", the Screen
 * Saver panel). zacos9-wm starts it (compositor/src/lock.c); it locks the
 * session with ext-session-lock-v1, covers every screen with the desktop
 * pattern, and on the main screen asks for the password of the person
 * logged in, checked through PAM: the "zacos9-lock" service
 * (/etc/pam.d/zacos9-lock, installed with ZacOS), or "login" if that is
 * missing, so nobody is ever locked out by a missing file. The check runs
 * in a child process, so the window keeps drawing meanwhile. Unlocked, it
 * exits. If it dies instead, zacos9-wm keeps the screens locked and starts
 * it again.
 *
 * Built with -DZACOS9_LOCK_TEST (test-lock, for compositor/tests/
 * test_lock.py) it takes "sesame" instead of asking PAM, and says on
 * stdout when it locks, rejects a password and unlocks. See
 * docs/energy-saver.md.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#ifndef ZACOS9_LOCK_TEST
#include <security/pam_appl.h>
#endif

#include "draw.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "logo.h"
#include "patterns.h"
#include "settings.h"
#include "text.h"
#include "viewporter-client-protocol.h"
#include "widgets.h"

#define DIALOG_W 360
#define DIALOG_H 164
#define FIELD_W 220
#define MAX_PASSWORD 256

struct screen;

struct buffer {
	struct wl_buffer *wl;
	uint32_t *px;
	size_t size;
	bool busy;
	struct screen *screen;
};

struct screen {
	struct wl_list link;
	uint32_t global;
	struct wl_output *output;
	char *name;
	int scale;
	int quarters; /* 1.25, 1.5, 1.75 (5, 6, 7): drawn at the screen's pixels; else 0 */
	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;
	int w, h;
	struct buffer buffers[2];
	uint32_t *logical;
	bool stale; /* a redraw waits for a buffer to come back */
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wp_viewporter *viewporter;
static struct wp_fractional_scale_manager_v1 *fractional_manager;
static struct ext_session_lock_manager_v1 *lock_manager;
static struct ext_session_lock_v1 *lock;
static struct wl_list screens;
static struct xkb_context *xkb;
static struct xkb_keymap *keymap;
static struct xkb_state *xkb_state;
static struct screen *pointer_screen;
static int pointer_x, pointer_y;
static bool button_down;

static char user[256], full_name[256], main_name[128];
static char password[MAX_PASSWORD];
static size_t password_len;
static const char *status = "";
static bool checking;
static pid_t checker = -1;
static int checker_fd = -1;
static bool locked;

/* ---- the password ------------------------------------------------------ */

static void forget_password(void) {
	explicit_bzero(password, sizeof(password));
	password_len = 0;
}

#ifndef ZACOS9_LOCK_TEST
static int conversation(int n, const struct pam_message **messages, struct pam_response **responses,
		void *data) {
	struct pam_response *r = calloc((size_t)n, sizeof(*r));
	if (!r) {
		return PAM_BUF_ERR;
	}
	for (int i = 0; i < n; i++) {
		if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF || messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			r[i].resp = strdup(data);
			if (!r[i].resp) {
				for (int j = 0; j < i; j++) {
					free(r[j].resp);
				}
				free(r);
				return PAM_BUF_ERR;
			}
		}
	}
	*responses = r;
	return PAM_SUCCESS;
}

static const char *pam_service(void) {
	/* Ours if installed; else the login stack, which every system has. */
	return access("/etc/pam.d/zacos9-lock", F_OK) == 0 ||
		access("/usr/lib/pam.d/zacos9-lock", F_OK) == 0 ? "zacos9-lock" : "login";
}
#endif

/* In the child: whether the password is right. */
static bool password_right(void) {
#ifdef ZACOS9_LOCK_TEST
	return strcmp(password, "sesame") == 0;
#else
	struct pam_conv conv = { conversation, password };
	pam_handle_t *pam = NULL;
	if (pam_start(pam_service(), user, &conv, &pam) != PAM_SUCCESS) {
		return false;
	}
	const int result = pam_authenticate(pam, 0);
	pam_end(pam, result);
	return result == PAM_SUCCESS;
#endif
}

static void redraw_all(void);

static void start_check(void) {
	if (checking || password_len == 0) {
		return;
	}
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0) {
		return;
	}
	checker = fork();
	if (checker == 0) {
		close(fds[0]);
		const char ok = password_right() ? 'y' : 'n';
		forget_password();
		if (write(fds[1], &ok, 1) < 0) {
			_exit(1);
		}
		_exit(0);
	}
	close(fds[1]);
	if (checker < 0) {
		close(fds[0]);
		return;
	}
	checker_fd = fds[0];
	checking = true;
	status = "Checking\xe2\x80\xa6";
	redraw_all();
}

static void unlock_and_exit(void) {
	ext_session_lock_v1_unlock_and_destroy(lock);
	wl_display_roundtrip(display);
#ifdef ZACOS9_LOCK_TEST
	printf("unlocked\n");
	fflush(stdout);
#endif
	exit(0);
}

static void check_finished(void) {
	char ok = 'n';
	if (read(checker_fd, &ok, 1) != 1) {
		ok = 'n';
	}
	close(checker_fd);
	checker_fd = -1;
	waitpid(checker, NULL, 0);
	checker = -1;
	checking = false;
	forget_password();
	if (ok == 'y') {
		unlock_and_exit();
	}
	status = "That password isn\xe2\x80\x99t right. Please try again.";
#ifdef ZACOS9_LOCK_TEST
	printf("wrong\n");
	fflush(stdout);
#endif
	redraw_all();
}

/* ---- drawing ------------------------------------------------------------- */

static void redraw(struct screen *s);

static void buffer_release(void *data, struct wl_buffer *b) {
	struct buffer *buffer = data;
	buffer->busy = false;
	if (buffer->screen && buffer->screen->stale) {
		redraw(buffer->screen); /* what changed while both were on screen */
	}
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void buffer_free(struct buffer *b) {
	if (b->wl) {
		wl_buffer_destroy(b->wl);
		munmap(b->px, b->size);
	}
	memset(b, 0, sizeof(*b));
}

static bool buffer_make(struct buffer *b, int width, int height) {
	b->size = (size_t)width * height * 4;
	const int fd = memfd_create("zacos9-lock", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)b->size) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return false;
	}
	b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->px == MAP_FAILED) {
		close(fd);
		b->px = NULL;
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	return true;
}

static bool is_main(struct screen *s) {
	if (main_name[0]) {
		return s->name && strcmp(s->name, main_name) == 0;
	}
	/* No outputs file: the first screen. */
	struct screen *last = NULL, *each;
	wl_list_for_each(each, &screens, link) {
		last = each;
	}
	return s == last;
}

/* The dialog's place on its screen, and its Unlock button's. */
static void dialog_box(struct screen *s, int *x, int *y) {
	*x = (s->w - DIALOG_W) / 2;
	*y = (s->h - DIALOG_H) / 3;
}

static void button_box(struct screen *s, int *x, int *y, int *w) {
	int dx, dy;
	dialog_box(s, &dx, &dy);
	*w = 70;
	*x = dx + DIALOG_W - 14 - *w;
	*y = dy + DIALOG_H - 14 - PL_BUTTON_H;
}

static void text_at(struct pl_canvas *c, const char *utf8, int x, int baseline, int max_w,
		enum pl_font font, uint32_t color) {
	struct plat_text *t = text_render_font(utf8, max_w, font);
	if (t) {
		if (t->ink_l >= 0) {
			pl_text(c, t, x + t->ink_l, baseline, color);
		}
		text_destroy(t);
	}
}

static void paint_dialog(struct screen *s, struct pl_canvas *c) {
	int x, y;
	dialog_box(s, &x, &y);
	const int x1 = x + DIALOG_W - 1, y1 = y + DIALOG_H - 1;
	/* A Platinum dialog: black line, white and gray bevel, gray face. */
	pl_outline(c, x, y, x1, y1, C_BLACK);
	pl_fill(c, x + 1, y + 1, x1 - 1, y1 - 1, GRAY(0xD));
	pl_hline(c, x + 1, x1 - 1, y + 1, C_WHITE);
	pl_vline(c, x + 1, y + 1, y1 - 1, C_WHITE);
	pl_hline(c, x + 1, x1 - 1, y1 - 1, GRAY(0x8));
	pl_vline(c, x1 - 1, y + 1, y1 - 1, GRAY(0x8));

	pl_image_blend(c, x + 16, y + 16, logo_pixels_hq(), PL_LOGO_SIZE_HQ, PL_LOGO_SIZE_HQ, false);
	char line[512];
	snprintf(line, sizeof(line), "Enter the password for \xe2\x80\x9c%s\xe2\x80\x9d", full_name);
	const int tx = x + 16 + PL_LOGO_SIZE_HQ + 14, tw = x1 - 14 - tx;
	text_at(c, line, tx, y + 30, tw, PL_FONT_SYSTEM, C_BLACK);
	text_at(c, "to wake ZacOS.", tx, y + 46, tw, PL_FONT_SYSTEM, C_BLACK);

	/* The password field: a bullet a character, and the caret. */
	const int fx = tx, fy = y + 60;
	pl_edit_frame_paint(c, fx, fy, fx + FIELD_W - 1, fy + PL_EDIT_H - 1);
	pl_focus_ring_paint(c, fx - 3, fy - 3, fx + FIELD_W + 2, fy + PL_EDIT_H + 2, pl_accent_current(),
		GRAY(0xD));
	char bullets[3 * 40 + 1] = "";
	for (size_t i = 0; i < password_len && i < 40; i++) {
		strcat(bullets, "\xe2\x80\xa2");
	}
	int caret = fx + 5;
	if (password_len) {
		struct plat_text *t = text_render_font(bullets, FIELD_W - 12, PL_FONT_VIEWS);
		if (t) {
			pl_text(c, t, fx + 5 + (t->ink_l > 0 ? t->ink_l : 0), fy + 15, C_BLACK);
			caret += t->advance;
			text_destroy(t);
		}
	}
	if (!checking) {
		pl_vline(c, caret, fy + 4, fy + PL_EDIT_H - 5, C_BLACK);
	}
	text_at(c, status, tx, fy + PL_EDIT_H + 18, tw, PL_FONT_VIEWS, C_BLACK);

	int bx, by, bw;
	button_box(s, &bx, &by, &bw);
	struct plat_text *label = text_render("Unlock", bw);
	if (label) {
		const bool pressed = button_down && pointer_screen == s && pointer_x >= bx && pointer_x < bx + bw &&
			pointer_y >= by && pointer_y < by + PL_BUTTON_H;
		pl_button_paint(c, bx, by, bw, label, PL_BUTTON_DEFAULT | (pressed ? PL_BUTTON_PRESSED : 0) |
			(checking ? PL_BUTTON_DISABLED : 0));
		text_destroy(label);
	}
}

static void paint(struct screen *s, struct pl_canvas *c) {
	char id[64] = "";
	pl_setting("pattern", id, sizeof(id));
	pl_pattern_fill(c, pl_pattern_find(id), 0, 0, s->w - 1, s->h - 1);
	if (is_main(s)) {
		paint_dialog(s, c);
	}
}

static void redraw(struct screen *s) {
	if (!s->surface || s->w <= 0) {
		return;
	}
	struct buffer *b = !s->buffers[0].busy ? &s->buffers[0] : !s->buffers[1].busy ? &s->buffers[1] : NULL;
	if (!b) {
		s->stale = true; /* drawn when one comes back */
		return;
	}
	s->stale = false;
	b->screen = s;
	const int k = s->scale;
	uint32_t *drawn = k == 1 && !s->quarters ? b->px : s->logical;
	struct pl_canvas c = { drawn, s->w, 0, 0, s->w, s->h,
		s->quarters ? pl_text_log_new(drawn, s->w) : NULL };
	paint(s, &c);
	if (s->quarters) {
		/* Each logical pixel one or two of the screen's, the text in the
		 * font for this scale: sharp. */
		pl_snap_scale_canvas(&c, 0, 0, s->quarters, b->px);
		pl_text_log_free(c.log);
	} else if (k > 1) {
		/* Each logical pixel k×k: sharp. */
		for (int y = 0; y < s->h; y++) {
			uint32_t *out = b->px + (size_t)y * k * s->w * k;
			const uint32_t *in = s->logical + (size_t)y * s->w;
			for (int x = 0; x < s->w; x++) {
				for (int i = 0; i < k; i++) {
					out[x * k + i] = in[x];
				}
			}
			for (int i = 1; i < k; i++) {
				memcpy(out + (size_t)i * s->w * k, out, (size_t)s->w * k * 4);
			}
		}
	}
	b->busy = true;
	wl_surface_set_buffer_scale(s->surface, s->quarters ? 1 : k);
	if (s->viewport) {
		wp_viewport_set_destination(s->viewport, s->quarters ? s->w : -1, s->quarters ? s->h : -1);
	}
	wl_surface_attach(s->surface, b->wl, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(s->surface);
}

static void redraw_all(void) {
	struct screen *s;
	wl_list_for_each(s, &screens, link) {
		if (is_main(s)) {
			redraw(s);
		}
	}
}

/* ---- the lock and its surfaces ---------------------------------------- */

/* Buffers of the screen's size at its scale: s times the logical size,
 * or at a fractional scale its own pixels; a logical frame to draw in. */
static bool make_buffers(struct screen *s) {
	buffer_free(&s->buffers[0]);
	buffer_free(&s->buffers[1]);
	free(s->logical);
	const bool fractional = s->quarters != 0;
	const int bw = fractional ? pl_snap(s->w, s->quarters) : s->w * s->scale;
	const int bh = fractional ? pl_snap(s->h, s->quarters) : s->h * s->scale;
	s->logical = s->scale > 1 || fractional ? malloc((size_t)s->w * s->h * 4) : NULL;
	if (!buffer_make(&s->buffers[0], bw, bh) || !buffer_make(&s->buffers[1], bw, bh) ||
			((s->scale > 1 || fractional) && !s->logical)) {
		s->w = 0;
		return false;
	}
	return true;
}

static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *ls, uint32_t serial,
		uint32_t w, uint32_t h) {
	struct screen *s = data;
	ext_session_lock_surface_v1_ack_configure(ls, serial);
	if ((int)w != s->w || (int)h != s->h || !s->buffers[0].wl) {
		s->w = (int)w;
		s->h = (int)h;
		if (!make_buffers(s)) {
			return;
		}
	}
	redraw(s);
}

/* 1.25, 1.5, 1.75 (in 120ths): the screen's own pixels from now on. */
static void preferred_scale(void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
	struct screen *s = data;
	const int quarters = (int)((scale + 15) / 30);
	const int q = quarters % 4 ? quarters : 0;
	if (q == s->quarters) {
		return;
	}
	s->quarters = q;
	if (s->w > 0 && make_buffers(s)) {
		redraw(s);
	}
}

static const struct wp_fractional_scale_v1_listener fractional_listener = { preferred_scale };

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	lock_surface_configure,
};

static void cover(struct screen *s) {
	if (s->surface || !lock || !s->name) {
		return;
	}
	s->surface = wl_compositor_create_surface(compositor);
	if (viewporter && fractional_manager) {
		s->viewport = wp_viewporter_get_viewport(viewporter, s->surface);
		s->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(fractional_manager,
			s->surface);
		wp_fractional_scale_v1_add_listener(s->fractional, &fractional_listener, s);
	}
	s->lock_surface = ext_session_lock_v1_get_lock_surface(lock, s->surface, s->output);
	ext_session_lock_surface_v1_add_listener(s->lock_surface, &lock_surface_listener, s);
}

static void lock_locked(void *data, struct ext_session_lock_v1 *l) {
	locked = true;
#ifdef ZACOS9_LOCK_TEST
	printf("locked\n");
	fflush(stdout);
#endif
}

static void lock_finished(void *data, struct ext_session_lock_v1 *l) {
	/* Refused (another lock is in place), or the compositor is done with us. */
	exit(locked ? 1 : 0);
}

static const struct ext_session_lock_v1_listener lock_listener = { lock_locked, lock_finished };

/* ---- input ----------------------------------------------------------------- */

static void keyboard_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size) {
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_keymap *next = xkb_keymap_new_from_string(xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!next) {
		return;
	}
	xkb_state_unref(xkb_state);
	xkb_keymap_unref(keymap);
	keymap = next;
	xkb_state = xkb_state_new(keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface,
		struct wl_array *keys) {
}

static void keyboard_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface) {
}

static void keyboard_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key,
		uint32_t state) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !xkb_state || checking) {
		return;
	}
	const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, key + 8);
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		start_check();
		return;
	}
	if (sym == XKB_KEY_BackSpace) {
		/* One character back (UTF-8: its continuation bytes too). */
		while (password_len > 0 && (password[--password_len] & 0xC0) == 0x80) {
		}
		password[password_len] = '\0';
	} else if (sym == XKB_KEY_Escape) {
		forget_password();
	} else {
		char utf8[16];
		const int n = xkb_state_key_get_utf8(xkb_state, key + 8, utf8, sizeof(utf8));
		if (n <= 0 || (unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f ||
				password_len + (size_t)n >= sizeof(password)) {
			return;
		}
		memcpy(password + password_len, utf8, (size_t)n);
		password_len += (size_t)n;
		password[password_len] = '\0';
	}
	status = "";
	redraw_all();
}

static void keyboard_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t depressed,
		uint32_t latched, uint32_t locked_mods, uint32_t group) {
	if (xkb_state) {
		xkb_state_update_mask(xkb_state, depressed, latched, locked_mods, 0, 0, group);
	}
}

static void keyboard_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers, keyboard_repeat,
};

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface,
		wl_fixed_t x, wl_fixed_t y) {
	struct screen *s;
	pointer_screen = NULL;
	wl_list_for_each(s, &screens, link) {
		if (s->surface == surface) {
			pointer_screen = s;
		}
	}
	pointer_x = wl_fixed_to_int(x);
	pointer_y = wl_fixed_to_int(y);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface) {
	pointer_screen = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	pointer_x = wl_fixed_to_int(x);
	pointer_y = wl_fixed_to_int(y);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state) {
	struct screen *s = pointer_screen;
	if (!s || !is_main(s)) {
		return;
	}
	int bx, by, bw;
	button_box(s, &bx, &by, &bw);
	const bool on = pointer_x >= bx && pointer_x < bx + bw && pointer_y >= by && pointer_y < by + PL_BUTTON_H;
	button_down = state == WL_POINTER_BUTTON_STATE_PRESSED && on;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED && on) {
		start_check();
	}
	redraw(s);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
	.button = pointer_button, .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	static struct wl_keyboard *keyboard;
	static struct wl_pointer *pointer;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
		keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
}

static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

/* ---- outputs ---------------------------------------------------------------- */

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
		int32_t subpixel, const char *make, const char *model, int32_t transform) {
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t r) {
}

static void output_done(void *data, struct wl_output *o) {
	cover(data); /* a screen plugged in while locked */
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
	((struct screen *)data)->scale = factor > 0 ? factor : 1;
}

static void output_name(void *data, struct wl_output *o, const char *name) {
	struct screen *s = data;
	free(s->name);
	s->name = strdup(name);
}

static void output_description(void *data, struct wl_output *o, const char *d) {
}

static const struct wl_output_listener output_listener = {
	output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *iface,
		uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	} else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		fractional_manager = wl_registry_bind(registry, name,
			&wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
		lock_manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		/* Version 2: no pointer frames or axis extras to handle. */
		struct wl_seat *seat = wl_registry_bind(registry, name, &wl_seat_interface, 2);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && version >= 4) {
		struct screen *s = calloc(1, sizeof(*s));
		s->global = name;
		s->scale = 1;
		s->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(s->output, &output_listener, s);
		wl_list_insert(&screens, &s->link);
	}
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	struct screen *s, *tmp;
	wl_list_for_each_safe(s, tmp, &screens, link) {
		if (s->global == name) {
			if (pointer_screen == s) {
				pointer_screen = NULL;
			}
			if (s->lock_surface) {
				ext_session_lock_surface_v1_destroy(s->lock_surface);
				if (s->fractional) {
					wp_fractional_scale_v1_destroy(s->fractional);
					wp_viewport_destroy(s->viewport);
				}
				wl_surface_destroy(s->surface);
			}
			buffer_free(&s->buffers[0]);
			buffer_free(&s->buffers[1]);
			free(s->logical);
			wl_output_release(s->output);
			wl_list_remove(&s->link);
			free(s->name);
			free(s);
		}
	}
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

/* The main screen (the menu bar's), from zacos9-wm's outputs file. */
static void find_main_screen(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *wayland = getenv("WAYLAND_DISPLAY");
	char path[1024], line[512], name[128];
	int main_flag;
	snprintf(path, sizeof(path), "%s/zacos9-outputs-%s", runtime ? runtime : "", wayland ? wayland : "");
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		const char *m = strstr(line, " main ");
		if (sscanf(line, "output %127s", name) == 1 && m && sscanf(m, " main %d", &main_flag) == 1 &&
				main_flag == 1) {
			snprintf(main_name, sizeof(main_name), "%s", name);
		}
	}
	fclose(f);
}

int main(void) {
	/* The password never goes to swap or a core dump. */
	mlock(password, sizeof(password));
	prctl(PR_SET_DUMPABLE, 0);
	signal(SIGPIPE, SIG_IGN);
	const struct passwd *pw = getpwuid(getuid());
	if (!pw) {
		fprintf(stderr, "zacos9-lock: who is this?\n");
		return 1;
	}
	snprintf(user, sizeof(user), "%s", pw->pw_name);
	snprintf(full_name, sizeof(full_name), "%s", pw->pw_gecos && *pw->pw_gecos ? pw->pw_gecos : pw->pw_name);
	char *comma = strchr(full_name, ',');
	if (comma) {
		*comma = '\0';
	}
	if (!full_name[0]) {
		snprintf(full_name, sizeof(full_name), "%s", pw->pw_name);
	}
	find_main_screen();

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "zacos9-lock: no Wayland display\n");
		return 1;
	}
	xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	wl_list_init(&screens);
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display); /* the screens' names */
	if (!compositor || !shm || !lock_manager) {
		fprintf(stderr, "zacos9-lock: the compositor can't lock the session\n");
		return 1;
	}
	lock = ext_session_lock_manager_v1_lock(lock_manager);
	ext_session_lock_v1_add_listener(lock, &lock_listener, NULL);
	struct screen *s;
	wl_list_for_each(s, &screens, link) {
		cover(s);
	}
	for (;;) {
		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);
		struct pollfd fds[2] = {
			{ wl_display_get_fd(display), POLLIN, 0 }, { checker_fd, POLLIN, 0 },
		};
		if (poll(fds, checker_fd >= 0 ? 2 : 1, -1) < 0 && errno != EINTR) {
			wl_display_cancel_read(display);
			return 1;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0) {
				return 1;
			}
		} else {
			wl_display_cancel_read(display);
		}
		if ((fds[0].revents & (POLLERR | POLLHUP)) || wl_display_dispatch_pending(display) < 0) {
			return 1;
		}
		if (checker_fd >= 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
			check_finished();
		}
	}
}
