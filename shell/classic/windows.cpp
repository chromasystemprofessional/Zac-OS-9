/*
 * zacos9-classic-windows: show Classic Window Agent frames as ordinary ZacOS
 * toplevels.
 *
 * The guest agent writes ClassicWindows.txt and ZacWin-<serial>.ppm into the
 * shared home folder. This frontend polls that inventory, creates one window
 * per captured guest window, and gives the compositor a normal title. It does
 * not forward input or close guest windows yet; Show Classic Desktop hides
 * the exported windows so the emulator window is usable.
 */
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRect>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QStringList>
#include <QTimer>

#include <csignal>
#include <unistd.h>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "toplevels.h"

namespace {

struct GuestWindow {
	int serial = 0;
	QString title;
	QString app;
	bool captured = false;
	bool visible = false;
	int error = 0;
	int kind = 0;
	bool goAway = false;
	QRect global;
	QRect frame;
	QString framePath;
};

QVector<int> rectNumbers(const QString &line, const char *name) {
	const QRegularExpression re(QString("%1=(-?[0-9]+),(-?[0-9]+),(-?[0-9]+),(-?[0-9]+)").arg(name));
	const auto match = re.match(line);
	if (!match.hasMatch())
		return {};
	return { match.captured(1).toInt(), match.captured(2).toInt(),
		match.captured(3).toInt(), match.captured(4).toInt() };
}

QString inputDirectory() {
	return QDir::homePath() + "/zacos9-classic/input";
}

qint64 nextInputId() {
	static qint64 id = [] {
		qint64 next = QDateTime::currentSecsSinceEpoch();
		const QDir directory(inputDirectory());
		for (const QString &name : directory.entryList({ "event.*.evt" }, QDir::Files)) {
			bool ok = false;
			const qint64 value = name.section('.', 1, 1).toLongLong(&ok);
			if (ok && value > next)
				next = value;
		}
		return next;
	}();
	return ++id;
}

void sendInput(const QString &body) {
	const QString directory = inputDirectory();
	QDir().mkpath(directory);
	const qint64 id = nextInputId();
	const QString temporary = QString("%1/.event.%2.tmp").arg(directory).arg(id);
	const QString target = QString("%1/event.%2.evt").arg(directory).arg(id);
	QFile file(temporary);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;
	file.write(QString("version=1\nid=%1\n%2").arg(id).arg(body).toUtf8());
	file.close();
	QFile::remove(target);
	QFile::rename(temporary, target);
}

void sendMouse(int x, int y, const char *action, int button = 0) {
	sendInput(QString("type=mouse\naction=%1\nx=%2\ny=%3\nbutton=%4\n")
		.arg(action).arg(x).arg(y).arg(button));
}

void sendKey(int code, const char *action) {
	sendInput(QString("type=key\naction=%1\ncode=%2\n").arg(action).arg(code));
}

int macKeyCode(QKeyEvent *event) {
	switch (event->key()) {
		case Qt::Key_Shift: return 0x38;
		case Qt::Key_Control: return 0x36;
		case Qt::Key_Alt: return 0x3A;
		case Qt::Key_Meta: return 0x37;
		case Qt::Key_CapsLock: return 0x39;
		case Qt::Key_Return:
		case Qt::Key_Enter: return 0x24;
		case Qt::Key_Tab: return 0x30;
		case Qt::Key_Space: return 0x31;
		case Qt::Key_Backspace: return 0x33;
		case Qt::Key_Escape: return 0x35;
		case Qt::Key_Delete: return 0x75;
		case Qt::Key_Home: return 0x73;
		case Qt::Key_End: return 0x77;
		case Qt::Key_PageUp: return 0x74;
		case Qt::Key_PageDown: return 0x79;
		case Qt::Key_Left: return 0x7B;
		case Qt::Key_Right: return 0x7C;
		case Qt::Key_Down: return 0x7D;
		case Qt::Key_Up: return 0x7E;
		case Qt::Key_F1: return 0x7A;
		case Qt::Key_F2: return 0x78;
		case Qt::Key_F3: return 0x63;
		case Qt::Key_F4: return 0x76;
		case Qt::Key_F5: return 0x60;
		case Qt::Key_F6: return 0x61;
		case Qt::Key_F7: return 0x62;
		case Qt::Key_F8: return 0x64;
		case Qt::Key_F9: return 0x65;
		case Qt::Key_F10: return 0x6D;
		case Qt::Key_F11: return 0x67;
		case Qt::Key_F12: return 0x6F;
		default: break;
	}
	if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
		static const int letters[26] = {
			0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26,
			0x28, 0x25, 0x2E, 0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11,
			0x20, 0x09, 0x0D, 0x07, 0x10, 0x06
		};
		return letters[event->key() - Qt::Key_A];
	}
	if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
		static const int digits[10] = { 0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19 };
		return digits[event->key() - Qt::Key_0];
	}
	switch (event->key()) {
		case Qt::Key_Minus: return 0x1B;
		case Qt::Key_Equal: return 0x18;
		case Qt::Key_BracketLeft: return 0x21;
		case Qt::Key_BracketRight: return 0x1E;
		case Qt::Key_Semicolon: return 0x29;
		case Qt::Key_Apostrophe: return 0x27;
		case Qt::Key_Comma: return 0x2B;
		case Qt::Key_Period: return 0x2F;
		case Qt::Key_Slash: return 0x2C;
		case Qt::Key_Backslash: return 0x2A;
		case Qt::Key_QuoteLeft: return 0x32;
		/* Shifted symbols report their own keys on some platforms. */
		case Qt::Key_Exclam: return 0x12;
		case Qt::Key_At: return 0x13;
		case Qt::Key_NumberSign: return 0x14;
		case Qt::Key_Dollar: return 0x15;
		case Qt::Key_Percent: return 0x17;
		case Qt::Key_AsciiCircum: return 0x16;
		case Qt::Key_Ampersand: return 0x1A;
		case Qt::Key_Asterisk: return 0x1C;
		case Qt::Key_ParenLeft: return 0x19;
		case Qt::Key_ParenRight: return 0x1D;
		case Qt::Key_Underscore: return 0x1B;
		case Qt::Key_Plus: return 0x18;
		case Qt::Key_BraceLeft: return 0x21;
		case Qt::Key_BraceRight: return 0x1E;
		case Qt::Key_Colon: return 0x29;
		case Qt::Key_QuoteDbl: return 0x27;
		case Qt::Key_Less: return 0x2B;
		case Qt::Key_Greater: return 0x2F;
		case Qt::Key_Question: return 0x2C;
		case Qt::Key_Bar: return 0x2A;
		case Qt::Key_AsciiTilde: return 0x32;
		default: return -1;
	}
}

