/*
 * platinum-tcpip: the TCP/IP control panel, after Mac OS 9's, for
 * NetworkManager.
 *
 * "Connect via" picks a network interface; the Setup box says how it gets
 * its address (from a DHCP server, or set manually) and which name servers
 * and search domains to use. Wi-Fi adds a Network pop-up (signal bars,
 * padlocks for secured networks, Other Network…, Wi-Fi on and off) and
 * joining with a password. Options… covers IPv6, private Wi-Fi addresses
 * and connecting automatically; Info… shows the hardware address and the
 * addresses in use.
 *
 * As on the Mac, changes to the configuration are saved when the window
 * closes or another interface is chosen, after asking. Joining a network
 * or turning Wi-Fi on or off happens at once. Everything goes through
 * nmcli. TODO: the layout is ours, from the HIG's controls and spacing and
 * the arrangement of the Mac OS 9 panel.
 */
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QHostAddress>
#include <QKeyEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include "alert.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr uint32_t FACE = GRAY(0xD);

/* ---- NetworkManager, through nmcli ---------------------------------------------- */

namespace nm {

/* nmcli's terse output escapes ':' and '\' in values with a backslash. */
static QStringList splitTerse(const QString &line) {
	QStringList parts{ QString() };
	for (int i = 0; i < line.size(); i++) {
		const QChar ch = line[i];
		if (ch == '\\' && i + 1 < line.size()) {
			parts.last() += line[++i];
		} else if (ch == ':') {
			parts << QString();
		} else {
			parts.last() += ch;
		}
	}
	return parts;
}

static bool run(const QStringList &args, QString *out = nullptr, QString *err = nullptr,
		int ms = 10000) {
	QProcess p;
	p.start("nmcli", args);
	if (!p.waitForStarted(3000)) {
		if (err) {
			*err = "nmcli isn't installed.";
		}
		return false;
	}
	p.closeWriteChannel();
	p.waitForFinished(ms);
	if (out) {
		*out = QString::fromUtf8(p.readAllStandardOutput());
	}
	if (err) {
		*err = QString::fromUtf8(p.readAllStandardError()).trimmed();
	}
	return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static QList<QStringList> rows(const QStringList &args) {
	QString out;
	QList<QStringList> list;
	if (run(args, &out)) {
		for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
			list << splitTerse(line);
		}
	}
	return list;
}

/* "name:value" lines from `-t -f … show`; indexed names (IP4.ADDRESS[1])
 * collect their values in order. "--" and "" mean unset. */
static QMap<QString, QStringList> fields(const QStringList &args) {
	QString out;
	QMap<QString, QStringList> map;
	if (!run(args, &out)) {
		return map;
	}
	for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
		const int colon = static_cast<int>(line.indexOf(':'));
		if (colon < 0) {
			continue;
		}
		QString name = line.left(colon);
		const int bracket = static_cast<int>(name.indexOf('['));
		if (bracket >= 0) {
			name.truncate(bracket);
		}
		const QString value = splitTerse(line.mid(colon + 1)).join(':');
		if (!value.isEmpty() && value != "--") {
			map[name] << value;
		}
	}
	return map;
}

static QString first(const QMap<QString, QStringList> &m, const QString &key) {
	return m.value(key).value(0);
}

bool available() {
	QString out;
	return run({ "-t", "-f", "RUNNING", "general" }, &out) && out.trimmed() == "running";
}

struct Device {
	QString name, type, state, connection;
	bool wifi() const { return type == "wifi"; }
};

QList<Device> devices() {
	QList<Device> list;
	for (const QStringList &r : rows({ "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device" })) {
		if (r.size() >= 4 && (r[1] == "ethernet" || r[1] == "wifi") && r[2] != "unmanaged") {
			list << Device{ r[0], r[1], r[2], r[3] };
		}
	}
	return list;
}

/* A connection profile's addressing ("configuration" on the Mac). */
struct Profile {
	QString uuid, name;
	QString method = "auto"; /* ipv4.method: auto or manual */
	QString address, gateway;
	int prefix = 24;
	QStringList dns, search;
	bool ignoreAutoDns = false;
	bool autoconnect = true;
	QString ipv6 = "auto";
	QString cloned; /* Wi-Fi: 802-11-wireless.cloned-mac-address */

	bool operator==(const Profile &o) const {
		return uuid == o.uuid && method == o.method && address == o.address &&
			gateway == o.gateway && prefix == o.prefix && dns == o.dns && search == o.search &&
			ignoreAutoDns == o.ignoreAutoDns && autoconnect == o.autoconnect &&
			ipv6 == o.ipv6 && cloned == o.cloned;
	}
};

static QStringList commaList(const QString &s) {
	QStringList l;
	for (const QString &part : s.split(',', Qt::SkipEmptyParts)) {
		l << part.trimmed();
	}
	return l;
}

/* The profile an interface uses: the active one, or for Ethernet one
 * that would come up on it. Empty if there is none yet. */
QString profileFor(const Device &d) {
	for (const QStringList &r : rows({ "-t", "-f", "UUID,DEVICE", "connection", "show", "--active" })) {
		if (r.size() >= 2 && r[1] == d.name) {
			return r[0];
		}
	}
	if (d.wifi()) {
		return QString();
	}
	for (const QStringList &r : rows({ "-t", "-f", "UUID,TYPE", "connection", "show" })) {
		if (r.size() >= 2 && r[1] == "802-3-ethernet") {
			const QString ifname = first(fields({ "-t", "-f", "connection.interface-name",
				"connection", "show", r[0] }), "connection.interface-name");
			if (ifname.isEmpty() || ifname == d.name) {
				return r[0];
			}
		}
	}
	return QString();
}

