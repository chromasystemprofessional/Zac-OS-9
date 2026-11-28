#pragma once

/*
 * The AFP replies that are parsed rather than acted on: a server's status,
 * an item's parameters, and an FPEnumerate reply. Kept pure (bytes in,
 * structs out) so the fuzzer and the unit test can exercise them without a
 * server, a socket or gcrypt.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "afp.h"

/* File and folder parameter bitmaps. */
enum {
	BIT_ATTR = 1 << 0,
	BIT_PARENT = 1 << 1,
	BIT_CREATED = 1 << 2,
	BIT_MODIFIED = 1 << 3,
	BIT_BACKUP = 1 << 4,
	BIT_FINDER_INFO = 1 << 5,
	BIT_LONG_NAME = 1 << 6,
	BIT_SHORT_NAME = 1 << 7,
	BIT_ID = 1 << 8,          /* file number, or directory ID */
	BIT_DATA_LEN = 1 << 9,    /* files */
	BIT_RSRC_LEN = 1 << 10,   /* files */
	BIT_OFFSPRING = 1 << 9,   /* folders */
	BIT_OWNER = 1 << 10,      /* folders */
	BIT_GROUP = 1 << 11,      /* folders */
	BIT_ACCESS = 1 << 12,     /* folders */
};
#define FILE_BITS (BIT_ATTR | BIT_CREATED | BIT_MODIFIED | BIT_FINDER_INFO | BIT_LONG_NAME | BIT_ID | BIT_DATA_LEN)
#define DIR_BITS (BIT_ATTR | BIT_CREATED | BIT_MODIFIED | BIT_FINDER_INFO | BIT_LONG_NAME | BIT_ID | BIT_OFFSPRING | BIT_ACCESS)

/* FPGetSrvrInfo's reply. False if it is shorter than the fixed part;
 * `info` is zeroed either way. */
bool afp_parse_server_info(const uint8_t *data, size_t len,
	struct afp_server_info *info);

/* One item's parameters, laid out in bitmap order; names are offsets
 * from the start of the parameters. */
void afp_parse_params(const uint8_t *p, size_t len, bool is_dir,
	uint16_t bitmap, struct afp_entry *e);

/* FPEnumerate's reply: the file and folder bitmaps, the entry count, then
 * length-prefixed entries. Returns how many entries fit in `e` (at most
 * `max`), or 0 for a reply that is too short or malformed. */
int afp_parse_list_reply(const uint8_t *data, size_t len, int max,
	struct afp_entry *e);
