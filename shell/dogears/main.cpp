/*
 * zacos9-dogears: Dog Ears, ZacOS 9's Stickies (Missing Manual ch. 13).
 * Coloured notes, each its own small window; they are saved as you type,
 * along with where they are, and come back the next time. Closing a note
 * throws it away (after offering to save its text in a file).
 */
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "alert.h"
#include "notes.h"
#include "platinumshell.h"
#include "settings.h"
#include "textfile.h"

namespace {

class NoteWindow;

/* All the notes, and saving them a moment after any change. */
class DogEars {
public:
	static DogEars &instance() {
		static DogEars d;
		return d;
	}
	NoteWindow *newNote(const Note &note);
	void changed() { m_save.start(); }
	void remove(NoteWindow *w);
	void saveNow();
	void quit();

private:
	DogEars() {
		m_save.setSingleShot(true);
		m_save.setInterval(500);
		m_save.callOnTimeout([this] { saveNow(); });
	}
	QList<NoteWindow *> m_notes;
	QTimer m_save;
	bool m_quitting = false;
};

class NoteWindow : public QMainWindow {
public:
	explicit NoteWindow(const Note &note) : m_note(note) {
		setAttribute(Qt::WA_DeleteOnClose);
		m_edit = new QPlainTextEdit(this);
		m_edit->setFrameShape(QFrame::NoFrame);
		m_edit->setPlainText(note.text);
		setCentralWidget(m_edit);
		resize(note.size);
		applyColor();
		setWindowTitle(noteTitle(note.text));
		connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
			m_note.text = m_edit->toPlainText();
			setWindowTitle(noteTitle(m_note.text));
			DogEars::instance().changed();
		});
		buildMenus();
	}

	const Note &note() const { return m_note; }
	void forget() { m_forget = true; } /* quitting: close without asking */

	void placed() {
		if (m_note.hasPosition) {
			platinumSetWindowPosition(this, m_note.position);
		}
		platinumOnWindowPosition(this, [this](QPoint p) {
			m_note.position = p;
			m_note.hasPosition = true;
			DogEars::instance().changed();
		});
	}

protected:
	void resizeEvent(QResizeEvent *e) override {
		QMainWindow::resizeEvent(e);
		m_note.size = size();
		DogEars::instance().changed();
	}

	/* The close box throws the note away: as on the Mac, it offers to
	 * keep the text in a file first. */
	void closeEvent(QCloseEvent *e) override {
		if (!m_forget && !m_note.text.trimmed().isEmpty()) {
			switch (Alert::choose("Closing this note throws it away. Save its text in a file first?",
					"Save…", "Cancel", "Don't Save")) {
			case Alert::Cancel:
				e->ignore();
				return;
			case Alert::Ok:
				if (!exportText()) {
					e->ignore();
					return;
				}
				break;
			default:
				break;
			}
		}
		if (!m_forget) {
			DogEars::instance().remove(this);
		}
		e->accept();
	}

