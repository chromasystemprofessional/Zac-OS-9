/*
 * zacos9-bluetooth: the Bluetooth control panel. Mac OS 9 had none; this
 * one is laid out like its other panels from the HIG's controls.
 *
 * Bluetooth On powers the adapter. The list shows connected devices, then
 * paired ones, then those a search has found. Connect pairs a device if it
 * isn't yet (as "no input, no output", so headphones and speakers pair
 * without a PIN), trusts it so it reconnects by itself, and connects it.
 * A headset or speaker then becomes the sound output as soon as PipeWire
 * makes an output for it; Play Sound Here switches back to it later.
 *
 * Everything goes to BlueZ over the system D-Bus, asynchronously except
 * reading its object list (local and quick), and the sound output through
 * pactl, as the Sound panel does. While the panel is open it is BlueZ's
 * pairing agent. See docs/bluetooth.md.
 */
#include <QApplication>
#include <QCloseEvent>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

typedef QMap<QString, QVariantMap> InterfaceMap;
typedef QMap<QDBusObjectPath, InterfaceMap> ManagedObjects;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 440, H = 284, M = 14;
static const QString BLUEZ = QStringLiteral("org.bluez");
static const QString ADAPTER = QStringLiteral("org.bluez.Adapter1");
static const QString DEVICE = QStringLiteral("org.bluez.Device1");
static const QString AGENT_PATH = QStringLiteral("/org/zacos9/bluetooth/agent");

/* ---- BlueZ ------------------------------------------------------------------------ */

namespace bt {

struct Device {
	QString path, name, address, icon;
	bool paired = false, connected = false;
	QStringList uuids;

	/* Something to play sound through: A2DP sink, headset or hands-free. */
	bool audio() const {
		for (const QString &u : uuids) {
			const QString s = u.left(8).toLower();
			if (s == "0000110b" || s == "00001108" || s == "0000111e") {
				return true;
			}
		}
		return icon.startsWith("audio-");
	}
	/* PipeWire's output for it is named after its address. */
	QString sinkKey() const { return QString(address).replace(':', '_'); }
};

struct State {
	bool available = false; /* BlueZ is running */
	QString error;          /* why not */
	QString adapter;        /* its object path */
	bool powered = false, discovering = false;
	std::vector<Device> devices;
};

static QDBusConnection bus() {
	return QDBusConnection::systemBus();
}

State read() {
	State s;
	QDBusMessage call = QDBusMessage::createMethodCall(BLUEZ, "/",
		"org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
	QDBusMessage reply = bus().call(call, QDBus::Block, 3000);
	if (reply.type() != QDBusMessage::ReplyMessage) {
		s.error = reply.errorName() == "org.freedesktop.DBus.Error.ServiceUnknown"
			? QString("Bluetooth isn’t running on this computer (the bluez package isn’t installed, "
				"or its service is stopped).")
			: reply.errorMessage();
		return s;
	}
	const ManagedObjects objects = qdbus_cast<ManagedObjects>(reply.arguments().value(0));
	for (auto it = objects.begin(); it != objects.end(); ++it) {
		const InterfaceMap &ifaces = it.value();
		if (ifaces.contains(ADAPTER) && s.adapter.isEmpty()) {
			const QVariantMap a = ifaces.value(ADAPTER);
			s.adapter = it.key().path();
			s.powered = a.value("Powered").toBool();
			s.discovering = a.value("Discovering").toBool();
		}
		if (ifaces.contains(DEVICE)) {
			const QVariantMap d = ifaces.value(DEVICE);
			Device dev;
			dev.path = it.key().path();
			dev.address = d.value("Address").toString();
			dev.name = d.value("Name").toString();
			dev.icon = d.value("Icon").toString();
			dev.paired = d.value("Paired").toBool();
			dev.connected = d.value("Connected").toBool();
			dev.uuids = d.value("UUIDs").toStringList();
			/* Unnamed devices nearby (beacons, phones not showing
			 * themselves) are noise in a list to pick from. */
			if (dev.name.isEmpty() && !dev.paired) {
				continue;
			}
			if (dev.name.isEmpty()) {
				dev.name = d.value("Alias").toString();
			}
			s.devices.push_back(dev);
		}
	}
	s.available = !s.adapter.isEmpty();
	if (!s.available && s.error.isEmpty()) {
		s.error = "This computer has no Bluetooth adapter that Bluetooth can use.";
	}
	/* Connected first, then paired, then found; by name within each. */
	std::stable_sort(s.devices.begin(), s.devices.end(), [](const Device &a, const Device &b) {
		const int ra = a.connected ? 0 : a.paired ? 1 : 2, rb = b.connected ? 0 : b.paired ? 1 : 2;
		if (ra != rb) {
			return ra < rb;
		}
		return a.name.localeAwareCompare(b.name) < 0;
	});
	return s;
}

/* An asynchronous call; `done` gets an empty string or the error's text. */
void call(QObject *ctx, const QString &path, const QString &iface, const QString &method,
		const QVariantList &args, std::function<void(const QString &)> done, int timeoutMs = 25000) {
	QDBusMessage m = QDBusMessage::createMethodCall(BLUEZ, path, iface, method);
	m.setArguments(args);
	auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m, timeoutMs), ctx);
	QObject::connect(w, &QDBusPendingCallWatcher::finished, ctx, [w, done] {
		w->deleteLater();
		const QDBusMessage r = w->reply();
		if (done) {
			done(r.type() == QDBusMessage::ErrorMessage ? r.errorMessage() : QString());
		}
	});
}

