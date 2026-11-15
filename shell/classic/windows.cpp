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
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct GuestWindow {
	int serial = 0;
	QString title;
	QString app;
	bool captured = false;
	int error = 0;
	QString framePath;
};

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
		window.error = number(line, "error");
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
	WindowViewer(const GuestWindow &window, QWidget *parent = nullptr)
		: QWidget(parent), m_serial(window.serial) {
		setAttribute(Qt::WA_DeleteOnClose);
		setWindowTitle(window.title.isEmpty() ? window.app : window.title);
		setToolTip(window.app);
		m_image = new QLabel(this);
		m_image->setAlignment(Qt::AlignCenter);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(m_image);
		auto *desktop = new QAction(tr("Hide This Window"), this);
		connect(desktop, &QAction::triggered, this, [this] { hide(); });
		addAction(desktop);
		setContextMenuPolicy(Qt::ActionsContextMenu);
	}

	int serial() const { return m_serial; }

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
		setFixedSize(image.size());
	}

private:
	int m_serial;
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
		auto *refresh = new QPushButton(tr("Refresh"), this);
		auto *buttons = new QHBoxLayout;
		buttons->addWidget(desktop);
		buttons->addWidget(windows);
		buttons->addWidget(refresh);
		auto *layout = new QVBoxLayout(this);
		layout->addWidget(m_status);
		layout->addWidget(m_list);
		layout->addLayout(buttons);
		connect(desktop, &QPushButton::clicked, this, [this] { showDesktop(); });
		connect(windows, &QPushButton::clicked, this, [this] { showWindows(); });
		connect(refresh, &QPushButton::clicked, this, [this] { poll(); });
		resize(360, 300);
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
			if (!window.captured || m_ignored.contains(window.serial)) {
				if (auto *viewer = m_viewers.value(window.serial))
					viewer->hide();
				continue;
			}
			WindowViewer *viewer = m_viewers.value(window.serial);
			if (!viewer) {
				viewer = new WindowViewer(window);
				m_viewers.insert(window.serial, viewer);
				connect(viewer, &QObject::destroyed, this, [this, window] {
					m_viewers.remove(window.serial);
					m_ignored.insert(window.serial);
				});
			}
			viewer->updateFrame(window.framePath);
			if (m_showing)
				viewer->show();
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
			QString text = QString("%1 — %2").arg(window.app,
				window.title.isEmpty() ? tr("untitled") : window.title);
			if (!window.captured)
				text += tr("  [Classic desktop only]");
			else if (window.error)
				text += tr("  [partial: Mac error %1]").arg(window.error);
			m_list->addItem(text);
		}
	}

private:
	void showDesktop() {
		m_showing = false;
		for (WindowViewer *viewer : m_viewers)
			viewer->hide();
	}
	void showWindows() {
		m_showing = true;
		m_ignored.clear();
		for (WindowViewer *viewer : m_viewers)
			viewer->show();
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
	Controller controller;
	controller.show();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, &controller, &Controller::poll);
	timer.start(500);
	controller.poll();
	return app.exec();
}
