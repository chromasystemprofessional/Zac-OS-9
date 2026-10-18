#include "scribblewindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QMenuBar>
#include <QTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBlock>

#include "alert.h"
#include "settings.h"

/* The Font and Size menus act on every Scribble window, and are kept. */
static const char *const FONTS[] = { "Sans Serif", "Serif", "Monospaced" };
static const int SIZES[] = { 9, 10, 12, 14, 18, 24 };

static QSettings &settings() {
	static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/scribble.conf", QSettings::IniFormat);
	return s;
}

static QList<ScribbleWindow *> &windows() {
	static QList<ScribbleWindow *> list;
	return list;
}

ScribbleWindow::ScribbleWindow() {
	setAttribute(Qt::WA_DeleteOnClose);
	m_edit = new QTextEdit(this);
	m_edit->setFrameShape(QFrame::NoFrame);
	m_edit->setAcceptRichText(false); /* pasted text takes the Style menu's styles, not the web's */
	setCentralWidget(m_edit);
	resize(480, 360);
	buildMenus();
	applyFont();
	connect(m_edit->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
	connect(&m_speech, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart && !m_speechStopped) {
			Alert::ask("Speech could not start. Install and configure speech-dispatcher.",
				"OK", QString());
		}
	});
	connect(&m_speech, &QProcess::finished, this,
		[this](int exitCode, QProcess::ExitStatus status) {
			const bool stopped = m_speechStopped;
			m_speechStopped = false;
			if (!stopped && (status != QProcess::NormalExit || exitCode != 0)) {
				QString reason = QString::fromUtf8(m_speech.readAllStandardError()).trimmed();
				if (reason.isEmpty()) {
					reason = "speech-dispatcher returned an error.";
				}
				Alert::ask("Speech failed: " + reason, "OK", QString());
			}
		});
	updateTitle();
	windows().append(this);
	connect(this, &QObject::destroyed, [this] { windows().removeOne(this); });
}

ScribbleWindow *ScribbleWindow::newWindow() {
	auto *w = new ScribbleWindow;
	w->show();
	return w;
}

void ScribbleWindow::chooseAndOpen() {
	const QString path = QFileDialog::getOpenFileName(nullptr, "Open",
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
		"Text documents (*.txt *.text *.md *.log *.conf *.ini *.csv README* *);;Styled documents (*.html *.htm);;All files (*)");
	if (path.isEmpty()) {
		return;
	}
	auto *w = new ScribbleWindow;
	if (w->open(path)) {
		w->show();
	} else {
		delete w;
	}
}

bool ScribbleWindow::open(const QString &path) {
	QString error;
	TextFile file;
	if (!readTextFile(path, &file, &error)) {
		Alert::ask(QString::fromUtf8("“%1” could not be opened. %2").arg(QFileInfo(path).fileName(), error),
			"OK", QString());
		return false;
	}
	m_file = file;
	m_path = QFileInfo(path).absoluteFilePath();
	/* A styled document (Style menu) is kept as HTML. */
	if (isStyledPath(m_path)) {
		m_edit->setHtml(file.text);
	} else {
		m_edit->setPlainText(file.text);
	}
	m_edit->document()->setModified(false);
	updateTitle();
	return true;
}

void ScribbleWindow::updateTitle() {
	setWindowTitle(m_path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_path).fileName());
}

bool ScribbleWindow::isStyledPath(const QString &path) {
	return path.endsWith(".html", Qt::CaseInsensitive) || path.endsWith(".htm", Qt::CaseInsensitive);
}

/* Any bold, italic or underlined text (the Style menu's). */
bool ScribbleWindow::hasStyles() const {
	for (QTextBlock b = m_edit->document()->begin(); b.isValid(); b = b.next()) {
		for (auto it = b.begin(); !it.atEnd(); ++it) {
			const QTextCharFormat f = it.fragment().charFormat();
			if (f.fontWeight() > QFont::Normal || f.fontItalic() || f.fontUnderline()) {
				return true;
			}
		}
	}
	return false;
}

