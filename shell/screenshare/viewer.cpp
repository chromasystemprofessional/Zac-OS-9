/*
 * zacos9-screenshare-viewer: another ZacOS computer's screen, full screen
 * on this one, from the Screen Sharing panel's Connect tab.
 *
 *   zacos9-screenshare-viewer HOST[:PORT] [NAME]
 *
 * The other screen is shown at this screen's own pixels, whatever its
 * Pixel size (Monitors): one for one when it fits, else shrunk evenly to
 * fit, centred on black, never stretched (fitRect). The pointer and
 * clicks land where they show; keys go as they are (X keysyms), ⌘ too.
 * Control-Option-Escape switches between full screen and a window;
 * closing the window disconnects.
 *
 * The connection (rfb.h): the other computer's certificate is shown by
 * its fingerprint the first time, to check against its Screen Sharing
 * panel, and remembered; a different one later is refused unless the
 * owner says otherwise. The owner's name and password as for the AFP
 * login (LoginDialog).
 */
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>
#include <QWheelEvent>
#include <QWidget>
#include <unistd.h>

#include "alert.h"
#include "common.h"
#include "logindialog.h"
#include "platinumshell.h"
#include "rfb.h"

using namespace screenshare;

/* host fingerprint, one a line. */
static QString knownFingerprint(const QString &host) {
	QFile f(knownHostsFile());
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : f.readAll().split('\n')) {
			if (line.startsWith(host.toUtf8() + ' ')) {
				return QString::fromUtf8(line.mid(host.toUtf8().size() + 1)).trimmed();
			}
		}
	}
	return QString();
}

static void rememberFingerprint(const QString &host, const QString &print) {
	QDir().mkpath(QFileInfo(knownHostsFile()).absolutePath());
	QByteArray kept;
	QFile f(knownHostsFile());
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : f.readAll().split('\n')) {
			if (!line.isEmpty() && !line.startsWith(host.toUtf8() + ' ')) {
				kept += line + '\n';
			}
		}
	}
	QSaveFile out(knownHostsFile());
	if (out.open(QIODevice::WriteOnly)) {
		out.write(kept + host.toUtf8() + ' ' + print.toUtf8() + '\n');
		out.commit();
	}
}

/* Its two lines, as the Screen Sharing panel shows them. */
static QString shownFingerprint(const QString &print) {
	return print.left(39) + "\n" + print.mid(40);
}

class Viewer : public QWidget {
public:
	Viewer(const QString &host, quint16 port, const QString &name) : m_host(host), m_name(name) {
		setWindowTitle(name.isEmpty() ? host : name);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		resize(800, 600);
		m_rfb.checkCertificate = [this](const QString &print) { return trust(print); };
		m_rfb.credentials = [this](QString *user, QString *password, const QString &error) {
			const LoginDialog::Result r = LoginDialog::ask(this, m_name.isEmpty() ? m_host : m_name,
				false, false, error, "computer");
			*user = r.user;
			*password = r.password;
			return r.accepted;
		};
		m_rfb.connected = [this](const QString &serverName) {
			if (m_name.isEmpty() && !serverName.isEmpty()) {
				setWindowTitle(serverName);
			}
		};
		m_rfb.resized = [this] { update(); };
		m_rfb.updated = [this](const QRect &r) { update(toWidget(r)); };
		m_rfb.cursorShape = [this](const QImage &shape, QPoint hot) {
			m_cursor = shape;
			m_hotspot = hot;
			applyCursor();
		};
		m_rfb.failed = [this](const QString &why) {
			if (!why.isEmpty()) {
				Alert::ask(why, "OK", "");
			}
			qApp->quit();
		};
		m_rfb.open(host, port);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		QPainter p(this);
		const qreal dpr = devicePixelRatioF();
		/* In this screen's pixels, whatever its scale. */
		p.setTransform(QTransform::fromScale(1 / dpr, 1 / dpr));
		const QRect fit = screenRect();
		const QImage &fb = m_rfb.framebuffer();
		const QRect dirty = QTransform::fromScale(dpr, dpr).mapRect(QRectF(e->rect())).toAlignedRect();
		for (const QRect &r : QRegion(dirty).subtracted(fit)) {
			p.fillRect(r, Qt::black);
		}
		if (fb.isNull()) {
			p.fillRect(fit.intersected(dirty), Qt::black);
			return;
		}
		if (fit.size() == fb.size()) {
			p.drawImage(fit.topLeft(), fb); /* one for one */
		} else {
			p.setRenderHint(QPainter::SmoothPixmapTransform, true);
			p.drawImage(fit, fb);
		}
	}

	void resizeEvent(QResizeEvent *) override { applyCursor(); }

	void mouseMoveEvent(QMouseEvent *e) override { sendPointer(e->position(), m_buttons); }
	void mousePressEvent(QMouseEvent *e) override {
		m_buttons |= button(e->button());
		sendPointer(e->position(), m_buttons);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		m_buttons &= ~button(e->button());
		sendPointer(e->position(), m_buttons);
	}
	void wheelEvent(QWheelEvent *e) override {
		const QPoint steps = e->angleDelta() / 120;
		auto click = [&](int bit, int n) {
			for (int i = 0; i < n; i++) {
				sendPointer(e->position(), m_buttons | bit);
				sendPointer(e->position(), m_buttons);
			}
		};
		click(steps.y() > 0 ? 8 : 16, std::abs(steps.y()));
		click(steps.x() > 0 ? 32 : 64, std::abs(steps.x()));
	}