QString inventoryPath() {
	const QByteArray override = qgetenv("ZACOS9_CLASSIC_WINDOWS");
	if (!override.isEmpty())
		return QString::fromLocal8Bit(override);
	return QDir::homePath() + "/ClassicWindows.txt";
}

QString field(const QString &line, const char *name) {
	const QRegularExpression re(QString("%1=\"([^\"]*)\"").arg(name));
	const auto match = re.match(line);
	return match.hasMatch() ? match.captured(1) : QString();
}

int number(const QString &line, const char *name) {
	const QRegularExpression re(QString("(?:^| )%1=(-?[0-9]+)").arg(name));
	const auto match = re.match(line);
	return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QVector<GuestWindow> readInventory() {
	QVector<GuestWindow> windows;
	QFile file(inventoryPath());
	if (!file.open(QIODevice::ReadOnly))
		return windows;
	const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
	bool versioned = false;
	for (const QString &line : lines) {
		if (line == "version=1")
			versioned = true;
		if (!line.startsWith("window="))
			continue;
		GuestWindow window;
		window.serial = number(line, "serial");
		window.title = field(line, "title");
		window.app = field(line, "app");
		window.captured = number(line, "capture") != 0;
		window.visible = number(line, "visible") != 0;
		window.error = number(line, "error");
		window.kind = number(line, "kind");
		window.goAway = number(line, "goAway") != 0;
		{
			const QVector<int> global = rectNumbers(line, "global");
			if (global.size() == 4)
				window.global = QRect(global[0], global[1],
					global[2] - global[0], global[3] - global[1]);
			const QVector<int> frame = rectNumbers(line, "frame");
			if (frame.size() == 4)
				window.frame = QRect(frame[0], frame[1],
					frame[2] - frame[0], frame[3] - frame[1]);
		}
		if (window.serial >= 0)
			window.framePath = QDir::homePath() +
				QString("/ZacWin-%1.ppm").arg(window.serial);
		windows.append(window);
	}
	if (!versioned)
		windows.clear();
	return windows;
}

/* Minimal P6 reader: the agent writes a comment after the magic. */
QImage readPpm(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QImage();
	const QByteArray data = file.readAll();
	int pos = 0;
	auto token = [&data, &pos]() {
		while (pos < data.size()) {
			const char c = data.at(pos);
			if (c == '#') {
				while (pos < data.size() && data.at(pos) != '\n')
					pos++;
			} else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				pos++;
			} else {
				break;
			}
		}
		const int start = pos;
		while (pos < data.size()) {
			const char c = data.at(pos);
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
				break;
			pos++;
		}
		return data.mid(start, pos - start);
	};
	if (token() != "P6")
		return QImage();
	const int width = token().toInt();
	const int height = token().toInt();
	const int max = token().toInt();
	if (width < 1 || height < 1 || width > 4096 || height > 4096 || max != 255)
		return QImage();
	while (pos < data.size() && (data.at(pos) == ' ' || data.at(pos) == '\t' ||
			data.at(pos) == '\r' || data.at(pos) == '\n'))
		pos++;
	const int stride = width * 3;
	if (data.size() - pos < stride * height)
		return QImage();
	return QImage(reinterpret_cast<const uchar *>(data.constData() + pos),
		width, height, stride, QImage::Format_RGB888).copy();
}