Profile load(const QString &uuid, bool wifi) {
	Profile p;
	p.uuid = uuid;
	QStringList f = { "connection.id", "connection.autoconnect", "ipv4.method", "ipv4.addresses",
		"ipv4.gateway", "ipv4.dns", "ipv4.dns-search", "ipv4.ignore-auto-dns", "ipv6.method" };
	if (wifi) {
		f << "802-11-wireless.cloned-mac-address";
	}
	const auto m = fields({ "-t", "-f", f.join(','), "connection", "show", uuid });
	p.name = first(m, "connection.id");
	p.autoconnect = first(m, "connection.autoconnect") != "no";
	p.method = first(m, "ipv4.method") == "manual" ? "manual" : "auto";
	const QString addr = commaList(first(m, "ipv4.addresses")).value(0);
	p.address = addr.section('/', 0, 0);
	p.prefix = addr.contains('/') ? addr.section('/', 1, 1).toInt() : 24;
	p.gateway = first(m, "ipv4.gateway");
	p.dns = commaList(m.value("ipv4.dns").join(','));
	p.search = commaList(m.value("ipv4.dns-search").join(','));
	p.ignoreAutoDns = first(m, "ipv4.ignore-auto-dns") == "yes";
	p.ipv6 = first(m, "ipv6.method");
	if (p.ipv6.isEmpty()) {
		p.ipv6 = "auto";
	}
	p.cloned = first(m, "802-11-wireless.cloned-mac-address");
	return p;
}

QStringList settingArgs(const Profile &p, bool wifi) {
	QStringList a;
	if (p.method == "manual") {
		a << "ipv4.method" << "manual" << "ipv4.addresses"
		  << QString("%1/%2").arg(p.address).arg(p.prefix) << "ipv4.gateway" << p.gateway;
	} else {
		a << "ipv4.method" << "auto" << "ipv4.addresses" << "" << "ipv4.gateway" << "";
	}
	a << "ipv4.dns" << p.dns.join(' ') << "ipv4.dns-search" << p.search.join(' ')
	  << "ipv4.ignore-auto-dns" << (p.ignoreAutoDns ? "yes" : "no")
	  << "ipv6.method" << p.ipv6 << "connection.autoconnect" << (p.autoconnect ? "yes" : "no");
	if (wifi && !p.cloned.isEmpty()) {
		a << "802-11-wireless.cloned-mac-address" << p.cloned;
	}
	return a;
}

/* What an interface is doing now. */
struct Live {
	QString hw, state, speed, gateway;
	QStringList ip4, dns, ip6;
};

Live live(const QString &dev) {
	const auto m = fields({ "-t", "-f", "GENERAL,IP4,IP6,CAPABILITIES", "device", "show", dev });
	Live l;
	l.hw = first(m, "GENERAL.HWADDR");
	l.state = first(m, "GENERAL.STATE").section('(', 1).section(')', 0, 0);
	l.speed = first(m, "CAPABILITIES.SPEED");
	l.gateway = first(m, "IP4.GATEWAY");
	l.ip4 = m.value("IP4.ADDRESS");
	l.dns = m.value("IP4.DNS");
	l.ip6 = m.value("IP6.ADDRESS");
	return l;
}

struct Network {
	QString ssid, security;
	int signal = 0;
	bool active = false;
	bool secured() const { return !security.isEmpty() && security != "--"; }
	int bars() const { return signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 25 ? 2 : 1; }
};

/* Networks in range, strongest first (the joined one at the top), each
 * name once; hidden networks are left out. */
QList<Network> networks(const QString &dev, bool rescan) {
	QList<Network> list;
	for (const QStringList &r : rows({ "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi",
			"list", "ifname", dev, "--rescan", rescan ? "auto" : "no" })) {
		if (r.size() < 4 || r[1].isEmpty()) {
			continue;
		}
		const Network n{ r[1], r[3], r[2].toInt(), r[0] == "*" };
		auto same = std::find_if(list.begin(), list.end(),
			[&](const Network &o) { return o.ssid == n.ssid; });
		if (same == list.end()) {
			list << n;
		} else {
			same->active |= n.active;
			same->signal = std::max(same->signal, n.signal);
		}
	}
	std::sort(list.begin(), list.end(), [](const Network &a, const Network &b) {
		return a.active != b.active ? a.active : a.signal > b.signal;
	});
	return list;
}

bool wifiOn() {
	QString out;
	return !run({ "radio", "wifi" }, &out) || out.trimmed() == "enabled";
}

/* nmcli's wifi-sec.key-mgmt for a network's SECURITY: empty for an open
 * network, "owe" for Enhanced Open, "enterprise" for 802.1X (which needs
 * more than a password). WPA2/WPA3 mixed networks take WPA2's wpa-psk. */
QString keyMgmt(const QString &security) {
	if (security.isEmpty() || security == "--") {
		return QString();
	}
	if (security.contains("802.1X")) {
		return "enterprise";
	}
	if (security.contains("WPA1") || security.contains("WPA2")) {
		return "wpa-psk";
	}
	if (security.contains("WPA3")) {
		return "sae";
	}
	if (security.contains("OWE")) {
		return "owe";
	}
	if (security.contains("WEP")) {
		return "none"; /* WEP keys */
	}
	return "wpa-psk";
}

bool needsPassword(const QString &mgmt) {
	return mgmt == "wpa-psk" || mgmt == "sae" || mgmt == "none";
}

/* A saved Wi-Fi profile named after the network (as we and nmcli name them). */
bool known(const QString &ssid) {
	for (const QStringList &r : rows({ "-t", "-f", "NAME,TYPE", "connection", "show" })) {
		if (r.size() >= 2 && r[0] == ssid && r[1] == "802-11-wireless") {
			return true;
		}
	}
	return false;
}

} // namespace nm

/* ---- small helpers --------------------------------------------------------------- */

static QString maskOf(int prefix) {
	const quint32 m = prefix <= 0 ? 0 : prefix >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1);
	return QHostAddress(m).toString();
}

