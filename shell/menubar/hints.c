/*
 * Hint Bubbles over the menu bar's own menus (Mac OS 9's Balloon Help):
 * with Help ▸ Show Hint Bubbles on, the item under the pointer gets a pale
 * yellow bubble beside the menu, its tail pointing at the item. Programs'
 * own menus (global menus) have no hints, as programs without balloons had
 * none. Not measured: the shape follows the period look, not a figure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "menubar.h"

static const struct {
	const char *label, *hint;
} hints[] = {
	{ "About This Computer…", "Shows how much memory this computer has and what it is running." },
	{ "Force Quit", "Quits a program that has stopped responding. Unsaved changes in it are lost." },
	{ "Control Panels", "Opens a control panel, to change how this computer works." },
	{ "Favorites", "Opens one of your favourite items. Add one with File ▸ Add To Favorites." },
	{ "Recent Applications", "Opens one of the programs you used most recently." },
	{ "Recent Documents", "Opens one of the documents you opened most recently." },
	{ "Recent Servers", "Connects again to one of the servers you used most recently." },
	{ "New Folder", "Makes a new, empty folder in the active window." },
	{ "Open", "Opens the selected items." },
	{ "Print", "Prints the selected documents." },
	{ "Move To Trash", "Moves the selected items to the Trash. They are deleted when you empty it." },
	{ "Close Window", "Closes the active window." },
	{ "Get Info", "Shows information about the selected item, such as its size, where it is and its comments." },
	{ "Label", "Gives the selected items a label, which colours their icons." },
	{ "Duplicate", "Makes a copy of the selected items in the same folder." },
	{ "Make Alias", "Makes an alias of the selected item: a small file that opens the original." },
	{ "Put Away", "Returns the selected items to where they came from: out of the Trash, or a disk off the desktop." },
	{ "Add To Favorites", "Adds the selected item to Favorites in the Zac menu and in Open and Save dialogs." },
	{ "Find…", "Finds files and folders by name, date, kind and more." },
	{ "Find Similar Items", "Finds items like the selected one." },
	{ "Index Selection", "Indexes the selected folders, so Find can search what is in their files." },
	{ "Search Internet…", "Searches the web with the search site you choose." },
	{ "Show Original", "Finds the original item of the selected alias." },
	{ "Encrypt…", "Makes a password-protected copy of the selected file." },
	{ "Decrypt…", "Makes an ordinary copy of the selected encrypted file, after asking for its password." },
	{ "Page Setup…", "Sets the paper size, orientation and scale for printing a window." },
	{ "Undo", "Undoes your last action." },
	{ "Cut", "Removes the selection and places it on the Clipboard." },
	{ "Copy", "Copies the selection to the Clipboard." },
	{ "Paste", "Inserts the contents of the Clipboard." },
	{ "Clear", "Removes the selection without placing it on the Clipboard." },
	{ "Select All", "Selects everything in the active window." },
	{ "Show Clipboard", "Shows what is on the Clipboard." },
	{ "as Icons", "Shows the active window's items as icons." },
	{ "as Buttons", "Shows the active window's items as buttons, which open with one click." },
	{ "as List", "Shows the active window's items in a list." },
	{ "as Window", "Makes the active pop-up window an ordinary window." },
	{ "as Pop-up Window", "Makes the active window a pop-up window, with a tab at the bottom of the screen." },
	{ "Clean Up", "Lines up the icons in the active window." },
	{ "Arrange", "Puts the icons in order, by name, date, size, kind or label." },
	{ "Sort List", "Sorts the list by one of its columns." },
	{ "Reset Column Positions", "Puts the list's columns back in their standard order and widths." },
	{ "View Options…", "Sets how the active window shows its items." },
	{ "Empty Trash…", "Deletes the items in the Trash for good." },
	{ "Empty Trash", "Deletes the items in the Trash for good." },
	{ "Eject", "Ejects the selected disks, or disconnects from the selected servers." },
	{ "Erase Disk…", "Erases everything on the selected disk." },
	{ "Show All Applications", "Shows the applications you have hidden from the Applications folder." },
	{ "Sleep", "Puts the computer to sleep. Press a key or move the mouse to wake it." },
	{ "Restart", "Quits your programs and starts the computer again." },
	{ "Shut Down", "Quits your programs and turns the computer off." },
	{ "Log Out…", "Quits your programs and returns to the login window." },
	{ "Help Center", "Opens the Help Center, where you can read help for ZacOS and your programs." },
	{ "Show Hint Bubbles", "Turns hint bubbles like this one on or off." },
	{ "ZacOS Help", "Opens help for ZacOS." },
	{ "Learn ZacOS", "Opens short lessons on using ZacOS, with a tour for people who used a Mac before." },
	{ "Mouse Practice", "Opens Mouse Practice, a game for learning pointing, clicking, double-clicking and dragging." },
	{ "Hide Others", "Hides the windows of every program except the active one." },
	{ "Show All", "Shows the windows of every hidden program." },
	{ "Quit", "Quits the active program." },
	{ "Change Desktop Background…", "Opens the Appearance control panel to choose a desktop picture or pattern." },
	{ "Attach a Folder Trick", "Attaches a script that runs when items are added to the folder." },
	{ "Remove a Folder Trick", "Removes a script attached to the folder." },
};

const char *hint_text(const struct mb_item *it, char *buf, size_t n) {
	if (!it->label || it->action == ACT_DBUSMENU) {
		return NULL;
	}
	if (it->action == ACT_ACTIVATE_APP) {
		snprintf(buf, n, "Makes %s the active program, bringing its windows forward.",
			it->label);
		return buf;
	}
	if (it->action == ACT_HIDE_APP) {
		return "Hides the active program's windows. Choose it again from this menu to show them.";
	}
	if (it->action == ACT_KEYBOARD_LAYOUT) {
		return "Types with this keyboard layout. Press ⌘-Space to switch layouts.";
	}
	for (size_t i = 0; i < sizeof(hints) / sizeof(hints[0]); i++) {
		if (strcmp(it->label, hints[i].label) == 0) {
			return hints[i].hint;
		}
	}
	return NULL;
}

#define HINT_MAX_W 180 /* text width */
#define HINT_PAD 5
#define HINT_LINE 12
#define HINT_MAX_LINES 8
#define HINT_RADIUS 4

