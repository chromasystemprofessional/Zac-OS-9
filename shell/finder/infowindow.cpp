#include "infowindow.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeDatabase>
#include <QPointer>
#include <QMouseEvent>
#include <QSysInfo>
#include <sys/xattr.h>

#include "items.h"
#include "logo.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int LABEL_RIGHT = 74; /* right edge of "Kind:" etc. */
static constexpr int VALUE_X = 82;
static constexpr int ROW_H = 16;
static constexpr int COMMENT_MAX = 200; /* Mac OS Finder comment limit */
static const char *const COMMENT_ATTR = "user.xdg.comment";

QString finderDate(const QDateTime &t) {
	if (!t.isValid()) {
		return "--";
	}
	return QLocale(QLocale::English).toString(t, "ddd, MMM d, yyyy, h:mm AP");
}

QString finderSize(qint64 bytes) {
	if (bytes < 1024 * 1024) {
		return QString::number(std::max<qint64>(bytes > 0 ? 1 : 0, (bytes + 1023) / 1024)) + "K";
	}
	if (bytes < 1024LL * 1024 * 1024) {
		return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
	}
	return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

/* "Hard Disk:home:root:" as the Mac wrote paths. */
static QString macPath(const QString &dir) {
	QStringList parts = QDir::cleanPath(dir).split('/', Qt::SkipEmptyParts);
	parts.prepend(displayName("/"));
	return parts.join(':') + ':';
}

/* The 2 px engraved separator (HIG chapter 2): a line and its highlight. */
static void separator(pl_canvas *c, int x0, int x1, int y) {
	pl_hline(c, x0, x1, y, GRAY(0x8));
	pl_hline(c, x0, x1, y + 1, C_WHITE);
}

static void row(pl_canvas *c, int y, const QString &label, const QString &value, int maxW) {
	Text l(label, 200, PL_FONT_SYSTEM);
	pl_text(c, l.t, LABEL_RIGHT - l.inkWidth() + 1, y, C_BLACK);
	Text v(value, maxW, PL_FONT_VIEWS);
	pl_text(c, v.t, VALUE_X, y, C_BLACK);
}

/* ---- Get Info ------------------------------------------------------------ */

static QHash<QString, QPointer<InfoWindow>> &infoWindows() {
	static QHash<QString, QPointer<InfoWindow>> windows;
	return windows;
}

void InfoWindow::open(const QString &path, pl_icon_kind kind, const QString &name) {
	if (InfoWindow *w = infoWindows().value(path)) {
		w->show();
		w->raise();
		return;
	}
	auto *w = new InfoWindow(path, kind, name);
	infoWindows().insert(path, w);
	w->show();
}

InfoWindow::InfoWindow(const QString &path, pl_icon_kind kind, const QString &name)
	: m_path(path), m_name(name), m_kind(kind) {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(name + " Info");
	setFixedSize(300, 280);

	QFileInfo info(path);
	if (kind == PL_ICON_DISK) {
		m_kindText = "disk";
	} else if (info.isDir()) {
		m_kindText = "folder";
	} else {
		static QMimeDatabase db;
		m_kindText = db.mimeTypeForFile(info).comment();
		if (kind == PL_ICON_APPLICATION) {
			m_kindText = "application program";
		}
	}
	m_where = macPath(path == "/" ? "/" : info.absolutePath());

	char buf[1024];
	ssize_t n = getxattr(QFile::encodeName(path).constData(), COMMENT_ATTR, buf, sizeof(buf));
	if (n > 0) {
		m_comment = QString::fromUtf8(buf, static_cast<int>(n));
	}
	m_caretTimer.callOnTimeout([this] {
		m_caretOn = !m_caretOn;
		update();
	});
	m_created = finderDate(info.birthTime().isValid() ? info.birthTime() : info.metadataChangeTime());
	m_modified = finderDate(info.lastModified());

	if (!info.isDir()) {
		QLocale en(QLocale::English);
		m_size = finderSize(info.size()) + " on disk (" + en.toString(info.size()) + " bytes)";
		return;
	}
	/* Folders: add it up off the main thread. */
	m_size = "calculating…";
	QPointer<InfoWindow> self(this);
	m_sizer = QThread::create([self, path] {
		qint64 bytes = 0;
		int items = 0;
		QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
			QDir::System, QDirIterator::Subdirectories);
		while (it.hasNext() && !QThread::currentThread()->isInterruptionRequested()) {
			it.next();
			items++;
			if (it.fileInfo().isFile() && !it.fileInfo().isSymLink()) {
				bytes += it.fileInfo().size();
			}
		}
		QString text = finderSize(bytes) + QStringLiteral(" on disk, for %1 %2")
			.arg(QLocale(QLocale::English).toString(items), items == 1 ? "item" : "items");
		QMetaObject::invokeMethod(qApp, [self, text] {
			if (self) {
				self->m_size = text;
				self->update();
			}
		});
	});
	m_sizer->start();
}

