/* The touchscreen's tap and two-finger tap (touch_tap.c). */
#include <stdio.h>
#include <string.h>

#include "touch_tap.h"

static int failures;

static void expect(struct tap_actions got, const char *want, const char *what) {
	static const char *names[] = { "none", "press", "move", "release", "right" };
	char text[64] = "";
	for (int i = 0; i < got.n; i++) {
		if (i) {
			strcat(text, " ");
		}
		strcat(text, names[got.a[i]]);
	}
	const bool ok = strcmp(text, want) == 0;
	printf("%s: %s (%s)\n", ok ? "ok" : "FAIL", what, text);
	failures += !ok;
}

int main(void) {
	struct tap_state s = { 0 };

	/* One finger tapped: a click, once it lifts. */
	tap_down_first(&s, 1, 1000, 100, 100);
	expect(tap_motion(&s, 1, 103, 102), "", "a finger wandering a little: nothing yet");
	expect(tap_up(&s, 1, 1080), "press release", "a quick tap is a click");

	/* Held still: pressed when the wait is over, released on lifting. */
	tap_down_first(&s, 2, 2000, 50, 50);
	expect(tap_wait_over(&s), "press", "held: the press once the wait is over");
	expect(tap_motion(&s, 2, 51, 50), "move", "then it moves the pointer");
	expect(tap_up(&s, 2, 2900), "release", "and lifting releases");

	/* A drag at once: pressed where it began, then moved. */
	tap_down_first(&s, 3, 3000, 50, 50);
	expect(tap_motion(&s, 3, 80, 50), "press move", "a drag presses at once");
	expect(tap_wait_over(&s), "", "the wait over later: nothing more");
	expect(tap_up(&s, 3, 3500), "release", "lifting releases");

	/* Two fingers tapped together: a right-click. */
	tap_down_first(&s, 4, 4000, 200, 200);
	if (!tap_down_other(&s, 5, 4040)) {
		printf("FAIL: a second finger soon after is a two-finger tap\n");
		failures++;
	}
	expect(tap_wait_over(&s), "", "no left press for a two-finger tap");
	expect(tap_up(&s, 5, 4150), "", "one finger up: not yet");
	expect(tap_up(&s, 4, 4180), "right", "both up: a right-click");

	/* Two fingers that moved: no click at all. */
	tap_down_first(&s, 6, 5000, 200, 200);
	tap_down_other(&s, 7, 5030);
	expect(tap_motion(&s, 6, 260, 200), "", "two fingers sliding");
	tap_up(&s, 6, 5200);
	expect(tap_up(&s, 7, 5210), "", "two fingers that slid: no right-click");

	/* Two fingers held too long: no click. */
	tap_down_first(&s, 8, 6000, 10, 10);
	tap_down_other(&s, 9, 6020);
	tap_up(&s, 8, 6900);
	expect(tap_up(&s, 9, 6950), "", "two fingers held: no right-click");

	/* A second finger after the press: it rests. */
	tap_down_first(&s, 10, 7000, 10, 10);
	tap_wait_over(&s);
	if (tap_down_other(&s, 11, 7300) || tap_owns(&s, 11)) {
		printf("FAIL: a finger after the press is ignored\n");
		failures++;
	}
	expect(tap_up(&s, 11, 7400), "", "its lift: nothing");
	expect(tap_up(&s, 10, 7500), "release", "the mouse finger's lift releases");

	/* Taken back while pressed: released. */
	tap_down_first(&s, 12, 8000, 10, 10);
	tap_wait_over(&s);
	expect(tap_cancel(&s), "release", "cancelled while pressed: released");
	if (!tap_down_first(&s, 13, 9000, 0, 0)) {
		printf("FAIL: after a cancel, a new finger is the mouse\n");
		failures++;
	}

	return failures ? 1 : 0;
}
