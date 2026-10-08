#define _GNU_SOURCE
#include "afp.h"

#include <errno.h>
#include <gcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* AFP commands. */
enum {
	FP_CLOSE_VOL = 2,
	FP_CLOSE_FORK = 4,
	FP_CREATE_DIR = 6,
	FP_CREATE_FILE = 7,
	FP_DELETE = 8,
	FP_ENUMERATE = 9,
	FP_FLUSH_FORK = 11,
	FP_GET_SRVR_PARMS = 16,
	FP_GET_VOL_PARMS = 17,
	FP_LOGIN = 18,
	FP_LOGIN_CONT = 19,
	FP_LOGOUT = 20,
	FP_MOVE_AND_RENAME = 23,
	FP_OPEN_VOL = 24,
	FP_OPEN_FORK = 26,
	FP_READ = 27,
	FP_RENAME = 28,
	FP_SET_FORK_PARMS = 31,
	FP_WRITE = 33,
	FP_GET_FILE_DIR_PARMS = 34,
	FP_SET_FILE_DIR_PARMS = 35,
};

/* AFP result codes. */
enum {
	AFP_ACCESS_DENIED = -5000,
	AFP_AUTH_CONTINUE = -5001,
	AFP_BAD_UAM = -5002,
	AFP_BAD_VERSION = -5003,
	AFP_BITMAP_ERR = -5004,
	AFP_CANT_MOVE = -5005,
	AFP_DENY_CONFLICT = -5006,
	AFP_DIR_NOT_EMPTY = -5007,
	AFP_DISK_FULL = -5008,
	AFP_EOF = -5009,
	AFP_FILE_BUSY = -5010,
	AFP_ITEM_NOT_FOUND = -5012,
	AFP_LOCK_ERR = -5013,
	AFP_MISC_ERR = -5014,
	AFP_OBJECT_EXISTS = -5017,
	AFP_OBJECT_NOT_FOUND = -5018,
	AFP_PARAM_ERR = -5019,
	AFP_SESSION_CLOSED = -5022,
	AFP_USER_NOT_AUTH = -5023,
	AFP_CALL_NOT_SUPPORTED = -5024,
	AFP_OBJECT_TYPE_ERR = -5025,
	AFP_TOO_MANY_FILES = -5026,
	AFP_SERVER_GOING_DOWN = -5027,
	AFP_CANT_RENAME = -5028,
	AFP_DIR_NOT_FOUND = -5029,
	AFP_VOL_LOCKED = -5031,
	AFP_OBJECT_LOCKED = -5032,
	AFP_PWD_EXPIRED = -5042,
};

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

/* Volume bitmap. */
enum {
	VOL_ATTR = 1 << 0,
	VOL_ID = 1 << 5,
	VOL_BYTES_FREE = 1 << 6,
	VOL_BYTES_TOTAL = 1 << 7,
	VOL_EXT_BYTES_FREE = 1 << 9,  /* AFP 2.2 */
	VOL_EXT_BYTES_TOTAL = 1 << 10,
};

#define PATH_LONG_NAMES 2
#define AFP_EPOCH 946684800 /* 2000-01-01 00:00:00 UTC */

/* ---- bytes ---------------------------------------------------------------- */

struct buf {
	uint8_t data[1024];
	size_t len;
};

static void put8(struct buf *b, uint8_t v) {
	if (b->len < sizeof(b->data)) {
		b->data[b->len++] = v;
	}
}

static void put16(struct buf *b, uint16_t v) {
	put8(b, v >> 8);
	put8(b, v);
}

static void put32(struct buf *b, uint32_t v) {
	put16(b, v >> 16);
	put16(b, v);
}

static void put_bytes(struct buf *b, const void *p, size_t n) {
	if (b->len + n <= sizeof(b->data)) {
		memcpy(b->data + b->len, p, n);
		b->len += n;
	}
}

static void put_pstring(struct buf *b, const char *s) {
	size_t n = strlen(s);
	put8(b, (uint8_t)n);
	put_bytes(b, s, n);
}

static void pad_even(struct buf *b) {
	if (b->len & 1) {
		put8(b, 0);
	}
}