void setProperty(QObject *ctx, const QString &path, const QString &iface, const QString &name,
		const QVariant &value, std::function<void(const QString &)> done) {
	call(ctx, path, "org.freedesktop.DBus.Properties", "Set",
		{ iface, name, QVariant::fromValue(QDBusVariant(value)) }, done, 5000);
}

/* The pairing agent: "no input, no output", so BlueZ pairs "just works"
 * (what headphones and speakers do) and asks nothing; anything it does
 * ask while we are the agent is a pairing the user just started here. */
class Agent : public QDBusVirtualObject {
public:
	QString introspect(const QString &) const override {
		return "<interface name='org.bluez.Agent1'>"
			"<method name='Release'/>"
			"<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
			"<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
			"<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
			"<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>"
			"<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
			"<method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
			"<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
			"<method name='Cancel'/>"
			"</interface>";
	}
	bool handleMessage(const QDBusMessage &m, const QDBusConnection &c) override {
		if (m.interface() != "org.bluez.Agent1") {
			return false;
		}
		const QString method = m.member();
		if (method == "RequestPinCode") {
			c.send(m.createReply(QString("0000"))); /* old devices' usual PIN */
		} else if (method == "RequestPasskey") {
			c.send(m.createReply(QVariant::fromValue(uint(0))));
		} else {
			c.send(m.createReply()); /* accept; nothing to show */
		}
		return true;
	}
};

} // namespace bt

/* ---- sound outputs, through pactl ----------------------------------------------------- */

/* pactl, without waiting: `done` gets its exit code and output. */
static void pactl(QObject *ctx, const QStringList &args, std::function<void(int, const QString &)> done) {
	auto *p = new QProcess(ctx);
	QObject::connect(p, &QProcess::finished, ctx, [p, done](int code, QProcess::ExitStatus st) {
		p->deleteLater();
		if (done) {
			done(st == QProcess::NormalExit ? code : -1, QString::fromUtf8(p->readAllStandardOutput()));
		}
	});
	QObject::connect(p, &QProcess::errorOccurred, ctx, [p, done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			p->deleteLater();
			if (done) {
				done(-1, QString());
			}
		}
	});
	p->start("pactl", args);
}

/* ---- the panel ----------------------------------------------------------------------- */

