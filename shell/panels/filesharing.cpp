/*
 * platinum-filesharing: the File Sharing control panel, after Mac OS 9's.
 *
 *   Start/Stop        Network Identity (owner, password, computer name),
 *                     File Sharing for Macs (AFP, through Netatalk: Mac OS
 *                     8.5 and later over TCP/IP, and macOS), and, where Mac
 *                     OS 9 had Program Linking, Windows File Sharing (SMB,
 *                     through Samba; wsdd2 makes it show up in Windows'
 *                     Network)
 *   Activity Monitor  who is connected, with Disconnect; what is shared
 *   Users & Groups    the owner and guests
 *
 * The owner connects with their login password, which is also the Owner
 * Password: Windows needs its own copy, which the panel stores (after
 * checking it with unix_chkpwd) when it is typed here. Everyone's home
 * folder is shared with them. Settings and the servers are changed by
 * platinum-sharing-helper, through pkexec. TODO: the layout is ours, from
 * HIG controls and the Mac OS 9 panel's arrangement.
 */
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <pwd.h>
#include <unistd.h>

#include "alert.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"
#include "sharingclient.h"

static constexpr int W = 470, H = 392;
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

static bool serviceActive(const char *unit) {
	QProcess p;
	p.start("systemctl", { "is-active", "--quiet", unit });
	p.waitForFinished(3000);
	return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/* The owner: this account (PLATINUM_SHARING_OWNER for tests). */
static QString ownerName() {
	const QString env = qEnvironmentVariable("PLATINUM_SHARING_OWNER");
	if (!env.isEmpty()) {
		return env;
	}
	const passwd *pw = getpwuid(getuid());
	return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}

/* Is this the owner's login password? unix_chkpwd, the helper pam_unix
 * itself uses, checks the caller's own password. */
static bool loginPassword(const QString &user, const QString &password) {
	for (const char *path : { "/usr/sbin/unix_chkpwd", "/sbin/unix_chkpwd" }) {
		if (QFile::exists(path)) {
			QProcess p;
			p.start(path, { user, "nullok" });
			p.write(password.toUtf8() + '\0');
			p.closeWriteChannel();
			p.waitForFinished(10000);
			return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
		}
	}
	return true; /* nothing to check with: let Samba have it */
}

struct Settings {
	QString name, owner;
	bool afp = false, smb = false, guests = false, cleartext = false;
};

static Settings readSettings() {
	Settings s;
	s.owner = ownerName();
	QFile f("/etc/platinum/sharing.conf");
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &raw : f.readAll().split('\n')) {
			const QString line = QString::fromUtf8(raw);
			const QString key = line.section('=', 0, 0), value = line.section('=', 1);
			if (key == "name") s.name = value;
			else if (key == "afp") s.afp = value == "yes";
			else if (key == "smb") s.smb = value == "yes";
			else if (key == "guests") s.guests = value == "yes";
			else if (key == "cleartext") s.cleartext = value == "yes";
		}
	}
	if (s.name.isEmpty()) {
		/* Mac OS 9's default: "Owner's Computer". */
		const passwd *pw = getpwnam(s.owner.toLocal8Bit().constData());
		QString full = pw && pw->pw_gecos ? QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0)
		                                  : QString();
		s.name = QString("%1’s Computer").arg(full.isEmpty() ? s.owner : full);
	}
	return s;
}

/* The address others reach this computer at: the IPv4 address of the
 * interface with the default route (/proc/net/route), else the first. */
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

/* One server's group on the Start/Stop tab: a Start/Stop button and its
 * status. */
struct ServerGroup {
	const char *title;
	int top = 0, bottom = 0;
	PanelButton button;
	bool on = false;
	bool busy = false;
	QString what;   /* "file sharing", "Windows file sharing" */
	QString whom;   /* "other Macs", "Windows computers" */
};

