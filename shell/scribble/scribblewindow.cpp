#include "scribblewindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QMenuBar>
#include <QPlainTextEdit>
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
	m_edit = new QPlainTextEdit(this);
	m_edit->setFrameShape(QFrame::NoFrame);
	setCentralWidget(m_edit);
	resize(480, 360);
	buildMenus();
	applyFont();
	connect(m_edit->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
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
		"Text documents (*.txt *.text *.md *.log *.conf *.ini *.csv README* *);;All files (*)");
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
	m_edit->setPlainText(file.text);
	m_edit->document()->setModified(false);
	updateTitle();
	return true;
}

void ScribbleWindow::updateTitle() {
	setWindowTitle(m_path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_path).fileName());
}

bool ScribbleWindow::save() {
	if (m_path.isEmpty()) {
		return saveAs();
	}
	m_file.text = m_edit->toPlainText();
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
	edit->addAction("Undo", QKeySequence::Undo, m_edit, &QPlainTextEdit::undo);
	edit->addAction("Redo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), m_edit, &QPlainTextEdit::redo);
	edit->addSeparator();
	edit->addAction("Cut", QKeySequence::Cut, m_edit, &QPlainTextEdit::cut);
	edit->addAction("Copy", QKeySequence::Copy, m_edit, &QPlainTextEdit::copy);
	edit->addAction("Paste", QKeySequence::Paste, m_edit, &QPlainTextEdit::paste);
	edit->addAction("Clear", this, [this] { m_edit->textCursor().removeSelectedText(); });
	edit->addAction("Select All", QKeySequence::SelectAll, m_edit, &QPlainTextEdit::selectAll);
	edit->addSeparator();
	edit->addAction("Find...", QKeySequence::Find, this, [this] { find(false); });
	edit->addAction("Find Again", QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] { find(true); });

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
