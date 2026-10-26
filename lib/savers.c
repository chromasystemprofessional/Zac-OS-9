#include "savers.h"

#include <stdlib.h>
#include <string.h>

#include "logo.h"
#include "patterns.h"

enum { SAVER_PATTERNS, SAVER_STARS, SAVER_LOGO, SAVER_BLANK, N_SAVERS };

static const struct {
	const char *id, *name;
} savers[N_SAVERS] = {
	{ "patterns", "Desktop Patterns" },
	{ "stars", "Starfield" },
	{ "logo", "Bouncing Logo" },
	{ "blank", "Blank Screen" },
};

/* Desktop Patterns: the screen fills with a pattern in blocks, one by one
 * in no order, then the next pattern takes over the same way, each this
 * long; the blocks come in during the first REVEAL of it. */
#define PATTERN_SECONDS 12.0
#define PATTERN_REVEAL 0.8
#define PATTERN_BLOCK 32
/* Starfield: stars per 2500 pixels of screen, and how fast they come. */
#define STAR_DENSITY 2500
#define STAR_SPEED 0.35
#define LOGO_SPEED 70.0 /* pixels a second, each way */

struct star {
	float x, y, z; /* x, y in -1..1; z in (0, 1], nearer when smaller */
};

struct pl_saver {
	int kind, w, h;
	unsigned rng;
	double last; /* the time of the frame before */
	/* Desktop Patterns: one tile of each pattern, made when first needed,
	 * and when each block takes the next pattern, into its period. */
	int first_pattern, n_tiles;
	uint32_t **tiles;
	int *tile_w, *tile_h;
	float *block_delay;
	int blocks_x, blocks_y;
	/* Starfield */
	struct star *stars;
	int n_stars;
	/* Bouncing Logo: where it is and which way it goes; on a big screen,
	 * the logo at double size. */
	double lx, ly, dx, dy;
	int logo_size;
	uint32_t *logo_big;
};

int pl_saver_count(void) {
	return N_SAVERS;
}

const char *pl_saver_id(int index) {
	return index >= 0 && index < N_SAVERS ? savers[index].id : NULL;
}

const char *pl_saver_name(int index) {
	return index >= 0 && index < N_SAVERS ? savers[index].name : NULL;
}

int pl_saver_find(const char *id) {
	for (int i = 0; id && i < N_SAVERS; i++) {
		if (strcmp(id, savers[i].id) == 0) {
			return i;
		}
	}
	return 0;
}

static unsigned next_random(struct pl_saver *s) {
	s->rng = s->rng * 1103515245u + 12345u;
	return s->rng >> 8;
}

/* -1 .. 1 */
static float random_unit(struct pl_saver *s) {
	return (float)(next_random(s) % 20001) / 10000.0f - 1.0f;
}

static void new_star(struct pl_saver *s, struct star *star, bool anywhere) {
	star->x = random_unit(s);
	star->y = random_unit(s);
	star->z = anywhere ? 0.05f + (float)(next_random(s) % 1000) / 1050.0f : 1.0f;
}

struct pl_saver *pl_saver_new(int index, int w, int h, unsigned seed) {
	struct pl_saver *s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->kind = index >= 0 && index < N_SAVERS ? index : 0;
	s->w = w > 0 ? w : 1;
	s->h = h > 0 ? h : 1;
	s->rng = seed ? seed : 1;
	s->first_pattern = (int)(next_random(s) % (unsigned)pl_pattern_count());
	if (s->kind == SAVER_PATTERNS) {
		s->n_tiles = pl_pattern_count();
		s->tiles = calloc((size_t)s->n_tiles, sizeof(*s->tiles));
		s->tile_w = calloc((size_t)s->n_tiles, sizeof(*s->tile_w));
		s->tile_h = calloc((size_t)s->n_tiles, sizeof(*s->tile_h));
		s->blocks_x = (s->w + PATTERN_BLOCK - 1) / PATTERN_BLOCK;
		s->blocks_y = (s->h + PATTERN_BLOCK - 1) / PATTERN_BLOCK;
		s->block_delay = calloc((size_t)s->blocks_x * s->blocks_y, sizeof(*s->block_delay));
		for (int i = 0; s->block_delay && i < s->blocks_x * s->blocks_y; i++) {
			s->block_delay[i] = (float)(PATTERN_SECONDS * PATTERN_REVEAL) *
				(float)(next_random(s) % 10000) / 10000.0f;
		}
	}
	if (s->kind == SAVER_STARS) {
		s->n_stars = s->w * s->h / STAR_DENSITY + 8;
		s->stars = calloc((size_t)s->n_stars, sizeof(*s->stars));
		for (int i = 0; s->stars && i < s->n_stars; i++) {
			new_star(s, &s->stars[i], true);
		}
	}
	s->logo_size = PL_LOGO_SIZE_HQ;
	if (s->kind == SAVER_LOGO && s->w >= 512 && s->h >= 384 &&
			(s->logo_big = malloc(4 * PL_LOGO_SIZE_HQ * PL_LOGO_SIZE_HQ * sizeof(uint32_t)))) {
		const uint32_t *logo = logo_pixels_hq();
		const int n = 2 * PL_LOGO_SIZE_HQ;
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				s->logo_big[y * n + x] = logo[(y / 2) * PL_LOGO_SIZE_HQ + x / 2];
			}
		}
		s->logo_size = n;
	}
	s->lx = next_random(s) % (unsigned)(s->w > s->logo_size ? s->w - s->logo_size : 1);
	s->ly = next_random(s) % (unsigned)(s->h > s->logo_size ? s->h - s->logo_size : 1);
	s->dx = next_random(s) & 1 ? LOGO_SPEED : -LOGO_SPEED;
	s->dy = next_random(s) & 1 ? LOGO_SPEED : -LOGO_SPEED;
	return s;
}

