#include "collar.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QDir>
#include <QUrl>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include "alert.h"
#include "customthemes.h"
#include "menudraw.h"
#include "panelkit.h"
#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

using namespace collarart;

static QString configDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9";
}

static QString configPath() {
	return configDir() + "/collar.conf";
}

static QString kdeConnectProgram(const QString &name) {
	return QStandardPaths::findExecutable(name);
}

/* ---- Behaviour -------------------------------------------------------------------- */

Collar::Collar(bool monitor)
	: m_sound(this), m_controls(this), m_displays(this), m_kdeConnect(this),
	  m_settings(configPath(), QSettings::IniFormat) {
	setWindowTitle("The Collar");
	setAccessibleName("The Collar");
	setAccessibleDescription("Quick sound, network, Bluetooth, phone, display and power controls.");
	setAttribute(Qt::WA_TranslucentBackground);
	setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	m_collapsed = m_settings.value("collapsed", false).toBool();
	m_wanted = std::max(0, m_settings.value("modules", 0).toInt());
	m_bottom = std::max(0, m_settings.value("offset", 0).toInt());
	m_hidden = m_settings.value("hidden", false).toBool();
	m_visibility = visibilitySetting();
	m_displays.refresh(false);
	rebuildModules();
	winId();
	if (auto *layer = LayerShellQt::Window::get(windowHandle())) {
		layer->setScope("zacos9-collar");
		layer->setLayer(LayerShellQt::Window::LayerTop);
		layer->setAnchors(LayerShellQt::Window::Anchors(
			LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorBottom));
		layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromCompositor);
		layer->setExclusiveZone(0);
		layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
	}
	applyGeometry();
	connect(qApp, &QGuiApplication::screenAdded, this, [this] { applyGeometry(); });
	connect(qApp, &QGuiApplication::screenRemoved, this, [this] { applyGeometry(); });
	if (windowHandle()->screen()) {
		connect(windowHandle()->screen(), &QScreen::geometryChanged, this, [this] { applyGeometry(); });
	}
	m_sound.changed = [this] {
		if (!m_sound.outputs.isEmpty()) {
			m_soundError.clear();
		}
		update();
	};
	m_sound.failed = [this](const QString &error) { m_soundError = error; update(); };
	m_sound.commandFailed = [](const QString &error) { Alert::ask(error, "OK", {}); };
	m_controls.changed = [this] { update(); };
	m_controls.failed = [](const QString &error) { Alert::ask(error, "OK", {}); };
	m_displays.changed = [this] { rebuildModules(); };
	m_displays.failed = [](const QString &error) { Alert::ask(error, "OK", {}); };
	m_kdeConnect.changed = [this] { rebuildModules(); };
	m_kdeConnect.failed = [](const QString &error) { Alert::ask(error, "OK", {}); };
	m_keychains.changed = [this] { rebuildModules(); };
	m_keychains.failed = [](const QString &error) { Alert::ask(error, "OK", {}); };
	// The Collar control panel writes desktop.conf; follow it as it changes.
	QDir().mkpath(configDir());
	auto *settingsWatcher = new QFileSystemWatcher(this);
	settingsWatcher->addPath(configDir());
	connect(settingsWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
		if (m_applied) {
			applySettings();
		}
		update();
	});
	watchCustomThemes(this, [this] { update(); });
	if (monitor) {
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] {
			m_sound.refresh();
			m_controls.refresh();
			m_displays.refresh();
			m_kdeConnect.refresh();
			m_keychains.refresh();
		});
		timer->start(5000);
		m_sound.refresh();
		m_controls.refresh();
		m_displays.refresh();
		m_kdeConnect.refresh();
		m_keychains.refresh();
	}
}

