/* Input methods on the panels' own text fields: a preedit is shown and
 * queried without changing the text, a commit inserts it, and the query
 * event tells an input method where the caret is. */
#include <QApplication>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QWidget>
#include <cstdio>

#include "panelkit.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

class TestWindow : public QWidget {
public:
	PanelEdit edit;
	PanelHost host{ this };

	TestWindow() {
		edit.rect = QRect(0, 0, 200, 20);
		host.edits = { &edit };
		host.setFocus(&edit);
	}
};

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
	TestWindow w;
	w.show();

	check(w.edit.text.isEmpty() && w.edit.preedit.isEmpty(), "a field starts empty");
	sendPreedit(&w, QString::fromUtf8("にほん"), 3);
	check(w.edit.preedit == QString::fromUtf8("にほん") && w.edit.text.isEmpty(),
		"a preedit is shown without changing the text");
	check(w.edit.preeditCursor == 3, "the input method's preedit cursor is kept");
	check(w.host.inputMethodQuery(Qt::ImCursorPosition).toInt() == 0,
		"queries see the caret before the preedit");
	check(w.host.inputMethodQuery(Qt::ImCursorRectangle).toRect().isValid(),
		"queries see a caret rectangle for the candidate window");

	QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImSurroundingText |
		Qt::ImCursorPosition | Qt::ImCursorRectangle);
	QCoreApplication::sendEvent(&w, &query);
	check(query.value(Qt::ImEnabled).toBool(), "the query event says the field is enabled");
	check(query.value(Qt::ImSurroundingText).toString().isEmpty(),
		"the preedit is not part of the surrounding text");
	check(query.value(Qt::ImCursorPosition).toInt() == 0,
		"the query event sees the caret at the insertion point");
	check(query.value(Qt::ImCursorRectangle).toRect().isValid(),
		"the query event sees the caret rectangle");

	sendCommit(&w, QString::fromUtf8("日本"));
	check(w.edit.text == QString::fromUtf8("日本") && w.edit.preedit.isEmpty(),
		"a commit replaces the preedit with real text");
	check(w.edit.caret == 2, "the caret follows the committed text");
	check(w.host.inputMethodQuery(Qt::ImSurroundingText).toString() == QString::fromUtf8("日本"),
		"the committed text is the surrounding text");

	int edits = 0;
	w.edit.edited = [&] { ++edits; };
	sendPreedit(&w, QString::fromUtf8("ご"), 1);
	sendCommit(&w, QString::fromUtf8("語"));
	check(w.edit.text == QString::fromUtf8("日本語") && edits == 1,
		"a commit appends at the caret and calls the field's edited callback");

	w.edit.password = true;
	sendPreedit(&w, QString::fromUtf8("ひみつ"), 3);
	check(w.host.inputMethodQuery(Qt::ImSurroundingText).toString().isEmpty(),
		"a password field keeps its text from the input method");
	sendCommit(&w, QString::fromUtf8("秘密"));
	check(w.edit.text.endsWith(QString::fromUtf8("秘密")), "a password field still accepts a commit");

	return fails ? 1 : 0;
}
