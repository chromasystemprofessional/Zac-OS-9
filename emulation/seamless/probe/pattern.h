#ifndef ZACOS9_CLASSIC_PROBE_PATTERN_H
#define ZACOS9_CLASSIC_PROBE_PATTERN_H

static inline int probe_black(unsigned int window, unsigned long frame, int x, int y) {
	if (y < 8) {
		return ((frame >> ((x / 8) % 32)) ^ window) & 1;
	}
	return ((x / 8) + (y / 8) + window + frame) & 1;
}

/* The color scene's host-checkable layout. The fixture draws it and the host
   verifier recomputes it independently: rows 0-7 are the grayscale frame
   marker, rows 8-15 eight known RGB bars, rows 16-47 a per-cell color grid.
   Anything below is the graphics scene, which the host compares against the
   fixture's own reference instead of predicting it. */
enum {
	PROBE_COLOR_MARKER_ROWS = 8,
	PROBE_COLOR_CALIBRATION_ROWS = 8,
	PROBE_COLOR_ORACLE_ROWS = 32,
	PROBE_COLOR_SCENE_ROWS = 48
};

static inline void probe_color(unsigned int window, unsigned long frame, int x, int y,
		unsigned char rgb[3]) {
	if (y < PROBE_COLOR_MARKER_ROWS) {
		int value = ((frame >> ((x / 8) % 32)) ^ window) & 1 ? 255 : 0;
		rgb[0] = rgb[1] = rgb[2] = (unsigned char)value;
	} else if (y < PROBE_COLOR_MARKER_ROWS + PROBE_COLOR_CALIBRATION_ROWS) {
		static const unsigned char bars[8][3] = {
			{ 0, 0, 0 }, { 255, 255, 255 },
			{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
			{ 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 }
		};
		int bar = (x / 32) % 8;
		rgb[0] = bars[bar][0];
		rgb[1] = bars[bar][1];
		rgb[2] = bars[bar][2];
	} else {
		unsigned int cell_x = (unsigned int)(x / 8);
		unsigned int cell_y = (unsigned int)((y - PROBE_COLOR_MARKER_ROWS -
			PROBE_COLOR_CALIBRATION_ROWS) / 8);
		unsigned int tick = frame & 0xff;
		/* Every channel is a 5-bit value, expanded to the exact 8-bit
		   equivalent, so 16-bit RGB555 and 32-bit ARGB exports agree. */
		unsigned int red = (cell_x * 37 + cell_y * 11 + tick * 5 + window * 127) & 31;
		unsigned int green = (cell_x * 13 + cell_y * 53 + tick * 7 + window * 191) & 31;
		unsigned int blue = (cell_x * 71 + cell_y * 29 + tick * 3 + window * 251) & 31;
		rgb[0] = (unsigned char)((red << 3) | (red >> 2));
		rgb[1] = (unsigned char)((green << 3) | (green >> 2));
		rgb[2] = (unsigned char)((blue << 3) | (blue >> 2));
	}
}

#endif
