#define _GNU_SOURCE
#include "dsi.h"

#include <errno.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define DSI_HEADER 16
#define TICKLE_SECONDS 30

struct header {
	uint8_t flags; /* 0 request, 1 reply */
	uint8_t command;
	uint16_t id;
	int32_t code; /* error code (replies) or write offset (DSIWrite) */
	uint32_t length;
};

static void put16(uint8_t *p, uint16_t v) {
	p[0] = v >> 8;
	p[1] = v;
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Sends the pieces as one message: in one go, so they leave in as few
 * TCP segments as possible. Mac OS 9's server misreads a request whose
 * header and parameters arrive in separate segments. */
static int write_all(int fd, struct iovec *iov, int n) {
	while (n > 0) {
		struct msghdr msg = { .msg_iov = iov, .msg_iovlen = (size_t)n };
		ssize_t w = sendmsg(fd, &msg, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			return -1;
		}
		while (n > 0 && (size_t)w >= iov->iov_len) {
			w -= (ssize_t)iov->iov_len;
			iov++;
			n--;
		}
		if (n > 0) {
			iov->iov_base = (uint8_t *)iov->iov_base + w;
			iov->iov_len -= (size_t)w;
		}
	}
	return 0;
}

static int read_all(int fd, void *buf, size_t n) {
	uint8_t *p = buf;
	while (n > 0) {
		ssize_t r = recv(fd, p, n, 0);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			return -1;
		}
		p += r;
		n -= (size_t)r;
	}
	return 0;
}

static int send_header(struct dsi *d, const struct header *h, const void *a, size_t alen,
		const void *b, size_t blen) {
	uint8_t raw[DSI_HEADER];
	raw[0] = h->flags;
	raw[1] = h->command;
	put16(raw + 2, h->id);
	put32(raw + 4, (uint32_t)h->code);
	put32(raw + 8, h->length);
	put32(raw + 12, 0);
	struct iovec iov[3] = {
		{ raw, sizeof(raw) },
		{ (void *)a, alen },
		{ (void *)b, blen },
	};
	if (write_all(d->fd, iov, blen ? 3 : alen ? 2 : 1) < 0) {
		d->dead = 1;
		return -1;
	}
	return 0;
}

int dsi_connect(struct dsi *d, const char *host, const char *port) {
	memset(d, 0, sizeof(*d));
	d->fd = -1;
	d->server_quantum = 128 * 1024;
	pthread_mutex_init(&d->lock, NULL);
	struct addrinfo hints = { .ai_socktype = SOCK_STREAM, .ai_family = AF_UNSPEC }, *res;
	int err = getaddrinfo(host, port && *port ? port : "548", &hints, &res);
	if (err) {
		return err == EAI_SYSTEM ? -errno : -EHOSTUNREACH;
	}
	int last = -ECONNREFUSED;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
		if (fd < 0) {
			last = -errno;
			continue;
		}
		/* Don't wait forever on a computer that isn't there, or stops
		 * answering. */
		struct timeval tv = { .tv_sec = 15 };
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		struct timeval rtv = { .tv_sec = 90 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
			int one = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
			d->fd = fd;
			break;
		}
		last = -errno;
		close(fd);
	}
	freeaddrinfo(res);
	return d->fd >= 0 ? 0 : last;
}

