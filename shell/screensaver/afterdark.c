/*
 * The After Dark helper, seen from the C side (afterdark.h): start it, collect
 * the frame records it writes to a pipe, and scale the latest into a canvas.
 *
 * The helper is a separate process on purpose. A malformed 68k module can
 * fault or spin, and the screen saver (and the control panel's preview) must
 * not go with it. The pipe is non-blocking, so a slow module never stalls the
 * Wayland loop; the helper simply waits for the next read.
 */
#define _GNU_SOURCE
#include "afterdark.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "locate.h"
#include "settings.h"

#define FRAME_MAGIC 0x31464441u /* "ADF1" little-endian */
#define HEADER_SIZE 12
#define PALETTE_SIZE (256 * 3)

enum { READING_HEADER, READING_PALETTE, READING_PIXELS };

struct ad_renderer {
	pid_t pid;
	int fd;
	bool failed;
	/* The frame being assembled. It is swapped in only when complete, so a
	 * half-read record never tears the picture. */
	int state;
	size_t got;
	uint8_t header[HEADER_SIZE];
	uint8_t palette[PALETTE_SIZE];
	uint8_t *incoming;
	size_t incoming_len;
	int incoming_w, incoming_h;
	uint32_t incoming_ticks;
	/* The displayed frame. */
	struct ad_frame frame;
	uint8_t *pixels;
	uint8_t shown_palette[PALETTE_SIZE];
	bool new_frame;
};

bool pl_afterdark_available(void) {
	char program[4096];
	return pl_afterdark_program(program, sizeof(program));
}

bool pl_afterdark_program(char *out, size_t size) {
	return pl_sibling_program("zacos9-afterdark", out, size);
}

bool pl_afterdark_module(char *out, size_t size) {
	return pl_setting("screensaver-afterdark", out, size) && *out;
}

bool pl_afterdark_library_dir(char *out, size_t size) {
	const char *data = getenv("XDG_DATA_HOME");
	int n;
	if (data && *data) {
		n = snprintf(out, size, "%s/after-dark/modules", data);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) {
			return false;
		}
		n = snprintf(out, size, "%s/.local/share/after-dark/modules", home);
	}
	return n > 0 && (size_t)n < size;
}

