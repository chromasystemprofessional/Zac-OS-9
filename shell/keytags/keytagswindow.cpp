#include "keytagswindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "xkblayouts.h"

namespace {

/* Linux key codes (linux/input-event-codes.h). */
enum {
	K_GRAVE = 41, K_1 = 2, K_EQUAL = 13, K_BACKSPACE = 14, K_TAB = 15, K_Q = 16,
	K_RIGHTBRACE = 27, K_BACKSLASH = 43, K_CAPSLOCK = 58, K_A = 30, K_APOSTROPHE = 40,
	K_ENTER = 28, K_LEFTSHIFT = 42, K_102ND = 86, K_Z = 44, K_SLASH = 53,
	K_RIGHTSHIFT = 54, K_LEFTCTRL = 29, K_LEFTALT = 56, K_LEFTMETA = 125, K_SPACE = 57,
	K_RIGHTMETA = 126, K_RIGHTALT = 100, K_RIGHTCTRL = 97,
};

/* One key in the picture: its code, width in key widths, and its name if
 * it types nothing (empty: it shows its character). */
struct KeyCap {
	int key;
	double width;
	const char *name;
};

/* An extended keyboard's main keys, with the ISO key left of Z. */
QVector<QVector<KeyCap>> rows() {
	QVector<QVector<KeyCap>> r(5);
	r[0] << KeyCap{ K_GRAVE, 1, "" };
	for (int k = K_1; k <= K_EQUAL; k++) {
		r[0] << KeyCap{ k, 1, "" };
	}
	r[0] << KeyCap{ K_BACKSPACE, 2, "delete" };
	r[1] << KeyCap{ K_TAB, 1.5, "tab" };
	for (int k = K_Q; k <= K_RIGHTBRACE; k++) {
		r[1] << KeyCap{ k, 1, "" };
	}
	r[1] << KeyCap{ K_BACKSLASH, 1.5, "" };
	r[2] << KeyCap{ K_CAPSLOCK, 1.75, "caps lock" };
	for (int k = K_A; k <= K_APOSTROPHE; k++) {
		r[2] << KeyCap{ k, 1, "" };
	}
	r[2] << KeyCap{ K_ENTER, 2.25, "return" };
	r[3] << KeyCap{ K_LEFTSHIFT, 1.25, "shift" } << KeyCap{ K_102ND, 1, "" };
	for (int k = K_Z; k <= K_SLASH; k++) {
		r[3] << KeyCap{ k, 1, "" };
	}
	r[3] << KeyCap{ K_RIGHTSHIFT, 2.75, "shift" };
	r[4] << KeyCap{ K_LEFTCTRL, 1.5, "control" } << KeyCap{ K_LEFTALT, 1.25, "option" }
		<< KeyCap{ K_LEFTMETA, 1.5, "⌘" } << KeyCap{ K_SPACE, 6.5, "" }
		<< KeyCap{ K_RIGHTMETA, 1.5, "⌘" } << KeyCap{ K_RIGHTALT, 1.25, "option" }
		<< KeyCap{ K_RIGHTCTRL, 1.5, "control" };
	return r;
}

constexpr int UNIT = 30;   /* one key's width and height, px */
constexpr int COLUMNS = 15;

} // namespace

/* The keyboard: keys held are drawn down, dead keys outlined. */
class KeyboardView : public QWidget {
public:
	explicit KeyboardView(KeyTagsWindow *window) : QWidget(window), m_window(window) {
		const QVector<QVector<KeyCap>> keyboard = rows();
		for (int r = 0; r < keyboard.size(); r++) {
			double x = 0;
			for (const KeyCap &cap : keyboard[r]) {
				m_caps.append({ cap, QRect(qRound(x * UNIT), r * UNIT, qRound(cap.width * UNIT),
					UNIT).adjusted(1, 1, -1, -1) });
				x += cap.width;
			}
		}
		setFixedSize(COLUMNS * UNIT + 1, 5 * UNIT + 1);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		QFont small = font();
		small.setPointSizeF(font().pointSizeF() * 0.8);
		const int level = m_window->level();
		for (const auto &[cap, rect] : m_caps) {
			const bool down = m_window->isDown(cap.key) || cap.key == m_pressed;
			p.setPen(Qt::black);
			p.setBrush(down ? QColor(Qt::black) : QColor(0xdd, 0xdd, 0xdd));
			p.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
			p.setPen(down ? Qt::white : Qt::black);
			if (*cap.name) {
				p.setFont(small);
				p.drawText(rect, Qt::AlignCenter, QString::fromUtf8(cap.name));
				continue;
			}
			const KeyTags::Tag tag = m_window->tags().tag(cap.key, level);
			p.setFont(font());
			if (tag.dead) {
				/* Outlined, as Key Caps showed keys that add an accent. */
				p.setBrush(Qt::NoBrush);
				p.drawRect(QRectF(rect).adjusted(5.5, 5.5, -5.5, -5.5));
			}
			p.drawText(rect, Qt::AlignCenter, tag.text);
		}
	}

