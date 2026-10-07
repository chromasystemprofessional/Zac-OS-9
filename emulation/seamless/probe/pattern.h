#ifndef ZACOS9_CLASSIC_PROBE_PATTERN_H
#define ZACOS9_CLASSIC_PROBE_PATTERN_H

static int probe_black(unsigned int window, unsigned long frame, int x, int y) {
	if (y < 8) {
		return ((frame >> ((x / 8) % 32)) ^ window) & 1;
	}
	return ((x / 8) + (y / 8) + window + frame) & 1;
}

#endif
