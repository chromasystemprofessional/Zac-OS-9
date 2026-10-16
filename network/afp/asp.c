#define _GNU_SOURCE
#include "asp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "afp.h"

/* Linux's DDP sockets leave the DDP type to us: the first byte of the data. */
#define DDP_NBP 2
#define DDP_ATP 3

#define NBP_SOCKET 2
#define NBP_LKUP 2
#define NBP_LKUP_REPLY 3

#define ATP_TREQ 0x40
#define ATP_TRESP 0x80
#define ATP_TREL 0xC0
#define ATP_FUNCTION 0xC0
#define ATP_XO 0x20
#define ATP_EOM 0x10
#define ATP_DATA 578
#define ATP_PACKETS 8

#define ASP_CLOSE 1
#define ASP_COMMAND 2
#define ASP_STATUS 3
#define ASP_OPEN 4
#define ASP_TICKLE 5
#define ASP_WRITE 6
#define ASP_WRITE_CONTINUE 7
#define ASP_ATTENTION 8

/* The server tickles every 30 s; four missed, and it's gone. */
#define ASP_SILENCE_SECONDS 120

static bool debug(void) {
	static int on = -1;
	if (on < 0) {
		on = getenv("ZACOS9_AFP_DEBUG") != NULL;
	}
	return on;
}

static long now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static bool same_node(const struct sockaddr_at *a, const struct sockaddr_at *b) {
	return a->sat_addr.s_net == b->sat_addr.s_net && a->sat_addr.s_node == b->sat_addr.s_node;
}

static bool same_socket(const struct sockaddr_at *a, const struct sockaddr_at *b) {
	return same_node(a, b) && a->sat_port == b->sat_port;
}

/* A dynamic socket on this computer's AppleTalk address. */
static int ddp_socket(uint8_t *port) {
	int fd = socket(AF_APPLETALK, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return -ENETDOWN; /* no appletalk module */
	}
	struct sockaddr_at me = {
		.sat_family = AF_APPLETALK,
		.sat_port = ATADDR_ANYPORT,
		.sat_addr = { htons(ATADDR_ANYNET), ATADDR_ANYNODE },
	};
	socklen_t len = sizeof(me);
	if (bind(fd, (struct sockaddr *)&me, sizeof(me)) < 0 ||
			getsockname(fd, (struct sockaddr *)&me, &len) < 0) {
		close(fd);
		return -ENETDOWN; /* no AppleTalk address: atalkd isn't running */
	}
	if (port) {
		*port = me.sat_port;
	}
	return fd;
}

/* One datagram within timeout_ms: its length, 0 on timeout, -1 on error. */
static int receive(int fd, uint8_t *buf, size_t size, struct sockaddr_at *from, int timeout_ms) {
	struct pollfd p = { .fd = fd, .events = POLLIN };
	int r = poll(&p, 1, timeout_ms < 0 ? 0 : timeout_ms);
	if (r < 0) {
		return errno == EINTR ? 0 : -1;
	}
	if (r == 0) {
		return 0;
	}
	socklen_t len = sizeof(*from);
	ssize_t n = recvfrom(fd, buf, size, 0, (struct sockaddr *)from, &len);
	return n < 0 ? (errno == EINTR || errno == EAGAIN ? 0 : -1) : (int)n;
}

/* ---- NBP -------------------------------------------------------------------- */

static size_t put_entity(uint8_t *p, const char *s) {
	const size_t n = strlen(s);
	p[0] = (uint8_t)n;
	memcpy(p + 1, s, n);
	return n + 1;
}