/* A subnet mask (255.255.255.0) or prefix length (24); -1 if neither. */
static int prefixOf(const QString &mask) {
	bool number = false;
	const int n = mask.trimmed().remove('/').toInt(&number);
	if (number) {
		return n >= 0 && n <= 32 ? n : -1;
	}
	QHostAddress a(mask.trimmed());
	if (a.protocol() != QAbstractSocket::IPv4Protocol) {
		return -1;
	}
	const quint32 m = a.toIPv4Address();
	int p = 0;
	for (quint32 bits = m; bits; bits >>= 1) {
		p += bits & 1;
	}
	return p == 32 || m == ~((1u << (32 - p)) - 1) ? p : -1;
}

static bool isIPv4(const QString &s) {
	return QHostAddress(s.trimmed()).protocol() == QAbstractSocket::IPv4Protocol;
}

static QStringList linesOf(const QString &s) {
	QStringList l;
	for (const QString &part : s.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) {
		l << part;
	}
	return l;
}

static void textAt(pl_canvas *c, const QString &s, int x, int baseline, pl_font font = PL_FONT_SYSTEM,
		uint32_t color = C_BLACK, int maxWidth = 1000) {
	Text t(s, maxWidth, font);
	if (t.t && t.t->ink_l >= 0) {
		pl_text(c, t.t, x + t.t->ink_l - 1, baseline, color);
	}
}

/* A label right-aligned so its ink ends at `right`. */
static void labelAt(pl_canvas *c, const QString &s, int right, int baseline, uint32_t color = C_BLACK) {
	Text t(s, 400, PL_FONT_SYSTEM);
	pl_text(c, t.t, right - t.inkWidth() + 1, baseline, color);
}

static bool addressChar(QChar c) {
	return c.isDigit() || c == '.' || c == ':' || c == '/' || (c.toLower() >= 'a' && c.toLower() <= 'f');
}

static bool domainChar(QChar c) {
	return c.isLetterOrNumber() || c == '.' || c == '-' || c == ' ';
}

/* ---- dialogs ----------------------------------------------------------------------- */

/* A movable modal dialog drawn with lib/, holding panelkit controls. */
class PanelDialog : public QDialog {
public:
	PanelDialog(QWidget *parent, const QString &title) : QDialog(parent), m_host(this) {
		setWindowTitle(title);
	}

protected:
	PanelHost m_host;
	virtual void paintContent(pl_canvas *) {}

	/* Cancel and OK-style buttons at the bottom right, the default last. */
	void placeButtons(PanelButton &cancel, const QString &cancelText, PanelButton &ok,
			const QString &okText) {
		const int y = height() - 12 - PL_BUTTON_H;
		const int x = width() - 12 - PL_BUTTON_MIN_W - 4;
		ok = PanelButton(okText, QRect(x, y, PL_BUTTON_MIN_W + 4, PL_BUTTON_H), true);
		cancel = PanelButton(cancelText, QRect(x - 12 - PL_BUTTON_MIN_W - 4 - 3, y,
			PL_BUTTON_MIN_W + 4, PL_BUTTON_H));
		ok.clicked = [this] { accept(); };
		cancel.clicked = [this] { reject(); };
		m_host.buttons = { &cancel, &ok };
		m_host.defaultButton = &ok;
		m_host.cancelButton = &cancel;
	}

	void showEvent(QShowEvent *e) override {
		QDialog::showEvent(e);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}

	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_fill(&px.c, 0, 0, width() - 1, height() - 1, FACE);
		paintContent(&px.c);
		m_host.paintControls(&px.c, FACE);
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override { m_host.hostPress(e); }
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); }
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_host.hostKey(e)) {
			QDialog::keyPressEvent(e);
		}
		update();
	}
};

/* Joining a secured network, or any network by name (Other Network…). */
class JoinDialog : public PanelDialog {
public:
	/* ssid empty: ask for the name and security too. minLength: the
	 * shortest password the network's security allows. */
	JoinDialog(QWidget *parent, const QString &ssid, int minLength = 8,
			const QString &message = QString())
		: PanelDialog(parent, ssid.isEmpty() ? "Other Network" : "Join Network"), m_ssid(ssid),
		  m_minLength(minLength), m_message(message) {
		const bool other = ssid.isEmpty();
		setFixedSize(380, other ? 216 : 150);
		int y = 52;
		if (other) {
			m_name.rect = QRect(FIELD_X, y, 220, PL_EDIT_H);
			m_name.edited = [this] { validate(); };
			y += PL_EDIT_H + 10;
			m_security.rect = QRect(FIELD_X, y, 220, PL_POPUP_H);
			m_security.label = "Security:";
			m_security.items = { { "None" }, { "WPA/WPA2 Personal" }, { "WPA3 Personal" } };
			m_security.selected = 1;
			m_security.chosen = [this](int i) {
				m_security.selected = i;
				m_password.enabled = i != 0;
				validate();
			};
			m_host.popups = { &m_security };
			y += PL_POPUP_H + 10;
		}
		m_password.rect = QRect(FIELD_X, y, 220, PL_EDIT_H);
		m_password.password = true;
		m_password.edited = [this] { validate(); };
		m_show = PanelCheckbox("Show password", QPoint(FIELD_X, y + PL_EDIT_H + 9));
		m_show.toggled = [this](bool on) { m_password.password = !on; };
		m_host.checks = { &m_show };
		placeButtons(m_cancel, "Cancel", m_join, "Join");
		if (other) {
			m_host.edits = { &m_name, &m_password };
			m_host.setFocus(&m_name);
		} else {
			m_host.edits = { &m_password };
			m_host.setFocus(&m_password);
		}
		validate();
	}

