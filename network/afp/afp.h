#pragma once

/*
 * AFP 2.x, as spoken by classic Macs: Mac OS 9's File Sharing (AFP 2.1
 * over TCP, Randnum logins), AppleShare IP and Netatalk (AFP 2.2, DHX).
 * Names are Mac Roman "long names" of up to 31 characters; dates count
 * seconds from 2000. Calls return 0 or a negative errno.
 */

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include "dsi.h"

#define AFP_NAME_MAX 31   /* long names */
#define AFP_ROOT_DIR 2    /* a volume's root folder */

struct afp_server_info {
	char name[256];         /* UTF-8 */
	char machine[64];
	char versions[8][32];
	int nversions;
	char uams[8][32];
	int nuams;
	uint16_t flags;
};

/* DSIGetStatus on a fresh connection: what the Chooser shows. */
int afp_server_info(const char *host, const char *port, struct afp_server_info *info);

struct afp {
	struct dsi dsi;
	char version[32]; /* the AFP version we logged in with */
	bool afp22;       /* 2.2 and later: 64-bit volume sizes */
	uint16_t vol;     /* the open volume */
};

/* Connects and logs in: as a guest if user is NULL, otherwise with the
 * best way both ends know (DHX, 2-Way Randnum, Randnum, clear text if
 * allowed). On failure, err gets a sentence for the person. */
int afp_connect(struct afp *a, const char *host, const char *port, const char *user,
	const char *password, bool allow_cleartext, char *err, size_t errlen);
void afp_disconnect(struct afp *a);

struct afp_volume {
	char name[AFP_NAME_MAX * 3 + 1]; /* UTF-8 */
	bool has_password;
};
/* The server's volumes; *n gets how many (at most max). */
int afp_volumes(struct afp *a, struct afp_volume *vols, int max, int *n);
int afp_open_volume(struct afp *a, const char *name);
int afp_volume_space(struct afp *a, uint64_t *free_bytes, uint64_t *total_bytes);

struct afp_entry {
	char name[AFP_NAME_MAX * 3 + 1]; /* UTF-8, with ':' for the Mac's '/' */
	bool is_dir;
	bool invisible; /* the Finder's "invisible" flag */
	uint32_t id;    /* directory ID, or file number */
	uint32_t size;  /* data fork */
	uint32_t entries; /* folders: how many items */
	time_t created, modified;
	bool writable;  /* we may change it */
};

/* An item named `name` in folder `dir` ("" for the folder itself). */
int afp_stat(struct afp *a, uint32_t dir, const char *name, struct afp_entry *e);
/* The items in a folder, `count` from index `start` (1-based); *n gets
 * how many came back (0 at the end). */
int afp_list(struct afp *a, uint32_t dir, int start, int count, struct afp_entry *e, int *n);

int afp_open_fork(struct afp *a, uint32_t dir, const char *name, bool write, uint16_t *fork);
int afp_close_fork(struct afp *a, uint16_t fork);
/* Bytes read (0 at the end). */
ssize_t afp_read(struct afp *a, uint16_t fork, uint32_t offset, void *buf, size_t size);
ssize_t afp_write(struct afp *a, uint16_t fork, uint32_t offset, const void *buf, size_t size);
int afp_set_fork_size(struct afp *a, uint16_t fork, uint32_t size);
int afp_flush_fork(struct afp *a, uint16_t fork);

int afp_create_file(struct afp *a, uint32_t dir, const char *name);
int afp_create_dir(struct afp *a, uint32_t dir, const char *name, uint32_t *new_id);
int afp_delete(struct afp *a, uint32_t dir, const char *name);
/* Moves and/or renames. */
int afp_move(struct afp *a, uint32_t from_dir, const char *from, uint32_t to_dir, const char *to);
int afp_set_modified(struct afp *a, uint32_t dir, const char *name, bool is_dir, time_t t);

/* UTF-8 <-> Mac Roman long names: '/' in a Mac name is ':' here, as macOS
 * shows it. False if the name can't be written in Mac Roman or is too
 * long. */
bool afp_name_to_mac(const char *utf8, uint8_t *mac, size_t *len);
void afp_name_from_mac(const uint8_t *mac, size_t len, char *utf8, size_t size);