static const int corner[HINT_RADIUS] = { 3, 1, 1, 0 }; /* inset by row */

static int ink_width(const struct plat_text *t) {
	return t->ink_r < 0 ? 0 : t->ink_r + 1;
}

/* Fills a rounded rectangle. */
static void round_rect(struct pl_canvas *c, int x0, int y0, int x1, int y1, uint32_t color) {
	for (int y = y0; y <= y1; y++) {
		int r = y - y0 < HINT_RADIUS ? corner[y - y0]
			: y1 - y < HINT_RADIUS ? corner[y1 - y] : 0;
		pl_hline(c, x0 + r, x1 - r, y, color);
	}
}

uint32_t *hint_paint(const char *text, bool tail_right, int *w, int *h, struct pl_text_log **log) {
	struct plat_text *lines[HINT_MAX_LINES];
	int n = 0, width = 0;
	char line[512] = "";
	const char *p = text;
	while (*p && n < HINT_MAX_LINES) {
		const char *end = strchr(p, ' ');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		char trial[512];
		size_t used = strlen(line) + (*line ? 1 : 0);
		if (used + len >= sizeof(trial)) {
			len = sizeof(trial) - used - 1;
		}
		memcpy(trial, line, strlen(line));
		if (*line) {
			trial[used - 1] = ' ';
		}
		memcpy(trial + used, p, len);
		trial[used + len] = '\0';
		struct plat_text *t = text_render_font(trial, 10000, PL_FONT_VIEWS);
		bool fits = !t || ink_width(t) <= HINT_MAX_W || !*line;
		text_destroy(t);
		if (fits) {
			snprintf(line, sizeof(line), "%s", trial);
			p += len;
			while (*p == ' ') {
				p++;
			}
		}
		if (!fits || !*p) {
			lines[n] = text_render_font(line, HINT_MAX_W, PL_FONT_VIEWS);
			if (lines[n]) {
				if (ink_width(lines[n]) > width) {
					width = ink_width(lines[n]);
				}
				n++;
			}
			line[0] = '\0';
		}
	}
	if (n == 0) {
		return NULL;
	}

	int bw = width + 2 * HINT_PAD + 2, bh = n * HINT_LINE + 2 * HINT_PAD;
	if (bh < HINT_TIP_Y + HINT_TAIL_H + HINT_RADIUS + 1) {
		bh = HINT_TIP_Y + HINT_TAIL_H + HINT_RADIUS + 1;
	}
	*w = bw + HINT_TAIL_W;
	*h = bh;
	uint32_t *px = calloc((size_t)*w * *h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = *w, .width = *w, .height = *h };
	if (log) {
		*log = NULL;
	}
	const uint32_t black = GRAY(0), yellow = RGB(0xFF, 0xFF, 0xCC);
	int x0 = HINT_TAIL_W;
	round_rect(&c, x0, 0, *w - 1, bh - 1, black);
	round_rect(&c, x0 + 1, 1, *w - 2, bh - 2, yellow);
	/* The tail: a wedge from the tip, flat on top, back to the body. */
	pl_hline(&c, 0, x0, HINT_TIP_Y, black);
	for (int k = 1; k <= HINT_TAIL_H; k++) {
		int a = (k - 1) * HINT_TAIL_W / HINT_TAIL_H, b = k * HINT_TAIL_W / HINT_TAIL_H;
		pl_hline(&c, a, b > a ? b - 1 : a, HINT_TIP_Y + k, black);
		if (b + 1 <= x0 && k < HINT_TAIL_H) {
			pl_hline(&c, b, x0, HINT_TIP_Y + k, yellow);
		}
	}
	if (tail_right) {
		for (int y = 0; y < *h; y++) {
			uint32_t *row = px + (size_t)y * *w;
			for (int a = 0, b = *w - 1; a < b; a++, b--) {
				uint32_t t = row[a];
				row[a] = row[b];
				row[b] = t;
			}
		}
	}
	/* Draw the text after mirroring: only the bubble turns around, the
	 * text on the right-hand side stays readable. */
	int ascent = lines[0]->baseline;
	int text_x = (tail_right ? 0 : x0) + HINT_PAD + 1;
	if (log) {
		c.log = *log = pl_text_log_new(px, *w);
	}
	for (int i = 0; i < n; i++) {
		pl_text(&c, lines[i], text_x, HINT_PAD + ascent + i * HINT_LINE, black);
		text_destroy(lines[i]);
	}
	return px;
}