	QString ssid() const { return m_ssid.isEmpty() ? m_name.text.trimmed() : m_ssid; }
	QString password() const { return m_password.enabled ? m_password.text : QString(); }
	/* Other Network…: the chosen security, as nmcli's key-mgmt. */
	QString keyMgmt() const {
		static const char *const mgmt[] = { "", "wpa-psk", "sae" };
		return mgmt[std::clamp(m_security.selected, 0, 2)];
	}

protected:
	void paintContent(pl_canvas *c) override {
		const QString message = !m_message.isEmpty() ? m_message
			: m_ssid.isEmpty() ? QString("Enter the name of the network to join.")
			: QString("The network “%1” needs a password.").arg(m_ssid);
		textAt(c, message, 16, 28, PL_FONT_SYSTEM, C_BLACK, width() - 32);
		if (m_ssid.isEmpty()) {
			labelAt(c, "Network name:", FIELD_X - 8, m_name.rect.top() + 15);
		}
		labelAt(c, "Password:", FIELD_X - 8, m_password.rect.top() + 15,
			m_password.enabled ? C_BLACK : GRAY(0x8));
	}

private:
	static constexpr int FIELD_X = 128;

	void validate() {
		const bool wpa = m_ssid.isEmpty() ? m_security.selected != 0 : true;
		m_join.enabled = !ssid().isEmpty() && (!wpa || m_password.text.size() >= m_minLength);
		update();
	}

	QString m_ssid;
	int m_minLength;
	QString m_message;
	PanelEdit m_name, m_password;
	PanelPopup m_security;
	PanelCheckbox m_show;
	PanelButton m_cancel, m_join;
};

/* Options…: IPv6, name servers, private Wi-Fi address, connecting
 * automatically. */
class OptionsDialog : public PanelDialog {
public:
	OptionsDialog(QWidget *parent, const nm::Profile &p, bool wifi)
		: PanelDialog(parent, "Options"), m_profile(p) {
		setFixedSize(360, wifi ? 196 : 148);
		static const char *const methods[] = { "auto", "link-local", "disabled" };
		m_ipv6.rect = QRect(112, 16, 200, PL_POPUP_H);
		m_ipv6.label = "IPv6:";
		m_ipv6.items = { { "Automatic" }, { "Link-local only" }, { "Off" } };
		m_ipv6.selected = 0;
		for (int i = 0; i < 3; i++) {
			if (p.ipv6 == methods[i]) {
				m_ipv6.selected = i;
			}
		}
		m_ipv6.chosen = [this](int i) {
			m_ipv6.selected = i;
			m_profile.ipv6 = methods[i];
		};
		m_host.popups = { &m_ipv6 };
		int y = 56;
		m_onlyDns = PanelCheckbox("Use only the name servers set here", QPoint(20, y));
		m_onlyDns.on = p.ignoreAutoDns;
		m_onlyDns.toggled = [this](bool on) { m_profile.ignoreAutoDns = on; };
		m_auto = PanelCheckbox("Connect automatically", QPoint(20, y += 24));
		m_auto.on = p.autoconnect;
		m_auto.toggled = [this](bool on) { m_profile.autoconnect = on; };
		m_host.checks = { &m_onlyDns, &m_auto };
		if (wifi) {
			m_private = PanelCheckbox("Private Wi-Fi address", QPoint(20, y += 24));
			m_private.on = p.cloned == "stable" || p.cloned == "random";
			m_private.toggled = [this](bool on) { m_profile.cloned = on ? "stable" : "permanent"; };
			m_host.checks.push_back(&m_private);
		}
		placeButtons(m_cancel, "Cancel", m_ok, "OK");
	}

	const nm::Profile &profile() const { return m_profile; }

protected:
	void paintContent(pl_canvas *c) override {
		if (m_private.label) {
			textAt(c, "A different hardware address for each network, so",
				20 + PL_CHECKBOX_LABEL_X, m_private.pos.y() + 24, PL_FONT_VIEWS, GRAY(0x5));
			textAt(c, "networks can't follow this computer from one to the next.",
				20 + PL_CHECKBOX_LABEL_X, m_private.pos.y() + 36, PL_FONT_VIEWS, GRAY(0x5));
		}
	}

private:
	nm::Profile m_profile;
	PanelPopup m_ipv6;
	PanelCheckbox m_onlyDns, m_auto, m_private;
	PanelButton m_cancel, m_ok;
};

/* Info…: the interface's hardware address and what it is using now. */
class InfoDialog : public PanelDialog {
public:
	InfoDialog(QWidget *parent, const QList<QPair<QString, QString>> &rows)
		: PanelDialog(parent, "Info"), m_rows(rows) {
		int lines = 0;
		for (const auto &r : rows) {
			lines += std::max<int>(1, static_cast<int>(r.second.split('\n').size()));
		}
		setFixedSize(420, 24 + 16 * lines + 16 + PL_BUTTON_H + 12);
		const int y = height() - 12 - PL_BUTTON_H;
		m_ok = PanelButton("OK", QRect(width() - 12 - PL_BUTTON_MIN_W, y, PL_BUTTON_MIN_W, PL_BUTTON_H), true);
		m_ok.clicked = [this] { accept(); };
		m_host.buttons = { &m_ok };
		m_host.defaultButton = m_host.cancelButton = &m_ok;
	}

protected:
	void paintContent(pl_canvas *c) override {
		int baseline = 28;
		for (const auto &r : m_rows) {
			labelAt(c, r.first, 150, baseline);
			for (const QString &line : r.second.split('\n')) {
				textAt(c, line, 158, baseline, PL_FONT_VIEWS, C_BLACK, width() - 170);
				baseline += 16;
			}
		}
	}

private:
	QList<QPair<QString, QString>> m_rows;
	PanelButton m_ok;
};

/* ---- the panel ---------------------------------------------------------------------- */

