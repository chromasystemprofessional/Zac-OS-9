/*
 * zacos9-screensharing: the Screen Sharing control panel, laid out after
 * Mac OS 9's File Sharing (Mac OS 9 had no screen sharing of its own;
 * Timbuktu and Apple Network Assistant looked like this).
 *
 *   Start/Stop        Network Identity (from File Sharing), and Screen
 *                     Sharing: Start/Stop, whether to ask first, whether
 *                     they may use the mouse and keyboard, whether they
 *                     hear this computer's sound, and starting at login;
 *                     the certificate's fingerprint
 *   Activity Monitor  the computers viewing this screen, with Disconnect
 *   Connect           other ZacOS computers sharing their screens (Bonjour),
 *                     or an address: Connect shows that screen here, full
 *                     screen (zacos9-screenshare-viewer)
 *
 * Sharing itself is zacos9-screenshare-host (shell/screenshare/host.cpp),
 * which runs as the owner and needs no administrator: the panel starts it
 * and talks to it on its socket. Others connect with the owner's login
 * password.
 */
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPainter>
#include <QProcess>
#include <QTimer>

#include "alert.h"
#include "process.h"
#include "programs.h"
#include "common.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

using namespace screenshare;

static constexpr int W = 470;
static constexpr int MARGIN = 10;
static constexpr int TABS_Y = 10;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int PANE_TOP = TABS_Y + PL_TAB_H;
static constexpr int BOX_L = MARGIN + 3 + PL_GROUP_MARGIN;
static constexpr int BOX_R = W - MARGIN - 3 - PL_GROUP_MARGIN - 2;
static constexpr int IN = 1 + PL_GROUP_MARGIN; /* box line to its items */
static constexpr int LABEL_R = 150;             /* labels' ink ends here */
static constexpr int FIELD_X = LABEL_R + 8;
static constexpr int BOX_GAP = 20;              /* box to box, with the title */
static constexpr int ROW = 18;

/* As File Sharing's: the IPv4 address of the interface with the default
 * route, else the first. */
static QString ipAddress() {
	QString routed;
	QFile f("/proc/net/route");
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : f.readAll().split('\n')) {
			const QList<QByteArray> col = line.simplified().split(' ');
			if (col.size() > 2 && col[1] == "00000000") {
				routed = QString::fromLatin1(col[0]);
				break;
			}
		}
	}
	QString first;
	for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
		if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) {
			continue;
		}
		for (const QNetworkAddressEntry &e : i.addressEntries()) {
			if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) {
				continue;
			}
			if (i.name() == routed) {
				return e.ip().toString();
			}
			if (first.isEmpty()) {
				first = e.ip().toString();
			}
		}
	}
	return first.isEmpty() ? QString("none (not connected)") : first;
}

static QString hostProgram() {
	return siblingProgram("zacos9-screenshare-host");
}

static QString viewerProgram() {
	return siblingProgram("zacos9-screenshare-viewer");
}

class ScreenSharingPanel : public QWidget {
public:
	ScreenSharingPanel() : m_host(this) {
		setWindowTitle("Screen Sharing");
		for (const char *label : { "Start/Stop", "Activity Monitor", "Connect" }) {
			m_tabLabels.push_back(std::make_unique<Text>(label, 200, PL_FONT_SYSTEM));
		}
		m_settings = readSettings();
		m_name = computerName();
		layoutStartStop();
		layoutActivity();
		layoutConnect();
		m_height = m_screenBottom + 3 + MARGIN + 6;
		setFixedSize(W, m_height);
		m_button.clicked = [this] { toggle(); };
		m_disconnect.clicked = [this] { disconnectSelected(); };
		m_connect.clicked = [this] { connectTo(); };
		m_browseTimer.callOnTimeout([this] { browse(); });
		refresh();
		showTab(0);
		m_timer.callOnTimeout([this] {
			if (!m_busy) {
				refresh();
				update();
			}
		});
		m_timer.start(2000);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, m_height);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, m_height - 1, FACE);
		std::vector<const plat_text *> labels;
		for (auto &t : m_tabLabels) {
			labels.push_back(t->t);
		}
		pl_tabs_paint(c, MARGIN, TABS_Y, W - MARGIN - 1, m_height - MARGIN - 1, labels.data(),
			static_cast<int>(labels.size()), m_tab);
		const uint32_t pane = GRAY(0xE);
		if (m_tab == 0) {
			paintStartStop(c, pane);
		} else if (m_tab == 1) {
			paintActivity(c, pane);
		} else {
			paintConnect(c, pane);
		}
		m_host.paintControls(c, pane);
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		std::vector<const plat_text *> labels;
		for (auto &t : m_tabLabels) {
			labels.push_back(t->t);
		}
		const int tab = pl_tabs_hit(MARGIN, TABS_Y, labels.data(), static_cast<int>(labels.size()),
			pos.x(), pos.y());
		if (tab >= 0 && tab != m_tab) {
			showTab(tab);
			return;
		}
		if (m_tab == 1 && m_list.press(pos)) {
			m_disconnect.enabled = m_list.state.selected >= 0 && !m_connections.isEmpty();
			update();
			return;
		}
		if (m_tab == 2 && m_screensList.press(pos)) {
			update();
			return;
		}
		m_host.hostPress(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (m_tab == 2 && m_screensList.move(e->position().toPoint())) {
			update();
			return;
		}
		m_host.hostMove(e);
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override {
		/* A computer in the list, double-clicked: connect to it. */
		if (m_tab == 2 && m_screensList.frame.contains(e->position().toPoint()) &&
				m_screensList.state.selected >= 0) {
			connectTo();
			return;
		}
		mousePressEvent(e);
	}
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); }

	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		m_host.hostKey(e);
	}