class FileSharingPanel : public QWidget {
public:
	FileSharingPanel() : m_host(this) {
		setWindowTitle("File Sharing");
		setFixedSize(W, H);
		for (const char *label : { "Start/Stop", "Activity Monitor", "Users & Groups" }) {
			m_tabLabels.push_back(std::make_unique<Text>(label, 200, PL_FONT_SYSTEM));
		}
		m_settings = readSettings();
		m_mac.title = "File Sharing";
		m_mac.what = "File sharing";
		m_mac.whom = "other Macs";
		m_win.title = "Windows File Sharing";
		m_win.what = "Windows file sharing";
		m_win.whom = "Windows computers";
		layoutStartStop();
		layoutActivity();
		layoutUsers();
		m_mac.button.clicked = [this] { toggle(&m_mac); };
		m_win.button.clicked = [this] { toggle(&m_win); };
		m_disconnect.clicked = [this] { disconnectSelected(); };
		refreshServers();
		bool ok;
		const QString status = runSharingHelper({ "status", m_settings.owner }, QByteArray(), &ok);
		m_hasWindowsPassword = ok && status.contains("password\tset");
		showTab(0);
		m_timer.callOnTimeout([this] {
			refreshServers();
			if (m_tab == 1) {
				refreshActivity();
			}
			update();
		});
		m_timer.start(5000);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		std::vector<const plat_text *> labels;
		for (auto &t : m_tabLabels) {
			labels.push_back(t->t);
		}
		pl_tabs_paint(c, MARGIN, TABS_Y, W - MARGIN - 1, H - MARGIN - 1, labels.data(),
			static_cast<int>(labels.size()), m_tab);
		const uint32_t pane = GRAY(0xE);
		if (m_tab == 0) {
			paintStartStop(c, pane);
		} else if (m_tab == 1) {
			paintActivity(c, pane);
		} else {
			paintUsers(c, pane);
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
			savePassword();
			showTab(tab);
			return;
		}
		if (m_tab == 1 && (m_users.press(pos) || m_items.press(pos))) {
			m_disconnect.enabled = m_users.state.selected >= 0 && !m_connections.isEmpty();
			update();
			return;
		}
		if (m_tab == 2 && m_people.press(pos)) {
			showPerson();
			update();
			return;
		}
		m_host.hostPress(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); }

	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_host.focus == &m_password) {
			savePassword();
			return;
		}
		m_host.hostKey(e);
	}

	void closeEvent(QCloseEvent *e) override {
		if (!savePassword()) {
			e->ignore();
			return;
		}
		if (m_name.text.trimmed() != m_settings.name) {
			Settings s = m_settings;
			s.name = m_name.text.trimmed();
			if (!apply(s, nullptr)) {
				e->ignore();
			}
		}
	}

