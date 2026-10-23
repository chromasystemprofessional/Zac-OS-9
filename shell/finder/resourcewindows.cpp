#include "resourcewindows.h"

#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWheelEvent>
#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

#include "alert.h"
#include "extensioncatalog.h"
#include "modules.h"
#include "tricksets.h"
#include "autostart.h"
#include "icons.h"
#include "infowindow.h"
#include "panelkit.h"
#include "pixels.h"
#include "resources.h"
#include "userservices.h"
#include "platinumshell.h"
#include "vfs.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD);

void text(pl_canvas *canvas, const QString &value, int x, int y, int width,
		pl_font font = PL_FONT_VIEWS, uint32_t color = C_BLACK) {
	Text rendered(value, width, font);
	if (rendered.t) {
		pl_text(canvas, rendered.t, x, y, color);
	}
}

class ResourceListWindow : public QWidget {
public:
	enum class Page { System, Extensions };
	enum Category { Extensions = 0, LoginItems = 1, Services = 2 };

	explicit ResourceListWindow(Page page) : m_page(page) {
		setAttribute(Qt::WA_DeleteOnClose);
		setMouseTracking(true);
		setWindowTitle(page == Page::System ? "System Information" : "Extensions Manager");
		setFixedSize(page == Page::System ? QSize(520, 420) : QSize(600, 460));
		m_title = page == Page::System ? "System Information" : "Extensions Manager";
		m_list.frame = page == Page::System ? QRect(12, 78, 496, 326) :
			QRect(12, 96, 240, 320);
		if (page == Page::Extensions) {
			m_showAll = PanelCheckbox("Show All", QPoint(500, 73));
			m_showAll.toggled = [this](bool) { loadCategory(); };
			m_categories.add("Extensions", QPoint(14, 73));
			m_categories.add("Login Items", QPoint(110, 73));
			m_categories.add("Background Services", QPoint(214, 73));
			m_categories.changed = [this](int) { loadCategory(); };
			m_info = PanelButton("Item Information…", QRect(12, 424, 150, 20));
			m_info.clicked = [this] { showSelectedInfo(); };
			m_toggle = PanelButton("Turn Off", QRect(270, 424, 150, 20));
			m_toggle.clicked = [this] { toggleSelected(); };
			m_run = PanelButton("Stop", QRect(430, 424, 150, 20));
			m_run.clicked = [this] { runSelected(); };
			m_list.picked = [this](int) { updateButtons(); };
			/* Mac OS 9's Selected Set: named on/off choices, applied at once. */
			m_set.rect = QRect(width() - 12 - 230, 13, 230, PL_POPUP_H);
			m_set.label = "Set:";
			m_set.chosen = [this](int row) { chooseSet(row); };
			fillSets();
			watchStartupSources();
			loadCategory();
		} else {
			m_explanation = "Current system details; viewing does not change this computer.";
			m_rows = systemInformation();
			m_list.setItems(m_rows);
			if (!m_rows.isEmpty()) {
				m_list.select(0, false);
			}
		}
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels pixels(width(), height());
		pl_fill(&pixels.c, 0, 0, width() - 1, height() - 1, FACE);
		text(&pixels.c, m_title, 16, 25, width() - 32, PL_FONT_SYSTEM);
		pl_hline(&pixels.c, 12, width() - 13, 36, GRAY(0x8));
		pl_hline(&pixels.c, 13, width() - 12, 37, C_WHITE);
		text(&pixels.c, m_explanation, 14, 57, width() - 28);
		m_list.paint(&pixels.c, true);
		if (m_page == Page::Extensions) {
			m_set.paint(&pixels.c);
			m_categories.paint(&pixels.c);
			m_info.paint(&pixels.c);
			if (category() == Extensions) {
				m_showAll.paint(&pixels.c);
			} else {
				m_toggle.paint(&pixels.c);
				if (category() == Services) {
					m_run.paint(&pixels.c);
				}
			}
			paintDetails(&pixels.c);
		}
		QPainter painter(this);
		pixels.blit(painter);
	}

	void keyPressEvent(QKeyEvent *event) override {
		if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_W) {
			close();
		} else if (m_page == Page::Extensions &&
				(event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_I) {
			showSelectedInfo();
		} else if (m_list.key(event->key(), event->text())) {
			update();
		}
	}