class WindowViewer : public QWidget {
public:
	WindowViewer(const GuestWindow &window, std::function<void()> showDesktop,
			QWidget *parent = nullptr)
		: QWidget(parent), m_serial(window.serial), m_showDesktop(std::move(showDesktop)) {
		setAttribute(Qt::WA_DeleteOnClose);
		setFocusPolicy(Qt::StrongFocus);
		setWindowTitle(window.title.isEmpty() ? window.app : window.title);
		setToolTip(window.app);
		m_image = new QLabel(this);
		m_image->setAlignment(Qt::AlignCenter);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(m_image);
		auto *desktop = new QAction(tr("Show Classic Desktop"), this);
		connect(desktop, &QAction::triggered, this, [this] { m_showDesktop(); });
		addAction(desktop);
		auto *resize = new QAction(tr("Resize in Classic…"), this);
		connect(resize, &QAction::triggered, this, [this] { requestResize(); });
		addAction(resize);
		auto *close = new QAction(tr("Close in Classic"), this);
		connect(close, &QAction::triggered, this, [this] { requestClose(); });
		addAction(close);
		auto *hideAction = new QAction(tr("Hide This Window"), this);
		connect(hideAction, &QAction::triggered, this, [this] { hide(); });
		addAction(hideAction);
		setContextMenuPolicy(Qt::ActionsContextMenu);
		auto *shortcut = new QShortcut(QKeySequence("Ctrl+Alt+D"), this);
		connect(shortcut, &QShortcut::activated, this,
			[this] { m_showDesktop(); });
		auto *resizeShortcut = new QShortcut(QKeySequence("Ctrl+Alt+R"), this);
		connect(resizeShortcut, &QShortcut::activated, this,
			[this] { requestResize(); });
	}

	int serial() const { return m_serial; }

	void updateInfo(const GuestWindow &window) {
		m_global = window.global;
		m_frame = window.frame;
		m_goAway = window.goAway;
		setWindowTitle(window.title.isEmpty() ? window.app : window.title);
		setToolTip(window.app);
	}

	void requestResize() {
		if (m_frame.isNull() || !m_guestSize.isValid())
			return;
		bool ok = false;
		const int width = QInputDialog::getInt(this, tr("Resize in Classic"),
			tr("Content width:"), m_guestSize.width(), 64, 1024, 1, &ok);
		if (!ok)
			return;
		const int height = QInputDialog::getInt(this, tr("Resize in Classic"),
			tr("Content height:"), m_guestSize.height(), 64, 768, 1, &ok);
		if (!ok)
			return;
		const QSize delta(width - m_guestSize.width(), height - m_guestSize.height());
		if (delta.isNull())
			return;
		const int x = m_frame.right() - 12;
		const int y = m_frame.bottom() - 12;
		sendMouse(x, y, "move");
		sendMouse(x, y, "down");
		for (int step = 1; step <= 8; step++) {
			sendMouse(x + delta.width() * step / 8,
				y + delta.height() * step / 8, "move");
		}
		sendMouse(x + delta.width(), y + delta.height(), "up");
	}

	void requestClose() {
		if (!m_goAway || m_closePending || m_frame.isNull())
			return;
		m_closePending = true;
		const int x = m_frame.left() + 10;
		const int y = m_frame.top() + 12;
		sendMouse(x, y, "move");
		sendMouse(x, y, "down");
		sendMouse(x, y, "up");
		hide();
		QTimer::singleShot(1500, this, [this] { m_closePending = false; });
	}