static uint32_t read_u32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const uint8_t *p) {
	return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

struct ad_renderer *ad_renderer_start(const char *module, int fps) {
	char program[4096];
	if (!pl_sibling_program("zacos9-afterdark", program, sizeof(program))) {
		return NULL;
	}
	return ad_renderer_start_program(program, module, fps);
}

struct ad_renderer *ad_renderer_start_program(const char *program, const char *module, int fps) {
	int fds[2];
	if (!module || !*module || pipe(fds) != 0) {
		return NULL;
	}
	/* Format the rate before fork(): after it, only async-signal-safe calls
	 * are allowed until exec, and snprintf is not one. */
	const int rate = fps > 0 ? (fps > 60 ? 60 : fps) : 30;
	char rate_text[4];
	snprintf(rate_text, sizeof(rate_text), "%d", rate);
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	const pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return NULL;
	}
	if (pid == 0) {
		/* Only async-signal-safe calls between fork and exec. The
		 * panel may have SIGTERM and SIGINT blocked for the compositor's
		 * event loop; the helper must start with the default mask or
		 * stopping it would wait forever. */
		sigset_t empty;
		sigemptyset(&empty);
		sigprocmask(SIG_SETMASK, &empty, NULL);
		if (fds[1] != STDOUT_FILENO) {
			dup2(fds[1], STDOUT_FILENO);
			close(fds[1]);
		}
		if (fds[0] != STDOUT_FILENO) {
			close(fds[0]);
		}
		execl(program, program, "--module", module, "--fps", rate_text, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	const int flags = fcntl(fds[0], F_GETFL, 0);
	if (flags >= 0) {
		fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
	}
	struct ad_renderer *r = calloc(1, sizeof(*r));
	if (!r) {
		close(fds[0]);
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		return NULL;
	}
	r->pid = pid;
	r->fd = fds[0];
	r->state = READING_HEADER;
	return r;
}

/* The pixels of the frame being read; NULL when its header is not sane. */
static bool begin_pixels(struct ad_renderer *r) {
	const uint32_t magic = read_u32(r->header);
	const int w = read_u16(r->header + 4);
	const int h = read_u16(r->header + 6);
	r->incoming_ticks = read_u32(r->header + 8);
	if (magic != FRAME_MAGIC || w < 1 || h < 1 || w > 4096 || h > 4096) {
		return false;
	}
	/* The helper's framebuffer is fixed for a session. A size change would
	 * leave the two swap buffers different lengths; refuse it and keep the
	 * last good frame. */
	if (r->frame.width && (r->frame.width != w || r->frame.height != h)) {
		return false;
	}
	const size_t need = (size_t)w * (size_t)h;
	/* The first frame's swap leaves `incoming` NULL while `incoming_len`
	 * still has its size; allocate the second buffer on the next frame. */
	if (need != r->incoming_len || !r->incoming) {
		uint8_t *grown = realloc(r->incoming, need);
		if (!grown) {
			return false;
		}
		r->incoming = grown;
		r->incoming_len = need;
	}
	r->incoming_w = w;
	r->incoming_h = h;
	return true;
}

static void finish_frame(struct ad_renderer *r) {
	uint8_t *pixels = r->pixels;
	r->pixels = r->incoming;
	r->incoming = pixels;
	r->frame.width = r->incoming_w;
	r->frame.height = r->incoming_h;
	r->frame.ticks = r->incoming_ticks;
	memcpy(r->shown_palette, r->palette, sizeof(r->shown_palette));
	r->frame.palette = r->shown_palette;
	r->frame.pixels = r->pixels;
	r->new_frame = true;
	r->state = READING_HEADER;
	r->got = 0;
}

bool ad_renderer_take_frame(struct ad_renderer *r) {
	if (!r || r->fd < 0 || r->failed) {
		return false;
	}
	r->new_frame = false;
	for (;;) {
		uint8_t *dst;
		size_t total;
		switch (r->state) {
		case READING_HEADER:
			dst = r->header;
			total = HEADER_SIZE;
			break;
		case READING_PALETTE:
			dst = r->palette;
			total = PALETTE_SIZE;
			break;
		default:
			dst = r->incoming;
			total = r->incoming_len;
			break;
		}
		const ssize_t n = read(r->fd, dst + r->got, total - r->got);
		if (n > 0) {
			r->got += (size_t)n;
			if (r->got < total) {
				continue;
			}
			r->got = 0;
			if (r->state == READING_HEADER) {
				if (!begin_pixels(r)) {
					r->failed = true;
					break;
				}
				r->state = READING_PALETTE;
			} else if (r->state == READING_PALETTE) {
				r->state = READING_PIXELS;
			} else {
				finish_frame(r);
				/* One whole frame per call: a helper that draws faster
				 * than the display can otherwise keep this read loop
				 * full forever and freeze the panel's event loop. */
				break;
			}
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			break;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		/* A closed pipe, or an interrupted one with nobody left to read:
		 * the helper has gone. Keep the last complete frame on screen. */
		r->failed = true;
		break;
	}
	return r->new_frame;
}

const struct ad_frame *ad_renderer_frame(const struct ad_renderer *r) {
	return r && r->frame.pixels ? &r->frame : NULL;
}

void ad_renderer_draw(const struct ad_renderer *r, struct pl_canvas *c, int x, int y, int w,
		int h) {
	if (!c || !c->px || !r || !r->frame.pixels || w <= 0 || h <= 0) {
		return;
	}
	/* The bars around a 4:3 module on a wider screen, and the first frames
	 * before the helper has drawn anything, are black. */
	for (int row = 0; row < h; row++) {
		if (y + row < 0 || y + row >= c->height) {
			continue;
		}
		uint32_t *out = c->px + (size_t)(y + row) * c->stride;
		for (int col = 0; col < w; col++) {
			if (x + col >= 0 && x + col < c->width) {
				out[x + col] = 0xFF000000u;
			}
		}
	}
	const int sw = r->frame.width, sh = r->frame.height;
	int dw = w, dh = w * sh / sw;
	if (dh > h) {
		dh = h;
		dw = h * sw / sh;
	}
	if (dw < 1 || dh < 1) {
		return;
	}
	const int ox = x + (w - dw) / 2, oy = y + (h - dh) / 2;
	for (int row = 0; row < dh; row++) {
		const int sy = row * sh / dh;
		if (oy + row < 0 || oy + row >= c->height) {
			continue;
		}
		const uint8_t *src = r->frame.pixels + (size_t)sy * sw;
		uint32_t *out = c->px + (size_t)(oy + row) * c->stride;
		for (int col = 0; col < dw; col++) {
			const int sx = col * sw / dw;
			if (ox + col < 0 || ox + col >= c->width) {
				continue;
			}
			const uint8_t *rgb = r->frame.palette + (size_t)src[sx] * 3;
			out[ox + col] = 0xFF000000u | (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 |
				rgb[2];
		}
	}
}

/* Reap a child without blocking the caller forever: a helper that ignores
 * SIGTERM must not freeze the panel's event loop. */
static bool reap(pid_t pid, int tries) {
	for (int i = 0; i < tries; i++) {
		const pid_t done = waitpid(pid, NULL, WNOHANG);
		if (done == pid || (done < 0 && errno == ECHILD)) {
			return true;
		}
		if (done < 0 && errno != EINTR) {
			return true;
		}
		const struct timespec wait = { 0, 10000000 };
		nanosleep(&wait, NULL);
	}
	return false;
}

void ad_renderer_stop(struct ad_renderer *r) {
	if (!r) {
		return;
	}
	if (r->pid > 0) {
		kill(r->pid, SIGTERM);
		if (!reap(r->pid, 20)) {
			kill(r->pid, SIGKILL);
			reap(r->pid, 20);
		}
	}
	if (r->fd >= 0) {
		close(r->fd);
	}
	free(r->incoming);
	free(r->pixels);
	free(r);
}
