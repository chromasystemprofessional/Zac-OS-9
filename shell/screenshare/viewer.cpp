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
 *
 * The bar (ControlBar) switches between the two computers: Use My
 * Desktop puts the other screen in a window, so this one's own desktop
 * is there beside it; Full Screen goes back. Full screen, the bar shows
 * for a few seconds at first, then whenever the pointer rests against
 * the top of the screen; in a window it is along the window's top.
 * Control-Option-Escape switches too. It also turns the other computer's
 * sound on and off (sound.h), and disconnects; so does closing the
 * window.
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
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <unistd.h>

#include "alert.h"
#include "common.h"
#include "logindialog.h"
#include "panelkit.h"
#include "pixels.h"
#include "platinumshell.h"
#include "rfb.h"
#include "sound.h"

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

static constexpr int BAR_H = 32;          /* the bar, at 1x */
static constexpr int BAR_W = 470;         /* full screen: a tab at the top */
static constexpr int BAR_SHOW_MS = 4000;  /* shown this long when full screen begins */
static constexpr int BAR_DWELL_MS = 400;  /* the pointer at the top this long shows it */
static constexpr int BAR_LINGER_MS = 700; /* and gone from it this long hides it */
static constexpr uint32_t FACE = GRAY(0xD);

/* Use My Desktop (or Full Screen), Sound, Disconnect, and who it is. */
class ControlBar : public QWidget {
public:
	explicit ControlBar(QWidget *parent) : QWidget(parent), m_panel(this) {
		setMouseTracking(true);
		setFocusPolicy(Qt::NoFocus);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setCursor(Qt::ArrowCursor); /* this computer's pointer, not the other's */
		m_switch = PanelButton("Use My Desktop", QRect(10, 6, 112, PL_BUTTON_H));
		m_sound = PanelCheckbox("Sound", QPoint(134, 10));
		m_sound.on = true;
		m_disconnect = PanelButton("Disconnect", QRect(0, 6, 86, PL_BUTTON_H));
		m_panel.buttons = { &m_switch, &m_disconnect };
		m_panel.checks = { &m_sound };
		m_disconnect.hint = "Stops showing the other computer’s screen here.";
	}

	PanelButton m_switch, m_disconnect;
	PanelCheckbox m_sound;

	/* Full screen: a tab; in a window: the window's top, full width. */
	void setFullScreen(bool on) {
		m_fullScreen = on;
		m_switch.label = std::make_unique<Text>(on ? "Use My Desktop" : "Full Screen",
			m_switch.rect.width() - 8, PL_FONT_SYSTEM);
		m_switch.hint = on
			? "Shows the other computer in a window, so you can use this one beside it "
			  "(Control-Option-Escape)."
			: "Shows the other computer full screen again (Control-Option-Escape).";
		update();
	}
	void setNote(const QString &note) {
		m_note = note;
		update();
	}
	void setSoundHint(const QString &hint) { m_sound.hint = hint; }

protected:
	void paintEvent(QPaintEvent *) override {
		const int w = width(), h = height();
		m_disconnect.rect.moveLeft(w - 10 - m_disconnect.rect.width());
		Pixels px(w, h);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, w - 1, h - 1, FACE);
		/* Platinum's raised face: light at the top left, shadow below. */
		pl_hline(c, 0, w - 1, 0, C_WHITE);
		pl_vline(c, 0, 0, h - 1, C_WHITE);
		pl_hline(c, 0, w - 1, h - 2, GRAY(0xA));
		pl_hline(c, 0, w - 1, h - 1, C_BLACK);
		if (m_fullScreen) {
			pl_vline(c, w - 2, 0, h - 2, GRAY(0xA));
			pl_vline(c, 0, 0, h - 1, C_BLACK);
			pl_vline(c, w - 1, 0, h - 1, C_BLACK);
		}
		const int noteX = m_sound.hitRect().right() + 14;
		panelText(c, m_note, noteX, 20, PL_FONT_SYSTEM, C_BLACK, m_disconnect.rect.left() - 10 - noteX);
		m_panel.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override { m_panel.hostPress(e); }
	void mouseMoveEvent(QMouseEvent *e) override { m_panel.hostMove(e); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_panel.hostRelease(e); }
	void mouseDoubleClickEvent(QMouseEvent *e) override { m_panel.hostPress(e); }

private:
	PanelHost m_panel;
	bool m_fullScreen = true;
	QString m_note;
};