	void keyPressEvent(QKeyEvent *e) override {
		const auto mods = e->modifiers();
		if (e->key() == Qt::Key_Escape && (mods & Qt::ControlModifier) && (mods & Qt::AltModifier)) {
			/* The way out of full screen, and back. */
			isFullScreen() ? showNormal() : showFullScreen();
			return;
		}
		m_rfb.key(e->nativeVirtualKey(), true);
	}
	void keyReleaseEvent(QKeyEvent *e) override { m_rfb.key(e->nativeVirtualKey(), false); }

	/* Keys held when focus goes would stay down over there. */
	void focusOutEvent(QFocusEvent *) override { m_buttons = 0; }

	void closeEvent(QCloseEvent *) override {
		m_rfb.close();
		qApp->quit();
	}

	bool focusNextPrevChild(bool) override { return false; } /* Tab goes over there too */

private:
	QSize devicePixels() const {
		const qreal dpr = devicePixelRatioF();
		return QSize(qRound(width() * dpr), qRound(height() * dpr));
	}

	QRect screenRect() const {
		const QSize s = devicePixels();
		const QImage &fb = m_rfb.framebuffer();
		return fb.isNull() ? QRect(QPoint(), s) : fitRect(fb.width(), fb.height(), s.width(), s.height());
	}

	/* Framebuffer pixels to widget (logical) coordinates, for repainting. */
	QRect toWidget(const QRect &r) const {
		const QRect fit = screenRect();
		const QImage &fb = m_rfb.framebuffer();
		if (fb.isNull()) {
			return rect();
		}
		const qreal sx = qreal(fit.width()) / fb.width(), sy = qreal(fit.height()) / fb.height();
		const qreal dpr = devicePixelRatioF();
		const QRectF device(fit.x() + r.x() * sx, fit.y() + r.y() * sy, r.width() * sx, r.height() * sy);
		return QTransform::fromScale(1 / dpr, 1 / dpr).mapRect(device).toAlignedRect().adjusted(-1, -1, 1, 1);
	}

	void sendPointer(QPointF at, int buttons) {
		const QImage &fb = m_rfb.framebuffer();
		const QRect fit = screenRect();
		if (fb.isNull() || fit.isEmpty()) {
			return;
		}
		/* Logical to this screen's pixels to the framebuffer's. */
		const QPointF device = at * devicePixelRatioF();
		const int x = int((device.x() - fit.x()) * fb.width() / fit.width());
		const int y = int((device.y() - fit.y()) * fb.height() / fit.height());
		m_rfb.pointer(QPoint(x, y), buttons);
	}

	static int button(Qt::MouseButton b) {
		return b == Qt::LeftButton ? 1 : b == Qt::MiddleButton ? 2 : b == Qt::RightButton ? 4 : 0;
	}

	/* The other computer's pointer, at the size it shows here. */
	void applyCursor() {
		if (m_cursor.isNull()) {
			setCursor(Qt::BlankCursor); /* until it says: its own is drawn over there */
			return;
		}
		const QRect fit = screenRect();
		const QImage &fb = m_rfb.framebuffer();
		const qreal s = fb.isNull() ? 1.0 : qreal(fit.width()) / fb.width();
		QImage shape = m_cursor;
		QPoint hot = m_hotspot;
		if (s < 1) {
			shape = m_cursor.scaled(QSize(std::max(1, qRound(m_cursor.width() * s)),
				std::max(1, qRound(m_cursor.height() * s))), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			hot = QPoint(qRound(m_hotspot.x() * s), qRound(m_hotspot.y() * s));
		}
		QPixmap pixmap = QPixmap::fromImage(shape);
		pixmap.setDevicePixelRatio(devicePixelRatioF());
		const qreal dpr = devicePixelRatioF();
		setCursor(QCursor(pixmap, qRound(hot.x() / dpr), qRound(hot.y() / dpr)));
	}

	/* The first time, its fingerprint to check; later, the same one. */
	bool trust(const QString &print) {
		const QString known = knownFingerprint(m_host);
		const QString who = m_name.isEmpty() ? m_host : "“" + m_name + "”";
		if (known == print) {
			return true;
		}
		if (known.isEmpty()) {
			if (!Alert::ask(QString("Connecting to %1 for the first time. Check that its certificate’s "
					"fingerprint, in Screen Sharing on that computer, is:\n%2").arg(who, shownFingerprint(print)),
					"Connect", "Cancel")) {
				return false;
			}
		} else if (!Alert::ask(QString("%1’s certificate isn’t the one it had before. Another computer "
				"may be pretending to be it. Its fingerprint is now:\n%2\nConnect only if this is what Screen "
				"Sharing on that computer shows.").arg(who, shownFingerprint(print)),
				"Connect Anyway", "Cancel")) {
			return false;
		}
		rememberFingerprint(m_host, print);
		return true;
	}

	RfbClient m_rfb;
	QString m_host, m_name;
	int m_buttons = 0;
	QImage m_cursor;
	QPoint m_hotspot;
};

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Screen Sharing");
	QGuiApplication::setDesktopFileName("zacos9-screenshare-viewer");
	platinumShellInit();
	const QStringList args = app.arguments().mid(1);
	if (args.isEmpty()) {
		fprintf(stderr, "usage: zacos9-screenshare-viewer HOST[:PORT] [NAME]\n");
		return 2;
	}
	/* HOST, HOST:PORT, or [IPv6]:PORT */
	QString host = args[0];
	quint16 port = PORT;
	if (host.startsWith('[') && host.contains("]:")) {
		port = static_cast<quint16>(host.section("]:", 1).toUInt());
		host = host.mid(1).section(']', 0, 0);
	} else if (host.count(':') == 1) {
		port = static_cast<quint16>(host.section(':', 1).toUInt());
		host = host.section(':', 0, 0);
	}
	Viewer viewer(host, port ? port : PORT, args.value(1));
	viewer.showFullScreen();
	return app.exec();
}