	void updateFrame(const QString &path) {
		const QFileInfo info(path);
		if (!info.exists())
			return;
		const QDateTime modified = info.lastModified();
		if (m_path == path && modified == m_modified)
			return;
		const QImage image = readPpm(path);
		if (image.isNull())
			return;
		m_path = path;
		m_modified = modified;
		m_image->setPixmap(QPixmap::fromImage(image));
		m_guestSize = image.size();
		setFixedSize(image.size());
	}

protected:
	void mousePressEvent(QMouseEvent *event) override {
		setFocus(Qt::MouseFocusReason);
		const QPoint point = event->position().toPoint();
		const int x = m_global.left() + point.x();
		const int y = m_global.top() + point.y();
		sendMouse(x, y, "move");
		sendMouse(x, y, "down");
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (!(event->buttons() & Qt::LeftButton))
			return;
		const QPoint point = event->position().toPoint();
		sendMouse(m_global.left() + point.x(),
			m_global.top() + point.y(), "move");
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		const QPoint point = event->position().toPoint();
		const int x = m_global.left() + point.x();
		const int y = m_global.top() + point.y();
		sendMouse(x, y, "move");
		sendMouse(x, y, "up");
	}

	void mouseDoubleClickEvent(QMouseEvent *event) override {
		const QPoint point = event->position().toPoint();
		const int x = m_global.left() + point.x();
		const int y = m_global.top() + point.y();
		sendMouse(x, y, "move");
		sendMouse(x, y, "down");
		sendMouse(x, y, "up");
	}

	void wheelEvent(QWheelEvent *event) override {
		const int key = event->angleDelta().y() >= 0 ? 0x7E : 0x7D;
		sendKey(key, "down");
		sendKey(key, "up");
	}

	void keyPressEvent(QKeyEvent *event) override {
		if (event->isAutoRepeat())
			return;
		const int code = macKeyCode(event);
		if (code >= 0)
			sendKey(code, "down");
	}

	void keyReleaseEvent(QKeyEvent *event) override {
		if (event->isAutoRepeat())
			return;
		const int code = macKeyCode(event);
		if (code >= 0)
			sendKey(code, "up");
	}

protected:
	void closeEvent(QCloseEvent *event) override {
		if (m_goAway) {
			requestClose();
			event->ignore();
		} else {
			event->accept();
		}
	}

private:
	int m_serial;
	std::function<void()> m_showDesktop;
	QRect m_global;
	QRect m_frame;
	bool m_goAway = false;
	bool m_closePending = false;
	QSize m_guestSize;
	QString m_path;
	QDateTime m_modified;
	QLabel *m_image = nullptr;
};

class Controller : public QWidget {
public:
	Controller() {
		setWindowTitle(tr("Classic Windows"));
		m_list = new QListWidget(this);
		m_status = new QLabel(this);
		m_status->setWordWrap(true);
		auto *desktop = new QPushButton(tr("Show Classic Desktop"), this);
		auto *windows = new QPushButton(tr("Show Exported Windows"), this);
		auto *resize = new QPushButton(tr("Resize"), this);
		auto *close = new QPushButton(tr("Close in Classic"), this);
		auto *refresh = new QPushButton(tr("Refresh"), this);
		auto *buttons = new QHBoxLayout;
		buttons->addWidget(desktop);
		buttons->addWidget(windows);
		buttons->addWidget(resize);
		buttons->addWidget(close);
		buttons->addWidget(refresh);
		auto *layout = new QVBoxLayout(this);
		layout->addWidget(m_status);
		layout->addWidget(m_list);
		layout->addLayout(buttons);
		connect(desktop, &QPushButton::clicked, this, [this] { showDesktop(); });
		connect(windows, &QPushButton::clicked, this, [this] { showWindows(); });
		connect(resize, &QPushButton::clicked, this, [this] { resizeSelected(); });
		connect(close, &QPushButton::clicked, this, [this] { closeSelected(); });
		connect(refresh, &QPushButton::clicked, this, [this] { poll(); });
		setFixedSize(620, 320);
	}