int dsi_request(struct dsi *d, enum dsi_command command, const void *req, size_t reqlen,
		const void *data, size_t datalen, uint8_t **out, size_t *outlen) {
	if (out) {
		*out = NULL;
	}
	if (outlen) {
		*outlen = 0;
	}
	pthread_mutex_lock(&d->lock);
	if (d->dead) {
		pthread_mutex_unlock(&d->lock);
		return DSI_ERR_IO;
	}
	struct header h = {
		.flags = 0,
		.command = command,
		.id = d->next_id++,
		.code = command == DSI_WRITE ? (int32_t)reqlen : 0,
		.length = (uint32_t)(reqlen + datalen),
	};
	if (send_header(d, &h, req, reqlen, data, datalen) < 0) {
		pthread_mutex_unlock(&d->lock);
		return DSI_ERR_IO;
	}
	for (;;) {
		uint8_t raw[DSI_HEADER];
		if (read_all(d->fd, raw, sizeof(raw)) < 0) {
			break;
		}
		struct header r = {
			.flags = raw[0],
			.command = raw[1],
			.id = (uint16_t)(raw[2] << 8 | raw[3]),
			.code = (int32_t)get32(raw + 4),
			.length = get32(raw + 8),
		};
		uint8_t *body = NULL;
		if (r.length) {
			if (r.length > 64u * 1024 * 1024 || !(body = malloc(r.length))) {
				break;
			}
			if (read_all(d->fd, body, r.length) < 0) {
				free(body);
				break;
			}
		}
		if (getenv("PLATINUM_AFP_DEBUG")) {
			fprintf(stderr, "dsi: got flags=%d cmd=%d id=%u code=%d len=%u (waiting for id %u)\n",
				r.flags, r.command, r.id, r.code, r.length, h.id);
		}
		if (r.flags == 0) {
			/* From the server: tickles need nothing; attentions (such as
			 * "shutting down") are acknowledged; a close ends us. */
			if (r.command == DSI_ATTENTION) {
				struct header ack = { .flags = 1, .command = DSI_ATTENTION, .id = r.id };
				send_header(d, &ack, NULL, 0, NULL, 0);
			} else if (r.command == DSI_CLOSE_SESSION) {
				free(body);
				break;
			}
			free(body);
			continue;
		}
		if (r.id != h.id) {
			free(body); /* a stale reply */
			continue;
		}
		if (out) {
			*out = body;
		} else {
			free(body);
		}
		if (outlen) {
			*outlen = r.length;
		}
		pthread_mutex_unlock(&d->lock);
		return r.code;
	}
	d->dead = 1;
	pthread_mutex_unlock(&d->lock);
	return DSI_ERR_IO;
}

int dsi_open_session(struct dsi *d) {
	/* Option 1: our attention quantum. */
	uint8_t opt[6] = { 0x01, 4 };
	put32(opt + 2, 1024);
	uint8_t *reply;
	size_t len;
	int code = dsi_request(d, DSI_OPEN_SESSION, opt, sizeof(opt), NULL, 0, &reply, &len);
	if (code == 0) {
		/* Option 0: the server's request quantum. */
		for (size_t i = 0; i + 2 <= len && i + 2 + reply[i + 1] <= len; i += 2 + reply[i + 1]) {
			if (reply[i] == 0x00 && reply[i + 1] == 4) {
				uint32_t q = get32(reply + i + 2);
				if (q >= 1024) {
					d->server_quantum = q;
				}
			}
		}
	}
	free(reply);
	return code;
}

static void *tickle(void *arg) {
	struct dsi *d = arg;
	for (;;) {
		struct timespec ts = { .tv_sec = TICKLE_SECONDS };
		while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
		}
		pthread_mutex_lock(&d->lock);
		if (d->dead) {
			pthread_mutex_unlock(&d->lock);
			return NULL;
		}
		struct header h = { .command = DSI_TICKLE, .id = d->next_id++ };
		send_header(d, &h, NULL, 0, NULL, 0);
		pthread_mutex_unlock(&d->lock);
	}
}

void dsi_start_tickles(struct dsi *d) {
	if (!d->tickling && pthread_create(&d->tickler, NULL, tickle, d) == 0) {
		pthread_detach(d->tickler);
		d->tickling = 1;
	}
}

void dsi_close(struct dsi *d) {
	if (d->fd < 0) {
		return;
	}
	pthread_mutex_lock(&d->lock);
	if (!d->dead) {
		struct header h = { .command = DSI_CLOSE_SESSION, .id = d->next_id++ };
		send_header(d, &h, NULL, 0, NULL, 0);
	}
	d->dead = 1;
	close(d->fd);
	d->fd = -1;
	pthread_mutex_unlock(&d->lock);
}