int nbp_lookup(const char *name, const char *type, struct nbp_entry *out, int max, int *n,
		int timeout_ms) {
	*n = 0;
	uint8_t me_port;
	int fd = ddp_socket(&me_port);
	if (fd < 0) {
		return fd;
	}
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	struct sockaddr_at me;
	socklen_t mlen = sizeof(me);
	getsockname(fd, (struct sockaddr *)&me, &mlen);

	/* Names are Mac Roman, at most 32 characters. */
	uint8_t object[40] = "=";
	size_t object_len = 1;
	if (name && strcmp(name, "=") != 0 &&
			(!afp_name_to_mac(name, object, &object_len) || object_len > 32)) {
		close(fd);
		return -ENOENT;
	}
	uint8_t q[128];
	size_t len = 0;
	q[len++] = DDP_NBP;
	q[len++] = NBP_LKUP << 4 | 1; /* one tuple */
	q[len++] = (uint8_t)(getpid() & 0xff);
	const uint16_t net = ntohs(me.sat_addr.s_net);
	q[len++] = net >> 8;
	q[len++] = net & 0xff;
	q[len++] = me.sat_addr.s_node;
	q[len++] = me.sat_port;
	q[len++] = 0; /* enumerator */
	q[len++] = (uint8_t)object_len;
	memcpy(q + len, object, object_len);
	len += object_len;
	len += put_entity(q + len, type);
	len += put_entity(q + len, "*");
	/* No router here (atalkd's startup range): a LkUp broadcast on this
	 * network reaches every node. TODO: BrRq through a router, and zones. */
	const struct sockaddr_at everyone = {
		.sat_family = AF_APPLETALK,
		.sat_port = NBP_SOCKET,
		.sat_addr = { htons(ATADDR_ANYNET), ATADDR_BCAST },
	};
	const long end = now_ms() + timeout_ms;
	long next_send = 0;
	int sends = 0;
	while (now_ms() < end) {
		if (sends < 3 && now_ms() >= next_send) {
			if (sendto(fd, q, len, 0, (const struct sockaddr *)&everyone, sizeof(everyone)) < 0) {
				const int e = errno;
				close(fd);
				return -(e == ENETUNREACH ? ENETDOWN : e);
			}
			sends++;
			next_send = now_ms() + 500;
		}
		uint8_t r[600];
		struct sockaddr_at from;
		const long until = sends < 3 ? next_send : end;
		int got = receive(fd, r, sizeof(r), &from, (int)(until - now_ms()));
		if (got < 0) {
			break;
		}
		if (got < 3 || r[0] != DDP_NBP || r[1] >> 4 != NBP_LKUP_REPLY || r[2] != q[2]) {
			continue;
		}
		size_t k = 3;
		for (int t = 0; t < (r[1] & 15) && k + 5 <= (size_t)got; t++) {
			struct nbp_entry e = { .addr = { .sat_family = AF_APPLETALK } };
			e.addr.sat_addr.s_net = htons((uint16_t)(r[k] << 8 | r[k + 1]));
			e.addr.sat_addr.s_node = r[k + 2];
			e.addr.sat_port = r[k + 3];
			k += 5;
			uint8_t field[3][33];
			size_t field_len[3];
			bool ok = true;
			for (int f = 0; f < 3; f++) {
				if (k >= (size_t)got || r[k] > 32 || k + 1 + r[k] > (size_t)got) {
					ok = false;
					break;
				}
				field_len[f] = r[k];
				memcpy(field[f], r + k + 1, r[k]);
				k += 1 + r[k];
			}
			if (!ok) {
				break;
			}
			afp_name_from_mac(field[0], field_len[0], e.name, sizeof(e.name));
			bool seen = false;
			for (int i = 0; i < *n; i++) {
				seen |= same_socket(&out[i].addr, &e.addr) && strcmp(out[i].name, e.name) == 0;
			}
			if (!seen && *n < max) {
				out[(*n)++] = e;
			}
		}
		/* Looking for one server by name: the first answer will do. */
		if (name && strcmp(name, "=") != 0 && *n > 0) {
			break;
		}
	}
	close(fd);
	return 0;
}

int asp_resolve(const char *server, struct sockaddr_at *sls) {
	unsigned net, node, port;
	char extra;
	memset(sls, 0, sizeof(*sls));
	sls->sat_family = AF_APPLETALK;
	if (sscanf(server, "%u.%u:%u%c", &net, &node, &port, &extra) == 3 && net <= 0xffff &&
			node > 0 && node < 255 && port > 0 && port < 255) {
		sls->sat_addr.s_net = htons((uint16_t)net);
		sls->sat_addr.s_node = (uint8_t)node;
		sls->sat_port = (uint8_t)port;
		return 0;
	}
	struct nbp_entry e[4];
	int n;
	int r = nbp_lookup(server, "AFPServer", e, 4, &n, 2000);
	if (r < 0) {
		return r;
	}
	for (int i = 0; i < n; i++) {
		if (strcasecmp(e[i].name, server) == 0) {
			*sls = e[i].addr;
			return 0;
		}
	}
	return -ENOENT;
}

