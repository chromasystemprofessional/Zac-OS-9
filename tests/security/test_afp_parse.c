/* The AFP reply parsers, without a server or a socket.

   These are the functions that read bytes a Mac (or anything answering on
   port 548) sends: server info, item parameters and an enumerate reply.
   The deterministic loop at the end feeds them pseudo-random bytes so the
   sanitizer build sees short, long and inconsistent fields too. */
#include "afp_parse.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what) {
	if (!ok) {
		fprintf(stderr, "afp-parse: %s\n", what);
		failures++;
	}
}

static void put16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void test_server_info(void) {
	uint8_t data[128] = { 0 };
	put16(data, 48);        /* machine type offset */
	put16(data + 2, 24);    /* AFP versions offset */
	put16(data + 4, 64);    /* UAMs offset */
	put16(data + 8, 0x1234); /* flags */
	data[10] = 7;
	memcpy(data + 11, "Zac Mac", 7);

	data[24] = 2;
	data[25] = 6;
	memcpy(data + 26, "AFP2.2", 6);
	data[32] = 6;
	memcpy(data + 33, "AFP2.1", 6);

	data[48] = 9;
	memcpy(data + 49, "Macintosh", 9);

	data[64] = 2;
	data[65] = 11;
	memcpy(data + 66, "DHCAST128", 9);
	data[77] = 15;
	memcpy(data + 78, "No User Authent", 15);

	struct afp_server_info info;
	check(afp_parse_server_info(data, sizeof(data), &info),
		"a complete server-info reply parses");
	check(strcmp(info.name, "Zac Mac") == 0, "server name");
	check(strcmp(info.machine, "Macintosh") == 0, "machine type");
	check(info.flags == 0x1234, "flags");
	check(info.nversions == 2, "two AFP versions");
	check(strcmp(info.versions[0], "AFP2.2") == 0, "first AFP version");
	check(strcmp(info.versions[1], "AFP2.1") == 0, "second AFP version");
	check(info.nuams == 2, "two UAMs");
	check(strcmp(info.uams[0], "DHCAST128") == 0, "first UAM");
	check(strcmp(info.uams[1], "No User Authent") == 0, "second UAM");

	for (size_t len = 0; len < 11; len++) {
		memset(&info, 0xff, sizeof(info));
		check(!afp_parse_server_info(data, len, &info),
			"a short server-info reply is refused");
		check(info.name[0] == 0, "a refused server-info reply is zeroed");
	}
}

static void test_params(void) {
	uint8_t data[80] = { 0 };
	put16(data, 0x0021);       /* invisible and write-inhibited */
	put32(data + 2, 0);        /* created: the AFP epoch */
	put32(data + 6, 0x80000000u); /* modified: no date */
	put16(data + 10 + 8, 0x4000); /* Finder info: invisible */
	put16(data + 42, 60);      /* the long name is at offset 60 */
	put32(data + 44, 0x12345678);
	put32(data + 48, 0x1000);
	data[60] = 5;
	memcpy(data + 61, "Hello", 5);

	struct afp_entry e;
	afp_parse_params(data, sizeof(data), false, FILE_BITS, &e);
	check(!e.is_dir, "a file is not a folder");
	check(!e.writable, "a write-inhibited file is not writable");
	check(e.invisible, "the attributes and Finder info make it invisible");
	check(e.created == 946684800, "created date (2000-01-01 UTC)");
	check(e.modified == 0, "the no-date sentinel");
	check(e.id == 0x12345678, "file ID");
	check(e.size == 0x1000, "data fork length");
	check(strcmp(e.name, "Hello") == 0, "long name");

	memset(data, 0, sizeof(data));
	put16(data + 48, 5);       /* offspring count */
	data[50 + 3] = 0x04;       /* the user's own access: read/write */
	afp_parse_params(data, sizeof(data), true, DIR_BITS, &e);
	check(e.is_dir, "a folder is a folder");
	check(e.entries == 5, "offspring count");
	check(e.writable, "the user's own access is writable");
}

static void test_list(void) {
	uint8_t data[32] = { 0 };
	put16(data, 1 << 8);       /* files: the ID */
	put16(data + 2, 1 << 9);   /* folders: the offspring count */
	put16(data + 4, 2);
	data[6] = 6;               /* file entry: 2 + 4 bytes */
	data[7] = 0;
	put32(data + 8, 0x11111111);
	data[12] = 4;              /* folder entry: 2 + 2 bytes */
	data[13] = 0x80;
	put16(data + 14, 3);

	struct afp_entry e[8];
	int n = afp_parse_list_reply(data, sizeof(data), 8, e);
	check(n == 2, "two list entries");
	check(!e[0].is_dir && e[0].id == 0x11111111, "the file entry");
	check(e[1].is_dir && e[1].entries == 3, "the folder entry");

	n = afp_parse_list_reply(data, sizeof(data), 1, e);
	check(n == 1, "the caller's limit is respected");

	data[6] = 1;
	check(afp_parse_list_reply(data, sizeof(data), 8, e) == 0,
		"an entry shorter than its own header is refused");
	data[6] = 100;
	check(afp_parse_list_reply(data, sizeof(data), 8, e) == 0,
		"an entry longer than the reply is refused");
	check(afp_parse_list_reply(data, 5, 8, e) == 0,
		"a reply shorter than its fixed header is refused");
}

static void test_malformed_loop(void) {
	uint32_t state = 0x9e3779b9;
	uint8_t data[512] = { 0 };
	for (size_t len = 0; len <= sizeof(data); len++) {
		for (size_t i = 0; i < len; i++) {
			state = state * 1664525u + 1013904223u;
			data[i] = (uint8_t)(state >> 24);
		}
		struct afp_server_info info;
		afp_parse_server_info(data, len, &info);
		struct afp_entry entries[8];
		afp_parse_list_reply(data, len, 8, entries);
		if (len >= 3) {
			const bool is_dir = data[0] & 1;
			const uint16_t bitmap = (uint16_t)(data[1] << 8 | data[2]);
			afp_parse_params(data + 3, len - 3, is_dir, bitmap, &entries[0]);
		}
	}
}

static void test_null(void) {
	struct afp_server_info info;
	struct afp_entry e;
	check(!afp_parse_server_info(NULL, 20, &info),
		"a null server-info reply is refused");
	afp_parse_params(NULL, 20, false, FILE_BITS, &e);
	check(e.name[0] == 0, "a null parameter block is zeroed");
	check(afp_parse_list_reply(NULL, 20, 8, &e) == 0,
		"a null list reply is refused");
}

int main(void) {
	test_server_info();
	test_params();
	test_list();
	test_null();
	test_malformed_loop();
	if (failures) {
		fprintf(stderr, "afp-parse: %d failure(s)\n", failures);
		return 1;
	}
	printf("afp-parse: ok\n");
	return 0;
}