void Collar::rebuildModules() {
	QList<Module> modules = { { Volume, {} }, { SoundSet, {} }, { Network, {} }, { Bluetooth, {} } };
	// KDE Connect, when installed: a module per phone or tablet in reach, else one to pair a device.
	for (const PhoneDevice &phone : m_kdeConnect.devices) {
		modules.append(Module{ Phone, phone.id });
	}
	if (m_kdeConnect.installed && m_kdeConnect.devices.isEmpty()) {
		modules.append(Module{ Phone, QString() });
	}
	// The keyring daemon's keychains, as Mac OS 9's Keychain Strip showed them.
	if (m_keychains.available) {
		modules.append(Module{ Keychain, QString() });
	}
	for (const CollarDisplay &d : m_displays.displays) {
		modules.append(Module{ Resolution, d.name });
		modules.append(Module{ Brightness, d.name });
	}
	if (m_displays.displays.isEmpty()) {
		modules.append(Module{ Resolution, QString() });
		modules.append(Module{ Brightness, QString() });
	}
	modules.append(Module{ Power, QString() });
	if (modules != m_modules) {
		m_modules = modules;
		m_focus = std::clamp(m_focus, 0, int(m_modules.size()) - 1);
		m_first = std::clamp(m_first, 0, int(m_modules.size()) - visibleModules());
		if (windowHandle()) {
			applyGeometry();
		}
	}
	update();
}

int Collar::visibleModules() const {
	const int count = m_modules.size();
	return m_wanted > 0 ? std::min(m_wanted, count) : count;
}

int Collar::moduleIndex(Kind kind, const QString &device) const {
	for (int i = 0; i < m_modules.size(); ++i) {
		if (m_modules[i].kind == kind && (device.isEmpty() || m_modules[i].device == device)) {
			return i;
		}
	}
	return -1;
}

QRect Collar::moduleRect(int index) const {
	if (m_collapsed || index < m_first || index >= m_first + visibleModules()) {
		return {};
	}
	return QRect(FirstModule + (index - m_first) * (Cell + 1), 0, Cell, Height);
}

