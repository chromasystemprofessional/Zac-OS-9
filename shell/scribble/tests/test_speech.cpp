#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <cassert>
#include <functional>

#include "scribblewindow.h"

static bool waitFor(const std::function<bool()> &ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 5000) {
		QApplication::processEvents();
		QThread::msleep(5);
	}
	return ready();
}

int main(int argc, char **argv) {
	QTemporaryDir root;
	assert(root.isValid());
	const QString bin = root.path() + "/bin";
	assert(QDir().mkpath(bin));
	const QString scriptPath = bin + "/spd-say";
	QFile script(scriptPath);
	assert(script.open(QIODevice::WriteOnly));
	assert(script.write("#!/bin/sh\nprintf '%s\\n%s\\n%s' \"$1\" \"$2\" \"$3\" > \"$SPEECH_LOG\"\n") > 0);
	assert(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	script.close();
	qputenv("PATH", bin.toUtf8() + ":" + qgetenv("PATH"));
	qputenv("SPEECH_LOG", (root.path() + "/speech.log").toUtf8());
	QApplication app(argc, argv);
	QApplication::setQuitOnLastWindowClosed(false);
	ScribbleWindow window;
	auto *edit = qobject_cast<QTextEdit *>(window.centralWidget());
	assert(edit);
	const QString document = "Ignore this.\nThis is selected.\nSecond line.\nAnd this.";
	const QString selected = "This is selected.\nSecond line.";
	edit->setPlainText(document);
	QTextCursor cursor = edit->textCursor();
	const int start = document.indexOf(selected);
	assert(start >= 0);
	cursor.setPosition(start);
	cursor.setPosition(start + selected.size(), QTextCursor::KeepAnchor);
	edit->setTextCursor(cursor);

	QAction *speak = nullptr;
	QAction *stop = nullptr;
	for (QAction *action : window.findChildren<QAction *>()) {
		if (action->text() == "Speak Selection") {
			speak = action;
		} else if (action->text() == "Stop Speaking") {
			stop = action;
		}
	}
	assert(speak && stop);
	speak->trigger();
	const QString logPath = qEnvironmentVariable("SPEECH_LOG");
	assert(waitFor([&] { return QFile::exists(logPath); }));
	QFile log(logPath);
	assert(log.open(QIODevice::ReadOnly));
	assert(log.readAll() == "--wait\n--\n" + selected.toUtf8());

	/* Styled text: kept as HTML, read back with its styles. */
	const QString styledPath = root.path() + "/letter.html";
	QFile styled(styledPath);
	assert(styled.open(QIODevice::WriteOnly) && styled.write("<p>Dear <b>Zac</b></p>") > 0);
	styled.close();
	ScribbleWindow letter;
	assert(letter.open(styledPath));
	auto *text = qobject_cast<QTextEdit *>(letter.centralWidget());
	text->moveCursor(QTextCursor::End);
	text->insertPlainText("!");
	assert(letter.save());
	assert(styled.open(QIODevice::ReadOnly));
	const QByteArray html = styled.readAll();
	assert(html.contains("font-weight:700") && html.contains("Zac") && html.contains("!"));
	return 0;
}