private:
	/* ---- Start/Stop ---- */

	void layoutStartStop() {
		m_identityTop = PANE_TOP + 3 + 16;
		const int idInner = m_identityTop + 1 + PL_GROUP_MARGIN_TOP;
		m_identityBase = idInner + 13;
		m_identityBottom = idInner + 4 * ROW + 4 + PL_GROUP_MARGIN;

		m_screenTop = m_identityBottom + BOX_GAP;
		const int inner = m_screenTop + 1 + PL_GROUP_MARGIN_TOP;
		m_button = PanelButton("Start", QRect(BOX_L + IN, inner + 4, 66, PL_BUTTON_H));
		m_button.hint = "Lets other ZacOS computers see this screen, or stops letting them.";
		m_statusTop = inner;

		int y = inner + 62;
		m_ask = PanelCheckbox("Ask before someone views my screen", QPoint(BOX_L + IN, y));
		m_ask.hint = "Shows an alert to Allow or Deny each computer that connects.";
		m_ask.on = m_settings.ask;
		m_ask.toggled = [this](bool on) {
			if (!on && !Alert::ask("Anyone who knows your login password will be able to see your "
					"screen without asking you first. Turn off asking?", "Turn Off", "Cancel")) {
				m_ask.on = true;
				return;
			}
			m_settings.ask = on;
			saveSettings(false);
		};
		y += 20;
		m_control = PanelCheckbox("Let them control the mouse and keyboard", QPoint(BOX_L + IN, y));
		m_control.hint = "Without this, other computers can watch but not click or type.";
		m_control.on = m_settings.control;
		m_control.toggled = [this](bool on) {
			if (m_on && !m_connections.isEmpty() &&
				!Alert::ask("Changing this disconnects the computers viewing this screen now.",
					"Change", "Cancel")) {
				m_control.on = !on;
				return;
			}
			m_settings.control = on;
			saveSettings(true);
		};
		y += 20;
		m_sound = PanelCheckbox("Let them hear this computer’s sound", QPoint(BOX_L + IN, y));
		m_sound.hint = "Plays what this computer plays on theirs too, while they view the screen.";
		m_sound.on = m_settings.sound;
		m_sound.toggled = [this](bool on) {
			m_settings.sound = on;
			saveSettings(false);
		};
		y += 20;
		m_login = PanelCheckbox("Start Screen Sharing when I log in", QPoint(BOX_L + IN, y));
		m_login.hint = "Turns screen sharing on each time you log in.";
		m_login.on = startsAtLogin();
		m_login.toggled = [this](bool on) {
			if (!setStartsAtLogin(on, hostProgram())) {
				m_login.on = !on;
				Alert::ask("The login setting couldn’t be changed.", "OK", "");
			}
		};
		m_certBase = y + 34;
		m_screenBottom = m_certBase + 2 * 12 + 4 + PL_GROUP_MARGIN;
	}

	void paintStartStop(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_identityTop, BOX_R, m_identityBottom, "Network Identity", pane);
		int base = m_identityBase;
		panelLabel(c, "Owner Name:", LABEL_R, base);
		panelText(c, ownerName(), FIELD_X, base);
		base += ROW;
		panelLabel(c, "Owner Password:", LABEL_R, base);
		panelText(c, "The password you log in with", FIELD_X, base, PL_FONT_VIEWS, GRAY(0x5));
		base += ROW;
		panelLabel(c, "Computer Name:", LABEL_R, base);
		panelText(c, m_name, FIELD_X, base, PL_FONT_SYSTEM, C_BLACK, BOX_R - IN - FIELD_X);
		base += ROW;
		panelLabel(c, "IP Address:", LABEL_R, base);
		panelText(c, ipAddress(), FIELD_X, base);

		panelGroup(c, BOX_L, m_screenTop, BOX_R, m_screenBottom, "Screen Sharing", pane);
		const int tx = BOX_L + IN + 66 + 16;
		panelText(c, "Status", tx, m_statusTop + 9);
		QString status;
		if (m_busy) {
			status = m_on ? "Screen sharing is shutting down…" : "Screen sharing is starting up…";
		} else if (m_on) {
			status = "Screen sharing on. Click Stop to prevent other computers from seeing this screen.";
		} else if (!m_error.isEmpty()) {
			status = "Screen sharing off. " + m_error;
		} else {
			status = "Screen sharing off. Click Start to let other ZacOS computers see this screen.";
		}
		base = m_statusTop + 23;
		for (const QString &line : panelWrap(status, BOX_R - IN - tx, PL_FONT_VIEWS)) {
			panelText(c, line, tx, base, PL_FONT_VIEWS);
			base += 12;
		}

		const QString print = certFingerprint(certFile());
		panelText(c, "Certificate:", BOX_L + IN, m_certBase, PL_FONT_VIEWS);
		const int fx = BOX_L + IN + 70;
		if (print.isEmpty()) {
			panelText(c, "Made when you first click Start", fx, m_certBase, PL_FONT_VIEWS, GRAY(0x5));
		} else {
			/* The other computer shows the same, on connecting the first time. */
			panelText(c, print.left(39), fx, m_certBase, PL_FONT_VIEWS, GRAY(0x3));
			panelText(c, print.mid(40), fx, m_certBase + 12, PL_FONT_VIEWS, GRAY(0x3));
		}
	}

	void refresh() {
		QStringList reply;
		m_on = hostRequest("status", &reply, 1500) && reply.value(0) == "on";
		m_connections.clear();
		for (const QString &line : reply) {
			const QStringList f = line.split('\t');
			if (f.value(0) == "conn" && f.size() >= 5) {
				m_connections << f;
			}
		}
		*m_button.label = Text(m_on ? "Stop" : "Start", 60, PL_FONT_SYSTEM);
		m_name = computerName();
		if (m_tab == 1) {
			refreshActivity();
		}
	}

	QString lastError() const {
		QFile f(runtimeDir() + "/error");
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
	}

	void toggle() {
		if (m_on && !m_connections.isEmpty() &&
			!Alert::ask(QString("%1 viewing this screen now. Stop screen sharing anyway?")
				.arg(m_connections.size() == 1 ? QString("A computer is")
				                               : QString("%1 computers are").arg(m_connections.size())),
				"Stop", "Cancel")) {
			return;
		}
		m_busy = true;
		m_button.enabled = false;
		m_error.clear();
		repaint();
		if (m_on) {
			hostRequest("quit", nullptr);
			waitUntil([] { return !hostRequest("status", nullptr, 500); }, 8000);
		} else {
			QFile::remove(runtimeDir() + "/error");
			if (!QProcess::startDetached(hostProgram(), {})) {
				m_error = "Screen Sharing’s server isn’t installed.";
			} else {
				waitUntil([this] { return hostRequest("status", nullptr, 500) || !lastError().isEmpty(); }, 15000);
				if (!hostRequest("status", nullptr, 500)) {
					m_error = lastError().isEmpty() ? QString("It didn’t start.") : lastError();
				}
			}
		}
		m_busy = false;
		m_button.enabled = true;
		refresh();
		update();
	}

	/* Saved, then the running host told; `restarts` when that drops
	 * everyone (whether they may control the screen). */
	void saveSettings(bool restarts) {
		if (!writeSettings(m_settings)) {
			Alert::ask("The screen sharing settings couldn’t be saved.", "OK", "");
			return;
		}
		if (m_on) {
			QStringList reply;
			if (restarts) {
				m_busy = true;
				repaint();
			}
			hostRequest("reload", &reply, 15000);
			m_busy = false;
			if (reply.value(0) != "ok") {
				m_error = reply.value(0, "It stopped.");
			}
			refresh();
		}
		update();
	}

	/* ---- Activity Monitor ---- */

	void layoutActivity() {
		m_listTop = PANE_TOP + 3 + 16;
		const int listTop = m_listTop + 1 + PL_GROUP_MARGIN_TOP;
		m_list.frame = QRect(QPoint(BOX_L + IN, listTop),
			QPoint(BOX_R - IN - 100, listTop + 8 * PL_LIST_ROW_H + 1));
		m_host.hintAreas.push_back({ &m_list.frame, "The computers viewing this screen now." });
		m_disconnect = PanelButton("Disconnect", QRect(BOX_R - IN - 86, listTop, 86, PL_BUTTON_H));
		m_disconnect.hint = "Disconnects the selected computer.";
		m_disconnect.enabled = false;
		m_listBottom = m_list.frame.bottom() + PL_GROUP_MARGIN;
	}

	void refreshActivity() {
		QStringList rows;
		const QString doing = m_settings.control ? "controlling" : "viewing";
		for (const QStringList &f : m_connections) {
			const QString who = f[3].isEmpty() ? f[2] : QString("%1 (%2)").arg(f[3], f[2]);
			const QString since = QLocale().toString(QDateTime::fromSecsSinceEpoch(f[4].toLongLong()).time(),
				QLocale::ShortFormat);
			rows << QString("%1 — %2 since %3").arg(who, doing, since);
		}
		if (rows.isEmpty()) {
			rows << (m_on ? "No one is viewing this screen." : "Screen sharing is off.");
		}
		const int keep = m_list.state.selected;
		m_list.setItems(rows);
		m_list.state.selected = keep < m_connections.size() ? keep : -1;
		m_disconnect.enabled = m_list.state.selected >= 0 && !m_connections.isEmpty();
	}

	void paintActivity(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_listTop, BOX_R, m_listBottom, "Connected Computers", pane);
		m_list.paint(c, true);
	}

	void disconnectSelected() {
		const int i = m_list.state.selected;
		if (i < 0 || i >= m_connections.size()) {
			return;
		}
		const QStringList f = m_connections[i];
		const QString who = f[3].isEmpty() ? f[2] : f[3];
		if (!Alert::ask(QString("Disconnect %1? It will stop seeing this screen.").arg(who),
				"Disconnect", "Cancel")) {
			return;
		}
		QStringList reply;
		hostRequest("disconnect " + f[1], &reply);
		if (reply.value(0) != "ok") {
			Alert::ask(reply.value(0, "Screen sharing isn’t on."), "OK", "");
		}
		refresh();
		update();
	}

	/* ---- Connect ---- */

	void layoutConnect() {
		m_screensTop = PANE_TOP + 3 + 16;
		const int listTop = m_screensTop + 1 + PL_GROUP_MARGIN_TOP;
		m_screensList.frame = QRect(QPoint(BOX_L + IN, listTop),
			QPoint(BOX_R - IN, listTop + 7 * PL_LIST_ROW_H + 1));
		m_screensList.picked = [this](int i) {
			if (i >= 0 && i < int(m_screens.size())) {
				m_address.setText(m_screens[i].target());
			}
		};
		m_host.hintAreas.push_back({ &m_screensList.frame,
			"Other ZacOS computers sharing their screens. Click one, then Connect." });
		const int below = m_screensList.frame.bottom() + 14;
		m_addressBase = below + 15;
		m_address.rect = QRect(BOX_L + IN + 62, below, BOX_R - IN - 98 - (BOX_L + IN + 62), PL_EDIT_H);
		m_address.hint = "Another computer’s name or IP address, to connect to one not in the list.";
		m_connect = PanelButton("Connect", QRect(BOX_R - IN - 86, below - 1, 86, PL_BUTTON_H));
		m_connect.hint = "Shows the other computer’s screen here, full screen. Control-Option-Escape "
			"switches between full screen and a window.";
		m_screensBottom = below + PL_EDIT_H + 8 + PL_GROUP_MARGIN;
	}

	/* Bonjour, a few seconds at a time, while the tab is showing. */
	void browse() {
		if (m_browsing.state() != QProcess::NotRunning) {
			return;
		}
		m_browsing.setProcessChannelMode(QProcess::SeparateChannels);
		QObject::disconnect(&m_browsing, nullptr, this, nullptr);
		connect(&m_browsing, &QProcess::finished, this, [this] {
			m_screens = parseScreenBrowse(QString::fromUtf8(m_browsing.readAllStandardOutput()), m_name);
			refreshScreens();
			update();
		});
		m_browsing.start("avahi-browse", { "-k", "-p", "-r", "-t", "_rfb._tcp" });
		QTimer::singleShot(4000, this, [this] {
			if (m_browsing.state() != QProcess::NotRunning) {
				m_browsing.terminate();
			}
		});
	}

	void refreshScreens() {
		QStringList rows;
		for (const SharedScreen &s : m_screens) {
			rows << QString("%1 (%2)").arg(s.name, s.address);
		}
		const QString keep = m_screensList.state.selected >= 0 && m_screensList.state.selected < m_shown.size()
			? m_shown[m_screensList.state.selected] : QString();
		m_shown.clear();
		for (const SharedScreen &s : m_screens) {
			m_shown << s.name;
		}
		if (rows.isEmpty()) {
			rows << "Looking for ZacOS computers sharing their screens…";
		}
		m_screensList.setItems(rows);
		m_screensList.state.selected = m_shown.indexOf(keep);
	}

	void paintConnect(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_screensTop, BOX_R, m_screensBottom, "Shared Screens", pane);
		m_screensList.paint(c, true);
		panelLabel(c, "Address:", BOX_L + IN + 54, m_addressBase);
	}

	void connectTo() {
		const QString target = m_address.text.trimmed();
		if (target.isEmpty()) {
			Alert::ask("Choose a computer in the list, or type its name or address.", "OK", "");
			return;
		}
		QString name;
		for (const SharedScreen &s : m_screens) {
			if (s.target() == target) {
				name = s.name;
			}
		}
		QStringList args{ target };
		if (!name.isEmpty()) {
			args << name;
		}
		if (!QProcess::startDetached(viewerProgram(), args)) {
			Alert::ask("Screen Sharing’s viewer isn’t installed.", "OK", "");
		}
	}

	/* ---- tabs ---- */

	void showTab(int tab) {
		m_tab = tab;
		m_host.buttons.clear();
		m_host.checks.clear();
		m_host.edits.clear();
		m_host.setFocus(nullptr);
		m_host.defaultButton = nullptr;
		m_browseTimer.stop();
		if (tab == 0) {
			m_host.buttons = { &m_button };
			m_host.checks = { &m_ask, &m_control, &m_sound, &m_login };
		} else if (tab == 1) {
			m_host.buttons = { &m_disconnect };
			refreshActivity();
		} else {
			m_host.buttons = { &m_connect };
			m_host.edits = { &m_address };
			m_host.defaultButton = &m_connect;
			m_host.setFocus(&m_address);
			refreshScreens();
			browse();
			m_browseTimer.start(10000);
		}
		update();
	}

	PanelHost m_host;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	int m_tab = 0;
	int m_height = 0;
	Settings m_settings;
	QString m_name, m_error;
	QTimer m_timer;
	bool m_on = false, m_busy = false;
	QList<QStringList> m_connections;

	PanelButton m_button;
	PanelCheckbox m_ask, m_control, m_sound, m_login;
	int m_identityTop = 0, m_identityBase = 0, m_identityBottom = 0;
	int m_screenTop = 0, m_statusTop = 0, m_certBase = 0, m_screenBottom = 0;

	PanelList m_list;
	PanelButton m_disconnect;
	int m_listTop = 0, m_listBottom = 0;

	PanelList m_screensList;
	PanelEdit m_address;
	PanelButton m_connect;
	std::vector<SharedScreen> m_screens;
	QStringList m_shown;
	QProcess m_browsing;
	QTimer m_browseTimer;
	int m_screensTop = 0, m_screensBottom = 0, m_addressBase = 0;
};

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Screen Sharing");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-screensharing"); /* Wayland app_id */
	platinumShellInit();

	ScreenSharingPanel panel;
	panel.show();
	return app.exec();
}
