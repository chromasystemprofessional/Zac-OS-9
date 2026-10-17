#pragma once

/*
 * AFP over AppleTalk, as Macs before Mac OS 9 serve it (File Sharing on
 * System 7 to Mac OS 8.6 speaks nothing else): ASP, the AppleTalk Session
 * Protocol, over ATP transactions, over DDP datagrams from the kernel's
 * AF_APPLETALK sockets (atalkd must be running to give this computer an
 * AppleTalk address). Servers are found by name with NBP. Inside AppleTalk,
 * 2nd edition: chapters 9 (ATP), 7 (NBP) and 11 (ASP).
 *
 * Nothing here needs root: a socket from the dynamic range (129-254) takes
 * the replies, and the server's requests to us (tickles, attentions,
 * Write Continue) arrive on the same one.
 */

#include <linux/atalk.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The most an ATP response carries: 8 packets of 578 bytes. */
#define ASP_QUANTUM (8 * 578)

struct asp {
	int fd;
	struct sockaddr_at sls; /* the server's listening socket */
	struct sockaddr_at sss; /* its session socket, once open */
	uint8_t wss;            /* ours */
	uint8_t session;
	uint16_t seq;           /* the next command's sequence number */
	int closed;             /* the server ended the session */
	time_t heard;           /* the last packet from the server */
};

struct nbp_entry {
	char name[100]; /* UTF-8 object name */
	struct sockaddr_at addr;
};

/* NBP lookup on this computer's network: =:TYPE@* (or NAME:TYPE@*),
 * listening for timeout_ms. *n gets how many (at most max). 0, or -errno
 * (-ENETDOWN without an AppleTalk address: atalkd isn't running). */
int nbp_lookup(const char *name, const char *type, struct nbp_entry *out, int max, int *n,
	int timeout_ms);

/* This computer's own AppleTalk address (port 0). 0, or -ENETDOWN. */
int appletalk_self(struct sockaddr_at *self);

/* "NAME" (an AFPServer by NBP name) or "NET.NODE:SOCKET" -> the server's
 * listening socket. 0, -ENOENT if no server answers to the name, or
 * -errno. */
int asp_resolve(const char *server, struct sockaddr_at *sls);

/* A socket for talking to the server at sls. 0 or -errno. */
int asp_open(struct asp *s, const struct sockaddr_at *sls);
/* SPGetStatus: the server's FPGetSrvrInfo block, malloc'd. 0, or -EIO
 * if it doesn't answer. */
int asp_get_status(struct asp *s, uint8_t **out, size_t *len);
/* SPOpenSession. 0, an ASP error (negative, -1066 "server busy" and so
 * on), or -EIO. */
int asp_open_session(struct asp *s);
/* SPCommand: an AFP request (at most 578 bytes) and its reply (at most
 * ASP_QUANTUM bytes, malloc'd into *out if wanted). The AFP result code,
 * or -EIO if the server stopped answering. */
int asp_command(struct asp *s, const void *req, size_t reqlen, uint8_t **out, size_t *outlen);
/* SPWrite: an AFP FPWrite, whose `data` the server then asks for with
 * Write Continue (as much of it as its buffer takes). Result as above;
 * the reply (FPWrite's last-written offset) goes to *out. */
int asp_write(struct asp *s, const void *req, size_t reqlen, const void *data, size_t datalen,
	uint8_t **out, size_t *outlen);
/* While idle: answers what the server sent (attentions, a close).
 * -EIO once the session is over, else 0. */
int asp_idle(struct asp *s);
/* SPTickle, as the session needs every 30 s. */
void asp_tickle(struct asp *s);
/* SPCloseSession (if open) and the socket. */
void asp_close(struct asp *s);
