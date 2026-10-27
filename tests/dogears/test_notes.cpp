/* Dog Ears' notes: saved and read back, with their colours, sizes and places. */
#include <QTemporaryDir>
#include <cstdio>

#include "notes.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main() {
	QTemporaryDir dir;
	const QString path = dir.filePath("sub/dog-ears.json");
	check(loadNotes(path).isEmpty(), "no file: no notes");
	Note a;
	a.text = "Buy biscuits\nand a ball";
	a.color = NoteColor::Pink;
	a.size = QSize(300, 200);
	a.position = QPoint(40, 60);
	a.hasPosition = true;
	Note b;
	b.text = "Second";
	check(saveNotes(path, { a, b }), "notes are saved (the folder is made)");
	const QList<Note> back = loadNotes(path);
	check(back.size() == 2 && back[0].text == a.text && back[0].color == NoteColor::Pink &&
		back[0].size == QSize(300, 200) && back[0].hasPosition && back[0].position == QPoint(40, 60),
		"and read back with their colour, size and place");
	check(!back[1].hasPosition && back[1].color == NoteColor::Yellow, "a note never placed has no place; Yellow is the default");
	check(noteTitle(a.text) == "Buy biscuits" && noteTitle("  ") == "Dog Ears" &&
		noteTitle(QString(40, 'x')).size() == 28, "a note's title is its first line, shortened");
	check(QString(noteColorName(NoteColor::Gray)) == "Gray" && noteColorRgb(NoteColor::Yellow) == 0xFFFFA0,
		"colours have names and values");
	return fails ? 1 : 0;
}