void pl_saver_free(struct pl_saver *s) {
	if (s) {
		for (int i = 0; s->tiles && i < s->n_tiles; i++) {
			free(s->tiles[i]);
		}
		free(s->tiles);
		free(s->tile_w);
		free(s->tile_h);
		free(s->block_delay);
		free(s->logo_big);
		free(s->stars);
		free(s);
	}
}

static void fill(struct pl_canvas *c, int w, int h, uint32_t color) {
	pl_fill(c, c->x, c->y, c->x + w - 1, c->y + h - 1, color);
}

/* One tile of pattern `index`, drawn the first time it's needed. */
static const uint32_t *pattern_tile(struct pl_saver *s, int index, int *tw, int *th) {
	if (!s->tiles[index]) {
		pl_pattern_size(index, &s->tile_w[index], &s->tile_h[index]);
		uint32_t *px = malloc((size_t)s->tile_w[index] * s->tile_h[index] * sizeof(uint32_t));
		if (!px) {
			return NULL;
		}
		struct pl_canvas tile = { px, s->tile_w[index], 0, 0, s->tile_w[index], s->tile_h[index], NULL };
		pl_pattern_fill(&tile, index, 0, 0, s->tile_w[index] - 1, s->tile_h[index] - 1);
		s->tiles[index] = px;
	}
	*tw = s->tile_w[index];
	*th = s->tile_h[index];
	return s->tiles[index];
}

static void draw_patterns(struct pl_saver *s, struct pl_canvas *c, int w, int h, double t) {
	if (!s->tiles || !s->block_delay) {
		fill(c, w, h, C_BLACK);
		return;
	}
	for (int by = 0; by < s->blocks_y; by++) {
		for (int bx = 0; bx < s->blocks_x; bx++) {
			/* Which pattern this block shows now; black until its first. */
			const double since = t - s->block_delay[by * s->blocks_x + bx];
			const int x0 = bx * PATTERN_BLOCK, y0 = by * PATTERN_BLOCK;
			const int x1 = x0 + PATTERN_BLOCK < w ? x0 + PATTERN_BLOCK : w;
			const int y1 = y0 + PATTERN_BLOCK < h ? y0 + PATTERN_BLOCK : h;
			int tw = 0, th = 0;
			const uint32_t *tile = since < 0 ? NULL : pattern_tile(s,
				(s->first_pattern + (int)(since / PATTERN_SECONDS)) % s->n_tiles, &tw, &th);
			for (int y = y0; y < y1; y++) {
				uint32_t *out = c->px + y * c->stride;
				/* Lined up across blocks, as on the desktop. */
				const uint32_t *row = tile ? tile + (y % th) * tw : NULL;
				for (int x = x0; x < x1; x++) {
					out[x] = row ? row[x % tw] : C_BLACK;
				}
			}
		}
	}
}

static void draw_stars(struct pl_saver *s, struct pl_canvas *c, int w, int h, double dt) {
	fill(c, w, h, C_BLACK);
	const float cx = w / 2.0f, cy = h / 2.0f;
	for (int i = 0; i < s->n_stars; i++) {
		struct star *star = &s->stars[i];
		star->z -= (float)(dt * STAR_SPEED);
		const int x = (int)(cx + star->x / (star->z > 0.01f ? star->z : 0.01f) * cx);
		const int y = (int)(cy + star->y / (star->z > 0.01f ? star->z : 0.01f) * cy);
		if (star->z <= 0.01f || x < 0 || y < 0 || x >= w || y >= h) {
			new_star(s, star, false);
			continue;
		}
		/* Nearer is brighter and, closest, bigger. */
		const int shade = star->z < 0.25f ? 0xF : star->z < 0.5f ? 0xD : star->z < 0.75f ? 0xA : 0x7;
		const int size = star->z < 0.2f ? 2 : 1;
		pl_fill(c, c->x + x, c->y + y, c->x + x + size - 1, c->y + y + size - 1, GRAY(shade));
	}
}

static void draw_logo(struct pl_saver *s, struct pl_canvas *c, int w, int h, double dt) {
	fill(c, w, h, C_BLACK);
	const int size = s->logo_size;
	s->lx += s->dx * dt;
	s->ly += s->dy * dt;
	/* Off each edge it bounces back. */
	if (s->lx < 0 || s->lx > w - size) {
		s->dx = s->lx < 0 ? LOGO_SPEED : -LOGO_SPEED;
		s->lx = s->lx < 0 ? 0 : w > size ? w - size : 0;
	}
	if (s->ly < 0 || s->ly > h - size) {
		s->dy = s->ly < 0 ? LOGO_SPEED : -LOGO_SPEED;
		s->ly = s->ly < 0 ? 0 : h > size ? h - size : 0;
	}
	pl_image_blend(c, c->x + (int)s->lx, c->y + (int)s->ly, s->logo_big ? s->logo_big : logo_pixels_hq(),
		size, size, false);
}

void pl_saver_draw(struct pl_saver *s, struct pl_canvas *c, double seconds) {
	double dt = seconds - s->last;
	dt = dt < 0 ? 0 : dt > 0.25 ? 0.25 : dt; /* a stall doesn't jump */
	s->last = seconds;
	const int w = s->w < c->width ? s->w : c->width, h = s->h < c->height ? s->h : c->height;
	switch (s->kind) {
	case SAVER_PATTERNS:
		draw_patterns(s, c, w, h, seconds);
		break;
	case SAVER_STARS:
		draw_stars(s, c, w, h, dt);
		break;
	case SAVER_LOGO:
		draw_logo(s, c, w, h, dt);
		break;
	default:
		fill(c, w, h, C_BLACK);
	}
}