class TcpipPanel : public QWidget {
public:
	TcpipPanel() : m_host(this) {
		setWindowTitle("TCP/IP");
		m_available = nm::available();

		m_via.label = "Connect via:";
		m_via.chosen = [this](int i) { chooseDevice(i); };
		m_network.label = "Network:";
		m_network.chosen = [this](int i) { chooseNetwork(i); };
		m_configure.label = "Configure:";
		m_configure.items = { { "Manually" }, { "Using DHCP Server" } };
		m_configure.chosen = [this](int i) {
			m_configure.selected = i;
			const bool manual = i == 0;
			if (manual && m_ip.text.isEmpty() && !m_live.ip4.isEmpty()) {
				/* Start from what the server gave out. */
				const QString a = m_live.ip4.value(0);
				m_ip.setText(a.section('/', 0, 0));
				m_mask.setText(maskOf(a.section('/', 1, 1).toInt()));
				m_router.setText(m_live.gateway);
			}
			edited();
			layout();
		};
		for (PanelEdit *e : { &m_ip, &m_mask, &m_router }) {
			e->accepts = addressChar;
			e->edited = [this] { edited(); };
		}
		m_dns.multiline = m_search.multiline = true;
		m_dns.accepts = addressChar;
		m_search.accepts = domainChar;
		m_dns.edited = m_search.edited = [this] { edited(); };
		m_info = PanelButton("Info…", QRect(0, 0, PL_BUTTON_MIN_W + 4, PL_BUTTON_H));
		m_info.clicked = [this] { showInfo(); };
		m_options = PanelButton("Options…", QRect(0, 0, PL_BUTTON_MIN_W + 16, PL_BUTTON_H));
		m_options.clicked = [this] { showOptions(); };

		refreshDevices();
		selectDevice(m_devices.isEmpty() ? -1 : preferredDevice(), true);
		/* Every 4 s; every third time, ask for a fresh Wi-Fi scan too
		 * (NetworkManager scans rarely on its own). */
		m_refresh.callOnTimeout([this] { refresh(++m_ticks % 3 == 0); });
		m_refresh.start(4000);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, width() - 1, height() - 1, FACE);
		panelGroup(c, M, m_groupTop, W - M - 2, m_groupBottom, "Setup", FACE);

		if (!manual()) {
			/* The server supplies these: show what it gave, if anything. */
			const QString none = "< will be supplied by server >";
			const QString a = m_live.ip4.value(0);
			const QString values[3] = {
				a.isEmpty() ? none : a.section('/', 0, 0),
				a.isEmpty() ? none : maskOf(a.section('/', 1, 1).toInt()),
				m_live.gateway.isEmpty() ? none : m_live.gateway,
			};
			for (int i = 0; i < 3; i++) {
				textAt(c, values[i], FIELD_X, m_rowTop[i] + 15, PL_FONT_VIEWS);
			}
		}
		const char *labels[3] = { "IP Address:", "Subnet mask:", "Router address:" };
		for (int i = 0; i < 3; i++) {
			labelAt(c, labels[i], LABEL_R, m_rowTop[i] + 15);
		}
		/* On the first line of the (multi-line) field. */
		labelAt(c, "Name server addr.:", LABEL_R, m_dns.rect.top() + 13);
		textAt(c, "Search domains:", m_search.rect.left(), m_dns.rect.top() - 6);

		m_host.paintControls(c, FACE);
		textAt(c, status(), M, m_statusBaseline, PL_FONT_VIEWS, GRAY(0x3), W - 2 * M);

		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override { m_host.hostPress(e); }
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); }

	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_I) {
			showInfo();
			return;
		}
		m_host.hostKey(e);
	}

	void closeEvent(QCloseEvent *e) override {
		if (!settle(true)) {
			e->ignore();
		}
	}

