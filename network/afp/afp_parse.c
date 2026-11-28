#include "afp_parse.h"

#include <string.h>

#define AFP_EPOCH 946684800 /* 2000-01-01 00:00:00 UTC */

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

static uint16_t get16(const uint8_t *p) {
	return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static time_t afp_time(uint32_t t) {
	return t == 0x80000000u ? 0 : (time_t)(int32_t)t + AFP_EPOCH;
}

static void pstring_at(const uint8_t *data, size_t len, size_t off, char *out, size_t size) {
	out[0] = 0;
	if (off < len && off + 1 + data[off] <= len) {
		afp_name_from_mac(data + off + 1, data[off], out, size);
	}
}

bool afp_parse_server_info(const uint8_t *data, size_t len,
		struct afp_server_info *info) {
	memset(info, 0, sizeof(*info));
	if (!data || len < 11) {
		return false;
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
	return true;
}

void afp_parse_params(const uint8_t *p, size_t len, bool is_dir,
		uint16_t bitmap, struct afp_entry *e) {
	static const uint8_t file_sizes[16] = { 2, 4, 4, 4, 4, 32, 2, 2, 4, 4, 4 };
	static const uint8_t dir_sizes[16] = { 2, 4, 4, 4, 4, 32, 2, 2, 4, 2, 4, 4, 4 };
	const uint8_t *sizes = is_dir ? dir_sizes : file_sizes;
	memset(e, 0, sizeof(*e));
	e->is_dir = is_dir;
	e->writable = true;
	if (!p) {
		return;
	}
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

int afp_parse_list_reply(const uint8_t *data, size_t len, int max,
		struct afp_entry *e) {
	if (!data || len < 6 || max <= 0) {
		return 0;
	}
	const uint16_t file_bits = get16(data), dir_bits = get16(data + 2);
	const int actual = get16(data + 4);
	int n = 0;
	size_t off = 6;
	for (int i = 0; i < actual && n < max && off + 2 <= len; i++) {
		const size_t entry = data[off];
		if (entry < 2 || off + entry > len) {
			break;
		}
		const bool is_dir = data[off + 1] & 0x80;
		afp_parse_params(data + off + 2, entry - 2, is_dir,
			is_dir ? dir_bits : file_bits, &e[n]);
		n++;
		off += entry;
	}
	return n;
}
