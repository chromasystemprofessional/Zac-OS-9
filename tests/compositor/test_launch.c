#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "launch.h"
#include "server.h"

static unsigned int refreshes;
void input_refresh_cursor(struct plat_server *server) {
	refreshes++;
}

static int64_t clock_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(void) {
	struct plat_server server = {0};
	server.display = wl_display_create();
	assert(server.display);
	launch_init(&server);
	int owner, other;
	assert(!launch_pending(&server));
	assert(launch_begin(&server, &owner, 1, 0, "Firefox.desktop"));
	launch_ready(&server, 0, "unrelated");
	assert(launch_pending(&server));
	launch_ready(&server, 0, "firefox");
	assert(!launch_pending(&server));
	launch_update(&server, &owner, 1, getpid());
	assert(!launch_pending(&server)); /* Late PID updates must not resurrect feedback. */

	assert(launch_begin(&server, &owner, 2, 0, ""));
	launch_update(&server, &owner, 2, getppid());
	launch_ready(&server, getpid(), "");
	assert(!launch_pending(&server)); /* Wrapper's descendants count as ready. */

	assert(launch_begin(&server, &owner, 3, 0, "first"));
	assert(launch_begin(&server, &other, 3, 0, "second"));
	launch_cancel(&server, &owner, 3);
	assert(launch_pending(&server));
	launch_ready(&server, 0, "first");
	assert(launch_pending(&server));
	launch_cancel_owner(&server, &other);
	assert(!launch_pending(&server));
	launch_cancel(&server, &owner, 999);
	assert(!launch_pending(&server));

	for (uint32_t cookie = 0; cookie < 32; cookie++) {
		assert(launch_begin(&server, &owner, cookie, 0, ""));
	}
	assert(!launch_begin(&server, &owner, 33, 0, ""));
	launch_cancel_owner(&server, &owner);
	assert(!launch_pending(&server));

	int64_t start = clock_ms();
	assert(launch_begin(&server, &owner, 100, 0, "no-window.desktop"));
	while (launch_pending(&server) && clock_ms() - start < 17000) {
		assert(wl_event_loop_dispatch(wl_display_get_event_loop(server.display), 50) == 0);
	}
	assert(!launch_pending(&server));
	assert(clock_ms() - start >= 15000);
	assert(refreshes > 0);
	wl_display_destroy(server.display);
	puts("ok: matching, wrappers, concurrent launches, cancellation, limits and 15-second timeout");
	return 0;
}