InfoWindow::~InfoWindow() {
	if (m_sizer) {
		m_sizer->requestInterruption();
		m_sizer->wait();
		delete m_sizer;
	}
}

void InfoWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	pl_icon_paint(c, 12, 10, m_kind, PL_ICON_LARGE, false);
	Text name(m_name, W - 70, PL_FONT_SYSTEM);
	pl_text(c, name.t, 56, 31, C_BLACK);
	separator(c, 10, W - 11, 52);

	int y = 74;
	row(c, y, "Kind:", m_kindText, W - VALUE_X - 10);
	row(c, y += ROW_H, "Size:", m_size, W - VALUE_X - 10);
	y += 8;
	row(c, y += ROW_H, "Where:", m_where, W - VALUE_X - 10);
	y += 8;
	row(c, y += ROW_H, "Created:", m_created, W - VALUE_X - 10);
	row(c, y += ROW_H, "Modified:", m_modified, W - VALUE_X - 10);
	separator(c, 10, W - 11, y + 10);

	/* Comments box: click to type. */
	Text label("Comments:", 200, PL_FONT_SYSTEM);
	pl_text(c, label.t, 12, y + 30, C_BLACK);
	m_commentBox = QRect(QPoint(12, y + 36), QPoint(W - 13, H - 12));
	pl_fill(c, 12, y + 36, W - 13, H - 12, C_WHITE);
	pl_outline(c, 12, y + 36, W - 13, H - 12, m_editingComment ? C_BLACK : GRAY(0x8));

	/* Wrap the comment to the box, word by word. */
	const int textW = m_commentBox.width() - 8;
	QStringList lines;
	for (const QString &para : m_comment.split('\n')) {
		QString line;
		for (const QString &word : para.split(' ')) {
			QString candidate = line.isEmpty() ? word : line + ' ' + word;
			Text probe(candidate, 100000, PL_FONT_VIEWS);
			if (!line.isEmpty() && probe.inkWidth() > textW) {
				lines << line;
				line = word;
			} else {
				line = candidate;
			}
		}
		lines << line;
	}
	int baseline = m_commentBox.top() + 12, caretX = m_commentBox.left() + 4;
	for (const QString &line : lines) {
		if (baseline > m_commentBox.bottom() - 2) {
			break;
		}
		Text t(line, textW, PL_FONT_VIEWS);
		if (t.t && t.t->ink_l >= 0) {
			pl_text(c, t.t, m_commentBox.left() + 4 + t.t->ink_l - 1, baseline, C_BLACK);
		}
		caretX = m_commentBox.left() + 4 + (t.t ? t.t->advance : 0);
		baseline += 12;
	}
	if (m_editingComment && m_caretOn) {
		pl_vline(c, caretX, baseline - 12 - 9, baseline - 12 + 1, C_BLACK);
	}

	QPainter p(this);
	px.blit(p);
}

void InfoWindow::saveComment() {
	const QByteArray name = QFile::encodeName(m_path);
	const QByteArray value = m_comment.toUtf8();
	if (value.isEmpty()) {
		removexattr(name.constData(), COMMENT_ATTR);
	} else {
		setxattr(name.constData(), COMMENT_ATTR, value.constData(), value.size(), 0);
	}
}