	void mousePressEvent(QMouseEvent *event) override {
		m_pressed = keyAt(event->pos());
		update();
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		const int key = m_pressed;
		m_pressed = -1;
		update();
		if (key >= 0 && keyAt(event->pos()) == key) {
			m_window->click(key);
		}
	}

private:
	int keyAt(QPoint pos) const {
		for (const auto &[cap, rect] : m_caps) {
			if (rect.contains(pos)) {
				return cap.key;
			}
		}
		return -1;
	}

	KeyTagsWindow *m_window;
	QVector<QPair<KeyCap, QRect>> m_caps;
	int m_pressed = -1;
};

KeyTagsWindow::KeyTagsWindow() {
	setWindowTitle("Key Tags");
	auto *central = new QWidget(this);
	auto *column = new QVBoxLayout(central);
	column->setContentsMargins(12, 12, 12, 10);
	column->setSpacing(10);
	m_line = new QLineEdit(central);
	m_line->installEventFilter(this);
	column->addWidget(m_line);
	m_view = new KeyboardView(this);
	column->addWidget(m_view);
	auto *hint = new QLabel("Hold Shift or Option (Alt Gr) to see their characters. "
		"Outlined keys add an accent to the next letter.", central);
	hint->setWordWrap(true);
	column->addWidget(hint);
	setCentralWidget(central);

	QMenu *file = menuBar()->addMenu("File");
	file->addAction("Close", QKeySequence::Close, this, &QWidget::close);
	file->addSeparator();
	file->addAction("Quit", QKeySequence::Quit, qApp, &QApplication::quit)
		->setMenuRole(QAction::NoRole);
	QMenu *edit = menuBar()->addMenu("Edit");
	edit->addAction("Undo", QKeySequence::Undo, m_line, &QLineEdit::undo);
	edit->addSeparator();
	edit->addAction("Cut", QKeySequence::Cut, m_line, &QLineEdit::cut);
	edit->addAction("Copy", QKeySequence::Copy, m_line, &QLineEdit::copy);
	edit->addAction("Paste", QKeySequence::Paste, m_line, &QLineEdit::paste);
	edit->addAction("Clear", m_line, &QLineEdit::clear);
	edit->addAction("Select All", QKeySequence::SelectAll, m_line, &QLineEdit::selectAll);
	m_layoutMenu = menuBar()->addMenu("Layout");
	m_layoutGroup = new QActionGroup(this);
	fillLayoutMenu();
	m_line->setFocus();
}

void KeyTagsWindow::fillLayoutMenu() {
	QHash<QString, QString> names;
	for (const XkbLayout &l : xkbLayouts()) {
		names.insert(l.code, l.name);
	}
	for (const QString &code : xkbLayoutsInUse()) {
		QAction *action = m_layoutMenu->addAction(names.value(code, code));
		action->setData(code);
		action->setCheckable(true);
		m_layoutGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, code] { showLayout(code); });
	}
}

bool KeyTagsWindow::showLayout(const QString &code) {
	if (!m_tags.load(code)) {
		return false;
	}
	for (QAction *action : m_layoutGroup->actions()) {
		action->setChecked(action->data().toString() == code);
	}
	m_view->update();
	return true;
}

/* The keys labelled option, and any the layout makes third-level keys:
 * Linux reads Option as Alt, but in the picture it stands for the third
 * level, as the Mac's Option key gave its extra characters. */
bool KeyTagsWindow::isOptionCap(int key) const {
	return key == K_LEFTALT || key == K_RIGHTALT || m_tags.isOption(key);
}

int KeyTagsWindow::level() const {
	bool shift = m_shiftLatch, option = m_optionLatch;
	for (int key : m_held) {
		shift |= m_tags.isShift(key);
		option |= m_tags.isOption(key);
	}
	return (shift ? 1 : 0) + (option ? 2 : 0);
}

bool KeyTagsWindow::isDown(int key) const {
	return m_held.contains(key) || (m_shiftLatch && m_tags.isShift(key)) ||
		(m_optionLatch && isOptionCap(key));
}

void KeyTagsWindow::keyEvent(int key, bool down) {
	if (down) {
		m_held.insert(key);
	} else {
		m_held.remove(key);
	}
	m_view->update();
}

void KeyTagsWindow::click(int key) {
	if (m_tags.isShift(key)) {
		m_shiftLatch = !m_shiftLatch;
	} else if (isOptionCap(key)) {
		m_optionLatch = !m_optionLatch;
	} else if (key == K_SPACE) {
		m_line->insert(" ");
	} else {
		m_line->insert(m_tags.tag(key, level()).text);
	}
	m_view->update();
}

QString KeyTagsWindow::typed() const {
	return m_line->text();
}

bool KeyTagsWindow::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_line) {
		if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
			auto *key = static_cast<QKeyEvent *>(event);
			/* zacos9-wm's scan codes are xkb's: Linux's plus 8. */
			if (!key->isAutoRepeat() && key->nativeScanCode() > 8) {
				keyEvent(int(key->nativeScanCode()) - 8, event->type() == QEvent::KeyPress);
			}
		} else if (event->type() == QEvent::FocusOut) {
			m_held.clear();
			m_view->update();
		}
	}
	return QMainWindow::eventFilter(watched, event);
}