class BluetoothPanel : public QWidget {
public:
	BluetoothPanel() : m_host(this) {
		setWindowTitle("Bluetooth");
		setFixedSize(W, H);
		qDBusRegisterMetaType<InterfaceMap>();
		qDBusRegisterMetaType<ManagedObjects>();

		m_power = PanelCheckbox("Bluetooth On", QPoint(M, 16));
		m_power.toggled = [this](bool on) { setPowered(on); };
		m_list.frame = QRect(M, 58, W - 2 * M, 150);
		m_list.picked = [this](int row) {
			m_selected = row >= 0 && row < int(m_state.devices.size()) ? m_state.devices[row].path : QString();
			updateButtons();
		};
		m_host.checks = { &m_power };

		bt::bus().registerVirtualObject(AGENT_PATH, &m_agent);
		m_refresh.callOnTimeout([this] { refresh(); });
		refresh();
		if (m_state.available) {
			bt::call(this, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
				{ QVariant::fromValue(QDBusObjectPath(AGENT_PATH)), QString("NoInputNoOutput") },
				[this](const QString &) {
					bt::call(this, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
						{ QVariant::fromValue(QDBusObjectPath(AGENT_PATH)) }, {});
				});
		}
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		panelText(c, "Devices:", M, m_list.frame.top() - 6);
		m_list.paint(c, true);
		m_host.paintControls(c, FACE);
		const QStringList lines = panelWrap(m_status, W - 2 * M, PL_FONT_VIEWS);
		int y = H - 14 - 13 * (std::min<int>(lines.size(), 2) - 1);
		for (int i = 0; i < lines.size() && i < 2; i++, y += 13) {
			panelText(c, lines[i], M, y, PL_FONT_VIEWS, GRAY(0x3));
		}
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		if (m_list.frame.contains(pos)) {
			if (m_list.press(pos)) {
				update();
			}
			return;
		}
		if (m_host.hostPress(e)) {
			update();
		}
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		if (m_list.frame.contains(pos) && selectedDevice() && m_connect.enabled) {
			m_connect.clicked(); /* double-click a device: Connect or Disconnect */
			return;
		}
		mousePressEvent(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (m_list.move(e->position().toPoint()) || m_host.hostMove(e)) {
			update();
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		const bool list = m_list.release();
		if (m_host.hostRelease(e) || list) {
			update();
		}
	}
	void wheelEvent(QWheelEvent *e) override {
		if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (m_list.key(e->key(), e->text()) || m_host.hostKey(e)) {
			update();
			return;
		}
		QWidget::keyPressEvent(e);
	}
	void closeEvent(QCloseEvent *e) override {
		if (m_searching) {
			bt::call(this, m_state.adapter, ADAPTER, "StopDiscovery", {}, {});
		}
		bt::call(this, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
			{ QVariant::fromValue(QDBusObjectPath(AGENT_PATH)) }, {});
		e->accept();
	}

private:
	PanelHost m_host;
	PanelCheckbox m_power;
	PanelList m_list;
	PanelButton m_search, m_forget, m_sound, m_connect;
	bt::Agent m_agent;
	bt::State m_state;
	QString m_selected;      /* the selected device's object path */
	QString m_status;
	QString m_defaultSink;   /* pactl get-default-sink */
	QString m_busy;          /* the device being paired or connected */
	bool m_searching = false;
	bool m_buttonsPending = false;
	QString m_searchText, m_connectText, m_soundText, m_forgetText; /* the buttons' words */
	QTimer m_refresh;

	const bt::Device *selectedDevice() const {
		for (const bt::Device &d : m_state.devices) {
			if (d.path == m_selected) {
				return &d;
			}
		}
		return nullptr;
	}

	bool playsHere(const bt::Device &d) const {
		return d.connected && m_defaultSink.contains(d.sinkKey());
	}

	void say(const QString &s) {
		m_status = s;
		update();
	}

	void refresh() {
		m_state = bt::read();
		pactl(this, { "get-default-sink" }, [this](int code, const QString &out) {
			const QString sink = code == 0 ? out.trimmed() : QString();
			if (sink != m_defaultSink) {
				m_defaultSink = sink;
				rebuildList();
			}
		});
		if (!m_state.available) {
			say(m_state.error);
		} else if (m_status == m_state.error || m_status.isEmpty()) {
			say(m_state.powered ? QString() : QString("Bluetooth is off."));
		}
		m_searching = m_searching && m_state.discovering;
		m_power.on = m_state.powered;
		m_power.enabled = m_state.available;
		rebuildList();
		/* Quickly while something is happening; otherwise every few seconds
		 * (devices connect and disconnect by themselves). */
		m_refresh.start(m_searching || !m_busy.isEmpty() ? 1000 : 3000);
	}

	void rebuildList() {
		QStringList rows;
		int sel = -1;
		for (size_t i = 0; i < m_state.devices.size(); i++) {
			const bt::Device &d = m_state.devices[i];
			QString status = d.path == m_busy ? "Connecting…"
				: playsHere(d) ? "Connected, sound plays here"
				: d.connected ? "Connected"
				: d.paired ? "Paired" : "Found";
			rows << d.name + "  —  " + status;
			if (d.path == m_selected) {
				sel = int(i);
			}
		}
		if (rows != m_list.items) {
			m_list.setItems(rows);
		}
		m_list.select(sel, false);
		if (sel < 0) {
			m_selected.clear();
		}
		updateButtons();
	}

	/* Called from anywhere, button handlers included: the buttons are
	 * updated on the next turn of the event loop, never under a click. */
	void updateButtons() {
		if (!m_buttonsPending) {
			m_buttonsPending = true;
			QTimer::singleShot(0, this, [this] {
				m_buttonsPending = false;
				layoutButtons();
			});
		}
	}

	/* A button is made again only when its words change. */
	static void place(PanelButton &b, QString &shown, const QString &text, QRect r, bool isDefault,
			std::function<void()> clicked) {
		if (shown != text) {
			b = PanelButton(text, r, isDefault);
			b.clicked = std::move(clicked);
			shown = text;
		}
		b.rect = r;
	}

	void layoutButtons() {
		const bt::Device *d = selectedDevice();
		const bool on = m_state.available && m_state.powered;
		const bool idle = m_busy.isEmpty();
		const int y = m_list.frame.bottom() + 12;
		place(m_search, m_searchText, m_searching ? "Stop Searching" : "Search",
			QRect(M, y, m_searching ? 112 : 80, PL_BUTTON_H), false, [this] { toggleSearch(); });
		m_search.enabled = on;
		int x = W - M - 92;
		place(m_connect, m_connectText, d && d->connected ? "Disconnect" : "Connect",
			QRect(x, y, 92, PL_BUTTON_H), true, [this] { connectSelected(); });
		m_connect.enabled = on && d && idle;
		x -= 12 + 118;
		place(m_sound, m_soundText, "Play Sound Here", QRect(x, y, 118, PL_BUTTON_H), false, [this] {
			if (const bt::Device *dev = selectedDevice()) {
				useForSound(*dev, 1);
			}
		});
		m_sound.enabled = on && d && d->connected && d->audio() && !playsHere(*d);
		x -= 12 + 70;
		place(m_forget, m_forgetText, "Forget", QRect(x, y, 70, PL_BUTTON_H), false,
			[this] { forgetSelected(); });
		m_forget.enabled = on && d && d->paired && idle;
		m_host.buttons = { &m_search, &m_forget, &m_sound, &m_connect };
		m_host.defaultButton = &m_connect;
		update();
	}

	void setPowered(bool on) {
		bt::setProperty(this, m_state.adapter, ADAPTER, "Powered", on, [this, on](const QString &err) {
			if (!err.isEmpty()) {
				say(QString("Bluetooth couldn’t be turned %1: %2").arg(on ? "on" : "off", err));
			} else {
				say(on ? QString() : QString("Bluetooth is off."));
			}
			refresh();
		});
	}

	void toggleSearch() {
		const bool start = !m_searching;
		bt::call(this, m_state.adapter, ADAPTER, start ? "StartDiscovery" : "StopDiscovery", {},
			[this, start](const QString &err) {
				if (!err.isEmpty() && start) {
					say("Searching didn’t start: " + err);
					return;
				}
				m_searching = start;
				say(start ? QString("Searching for devices. Put the device you want in pairing mode.")
				          : QString());
				refresh();
			});
		if (start) {
			/* Searching costs radio time; stop after a minute. */
			QTimer::singleShot(60000, this, [this] {
				if (m_searching) {
					toggleSearch();
				}
			});
		}
	}

	void connectSelected() {
		const bt::Device *dp = selectedDevice();
		if (!dp) {
			return;
		}
		const bt::Device d = *dp;
		if (d.connected) {
			bt::call(this, d.path, DEVICE, "Disconnect", {}, [this, d](const QString &err) {
				say(err.isEmpty() ? d.name + " is disconnected." : d.name + " couldn’t be disconnected: " + err);
				refresh();
			});
			return;
		}
		m_busy = d.path;
		say((d.paired ? "Connecting to " : "Pairing with ") + d.name + "…");
		rebuildList();
		auto connectNow = [this, d] {
			bt::call(this, d.path, DEVICE, "Connect", {}, [this, d](const QString &err) {
				m_busy.clear();
				if (!err.isEmpty()) {
					say(d.name + " couldn’t be connected: " + err + " Make sure it is on and nearby.");
				} else if (d.audio()) {
					say(d.name + " is connected. Waiting for its sound output…");
					useForSound(d, 12);
				} else {
					say(d.name + " is connected.");
				}
				refresh();
			}, 30000);
		};
		if (d.paired) {
			connectNow();
			return;
		}
		/* Trusted: it may reconnect by itself later, without the panel. */
		bt::setProperty(this, d.path, DEVICE, "Trusted", true, {});
		bt::call(this, d.path, DEVICE, "Pair", {}, [this, d, connectNow](const QString &err) {
			if (!err.isEmpty() && !err.contains("Already Exists")) {
				m_busy.clear();
				say(d.name + " couldn’t be paired: " + err + " Put it in pairing mode and try again.");
				refresh();
				return;
			}
			say("Connecting to " + d.name + "…");
			connectNow();
		}, 60000);
	}

	/* Make the device's PipeWire output the default, once there is one
	 * (it appears a moment after connecting); tries every 800 ms. */
	void useForSound(const bt::Device &d, int tries) {
		pactl(this, { "list", "short", "sinks" }, [this, d, tries](int code, const QString &out) {
			QString sink;
			for (const QString &line : out.split('\n')) {
				const QString name = line.section('\t', 1, 1);
				if (name.contains(d.sinkKey())) {
					sink = name;
					break;
				}
			}
			if (code != 0 || sink.isEmpty()) {
				if (tries > 1) {
					QTimer::singleShot(800, this, [this, d, tries] { useForSound(d, tries - 1); });
				} else {
					say(code < 0 ? d.name + " is connected, but there is no sound server (pactl) to send sound to it."
						: d.name + " is connected, but no sound output appeared for it. Choose a sound "
						  "profile on the device, or disconnect and connect it again.");
				}
				return;
			}
			pactl(this, { "set-default-sink", sink }, [this, d, sink](int code, const QString &) {
				if (code == 0) {
					m_defaultSink = sink;
					say("Sound now plays through " + d.name + ".");
				} else {
					say("Sound couldn’t be switched to " + d.name + ".");
				}
				rebuildList();
			});
		});
	}

	void forgetSelected() {
		const bt::Device *dp = selectedDevice();
		if (!dp) {
			return;
		}
		const bt::Device d = *dp;
		bt::call(this, m_state.adapter, ADAPTER, "RemoveDevice",
			{ QVariant::fromValue(QDBusObjectPath(d.path)) }, [this, d](const QString &err) {
				say(err.isEmpty() ? d.name + " is forgotten. Pair it again to use it."
				                  : d.name + " couldn’t be forgotten: " + err);
				m_selected.clear();
				refresh();
			});
	}
};

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Bluetooth");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-bluetooth"); /* Wayland app_id */
	platinumShellInit();

	BluetoothPanel panel;
	panel.show();
	return app.exec();
}
