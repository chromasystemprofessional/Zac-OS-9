#include "collar.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QRegion>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cmath>

#include "alert.h"
#include "panelkit.h"
#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

static QString configPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/collar.conf";
}

Collar::Collar(bool monitor)
	: m_sound(this), m_controls(this), m_settings(configPath(), QSettings::IniFormat) {
	setWindowTitle("The Collar");
	setAccessibleName("The Collar");
	setAccessibleDescription("Quick volume, network, Bluetooth, brightness and power controls.");
	setAttribute(Qt::WA_TranslucentBackground);
	setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	m_collapsed = m_settings.value("collapsed", false).toBool();
	m_visible = std::clamp(m_settings.value("visible", ModuleCount).toInt(), 1, int(ModuleCount));
	m_bottom = std::max(0, m_settings.value("bottom", 4).toInt());
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
	if (monitor) {
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { m_sound.refresh(); m_controls.refresh(); });
		timer->start(5000);
		m_sound.refresh();
		m_controls.refresh();
	}
}

QRect Collar::moduleRect(int module) const {
	if (m_collapsed || module < m_first || module >= m_first + m_visible) {
		return {};
	}
	return QRect(Cap + Scroll + (module - m_first) * Cell, 0, Cell, Height);
}

int Collar::moduleAt(QPoint point) const {
	for (int i = m_first; i < m_first + m_visible; ++i) {
		if (moduleRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void Collar::applyGeometry() {
	QScreen *screen = windowHandle()->screen();
	if (screen) {
		m_bottom = std::clamp(m_bottom, 0, std::max(0, screen->size().height() - Height - 22));
	}
	setFixedSize(m_collapsed ? Cap : Cap + Scroll * 2 + Cell * m_visible + Tab, Height);
	QRegion shape;
	for (int y = 0; y < Height; ++y) {
		const int left = y < 8 ? 8 - y : y > 15 ? y - 15 : 0;
		shape += QRect(left, y, width() - left, 1);
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
	m_settings.setValue("visible", m_visible);
	m_settings.setValue("bottom", m_bottom);
	m_settings.sync();
	if (m_settings.status() != QSettings::NoError) {
		qWarning("The Collar couldn't save its position and size.");
		Alert::ask("The Collar couldn't save its position and size.", "OK", {});
	}
}

void Collar::setCollapsed(bool collapsed) {
	m_collapsed = collapsed;
	applyGeometry();
	save();
}

void Collar::setVisibleModules(int count) {
	m_visible = std::clamp(count, 1, int(ModuleCount));
	m_first = std::min(m_first, int(ModuleCount) - m_visible);
	applyGeometry();
	save();
}

static constexpr uint32_t CollarFace = RGB(0xC0, 0xC0, 0xC0);
static constexpr uint32_t CollarShadow = RGB(0x80, 0x80, 0x80);

static void menuArrow(pl_canvas *c, int x, int y) {
	for (int i = 0; i < 4; ++i) {
		pl_vline(c, x + i, y + i, y + 8 - i, C_BLACK);
	}
}

static void scrollArrow(pl_canvas *c, int x, bool right, bool enabled) {
	const uint32_t ink = enabled ? C_BLACK : CollarShadow;
	static const char *const rows[] = {
		"....#...", "...##...", "..#.#...", ".#..####", "#......#",
		".#..####", "..#.#...", "...##...", "....#...",
	};
	for (int row = 0; row < 9; ++row) {
		for (int column = 0; column < 8; ++column) {
			if (rows[row][column] == '#') {
				pl_put(c, x + (right ? 7 - column : column), 8 + row, ink);
			}
		}
	}
	pl_hline(c, x + 1, x + 6, 12, C_WHITE);
}

static void cellFrame(pl_canvas *c, int x, int width, bool inset) {
	const int end = x + width - 1;
	pl_fill(c, x, 0, end, Collar::Height - 1, CollarFace);
	pl_outline(c, x, 0, end, Collar::Height - 1, C_BLACK);
	pl_hline(c, x + 1, end - 1, 1, inset ? CollarShadow : C_WHITE);
	pl_vline(c, x + 1, 1, Collar::Height - 2, inset ? CollarShadow : C_WHITE);
	pl_hline(c, x + 2, end - 2, 2, inset ? CollarFace : GRAY(0xD));
	pl_vline(c, x + 2, 2, Collar::Height - 3, inset ? CollarFace : GRAY(0xD));
	pl_hline(c, x + 2, end - 2, Collar::Height - 3, inset ? CollarFace : GRAY(0x9));
	pl_vline(c, end - 2, 2, Collar::Height - 3, inset ? CollarFace : GRAY(0x9));
	pl_hline(c, x + 1, end - 1, Collar::Height - 2, inset ? C_WHITE : CollarShadow);
	pl_vline(c, end - 1, 1, Collar::Height - 2, inset ? C_WHITE : CollarShadow);
}

static void grip(pl_canvas *c, bool collapsed) {
	for (int y = 0; y < Collar::Height; ++y) {
		const int left = y < 8 ? 8 - y : y > 15 ? y - 15 : 0;
		pl_hline(c, left, Collar::Cap - 1, y, CollarFace);
		pl_put(c, left, y, C_BLACK);
		pl_put(c, Collar::Cap - 1, y, C_BLACK);
		if (y == 0 || y == Collar::Height - 1) {
			pl_hline(c, left, Collar::Cap - 1, y, C_BLACK);
		} else if (y < 16) {
			pl_put(c, left + 1, y, C_WHITE);
		} else {
			pl_put(c, left + 1, y, CollarShadow);
		}
	}
	pl_hline(c, 8, Collar::Cap - 2, 1, C_WHITE);
	pl_hline(c, 8, Collar::Cap - 2, Collar::Height - 2, CollarShadow);
	pl_vline(c, Collar::Cap - 2, 2, Collar::Height - 3, CollarShadow);
	for (int row = 0; row < 5; ++row) {
		const int y = 4 + row * 3;
		const int x = row % 2 ? 12 : 9;
		pl_put(c, x, y, C_WHITE);
		pl_put(c, x + 1, y + 1, CollarShadow);
	}
	pl_vline(c, 6, 7, 16, C_BLACK);
	pl_vline(c, 5, 8, 15, C_WHITE);
	pl_vline(c, 2, 9, 14, C_BLACK);
	pl_put(c, 3, 8, C_BLACK);
	pl_put(c, 4, 7, C_BLACK);
	pl_put(c, 3, 15, C_BLACK);
	pl_put(c, 4, 16, C_BLACK);
	if (collapsed) {
		menuArrow(c, 9, 8);
	}
}

static void icon(pl_canvas *c, int module, int x, int y, bool on) {
	const uint32_t ink = on ? C_BLACK : CollarShadow;
	const uint32_t blue = on ? RGB(0x66, 0x66, 0xCC) : GRAY(0xA);
	const uint32_t light = on ? RGB(0xCC, 0xCC, 0xFF) : GRAY(0xD);
	const uint32_t gold = on ? RGB(0xEE, 0xBB, 0x33) : GRAY(0xB);
	const uint32_t lineInk = module == Collar::Bluetooth ? (on ? C_WHITE : GRAY(0xD)) : ink;
	auto line = [=](int x0, int y0, int x1, int y1) {
		const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
		const int n = std::max(dx, dy);
		for (int i = 0; i <= n; ++i) {
			pl_put(c, x + x0 + (n ? qRound((x1 - x0) * double(i) / n) : 0),
				y + y0 + (n ? qRound((y1 - y0) * double(i) / n) : 0), lineInk);
		}
	};
	if (module == Collar::Volume) {
		for (int row = 0; row < 13; ++row) {
			const int edge = row < 6 ? 8 - row : row > 8 ? row - 6 : 2;
			pl_hline(c, x + edge, x + 8, y + 2 + row, blue);
		}
		pl_vline(c, x + 7, y + 3, y + 13, light);
		pl_fill(c, x + 2, y + 7, x + 3, y + 9, light);
		pl_outline(c, x + 1, y + 6, x + 4, y + 10, ink);
		line(4, 6, 8, 2); line(8, 2, 8, 14); line(8, 14, 4, 10);
		line(11, 5, 13, 7); line(13, 7, 13, 9); line(13, 9, 11, 11);
		line(14, 2, 16, 5); line(16, 5, 16, 11); line(16, 11, 14, 14);
	} else if (module == Collar::Network) {
		pl_fill(c, x + 1, y + 2, x + 5, y + 5, light);
		pl_fill(c, x + 11, y + 2, x + 15, y + 5, light);
		pl_outline(c, x, y + 1, x + 6, y + 6, ink);
		pl_outline(c, x + 10, y + 1, x + 16, y + 6, ink);
		line(3, 6, 3, 11); line(13, 6, 13, 11); line(1, 11, 15, 11);
		line(8, 11, 8, 15); line(5, 15, 11, 15);
	} else if (module == Collar::Bluetooth) {
		pl_fill(c, x + 1, y, x + 14, y + 16, blue);
		pl_outline(c, x + 1, y, x + 14, y + 16, ink);
		pl_vline(c, x + 2, y + 1, y + 15, light);
		line(7, 1, 7, 15); line(7, 1, 12, 5); line(12, 5, 3, 13);
		line(3, 3, 12, 11); line(12, 11, 7, 15);
	} else if (module == Collar::Brightness) {
		pl_outline(c, x + 5, y + 5, x + 11, y + 11, ink);
		pl_fill(c, x + 6, y + 6, x + 10, y + 10, gold);
		pl_hline(c, x + 6, x + 9, y + 6, C_WHITE);
		line(8, 0, 8, 2); line(8, 14, 8, 16); line(0, 8, 2, 8); line(14, 8, 16, 8);
		line(2, 2, 3, 3); line(13, 13, 14, 14); line(2, 14, 3, 13); line(13, 3, 14, 2);
	} else {
		pl_outline(c, x, y + 4, x + 14, y + 12, ink);
		pl_fill(c, x + 15, y + 6, x + 16, y + 10, ink);
		pl_fill(c, x + 2, y + 6, x + 11, y + 10, on ? RGB(0x66, 0x99, 0x55) : GRAY(0xA));
		pl_hline(c, x + 2, x + 11, y + 6, on ? RGB(0xBB, 0xDD, 0x99) : GRAY(0xD));
	}
}

void Collar::paintEvent(QPaintEvent *) {
	Pixels px(width(), Height);
	auto *c = &px.c;
	grip(c, m_collapsed);
	if (!m_collapsed) {
		cellFrame(c, Cap, Scroll, true);
		scrollArrow(c, Cap + 3, false, m_first > 0);
		const auto &s = m_controls.state;
		for (int i = m_first; i < m_first + m_visible; ++i) {
			const QRect r = moduleRect(i);
			const bool active = i == Volume ? !m_sound.outputs.isEmpty() :
				i == Network ? s.networkAvailable : i == Bluetooth ? !s.adapter.isEmpty() :
				i == Brightness ? !s.backlight.isEmpty() : s.powerError.isEmpty();
			cellFrame(c, r.left(), Cell, m_pressed == i);
			icon(c, i, r.left() + 4, 3, active);
			menuArrow(c, r.left() + 24, 8);
			if ((i == Bluetooth && !s.bluetooth) || (i == Network && !s.wifi)) {
				pl_hline(c, r.left() + 4, r.left() + 20, 20, CollarShadow);
			}
			if (i == Volume) {
				for (const SoundOutput &output : m_sound.outputs) {
					if (output.current && output.muted) {
						pl_hline(c, r.left() + 4, r.left() + 20, 20, C_BLACK);
					}
				}
			}
			if (i == Power && s.batteryPresent) {
				const int fill = qRound(10 * s.batteryPercent / 100);
				pl_fill(c, r.left() + 6, 9, r.left() + 15, 13, CollarFace);
				if (fill) {
					pl_fill(c, r.left() + 6, 9, r.left() + 5 + fill, 13,
						s.batteryPercent < 15 ? RGB(0xAA, 0x22, 0x22) : RGB(0x66, 0x99, 0x55));
				}
			}
			if (hasFocus() && m_focus == i) {
				for (int x = r.left() + 3; x < r.right() - 2; x += 2) {
					pl_put(c, x, Height - 5, C_BLACK);
				}
			}
		}
		const int end = Cap + Scroll + Cell * m_visible;
		cellFrame(c, end, Scroll, true);
		scrollArrow(c, end + 3, true, m_first + m_visible < ModuleCount);
		cellFrame(c, end + Scroll, Tab, false);
		pl_outline(c, end + Scroll + 3, 8, end + Scroll + 10, 15, C_WHITE);
		pl_outline(c, end + Scroll + 4, 9, end + Scroll + 9, 14, CollarShadow);
	}
	QPainter painter(this);
	px.blit(painter);
}

QString Collar::description(int module) const {
	const auto &s = m_controls.state;
	switch (module) {
	case Volume:
		for (const auto &o : m_sound.outputs) {
			if (o.current) {
				return QString("Volume: %1%2").arg(o.volume).arg(o.muted ? "% (muted)" : "%");
			}
		}
		return m_soundError.isEmpty() ? "No sound output available" : m_soundError;
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
	case Brightness:
		return s.brightness < 0 ? s.brightnessError : QString("Brightness: %1%").arg(s.brightness);
	case Power:
		if (!s.powerError.isEmpty()) {
			return s.powerError;
		}
		return s.batteryPresent ? QString("Battery: %1%").arg(qRound(s.batteryPercent)) : "AC power (no battery)";
	default:
		return "The Collar";
	}
}

void Collar::launch(const QString &program, const QStringList &args) {
	const QString local = QCoreApplication::applicationDirPath() + "/" + program;
	if (!platinumStartApplication(QFile::exists(local) ? local : program, args)) {
		Alert::ask("Couldn't open " + program + ".", "OK", {});
	}
}

void Collar::openModule(int module) {
	if (moduleRect(module).isEmpty()) {
		m_first = std::clamp(module - m_visible + 1, 0, int(ModuleCount) - m_visible);
		applyGeometry();
	}
	m_pressed = module;
	update();
	std::vector<PopupItem> items;
	std::vector<std::function<void()>> actions;
	int checked = -1;
	auto add = [&](const QString &label, bool enabled, std::function<void()> action = {}) {
		items.emplace_back(label, enabled);
		actions.push_back(std::move(action));
	};
	auto separator = [&] { items.emplace_back(QString(), false, true); actions.emplace_back(); };
	const auto s = m_controls.state;
	const bool enabled = !m_controls.updating();
	if (module == Volume) {
		add("Open Sound Control Panel", true, [this] { launch("zacos9-controlpanel", { "sound" }); });
		separator();
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
			add(description(Volume), false);
		}
	} else if (module == Network) {
		add("Open TCP/IP Control Panel", true, [this] { launch("zacos9-tcpip"); });
		separator();
		add(description(Network), false);
		add(s.wifi ? "Turn Wi-Fi Off" : "Turn Wi-Fi On", enabled && s.networkAvailable,
			[this, s] { m_controls.setWifi(!s.wifi); });
		add("Network Browser...", true, [this] { launch("zacos9-netbrowser"); });
	} else if (module == Bluetooth) {
		add("Open Bluetooth Control Panel", true, [this] { launch("zacos9-bluetooth"); });
		separator();
		add(description(Bluetooth), false);
		add(s.bluetooth ? "Turn Bluetooth Off" : "Turn Bluetooth On", enabled && !s.adapter.isEmpty(),
			[this, s] { m_controls.setBluetooth(!s.bluetooth); });
	} else if (module == Brightness) {
		add("Open Monitors Control Panel", true, [this] { launch("zacos9-controlpanel", { "monitors" }); });
		separator();
		add(description(Brightness), false);
		for (int percent : { 10, 25, 50, 75, 100 }) {
			if (s.brightness == percent) {
				checked = static_cast<int>(items.size());
			}
			add(QString("%1%").arg(percent), enabled && !s.backlight.isEmpty(),
				[this, percent] { m_controls.setBrightness(percent); });
		}
	} else if (module == Power) {
		add(description(Power), false);
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
	}
	panelMenuAbove(this, items, checked, moduleRect(module), [actions](int index) {
		if (index >= 0 && index < static_cast<int>(actions.size()) && actions[index]) {
			pl_sound_event("menu-command");
			actions[index]();
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
	m_startVisible = m_visible;
	m_dragged = false;
	m_moving = event->modifiers().testFlag(Qt::AltModifier);
	m_resizing = !m_moving && !m_collapsed && event->position().x() >= width() - Tab;
	if (m_moving || m_resizing) {
		setCursor(m_moving ? Qt::ClosedHandCursor : Qt::SizeHorCursor);
		return;
	}
	const QPoint point = event->position().toPoint();
	m_pressed = moduleAt(point);
	update();
	if (m_pressed >= 0) {
		m_focus = m_pressed;
		openModule(m_pressed);
	}
}

void Collar::mouseMoveEvent(QMouseEvent *event) {
	const QPoint delta = event->globalPosition().toPoint() - m_start;
	if (m_moving || m_resizing) {
		m_dragged |= delta.manhattanLength() >= QApplication::startDragDistance();
		if (m_moving && m_dragged) {
			m_bottom = m_startBottom - delta.y();
			applyGeometry();
		} else if (m_resizing && m_dragged) {
			m_visible = std::clamp(m_startVisible + qRound(delta.x() / double(Cell)), 1, int(ModuleCount));
			m_first = std::min(m_first, int(ModuleCount) - m_visible);
			applyGeometry();
		}
		return;
	}
	const int module = moduleAt(event->position().toPoint());
	setToolTip(module >= 0 ? description(module) : "The Collar: click the tab to fold; Alt-drag to move.");
}

void Collar::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	if (m_moving || m_resizing) {
		const bool fold = m_resizing && !m_dragged;
		m_moving = m_resizing = false;
		unsetCursor();
		if (fold) {
			setCollapsed(true);
		} else {
			save();
		}
		return;
	}
	if (m_pressed >= 0 && QApplication::activePopupWidget()) {
		return;
	}
	const QPoint point = event->position().toPoint();
	m_pressed = -1;
	update();
	if (!rect().contains(point)) {
		return;
	}
	if ((m_collapsed || point.x() < Cap) && (m_collapsed || m_pressPoint.x() < Cap)) {
		setCollapsed(!m_collapsed);
	} else if (point.x() >= Cap && point.x() < Cap + Scroll &&
			m_pressPoint.x() >= Cap && m_pressPoint.x() < Cap + Scroll) {
		m_first = std::max(0, m_first - 1);
		update();
	} else if (point.x() >= width() - Tab - Scroll && m_pressPoint.x() >= width() - Tab - Scroll) {
		m_first = std::min(int(ModuleCount) - m_visible, m_first + 1);
		update();
	}
}

void Collar::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		setCollapsed(true);
	} else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
		if (m_collapsed) {
			setCollapsed(false);
		}
		m_focus = std::clamp(m_focus + (event->key() == Qt::Key_Right ? 1 : -1), 0, int(ModuleCount) - 1);
		m_first = std::clamp(m_first, std::max(0, m_focus - m_visible + 1), std::min(m_focus, int(ModuleCount) - m_visible));
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
