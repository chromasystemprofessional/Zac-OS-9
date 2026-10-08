#pragma once

/*
 * DSI, the Data Stream Interface: AFP over TCP (Inside AppleTalk's AFP
 * over TCP chapter). Each request has a 16-byte header and a reply with
 * the same request ID. The server also sends tickles (ignored), and
 * attentions (acknowledged); a thread monitors idle messages and tickles
 * the server every 30 s so an idle session isn't dropped.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

enum dsi_command {
	DSI_CLOSE_SESSION = 1,
	DSI_COMMAND = 2,
	DSI_GET_STATUS = 3,
	DSI_OPEN_SESSION = 4,
	DSI_TICKLE = 5,
	DSI_WRITE = 6,
	DSI_ATTENTION = 8,
};

/* dsi_request's result when the connection fails (AFP codes are -5000ish). */
#define DSI_ERR_IO (-100000)

struct dsi {
	int fd;
	uint16_t next_id;
	pthread_mutex_t lock;
	pthread_t tickler;
	int tickling;
	int dead;
	void (*disconnected)(void *);
	void *disconnected_data;
	/* The most the server takes in one request (DSIOpenSession's reply). */
	uint32_t server_quantum;
};

/* Connects to host:port (port NULL for 548). 0, or -errno. */
int dsi_connect(struct dsi *d, const char *host, const char *port);
/* DSIOpenSession; learns the server's request quantum. 0 or an error. */
int dsi_open_session(struct dsi *d);
/* One request and its reply. For DSI_WRITE, `data` follows the AFP
 * parameters `req`. The reply's data is malloc'd into *out (may be NULL
 * if the caller doesn't want it). Returns the AFP result: 0, an AFP error
 * code, or DSI_ERR_IO. */
int dsi_request(struct dsi *d, enum dsi_command command, const void *req, size_t reqlen,
	const void *data, size_t datalen, uint8_t **out, size_t *outlen);
/* Starts the tickle thread (after any fork). */
void dsi_start_tickles(struct dsi *d);
/* Marks a terminal session failure, notifying once (under the DSI lock). */
void dsi_fail(struct dsi *d);
/* DSICloseSession and close. */
void dsi_close(struct dsi *d);