/* ---- ATP ---------------------------------------------------------------------- */

static int send_atp(struct asp *s, const struct sockaddr_at *to, uint8_t control, uint8_t bitmap,
		uint16_t tid, const uint8_t user[4], const void *data, size_t len) {
	uint8_t p[1 + 8 + ATP_DATA];
	if (len > ATP_DATA) {
		return -EMSGSIZE;
	}
	p[0] = DDP_ATP;
	p[1] = control;
	p[2] = bitmap;
	p[3] = tid >> 8;
	p[4] = tid & 0xff;
	memcpy(p + 5, user, 4);
	if (len) {
		memcpy(p + 9, data, len);
	}
	if (sendto(s->fd, p, 9 + len, 0, (const struct sockaddr *)to, sizeof(*to)) < 0) {
		return -errno;
	}
	return 0;
}

/* One counter for every socket: a server keeps a transaction's
 * responses for its TID a while (XO), so a new socket mustn't reuse one. */
static uint16_t next_tid;

/* What Write Continue hands the server: the data of the write in progress. */
struct write_data {
	uint16_t seq;
	const uint8_t *data;
	size_t len;
	size_t sent; /* how much the server took */
};

/* A request from the server (to our WSS): tickles need nothing, the rest
 * a response. */
static void server_request(struct asp *s, const uint8_t *p, size_t len,
		const struct sockaddr_at *from, struct write_data *w) {
	if (len < 8 || (p[0] & ATP_FUNCTION) != ATP_TREQ) {
		return;
	}
	const uint8_t bitmap = p[1];
	const uint16_t tid = (uint16_t)(p[2] << 8 | p[3]);
	const uint8_t *user = p + 4;
	static const uint8_t none[4] = { 0 };
	if (debug()) {
		fprintf(stderr, "asp: server request %d (tid %u)\n", user[0], tid);
	}
	switch (user[0]) {
	case ASP_TICKLE:
		break;
	case ASP_ATTENTION:
		send_atp(s, from, ATP_TRESP | ATP_EOM, 0, tid, none, NULL, 0);
		break;
	case ASP_CLOSE:
		send_atp(s, from, ATP_TRESP | ATP_EOM, 0, tid, none, NULL, 0);
		s->closed = 1;
		break;
	case ASP_WRITE_CONTINUE: {
		const uint16_t seq = (uint16_t)(user[2] << 8 | user[3]);
		if (!w || user[1] != s->session || seq != w->seq) {
			break;
		}
		/* As much as its buffer takes (the request's two data bytes), in
		 * the packets it asked for. A repeated request (our response was
		 * lost) gets the same again. */
		size_t room = len >= 10 ? (size_t)(p[8] << 8 | p[9]) : ASP_QUANTUM;
		size_t n = w->len < room ? w->len : room;
		if (n > ASP_QUANTUM) {
			n = ASP_QUANTUM;
		}
		const int packets = n ? (int)((n + ATP_DATA - 1) / ATP_DATA) : 1;
		for (int i = 0; i < packets; i++) {
			if (!(bitmap & (1 << i))) {
				continue;
			}
			const size_t off = (size_t)i * ATP_DATA;
			const size_t part = n - off < ATP_DATA ? n - off : ATP_DATA;
			send_atp(s, from, ATP_TRESP | (i == packets - 1 ? ATP_EOM : 0), (uint8_t)i, tid, none,
				w->data + off, n ? part : 0);
		}
		w->sent = n;
		break;
	}
	}
}

/* Any packet from the server's computer shows it's still there. */
static void heard(struct asp *s, const struct sockaddr_at *from) {
	if (same_node(from, &s->sls)) {
		s->heard = time(NULL);
	}
}

/* One ATP transaction: a request to `to`, and its response packets (up
 * to 8, each up to 578 bytes; `packets` is how many to ask for), retried
 * every interval_ms, `tries` times. The response ends at its EOM packet,
 * or with all that were asked for: Mac OS answers a one-packet request
 * (SPOpenSession) without EOM. XO (exactly once) for anything that changes the server: it then
 * keeps its responses until our release. The first packet's user bytes go
 * to `result`, the data, in order, to *out. */