void InfoWindow::mousePressEvent(QMouseEvent *e) {
	const bool inBox = m_commentBox.contains(e->position().toPoint());
	if (inBox != m_editingComment) {
		m_editingComment = inBox;
		m_caretOn = true;
		inBox ? m_caretTimer.start(QApplication::cursorFlashTime() / 2) : m_caretTimer.stop();
		update();
	}
}

void InfoWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (!m_editingComment) {
		return;
	}
	if (e->key() == Qt::Key_Backspace) {
		m_comment.chop(1);
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		m_comment += '\n';
	} else if (e->key() == Qt::Key_Escape) {
		m_editingComment = false;
		m_caretTimer.stop();
	} else if (!e->text().isEmpty() && e->text().at(0).isPrint() &&
			!(e->modifiers() & Qt::ControlModifier)) {
		m_comment += e->text();
	} else {
		return;
	}
	m_comment = m_comment.left(COMMENT_MAX);
	m_caretOn = true;
	saveComment();
	update();
}

/* ---- About This Computer -------------------------------------------------- */

static QPointer<AboutWindow> &aboutWindow() {
	static QPointer<AboutWindow> window;
	return window;
}

void AboutWindow::open() {
	if (!aboutWindow()) {
		aboutWindow() = new AboutWindow;
	}
	aboutWindow()->show();
	aboutWindow()->raise();
}

static qint64 meminfoKB(const QString &key) {
	QFile f("/proc/meminfo");
	if (!f.open(QIODevice::ReadOnly)) {
		return 0;
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		if (line.startsWith(key.toLatin1() + ":")) {
			return line.mid(key.size() + 1).trimmed().split(' ').value(0).toLongLong();
		}
	}
	return 0;
}

static QString megabytes(qint64 kb) {
	return QLocale(QLocale::English).toString(kb / 1024) + " MB";
}

AboutWindow::AboutWindow() {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("About This Computer");
	setFixedSize(360, 170);
	const qint64 swap = meminfoKB("SwapTotal");
	m_lines = {
		"Built-in Memory:\t" + megabytes(meminfoKB("MemTotal")),
		"Virtual Memory:\t" + (swap > 0 ? megabytes(swap) + " used on disk" : QStringLiteral("Off")),
		"Largest Unused Block:\t" + megabytes(meminfoKB("MemAvailable")),
	};
}

void AboutWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* The logo, 3x, like the big Mac OS logo of the original. */
	const uint32_t *logo = logo_pixels();
	for (int y = 0; y < PL_LOGO_SIZE * 3; y++) {
		for (int x = 0; x < PL_LOGO_SIZE * 3; x++) {
			uint32_t v = logo[(y / 3) * PL_LOGO_SIZE + x / 3];
			if (v >> 24) {
				pl_put(c, 16 + x, 10 + y, v);
			}
		}
	}
	Text title("Platinum 2026", 300, PL_FONT_SYSTEM);
	pl_text(c, title.t, 80, 30, C_BLACK);
	QString kernel = QSysInfo::kernelType();
	kernel[0] = kernel[0].toUpper(); /* "Linux" */
	Text version("Version 0.3 — " + kernel + " " +
		QSysInfo::kernelVersion().section('-', 0, 0), 260, PL_FONT_VIEWS);
	pl_text(c, version.t, 80, 46, C_BLACK);
	separator(c, 10, W - 11, 70);

	int y = 94;
	for (const QString &line : m_lines) {
		Text label(line.section('\t', 0, 0), 200, PL_FONT_SYSTEM);
		pl_text(c, label.t, 180 - label.inkWidth(), y, C_BLACK);
		Text value(line.section('\t', 1), 160, PL_FONT_VIEWS);
		pl_text(c, value.t, 190, y, C_BLACK);
		y += 20;
	}
	/* TODO: Mac OS 8 also charted each running program's memory. */
	QPainter p(this);
	px.blit(p);
}

void AboutWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
	}
}