private:
	static constexpr int W = 476, M = 12;
	static constexpr int LABEL_R = 148;          /* labels' ink ends here */
	static constexpr int FIELD_X = LABEL_R + PL_POPUP_LABEL_GAP + 1;
	static constexpr int FIELD_W = 150;
	static constexpr int ROW = PL_EDIT_H + 6;     /* edit fields, one above the other */
	static constexpr int LIST_H = 3 * 12 + 8;     /* three lines */

	bool manual() const { return m_configure.selected == 0; }
	bool wifi() const { return m_dev >= 0 && m_devices[m_dev].wifi(); }

	void layout() {
		const int popW = 240;
		int y = 14;
		m_via.rect = QRect(FIELD_X, y, popW, PL_POPUP_H);
		m_groupTop = y + PL_POPUP_H + 14;
		y = m_groupTop + 1 + PL_GROUP_MARGIN_TOP;
		m_host.popups = { &m_via };
		if (wifi()) {
			m_network.rect = QRect(FIELD_X, y, popW, PL_POPUP_H);
			m_host.popups.push_back(&m_network);
			y += PL_POPUP_H + PL_POPUP_SPACING;
		}
		m_configure.rect = QRect(FIELD_X, y, popW, PL_POPUP_H);
		m_host.popups.push_back(&m_configure);
		y += PL_POPUP_H + 16;
		PanelEdit *rows[3] = { &m_ip, &m_mask, &m_router };
		for (int i = 0; i < 3; i++) {
			m_rowTop[i] = y;
			rows[i]->rect = QRect(FIELD_X, y, FIELD_W, PL_EDIT_H);
			y += ROW;
		}
		y += 20;
		m_dns.rect = QRect(FIELD_X, y, FIELD_W, LIST_H);
		const int sx = FIELD_X + FIELD_W + 22;
		m_search.rect = QRect(sx, y, W - M - 2 - PL_GROUP_MARGIN - sx, LIST_H);
		m_groupBottom = y + LIST_H + PL_GROUP_MARGIN;
		m_statusBaseline = m_groupBottom + 18;
		const int by = m_statusBaseline + 12;
		m_info.rect.moveTopLeft(QPoint(M + 2, by));
		m_options.rect.moveTopLeft(QPoint(W - M - 2 - m_options.rect.width(), by));
		m_host.buttons = { &m_info, &m_options };

		m_host.edits.clear();
		if (manual()) {
			m_host.edits = { &m_ip, &m_mask, &m_router };
		}
		m_host.edits.push_back(&m_dns);
		m_host.edits.push_back(&m_search);
		if (std::find(m_host.edits.begin(), m_host.edits.end(), m_host.focus) == m_host.edits.end()) {
			m_host.focus = nullptr;
		}
		const bool on = m_available && m_dev >= 0;
		for (PanelEdit *e : m_host.edits) {
			e->enabled = on;
		}
		m_configure.enabled = m_info.enabled = m_options.enabled = on;
		m_via.enabled = m_available && !m_devices.isEmpty();
		setFixedSize(W, by + PL_BUTTON_H + 14);
		update();
	}

	void edited() {
		m_dirty = true;
	}

	/* ---- interfaces and their configuration ---- */

	void refreshDevices() {
		m_devices = m_available ? nm::devices() : QList<nm::Device>();
		m_via.items.clear();
		int ethernet = 0, wifis = 0;
		for (const nm::Device &d : m_devices) {
			(d.wifi() ? wifis : ethernet)++;
		}
		for (const nm::Device &d : m_devices) {
			const QString kind = d.wifi() ? "Wi-Fi" : "Ethernet";
			const bool several = (d.wifi() ? wifis : ethernet) > 1;
			m_via.items.push_back({ several ? QString("%1 (%2)").arg(kind, d.name) : kind });
		}
		if (m_via.items.empty()) {
			m_via.items.push_back({ m_available ? "No network interfaces" : "Not available", false });
		}
	}

	/* The interface in use: connected first, Ethernet before Wi-Fi. */
	int preferredDevice() const {
		for (int i = 0; i < m_devices.size(); i++) {
			if (m_devices[i].state == "connected") {
				return i;
			}
		}
		return 0;
	}

	void selectDevice(int i, bool initial) {
		m_dev = i;
		m_via.selected = std::max(0, i);
		m_dirty = false;
		m_profile = nm::Profile();
		m_hasProfile = false;
		m_live = nm::Live();
		if (i >= 0) {
			const nm::Device &d = m_devices[i];
			const QString uuid = nm::profileFor(d);
			m_hasProfile = !uuid.isEmpty();
			if (m_hasProfile) {
				m_profile = nm::load(uuid, d.wifi());
			}
			m_live = nm::live(d.name);
			if (d.wifi()) {
				m_wifiOn = nm::wifiOn();
				m_networks = nm::networks(d.name, initial);
				fillNetworks();
			}
		}
		showProfile();
		layout();
	}

	void showProfile() {
		const nm::Profile &p = m_profile;
		m_configure.selected = p.method == "manual" ? 0 : 1;
		m_ip.setText(p.method == "manual" ? p.address : QString());
		m_mask.setText(p.method == "manual" ? maskOf(p.prefix) : QString());
		m_router.setText(p.method == "manual" ? p.gateway : QString());
		m_dns.setText(p.dns.join('\n'));
		m_search.setText(p.search.join('\n'));
		setWindowTitle(m_hasProfile ? QString("TCP/IP (%1)").arg(p.name) : QString("TCP/IP"));
	}

	void chooseDevice(int i) {
		if (i == m_dev || i < 0 || i >= m_devices.size()) {
			return;
		}
		if (!settle()) {
			return;
		}
		selectDevice(i, true);
	}

	/* Unsaved changes: ask, as the Mac did, before they would be lost.
	 * False if the user cancels or the settings can't be saved. */
	bool settle(bool closing = false) {
		if (!m_dirty || m_dev < 0) {
			return true;
		}
		switch (Alert::choose("Save changes to the current configuration?", "Save", "Cancel",
				"Don’t Save")) {
		case Alert::Ok:
			return save(closing);
		case Alert::Other:
			m_dirty = false;
			return true;
		default:
			return false;
		}
	}

	/* The configuration as the fields show it; false (after saying why)
	 * if it isn't usable. */
	bool collect(nm::Profile *p) {
		p->method = manual() ? "manual" : "auto";
		if (manual()) {
			const int prefix = prefixOf(m_mask.text);
			if (!isIPv4(m_ip.text)) {
				Alert::ask("The IP address must be four numbers from 0 to 255 separated by "
					"periods, such as 192.168.1.20.", "OK", "");
				m_host.setFocus(&m_ip);
				return false;
			}
			if (prefix < 0) {
				Alert::ask("The subnet mask isn’t valid. It is usually 255.255.255.0.", "OK", "");
				m_host.setFocus(&m_mask);
				return false;
			}
			if (!m_router.text.trimmed().isEmpty() && !isIPv4(m_router.text)) {
				Alert::ask("The router address isn’t a valid IP address.", "OK", "");
				m_host.setFocus(&m_router);
				return false;
			}
			p->address = m_ip.text.trimmed();
			p->prefix = prefix;
			p->gateway = m_router.text.trimmed();
		}
		p->dns = linesOf(m_dns.text);
		for (const QString &server : p->dns) {
			if (QHostAddress(server).isNull()) {
				Alert::ask(QString("“%1” isn’t a valid name server address.").arg(server),
					"OK", "");
				m_host.setFocus(&m_dns);
				return false;
			}
		}
		p->search = linesOf(m_search.text);
		return true;
	}

	/* closing: the panel is about to quit, so the interface is brought
	 * up by an nmcli that outlives it. */
	bool save(bool closing) {
		nm::Profile p = m_profile;
		if (!collect(&p)) {
			return false;
		}
		const nm::Device &d = m_devices[m_dev];
		QString err;
		bool ok;
		if (m_hasProfile) {
			ok = nm::run(QStringList{ "connection", "modify", p.uuid } + nm::settingArgs(p, d.wifi()),
				nullptr, &err);
		} else if (d.wifi()) {
			/* Settings belong to a network: there is none joined. */
			m_dirty = false;
			return true;
		} else {
			const QString name = QString("Ethernet (%1)").arg(d.name);
			ok = nm::run(QStringList{ "connection", "add", "type", "ethernet", "ifname", d.name,
				"con-name", name } + nm::settingArgs(p, false), nullptr, &err);
			p.uuid = name;
		}
		if (!ok) {
			Alert::ask(QString("The configuration couldn’t be saved. %1").arg(err), "OK", "");
			return false;
		}
		m_dirty = false;
		m_profile = p;
		/* Bring the interface up with the new settings. */
		const QStringList up = { "connection", "up", p.uuid.isEmpty() ? p.name : p.uuid };
		if (closing) {
			QProcess::startDetached("nmcli", up);
		} else {
			startJob(up, QString(), "Applying the new configuration…");
		}
		return true;
	}

	/* ---- Wi-Fi ---- */

	void fillNetworks() {
		m_network.items.clear();
		m_network.enabled = m_available;
		if (!m_wifiOn) {
			m_network.items = { { "Wi-Fi Off", false }, { QString(), false, true }, { "Turn Wi-Fi On" } };
			m_network.selected = 0;
			return;
		}
		int active = -1;
		for (const nm::Network &n : m_networks) {
			if (n.active) {
				active = static_cast<int>(m_network.items.size());
			}
			m_network.items.push_back({ n.ssid, true, false, n.bars(), n.secured() });
		}
		if (active < 0) {
			m_network.items.insert(m_network.items.begin(), { "Choose a Network", false });
			active = 0;
		}
		m_network.items.push_back({ QString(), false, true });
		m_network.items.push_back({ "Other Network…" });
		m_network.items.push_back({ "Turn Wi-Fi Off" });
		m_network.selected = active;
	}

	/* Copies, not references: dialogs run the event loop, and the
	 * periodic refresh replaces the lists meanwhile. */
	void chooseNetwork(int i) {
		const QString text = m_network.items[i].text;
		if (text == "Turn Wi-Fi On" || text == "Turn Wi-Fi Off") {
			const bool on = text == "Turn Wi-Fi On";
			startJob({ "radio", "wifi", on ? "on" : "off" }, QString(),
				on ? "Turning Wi-Fi on…" : "Turning Wi-Fi off…");
			return;
		}
		if (text == "Other Network…") {
			JoinDialog d(this, QString());
			if (d.exec() == QDialog::Accepted) {
				join(d.ssid(), d.password(), d.keyMgmt(), true);
			}
			return;
		}
		auto found = std::find_if(m_networks.begin(), m_networks.end(),
			[&](const nm::Network &n) { return n.ssid == text; });
		if (found == m_networks.end() || found->active) {
			return;
		}
		const nm::Network n = *found;
		const QString mgmt = nm::keyMgmt(n.security);
		if (!settle()) {
			return;
		}
		if (nm::known(n.ssid)) {
			activate(n.ssid, QString(), mgmt, false);
			return;
		}
		if (mgmt == "enterprise") {
			Alert::ask(QString("\u201c%1\u201d needs a user name and certificate (802.1X), which "
				"TCP/IP can\u2019t set up yet. Use nmtui in Terminal instead.").arg(n.ssid), "OK", "");
			return;
		}
		QString password;
		if (nm::needsPassword(mgmt)) {
			JoinDialog d(this, n.ssid, mgmt == "none" ? 5 : 8);
			if (d.exec() != QDialog::Accepted) {
				return;
			}
			password = d.password();
		}
		join(n.ssid, password, mgmt, false);
	}

	/* A new profile for the network, then activation. The password never
	 * goes on a command line, where other users could see it: nmcli reads
	 * it from its standard input (passwd-file), and NetworkManager keeps
	 * it with the profile. */
	void join(const QString &ssid, const QString &password, const QString &mgmt, bool hidden) {
		QStringList add = { "connection", "add", "type", "wifi", "ifname", m_devices[m_dev].name,
			"con-name", ssid, "ssid", ssid };
		if (!mgmt.isEmpty()) {
			add << "wifi-sec.key-mgmt" << mgmt;
		}
		if (hidden) {
			add << "802-11-wireless.hidden" << "yes";
		}
		QString err;
		if (!nm::run(add, nullptr, &err)) {
			Alert::ask(cleanError(err), "OK", "");
			return;
		}
		activate(ssid, password, mgmt, true);
	}

	/* fresh: the profile was just made; forget it if joining is given up. */
	void activate(const QString &ssid, const QString &password, const QString &mgmt, bool fresh) {
		QStringList up = { "connection", "up", "id", ssid };
		QString input;
		if (!password.isEmpty()) {
			up << "passwd-file" << "/dev/stdin";
			input = QString("802-11-wireless-security.%1:%2\n")
				.arg(mgmt == "none" ? "wep-key0" : "psk", password);
		}
		startJob(up, input, QString("Joining \u201c%1\u201d\u2026").arg(ssid),
			[this, ssid, mgmt, fresh](const QString &) {
				if (!nm::needsPassword(mgmt)) {
					Alert::ask(QString("Couldn\u2019t join \u201c%1\u201d.").arg(ssid), "OK", "");
					return;
				}
				/* Most likely the password: ask again. */
				JoinDialog d(this, ssid, mgmt == "none" ? 5 : 8,
					QString("Couldn\u2019t join \u201c%1\u201d. Check the password and try again.")
						.arg(ssid));
				if (d.exec() == QDialog::Accepted) {
					activate(ssid, d.password(), mgmt, fresh);
				} else if (fresh) {
					nm::run({ "connection", "delete", "id", ssid });
				}
			});
	}

	static QString cleanError(const QString &err) {
		QString why = err.section('\n', -1).remove(QRegularExpression("^Error: "));
		return why.isEmpty() ? QString("That didn\u2019t work.") : why;
	}

	/* ---- background work ---- */

	/* Runs nmcli in the background; on failure, onFail (or an alert). */
	void startJob(const QStringList &args, const QString &input, const QString &what,
			std::function<void(const QString &)> onFail = {}) {
		if (m_job) {
			return;
		}
		m_job = new QProcess(this);
		m_jobStatus = what;
		connect(m_job, &QProcess::finished, this, [this, onFail](int code, QProcess::ExitStatus st) {
			const QString err = QString::fromUtf8(m_job->readAllStandardError()).trimmed();
			m_job->deleteLater();
			m_job = nullptr;
			m_jobStatus.clear();
			if (st != QProcess::NormalExit || code != 0) {
				if (onFail) {
					onFail(err);
				} else {
					Alert::ask(cleanError(err), "OK", "");
				}
			}
			refresh(true);
		});
		m_job->start("nmcli", args);
		if (!input.isEmpty()) {
			m_job->write(input.toUtf8());
		}
		m_job->closeWriteChannel();
		update();
	}

	/* Keep the shown state current; the user's unsaved edits stay. */
	void refresh(bool rescan) {
		if (!m_available || m_job) {
			return;
		}
		const QString current = m_dev >= 0 ? m_devices[m_dev].name : QString();
		refreshDevices();
		m_dev = -1;
		for (int i = 0; i < m_devices.size(); i++) {
			if (m_devices[i].name == current) {
				m_dev = i;
			}
		}
		if (m_dev < 0) {
			selectDevice(m_devices.isEmpty() ? -1 : preferredDevice(), rescan);
			return;
		}
		m_via.selected = m_dev;
		const nm::Device &d = m_devices[m_dev];
		m_live = nm::live(d.name);
		if (d.wifi()) {
			m_wifiOn = nm::wifiOn();
			m_networks = nm::networks(d.name, rescan);
			fillNetworks();
		}
		if (!m_dirty) {
			const QString uuid = nm::profileFor(d);
			if (uuid != m_profile.uuid) {
				m_hasProfile = !uuid.isEmpty();
				m_profile = m_hasProfile ? nm::load(uuid, d.wifi()) : nm::Profile();
				showProfile();
				layout();
			}
		}
		update();
	}

	QString status() const {
		if (!m_available) {
			return "NetworkManager isn’t running, so TCP/IP can’t be set up here.";
		}
		if (!m_jobStatus.isEmpty()) {
			return m_jobStatus;
		}
		if (m_dev < 0) {
			return "There are no network interfaces.";
		}
		const nm::Device &d = m_devices[m_dev];
		const QString state = m_live.state.isEmpty() ? d.state : m_live.state;
		if (d.wifi() && !m_wifiOn) {
			return "Wi-Fi is off.";
		}
		if (state == "connected") {
			QString s = d.wifi() ? QString("Connected to “%1”").arg(d.connection) : "Connected";
			if (!m_live.ip4.isEmpty()) {
				s += QString(", with the address %1").arg(m_live.ip4[0].section('/', 0, 0));
			}
			return s + ".";
		}
		if (state.startsWith("connecting")) {
			return "Connecting…";
		}
		if (state == "unavailable") {
			return d.wifi() ? "Wi-Fi isn’t available." : "No cable is connected.";
		}
		return d.wifi() ? "Not connected to a network." : "Not connected.";
	}

	void showInfo() {
		if (m_dev < 0) {
			return;
		}
		const nm::Device &d = m_devices[m_dev];
		QList<QPair<QString, QString>> rows;
		rows << qMakePair(QString("Hardware address:"), m_live.hw.isEmpty() ? "—" : m_live.hw);
		if (d.wifi()) {
			for (const nm::Network &n : m_networks) {
				if (n.active) {
					rows << qMakePair(QString("Network:"), n.ssid)
					     << qMakePair(QString("Signal:"), QString("%1%").arg(n.signal))
					     << qMakePair(QString("Security:"), n.secured() ? n.security : "None");
				}
			}
		} else if (!m_live.speed.isEmpty() && m_live.speed != "unknown") {
			rows << qMakePair(QString("Speed:"), m_live.speed);
		}
		auto list = [](const QStringList &l) { return l.isEmpty() ? QString("—") : l.join('\n'); };
		QStringList ip4;
		for (const QString &a : m_live.ip4) {
			ip4 << QString("%1  (mask %2)").arg(a.section('/', 0, 0), maskOf(a.section('/', 1, 1).toInt()));
		}
		rows << qMakePair(QString("IP address:"), list(ip4))
		     << qMakePair(QString("Router address:"), m_live.gateway.isEmpty() ? "—" : m_live.gateway)
		     << qMakePair(QString("Name servers:"), list(m_live.dns))
		     << qMakePair(QString("IPv6 addresses:"), list(m_live.ip6));
		InfoDialog(this, rows).exec();
	}

	void showOptions() {
		if (m_dev < 0) {
			return;
		}
		OptionsDialog d(this, m_profile, wifi());
		if (d.exec() == QDialog::Accepted && !(d.profile() == m_profile)) {
			m_profile = d.profile();
			m_dirty = true;
		}
	}

	PanelHost m_host;
	bool m_available = false;
	QList<nm::Device> m_devices;
	int m_dev = -1;
	nm::Profile m_profile;
	bool m_hasProfile = false;
	bool m_dirty = false;
	nm::Live m_live;
	QList<nm::Network> m_networks;
	bool m_wifiOn = true;
	QProcess *m_job = nullptr;
	QString m_jobStatus;
	QTimer m_refresh;
	int m_ticks = 0;

	PanelPopup m_via, m_network, m_configure;
	PanelEdit m_ip, m_mask, m_router, m_dns, m_search;
	PanelButton m_info, m_options;
	int m_groupTop = 0, m_groupBottom = 0, m_statusBaseline = 0;
	int m_rowTop[3] = {};
};

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("TCP/IP");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("platinum-tcpip"); /* Wayland app_id */
	platinumShellInit();

	TcpipPanel panel;
	panel.show();
	return app.exec();
}