static int transact(struct asp *s, const struct sockaddr_at *to, bool xo, int packets,
		const uint8_t user[4], const void *data, size_t len, int tries, int interval_ms, struct write_data *w,
		uint8_t result[4], uint8_t **out, size_t *outlen) {
	if (out) {
		*out = NULL;
	}
	if (outlen) {
		*outlen = 0;
	}
	const uint16_t tid = next_tid++;
	uint8_t got[ATP_PACKETS][ATP_DATA];
	size_t got_len[ATP_PACKETS] = { 0 };
	uint8_t have = 0;     /* response packets received */
	uint8_t wanted = (uint8_t)((1u << packets) - 1); /* EOM narrows it */
	uint8_t first_user[4] = { 0 };
	for (int attempt = 0; attempt < tries && !s->closed; attempt++) {
		int r = send_atp(s, to, ATP_TREQ | (xo ? ATP_XO : 0), (uint8_t)(wanted & ~have), tid,
			user, data, len);
		if (r < 0) {
			return r;
		}
		const long end = now_ms() + interval_ms;
		while (!s->closed && (have & wanted) != wanted) {
			const long left = end - now_ms();
			if (left <= 0) {
				break;
			}
			uint8_t p[1 + 8 + ATP_DATA + 16];
			struct sockaddr_at from;
			int n = receive(s->fd, p, sizeof(p), &from, (int)left);
			if (n < 0) {
				return -EIO;
			}
			if (debug()) {
				fprintf(stderr, "asp: got %d bytes from %u.%u:%u type %u ctl %02x seq %u tid %u (waiting for %u)\n",
					n, ntohs(from.sat_addr.s_net), from.sat_addr.s_node, from.sat_port, p[0],
					n > 1 ? p[1] : 0, n > 2 ? p[2] : 0, n > 4 ? (unsigned)(p[3] << 8 | p[4]) : 0, tid);
			}
			if (n < 9 || p[0] != DDP_ATP) {
				continue;
			}
			heard(s, &from);
			const uint8_t *atp = p + 1;
			if ((atp[0] & ATP_FUNCTION) == ATP_TREQ) {
				server_request(s, atp, (size_t)n - 1, &from, w);
				continue;
			}
			if ((atp[0] & ATP_FUNCTION) != ATP_TRESP || !same_socket(&from, to) ||
					(uint16_t)(atp[2] << 8 | atp[3]) != tid || atp[1] >= ATP_PACKETS) {
				continue; /* not ours, or late */
			}
			const int seq = atp[1];
			if (!(have & (1 << seq))) {
				have |= (uint8_t)(1 << seq);
				got_len[seq] = (size_t)n - 9;
				memcpy(got[seq], p + 9, got_len[seq]);
				if (seq == 0) {
					memcpy(first_user, atp + 4, 4);
				}
			}
			if (atp[0] & ATP_EOM) {
				wanted = (uint8_t)((1 << (seq + 1)) - 1);
			}
		}
		if ((have & wanted) == wanted) {
			if (xo) {
				static const uint8_t none[4] = { 0 };
				send_atp(s, to, ATP_TREL, 0, tid, none, NULL, 0);
			}
			size_t total = 0;
			for (int i = 0; i < ATP_PACKETS && (wanted & (1 << i)); i++) {
				total += got_len[i];
			}
			if (out) {
				*out = malloc(total ? total : 1);
				if (!*out) {
					return -ENOMEM;
				}
				size_t at = 0;
				for (int i = 0; i < ATP_PACKETS && (wanted & (1 << i)); i++) {
					memcpy(*out + at, got[i], got_len[i]);
					at += got_len[i];
				}
			}
			if (outlen) {
				*outlen = total;
			}
			memcpy(result, first_user, 4);
			return 0;
		}
		if (debug()) {
			fprintf(stderr, "asp: no reply to tid %u yet (try %d of %d)\n", tid, attempt + 1, tries);
		}
	}
	return s->closed ? -ECONNRESET : -EIO;
}

/* ---- ASP -------------------------------------------------------------------- */

