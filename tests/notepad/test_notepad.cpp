#include <QApplication>
#include <QTemporaryDir>

#include "notepadwindow.h"

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
	QApplication app(argc, argv);
	NotepadWindow first(0);
	if (first.currentPage() != 0 || !first.pageText(0).isEmpty()) {
		return 1;
	}
	first.setPageText(0, "first note");
	first.setPageText(7, "last note");
	NotepadWindow reopened(7);
	if (reopened.currentPage() != 7 || reopened.pageText(0) != "first note" ||
			reopened.pageText(7) != "last note" || !reopened.pageText(-1).isEmpty() ||
			!reopened.pageText(8).isEmpty()) {
		return 1;
	}
	return 0;
}
