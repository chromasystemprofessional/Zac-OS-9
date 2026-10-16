#include "notepadwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int PAGE_COUNT = 8;

QString settingsPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/notepad.ini";
}

} // namespace

NotepadWindow::NotepadWindow(int page) {
	setAttribute(Qt::WA_DeleteOnClose);
	m_edit = new QPlainTextEdit(this);
	m_edit->setFrameShape(QFrame::NoFrame);
	setCentralWidget(m_edit);
	m_saveTimer = new QTimer(this);
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(250);
	connect(m_saveTimer, &QTimer::timeout, this, [this] { savePage(); });
	resize(420, 520);

	QMenu *file = menuBar()->addMenu("File");
	file->addAction("Close", QKeySequence::Close, this, &QWidget::close);
	file->addSeparator();
	file->addAction("Quit", QKeySequence::Quit, qApp, &QApplication::quit)
		->setMenuRole(QAction::NoRole);
	QMenu *edit = menuBar()->addMenu("Edit");
	edit->addAction("Undo", QKeySequence::Undo, m_edit, &QPlainTextEdit::undo);
	edit->addAction("Cut", QKeySequence::Cut, m_edit, &QPlainTextEdit::cut);
	edit->addAction("Copy", QKeySequence::Copy, m_edit, &QPlainTextEdit::copy);
	edit->addAction("Paste", QKeySequence::Paste, m_edit, &QPlainTextEdit::paste);
	edit->addAction("Select All", QKeySequence::SelectAll, m_edit, &QPlainTextEdit::selectAll);

	QMenu *pageMenu = menuBar()->addMenu("Page");
	pageMenu->addAction("Previous Page", QKeySequence(Qt::CTRL | Qt::Key_Up), this, [this] {
		showPage(m_page - 1);
	});
	pageMenu->addAction("Next Page", QKeySequence(Qt::CTRL | Qt::Key_Down), this, [this] {
		showPage(m_page + 1);
	});

	connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
		if (!m_loading) {
			m_saveTimer->start();
		}
	});
	showPage(page);
}

QString NotepadWindow::pageText(int page) const {
	if (page < 0 || page >= PAGE_COUNT) {
		return {};
	}
	QSettings s(settingsPath(), QSettings::IniFormat);
	return s.value(QString("page/%1").arg(page)).toString();
}

void NotepadWindow::setPageText(int page, const QString &text) {
	if (page < 0 || page >= PAGE_COUNT) {
		return;
	}
	QSettings s(settingsPath(), QSettings::IniFormat);
	s.setValue(QString("page/%1").arg(page), text);
	s.sync();
	if (s.status() != QSettings::NoError) {
		qWarning("Notepad: could not save page %d", page + 1);
	}
}

void NotepadWindow::savePage() {
	setPageText(m_page, m_edit->toPlainText());
}

void NotepadWindow::showPage(int page) {
	if (page < 0 || page >= PAGE_COUNT || (page == m_page && !windowTitle().isEmpty())) {
		return;
	}
	if (!windowTitle().isEmpty()) {
		m_saveTimer->stop();
		savePage();
	}
	m_page = page;
	m_loading = true;
	m_edit->setPlainText(pageText(page));
	m_loading = false;
	updateTitle();
}

void NotepadWindow::updateTitle() {
	setWindowTitle(QString("Notepad - Page %1 of %2").arg(m_page + 1).arg(PAGE_COUNT));
}

void NotepadWindow::closeEvent(QCloseEvent *event) {
	m_saveTimer->stop();
	savePage();
	event->accept();
}
