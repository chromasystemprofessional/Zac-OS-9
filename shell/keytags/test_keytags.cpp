/* Key Tags: characters by level from xkb, dead keys, and the window's
 * Shift and Option (held or clicked) and typing by clicking. */
#include <QApplication>
#include <cstdio>

#include "keytagswindow.h"

#define CHECK(cond) do { if (!(cond)) { \
	std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

enum { K_2 = 3, K_Q = 16, K_Y = 21, K_A = 30, K_GRAVE = 41, K_LEFTSHIFT = 42,
	K_LEFTALT = 56, K_SPACE = 57, K_RIGHTALT = 100 };

int main(int argc, char **argv) {
	/* No /etc/default/keyboard model or options from this machine. */
	qputenv("ZACOS9_KEYBOARD_DEFAULTS", "/nonexistent");
	QApplication app(argc, argv);

	KeyTags us;
	CHECK(!us.load("no-such(thing") && !us.load(""));
	CHECK(us.load("us") && us.code() == "us");
	CHECK(us.tag(K_A, 0).text == "a" && us.tag(K_A, 1).text == "A");
	CHECK(us.tag(K_2, 1).text == "@" && !us.tag(K_2, 1).dead);
	CHECK(us.tag(K_SPACE, 0).text == " " && us.tag(15, 0).text.isEmpty()); /* Tab */
	CHECK(us.tag(K_A, 7).text.isEmpty());
	CHECK(us.isShift(K_LEFTSHIFT) && !us.isShift(K_A) && !us.isOption(K_LEFTALT));

	KeyTags de;
	CHECK(de.load("de"));
	CHECK(de.tag(K_Y, 0).text == "z");
	CHECK(de.tag(K_GRAVE, 0).dead && de.tag(K_GRAVE, 0).text == "^");
	CHECK(de.tag(K_Q, 2).text == "@"); /* Alt Gr-Q */
	CHECK(de.isOption(K_RIGHTALT));

	KeyTagsWindow window;
	CHECK(window.showLayout("de") && !window.showLayout("no-such(thing"));
	CHECK(window.tags().code() == "de" && window.level() == 0);
	window.keyEvent(K_LEFTSHIFT, true);
	CHECK(window.level() == 1 && window.isDown(K_LEFTSHIFT));
	window.keyEvent(K_RIGHTALT, true);
	CHECK(window.level() == 3);
	window.keyEvent(K_LEFTSHIFT, false);
	window.keyEvent(K_RIGHTALT, false);
	CHECK(window.level() == 0 && !window.isDown(K_LEFTSHIFT));

	/* Clicking: Option stays down until clicked again. */
	window.click(K_Y);
	window.click(K_LEFTALT);
	CHECK(window.level() == 2 && window.isDown(K_LEFTALT) && window.isDown(K_RIGHTALT));
	window.click(K_Q);
	window.click(K_LEFTALT);
	window.click(K_LEFTSHIFT);
	window.click(K_A);
	window.click(K_SPACE);
	CHECK(window.typed() == "z@A ");
	return 0;
}