bool ScribbleWindow::save() {
	if (m_path.isEmpty()) {
		return saveAs();
	}
	if (!isStyledPath(m_path) && hasStyles()) {
		/* SimpleText kept styles beside the text; a plain file can't. */
		const QString styled = QFileInfo(m_path).path() + '/' + QFileInfo(m_path).completeBaseName() + ".html";
		switch (Alert::choose(QString::fromUtf8("“%1” has styled text, which a plain text file can't keep. "
				"Save it as a styled document, “%2”?").arg(QFileInfo(m_path).fileName(), QFileInfo(styled).fileName()),
				"Save Styled", "Cancel", "Plain Text")) {
		case Alert::Ok:
			m_path = styled;
			updateTitle();
			break;
		case Alert::Cancel:
			return false;
		case Alert::Other:
			break;
		}
	}
	m_file.text = isStyledPath(m_path) ? m_edit->toHtml() : m_edit->toPlainText();
	QString error;
	if (!writeTextFile(m_path, m_file, &error)) {
		Alert::ask(QString::fromUtf8("“%1” could not be saved. %2").arg(QFileInfo(m_path).fileName(), error),
			"OK", QString());
		return false;
	}
	m_edit->document()->setModified(false);
	return true;
}

bool ScribbleWindow::saveAs() {
	const QString start = m_path.isEmpty() ?
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/untitled.txt" : m_path;
	const QString path = QFileDialog::getSaveFileName(this, "Save As", start);
	if (path.isEmpty()) {
		return false;
	}
	m_path = QFileInfo(path).absoluteFilePath();
	updateTitle();
	return save();
}

void ScribbleWindow::closeEvent(QCloseEvent *e) {
	stopSpeaking();
	if (m_edit->document()->isModified() && !m_edit->toPlainText().isEmpty()) {
		const QString name = m_path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_path).fileName();
		switch (Alert::choose(QString::fromUtf8("Save changes to the document “%1” before closing?").arg(name),
				"Save", "Cancel", "Don't Save")) {
		case Alert::Ok:
			if (!save()) {
				e->ignore();
				return;
			}
			break;
		case Alert::Cancel:
			e->ignore();
			return;
		default:
			break;
		}
	}
	e->accept();
}

void ScribbleWindow::speakSelection() {
	if (m_speech.state() != QProcess::NotRunning) {
		Alert::ask("Speech is already in progress. Choose Stop Speaking first.", "OK", QString());
		return;
	}
	QString text = m_edit->textCursor().selectedText();
	text.replace(QChar::ParagraphSeparator, '\n');
	text.replace(QChar::LineSeparator, '\n');
	if (text.trimmed().isEmpty()) {
		Alert::ask("Select some text to speak.", "OK", QString());
		return;
	}
	if (text.size() > 16000) {
		Alert::ask("The selection is too long to speak at once.", "OK", QString());
		return;
	}
	m_speechStopped = false;
	/* The Speech control panel's voice and rate. */
	const char *argv[10];
	char buf[128];
	const int n = pl_speech_args(argv, buf, sizeof(buf));
	QStringList args{ "--wait" };
	for (int i = 1; i < n; i++) {
		args << QString::fromUtf8(argv[i]);
	}
	m_speech.start("spd-say", args << text);
}

void ScribbleWindow::stopSpeaking() {
	if (m_speech.state() != QProcess::NotRunning) {
		m_speechStopped = true;
		m_speech.kill();
	}
}

void ScribbleWindow::find(bool again) {
	if (!again || m_findText.isEmpty()) {
		bool ok = false;
		const QString text = QInputDialog::getText(this, "Find", "Find what:", QLineEdit::Normal,
			m_findText, &ok);
		if (!ok || text.isEmpty()) {
			return;
		}
		m_findText = text;
	}
	/* From the selection on, then round from the top, as SimpleText did. */
	if (!m_edit->find(m_findText)) {
		QTextCursor start = m_edit->textCursor();
		m_edit->moveCursor(QTextCursor::Start);
		if (!m_edit->find(m_findText)) {
			m_edit->setTextCursor(start);
			pl_beep();
		}
	}
}

void ScribbleWindow::applyFont() {
	const QString family = settings().value("font", FONTS[0]).toString();
	const int size = settings().value("size", 12).toInt();
	QFont font = family == "Monospaced" ? QFontDatabase::systemFont(QFontDatabase::FixedFont) :
		QFont(family == "Serif" ? QStringLiteral("serif") : QStringLiteral("sans-serif"));
	if (family == "Serif") {
		font.setStyleHint(QFont::Serif);
	}
	font.setPointSize(size);
	m_edit->setFont(font);
	m_edit->setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(' '));
}

