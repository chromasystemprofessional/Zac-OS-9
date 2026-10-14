/* Easy Access's state machines (compositor/src/access.c): Sticky Keys'
 * latching and locking, Zoom's steps and the part of the screen it shows. */
#include <assert.h>
#include <math.h>

#include "../src/access.c"
#include "../src/output.c"

static void tap(struct sticky *s, unsigned mod) {
	sticky_event(s, mod, true);
	sticky_event(s, mod, false);
}

/* Another key typed: down, up, and the latch used. */
static void type(struct sticky *s) {
	sticky_event(s, 0, true);
	sticky_event(s, 0, false);
	sticky_key_done(s);
}

static void test_sticky(void) {
	struct sticky s = { 0 };
	assert(sticky_mod_for(XKB_KEY_Shift_L) == STICKY_SHIFT);
	assert(sticky_mod_for(XKB_KEY_Super_R) == STICKY_COMMAND);
	assert(sticky_mod_for(XKB_KEY_Alt_L) == STICKY_OPTION);
	assert(sticky_mod_for(XKB_KEY_a) == 0);

	/* Once: for the next key only. */
	tap(&s, STICKY_COMMAND);
	assert(s.latched == STICKY_COMMAND && !s.locked);
	type(&s);
	assert(!s.latched && !s.locked);

	/* Two at once, one at a time: ⌘ then Shift then the key. */
	tap(&s, STICKY_COMMAND);
	tap(&s, STICKY_SHIFT);
	assert(s.latched == (STICKY_COMMAND | STICKY_SHIFT));
	type(&s);
	assert(!s.latched);

	/* Twice: locked through several keys; a third time lets go. */
	tap(&s, STICKY_SHIFT);
	tap(&s, STICKY_SHIFT);
	assert(!s.latched && s.locked == STICKY_SHIFT);
	type(&s);
	type(&s);
	assert(s.locked == STICKY_SHIFT);
	tap(&s, STICKY_SHIFT);
	assert(!s.locked && !s.latched);

	/* Held down with another key: an ordinary chord, nothing sticks. */
	sticky_event(&s, STICKY_CONTROL, true);
	sticky_event(&s, 0, true);
	sticky_event(&s, 0, false);
	sticky_event(&s, STICKY_CONTROL, false);
	assert(!s.latched && !s.locked);
}

static void test_zoom(void) {
	assert(access_zoom_step(2, 1) == 3);
	assert(access_zoom_step(2, -1) == 1.5);
	assert(access_zoom_step(16, 1) == 16);
	assert(access_zoom_step(1.5, -1) == 1);
	assert(access_zoom_step(5, 1) == 8); /* between steps: from the next one up */

	struct wlr_fbox box;
	/* At the top-left corner, the top-left quarter. */
	output_zoom_box(800, 600, 0, 0, 2, &box);
	assert(box.x == 0 && box.y == 0 && box.width == 400 && box.height == 300);
	/* At the bottom-right, the bottom-right quarter. */
	output_zoom_box(800, 600, 800, 600, 2, &box);
	assert(box.x == 400 && box.y == 300);
	/* Anywhere, the point under the pointer stays under it. */
	const double px = 123, py = 456, f = 4;
	output_zoom_box(800, 600, px, py, f, &box);
	assert(fabs(box.x + px / f - px) < 1e-9 && fabs(box.y + py / f - py) < 1e-9);
}

int main(void) {
	test_sticky();
	test_zoom();
	return 0;
}