static uint16_t get16(const uint8_t *p) {
	return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- names ---------------------------------------------------------------- */

/* Mac Roman 0x80..0xFF as Unicode (0xDB is the euro sign since Mac OS 8.5). */
static const uint16_t mac_roman[128] = {
	0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3,
	0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
	0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3,
	0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
	0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
	0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
	0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
	0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
	0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
	0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
	0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

static size_t utf8_put(char *out, uint32_t c) {
	if (c < 0x80) {
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = (char)(0xC0 | c >> 6);
		out[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	out[0] = (char)(0xE0 | c >> 12);
	out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
	out[2] = (char)(0x80 | (c & 0x3F));
	return 3;
}

void afp_name_from_mac(const uint8_t *mac, size_t len, char *utf8, size_t size) {
	size_t o = 0;
	for (size_t i = 0; i < len && o + 4 < size; i++) {
		uint32_t c = mac[i] < 0x80 ? mac[i] : mac_roman[mac[i] - 0x80];
		if (c == '/') {
			c = ':'; /* as macOS shows a Mac name's slash */
		}
		o += utf8_put(utf8 + o, c);
	}
	utf8[o] = 0;
}

bool afp_name_to_mac(const char *utf8, uint8_t *mac, size_t *len) {
	const uint8_t *s = (const uint8_t *)utf8;
	size_t n = 0;
	while (*s) {
		uint32_t c;
		int extra;
		if (*s < 0x80) {
			c = *s;
			extra = 0;
		} else if ((*s & 0xE0) == 0xC0) {
			c = *s & 0x1F;
			extra = 1;
		} else if ((*s & 0xF0) == 0xE0) {
			c = *s & 0x0F;
			extra = 2;
		} else {
			return false; /* beyond the Basic Multilingual Plane */
		}
		s++;
		for (int i = 0; i < extra; i++, s++) {
			if ((*s & 0xC0) != 0x80) {
				return false;
			}
			c = c << 6 | (*s & 0x3F);
		}
		uint8_t m;
		if (c == ':') {
			m = '/';
		} else if (c < 0x80) {
			m = (uint8_t)c;
		} else {
			int k = 0;
			while (k < 128 && mac_roman[k] != c) {
				k++;
			}
			if (k == 128) {
				return false;
			}
			m = (uint8_t)(0x80 + k);
		}
		if (n == AFP_NAME_MAX) {
			return false;
		}
		mac[n++] = m;
	}
	*len = n;
	return true;
}

/* A pathname: type 2 (long names), one component (or none: the folder
 * itself). False if the name won't do. */
static bool put_path(struct buf *b, const char *name) {
	uint8_t mac[AFP_NAME_MAX];
	size_t n;
	if (!afp_name_to_mac(name, mac, &n)) {
		return false;
	}
	put8(b, PATH_LONG_NAMES);
	put8(b, (uint8_t)n);
	put_bytes(b, mac, n);
	return true;
}

/* ---- errors ---------------------------------------------------------------- */

static int to_errno(int code) {
	switch (code) {
	case 0: return 0;
	case DSI_ERR_IO:
	case AFP_SESSION_CLOSED:
	case AFP_SERVER_GOING_DOWN: return -EIO;
	case AFP_ACCESS_DENIED:
	case AFP_USER_NOT_AUTH: return -EACCES;
	case AFP_OBJECT_LOCKED:
	case AFP_VOL_LOCKED: return -EROFS;
	case AFP_DIR_NOT_EMPTY: return -ENOTEMPTY;
	case AFP_DISK_FULL: return -ENOSPC;
	case AFP_OBJECT_EXISTS: return -EEXIST;
	case AFP_OBJECT_NOT_FOUND:
	case AFP_DIR_NOT_FOUND:
	case AFP_ITEM_NOT_FOUND: return -ENOENT;
	case AFP_OBJECT_TYPE_ERR: return -ENOTDIR;
	case AFP_DENY_CONFLICT:
	case AFP_FILE_BUSY:
	case AFP_LOCK_ERR: return -EBUSY;
	case AFP_TOO_MANY_FILES: return -EMFILE;
	case AFP_CANT_MOVE: return -EINVAL;
	case AFP_CANT_RENAME:
	case AFP_PARAM_ERR: return -EINVAL;
	case AFP_CALL_NOT_SUPPORTED: return -ENOSYS;
	case AFP_BITMAP_ERR: return -EOPNOTSUPP; /* a parameter this server can't set */
	default: return -EIO;
	}
}

static int call(struct afp *a, const struct buf *b, uint8_t **out, size_t *outlen) {
	int code = dsi_request(&a->dsi, DSI_COMMAND, b->data, b->len, NULL, 0, out, outlen);
	if (code == AFP_SESSION_CLOSED || code == AFP_SERVER_GOING_DOWN) {
		dsi_fail(&a->dsi);
	}
	/* ZACOS9_AFP_DEBUG=1: each command and its result on stderr. */
	static int debug = -1;
	if (debug < 0) {
		debug = getenv("ZACOS9_AFP_DEBUG") != NULL;
	}
	if (debug) {
		fprintf(stderr, "afp: command %d (%zu bytes) -> %d  [", b->data[0], b->len, code);
		for (size_t i = 0; i < b->len && i < 40; i++) {
			fprintf(stderr, "%02x", b->data[i]);
		}
		fprintf(stderr, "] -> [");
		for (size_t i = 0; out && *out && outlen && i < *outlen && i < 24; i++) {
			fprintf(stderr, "%02x", (*out)[i]);
		}
		fprintf(stderr, "]\n");
	}
	return code;
}

/* ---- server status ---------------------------------------------------------- */

static void pstring_at(const uint8_t *data, size_t len, size_t off, char *out, size_t size) {
	out[0] = 0;
	if (off < len && off + 1 + data[off] <= len) {
		afp_name_from_mac(data + off + 1, data[off], out, size);
	}
}

int afp_server_info(const char *host, const char *port, struct afp_server_info *info) {
	memset(info, 0, sizeof(*info));
	struct dsi d;
	int r = dsi_connect(&d, host, port);
	if (r < 0) {
		return r;
	}
	uint8_t *data;
	size_t len;
	int code = dsi_request(&d, DSI_GET_STATUS, NULL, 0, NULL, 0, &data, &len);
	dsi_close(&d);
	if (code != 0 || len < 11) {
		free(data);
		return -EPROTO;
	}
	/* FPGetSrvrInfo: offsets to the machine type, AFP versions and UAMs;
	 * the icon; flags; then the server's name. */
	pstring_at(data, len, 10, info->name, sizeof(info->name));
	pstring_at(data, len, get16(data), info->machine, sizeof(info->machine));
	info->flags = get16(data + 8);
	for (int list = 0; list < 2; list++) {
		size_t off = get16(data + 2 + 2 * list);
		if (off >= len) {
			continue;
		}
		int count = data[off++];
		for (int i = 0; i < count && i < 8 && off < len; i++) {
			char *slot = list == 0 ? info->versions[info->nversions++] : info->uams[info->nuams++];
			pstring_at(data, len, off, slot, 32);
			off += 1 + data[off];
		}
	}
	free(data);
	return 0;
}

/* ---- logging in -------------------------------------------------------------- */

static void crypto_init(void) {
	static bool done;
	if (!done) {
		gcry_check_version(NULL);
		gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
		done = true;
	}
}

static bool offers(const struct afp_server_info *info, const char *uam) {
	for (int i = 0; i < info->nuams; i++) {
		if (strcasecmp(info->uams[i], uam) == 0) {
			return true;
		}
	}
	return false;
}

static void des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]) {
	gcry_cipher_hd_t h;
	gcry_cipher_open(&h, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
	gcry_cipher_setkey(h, key, 8); /* weak keys are reported, but still used */
	gcry_cipher_encrypt(h, out, 8, in, 8);
	gcry_cipher_close(h);
}

/* "Randnum exchange" and "2-Way Randnum exchange": the server sends 8
 * random bytes, we send them back DES-encrypted with the password as the
 * key (2-way: each key byte shifted left a bit, and the server proves it
 * knows the password too by encrypting our own random bytes). */
static int login_randnum(struct afp *a, const char *uam, const char *user, const char *password,
		bool two_way) {
	struct buf b = { .len = 0 };
	put8(&b, FP_LOGIN);
	put_pstring(&b, a->version);
	put_pstring(&b, uam);
	put_pstring(&b, user);
	uint8_t *reply;
	size_t len;
	int code = call(a, &b, &reply, &len);
	if (code != AFP_AUTH_CONTINUE) {
		free(reply);
		return code ? code : AFP_MISC_ERR;
	}
	if (len < 10) {
		free(reply);
		return AFP_MISC_ERR;
	}
	uint8_t key[8] = { 0 };
	memcpy(key, password, strnlen(password, 8));
	if (two_way) {
		for (int i = 0; i < 8; i++) {
			key[i] <<= 1;
		}
	}
	uint8_t answer[8], ours[8], theirs[8];
	des_encrypt(key, reply + 2, answer);
	b.len = 0;
	put8(&b, FP_LOGIN_CONT);
	put8(&b, 0);
	put_bytes(&b, reply, 2); /* the session ID */
	put_bytes(&b, answer, 8);
	free(reply);
	if (two_way) {
		gcry_randomize(ours, 8, GCRY_STRONG_RANDOM);
		put_bytes(&b, ours, 8);
	}
	code = call(a, &b, &reply, &len);
	if (code == 0 && two_way) {
		des_encrypt(key, ours, theirs);
		if (len < 8 || memcmp(reply, theirs, 8) != 0) {
			code = AFP_USER_NOT_AUTH; /* not the server it claims to be */
		}
	}
	free(reply);
	return code;
}

/* DHX ("DHCAST128"): a Diffie-Hellman exchange gives a CAST-128 key; the
 * server sends a nonce, and we send back nonce + 1 and the password,
 * encrypted. */
static int login_dhx(struct afp *a, const char *user, const char *password) {
	static const uint8_t prime[16] = { 0xba, 0x28, 0x73, 0xdf, 0xb0, 0x60, 0x57, 0xd4,
		0x3f, 0x20, 0x24, 0x74, 0x4c, 0xee, 0xe7, 0x5b };
	gcry_mpi_t p, g = gcry_mpi_set_ui(NULL, 7), ra = gcry_mpi_new(256), ma = gcry_mpi_new(128),
		mb, k = gcry_mpi_new(128);
	gcry_mpi_scan(&p, GCRYMPI_FMT_USG, prime, sizeof(prime), NULL);
	gcry_mpi_randomize(ra, 256, GCRY_STRONG_RANDOM);
	gcry_mpi_powm(ma, g, ra, p);
	uint8_t ma_bytes[16] = { 0 }, raw[16];
	size_t n;
	gcry_mpi_print(GCRYMPI_FMT_USG, raw, sizeof(raw), &n, ma);
	memcpy(ma_bytes + 16 - n, raw, n);

	struct buf b = { .len = 0 };
	put8(&b, FP_LOGIN);
	put_pstring(&b, a->version);
	put_pstring(&b, "DHCAST128");
	put_pstring(&b, user);
	pad_even(&b);
	put_bytes(&b, ma_bytes, 16);
	uint8_t *reply;
	size_t len;
	int code = call(a, &b, &reply, &len);
	if (code != AFP_AUTH_CONTINUE || len < 2 + 16 + 32) {
		free(reply);
		gcry_mpi_release(p);
		gcry_mpi_release(g);
		gcry_mpi_release(ra);
		gcry_mpi_release(ma);
		gcry_mpi_release(k);
		return code == AFP_AUTH_CONTINUE ? AFP_MISC_ERR : code;
	}
	gcry_mpi_scan(&mb, GCRYMPI_FMT_USG, reply + 2, 16, NULL);
	gcry_mpi_powm(k, mb, ra, p);
	uint8_t key[16] = { 0 };
	gcry_mpi_print(GCRYMPI_FMT_USG, raw, sizeof(raw), &n, k);
	memcpy(key + 16 - n, raw, n);

	uint8_t secret[32], out[80], plain[80] = { 0 };
	gcry_cipher_hd_t h;
	gcry_cipher_open(&h, GCRY_CIPHER_CAST5, GCRY_CIPHER_MODE_CBC, 0);
	gcry_cipher_setkey(h, key, 16);
	gcry_cipher_setiv(h, "CJalbert", 8);
	gcry_cipher_decrypt(h, secret, 32, reply + 18, 32);
	/* nonce + 1, then the password in 64 bytes */
	memcpy(plain, secret, 16);
	for (int i = 15; i >= 0 && ++plain[i] == 0; i--) {
	}
	memcpy(plain + 16, password, strnlen(password, 64));
	gcry_cipher_setiv(h, "LWallace", 8);
	gcry_cipher_encrypt(h, out, 80, plain, 80);
	gcry_cipher_close(h);

	b.len = 0;
	put8(&b, FP_LOGIN_CONT);
	put8(&b, 0);
	put_bytes(&b, reply, 2);
	put_bytes(&b, out, 80);
	free(reply);
	code = call(a, &b, NULL, NULL);
	memset(plain, 0, sizeof(plain));
	gcry_mpi_release(p);
	gcry_mpi_release(g);
	gcry_mpi_release(ra);
	gcry_mpi_release(ma);
	gcry_mpi_release(mb);
	gcry_mpi_release(k);
	return code;
}

static int login_cleartext(struct afp *a, const char *user, const char *password) {
	struct buf b = { .len = 0 };
	put8(&b, FP_LOGIN);
	put_pstring(&b, a->version);
	put_pstring(&b, "Cleartxt Passwrd");
	put_pstring(&b, user);
	pad_even(&b);
	uint8_t pw[8] = { 0 };
	memcpy(pw, password, strnlen(password, 8));
	put_bytes(&b, pw, 8);
	return call(a, &b, NULL, NULL);
}

static int login_guest(struct afp *a) {
	struct buf b = { .len = 0 };
	put8(&b, FP_LOGIN);
	put_pstring(&b, a->version);
	put_pstring(&b, "No User Authent");
	return call(a, &b, NULL, NULL);
}

int afp_connect(struct afp *a, const char *host, const char *port, const char *user,
		const char *password, bool allow_cleartext, char *err, size_t errlen) {
	memset(a, 0, sizeof(*a));
	a->dsi.fd = -1;
	crypto_init();
	struct afp_server_info info;
	int r = afp_server_info(host, port, &info);
	if (r < 0) {
		snprintf(err, errlen, r == -EPROTO ? "“%s” isn’t a file server." :
			"The server “%s” could not be reached. It may not be on the network.", host);
		return r;
	}
	static const char *const ours[] = { "AFP2.2", "AFPVersion 2.1", "AFPVersion 2.0" };
	for (size_t i = 0; i < sizeof(ours) / sizeof(*ours) && !a->version[0]; i++) {
		for (int j = 0; j < info.nversions; j++) {
			if (strcmp(info.versions[j], ours[i]) == 0) {
				snprintf(a->version, sizeof(a->version), "%s", ours[i]);
				a->afp22 = i == 0;
			}
		}
	}
	if (!a->version[0]) {
		snprintf(err, errlen, "“%s” uses a version of AFP this computer doesn’t know.", info.name);
		return -EPROTONOSUPPORT;
	}
	const char *how = NULL;
	if (!user) {
		how = offers(&info, "No User Authent") ? "guest" : NULL;
	} else if (offers(&info, "DHCAST128")) {
		how = "dhx";
	} else if (offers(&info, "2-Way Randnum exchange")) {
		how = "2way";
	} else if (offers(&info, "Randnum exchange")) {
		how = "randnum";
	} else if (offers(&info, "Cleartxt Passwrd") && allow_cleartext) {
		how = "cleartext";
	}
	if (!how) {
		snprintf(err, errlen, user ? "“%s” only takes passwords sent as clear text."
		                           : "“%s” doesn’t let guests connect.", info.name);
		return -EACCES;
	}
	if ((r = dsi_connect(&a->dsi, host, port)) < 0) {
		snprintf(err, errlen, "The server “%s” could not be reached.", info.name);
		return r;
	}
	int code = dsi_open_session(&a->dsi);
	if (code == 0) {
		if (strcmp(how, "guest") == 0) {
			code = login_guest(a);
		} else if (strcmp(how, "dhx") == 0) {
			code = login_dhx(a, user, password);
		} else if (strcmp(how, "cleartext") == 0) {
			code = login_cleartext(a, user, password);
		} else {
			code = login_randnum(a, strcmp(how, "2way") == 0 ? "2-Way Randnum exchange"
				: "Randnum exchange", user, password, strcmp(how, "2way") == 0);
		}
	}
	if (code != 0) {
		if (code == AFP_USER_NOT_AUTH || code == AFP_PARAM_ERR) {
			snprintf(err, errlen, "Sorry, your name or password is incorrect. Please re-enter it.");
		} else if (code == AFP_PWD_EXPIRED) {
			snprintf(err, errlen, "Your password on “%s” has expired.", info.name);
		} else {
			snprintf(err, errlen, "Could not log in to “%s” (AFP error %d).", info.name, code);
		}
		dsi_close(&a->dsi);
		return code == AFP_USER_NOT_AUTH || code == AFP_PARAM_ERR ? -EACCES : to_errno(code);
	}
	return 0;
}

void afp_disconnect(struct afp *a) {
	if (a->dsi.fd < 0) {
		return;
	}
	struct buf b = { .len = 0 };
	if (a->vol) {
		put8(&b, FP_CLOSE_VOL);
		put8(&b, 0);
		put16(&b, a->vol);
		call(a, &b, NULL, NULL);
		b.len = 0;
	}
	put8(&b, FP_LOGOUT);
	put8(&b, 0);
	call(a, &b, NULL, NULL);
	dsi_close(&a->dsi);
}

/* ---- volumes ------------------------------------------------------------------ */

int afp_volumes(struct afp *a, struct afp_volume *vols, int max, int *n) {
	struct buf b = { .len = 0 };
	put8(&b, FP_GET_SRVR_PARMS);
	put8(&b, 0);
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	*n = 0;
	if (code == 0 && len >= 5) {
		size_t off = 5;
		for (int i = 0; i < data[4] && off + 2 <= len && *n < max; i++) {
			uint8_t flags = data[off], nlen = data[off + 1];
			if (off + 2 + nlen > len) {
				break;
			}
			afp_name_from_mac(data + off + 2, nlen, vols[*n].name, sizeof(vols[*n].name));
			vols[*n].has_password = flags & 0x80;
			(*n)++;
			off += 2 + nlen;
		}
	}
	free(data);
	return to_errno(code);
}

int afp_open_volume(struct afp *a, const char *name) {
	uint8_t mac[AFP_NAME_MAX];
	size_t n;
	if (!afp_name_to_mac(name, mac, &n)) {
		return -EINVAL;
	}
	struct buf b = { .len = 0 };
	put8(&b, FP_OPEN_VOL);
	put8(&b, 0);
	put16(&b, VOL_ID);
	put8(&b, (uint8_t)n);
	put_bytes(&b, mac, n);
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	if (code == 0 && len >= 4) {
		a->vol = get16(data + 2);
	}
	free(data);
	return to_errno(code);
}

int afp_volume_space(struct afp *a, uint64_t *free_bytes, uint64_t *total_bytes) {
	struct buf b = { .len = 0 };
	const uint16_t bits = a->afp22 ? VOL_EXT_BYTES_FREE | VOL_EXT_BYTES_TOTAL
	                               : VOL_BYTES_FREE | VOL_BYTES_TOTAL;
	put8(&b, FP_GET_VOL_PARMS);
	put8(&b, 0);
	put16(&b, a->vol);
	put16(&b, bits);
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	if (code == 0) {
		if (a->afp22 && len >= 2 + 16) {
			*free_bytes = (uint64_t)get32(data + 2) << 32 | get32(data + 6);
			*total_bytes = (uint64_t)get32(data + 10) << 32 | get32(data + 14);
		} else if (len >= 2 + 8) {
			*free_bytes = get32(data + 2);
			*total_bytes = get32(data + 6);
		}
	}
	free(data);
	return to_errno(code);
}

/* ---- files and folders ------------------------------------------------------- */

static time_t afp_time(uint32_t t) {
	return t == 0x80000000u ? 0 : (time_t)(int32_t)t + AFP_EPOCH;
}

/* One entry's parameters, laid out in bitmap order; names are offsets
 * from the start of the parameters. */
static void parse_params(const uint8_t *p, size_t len, bool is_dir, uint16_t bitmap,
		struct afp_entry *e) {
	static const uint8_t file_sizes[16] = { 2, 4, 4, 4, 4, 32, 2, 2, 4, 4, 4 };
	static const uint8_t dir_sizes[16] = { 2, 4, 4, 4, 4, 32, 2, 2, 4, 2, 4, 4, 4 };
	const uint8_t *sizes = is_dir ? dir_sizes : file_sizes;
	memset(e, 0, sizeof(*e));
	e->is_dir = is_dir;
	e->writable = true;
	size_t off = 0;
	for (int bit = 0; bit < 16; bit++) {
		if (!(bitmap & (1 << bit))) {
			continue;
		}
		const size_t sz = sizes[bit];
		if (sz == 0 || off + sz > len) {
			return; /* a parameter we don't know: can't go further */
		}
		const uint8_t *v = p + off;
		switch (1 << bit) {
		case BIT_ATTR:
			e->invisible |= get16(v) & 0x0001;
			if (!is_dir && (get16(v) & 0x0020)) { /* write inhibit */
				e->writable = false;
			}
			break;
		case BIT_CREATED: e->created = afp_time(get32(v)); break;
		case BIT_MODIFIED: e->modified = afp_time(get32(v)); break;
		case BIT_FINDER_INFO: e->invisible |= (get16(v + 8) & 0x4000) != 0; break;
		case BIT_LONG_NAME: {
			const size_t at = get16(v);
			if (at < len && at + 1 + p[at] <= len) {
				afp_name_from_mac(p + at + 1, p[at], e->name, sizeof(e->name));
			}
			break;
		}
		case BIT_ID: e->id = get32(v); break;
		default:
			if (!is_dir && (1 << bit) == BIT_DATA_LEN) {
				e->size = get32(v);
			} else if (is_dir && (1 << bit) == BIT_OFFSPRING) {
				e->entries = get16(v);
			} else if (is_dir && (1 << bit) == BIT_ACCESS) {
				e->writable = (v[3] & 0x04) != 0; /* the user's own rights */
			}
		}
		off += sz;
	}
}

int afp_stat(struct afp *a, uint32_t dir, const char *name, struct afp_entry *e) {
	struct buf b = { .len = 0 };
	put8(&b, FP_GET_FILE_DIR_PARMS);
	put8(&b, 0);
	put16(&b, a->vol);
	put32(&b, dir);
	put16(&b, FILE_BITS);
	put16(&b, DIR_BITS);
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	if (code == 0) {
		if (len < 6) {
			code = AFP_MISC_ERR;
		} else {
			const bool is_dir = data[4] & 0x80;
			parse_params(data + 6, len - 6, is_dir, is_dir ? get16(data + 2) : get16(data), e);
			if (!e->name[0]) {
				snprintf(e->name, sizeof(e->name), "%s", name);
			}
		}
	}
	free(data);
	return to_errno(code);
}

int afp_list(struct afp *a, uint32_t dir, int start, int count, struct afp_entry *e, int *n) {
	struct buf b = { .len = 0 };
	put8(&b, FP_ENUMERATE);
	put8(&b, 0);
	put16(&b, a->vol);
	put32(&b, dir);
	put16(&b, FILE_BITS);
	put16(&b, DIR_BITS);
	put16(&b, (uint16_t)count);
	put16(&b, (uint16_t)start);
	put16(&b, 4096); /* the largest reply we take */
	put_path(&b, "");
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	*n = 0;
	if (code == AFP_OBJECT_NOT_FOUND) {
		free(data);
		return 0; /* no more */
	}
	if (code == 0 && len >= 6) {
		const uint16_t file_bits = get16(data), dir_bits = get16(data + 2);
		const int actual = get16(data + 4);
		size_t off = 6;
		for (int i = 0; i < actual && i < count && off + 2 <= len; i++) {
			const size_t entry = data[off];
			if (entry < 2 || off + entry > len) {
				break;
			}
			const bool is_dir = data[off + 1] & 0x80;
			parse_params(data + off + 2, entry - 2, is_dir, is_dir ? dir_bits : file_bits, &e[*n]);
			(*n)++;
			off += entry;
		}
	}
	free(data);
	return to_errno(code);
}

int afp_open_fork(struct afp *a, uint32_t dir, const char *name, bool write, uint16_t *fork) {
	struct buf b = { .len = 0 };
	put8(&b, FP_OPEN_FORK);
	put8(&b, 0); /* the data fork */
	put16(&b, a->vol);
	put32(&b, dir);
	put16(&b, 0);
	put16(&b, write ? 0x0003 : 0x0001); /* read (and write); deny nothing */
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	if (code == 0 && len >= 4) {
		*fork = get16(data + 2);
	}
	free(data);
	return to_errno(code);
}

int afp_close_fork(struct afp *a, uint16_t fork) {
	struct buf b = { .len = 0 };
	put8(&b, FP_CLOSE_FORK);
	put8(&b, 0);
	put16(&b, fork);
	return to_errno(call(a, &b, NULL, NULL));
}

/* A read or write at most this big, within what the server takes. */
static size_t chunk(struct afp *a) {
	size_t q = a->dsi.server_quantum > 64 ? a->dsi.server_quantum - 64 : 1024;
	return q < 128 * 1024 ? q : 128 * 1024;
}

ssize_t afp_read(struct afp *a, uint16_t fork, uint32_t offset, void *buf, size_t size) {
	size_t done = 0;
	while (done < size) {
		const size_t want = size - done < chunk(a) ? size - done : chunk(a);
		struct buf b = { .len = 0 };
		put8(&b, FP_READ);
		put8(&b, 0);
		put16(&b, fork);
		put32(&b, offset + (uint32_t)done);
		put32(&b, (uint32_t)want);
		put8(&b, 0); /* no newline mask */
		put8(&b, 0);
		uint8_t *data;
		size_t len;
		int code = call(a, &b, &data, &len);
		if (code != 0 && code != AFP_EOF) {
			free(data);
			return done ? (ssize_t)done : to_errno(code);
		}
		if (len > want) {
			len = want;
		}
		memcpy((uint8_t *)buf + done, data, len);
		free(data);
		done += len;
		if (code == AFP_EOF || len == 0) {
			break;
		}
	}
	return (ssize_t)done;
}

ssize_t afp_write(struct afp *a, uint16_t fork, uint32_t offset, const void *buf, size_t size) {
	size_t done = 0;
	while (done < size) {
		const size_t n = size - done < chunk(a) ? size - done : chunk(a);
		struct buf b = { .len = 0 };
		put8(&b, FP_WRITE);
		put8(&b, 0); /* offset from the start */
		put16(&b, fork);
		put32(&b, offset + (uint32_t)done);
		put32(&b, (uint32_t)n);
		int code = dsi_request(&a->dsi, DSI_WRITE, b.data, b.len, (const uint8_t *)buf + done, n,
			NULL, NULL);
		if (code == AFP_SESSION_CLOSED || code == AFP_SERVER_GOING_DOWN) {
			dsi_fail(&a->dsi);
		}
		if (getenv("ZACOS9_AFP_DEBUG")) {
			fprintf(stderr, "afp: write %zu bytes at %u -> %d\n", n, offset + (unsigned)done, code);
		}
		if (code != 0) {
			return done ? (ssize_t)done : to_errno(code);
		}
		done += n;
	}
	return (ssize_t)done;
}

int afp_set_fork_size(struct afp *a, uint16_t fork, uint32_t size) {
	struct buf b = { .len = 0 };
	put8(&b, FP_SET_FORK_PARMS);
	put8(&b, 0);
	put16(&b, fork);
	put16(&b, BIT_DATA_LEN);
	put32(&b, size);
	return to_errno(call(a, &b, NULL, NULL));
}

int afp_flush_fork(struct afp *a, uint16_t fork) {
	struct buf b = { .len = 0 };
	put8(&b, FP_FLUSH_FORK);
	put8(&b, 0);
	put16(&b, fork);
	return to_errno(call(a, &b, NULL, NULL));
}

int afp_create_file(struct afp *a, uint32_t dir, const char *name) {
	struct buf b = { .len = 0 };
	put8(&b, FP_CREATE_FILE);
	put8(&b, 0); /* soft: fail if it exists */
	put16(&b, a->vol);
	put32(&b, dir);
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	return to_errno(call(a, &b, NULL, NULL));
}

int afp_create_dir(struct afp *a, uint32_t dir, const char *name, uint32_t *new_id) {
	struct buf b = { .len = 0 };
	put8(&b, FP_CREATE_DIR);
	put8(&b, 0);
	put16(&b, a->vol);
	put32(&b, dir);
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	uint8_t *data;
	size_t len;
	int code = call(a, &b, &data, &len);
	if (code == 0 && new_id && len >= 4) {
		*new_id = get32(data);
	}
	free(data);
	return to_errno(code);
}

int afp_delete(struct afp *a, uint32_t dir, const char *name) {
	struct buf b = { .len = 0 };
	put8(&b, FP_DELETE);
	put8(&b, 0);
	put16(&b, a->vol);
	put32(&b, dir);
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	return to_errno(call(a, &b, NULL, NULL));
}

int afp_move(struct afp *a, uint32_t from_dir, const char *from, uint32_t to_dir, const char *to) {
	struct buf b = { .len = 0 };
	if (from_dir == to_dir) {
		put8(&b, FP_RENAME);
		put8(&b, 0);
		put16(&b, a->vol);
		put32(&b, from_dir);
		if (!put_path(&b, from) || !put_path(&b, to)) {
			return -ENAMETOOLONG;
		}
	} else {
		put8(&b, FP_MOVE_AND_RENAME);
		put8(&b, 0);
		put16(&b, a->vol);
		put32(&b, from_dir);
		put32(&b, to_dir);
		if (!put_path(&b, from) || !put_path(&b, "") || !put_path(&b, to)) {
			return -ENAMETOOLONG;
		}
	}
	return to_errno(call(a, &b, NULL, NULL));
}

int afp_set_modified(struct afp *a, uint32_t dir, const char *name, bool is_dir, time_t t) {
	struct buf b = { .len = 0 };
	put8(&b, FP_SET_FILE_DIR_PARMS);
	put8(&b, 0);
	put16(&b, a->vol);
	put32(&b, dir);
	put16(&b, BIT_MODIFIED);
	if (!put_path(&b, name)) {
		return -ENAMETOOLONG;
	}
	pad_even(&b);
	put32(&b, (uint32_t)(int32_t)(t - AFP_EPOCH));
	(void)is_dir;
	return to_errno(call(a, &b, NULL, NULL));
}
