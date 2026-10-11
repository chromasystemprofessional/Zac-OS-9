/*
 * When screens and the system sleep (src/idle_policy.c): the screen saver
 * first, then display sleep; a screen kept awake (a full-screen window, a
 * video asking) stays on while the others sleep; the system sleeps only
 * when nothing keeps a screen awake.
 */
#include <stdio.h>

#include "idle_policy.h"

static int fails;

static void check(int ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(void) {
	const long min = 60000;
	const struct idle_timeouts t = { .saver_ms = 5 * min, .display_ms = 10 * min, .system_ms = 30 * min };

	check(idle_output_state(&t, 0, 0) == IDLE_AWAKE &&
		idle_output_state(&t, 5 * min - 1, 0) == IDLE_AWAKE, "awake until the screen saver's time");
	check(idle_output_state(&t, 5 * min, 0) == IDLE_SAVER &&
		idle_output_state(&t, 10 * min - 1, 0) == IDLE_SAVER, "then the screen saver");
	check(idle_output_state(&t, 10 * min, 0) == IDLE_ASLEEP &&
		idle_output_state(&t, 600 * min, 0) == IDLE_ASLEEP, "then the display sleeps");
	check(idle_output_next(&t, 0, 0) == 5 * min && idle_output_next(&t, 6 * min, 0) == 4 * min &&
		idle_output_next(&t, 10 * min, 0) == -1, "the next change is timed from now");

	/* Two screens, one playing a film full screen. */
	check(idle_output_state(&t, 7 * min, 1) == IDLE_AWAKE && idle_output_state(&t, 7 * min, 0) == IDLE_SAVER &&
		idle_output_state(&t, 20 * min, 1) == IDLE_AWAKE && idle_output_state(&t, 20 * min, 0) == IDLE_ASLEEP,
		"a screen kept awake stays on while the other shows the saver, then sleeps");
	check(idle_output_next(&t, 7 * min, 1) == -1, "and has nothing to wait for");
	check(!idle_system_due(&t, 60 * min, 1) && idle_system_next(&t, 60 * min, 1) == -1,
		"the system never sleeps while a screen is kept awake");
	check(!idle_system_due(&t, 30 * min - 1, 0) && idle_system_due(&t, 30 * min, 0) &&
		idle_system_next(&t, 20 * min, 0) == 10 * min, "otherwise it sleeps on time");

	/* Stages turned off. */
	const struct idle_timeouts no_saver = { .display_ms = 10 * min };
	check(idle_output_state(&no_saver, 9 * min, 0) == IDLE_AWAKE &&
		idle_output_state(&no_saver, 10 * min, 0) == IDLE_ASLEEP && idle_output_next(&no_saver, 0, 0) == 10 * min,
		"no screen saver: straight to display sleep");
	const struct idle_timeouts saver_only = { .saver_ms = 5 * min };
	check(idle_output_state(&saver_only, 600 * min, 0) == IDLE_SAVER && idle_output_next(&saver_only, 6 * min, 0) == -1,
		"no display sleep: the screen saver for good");
	const struct idle_timeouts late_saver = { .saver_ms = 20 * min, .display_ms = 10 * min };
	check(idle_output_state(&late_saver, 15 * min, 0) == IDLE_ASLEEP && idle_output_state(&late_saver, 25 * min, 0) == IDLE_ASLEEP &&
		idle_output_next(&late_saver, 0, 0) == 10 * min,
		"a screen saver timed after display sleep never shows");
	const struct idle_timeouts never = { 0 };
	check(idle_output_state(&never, 6000 * min, 0) == IDLE_AWAKE && idle_output_next(&never, 0, 0) == -1 &&
		!idle_system_due(&never, 6000 * min, 0) && idle_system_next(&never, 0, 0) == -1,
		"all never: nothing ever sleeps");
	return fails ? 1 : 0;
}