class Viewer : public QWidget {
public:
	Viewer(const QString &host, quint16 port, const QString &name) : m_host(host), m_name(name), m_port(port) {
		setWindowTitle(name.isEmpty() ? host : name);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		resize(800, 600);
		m_bar = new ControlBar(this);
		m_bar->setNote(name.isEmpty() ? host : name);
		m_bar->m_switch.clicked = [this] { toggleFullScreen(); };
		m_bar->m_disconnect.clicked = [this] { close(); };
		m_bar->m_sound.toggled = [this](bool on) {
			m_wantSound = on;
			on ? openSound() : m_sound.close();
		};
		m_bar->setSoundHint("Plays the other computer’s sound here.");
		m_barHide.setSingleShot(true);
		m_barHide.callOnTimeout([this] {
			if (isFullScreen() && !m_bar->underMouse()) {
				m_bar->hide();
			}
		});
		m_barDwell.setSingleShot(true);
		m_barDwell.callOnTimeout([this] {
			if (isFullScreen()) {
				m_bar->show();
				m_bar->raise();
			}
		});
		m_sound.playing = [this] {
			m_soundWhy.clear();
			m_bar->setSoundHint("Plays the other computer’s sound here.");
			showNote();
		};
		m_sound.stopped = [this](const QString &why) {
			if (why.isEmpty()) {
				return; /* turned off here */
			}
			/* Not to be had: say why, and leave the box for another try. */
			m_soundWhy = why;
			m_bar->m_sound.on = false;
			m_wantSound = false;
			m_bar->setSoundHint(why);
			showNote();
		};
		m_rfb.checkCertificate = [this](const QString &print) { return trust(print); };
		m_rfb.credentials = [this](QString *user, QString *password, const QString &error) {
			const LoginDialog::Result r = LoginDialog::ask(this, m_name.isEmpty() ? m_host : m_name,
				false, false, error, "computer");
			*user = r.user;
			*password = r.password;
			/* Kept for the sound, which logs in the same way. */
			m_user = r.user;
			m_password = r.password;
			return r.accepted;
		};
		m_rfb.connected = [this](const QString &serverName) {
			if (m_name.isEmpty() && !serverName.isEmpty()) {
				setWindowTitle(serverName);
				m_serverName = serverName;
				showNote();
			}
			if (m_wantSound) {
				openSound();
			}
			if (isFullScreen()) {
				m_bar->show();
				m_bar->raise();
				m_barHide.start(BAR_SHOW_MS);
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

	void resizeEvent(QResizeEvent *) override {
		placeBar();
		applyCursor();
	}

	void changeEvent(QEvent *e) override {
		QWidget::changeEvent(e);
		if (e->type() == QEvent::WindowStateChange) {
			placeBar();
			if (isFullScreen()) {
				/* Where the way back is: shown at first, then put away. */
				m_bar->show();
				m_bar->raise();
				m_barHide.start(BAR_SHOW_MS);
			}
			update();
			applyCursor();
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (isFullScreen()) {
			/* Resting against the top: the bar. Away from it: hidden. */
			const bool atTop = e->position().y() < 2;
			if (atTop && !m_bar->isVisible() && !m_barDwell.isActive() && m_buttons == 0) {
				m_barDwell.start(BAR_DWELL_MS);
			} else if (!atTop) {
				m_barDwell.stop();
			}
			if (m_bar->isVisible() && e->position().y() > BAR_H + 24 && !m_barHide.isActive()) {
				m_barHide.start(BAR_LINGER_MS);
			}
		}
		sendPointer(e->position(), m_buttons);
	}
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
			toggleFullScreen();
			return;
		}
		m_rfb.key(e->nativeVirtualKey(), true);
	}
	void keyReleaseEvent(QKeyEvent *e) override { m_rfb.key(e->nativeVirtualKey(), false); }

	/* Keys held when focus goes would stay down over there. */
	void focusOutEvent(QFocusEvent *) override { m_buttons = 0; }

	void closeEvent(QCloseEvent *) override {
		m_sound.close();
		m_rfb.close();
		qApp->quit();
	}

	bool focusNextPrevChild(bool) override { return false; } /* Tab goes over there too */

private:
	/* In a window, the bar takes the top; full screen, it floats over. */
	int barSpace() const { return isFullScreen() ? 0 : BAR_H; }

	/* Where the other screen may go, in this screen's pixels. */
	QRect viewPixels() const {
		const qreal dpr = devicePixelRatioF();
		const int top = qRound(barSpace() * dpr);
		return QRect(0, top, qRound(width() * dpr), qRound(height() * dpr) - top);
	}

	QRect screenRect() const {
		const QRect area = viewPixels();
		const QImage &fb = m_rfb.framebuffer();
		return fb.isNull() ? area
			: fitRect(fb.width(), fb.height(), area.width(), area.height()).translated(area.topLeft());
	}

	void placeBar() {
		m_bar->setFullScreen(isFullScreen());
		if (isFullScreen()) {
			const int w = std::min(BAR_W, width());
			m_bar->setGeometry((width() - w) / 2, 0, w, BAR_H);
		} else {
			m_barHide.stop();
			m_barDwell.stop();
			m_bar->setGeometry(0, 0, width(), BAR_H);
			m_bar->show();
		}
	}

	/* Between full screen and a window, with this computer's own desktop
	 * around it: big enough to see, small enough to leave room. */
	void toggleFullScreen() {
		if (!isFullScreen()) {
			showFullScreen();
			return;
		}
		showNormal();
		if (!m_sized) {
			m_sized = true;
			const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1024, 768);
			const QImage &fb = m_rfb.framebuffer();
			const QSize room(avail.width() * 3 / 4, avail.height() * 3 / 4 - BAR_H);
			QSize view = fb.isNull() ? room : QSize(fb.width(), fb.height()).scaled(room, Qt::KeepAspectRatio);
			resize(view.width(), view.height() + BAR_H);
		}
	}

	void showNote() {
		const QString who = !m_name.isEmpty() ? m_name : !m_serverName.isEmpty() ? m_serverName : m_host;
		/* Why not is the Sound box's hint: too long for here. */
		m_bar->setNote(m_soundWhy.isEmpty() ? who : who + " · no sound");
	}

	void openSound() {
		if (m_print.isEmpty()) {
			m_sound.stopped("The connection isn’t secure enough for sound.");
			return;
		}
		m_sound.open(m_host, static_cast<quint16>(m_port + AUDIO_PORT_OFFSET), m_print, m_user, m_password);
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
			m_print = print;
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
		m_print = print;
		return true;
	}

	RfbClient m_rfb;
	SoundClient m_sound;
	ControlBar *m_bar = nullptr;
	QTimer m_barHide, m_barDwell;
	QString m_host, m_name, m_serverName;
	quint16 m_port;
	QString m_print;           /* the certificate's, once trusted */
	QString m_user, m_password; /* the login, for the sound's */
	bool m_wantSound = true;
	QString m_soundWhy;        /* why there's no sound, if there isn't */
	bool m_sized = false;
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