void ScribbleWindow::buildMenus() {
	/* Exported to ZacOS 9's menu bar, as other programs' are. */
	QMenu *file = menuBar()->addMenu("File");
	file->addAction("New", QKeySequence::New, [] { newWindow(); });
	file->addAction("Open...", QKeySequence::Open, [] { chooseAndOpen(); })->setMenuRole(QAction::NoRole);
	file->addSeparator();
	file->addAction("Close", QKeySequence::Close, this, [this] { close(); });
	file->addAction("Save", QKeySequence::Save, this, [this] { save(); });
	file->addAction("Save As...", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, [this] { saveAs(); });
	file->addSeparator();
	QAction *quit = file->addAction("Quit", QKeySequence::Quit, [] {
		/* Each window asks about its own changes; Cancel stops the quit. */
		for (ScribbleWindow *w : QList<ScribbleWindow *>(windows())) {
			if (!w->close()) {
				return;
			}
		}
	});
	quit->setMenuRole(QAction::NoRole);

	QMenu *edit = menuBar()->addMenu("Edit");
	edit->addAction("Undo", QKeySequence::Undo, m_edit, &QTextEdit::undo);
	edit->addAction("Redo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), m_edit, &QTextEdit::redo);
	edit->addSeparator();
	edit->addAction("Cut", QKeySequence::Cut, m_edit, &QTextEdit::cut);
	edit->addAction("Copy", QKeySequence::Copy, m_edit, &QTextEdit::copy);
	edit->addAction("Paste", QKeySequence::Paste, m_edit, &QTextEdit::paste);
	edit->addAction("Clear", this, [this] { m_edit->textCursor().removeSelectedText(); });
	edit->addAction("Select All", QKeySequence::SelectAll, m_edit, &QTextEdit::selectAll);
	edit->addSeparator();
	edit->addAction("Find...", QKeySequence::Find, this, [this] { find(false); });
	edit->addAction("Find Again", QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] { find(true); });

	QMenu *speech = menuBar()->addMenu("Speech");
	speech->addAction("Speak Selection", this, [this] { speakSelection(); });
	speech->addAction("Stop Speaking", this, [this] { stopSpeaking(); });

	/* SimpleText's Style menu, for the selection (or what's typed next). */
	QMenu *style = menuBar()->addMenu("Style");
	const auto restyle = [this](std::function<void(QTextCharFormat &, const QTextCharFormat &)> change) {
		QTextCharFormat now = m_edit->currentCharFormat(), f;
		change(f, now);
		m_edit->mergeCurrentCharFormat(f);
	};
	style->addAction("Plain Text", QKeySequence(Qt::CTRL | Qt::Key_T), this, [restyle] {
		restyle([](QTextCharFormat &f, const QTextCharFormat &) {
			f.setFontWeight(QFont::Normal);
			f.setFontItalic(false);
			f.setFontUnderline(false);
		});
	});
	style->addSeparator();
	style->addAction("Bold", QKeySequence::Bold, this, [restyle] {
		restyle([](QTextCharFormat &f, const QTextCharFormat &now) {
			f.setFontWeight(now.fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
		});
	});
	style->addAction("Italic", QKeySequence::Italic, this, [restyle] {
		restyle([](QTextCharFormat &f, const QTextCharFormat &now) { f.setFontItalic(!now.fontItalic()); });
	});
	style->addAction("Underline", QKeySequence::Underline, this, [restyle] {
		restyle([](QTextCharFormat &f, const QTextCharFormat &now) { f.setFontUnderline(!now.fontUnderline()); });
	});

	QMenu *fontMenu = menuBar()->addMenu("Font");
	auto *fonts = new QActionGroup(fontMenu);
	for (const char *name : FONTS) {
		QAction *a = fontMenu->addAction(name, [name] {
			settings().setValue("font", name);
			for (ScribbleWindow *w : windows()) {
				w->applyFont();
			}
		});
		a->setCheckable(true);
		a->setChecked(settings().value("font", FONTS[0]).toString() == name);
		fonts->addAction(a);
	}
	QMenu *sizeMenu = menuBar()->addMenu("Size");
	auto *sizes = new QActionGroup(sizeMenu);
	for (int size : SIZES) {
		QAction *a = sizeMenu->addAction(QString("%1 Point").arg(size), [size] {
			settings().setValue("size", size);
			for (ScribbleWindow *w : windows()) {
				w->applyFont();
			}
		});
		a->setCheckable(true);
		a->setChecked(settings().value("size", 12).toInt() == size);
		sizes->addAction(a);
	}
}