	void poll() {
		const QVector<GuestWindow> windows = readInventory();
		if (windows.isEmpty()) {
			m_status->setText(tr("No Classic Window Agent inventory. Start Classic with "
				"ClassicWindowAgent in Startup Items."));
		} else {
			m_status->setText(tr("%1 guest window(s)").arg(windows.size()));
		}
		QSet<int> seen;
		for (const GuestWindow &window : windows) {
			seen.insert(window.serial);
			if (!window.captured || !window.visible || window.kind != 8 ||
					m_ignored.contains(window.serial)) {
				if (auto *viewer = m_viewers.value(window.serial))
					viewer->hide();
				continue;
			}
			WindowViewer *viewer = m_viewers.value(window.serial);
			if (!viewer) {
				viewer = new WindowViewer(window, [this] { showDesktop(); });
				m_viewers.insert(window.serial, viewer);
				connect(viewer, &QObject::destroyed, this, [this, window] {
					m_viewers.remove(window.serial);
					m_ignored.insert(window.serial);
				});
			}
			viewer->updateInfo(window);
			viewer->updateFrame(window.framePath);
			if (m_showing) {
				viewer->show();
				if (!viewer->property("raised").toBool()) {
					viewer->setProperty("raised", true);
					viewer->raise();
					viewer->activateWindow();
				}
			}
		}
		for (auto it = m_viewers.begin(); it != m_viewers.end();) {
			if (!seen.contains(it.key())) {
				it.value()->deleteLater();
				it = m_viewers.erase(it);
			} else {
				++it;
			}
		}
		for (int serial : QSet<int>(m_ignored)) {
			bool present = false;
			for (const GuestWindow &window : windows)
				present = present || window.serial == serial;
			if (!present)
				m_ignored.remove(serial);
		}
		m_list->clear();
		for (const GuestWindow &window : windows) {
			if (!window.visible)
				continue;
			QString text = QString("%1 — %2").arg(window.app,
				window.title.isEmpty() ? tr("untitled") : window.title);
			if (!window.captured || window.kind != 8)
				text += tr("  [Classic desktop only]");
			else if (window.error)
				text += tr("  [partial: Mac error %1]").arg(window.error);
			auto *item = new QListWidgetItem(text);
			item->setData(Qt::UserRole, window.serial);
			m_list->addItem(item);
		}
		if (m_list->count() > 0 && !m_list->currentItem())
			m_list->setCurrentRow(0);
	}

private:
	void showDesktop() {
		m_showing = false;
		for (WindowViewer *viewer : m_viewers)
			viewer->hide();
		if (!Toplevels::activate("SheepShaver"))
			Toplevels::activate("BasiliskII");
	}
	void showWindows() {
		m_showing = true;
		m_ignored.clear();
		for (WindowViewer *viewer : m_viewers) {
			viewer->show();
			viewer->raise();
			viewer->activateWindow();
		}
	}
	void resizeSelected() {
		auto *item = m_list->currentItem();
		int serial = item ? item->data(Qt::UserRole).toInt() : -1;
		if (serial < 0 && m_viewers.size() == 1)
			serial = m_viewers.firstKey();
		if (auto *viewer = m_viewers.value(serial))
			viewer->requestResize();
	}

	void closeSelected() {
		auto *item = m_list->currentItem();
		int serial = item ? item->data(Qt::UserRole).toInt() : -1;
		if (serial < 0 && m_viewers.size() == 1)
			serial = m_viewers.firstKey();
		if (auto *viewer = m_viewers.value(serial))
			viewer->requestClose();
	}

	QListWidget *m_list = nullptr;
	QLabel *m_status = nullptr;
	QMap<int, WindowViewer *> m_viewers;
	QSet<int> m_ignored;
	bool m_showing = true;
};

} // namespace

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QApplication::setApplicationName("Classic Windows");
	QApplication::setQuitOnLastWindowClosed(true);
	Toplevels::start([] {});
	Controller controller;
	controller.show();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, &controller, &Controller::poll);
	timer.start(150);
	controller.poll();

	/* A viewer started by zacos9-classic exits with that session; a manually
	   started viewer stays until closed. */
	QTimer session;
	bool seen = false;
	QObject::connect(&session, &QTimer::timeout, [&seen] {
		QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
		if (runtime.isEmpty())
			runtime = QString("/tmp/zacos9-classic-%1").arg(getuid());
		QFile file(runtime + "/zacos9-classic/session.pid");
		if (!file.open(QIODevice::ReadOnly)) {
			if (seen)
				QCoreApplication::quit();
			return;
		}
		seen = true;
		bool ok = false;
		const qint64 pid = QString::fromUtf8(file.readAll()).trimmed().toLongLong(&ok);
		if (!ok || pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0)
			QCoreApplication::quit();
	});
	session.start(1000);
	return app.exec();
}
