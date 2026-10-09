#include "collarart.h"

#include <QtGlobal>
#include <algorithm>
#include <cstdlib>

#include "settings.h"

namespace collarart {

/* ---- Measured artwork ------------------------------------------------------------
 * Traced pixel for pixel from Mac OS 9.2 screenshots of the strip; only the
 * generic chrome is reproduced (the module icons are ZacOS's own).
 * '#' black, 'W' white, '.' C0, '-' A0, '+' 80, ':' CC, '5' 55, ',' DD, '8' 88,
 * '4' 44, 'a' the accent's light shade, ' ' transparent. */

const char *const CloseBoxArt[CollarParts::Height] = {
	"###############",
	"#.............#",
	"#.WWWWWWWWWW.+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W55555555.-+#",
	"#.W5aaaaaaa.-+#",
	"#.W5a88884W.-+#",
	"#.W5a88884W.-+#",
	"#.W5a88884W.-+#",
	"#.W5a88884W.-+#",
	"#.W5a44444W.-+#",
	"#.W5aWWWWWW.-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#.W.........-+#",
	"#..----------+#",
	"#.++++++++++++#",
	"###############",
};

/* The left scroll arrow, rows 7-16 of its button; the right one is its mirror. */
static const char *const ArrowArt[10] = {
	"......5",
	".....55",
	"....5,5555",
	"...5,,,,,5",
	"..5,,,,,,5",
	"..5,,,,,,5",
	"...5,,,,,5",
	"....5,5555",
	".....55",
	"......5",
};

const char *const GripArt[CollarParts::Height] = {
	"##########",
	"##:::::::##",
	"##:WWWWWWW##",
	"##:W:::::::##",
	"##:WW:::::::##",
	"##:W:5:::::::##",
	"##:W:::W::::::##",
	"##:W::::5::##::##",
	"##:WW::::::#W#::##",
	"##:W:5:::::#WW#:8#",
	"##:W:::W:::#WW#:8#",
	"##:W::::5::#WW#:8#",
	"##:WW::::::#WW#:8#",
	"##:W:5:::::#WW#:8#",
	"##:W:::W:::#WW#:8#",
	"##:W::::5::#W#::##",
	"##:WW::::::##::##",
	"##:W:5::::::::##",
	"##:W:::::::::##",
	"##:W::::::::##",
	"##:W:::::::##",
	"##:8888888##",
	"##8444444##",
	"##########",
};

static uint32_t artColor(char ch) {
	switch (ch) {
	case '#': return C_BLACK;
	case 'W': return C_WHITE;
	case '.': return RGB(0xC0, 0xC0, 0xC0);
	case '-': return RGB(0xA0, 0xA0, 0xA0);
	case '+': return RGB(0x80, 0x80, 0x80);
	case ':': return GRAY(0xC);
	case '5': return GRAY(0x5);
	case ',': return GRAY(0xD);
	case '8': return GRAY(0x8);
	case '4': return GRAY(0x4);
	case 'a': return pl_accent_current().light;
	default: return 0;
	}
}

void art(pl_canvas *c, int x, int y, const char *row, bool mirror, int width) {
	for (int i = 0; row[i]; ++i) {
		if (const uint32_t color = artColor(row[i])) {
			pl_put(c, mirror ? x + width - 1 - i : x + i, y, color);
		}
	}
}


/* A raised module cell, or recessed while its menu is open (the scroll buttons' bevel). */
void cell(pl_canvas *c, int x, int width, bool recessed) {
	const int end = x + width - 1;
	pl_fill(c, x, 1, end, CollarParts::Height - 2, Face);
	if (recessed) {
		pl_hline(c, x, end - 1, 1, Shadow);
		pl_vline(c, x, 1, CollarParts::Height - 3, Shadow);
		pl_vline(c, end, 2, CollarParts::Height - 3, C_WHITE);
		pl_hline(c, x + 1, end, CollarParts::Height - 2, C_WHITE);
		return;
	}
	pl_hline(c, x + 1, end - 2, 2, C_WHITE);
	pl_vline(c, x + 1, 2, CollarParts::Height - 4, C_WHITE);
	pl_vline(c, end - 1, 3, CollarParts::Height - 4, Shade);
	pl_hline(c, x + 2, end - 1, CollarParts::Height - 3, Shade);
	pl_vline(c, end, 2, CollarParts::Height - 3, Shadow);
	pl_hline(c, x + 1, end, CollarParts::Height - 2, Shadow);
}

void scrollButton(pl_canvas *c, int x, bool right, bool enabled) {
	cell(c, x, CollarParts::Arrow, true);
	for (int row = 0; row < 10; ++row) {
		char line[16] = {};
		for (int i = 0; ArrowArt[row][i]; ++i) {
			const char ch = ArrowArt[row][i];
			line[i] = enabled && ch == '5' ? '#' : enabled && ch == ',' ? 'W' : ch == '.' ? ' ' : ch;
		}
		art(c, x, 7 + row, line, right, CollarParts::Arrow - 1);
	}
}

void menuArrow(pl_canvas *c, int x) {
	for (int i = 0; i < 4; ++i) {
		pl_vline(c, x + i, 8 + i, 15 - i, C_BLACK);
	}
}

/* ZacOS's own 16 x 16 module icons. */
void icon(pl_canvas *c, CollarParts::Kind kind, int x, int y, bool on, int level) {
	const uint32_t ink = on ? C_BLACK : Shadow;
	const uint32_t blue = on ? RGB(0x66, 0x66, 0xCC) : GRAY(0xA);
	const uint32_t light = on ? RGB(0xCC, 0xCC, 0xFF) : GRAY(0xD);
	const uint32_t gold = on ? RGB(0xEE, 0xBB, 0x33) : GRAY(0xB);
	auto line = [=](int x0, int y0, int x1, int y1, uint32_t color) {
		const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
		for (int i = 0; i <= n; ++i) {
			pl_put(c, x + x0 + (n ? qRound((x1 - x0) * double(i) / n) : 0),
				y + y0 + (n ? qRound((y1 - y0) * double(i) / n) : 0), color);
		}
	};
	switch (kind) {
	case CollarParts::Volume:
		for (int row = 0; row < 11; ++row) {
			const int edge = row < 4 ? 7 - row : row > 6 ? row - 3 : 4;
			pl_hline(c, x + edge, x + 7, y + 3 + row, blue);
		}
		pl_fill(c, x + 1, y + 6, x + 3, y + 10, light);
		pl_outline(c, x, y + 5, x + 4, y + 11, ink);
		line(4, 5, 8, 1, ink); line(8, 1, 8, 15, ink); line(8, 15, 4, 11, ink);
		line(10, 6, 10, 10, ink);
		line(12, 3, 14, 5, ink); line(14, 5, 14, 11, ink); line(14, 11, 12, 13, ink);
		break;
	case CollarParts::SoundSet:
		pl_fill(c, x + 1, y + 11, x + 4, y + 13, blue);
		pl_fill(c, x + 9, y + 9, x + 12, y + 11, blue);
		line(1, 11, 4, 11, ink); line(0, 12, 0, 12, ink); line(1, 14, 3, 14, ink);
		line(9, 9, 12, 9, ink); line(8, 10, 8, 10, ink); line(9, 12, 11, 12, ink);
		line(5, 3, 5, 13, ink); line(13, 1, 13, 11, ink);
		line(5, 3, 13, 1, ink); line(5, 4, 13, 2, ink); line(5, 5, 13, 3, ink);
		pl_put(c, x + 1, y + 12, light);
		pl_put(c, x + 9, y + 10, light);
		break;
	case CollarParts::Network:
		pl_fill(c, x + 1, y + 1, x + 4, y + 5, light);
		pl_fill(c, x + 11, y + 1, x + 14, y + 5, light);
		pl_outline(c, x, y, x + 5, y + 6, ink);
		pl_outline(c, x + 10, y, x + 15, y + 6, ink);
		line(2, 7, 2, 10, ink); line(13, 7, 13, 10, ink); line(1, 10, 14, 10, ink);
		line(7, 10, 7, 14, ink); line(4, 14, 11, 14, ink);
		break;
	case CollarParts::Bluetooth:
		pl_fill(c, x + 2, y, x + 13, y + 15, blue);
		pl_outline(c, x + 2, y, x + 13, y + 15, ink);
		pl_vline(c, x + 3, y + 1, y + 14, light);
		{
			const uint32_t rune = on ? C_WHITE : GRAY(0xD);
			line(7, 2, 7, 13, rune); line(7, 2, 10, 5, rune); line(10, 5, 5, 10, rune);
			line(5, 5, 10, 10, rune); line(10, 10, 7, 13, rune);
		}
		break;
	case CollarParts::Resolution:
		pl_fill(c, x + 1, y + 1, x + 14, y + 10, Face);
		pl_outline(c, x, y, x + 15, y + 11, ink);
		pl_fill(c, x + 2, y + 2, x + 13, y + 9, light);
		pl_outline(c, x + 2, y + 2, x + 13, y + 9, ink);
		line(5, 7, 10, 4, blue);
		line(4, 8, 6, 8, ink); line(4, 6, 4, 8, ink);
		line(9, 4, 11, 4, ink); line(11, 4, 11, 6, ink);
		pl_fill(c, x + 6, y + 12, x + 9, y + 12, ink);
		pl_hline(c, x + 3, x + 12, y + 13, ink);
		pl_hline(c, x + 4, x + 11, y + 14, Shadow);
		break;
	case CollarParts::Brightness:
		pl_outline(c, x + 4, y + 4, x + 11, y + 11, ink);
		pl_fill(c, x + 5, y + 5, x + 10, y + 10, gold);
		pl_hline(c, x + 5, x + 9, y + 5, C_WHITE);
		line(7, 0, 7, 2, ink); line(8, 0, 8, 2, ink); line(7, 13, 7, 15, ink); line(8, 13, 8, 15, ink);
		line(0, 7, 2, 7, ink); line(0, 8, 2, 8, ink); line(13, 7, 15, 7, ink); line(13, 8, 15, 8, ink);
		line(2, 2, 3, 3, ink); line(12, 12, 13, 13, ink); line(2, 13, 3, 12, ink); line(12, 3, 13, 2, ink);
		break;
	case CollarParts::Power:
		pl_outline(c, x, y + 4, x + 13, y + 11, ink);
		pl_fill(c, x + 14, y + 6, x + 15, y + 9, ink);
		pl_fill(c, x + 2, y + 6, x + 11, y + 9, on ? RGB(0x66, 0x99, 0x55) : GRAY(0xA));
		pl_hline(c, x + 2, x + 11, y + 6, on ? RGB(0xBB, 0xDD, 0x99) : GRAY(0xD));
		break;
	case CollarParts::Phone:
		pl_hline(c, x + 4, x + 11, y, ink);
		pl_hline(c, x + 4, x + 11, y + 15, ink);
		pl_vline(c, x + 3, y + 1, y + 14, ink);
		pl_vline(c, x + 12, y + 1, y + 14, ink);
		pl_fill(c, x + 4, y + 1, x + 11, y + 14, blue);
		pl_vline(c, x + 4, y + 1, y + 14, light);
		pl_outline(c, x + 5, y + 2, x + 10, y + 11, ink);
		pl_fill(c, x + 6, y + 3, x + 9, y + 10, on ? C_WHITE : GRAY(0xD));
		if (level >= 0) {
			// The phone's battery charge fills its screen from the bottom.
			const int rows = qRound(8 * std::clamp(level, 0, 100) / 100.0);
			if (rows) {
				pl_fill(c, x + 6, y + 11 - rows, x + 9, y + 10,
					!on ? GRAY(0xA) : level < 15 ? RGB(0xAA, 0x22, 0x22) : RGB(0x66, 0x99, 0x55));
			}
		}
		pl_hline(c, x + 7, x + 8, y + 13, on ? light : GRAY(0xD));
		break;
	case CollarParts::Keychain: {
		// A padlock with a key at it; level 1 opens the shackle (the default keychain is unlocked).
		const int lift = level > 0 ? 2 : 0;
		pl_hline(c, x + 10, x + 13, y + 2 - lift, ink);
		pl_vline(c, x + 9, y + 3 - lift, y + (lift ? 4 : 7), ink);
		pl_vline(c, x + 14, y + 3 - lift, y + 7, ink);
		pl_hline(c, x + 10, x + 13, y + 3 - lift, light);
		pl_fill(c, x + 8, y + 8, x + 15, y + 15, gold);
		pl_outline(c, x + 7, y + 7, x + 15, y + 15, ink);
		pl_hline(c, x + 8, x + 14, y + 8, on ? RGB(0xFF, 0xEE, 0x99) : GRAY(0xD));
		pl_fill(c, x + 11, y + 10, x + 11, y + 12, ink);
		pl_fill(c, x + 1, y + 10, x + 3, y + 12, gold);
		pl_outline(c, x, y + 9, x + 4, y + 13, ink);
		pl_put(c, x + 2, y + 11, ink);
		pl_hline(c, x + 5, x + 6, y + 11, ink);
		pl_put(c, x + 6, y + 12, ink);
		break;
	}
	}
}

void preview(pl_canvas *c, int x, int y) {
	pl_canvas sub = *c;
	sub.x = c->x - x;
	sub.y = c->y - y;
	const int right = 1 + CollarParts::Cell + 1;
	pl_hline(&sub, 0, right + CollarParts::Arrow, 0, C_BLACK);
	pl_hline(&sub, 0, right + CollarParts::Arrow, CollarParts::Height - 1, C_BLACK);
	pl_vline(&sub, 0, 0, CollarParts::Height - 1, C_BLACK);
	cell(&sub, 1, CollarParts::Cell, false);
	pl_vline(&sub, right - 1, 0, CollarParts::Height - 1, C_BLACK);
	icon(&sub, CollarParts::Volume, 3, 4, true);
	menuArrow(&sub, 23);
	scrollButton(&sub, right, true, false);
	for (int row = 0; row < CollarParts::Height; ++row) {
		art(&sub, right + CollarParts::Arrow, row, GripArt[row]);
	}
}

} // namespace collarart
