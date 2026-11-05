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
#include "i18n.h"
#include "menubar.h"

static const struct {
	const char *label, *hint;
} hints[] = {
	{ N_("About This Computer…"), N_("Shows how much memory this computer has and what it is running.") },
	{ N_("Force Quit"), N_("Quits a program that has stopped responding. Unsaved changes in it are lost.") },
	{ N_("Control Panels"), N_("Opens a control panel, to change how this computer works.") },
	{ N_("Favorites"), N_("Opens one of your favourite items. Add one with File ▸ Add To Favorites.") },
	{ N_("Recent Applications"), N_("Opens one of the programs you used most recently.") },
	{ N_("Recent Documents"), N_("Opens one of the documents you opened most recently.") },
	{ N_("Recent Servers"), N_("Connects again to one of the servers you used most recently.") },
	{ N_("New Folder"), N_("Makes a new, empty folder in the active window.") },
	{ N_("Open"), N_("Opens the selected items.") },
	{ N_("Print"), N_("Prints the selected documents.") },
	{ N_("Move To Trash"), N_("Moves the selected items to the Trash. They are deleted when you empty it.") },
	{ N_("Close Window"), N_("Closes the active window.") },
	{ N_("Get Info"), N_("Shows information about the selected item, such as its size, where it is and its comments.") },
	{ N_("Label"), N_("Gives the selected items a label, which colours their icons.") },
	{ N_("Duplicate"), N_("Makes a copy of the selected items in the same folder.") },
	{ N_("Make Alias"), N_("Makes an alias of the selected item: a small file that opens the original.") },
	{ N_("Put Away"), N_("Returns the selected items to where they came from: out of the Trash, or a disk off the desktop.") },
	{ N_("Add To Favorites"), N_("Adds the selected item to Favorites in the Zac menu and in Open and Save dialogs.") },
	{ N_("Find…"), N_("Finds files and folders by name, date, kind and more.") },
	{ N_("Find Similar Items"), N_("Finds items like the selected one.") },
	{ N_("Index Selection"), N_("Indexes the selected folders, so Find can search what is in their files.") },
	{ N_("Search Internet…"), N_("Searches the web with the search site you choose.") },
	{ N_("Show Original"), N_("Finds the original item of the selected alias.") },
	{ N_("Encrypt…"), N_("Makes a password-protected copy of the selected file.") },
	{ N_("Decrypt…"), N_("Makes an ordinary copy of the selected encrypted file, after asking for its password.") },
	{ N_("Page Setup…"), N_("Sets the paper size, orientation and scale for printing a window.") },
	{ N_("Undo"), N_("Undoes your last action.") },
	{ N_("Cut"), N_("Removes the selection and places it on the Clipboard.") },
	{ N_("Copy"), N_("Copies the selection to the Clipboard.") },
	{ N_("Paste"), N_("Inserts the contents of the Clipboard.") },
	{ N_("Clear"), N_("Removes the selection without placing it on the Clipboard.") },
	{ N_("Select All"), N_("Selects everything in the active window.") },
	{ N_("Show Clipboard"), N_("Shows what is on the Clipboard.") },
	{ N_("as Icons"), N_("Shows the active window's items as icons.") },
	{ N_("as Buttons"), N_("Shows the active window's items as buttons, which open with one click.") },
	{ N_("as List"), N_("Shows the active window's items in a list.") },
	{ N_("as Window"), N_("Makes the active pop-up window an ordinary window.") },
	{ N_("as Pop-up Window"), N_("Makes the active window a pop-up window, with a tab at the bottom of the screen.") },
	{ N_("Clean Up"), N_("Lines up the icons in the active window.") },
	{ N_("Arrange"), N_("Puts the icons in order, by name, date, size, kind or label.") },
	{ N_("Sort List"), N_("Sorts the list by one of its columns.") },
	{ N_("Reset Column Positions"), N_("Puts the list's columns back in their standard order and widths.") },
	{ N_("View Options…"), N_("Sets how the active window shows its items.") },
	{ N_("Empty Trash…"), N_("Deletes the items in the Trash for good.") },
	{ N_("Empty Trash"), N_("Deletes the items in the Trash for good.") },
	{ N_("Eject"), N_("Ejects the selected disks, or disconnects from the selected servers.") },
	{ N_("Erase Disk…"), N_("Erases everything on the selected disk.") },
	{ N_("Show All Applications"), N_("Shows the applications you have hidden from the Applications folder.") },
	{ N_("Sleep"), N_("Puts the computer to sleep. Press a key or move the mouse to wake it.") },
	{ N_("Restart"), N_("Quits your programs and starts the computer again.") },
	{ N_("Shut Down"), N_("Quits your programs and turns the computer off.") },
	{ N_("Log Out…"), N_("Quits your programs and returns to the login window.") },
	{ N_("Help Center"), N_("Opens the Help Center, where you can read help for ZacOS and your programs.") },
	{ N_("Show Hint Bubbles"), N_("Turns hint bubbles like this one on or off.") },
	{ N_("ZacOS Help"), N_("Opens help for ZacOS.") },
	{ N_("Bark Recorder"), N_("Opens Bark Recorder, to record a sound and play it back.") },
	{ N_("Calculator"), N_("Opens the Calculator, for adding, subtracting, multiplying and dividing.") },
	{ N_("Dog Ears"), N_("Opens Dog Ears: notes that stay on your screen, saved as you type.") },
	{ N_("Jigsaw Puzzle"), N_("Opens Jigsaw Puzzle, a picture in pieces to put back together.") },
	{ N_("Key Tags"), N_("Opens Key Tags, which shows the character each key types with your keyboard layout.") },
	{ N_("Notepad"), N_("Opens Notepad, eight pages for jotting things down.") },
	{ N_("Pedigree"), N_("Opens Pedigree, which describes this computer: its hardware, disks and devices.") },
	{ N_("Photo Booth"), N_("Opens Photo Booth, to take a photo with the built-in camera or a connected webcam.") },
	{ N_("Plotter"), N_("Opens Plotter, which draws the graph of an equation.") },
	{ N_("Stash"), N_("Opens Stash, a place to keep pictures and text to use again.") },
	{ N_("Terminal"), N_("Opens a terminal window, for typing commands.") },
	{ N_("XTerm"), N_("Opens a terminal window, for typing commands.") },
	{ N_("Learn ZacOS"), N_("Opens short lessons on using ZacOS, with a tour for people who used a Mac before.") },
	{ N_("Mouse Practice"), N_("Opens Mouse Practice, a game for learning pointing, clicking, double-clicking and dragging.") },
	{ N_("Hide Others"), N_("Hides the windows of every program except the active one.") },
	{ N_("Show All"), N_("Shows the windows of every hidden program.") },
	{ N_("Quit"), N_("Quits the active program.") },
	{ N_("Change Desktop Background…"), N_("Opens the Appearance control panel to choose a desktop picture or pattern.") },
	{ N_("Attach a Folder Trick"), N_("Attaches a script that runs when items are added to the folder.") },
	{ N_("Remove a Folder Trick"), N_("Removes a script attached to the folder.") },
};

const char *hint_text(const struct mb_item *it, char *buf, size_t n) {
	if (!it->label || it->action == ACT_DBUSMENU) {
		return NULL;
	}
	if (it->action == ACT_ACTIVATE_APP) {
		snprintf(buf, n, _("Makes %s the active program, bringing its windows forward."),
			it->label);
		return buf;
	}
	if (it->action == ACT_HIDE_APP) {
		return _("Hides the active program's windows. Choose it again from this menu to show them.");
	}
	if (it->action == ACT_KEYBOARD_LAYOUT) {
		return _("Types with this keyboard layout. Press ⌘-Space to switch layouts.");
	}
	for (size_t i = 0; i < sizeof(hints) / sizeof(hints[0]); i++) {
		if (strcmp(it->label, _(hints[i].label)) == 0) {
			return _(hints[i].hint);
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
