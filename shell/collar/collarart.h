#pragma once

/* The Collar's artwork, shared by the strip and its control panel's preview. */

#include <cstdint>

#include "draw.h"

struct CollarParts {
	enum Kind { Volume, SoundSet, Network, Bluetooth, Resolution, Brightness, Power, Phone, Keychain };
	/* Measured from the reference: a 13 px close box and scroll buttons, 30 px
	 * module cells, each followed by a 1 px black divider, and an 18 px grip
	 * whose first column is the divider. Folded, only the grip shows. */
	static constexpr int Height = 24, Close = 13, Arrow = 13, Cell = 30, Grip = 18, Closed = Grip;
	static constexpr int FirstModule = 1 + Close + 1 + Arrow + 1, MenuBar = 20;
	static constexpr int openWidth(int modules) { return FirstModule + modules * (Cell + 1) + Arrow + Grip; }
	/* The control panel's preview: one module, the right scroll button and the grip. */
	static constexpr int PreviewWidth = 1 + Cell + 1 + Arrow + Grip;
};

namespace collarart {

constexpr uint32_t Face = RGB(0xC0, 0xC0, 0xC0);
constexpr uint32_t Shade = RGB(0xA0, 0xA0, 0xA0);
constexpr uint32_t Shadow = RGB(0x80, 0x80, 0x80);

extern const char *const CloseBoxArt[CollarParts::Height];
extern const char *const GripArt[CollarParts::Height];

void art(pl_canvas *c, int x, int y, const char *row, bool mirror = false, int width = 0);
void cell(pl_canvas *c, int x, int width, bool recessed);
void scrollButton(pl_canvas *c, int x, bool right, bool enabled);
void menuArrow(pl_canvas *c, int x);
/* ZacOS's own 16 x 16 module icons; `level` (0-100, or -1) fills a phone's screen. */
void icon(pl_canvas *c, CollarParts::Kind kind, int x, int y, bool on, int level = -1);
/* A sound module, the right scroll button and the grip, top-left at (x, y). */
void preview(pl_canvas *c, int x, int y);

} // namespace collarart