private:
	void applyColor() {
		QPalette p = m_edit->palette();
		p.setColor(QPalette::Base, QColor::fromRgb(noteColorRgb(m_note.color)));
		p.setColor(QPalette::Text, Qt::black);
		m_edit->setPalette(p);
	}

	bool exportText() {
		const QString path = QFileDialog::getSaveFileName(this, "Export Text",
			QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" +
				noteTitle(m_note.text).remove(QChar(0x2026)).replace('/', '-') + ".txt");
		if (path.isEmpty()) {
			return false;
		}
		QString error;
		if (!writeTextFile(path, TextFile{ m_note.text, LineEnding::Lf }, &error)) {
			Alert::ask("The note's text could not be saved. " + error, "OK", QString());
			return false;
		}
		return true;
	}

	void importText() {
		const QString path = QFileDialog::getOpenFileName(this, "Import Text",
			QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
		if (path.isEmpty()) {
			return;
		}
		TextFile file;
		QString error;
		if (!readTextFile(path, &file, &error)) {
			Alert::ask("That file could not be read. " + error, "OK", QString());
			return;
		}
		Note note;
		note.text = file.text;
		note.color = m_note.color;
		DogEars::instance().newNote(note);
	}

	void buildMenus() {
		/* Exported to ZacOS 9's menu bar, as other programs' are. */
		QMenu *file = menuBar()->addMenu("File");
		file->addAction("New Note", QKeySequence::New, [this] {
			Note note;
			note.color = m_note.color;
			DogEars::instance().newNote(note);
		});
		file->addAction("Close Note", QKeySequence::Close, this, [this] { close(); });
		file->addSeparator();
		file->addAction("Import Text...", this, [this] { importText(); });
		file->addAction("Export Text...", this, [this] { exportText(); });
		file->addSeparator();
		file->addAction("Quit", QKeySequence::Quit, [] { DogEars::instance().quit(); })
			->setMenuRole(QAction::NoRole);

		QMenu *edit = menuBar()->addMenu("Edit");
		edit->addAction("Undo", QKeySequence::Undo, m_edit, &QPlainTextEdit::undo);
		edit->addSeparator();
		edit->addAction("Cut", QKeySequence::Cut, m_edit, &QPlainTextEdit::cut);
		edit->addAction("Copy", QKeySequence::Copy, m_edit, &QPlainTextEdit::copy);
		edit->addAction("Paste", QKeySequence::Paste, m_edit, &QPlainTextEdit::paste);
		edit->addAction("Clear", this, [this] { m_edit->textCursor().removeSelectedText(); });
		edit->addAction("Select All", QKeySequence::SelectAll, m_edit, &QPlainTextEdit::selectAll);

		QMenu *color = menuBar()->addMenu("Color");
		auto *group = new QActionGroup(color);
		for (int i = 0; i < NOTE_COLORS; i++) {
			QAction *a = color->addAction(noteColorName(static_cast<NoteColor>(i)), this, [this, i] {
				m_note.color = static_cast<NoteColor>(i);
				applyColor();
				DogEars::instance().changed();
			});
			a->setCheckable(true);
			a->setChecked(static_cast<int>(m_note.color) == i);
			group->addAction(a);
		}
	}

	Note m_note;
	QPlainTextEdit *m_edit = nullptr;
	bool m_forget = false;
};

NoteWindow *DogEars::newNote(const Note &note) {
	auto *w = new NoteWindow(note);
	m_notes << w;
	w->show();
	w->placed();
	w->activateWindow();
	changed();
	return w;
}

void DogEars::remove(NoteWindow *w) {
	m_notes.removeOne(w);
	saveNow();
	if (m_notes.isEmpty() && !m_quitting) {
		qApp->quit();
	}
}

void DogEars::saveNow() {
	m_save.stop();
	QList<Note> notes;
	for (NoteWindow *w : m_notes) {
		notes << w->note();
	}
	saveNotes(notesPath(), notes);
}

/* Quitting keeps every note for next time. */
void DogEars::quit() {
	m_quitting = true;
	saveNow();
	for (NoteWindow *w : m_notes) {
		w->forget();
	}
	qApp->quit();
}

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Dog Ears");
	QApplication::setQuitOnLastWindowClosed(false);
	QGuiApplication::setDesktopFileName("zacos9-dogears"); /* Wayland app_id */
	platinumShellInit();
	QList<Note> notes = loadNotes(notesPath());
	if (notes.isEmpty()) {
		Note welcome;
		welcome.text = "Welcome to Dog Ears.\n\nType a note here. Notes keep themselves, "
			"and come back the next time you open Dog Ears.\n\nFile > New Note makes another; "
			"the Color menu changes this one's colour.";
		welcome.size = QSize(260, 210);
		notes << welcome;
	}
	for (const Note &n : notes) {
		DogEars::instance().newNote(n);
	}
	QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { DogEars::instance().saveNow(); });
	return app.exec();
}