	void mousePressEvent(QMouseEvent *event) override {
		const QPoint pos = event->position().toPoint();
		if (m_list.press(pos) || (m_page == Page::Extensions && pressExtras(pos))) {
			update();
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		const QPoint pos = event->position().toPoint();
		if (m_list.move(pos) || (m_page == Page::Extensions && moveExtras(pos))) {
			update();
		}
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		const bool list = m_list.release();
		const bool box = m_page == Page::Extensions && releaseExtras(event->position().toPoint());
		if (list || box) {
			update();
		}
	}

	void wheelEvent(QWheelEvent *event) override {
		if (m_list.wheel(event->position().toPoint(), event->angleDelta().y())) {
			update();
		}
	}

private:
	Category category() const { return Category(m_categories.selected); }

	bool pressExtras(QPoint pos) {
		if (m_set.press(this, pos)) {
			return true;
		}
		bool hit = m_categories.press(pos);
		hit = hit || m_info.press(pos);
		if (category() == Extensions) {
			return hit || m_showAll.press(pos);
		}
		return hit || m_toggle.press(pos) || (category() == Services && m_run.press(pos));
	}
	bool moveExtras(QPoint pos) {
		bool hit = m_categories.move(pos);
		hit = hit || m_info.move(pos);
		if (category() == Extensions) {
			return hit || m_showAll.move(pos);
		}
		return hit || m_toggle.move(pos) || (category() == Services && m_run.move(pos));
	}
	bool releaseExtras(QPoint pos) {
		bool hit = m_categories.release(pos);
		hit = hit || m_info.release(pos);
		if (category() == Extensions) {
			return hit || m_showAll.release(pos);
		}
		return hit || m_toggle.release(pos) || (category() == Services && m_run.release(pos));
	}

	/* Rows come from the chosen category. Extensions can be turned on or
	 * off for start-up; login items and services can too. */
	void loadCategory() {
		const int keep = m_list.state.selected;
		const QString keepKey = keep >= 0 && keep < int(m_resources.size()) ?
			m_resources[size_t(keep)].key : QString();
		m_resources.clear();
		m_state.clear();
		if (category() == Extensions) {
			loadExtensions();
		} else if (category() == LoginItems) {
			loadLoginItems();
		} else {
			loadServices();
		}
		m_rows.clear();
		std::vector<int> icons;
		for (const SystemResource &resource : m_resources) {
			m_rows << resource.name;
			icons.push_back(resource.icon);
		}
		m_list.state.top = 0;
		m_list.state.selected = -1;
		m_list.setItems(m_rows, icons);
		int select = m_rows.isEmpty() ? -1 : 0;
		for (int i = 0; i < int(m_resources.size()) && !keepKey.isEmpty(); i++) {
			if (m_resources[size_t(i)].key == keepKey) {
				select = i;
			}
		}
		if (select >= 0) {
			m_list.select(select, false);
		}
		updateButtons();
		update();
	}

	/* The curated entries, or under Show All every loaded module. */
	void loadExtensions() {
		m_resources = resourceChildren(m_showAll.on ? "device-drivers" : "extensions");
		std::stable_sort(m_resources.begin(), m_resources.end(),
			[](const SystemResource &a, const SystemResource &b) {
				return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
			});
		m_explanation = m_showAll.on
			? "Every loaded kernel module, by its technical name. For viewing only."
			: "Extensions add features to this computer. Turn one off to stop it loading "
				"at start-up, or on to start it at every start-up. Changes take effect "
				"after restart.";
		m_state.assign(m_resources.size(), RowState());
		if (!m_showAll.on) {
			for (size_t i = 0; i < m_resources.size(); i++) {
				const ExtensionEntry entry = extensionEntry(m_resources[i].key);
				RowState st;
				st.canToggle = !entry.key.isEmpty();
				st.on = entry.startup;
				m_state[i] = st;
			}
		}
	}

	void loadLoginItems() {
		m_explanation = "Applications that open when you log in. Changes affect only your "
			"account and take effect at your next login.";
		for (const AutostartEntry &e : autostartEntries()) {
			SystemResource r;
			r.key = e.id;
			r.name = e.enabled ? e.name : e.name + " (off)";
			r.kind = "login item";
			r.icon = PL_ICON_APPLICATION;
			r.description = e.description.isEmpty() ? "A login item." : e.description;
			r.metadata = { QString("Status: ") + (e.enabled ? "opens at login" :
					(e.reason.isEmpty() ? "off" : e.reason)),
				QString("Source: ") + (e.overridesSystem ? "your settings (overrides a system item)" :
					e.userEntry ? "your account" : "this computer (shared by all users)"),
				"Takes effect: at your next login" };
			RowState st;
			st.canToggle = e.enabled || e.reason == "Disabled";
			st.on = e.enabled;
			if (!st.canToggle) {
				r.metadata << "Read-only: " + e.reason;
			}
			m_resources.push_back(r);
			m_state.push_back(st);
		}
		if (m_resources.empty()) {
			m_explanation = "No login items were found.";
		}
	}

	void loadServices() {
		if (!m_serviceResultValid) {
			m_explanation = m_serviceQuerying ? "Checking your account's background services…"
				: "Background service status is not available.";
			if (!m_serviceQuerying) {
				requestServiceRefresh();
			}
			return;
		}
		if (!m_serviceResult.error.isEmpty()) {
			m_explanation = "Background services could not be listed: " + m_serviceResult.error;
			return;
		}
		m_explanation = "Background services of your own account. Essential services are "
			"shown but can't be changed here.";
		for (const UserService &u : m_serviceResult.services) {
			SystemResource r;
			r.key = u.unit;
			r.name = u.unit.chopped(8);
			r.kind = "background service";
			r.icon = PL_ICON_EXT_GENERIC;
			r.description = u.description.isEmpty() ? "A background service." : u.description;
			r.metadata = { "Running now: " + QString(u.activeState == "active" ? "yes" : "no"),
				"Starts at login: " + QString(u.fileState == "enabled" ? "yes" :
					u.fileState == "disabled" ? "no" : "set by the system") };
			RowState st;
			st.canToggle = u.manageable;
			st.on = u.fileState == "enabled";
			st.canRun = u.manageable;
			st.running = u.activeState == "active";
			if (u.manageable) {
				r.metadata << "Login changes take effect: at your next login"
					<< "Start and Stop take effect: immediately";
			} else {
				r.metadata << "Read-only: " + u.reason;
			}
			m_resources.push_back(r);
			m_state.push_back(st);
		}
		if (m_resources.empty()) {
			m_explanation = "No user services are available.";
		}
	}

	void updateButtons() {
		const int row = m_list.state.selected;
		const bool valid = row >= 0 && row < int(m_state.size());
		const RowState st = valid ? m_state[size_t(row)] : RowState();
		m_info.enabled = valid;
		m_toggle.enabled = st.canToggle && !m_serviceActionPending;
		m_toggle.label = std::make_unique<Text>(category() == Services ?
			(st.on ? "Don't Start at Login" : "Start at Login") :
			(st.on ? "Turn Off" : "Turn On"), 142, PL_FONT_SYSTEM);
		m_run.enabled = st.canRun && !m_serviceActionPending;
		m_run.label = std::make_unique<Text>(st.running ? "Stop Now" : "Start Now", 150,
			PL_FONT_SYSTEM);
		update();
	}

	void showSelectedInfo() {
		const int row = m_list.state.selected;
		if (row < 0 || row >= int(m_resources.size())) {
			return;
		}
		const SystemResource &resource = m_resources[size_t(row)];
		ResourceDetails details;
		details.name = resource.name;
		details.kind = resource.kind;
		details.description = resource.description;
		details.metadata = resource.metadata;
		details.location = category() == Extensions ? "System Folder:Extensions" :
			category() == LoginItems ? "System Folder:Startup Items" : "Extensions Manager";
		details.access = category() == Extensions ?
			"Turn it on or off; the change takes effect after restart." :
			category() == LoginItems ?
				"Changes affect only your account and take effect at your next login." :
				"Changes affect only your account's user services.";
		const QString path = QStringLiteral("tricks-manager:") +
			QString::number(int(category())) + ':' + resource.key;
		InfoWindow::openVirtual(path, resource.icon, resource.name, details);
	}

	void toggleSelected() {
		const int row = m_list.state.selected;
		if (row < 0 || row >= int(m_state.size()) || !m_state[size_t(row)].canToggle) {
			return;
		}
		const bool turnOn = !m_state[size_t(row)].on;
		const QString name = m_resources[size_t(row)].name;
		const QString key = m_resources[size_t(row)].key;
		if (category() == Extensions) {
			const ExtensionEntry entry = extensionEntry(key);
			QStringList modules = entry.modules;
			if (modules.isEmpty()) {
				modules << key;
			}
			m_serviceActionPending = true;
			updateButtons();
			setExtensionEnabledAsync(this, key, turnOn, modules,
				[this](bool success, const QString &error) {
					m_serviceActionPending = false;
					if (!success) {
						Alert::ask(error, "OK", QString());
					}
					loadCategory();
				});
			return;
		}
		if (!turnOn && !Alert::ask("Turn off “" + name + "”? It will no longer start when you log "
				"in. This takes effect at your next login.", "Turn Off", "Cancel")) {
			return;
		}
		QString error;
		bool ok;
		if (category() == LoginItems) {
			ok = autostartSetEnabled(autostartDefaultContext(), key, turnOn, &error);
		} else {
			m_serviceActionPending = true;
			updateButtons();
			userServiceActAsync(this, key, turnOn ? UserServiceAction::EnableAtLogin :
				UserServiceAction::DisableAtLogin, [this](bool success, const QString &message) {
				m_serviceActionPending = false;
				m_serviceResultValid = false;
				if (!success) {
					Alert::ask(message, "OK", QString());
				}
				loadCategory();
			});
			return;
		}
		if (!ok) {
			Alert::ask(error, "OK", QString());
		}
		loadCategory();
	}

	void runSelected() {
		const int row = m_list.state.selected;
		if (row < 0 || row >= int(m_state.size()) || !m_state[size_t(row)].canRun) {
			return;
		}
		const bool stop = m_state[size_t(row)].running;
		const QString name = m_resources[size_t(row)].name;
		if (stop && !Alert::ask("Stop “" + name + "” now? Programs that depend on it may stop "
				"working until it is started again.", "Stop", "Cancel")) {
			return;
		}
		m_serviceActionPending = true;
		updateButtons();
		userServiceActAsync(this, m_resources[size_t(row)].key,
			stop ? UserServiceAction::Stop : UserServiceAction::Start,
			[this](bool success, const QString &error) {
				m_serviceActionPending = false;
				m_serviceResultValid = false;
				if (!success) {
					Alert::ask(error, "OK", QString());
				}
				loadCategory();
			});
	}

	/* Event-driven refresh: autostart folders and the user's unit folder. */
	void watchStartupSources() {
		m_reload.setSingleShot(true);
		m_reload.setInterval(500);
		m_reload.callOnTimeout([this] {
			rewatch();
			if (category() != Extensions) {
				if (category() == Services) {
					m_serviceResultValid = false;
				}
				loadCategory();
			}
		});
		connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reload,
			qOverload<>(&QTimer::start));
		connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reload,
			qOverload<>(&QTimer::start));
		m_servicePoll.setInterval(5000);
		connect(&m_servicePoll, &QTimer::timeout, this, [this] {
			if (category() == Services) {
				m_serviceResultValid = false;
				requestServiceRefresh();
			}
		});
		m_servicePoll.start();
		rewatch();
	}

	void rewatch() {
		const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
		const AutostartContext autostart = autostartDefaultContext();
		QList<QPair<QString, QStringList>> sources = {
			{ config + "/autostart", { "*.desktop" } },
			{ config + "/systemd/user", { "*.service" } },
		};
		for (const QString &d : autostart.systemDirs) {
			sources << qMakePair(d, QStringList{ "*.desktop" });
		}
		int fileCount = m_watcher.files().size();
		for (const auto &source : sources) {
			QString existing = source.first;
			while (!QDir(existing).exists() && existing != "/" && !existing.isEmpty()) {
				existing = QFileInfo(existing).absolutePath();
			}
			if (!existing.isEmpty() && m_watcher.directories().size() < 64 &&
					!m_watcher.directories().contains(existing)) {
				m_watcher.addPath(existing);
			}
			if (existing != source.first || !QDir(source.first).exists()) {
				continue;
			}
			QDirIterator it(source.first, source.second, QDir::Files | QDir::Readable,
				QDirIterator::NoIteratorFlags);
			while (it.hasNext() && fileCount < 512) {
				const QString path = it.next();
				if (!m_watcher.files().contains(path) && m_watcher.addPath(path)) {
					++fileCount;
				}
			}
		}
	}

	void requestServiceRefresh() {
		if (m_serviceQuerying) {
			m_serviceRefreshPending = true;
			return;
		}
		m_serviceQuerying = true;
		userServicesAsync(this, [this](UserServiceQuery result) {
			m_serviceQuerying = false;
			m_serviceResult = std::move(result);
			m_serviceResultValid = true;
			if (category() == Services) {
				loadCategory();
			}
			if (m_serviceRefreshPending) {
				m_serviceRefreshPending = false;
				m_serviceResultValid = false;
				requestServiceRefresh();
			}
		});
	}

	void paintDetails(pl_canvas *c) const {
		const int row = m_list.state.selected;
		if (row < 0 || row >= static_cast<int>(m_resources.size())) {
			return;
		}
		const SystemResource &resource = m_resources[static_cast<size_t>(row)];
		const int x = 270, right = width() - 20;
		pl_icon_paint(c, x, 100, resource.icon, PL_ICON_LARGE, false);
		text(c, resource.name, x + 42, 121, right - x - 42, PL_FONT_SYSTEM);
		pl_hline(c, 264, right, 140, GRAY(0x8));
		pl_hline(c, 265, right + 1, 141, C_WHITE);
		int y = 162;
		for (const QString &line : panelWrap(resource.description, right - x, PL_FONT_VIEWS)) {
			text(c, line, x, y, right - x);
			y += 16;
		}
		y += 10;
		for (const QString &item : resource.metadata) {
			for (const QString &line : panelWrap(item, right - x, PL_FONT_VIEWS)) {
				if (y > height() - 14) {
					return;
				}
				text(c, line, x, y, right - x, PL_FONT_VIEWS, GRAY(0x4));
				y += 16;
			}
		}
	}

	struct RowState {
		bool canToggle = false, on = false, canRun = false, running = false;
	};

	Page m_page;
	PanelRadios m_categories;
	PanelButton m_info, m_toggle, m_run;
	std::vector<RowState> m_state;
	QFileSystemWatcher m_watcher;
	QTimer m_reload;
	QTimer m_servicePoll;
	UserServiceQuery m_serviceResult;
	bool m_serviceResultValid = false;
	bool m_serviceQuerying = false;
	bool m_serviceRefreshPending = false;
	bool m_serviceActionPending = false;
	QString m_title, m_explanation;
	QStringList m_rows;
	PanelList m_list;
	PanelCheckbox m_showAll;
	PanelPopup m_set;
	QStringList m_setNames;

	/* The sets, then Save Set… and Delete Set…; the current one shown. */
	void fillSets() {
		m_setNames = trickSetNames();
		m_set.items.clear();
		const QString current = currentTrickSet();
		m_set.selected = -1;
		for (const QString &name : m_setNames) {
			if (name == current) {
				m_set.selected = int(m_set.items.size());
			}
			m_set.items.emplace_back(name);
		}
		if (m_set.selected < 0) {
			m_set.items.insert(m_set.items.begin(), PopupItem("(none chosen)", false));
			m_setNames.prepend(QString());
			m_set.selected = 0;
		}
		m_set.items.emplace_back("Save Set…");
		m_set.items.emplace_back("Delete Set…", m_setNames.size() > 1 || !m_setNames.value(0).isEmpty());
		update();
	}

	void chooseSet(int row) {
		if (row == int(m_setNames.size())) {
			saveSet();
		} else if (row == int(m_setNames.size()) + 1) {
			deleteSet();
		} else if (row >= 0 && row < int(m_setNames.size()) && !m_setNames[row].isEmpty()) {
			applySet(m_setNames[row]);
		}
		fillSets();
	}

	/* Login items now, then (once listed) user services: their on/off at login. */
	void saveSet() {
		bool ok = false;
		const QString name = QInputDialog::getText(this, "Save Set", "Name of the set:",
			QLineEdit::Normal, currentTrickSet(), &ok).trimmed();
		if (!ok || name.isEmpty()) {
			return;
		}
		auto set = std::make_shared<TrickSet>();
		for (const AutostartEntry &e : autostartEntries()) {
			if (e.enabled || e.reason == "Disabled") {
				set->loginItems.insert(e.id, e.enabled);
			}
		}
		userServicesAsync(this, [this, set, name](UserServiceQuery q) {
			for (const UserService &u : q.services) {
				if (u.manageable && (u.fileState == "enabled" || u.fileState == "disabled")) {
					set->services.insert(u.unit, u.fileState == "enabled");
				}
			}
			QString error;
			if (saveTrickSet(name, *set, &error)) {
				setCurrentTrickSet(name);
			} else {
				Alert::ask(error, "OK", QString());
			}
			fillSets();
		});
	}

	void deleteSet() {
		const QString name = currentTrickSet();
		if (name.isEmpty() || !Alert::ask("Delete the set “" + name + "”? The items stay as they are.",
				"Delete", "Cancel")) {
			return;
		}
		deleteTrickSet(name);
		setCurrentTrickSet(QString());
	}

	void applySet(const QString &name) {
		TrickSet set;
		if (!readTrickSet(name, &set)) {
			return;
		}
		QStringList problems;
		const AutostartContext context = autostartDefaultContext();
		for (const AutostartEntry &e : autostartEntries()) {
			const auto want = set.loginItems.constFind(e.id);
			if (want != set.loginItems.cend() && *want != e.enabled && (e.enabled || e.reason == "Disabled")) {
				QString error;
				if (!autostartSetEnabled(context, e.id, *want, &error)) {
					problems << error;
				}
			}
		}
		setCurrentTrickSet(name);
		/* Services one after another, as each is a call to the user manager. */
		userServicesAsync(this, [this, set, problems](UserServiceQuery q) mutable {
			auto pending = std::make_shared<QList<std::pair<QString, bool>>>();
			for (const UserService &u : q.services) {
				const auto want = set.services.constFind(u.unit);
				if (u.manageable && want != set.services.cend() && *want != (u.fileState == "enabled")) {
					pending->append({ u.unit, *want });
				}
			}
			applyServices(pending, std::make_shared<QStringList>(problems));
		});
	}

	void applyServices(std::shared_ptr<QList<std::pair<QString, bool>>> pending,
			std::shared_ptr<QStringList> errors) {
		if (pending->isEmpty()) {
			if (!errors->isEmpty()) {
				Alert::ask("Some items couldn't be changed: " + errors->join(" "), "OK", QString());
			}
			m_serviceResultValid = false;
			loadCategory();
			return;
		}
		const auto [unit, on] = pending->takeFirst();
		userServiceActAsync(this, unit, on ? UserServiceAction::EnableAtLogin : UserServiceAction::DisableAtLogin,
			[this, pending, errors](bool ok, const QString &message) {
				if (!ok) {
					*errors << message;
				}
				applyServices(pending, errors);
			});
	}
	std::vector<SystemResource> m_resources;
};

QPointer<ResourceListWindow> &systemWindow() {
	static QPointer<ResourceListWindow> window;
	return window;
}

QPointer<ResourceListWindow> &extensionsWindow() {
	static QPointer<ResourceListWindow> window;
	return window;
}

} // namespace

void openSystemInformation() {
	// Pedigree, if it's here; this summary otherwise.
	if (platinumStartApplication(siblingProgram("zacos9-pedigree"), {})) {
		return;
	}
	if (!systemWindow()) {
		systemWindow() = new ResourceListWindow(ResourceListWindow::Page::System);
	}
	systemWindow()->show();
	systemWindow()->raise();
}

void openExtensionsManager() {
	if (!extensionsWindow()) {
		extensionsWindow() = new ResourceListWindow(ResourceListWindow::Page::Extensions);
	}
	extensionsWindow()->show();
	extensionsWindow()->raise();
}
