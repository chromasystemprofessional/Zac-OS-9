/* Input methods in the Finder's own text fields: an icon's name and the
 * Get Info comments. A preedit is shown without changing the text, a
 * commit inserts it, and the queries give the candidate window a caret. */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <cerrno>
#include <cstdio>
#include <sys/xattr.h>

#include "items.h"
#include "labeleditor.h"

#define private public
#define protected public
#include "infowindow.h"
#undef protected
#undef private

static int failures;

static void check(bool ok, const char *what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static void sendLabelPreedit(LabelEditor &editor, const QString &text, int cursor) {
	QInputMethodEvent e(text, { QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, cursor, 1,
		QVariant()) });
	check(editor.inputMethodEvent(&e), "the name field takes a preedit");
}

static void sendLabelCommit(LabelEditor &editor, const QString &text) {
	QInputMethodEvent e;
	e.setCommitString(text);
	check(editor.inputMethodEvent(&e), "the name field takes a commit");
}

static void sendPreedit(QWidget *w, const QString &text, int cursor) {
	QInputMethodEvent e(text, { QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, cursor, 1,
		QVariant()) });
	QCoreApplication::sendEvent(w, &e);
}

static void sendCommit(QWidget *w, const QString &text) {
	QInputMethodEvent e;
	e.setCommitString(text);
	QCoreApplication::sendEvent(w, &e);
}

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir dir;
	check(dir.isValid(), "create a temporary folder");

	const QString oldPath = dir.path() + "/old name.txt";
	QFile file(oldPath);
	check(file.open(QIODevice::WriteOnly), "create a file to rename");
	file.write("x");
	file.close();

	Item item;
	item.name = "old name.txt";
	item.path = oldPath;
	item.shownName = "old name";
	item.kind = PL_ICON_DOCUMENT;

	bool reported = false;
	LabelEditor editor([] {});
	editor.setEditingChanged([&reported](bool editing) { reported = editing; });
	editor.begin(&item);
	check(reported && editor.active(), "renaming reports that a text field has the focus");
	check(editor.text() == "old name", "the label starts with the shown name");
	check(editor.inputMethodQuery(Qt::ImEnabled, QPoint(0, 0)).toBool(),
		"the name field is enabled for an input method");
	check(editor.inputMethodQuery(Qt::ImSurroundingText, QPoint(0, 0)).toString() == "old name",
		"the input method hears the name");
	check(editor.inputMethodQuery(Qt::ImCursorPosition, QPoint(0, 0)).toInt() == 8,
		"the whole name is selected before typing");

	sendLabelPreedit(editor, QString::fromUtf8("にほん"), 3);
	check(editor.preedit() == QString::fromUtf8("にほん") && editor.text() == "old name",
		"a preedit is shown without changing the name");
	check(editor.cursorRect(QPoint(10, 10)).isValid(),
		"the candidate window has a caret rectangle during the preedit");

	sendLabelCommit(editor, QString::fromUtf8("日本"));
	check(editor.text() == QString::fromUtf8("日本") && editor.preedit().isEmpty(),
		"a commit replaces the selected name");
	editor.commit();
	check(!reported, "ending the rename reports that the text field is gone");
	check(QFile::exists(dir.path() + QString::fromUtf8("/日本.txt")),
		"the committed name is renamed on disk, keeping its extension");

	const QString notePath = dir.path() + "/notes.txt";
	QFile note(notePath);
	check(note.open(QIODevice::WriteOnly), "create a file with a comment");
	note.write("x");
	note.close();
	InfoWindow::open(notePath, PL_ICON_DOCUMENT, "notes.txt");
	InfoWindow *info = nullptr;
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (auto *window = dynamic_cast<InfoWindow *>(widget)) {
			info = window;
		}
	}
	check(info != nullptr, "Get Info opens");
	if (info) {
		info->resize(420, 420);
		info->show();
		QApplication::processEvents();
		info->repaint();
		QApplication::processEvents();
		check(info->m_commentBox.isValid(), "the comments box is laid out");

		info->setEditingComment(true);
		check(info->testAttribute(Qt::WA_InputMethodEnabled),
			"the comments box makes the window a text field");
		check(info->inputMethodQuery(Qt::ImEnabled).toBool(),
			"the comments box is enabled for an input method");
		sendPreedit(info, QString::fromUtf8("にほん"), 3);
		check(info->m_commentPreedit == QString::fromUtf8("にほん") && info->m_comment.isEmpty(),
			"a comment preedit is shown without changing the comment");
		check(info->inputMethodQuery(Qt::ImCursorRectangle).toRect().isValid(),
			"the comment candidate window has a caret rectangle");
		sendCommit(info, QString::fromUtf8("日本"));
		check(info->m_comment == QString::fromUtf8("日本") &&
			info->m_commentPreedit.isEmpty(), "a comment commit is inserted");
		char value[256];
		const ssize_t n = getxattr(QFile::encodeName(notePath).constData(),
			"user.xdg.comment", value, sizeof(value));
		if (n < 0 && errno == ENOTSUP) {
			QTextStream(stdout) << "skip  comments (no extended attributes here)\n";
		} else {
			check(n == 6 && QString::fromUtf8(value, static_cast<int>(n)) ==
				QString::fromUtf8("日本"), "the committed comment is saved");
		}

		/* Japanese comments have no spaces: wrap at Unicode line breaks.
		 * Long enough that the font's exact metrics can't decide it. */
		info->m_comment = QString::fromUtf8(
			"日本語の文章には空白がないので、単語で折り返すことができません。").repeated(3);
		const QStringList lines = info->commentLines();
		check(lines.size() > 1, "a Japanese comment wraps without spaces");
		bool punctuationOk = true;
		for (const QString &line : lines) {
			punctuationOk &= !line.startsWith(QString::fromUtf8("。")) &&
				!line.startsWith(QString::fromUtf8("、"));
		}
		check(punctuationOk, "Japanese punctuation stays off the line start");

		info->setEditingComment(false);
		check(!info->testAttribute(Qt::WA_InputMethodEnabled),
			"leaving the comments box stops being a text field");
		info->close();
	}

	return failures ? 1 : 0;
}