int asp_open(struct asp *s, const struct sockaddr_at *sls) {
	memset(s, 0, sizeof(*s));
	s->sls = *sls;
	if (!next_tid) {
		next_tid = (uint16_t)(getpid() * 31 + time(NULL));
	}
	s->heard = time(NULL);
	s->fd = ddp_socket(&s->wss);
	return s->fd < 0 ? s->fd : 0;
}

int asp_get_status(struct asp *s, uint8_t **out, size_t *len) {
	const uint8_t user[4] = { ASP_STATUS, 0, 0, 0 };
	uint8_t result[4];
	return transact(s, &s->sls, false, 8, user, NULL, 0, 4, 1000, NULL, result, out, len) < 0 ? -EIO
		: 0;
}

int asp_open_session(struct asp *s) {
	const uint8_t user[4] = { ASP_OPEN, s->wss, 0x01, 0x00 }; /* ASP version 1.0 */
	uint8_t result[4];
	if (transact(s, &s->sls, true, 1, user, NULL, 0, 4, 2000, NULL, result, NULL, NULL) < 0) {
		return -EIO;
	}
	const int16_t err = (int16_t)(result[2] << 8 | result[3]);
	if (err) {
		return err;
	}
	s->sss = s->sls;
	s->sss.sat_port = result[0];
	s->session = result[1];
	s->seq = 0;
	return 0;
}

static int32_t get_result(const uint8_t r[4]) {
	return (int32_t)((uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3]);
}

/* Commands are retried for half a minute: a busy old Mac can be slow. */
#define COMMAND_TRIES 15
#define COMMAND_INTERVAL 2000

int asp_command(struct asp *s, const void *req, size_t reqlen, uint8_t **out, size_t *outlen) {
	if (reqlen > ATP_DATA) {
		return -EMSGSIZE;
	}
	const uint16_t seq = s->seq++;
	const uint8_t user[4] = { ASP_COMMAND, s->session, seq >> 8, seq & 0xff };
	uint8_t result[4];
	int r = transact(s, &s->sss, true, 8, user, req, reqlen, COMMAND_TRIES, COMMAND_INTERVAL, NULL,
		result, out, outlen);
	return r < 0 ? -EIO : get_result(result);
}

int asp_write(struct asp *s, const void *req, size_t reqlen, const void *data, size_t datalen,
		uint8_t **out, size_t *outlen) {
	if (reqlen > ATP_DATA) {
		return -EMSGSIZE;
	}
	const uint16_t seq = s->seq++;
	const uint8_t user[4] = { ASP_WRITE, s->session, seq >> 8, seq & 0xff };
	struct write_data w = { .seq = seq, .data = data, .len = datalen };
	uint8_t result[4];
	int r = transact(s, &s->sss, true, 8, user, req, reqlen, COMMAND_TRIES, COMMAND_INTERVAL, &w,
		result, out, outlen);
	return r < 0 ? -EIO : get_result(result);
}

int asp_idle(struct asp *s) {
	for (;;) {
		uint8_t p[1 + 8 + ATP_DATA + 16];
		struct sockaddr_at from;
		int n = receive(s->fd, p, sizeof(p), &from, 0);
		if (n < 0) {
			return -EIO;
		}
		if (n == 0) {
			break;
		}
		if (n >= 9 && p[0] == DDP_ATP) {
			heard(s, &from);
			server_request(s, p + 1, (size_t)n - 1, &from, NULL);
		}
	}
	if (s->closed || time(NULL) - s->heard > ASP_SILENCE_SECONDS) {
		return -EIO;
	}
	return 0;
}

void asp_tickle(struct asp *s) {
	/* To the listening socket, with nothing to wait for. */
	const uint8_t user[4] = { ASP_TICKLE, s->session, 0, 0 };
	send_atp(s, &s->sls, ATP_TREQ, 0, next_tid++, user, NULL, 0);
}

void asp_close(struct asp *s) {
	if (s->fd < 0) {
		return;
	}
	if (s->sss.sat_family && !s->closed) {
		const uint8_t user[4] = { ASP_CLOSE, s->session, 0, 0 };
		uint8_t result[4];
		transact(s, &s->sss, true, 1, user, NULL, 0, 1, 1000, NULL, result, NULL, NULL);
	}
	close(s->fd);
	s->fd = -1;
}