int Collar::moduleAt(QPoint point) const {
	for (int i = m_first; i < m_first + visibleModules(); ++i) {
		if (moduleRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

Collar::Part Collar::partAt(QPoint point) const {
	if (!mask().contains(point)) {
		return None;
	}
	if (m_collapsed) {
		return GripPart;
	}
	const int right = FirstModule + visibleModules() * (Cell + 1);
	if (point.x() <= Close + 1) {
		return CloseBox;
	} else if (point.x() < FirstModule) {
		return LeftArrow;
	} else if (moduleAt(point) >= 0) {
		return ModulePart;
	} else if (point.x() >= right && point.x() < right + Arrow) {
		return RightArrow;
	} else if (point.x() >= width() - Grip) {
		return GripPart;
	}
	return None;
}

void Collar::applyGeometry() {
	QScreen *screen = windowHandle()->screen();
	if (screen) {
		m_bottom = std::clamp(m_bottom, 0, std::max(0, screen->size().height() - Height - MenuBar));
	}
	const int width = m_collapsed ? Closed : openWidth(visibleModules());
	setFixedSize(width, Height);
	QRegion shape;
	const int grip = width - Grip;
	shape += QRect(0, 0, grip, Height);
	for (int y = 0; y < Height; ++y) {
		shape += QRect(grip, y, int(strlen(GripArt[y])), 1);
	}
	setMask(shape);
	if (auto *layer = LayerShellQt::Window::get(windowHandle())) {
		layer->setMargins(QMargins(0, 0, 0, m_bottom));
	} else if (screen) {
		move(screen->geometry().left(), screen->geometry().bottom() - Height + 1 - m_bottom);
	}
	update();
}

void Collar::save() {
	m_settings.setValue("collapsed", m_collapsed);
	m_settings.setValue("modules", m_wanted);
	m_settings.setValue("offset", m_bottom);
	m_settings.setValue("hidden", m_hidden);
	m_settings.sync();
	if (m_settings.status() != QSettings::NoError) {
		qWarning("The Collar couldn't save its position and size.");
		Alert::ask("The Collar couldn't save its position and size.", "OK", {});
	}
}

void Collar::setCollapsed(bool collapsed) {
	if (collapsed != m_collapsed) {
		pl_sound_event(collapsed ? "window-collapse" : "window-expand");
	}
	m_collapsed = collapsed;
	applyGeometry();
	save();
}

void Collar::setVisibleModules(int count) {
	const int total = m_modules.size();
	count = std::clamp(count, 1, total);
	m_wanted = count >= total ? 0 : count;
	m_first = std::clamp(m_first, 0, total - visibleModules());
	applyGeometry();
	save();
}

bool Collar::active(int index) const {
	const Module &m = m_modules[index];
	const auto &s = m_controls.state;
	const CollarDisplay *d = m_displays.display(m.device);
	switch (m.kind) {
	case Volume: return !m_sound.outputs.isEmpty();
	case SoundSet: return true;
	case Network: return s.networkAvailable && s.wifi;
	case Bluetooth: return !s.adapter.isEmpty() && s.bluetooth;
	case Resolution: return d;
	case Brightness: return d && d->brightness >= 0;
	case Power: return s.powerError.isEmpty();
	case Phone: return m_kdeConnect.device(m.device) || !m_kdeConnect.requests.isEmpty();
	case Keychain: return !m_keychains.keychains.isEmpty();
	}
	return false;
}

void Collar::paintEvent(QPaintEvent *) {
	Pixels px(width(), Height);
	auto *c = &px.c;
	if (m_collapsed) {
		for (int y = 0; y < Height; ++y) {
			art(c, 0, y, GripArt[y]);
		}
	} else {
		const int visible = visibleModules();
		const int right = FirstModule + visible * (Cell + 1);
		pl_hline(c, 0, right + Arrow, 0, C_BLACK);
		pl_hline(c, 0, right + Arrow, Height - 1, C_BLACK);
		for (int y = 0; y < Height; ++y) {
			art(c, 0, y, CloseBoxArt[y]);
		}
		scrollButton(c, Close + 2, false, m_first > 0);
		pl_vline(c, FirstModule - 1, 0, Height - 1, C_BLACK);
		for (int i = m_first; i < m_first + visible; ++i) {
			const QRect r = moduleRect(i);
			const Module &m = m_modules[i];
			const bool on = active(i);
			cell(c, r.left(), Cell, m_pressed == i);
			pl_vline(c, r.right() + 1, 0, Height - 1, C_BLACK);
			const PhoneDevice *phone = m.kind == Phone ? m_kdeConnect.device(m.device) : nullptr;
			const KeychainInfo *keychain = m.kind == Keychain ? m_keychains.defaultKeychain() : nullptr;
			icon(c, m.kind, r.left() + 2, 4, on, phone ? phone->charge : keychain ? !keychain->locked : -1);
			menuArrow(c, r.left() + 22);
			if (m.kind == Volume) {
				for (const SoundOutput &output : m_sound.outputs) {
					if (output.current && output.muted) {
						const QRect waves(r.left() + 12, 6, 5, 13);
						pl_fill(c, waves.left(), waves.top(), waves.right(), waves.bottom(), Face);
						for (int k = 0; k < 5; ++k) {
							pl_put(c, waves.left() + k, 10 + k, C_BLACK);
							pl_put(c, waves.right() - k, 10 + k, C_BLACK);
						}
					}
				}
			}
			if (m.kind == Power && m_controls.state.batteryPresent) {
				const auto &s = m_controls.state;
				const int fill = qRound(10 * s.batteryPercent / 100);
				pl_fill(c, r.left() + 4, 10, r.left() + 13, 13, Face);
				if (fill) {
					pl_fill(c, r.left() + 4, 10, r.left() + 3 + fill, 13,
						s.batteryPercent < 15 ? RGB(0xAA, 0x22, 0x22) : RGB(0x66, 0x99, 0x55));
				}
			}
			if (hasFocus() && m_focus == i) {
				for (int x = r.left() + 2; x < r.left() + 20; x += 2) {
					pl_put(c, x, Height - 4, C_BLACK);
				}
			}
		}
		scrollButton(c, right, true, m_first + visible < m_modules.size());
		for (int y = 0; y < Height; ++y) {
			art(c, width() - Grip, y, GripArt[y]);
		}
	}
	QPainter painter(this);
	px.blit(painter);
}

QString Collar::menuLabel(int index) const {
	const Module &m = m_modules.value(index, { Volume, {} });
	switch (m.kind) {
	case Volume: return "Sound";
	case SoundSet: return "Sound Set";
	case Network: return "Network";
	case Bluetooth: return "Bluetooth";
	case Resolution:
	case Brightness: return m.device.isEmpty() ? QString("Display") : m_displays.label(m.device);
	case Power: return "Power";
	case Phone:
		if (const PhoneDevice *phone = m_kdeConnect.device(m.device)) {
			return phone->name;
		}
		return "KDE Connect";
	case Keychain: return "Keychain";
	}
	return "The Collar";
}

static QString currentSoundSet() {
	char id[256];
	return pl_setting("sound-theme", id, sizeof(id)) && *id ? QString::fromUtf8(id) : QString("none");
}

QString Collar::description(int index) const {
	if (index < 0 || index >= m_modules.size()) {
		return "The Collar";
	}
	const Module &m = m_modules[index];
	const auto &s = m_controls.state;
	const CollarDisplay *d = m_displays.display(m.device);
	switch (m.kind) {
	case Volume:
		for (const auto &o : m_sound.outputs) {
			if (o.current) {
				return QString("Volume: %1%2").arg(o.volume).arg(o.muted ? "% (muted)" : "%");
			}
		}
		return m_soundError.isEmpty() ? "No sound output available" : m_soundError;
	case SoundSet:
		for (int i = 0; i < soundThemeCount(); ++i) {
			if (soundThemeId(i) == currentSoundSet()) {
				return "Sound set: " + soundThemeName(i);
			}
		}
		return "Sound set: None";
	case Network:
		if (!s.networkAvailable) {
			return s.networkError;
		}
		switch (s.networkState) {
		case 10: return "Networking asleep";
		case 20: return "Networking disabled";
		case 30: return "Network disconnected";
		case 40: return "Connecting...";
		case 50: return "Local network connected";
		case 60: return "Network connected (local/site only)";
		case 70: return "Network connected";
		default: return "Network status unknown";
		}
	case Bluetooth:
		return s.adapter.isEmpty() ? s.bluetoothError : s.bluetooth ? "Bluetooth on" : "Bluetooth off";
	case Resolution:
		return d ? QString("%1: %2 × %3").arg(menuLabel(index)).arg(d->size.width()).arg(d->size.height())
			: QString("No display information is available.");
	case Brightness:
		if (!d) {
			return "No display information is available.";
		}
		return menuLabel(index) + ": " +
			(d->brightness < 0 ? d->brightnessError : QString("Brightness %1%").arg(d->brightness));
	case Power:
		if (!s.powerError.isEmpty()) {
			return s.powerError;
		}
		return s.batteryPresent ? QString("Battery: %1%").arg(qRound(s.batteryPercent)) : "AC power (no battery)";
	case Phone: {
		const PhoneDevice *phone = m_kdeConnect.device(m.device);
		if (!m_kdeConnect.requests.isEmpty() && !phone) {
			return QString("KDE Connect: %1 wants to pair").arg(m_kdeConnect.requests.first().name);
		}
		if (!phone) {
			return "KDE Connect: no devices connected";
		}
		if (phone->charge < 0) {
			return phone->name + ": connected";
		}
		return QString("%1: battery %2%%3").arg(phone->name).arg(phone->charge).arg(phone->charging ? " (charging)" : "");
	}
	case Keychain:
		if (const KeychainInfo *k = m_keychains.defaultKeychain()) {
			return QString("Keychain “%1” is %2").arg(k->name, k->locked ? "locked" : "unlocked");
		}
		return m_keychains.keychains.isEmpty() ? "No keychains" : "No default keychain";
	}
	return "The Collar";
}

void Collar::launch(const QString &program, const QStringList &args) {
	const QString local = QCoreApplication::applicationDirPath() + "/" + program;
	if (!platinumStartApplication(QFile::exists(local) ? local : program, args)) {
		Alert::ask("Couldn't open " + program + ".", "OK", {});
	}
}

Collar::Visibility Collar::visibilitySetting() {
	char value[16];
	if (pl_setting("collar-visibility", value, sizeof(value))) {
		if (!strcmp(value, "hide")) {
			return Hide;
		}
		if (!strcmp(value, "hotkey")) {
			return HotKey;
		}
	}
	return Show;
}

pl_font Collar::menuFont() {
	char value[16];
	return pl_setting("collar-menu-font", value, sizeof(value)) && !strcmp(value, "views") ? PL_FONT_VIEWS
		: PL_FONT_SYSTEM;
}

void Collar::applySettings() {
	m_applied = true;
	m_visibility = visibilitySetting();
	const bool shown = m_visibility == Show || (m_visibility == HotKey && !m_hidden);
	if (shown == isVisible()) {
		return;
	}
	if (!shown) {
		if (QWidget *menu = QApplication::activePopupWidget()) {
			menu->close();
		}
		hide();
		return;
	}
	show();
	applyGeometry();
}

void Collar::toggle() {
	/* The hot key can beat the file watcher to a just-changed setting. */
	m_visibility = visibilitySetting();
	if (m_visibility != HotKey) {
		return;
	}
	m_hidden = !m_hidden;
	pl_sound_event(m_hidden ? "window-collapse" : "window-expand");
	save();
	applySettings();
}

void Collar::addPhoneItems(const QString &id, std::vector<PopupItem> &items,
		std::vector<std::function<void()>> &actions) {
	auto add = [&](const QString &label, bool enabled, std::function<void()> action = {}) {
		items.emplace_back(label, enabled);
		actions.push_back(std::move(action));
	};
	auto separator = [&] { items.emplace_back(QString(), false, true); actions.emplace_back(); };
	if (const PhoneDevice *phone = m_kdeConnect.device(id)) {
		const PhoneDevice d = *phone;
		if (d.charge >= 0) {
			add(QString("Battery: %1%%2").arg(d.charge).arg(d.charging ? " (charging)" : ""), false);
		}
		if (d.signal >= 0) {
			add(QString("Signal: %1 of 4 bars%2").arg(d.signal).arg(d.network.isEmpty() ? QString() : " (" + d.network + ")"),
				false);
		}
		if (d.charge >= 0 || d.signal >= 0) {
			separator();
		}
		if (d.has("sftp")) {
			add("Browse Device", true, [this, id] { m_kdeConnect.browse(id); });
		}
		if (d.has("clipboard")) {
			add("Send Clipboard", true, [this, id] { m_kdeConnect.sendClipboard(id); });
		}
		if (d.has("findmyphone")) {
			add("Ring Device", true, [this, id] { m_kdeConnect.ring(id); });
		}
		if (d.has("share")) {
			add("Send Files...", true, [this, id, name = d.name] {
				const QList<QUrl> urls = QFileDialog::getOpenFileUrls(nullptr, "Send Files to " + name);
				QStringList list;
				for (const QUrl &url : urls) {
					list << url.toString();
				}
				if (!list.isEmpty()) {
					m_kdeConnect.shareFiles(id, list);
				}
			});
		}
		const QString sms = kdeConnectProgram("kdeconnect-sms");
		if (d.has("sms") && !sms.isEmpty()) {
			add("SMS Messages...", true, [this, sms, id] { launch(sms, { "--device", id }); });
		}
		if (d.has("ping")) {
			add("Send Ping", true, [this, id] { m_kdeConnect.ping(id); });
		}
		if (d.has("remotecommands")) {
			separator();
			add("Run Command", false);
			for (const auto &[key, name] : d.commands) {
				add("  " + name, true, [this, id, key = key] { m_kdeConnect.runCommand(id, key); });
			}
			add("Add Commands...", true, [this, id] { m_kdeConnect.editCommands(id); });
		}
	} else {
		add("No devices connected", false);
	}
	if (!m_kdeConnect.requests.isEmpty()) {
		separator();
		for (const PhoneDevice &request : m_kdeConnect.requests) {
			const QString requestId = request.id;
			add(request.name + " wants to pair", false);
			add("Pair with " + request.name, true, [this, requestId] { m_kdeConnect.acceptPairing(requestId); });
			add("Reject " + request.name, true, [this, requestId] { m_kdeConnect.rejectPairing(requestId); });
		}
	}
	separator();
	QString settings = kdeConnectProgram("kdeconnect-settings");
	if (settings.isEmpty()) {
		settings = kdeConnectProgram("kdeconnect-app");
	}
	add("KDE Connect Settings...", !settings.isEmpty(), [this, settings] { launch(settings); });
}

void Collar::addKeychainItems(std::vector<PopupItem> &items, std::vector<std::function<void()>> &actions,
		int &checked) {
	auto add = [&](const QString &label, bool enabled, std::function<void()> action = {}) {
		items.emplace_back(label, enabled);
		actions.push_back(std::move(action));
	};
	auto separator = [&] { items.emplace_back(QString(), false, true); actions.emplace_back(); };
	if (m_keychains.keychains.isEmpty()) {
		add("No keychains", false);
	}
	// Picking a keychain makes it the default, where apps keep new passwords.
	for (const KeychainInfo &k : m_keychains.keychains) {
		const QString path = k.path;
		if (path == m_keychains.defaultPath) {
			checked = static_cast<int>(items.size());
		}
		add(k.locked ? k.name + " (locked)" : k.name, true,
			[this, path] { m_keychains.setDefault(path); });
	}
	if (!m_keychains.keychains.isEmpty()) {
		separator();
		for (const KeychainInfo &k : m_keychains.keychains) {
			const QString path = k.path;
			if (k.locked) {
				add(QString("Unlock “%1”...").arg(k.name), true, [this, path] { m_keychains.unlock(path); });
			} else {
				add(QString("Lock “%1”").arg(k.name), true, [this, path] { m_keychains.lock(path); });
			}
		}
		const bool anyUnlocked = std::any_of(m_keychains.keychains.cbegin(), m_keychains.keychains.cend(),
			[](const KeychainInfo &k) { return !k.locked; });
		add("Lock All Keychains", anyUnlocked, [this] { m_keychains.lockAll(); });
	}
	const QString access = QStandardPaths::findExecutable("seahorse");
	if (!access.isEmpty()) {
		separator();
		add("Keychain Access...", true, [this, access] { launch(access); });
	}
}

void Collar::openModule(int index) {
	if (index < 0 || index >= m_modules.size()) {
		return;
	}
	if (moduleRect(index).isEmpty()) {
		m_first = std::clamp(index - visibleModules() + 1, 0, int(m_modules.size()) - visibleModules());
		applyGeometry();
	}
	m_pressed = index;
	update();
	const Module module = m_modules[index];
	std::vector<PopupItem> items;
	std::vector<std::function<void()>> actions;
	int checked = -1;
	// A long list (sound sets, display modes) is trimmed to fit the screen, with "More..." after it.
	int listStart = -1, listEnd = -1;
	QString more;
	std::function<void()> moreAction;
	auto add = [&](const QString &label, bool enabled, std::function<void()> action = {}) {
		items.emplace_back(label, enabled);
		actions.push_back(std::move(action));
	};
	auto separator = [&] { items.emplace_back(QString(), false, true); actions.emplace_back(); };
	// Modules can repeat (one per display), so every menu names its device first.
	add(menuLabel(index), false);
	separator();
	const auto s = m_controls.state;
	const bool enabled = !m_controls.updating();
	const CollarDisplay *display = m_displays.display(module.device);
	if (module.kind == Volume) {
		auto it = std::find_if(m_sound.outputs.cbegin(), m_sound.outputs.cend(),
			[](const SoundOutput &output) { return output.current; });
		if (it != m_sound.outputs.cend()) {
			const SoundOutput output = *it;
			add(output.label, false);
			add(output.muted ? "Unmute" : "Mute", !m_sound.switching, [this, output] {
				m_sound.setMuted(output.sink, !output.muted);
			});
			separator();
			for (int volume : { 0, 17, 33, 50, 67, 83, 100 }) {
				if (std::abs(volume - output.volume) <= 8) {
					checked = static_cast<int>(items.size());
				}
				add(QString("%1%").arg(volume), !m_sound.switching,
					[this, output, volume] { m_sound.setVolume(output.sink, volume); });
			}
			separator();
			for (const SoundOutput &target : m_sound.outputs) {
				add("Play on " + target.label, !m_sound.switching,
					[this, target] { m_sound.selectOutput(target); });
			}
		} else {
			add(description(index), false);
		}
		separator();
		add("Open Sound Control Panel", true, [this] { launch("zacos9-controlpanel", { "sound" }); });
	} else if (module.kind == SoundSet) {
		const QString current = currentSoundSet();
		listStart = static_cast<int>(items.size());
		for (int i = 0; i < soundThemeCount(); ++i) {
			if (soundThemeId(i) == current) {
				checked = static_cast<int>(items.size());
			}
			add(soundThemeName(i), true, [this, i] {
				QString error;
				if (!applySoundTheme(i, &error)) {
					Alert::ask(error, "OK", {});
				}
				update();
			});
		}
		listEnd = static_cast<int>(items.size());
		more = "More Sound Sets...";
		moreAction = [this] { launch("zacos9-appearance", { "sound" }); };
		separator();
		add("Open Appearance Control Panel", true, moreAction);
	} else if (module.kind == Network) {
		add(description(index), false);
		add(s.wifi ? "Turn Wi-Fi Off" : "Turn Wi-Fi On", enabled && s.networkAvailable,
			[this, s] { m_controls.setWifi(!s.wifi); });
		add("Network Browser...", true, [this] { launch("zacos9-netbrowser"); });
		separator();
		add("Open TCP/IP Control Panel", true, [this] { launch("zacos9-tcpip"); });
	} else if (module.kind == Bluetooth) {
		add(description(index), false);
		add(s.bluetooth ? "Turn Bluetooth Off" : "Turn Bluetooth On", enabled && !s.adapter.isEmpty(),
			[this, s] { m_controls.setBluetooth(!s.bluetooth); });
		separator();
		add("Open Bluetooth Control Panel", true, [this] { launch("zacos9-bluetooth"); });
	} else if (module.kind == Resolution) {
		if (display) {
			const QString name = display->name;
			listStart = static_cast<int>(items.size());
			for (const QSize &size : display->modes) {
				if (size == display->size) {
					checked = static_cast<int>(items.size());
				}
				add(QString("%1 × %2").arg(size.width()).arg(size.height()), true, [this, name, size] {
					QString error;
					if (!m_displays.setResolution(name, size, &error)) {
						Alert::ask(error, "OK", {});
					}
				});
			}
			listEnd = static_cast<int>(items.size());
			more = "More Resolutions...";
			moreAction = [this] { launch("zacos9-controlpanel", { "monitors" }); };
		} else {
			add(description(index), false);
		}
		separator();
		add("Open Monitors Control Panel", true, [this] { launch("zacos9-controlpanel", { "monitors" }); });
	} else if (module.kind == Brightness) {
		const bool ready = display && display->brightness >= 0 && !m_displays.busy(display->name);
		add(display && display->brightness >= 0 ? QString("Brightness: %1%").arg(display->brightness)
			: display ? display->brightnessError : description(index), false);
		const QString name = module.device;
		for (int percent : { 10, 25, 50, 75, 100 }) {
			if (display && std::abs(display->brightness - percent) <= 6) {
				checked = static_cast<int>(items.size());
			}
			add(QString("%1%").arg(percent), ready, [this, name, percent] { m_displays.setBrightness(name, percent); });
		}
		separator();
		add("Open Monitors Control Panel", true, [this] { launch("zacos9-controlpanel", { "monitors" }); });
	} else if (module.kind == Power) {
		add(description(index), false);
		if (s.batteryPresent) {
			const QStringList states = { "Battery status unknown", "Charging", "On battery", "Battery empty",
				"Fully charged", "Waiting to charge", "Waiting to discharge" };
			add(states.at(s.batteryState), false);
		}
		separator();
		if (!s.sleepError.isEmpty()) {
			add(s.sleepError, false);
		}
		add("Sleep", enabled && (s.canSuspend == "yes" || s.canSuspend == "challenge"), [this] {
			if (Alert::ask("Put this computer to sleep?", "Sleep", "Cancel")) {
				m_controls.suspend();
			}
		});
	} else if (module.kind == Phone) {
		addPhoneItems(module.device, items, actions);
	} else if (module.kind == Keychain) {
		addKeychainItems(items, actions, checked);
	}
	int room = INT_MAX, below = 0;
	if (QScreen *screen = windowHandle()->screen()) {
		room = screen->size().height() - m_bottom - Height - MenuBar;
		below = m_bottom;
	}
	auto menuHeight = [&] {
		int h = 2 + MENU_SHADOW;
		for (const PopupItem &item : items) {
			h += item.separator ? MENU_SEP_H : MENU_ITEM_H;
		}
		return h;
	};
	const int fit = std::max(room, below);
	if (listEnd > listStart && listStart >= 0 && menuHeight() > fit) {
		const int count = listEnd - listStart;
		const int keep = std::clamp((fit - (menuHeight() - count * MENU_ITEM_H) - MENU_ITEM_H) / MENU_ITEM_H, 1, count);
		const int focus = checked >= listStart && checked < listEnd ? checked - listStart : 0;
		const int first = std::clamp(focus - keep / 2, 0, count - keep);
		const auto trim = [&](auto &list) {
			list.erase(list.begin() + listStart + first + keep, list.begin() + listEnd);
			list.erase(list.begin() + listStart, list.begin() + listStart + first);
		};
		trim(items);
		trim(actions);
		if (checked >= listStart) {
			checked = checked < listEnd ? checked - first : -1;
		}
		items.insert(items.begin() + listStart + keep, PopupItem(more));
		actions.insert(actions.begin() + listStart + keep, moreAction);
	}
	panelMenuNear(this, items, checked, moduleRect(index), room, menuFont(), [actions](int chosen) {
		if (chosen >= 0 && chosen < static_cast<int>(actions.size()) && actions[chosen]) {
			// After the action, so choosing a sound set is heard in the new set.
			actions[chosen]();
			pl_sound_event("menu-command");
		}
	}, [this] { m_pressed = -1; update(); });
}

void Collar::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	m_start = event->globalPosition().toPoint();
	m_pressPoint = event->position().toPoint();
	m_startBottom = m_bottom;
	m_startVisible = visibleModules();
	m_dragged = false;
	m_part = partAt(m_pressPoint);
	// ⌘ arrives as Control: the compositor maps the Command (logo) key for Qt clients.
	m_moving = m_part != None && (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
	if (m_moving) {
		setCursor(Qt::SizeVerCursor);
		return;
	}
	m_resizing = m_part == GripPart && !m_collapsed;
	if (m_resizing) {
		setCursor(Qt::SizeHorCursor);
		return;
	}
	if (m_part == ModulePart) {
		m_pressed = moduleAt(m_pressPoint);
		m_focus = m_pressed;
		openModule(m_pressed);
	}
}

void Collar::stopMoving(bool cancel) {
	if (!m_moving) {
		return;
	}
	m_moving = false;
	m_part = None;
	unsetCursor();
	if (m_dragSound) {
		m_dragSound = false;
		pl_sound_loop_stop();
		pl_sound_event("window-drag-end");
	}
	if (cancel) {
		m_bottom = m_startBottom;
		applyGeometry();
	} else if (m_dragged) {
		save();
	}
}

void Collar::mouseMoveEvent(QMouseEvent *event) {
	const QPoint delta = event->globalPosition().toPoint() - m_start;
	if (m_moving) {
		if (!m_dragged && std::abs(delta.y()) >= QApplication::startDragDistance()) {
			m_dragged = true;
			m_dragSound = true;
			pl_sound_loop_start("window-drag");
		}
		if (m_dragged) {
			m_bottom = std::max(0, m_startBottom - delta.y());
			applyGeometry();
		}
		return;
	}
	if (m_resizing) {
		m_dragged |= std::abs(delta.x()) >= QApplication::startDragDistance();
		if (m_dragged) {
			setVisibleModules(m_startVisible + qRound(delta.x() / double(Cell + 1)));
		}
		return;
	}
	const QPoint point = event->position().toPoint();
	const Part part = partAt(point);
	setToolTip(part == ModulePart ? description(moduleAt(point))
		: part == GripPart ? (m_collapsed ? "Click to open The Collar. ⌘-drag to move it up or down."
			: "Click to close The Collar; drag to show more or fewer modules. ⌘-drag to move it.")
		: part == CloseBox ? "Close The Collar"
		: QString());
}

void Collar::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	if (m_moving) {
		stopMoving(false);
		return;
	}
	if (m_resizing) {
		m_resizing = false;
		unsetCursor();
		if (!m_dragged) {
			setCollapsed(true);
		}
		return;
	}
	if (m_pressed >= 0 && QApplication::activePopupWidget()) {
		return;
	}
	const Part pressed = m_part;
	m_part = None;
	m_pressed = -1;
	update();
	if (pressed == None || partAt(event->position().toPoint()) != pressed) {
		return;
	}
	if (pressed == GripPart && m_collapsed) {
		setCollapsed(false);
	} else if (pressed == CloseBox) {
		setCollapsed(true);
	} else if (pressed == LeftArrow && m_first > 0) {
		--m_first;
		update();
	} else if (pressed == RightArrow && m_first + visibleModules() < m_modules.size()) {
		++m_first;
		update();
	}
}

void Collar::hideEvent(QHideEvent *event) {
	stopMoving(true);
	QWidget::hideEvent(event);
}

void Collar::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		if (m_moving) {
			stopMoving(true);
		} else {
			setCollapsed(true);
		}
	} else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
		if (m_collapsed) {
			setCollapsed(false);
		}
		const int count = m_modules.size();
		m_focus = std::clamp(m_focus + (event->key() == Qt::Key_Right ? 1 : -1), 0, count - 1);
		m_first = std::clamp(m_first, std::max(0, m_focus - visibleModules() + 1),
			std::min(m_focus, count - visibleModules()));
		update();
	} else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
		if (m_collapsed) {
			setCollapsed(false);
		} else {
			openModule(m_focus);
		}
	} else {
		QWidget::keyPressEvent(event);
	}
}