private:
	/* ---- Start/Stop ---- */

	void layoutStartStop() {
		int y = PANE_TOP + 3 + 16;
		m_identityTop = y;
		y += 1 + PL_GROUP_MARGIN_TOP;
		m_ownerBaseline = y + 13;
		y += 24;
		m_password.rect = QRect(FIELD_X, y, 150, PL_EDIT_H);
		m_password.password = true;
		m_password.edited = [this] { m_passwordEdited = true; };
		y += PL_EDIT_H + 6;
		m_name.rect = QRect(FIELD_X, y, 220, PL_EDIT_H);
		m_name.setText(m_settings.name);
		m_name.accepts = [](QChar ch) { return !QString(";#[]=\\\"").contains(ch); };
		y += PL_EDIT_H + 6;
		m_ipBaseline = y + 12;
		m_identityBottom = y + 18 + PL_GROUP_MARGIN;

		y = m_identityBottom + BOX_GAP;
		for (ServerGroup *g : { &m_mac, &m_win }) {
			g->top = y;
			const int inner = y + 1 + PL_GROUP_MARGIN_TOP;
			g->button = PanelButton(g->on ? "Stop" : "Start",
				QRect(BOX_L + IN, inner + 4, 66, PL_BUTTON_H));
			g->bottom = inner + (g == &m_mac ? 64 : 40) + PL_GROUP_MARGIN;
			y = g->bottom + BOX_GAP;
		}
		m_cleartext = PanelCheckbox("Allow clear-text passwords (Mac OS 8.1 and earlier)",
			QPoint(BOX_L + IN, m_mac.top + 1 + PL_GROUP_MARGIN_TOP + 46));
		m_cleartext.on = m_settings.cleartext;
		m_cleartext.toggled = [this](bool on) {
			Settings s = m_settings;
			s.cleartext = on;
			if (!apply(s, nullptr)) {
				m_cleartext.on = !on;
			}
		};
	}

	void paintStartStop(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_identityTop, BOX_R, m_identityBottom, "Network Identity", pane);
		panelLabel(c, "Owner Name:", LABEL_R, m_ownerBaseline);
		panelText(c, m_settings.owner, FIELD_X, m_ownerBaseline);
		panelLabel(c, "Owner Password:", LABEL_R, m_password.rect.top() + 15);
		panelText(c, m_hasWindowsPassword ? "Set for Windows sharing" : "Not set yet",
			m_password.rect.right() + 10, m_password.rect.top() + 15, PL_FONT_VIEWS, GRAY(0x5));
		panelLabel(c, "Computer Name:", LABEL_R, m_name.rect.top() + 15);
		panelLabel(c, "IP Address:", LABEL_R, m_ipBaseline);
		panelText(c, ipAddress(), FIELD_X, m_ipBaseline);

		for (ServerGroup *g : { &m_mac, &m_win }) {
			panelGroup(c, BOX_L, g->top, BOX_R, g->bottom, g->title, pane);
			const int tx = BOX_L + IN + 66 + 16, inner = g->top + 1 + PL_GROUP_MARGIN_TOP;
			panelText(c, "Status", tx, inner + 9);
			const QString status = g->busy
				? QString("%1 is %2…").arg(g->what, g->on ? "shutting down" : "starting up")
				: g->on ? QString("%1 on. Click Stop to prevent %2 from accessing this computer.")
				              .arg(g->what, g->whom)
				        : QString("%1 off. Click Start to allow %2 to access this computer.")
				              .arg(g->what, g->whom);
			int base = inner + 23;
			for (const QString &line : panelWrap(status, BOX_R - IN - tx, PL_FONT_VIEWS)) {
				panelText(c, line, tx, base, PL_FONT_VIEWS);
				base += 12;
			}
		}
	}

	void refreshServers() {
		for (ServerGroup *g : { &m_mac, &m_win }) {
			if (!g->busy) {
				g->on = serviceActive(g == &m_mac ? "netatalk" : "smbd");
				*g->button.label = Text(g->on ? "Stop" : "Start", 60, PL_FONT_SYSTEM);
			}
		}
	}

	void toggle(ServerGroup *g) {
		if (!savePassword()) {
			return;
		}
		Settings s = m_settings;
		s.name = m_name.text.trimmed();
		const bool on = !g->on;
		if (g == &m_win && on && !m_hasWindowsPassword) {
			Alert::ask("Type your password in Owner Password first. Windows computers need it "
				"to connect, and it is the password you log in with.", "OK", "");
			showTab(0);
			m_host.setFocus(&m_password);
			return;
		}
		(g == &m_mac ? s.afp : s.smb) = on;
		apply(s, g);
	}

	/* Hands the settings to the helper; the servers start or stop. With a
	 * group, it shows "starting up…" meanwhile. */
	bool apply(const Settings &s, ServerGroup *g) {
		if (s.name.isEmpty()) {
			Alert::ask("The computer needs a name.", "OK", "");
			return false;
		}
		const QByteArray conf = QString("name=%1\nowner=%2\nafp=%3\nsmb=%4\nguests=%5\ncleartext=%6\n")
			.arg(s.name, s.owner, s.afp ? "yes" : "no", s.smb ? "yes" : "no",
				s.guests ? "yes" : "no", s.cleartext ? "yes" : "no").toUtf8();
		if (g) {
			g->busy = true;
			g->button.enabled = false;
			repaint();
		}
		bool ok;
		QString err;
		runSharingHelper({ "apply" }, conf, &ok, &err);
		if (g) {
			g->busy = false;
			g->button.enabled = true;
		}
		if (ok) {
			m_settings = s;
		} else {
			Alert::ask(err.isEmpty() ? QString("The file sharing settings couldn’t be changed.")
			                         : err.section('\n', -1), "OK", "");
		}
		refreshServers();
		update();
		return ok;
	}

	/* A typed Owner Password: checked against the login password, then
	 * stored for Windows sharing. False if it was wrong. */
	bool savePassword() {
		if (!m_passwordEdited || m_password.text.isEmpty()) {
			return true;
		}
		if (!loginPassword(m_settings.owner, m_password.text)) {
			Alert::ask("That isn’t the password you log in with. The Owner Password is your "
				"login password.", "OK", "");
			m_host.setFocus(&m_password);
			return false;
		}
		bool ok;
		QString err;
		runSharingHelper({ "password" }, m_password.text.toUtf8() + '\n', &ok, &err);
		if (!ok) {
			Alert::ask(QString("The password couldn’t be stored. %1").arg(err), "OK", "");
			return false;
		}
		m_hasWindowsPassword = true;
		m_passwordEdited = false;
		m_password.setText(QString());
		update();
		return true;
	}

	/* ---- Activity Monitor ---- */

	void layoutActivity() {
		m_usersTop = PANE_TOP + 3 + 16;
		const int listTop = m_usersTop + 1 + PL_GROUP_MARGIN_TOP;
		m_users.frame = QRect(QPoint(BOX_L + IN, listTop),
			QPoint(BOX_R - IN - 100, listTop + 6 * PL_LIST_ROW_H + 1));
		m_disconnect = PanelButton("Disconnect", QRect(BOX_R - IN - 86, listTop, 86, PL_BUTTON_H));
		m_disconnect.enabled = false;
		m_usersBottom = m_users.frame.bottom() + PL_GROUP_MARGIN;
		m_itemsTop = m_usersBottom + BOX_GAP;
		const int itemsList = m_itemsTop + 1 + PL_GROUP_MARGIN_TOP;
		m_items.frame = QRect(QPoint(BOX_L + IN, itemsList),
			QPoint(BOX_R - IN, itemsList + 4 * PL_LIST_ROW_H + 1));
		m_itemsBottom = m_items.frame.bottom() + PL_GROUP_MARGIN;
	}

	void refreshActivity() {
		bool ok;
		const QString out = runSharingHelper({ "status", m_settings.owner }, QByteArray(), &ok);
		m_connections.clear();
		QStringList rows;
		for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
			const QStringList f = line.split('\t');
			if (f.value(0) == "conn" && f.size() >= 5) {
				m_connections << f;
				rows << QString("%1 — %2, from %3").arg(f[2],
					f[1] == "afp" ? QString("Mac") : QString("Windows"), f[3]);
			}
		}
		if (rows.isEmpty()) {
			rows << "No one is connected.";
		}
		const int keep = m_users.state.selected;
		m_users.setItems(rows);
		m_users.state.selected = keep < m_connections.size() ? keep : -1;
		m_disconnect.enabled = m_users.state.selected >= 0 && !m_connections.isEmpty();
		QStringList items;
		if (m_settings.afp || m_settings.smb) {
			items << "Home folders: each user’s own, with their password";
			for (const SharedFolder &f : sharedFolders()) {
				items << QString("“%1”: %2").arg(f.name, f.path);
			}
		} else {
			items << "Nothing: file sharing is off.";
		}
		m_items.setItems(items);
	}

	void paintActivity(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_usersTop, BOX_R, m_usersBottom, "Connected Users", pane);
		m_users.paint(c, true);
		panelGroup(c, BOX_L, m_itemsTop, BOX_R, m_itemsBottom, "Shared Items", pane);
		m_items.paint(c, false);
	}

	void disconnectSelected() {
		const int i = m_users.state.selected;
		if (i < 0 || i >= m_connections.size()) {
			return;
		}
		const QStringList f = m_connections[i];
		if (!Alert::ask(QString("Disconnect %1? Any work they have open on this computer may be "
				"lost.").arg(f[2]), "Disconnect", "Cancel")) {
			return;
		}
		bool ok;
		QString err;
		runSharingHelper({ "disconnect", f[4] }, QByteArray(), &ok, &err);
		if (!ok) {
			Alert::ask(err, "OK", "");
		}
		refreshActivity();
		update();
	}

	/* ---- Users & Groups ---- */

	void layoutUsers() {
		m_peopleTop = PANE_TOP + 3 + 16;
		const int listTop = m_peopleTop + 1 + PL_GROUP_MARGIN_TOP;
		m_people.frame = QRect(QPoint(BOX_L + IN, listTop),
			QPoint(BOX_L + IN + 150, listTop + 8 * PL_LIST_ROW_H + 1));
		m_people.setItems({ QString("%1 (Owner)").arg(m_settings.owner), "Guest" });
		m_people.select(0, false);
		m_peopleBottom = m_people.frame.bottom() + PL_GROUP_MARGIN;
		m_guests = PanelCheckbox("Allow guests to connect",
			QPoint(m_people.frame.right() + 16, listTop + 4));
		m_guests.on = m_settings.guests;
		m_guests.toggled = [this](bool on) {
			Settings s = m_settings;
			s.guests = on;
			if (!apply(s, nullptr)) {
				m_guests.on = !on;
			}
		};
	}

	void showPerson() {
		m_host.checks.clear();
		if (m_people.state.selected == 1) {
			m_host.checks.push_back(&m_guests);
		}
	}

	void paintUsers(pl_canvas *c, uint32_t pane) {
		panelGroup(c, BOX_L, m_peopleTop, BOX_R, m_peopleBottom, "Users & Groups", pane);
		m_people.paint(c, true);
		const int x = m_people.frame.right() + 16, w = BOX_R - IN - x;
		QString text;
		int base = m_people.frame.top() + 11;
		if (m_people.state.selected == 1) {
			base += 28;
			text = "Guests connect without a name or password, and see only folders shared "
			       "with Everyone.";
		} else {
			text = QString("%1 owns this computer and connects to their home folder with the "
			               "password they log in with, from Macs and from Windows.")
			           .arg(m_settings.owner);
		}
		for (const QString &line : panelWrap(text, w, PL_FONT_VIEWS)) {
			panelText(c, line, x, base, PL_FONT_VIEWS);
			base += 12;
		}
	}

	/* ---- tabs ---- */

	void showTab(int tab) {
		m_tab = tab;
		m_host.buttons.clear();
		m_host.edits.clear();
		m_host.checks.clear();
		m_host.focus = nullptr;
		if (tab == 0) {
			m_host.buttons = { &m_mac.button, &m_win.button };
			m_host.edits = { &m_password, &m_name };
			m_host.checks = { &m_cleartext };
		} else if (tab == 1) {
			m_host.buttons = { &m_disconnect };
			refreshActivity();
		} else {
			showPerson();
		}
		update();
	}

	PanelHost m_host;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	int m_tab = 0;
	Settings m_settings;
	QTimer m_timer;

	PanelEdit m_password, m_name;
	bool m_passwordEdited = false, m_hasWindowsPassword = false;
	ServerGroup m_mac, m_win;
	PanelCheckbox m_cleartext;
	int m_identityTop = 0, m_identityBottom = 0, m_ownerBaseline = 0, m_ipBaseline = 0;

	PanelList m_users, m_items;
	PanelButton m_disconnect;
	QList<QStringList> m_connections;
	int m_usersTop = 0, m_usersBottom = 0, m_itemsTop = 0, m_itemsBottom = 0;

	PanelList m_people;
	PanelCheckbox m_guests;
	int m_peopleTop = 0, m_peopleBottom = 0;
};

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("File Sharing");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("platinum-filesharing"); /* Wayland app_id */
	platinumShellInit();

	FileSharingPanel panel;
	panel.show();
	return app.exec();
}
