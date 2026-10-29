#include "vfs.h"
#include "platinumshell.h"
#include "programs.h"
#include "controlpanels.h"
#include "collarmodules.h"
#include "custompatterns.h"
#include "../panels/xkblayouts.h"
#include "../../lib/settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

#include "appdb.h"
#include "apptrash.h"
#include "autostart.h"
#include "resources.h"
#include "fonts.h"

/*
 * The default mapping. Written to the registry the first time the Finder
 * runs, then owned by the user: edit it to rename the disk, add folders,
 * point Home somewhere else, or move Utilities.
 * Folders the user makes on the startup disk are nodes with "user": true.
 *
 * Changing REGISTRY_VERSION migrates an older file: fields the old file
 * doesn't have take their default, and nodes it doesn't know about are
 * added, while the user's own nodes and overrides are kept.
 */
/* 14: Fonts is a generated catalog, not a raw directory.
 * 15: Fonts has a Language Kits folder.
 * 16: Applications, Home and the System Folder's folders have their own
 *     icons (a bone, Zac's doghouse, an emblem each).
 * 17: Shutdown Items, Zac Menu Items and Extensions (Disabled).
 * 18: Extensions Manager uses its Mac OS 9 name again.
 * 19: Zac Menu Items (Disabled).
 * 20: Control Panels (Disabled).
 * 21: Utilities moves from the System Folder into Applications, and holds
 *     only shipped utilities and the user's own; the old optional root
 *     Utilities folder is gone. */
static constexpr int REGISTRY_VERSION = 21;

static const char *DEFAULT_REGISTRY = R"JSON({
  "version": 21,
  "volume": {},
  "showUnixVolume": false,
  "nodes": [
    { "id": "system-folder", "name": "System Folder", "kind": "folder",
      "icon": "system-folder", "order": 0 },
    { "id": "system-folder/appearance", "icon": "sf-appearance", "name": "Appearance", "kind": "backed",
      "backing": "$XDG_DATA_HOME/zacos9/appearance" },
    { "id": "system-folder/appearance/desktop-patterns", "name": "Desktop Patterns",
      "kind": "backed", "backing": "$XDG_DATA_HOME/zacos9/appearance/Desktop Patterns" },
    { "id": "system-folder/appearance/wallpaper", "name": "Wallpaper", "kind": "backed",
      "backing": "$XDG_DATA_HOME/zacos9/appearance/Wallpaper" },
    { "id": "system-folder/appearance/themes", "name": "Themes", "kind": "backed",
      "backing": "$XDG_DATA_HOME/zacos9/appearance/Themes" },
    { "id": "system-folder/appearance/sound-themes", "name": "Sound Themes", "kind": "backed",
      "backing": "$XDG_DATA_HOME/zacos9/appearance/Sound Themes" },
    { "id": "system-folder/control-panels", "name": "Control Panels",
      "kind": "panels", "icon": "control-panels" },
    { "id": "system-folder/control-panels-disabled", "name": "Control Panels (Disabled)",
      "kind": "panels-disabled", "icon": "control-panels" },
    { "id": "system-folder/collar-modules", "name": "Collar Modules",
      "kind": "collar-modules", "icon": "control-strip" },
    { "id": "system-folder/collar-modules-disabled", "name": "Collar Modules (Disabled)",
      "kind": "collar-modules-disabled", "icon": "control-strip" },
    { "id": "system-folder/help", "icon": "help", "name": "Help", "kind": "help-books" },
    { "id": "system-folder/voices", "icon": "speech", "name": "Voices", "kind": "voices" },
    { "id": "system-folder/keyboard-layouts", "icon": "keyboard", "name": "Keyboard Layouts",
      "kind": "keyboard-layouts" },
    { "id": "system-folder/keychains", "icon": "keychain", "name": "Keychains",
      "kind": "keychains" },
    { "id": "system-folder/extensions", "icon": "sf-extensions", "name": "Extensions", "kind": "resource",
      "provider": "extensions" },
    { "id": "system-folder/extensions-disabled", "icon": "sf-extensions",
      "name": "Extensions (Disabled)", "kind": "resource", "provider": "extensions-disabled" },
    { "id": "system-folder/sounds", "icon": "sf-sounds", "name": "Sounds", "kind": "resource",
      "provider": "sounds" },
    { "id": "system-folder/startup-items", "icon": "sf-startup-items", "name": "Startup Items", "kind": "resource",
      "provider": "startup-items" },
    { "id": "system-folder/shutdown-items", "icon": "sf-startup-items", "name": "Shutdown Items",
      "kind": "backed", "backing": "$XDG_DATA_HOME/zacos9/Shutdown Items" },
    { "id": "system-folder/application-support", "icon": "sf-application-support", "name": "Application Support", "kind": "resource",
      "provider": "application-support" },
    { "id": "system-folder/device-drivers", "icon": "sf-device-drivers", "name": "Device Drivers", "kind": "resource",
      "provider": "device-drivers" },
    { "id": "system-folder/network", "icon": "sf-network", "name": "Network", "kind": "resource",
      "provider": "network" },
    { "id": "system-folder/system-logs", "icon": "sf-system-logs", "name": "System Logs", "kind": "resource",
      "provider": "system-logs" },
    { "id": "system-folder/software-components", "icon": "sf-software-components", "name": "Software Components",
      "kind": "resource", "provider": "software-components" },
    { "id": "system-folder/system-information", "name": "System Information",
      "kind": "launcher", "action": "system-information" },
    { "id": "system-folder/extensions-manager", "name": "Extensions Manager",
      "kind": "launcher", "action": "extensions-manager" },
    { "id": "system-folder/fonts", "icon": "sf-fonts", "name": "Fonts", "kind": "fonts" },
    { "id": "system-folder/fonts/language-kits", "name": "Language Kits", "kind": "fonts" },
    { "id": "system-folder/favorites", "icon": "sf-favorites", "name": "Favorites", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME/zacos9/Favorites" },
    { "id": "system-folder/toy-box-items", "icon": "sf-toy-box-items", "name": "Toy Box Items", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME/zacos9/Toy Box Items" },
    { "id": "system-folder/system", "icon": "system-folder", "name": "System", "kind": "launcher",
      "action": "system" },
    { "id": "system-folder/sniffer", "icon": "applications-folder", "name": "Sniffer", "kind": "launcher",
      "action": "sniffer" },
    { "id": "system-folder/clipboard", "icon": "clipboard", "name": "Clipboard", "kind": "launcher",
      "action": "clipboard" },
    { "id": "system-folder/stash", "icon": "stash", "name": "Stash", "kind": "launcher",
      "appId": "zacos9-stash" },
    { "id": "system-folder/notepad", "icon": "notepad", "name": "Notepad", "kind": "launcher",
      "appId": "zacos9-notepad" },
    { "id": "system-folder/folder-tricks", "icon": "sf-folder-tricks", "name": "Folder Tricks", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME/zacos9/Folder Tricks" },
    { "id": "system-folder/contextual-menu-items", "icon": "sf-contextual-menu-items", "name": "Contextual Menu Items",
      "kind": "backed", "backing": "$XDG_CONFIG_HOME/zacos9/Contextual Menu Items" },
    { "id": "system-folder/zac-menu-items", "icon": "sf-contextual-menu-items", "name": "Zac Menu Items",
      "kind": "backed", "backing": "$XDG_CONFIG_HOME/zacos9/ZacOS 9 Menu Items" },
    { "id": "system-folder/zac-menu-items-disabled", "icon": "sf-contextual-menu-items",
      "name": "Zac Menu Items (Disabled)", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME/zacos9/ZacOS 9 Menu Items (Disabled)" },
    { "id": "system-folder/preferences", "icon": "sf-preferences", "name": "Preferences", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME" },
    { "id": "applications", "name": "Applications", "kind": "apps", "order": 1,
      "icon": "applications-folder",
      "excludeCategories": ["X-ZacOS9-Utility"] },
    { "id": "applications/utilities", "icon": "sf-utilities", "name": "Utilities",
      "kind": "apps", "order": -1 },
    { "id": "home", "name": "Home", "kind": "backed",
      "backing": "$HOME", "order": 2, "icon": "home-folder" },
    { "id": "shared-documents", "name": "Shared Documents", "kind": "backed",
      "backing": "/home/Shared", "order": 3 }
  ],
  "overrides": {}
})JSON";

namespace {

struct Override {
	QString name;
	int label = 0;
	bool hidden = false;
	bool hasName = false, hasLabel = false, hasHidden = false;
};

QHash<QString, VNode> g_nodes;
QHash<QString, Override> g_overrides;
QString g_volumeName;
bool g_showUnix = false;
std::vector<VfsZacosVolume> g_zacosVolumes;
bool g_loaded = false;
/* Keyed by token so a window can stop listening when it closes. */
QHash<int, std::function<void()>> g_callbacks;
int g_nextToken = 1;
bool g_refreshing = false;

const char *SCHEME = "vfs:/";

/* ---- the registry ------------------------------------------------------- */

QString dataDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/finder";
}

/* $HOME and the XDG directories, so a registry is portable between users. */
QString expand(const QString &in) {
	QString out = in;
	const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	if (documents.isEmpty()) {
		documents = QDir::homePath() + "/Documents";
	}
	out.replace("$XDG_DOCUMENTS_DIR", documents);
	out.replace("$XDG_CONFIG_HOME", config);
	out.replace("$XDG_DATA_HOME", data);
	out.replace("$HOME", QDir::homePath());
	return out;
}

VKind kindFromName(const QString &name) {
	if (name == "fonts") {
		return VKind::Fonts;
	}
	if (name == "backed") {
		return VKind::Backed;
	}
	if (name == "apps") {
		return VKind::Apps;
	}
	if (name == "panels") {
		return VKind::Panels;
	}
	if (name == "panels-disabled") {
		return VKind::PanelsDisabled;
	}
	if (name == "collar-modules") {
		return VKind::CollarModules;
	}
	if (name == "collar-modules-disabled") {
		return VKind::CollarModulesDisabled;
	}
	if (name == "help-books") {
		return VKind::HelpBooks;
	}
	if (name == "voices") {
		return VKind::Voices;
	}
	if (name == "keyboard-layouts") {
		return VKind::KeyboardLayouts;
	}
	if (name == "keychains") {
		return VKind::Keychains;
	}
	if (name == "unix") {
		return VKind::Unix;
	}
	if (name == "resource") {
		return VKind::Resource;
	}
	if (name == "launcher") {
		return VKind::Launcher;
	}
	return VKind::Folder;
}

pl_icon_kind iconFromName(const QString &name, VKind kind) {
	if (name == "system-folder") {
		return PL_ICON_SYSTEM_FOLDER;
	}
	if (name == "control-panels") {
		return PL_ICON_CONTROL_PANELS;
	}
	if (name == "control-strip") {
		return PL_ICON_CP_COLLAR;
	}
	if (name == "help") {
		return PL_ICON_FOLDER;
	}
	if (name == "speech") {
		return PL_ICON_CP_SPEECH;
	}
	if (name == "keyboard") {
		return PL_ICON_CP_KEYBOARD;
	}
	if (name == "keychain") {
		return PL_ICON_CP_KEYCHAIN;
	}
	if (name == "clipboard") {
		return PL_ICON_CLIPPING;
	}
	if (name == "stash") {
		return PL_ICON_APP_STASH;
	}
	if (name == "notepad") {
		return PL_ICON_APP_NOTEPAD;
	}
	if (name == "applications-folder") {
		return PL_ICON_APPLICATIONS_FOLDER;
	}
	/* The System Folder's folders: "sf-fonts" and so on. */
	static const QHash<QString, pl_icon_kind> systemFolders = {
		{ "sf-appearance", PL_ICON_SF_APPEARANCE }, { "sf-extensions", PL_ICON_SF_EXTENSIONS },
		{ "sf-sounds", PL_ICON_SF_SOUNDS }, { "sf-startup-items", PL_ICON_SF_STARTUP_ITEMS },
		{ "sf-application-support", PL_ICON_SF_APPLICATION_SUPPORT },
		{ "sf-device-drivers", PL_ICON_SF_DEVICE_DRIVERS }, { "sf-network", PL_ICON_SF_NETWORK },
		{ "sf-system-logs", PL_ICON_SF_SYSTEM_LOGS }, { "sf-software-components", PL_ICON_SF_SOFTWARE_COMPONENTS },
		{ "sf-fonts", PL_ICON_SF_FONTS }, { "sf-favorites", PL_ICON_SF_FAVORITES },
		{ "sf-toy-box-items", PL_ICON_SF_TOY_BOX_ITEMS }, { "sf-folder-tricks", PL_ICON_SF_FOLDER_TRICKS },
		{ "sf-contextual-menu-items", PL_ICON_SF_CONTEXTUAL_MENU_ITEMS },
		{ "sf-preferences", PL_ICON_SF_PREFERENCES }, { "sf-utilities", PL_ICON_SF_UTILITIES },
	};
	if (systemFolders.contains(name)) {
		return systemFolders.value(name);
	}
	if (name == "home-folder") {
		return PL_ICON_HOME_FOLDER;
	}
	if (name == "disk") {
		return PL_ICON_DISK;
	}
	if (name == "application") {
		return PL_ICON_APPLICATION;
	}
	if (name == "document") {
		return PL_ICON_DOCUMENT;
	}
	return kind == VKind::Panels || kind == VKind::PanelsDisabled ?
		PL_ICON_CONTROL_PANELS : PL_ICON_FOLDER;
}

QStringList stringList(const QJsonValue &v) {
	QStringList out;
	for (const QJsonValue &item : v.toArray()) {
		out << item.toString();
	}
	return out;
}

QJsonObject readRegistryFile() {
	QFile file(vfsRegistryPath());
	if (file.open(QIODevice::ReadOnly)) {
		const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
		if (doc.isObject()) {
			return doc.object();
		}
	}
	return QJsonDocument::fromJson(DEFAULT_REGISTRY).object();
}

void writeRegistryFile(const QJsonObject &root) {
	QDir().mkpath(dataDir());
	QFile file(vfsRegistryPath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	}
}

/* Add any node the running version expects that `root` is missing, so an
 * older registry gains new folders without losing the user's own. */
bool migrate(QJsonObject *root) {
	const int version = root->value("version").toInt();
	if (version >= REGISTRY_VERSION) {
		return false;
	}
	const QJsonObject fresh = QJsonDocument::fromJson(DEFAULT_REGISTRY).object();
	QJsonArray nodes = root->value("nodes").toArray();
	for (int i = 0; i < nodes.size(); i++) {
		QJsonObject n = nodes.at(i).toObject();
		if (n.value("id").toString() == QLatin1String("system-folder/fonts")) {
			n.insert("kind", "fonts");
			n.remove("backing");
			nodes.replace(i, n);
		}
		if (n.value("id").toString() == QLatin1String("system-folder/extensions")) {
			n.insert("kind", "resource");
			n.insert("provider", "extensions");
			n.remove("backing");
			nodes.replace(i, n);
		}
		if (version < 9 &&
				n.value("id").toString() == QLatin1String("applications/utilities")) {
			n.insert("id", "system-folder/utilities");
			if (n.value("categories").toArray() == QJsonArray{ "X-ZacOS9-Utility" }) {
				n.remove("categories");
			}
			nodes.replace(i, n);
		}
		if (n.value("id").toString() == QLatin1String("system-folder/wallpaper")) {
			n.insert("id", "system-folder/appearance/wallpaper");
			if (n.value("backing").toString() == QLatin1String("$XDG_DATA_HOME/zacos9/wallpaper")) {
				n.insert("backing", desktopWallpaperFolder());
			}
			nodes.replace(i, n);
		}
		/* It manages Extensions, as it did in Mac OS 9; the old renamed
		 * title was confusing beside the Extensions folder. Only the old
		 * default name is changed, never one the user chose. */
		if (n.value("id").toString() == QLatin1String("system-folder/extensions-manager") &&
				n.value("name").toString() == QLatin1String("Tricks Manager")) {
			n.insert("name", "Extensions Manager");
			nodes.replace(i, n);
		}
	}
	/* Version 21: Utilities returns to Applications, with the tools ZacOS
	 * 9 ships and the user's own. The old optional root Utilities folder
	 * is replaced by it. */
	bool haveUtilities = false;
	for (const QJsonValue &v : nodes) {
		haveUtilities |= v.toObject().value("id").toString() ==
			QLatin1String("applications/utilities");
	}
	for (int i = nodes.size() - 1; i >= 0; i--) {
		const QJsonObject n = nodes.at(i).toObject();
		const QString id = n.value("id").toString();
		if (id == QLatin1String("utilities")) {
			nodes.removeAt(i);
			continue;
		}
		if (id == QLatin1String("system-folder/utilities")) {
			if (haveUtilities) {
				nodes.removeAt(i);
				continue;
			}
			QJsonObject moved = n;
			moved.insert("id", "applications/utilities");
			moved.insert("order", -1);
			if (!moved.contains("icon")) {
				moved.insert("icon", "sf-utilities");
			}
			nodes.replace(i, moved);
			haveUtilities = true;
		}
	}
	/* Documents was the default before Home: Home holds it now. Only the
	 * untouched default goes; one the user pointed elsewhere stays. */
	for (int i = nodes.size() - 1; i >= 0; i--) {
		const QJsonObject n = nodes.at(i).toObject();
		if (n.value("id").toString() == QLatin1String("documents") &&
				n.value("backing").toString() == QLatin1String("$XDG_DOCUMENTS_DIR")) {
			nodes.removeAt(i);
		}
	}
	/* Version 16: Applications, Home and the System Folder's folders get
	 * their own icons, unless the user has given them one. */
	QHash<QString, QString> icons;
	for (const QJsonValue &v : fresh.value("nodes").toArray()) {
		const QJsonObject n = v.toObject();
		if (n.contains("icon")) {
			icons.insert(n.value("id").toString(), n.value("icon").toString());
		}
	}
	for (int i = 0; i < nodes.size(); i++) {
		QJsonObject n = nodes.at(i).toObject();
		const QString id = n.value("id").toString();
		if (icons.contains(id) && !n.contains("icon")) {
			n.insert("icon", icons.value(id));
			nodes.replace(i, n);
		}
	}
	/* Version 4: Applications > Utilities holds what ZacOS 9 ships as a
	 * utility (balenaEtcher, ...), so Applications itself leaves them out. */
	for (int i = 0; i < nodes.size(); i++) {
		QJsonObject n = nodes.at(i).toObject();
		if (n.value("id").toString() == QLatin1String("applications")) {
			QJsonArray ex = n.value("excludeCategories").toArray();
			if (!ex.contains(QJsonValue("X-ZacOS9-Utility"))) {
				ex.append("X-ZacOS9-Utility");
				n.insert("excludeCategories", ex);
				nodes.replace(i, n);
			}
		}
	}
	QStringList have;
	for (const QJsonValue &v : nodes) {
		have << v.toObject().value("id").toString();
	}
	for (const QJsonValue &v : fresh.value("nodes").toArray()) {
		if (!have.contains(v.toObject().value("id").toString())) {
			nodes.append(v);
		}
	}
	root->insert("nodes", nodes);
	QJsonObject overrides = root->value("overrides").toObject();
	const QStringList keys = overrides.keys();
	for (const QString &key : keys) {
		QString destination;
		if (key == "system-folder/utilities") {
			destination = "applications/utilities";
		} else if (key.startsWith(QLatin1String("system-folder/utilities/"))) {
			destination = "applications/" + key.mid(static_cast<int>(
				QLatin1String("system-folder/").size()));
		} else if (key.startsWith(QLatin1String("applications/"))) {
			const AppEntry *app = appById(key.section('/', -1));
			if (app && app->inUtilities) {
				destination = "applications/utilities/" + app->id;
			}
		}
		if (!destination.isEmpty() && !overrides.contains(destination)) {
			overrides.insert(destination, overrides.take(key));
		}
	}
	/* The optional root Utilities folder is gone with version 21. */
	for (const QString &key : overrides.keys()) {
		if (key == QLatin1String("utilities") || key.startsWith(QLatin1String("utilities/"))) {
			overrides.remove(key);
		}
	}
	root->insert("overrides", overrides);
	if (!root->contains("volume")) {
		root->insert("volume", fresh.value("volume"));
	}
	root->insert("version", REGISTRY_VERSION);
	return true;
}

/* ---- generated nodes ---------------------------------------------------- */

} // namespace

namespace {

bool matchesCategories(const AppEntry &app, const VNode &node) {
	for (const QString &bad : node.excludeCategories) {
		if (app.categories.contains(bad, Qt::CaseInsensitive)) {
			return false;
		}
	}
	if (node.categories.isEmpty()) {
		return true;
	}
	for (const QString &want : node.categories) {
		if (app.categories.contains(want, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void addNode(const VNode &node) {
	g_nodes.insert(node.id, node);
}

/* Each application appears as a direct launcher in the parent folder.
 * Nothing is copied: these are entries in this model only. */
void generateApps(const VNode &parent) {
	QString error;
	const auto queued = appTrashEntries(&error);
	if (!error.isEmpty()) {
		qWarning().noquote() << error;
	}
	for (const AppEntry &app : appList()) {
		/* Utilities holds only what belongs there; Applications holds
		 * the rest, and neither repeats an app. System tools (settings,
		 * file managers) are reached from Control Panels instead. */
		const bool utilities = parent.id == QLatin1String("applications/utilities");
		const bool shown = utilities ? app.inUtilities
			: (app.userInstalled && !app.systemUtility && !app.inUtilities);
		if (!shown ||
				!matchesCategories(app, parent) ||
				std::any_of(queued.begin(), queued.end(),
					[&app](const TrashedApplication &entry) { return entry.id == app.id; })) {
			continue;
		}
		VNode launcher;
		launcher.id = parent.id + "/" + app.id;
		launcher.name = app.folderName;
		launcher.kind = VKind::Launcher;
		launcher.icon = PL_ICON_APPLICATION;
		launcher.appId = app.id;
		addNode(launcher);
	}
}

/* One panel's item. An id from the program and its argument, as the
 * folder's items always had, so the places and labels given to them stay
 * theirs; the entry's own id rides along, to turn it off and on again. */
void addPanel(const VNode &parent, const pl_control_panel &panel) {
	const QString program = QString::fromUtf8(panel.program);
	const QStringList args = QString::fromUtf8(panel.args).split(' ', Qt::SkipEmptyParts);
	VNode node;
	node.id = parent.id + "/" + program + (args.isEmpty() ? QString() : "-" + args.join('-'));
	node.name = QString::fromUtf8(panel.name);
	node.kind = VKind::Launcher;
	node.icon = panel.icon;
	node.program = program;
	node.args = args;
	node.panelId = QString::fromUtf8(panel.id);
	addNode(node);
}

void generatePanels(const VNode &parent) {
	/* The installed control panels (lib/controlpanels.h). */
	size_t count = 0;
	pl_control_panel *panels = pl_control_panels(&count);
	for (size_t i = 0; i < count; i++) {
		addPanel(parent, panels[i]);
	}
	pl_control_panels_free(panels, count);
	/* The Toy Box is the Sniffer's own window, listed here as the Mac
	 * listed its Launcher. */
	VNode toyBox;
	toyBox.id = parent.id + "/toy-box";
	toyBox.name = QStringLiteral("Toy Box");
	toyBox.kind = VKind::Launcher;
	toyBox.icon = PL_ICON_APPLICATION;
	toyBox.builtinAction = QStringLiteral("toy-box");
	addNode(toyBox);
}

/* The panels the user turned off. The same items, so a panel keeps its
 * place and its label while it goes into and out of the folder; the Toy
 * Box is not a panel file, so it stays in Control Panels. */
void generateDisabledPanels(const VNode &parent) {
	size_t count = 0;
	pl_control_panel *panels = pl_control_panels_disabled(&count);
	for (size_t i = 0; i < count; i++) {
		addPanel(parent, panels[i]);
	}
	pl_control_panels_free(panels, count);
}

static void addCollarModule(const VNode &parent, const pl_collar_module &module) {
	const QString program = QString::fromUtf8(module.program);
	const QStringList args = QString::fromUtf8(module.args).split(' ', Qt::SkipEmptyParts);
	VNode node;
	node.id = parent.id + "/" + program + (args.isEmpty() ? QString() : "-" + args.join('-'));
	node.name = QString::fromUtf8(module.name);
	node.kind = VKind::Launcher;
	node.icon = module.icon;
	node.program = program;
	node.args = args;
	node.panelId = QString::fromUtf8(module.id); /* reuse panelId field for module id */
	addNode(node);
}

void generateCollarModules(const VNode &parent) {
	size_t count = 0;
	pl_collar_module *modules = pl_collar_modules(&count);
	for (size_t i = 0; i < count; i++) {
		addCollarModule(parent, modules[i]);
	}
	pl_collar_modules_free(modules, count);
}

void generateDisabledCollarModules(const VNode &parent) {
	size_t count = 0;
	pl_collar_module *modules = pl_collar_modules_disabled(&count);
	for (size_t i = 0; i < count; i++) {
		addCollarModule(parent, modules[i]);
	}
	pl_collar_modules_free(modules, count);
}

static QString helpRootPath() {
	const QString env = qEnvironmentVariable("ZACOS9_HELP_DIR");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { QString(sourceTreePath("share/zacos9/help")), dir + "/../share/zacos9/help" }) {
		if (!candidate.isEmpty() && QFileInfo(candidate + "/zacos9/index.html").exists()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/share/zacos9/help";
}

static void addHelpBook(const VNode &parent, const QString &bookId, const QString &title) {
	VNode node;
	node.id = parent.id + "/" + bookId;
	node.name = title.isEmpty() ? bookId : title;
	node.kind = VKind::Launcher;
	node.icon = PL_ICON_DOCUMENT;
	node.program = QStringLiteral("zacos9-help");
	node.args = { bookId };
	addNode(node);
}

void generateHelpBooks(const VNode &parent) {
	const QString root = helpRootPath();
	QDir dir(root);
	bool any = false;
	for (const QFileInfo &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		const QString index = entry.filePath() + "/index.html";
		if (!QFileInfo(index).isFile()) {
			continue;
		}
		QFile file(index);
		QString title = entry.fileName();
		if (file.open(QIODevice::ReadOnly)) {
			const QString html = QString::fromUtf8(file.read(64 * 1024));
			const QRegularExpressionMatch m =
				QRegularExpression("<title>(.*?)</title>", QRegularExpression::CaseInsensitiveOption |
					QRegularExpression::DotMatchesEverythingOption).match(html);
			if (m.hasMatch()) {
				title = QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText().simplified();
			}
		}
		addHelpBook(parent, entry.fileName(), title);
		any = true;
	}
	if (!any) {
		VNode none;
		none.id = parent.id + "/none";
		none.name = QStringLiteral("No help books found");
		none.kind = VKind::Launcher;
		none.icon = PL_ICON_DOCUMENT;
		none.readOnly = true;
		addNode(none);
	}
}

void generateVoices(const VNode &parent) {
	/* The voices come from speech-dispatcher asynchronously (shell/panels/
	 * speech.cpp). Until they are known, the folder lists opening the
	 * Speech panel, where voices can be chosen and tried. */
	VNode open;
	open.id = parent.id + "/speech-panel";
	open.name = QStringLiteral("Open the Speech panel");
	open.kind = VKind::Launcher;
	open.icon = PL_ICON_CP_SPEECH;
	open.appId = QStringLiteral("zacos9-speech");
	addNode(open);
}

static void addKeyboardLayout(const VNode &parent, const XkbLayout &layout) {
	VNode node;
	node.id = parent.id + "/" + layout.code;
	node.name = layout.name;
	node.kind = VKind::Launcher;
	node.icon = PL_ICON_CP_KEYBOARD;
	node.builtinAction = QStringLiteral("keyboard-layout");
	node.panelId = layout.code;
	addNode(node);
}

void generateKeyboardLayouts(const VNode &parent) {
	for (const XkbLayout &layout : xkbLayouts()) {
		addKeyboardLayout(parent, layout);
	}
	if (xkbLayouts().isEmpty()) {
		VNode none;
		none.id = parent.id + "/none";
		none.name = QStringLiteral("No keyboard layouts found");
		none.kind = VKind::Launcher;
		none.icon = PL_ICON_CP_KEYBOARD;
		none.readOnly = true;
		addNode(none);
	}
}

void generateKeychains(const VNode &parent) {
	/* Keychains come from the Secret Service asynchronously (shell/collar/
	 * keychain.cpp). Until they are known, the folder lists opening Keychain
	 * Access, where keychains can be managed. */
	VNode open;
	open.id = parent.id + "/keychain-access";
	open.name = QStringLiteral("Open Keychain Access");
	open.kind = VKind::Launcher;
	open.icon = PL_ICON_CP_KEYCHAIN;
	open.appId = QStringLiteral("zacos9-keychain");
	addNode(open);
}

void generateResourceLaunchers(const VNode &parent) {
	for (const auto &entry : std::vector<std::pair<QString, QString>>{
			{ "System Information", "system-information" },
			{ "Extensions Manager", "extensions-manager" } }) {
		VNode node;
		node.id = parent.id + "/" + entry.second;
		/* A registry node of the same id keeps its own name (the user may
		 * have renamed it); these are only the generated System Folder
		 * entries. */
		if (g_nodes.contains(node.id)) {
			continue;
		}
		node.name = entry.first;
		node.kind = VKind::Launcher;
		node.icon = PL_ICON_APPLICATION;
		node.builtinAction = entry.second;
		addNode(node);
	}
}

/* ---- mounted ZacOS 9 disks --------------------------------------------- */

static QString externalToken(const QString &mountPath) {
	return QString::fromLatin1(QDir::cleanPath(mountPath).toUtf8().toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

static QString externalRootId(const QString &mountPath) {
	return "volumes/" + externalToken(mountPath);
}

static void addExternalNode(VNode node, const VfsZacosVolume &volume) {
	node.readOnly = true;
	node.externalMount = volume.mountPath;
	node.externalVolumeName = volume.name;
	node.externalVersion = volume.version;
	addNode(node);
}

/* A ZacOS 9 disk's Finder view: the Macintosh presentation of that system,
 * not Debian's Unix root. The source directories are real and read-only;
 * this only decides what the disk's root shows. */
static void addExternalVolume(const VfsZacosVolume &volume) {
	if (volume.mountPath.isEmpty() || volume.name.isEmpty()) {
		return;
	}
	const QString root = externalRootId(volume.mountPath);
	const QString mount = QDir::cleanPath(volume.mountPath);
	VNode disk;
	disk.id = root;
	disk.name = volume.name;
	disk.kind = VKind::ExternalVolume;
	disk.icon = PL_ICON_DISK;
	disk.order = -1;
	addExternalNode(disk, volume);

	VNode apps;
	apps.id = root + "/applications";
	apps.name = "Applications";
	apps.kind = VKind::Backed;
	apps.backing = mount + "/usr/share/applications";
	apps.icon = PL_ICON_APPLICATIONS_FOLDER;
	apps.order = 1;
	addExternalNode(apps, volume);

	VNode systemFolder;
	systemFolder.id = root + "/system-folder";
	systemFolder.name = "System Folder";
	systemFolder.kind = VKind::Folder;
	systemFolder.icon = PL_ICON_SYSTEM_FOLDER;
	systemFolder.order = 0;
	addExternalNode(systemFolder, volume);

	const auto systemChild = [&](const char *id, const char *name, const QString &backing,
			pl_icon_kind icon, int order) {
		VNode child;
		child.id = systemFolder.id + "/" + id;
		child.name = name;
		child.kind = VKind::Backed;
		child.backing = backing;
		child.icon = icon;
		child.order = order;
		addExternalNode(child, volume);
	};
	systemChild("system", "System", mount + "/usr", PL_ICON_FOLDER, 0);
	systemChild("extensions", "Extensions", mount + "/usr/lib", PL_ICON_SF_EXTENSIONS, 1);
	systemChild("control-panels", "Control Panels", mount + "/usr/bin",
		PL_ICON_CONTROL_PANELS, 2);
	systemChild("fonts", "Fonts", mount + "/usr/share/fonts", PL_ICON_SF_FONTS, 3);
	systemChild("preferences", "Preferences", mount + "/etc", PL_ICON_SF_PREFERENCES, 4);
	systemChild("startup-items", "Startup Items", mount + "/etc/xdg/autostart",
		PL_ICON_SF_STARTUP_ITEMS, 5);
	systemChild("application-support", "Application Support", mount + "/usr/share",
		PL_ICON_SF_APPLICATION_SUPPORT, 6);

	/* Home is the person's own folder on that disk when it can be named,
	 * as on the startup disk; otherwise it shows that disk's home. */
	QString home = mount + "/home";
	const QStringList users = QDir(home).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	QStringList people;
	for (const QString &user : users) {
		if (user != "Shared" && user != "lost+found") {
			people << user;
		}
	}
	if (people.contains(QFileInfo(QDir::homePath()).fileName())) {
		home += "/" + QFileInfo(QDir::homePath()).fileName();
	} else if (people.size() == 1) {
		home += "/" + people.first();
	}
	VNode homeNode;
	homeNode.id = root + "/home";
	homeNode.name = "Home";
	homeNode.kind = VKind::Backed;
	homeNode.backing = home;
	homeNode.icon = PL_ICON_HOME_FOLDER;
	homeNode.order = 2;
	addExternalNode(homeNode, volume);

	if (QFileInfo(mount + "/home/Shared").isDir()) {
		VNode shared;
		shared.id = root + "/shared-documents";
		shared.name = "Shared Documents";
		shared.kind = VKind::Backed;
		shared.backing = mount + "/home/Shared";
		shared.order = 3;
		addExternalNode(shared, volume);
	}
}

/* ---- loading ------------------------------------------------------------ */

void load() {
	g_nodes.clear();
	g_overrides.clear();

	QJsonObject root = readRegistryFile();
	const bool migrated = migrate(&root);
	if (!QFile::exists(vfsRegistryPath()) || migrated) {
		writeRegistryFile(root);
	}

	/* The startup disk's name: the user's own, if they renamed it; else
	 * the one it was given when ZacOS 9 was installed (zacos9-install
	 * writes /etc/zacos9/disk-name); else Zacintosh HD. "Macintosh HD",
	 * the default before, is Apple's, so a file still holding it (never
	 * renamed) counts as not named. */
	QJsonObject volumeObj = root.value("volume").toObject();
	if (volumeObj.value("name").toString() == QLatin1String("Macintosh HD")) {
		volumeObj.remove("name");
		root.insert("volume", volumeObj);
		writeRegistryFile(root);
	}
	g_volumeName = volumeObj.value("name").toString();
	if (g_volumeName.isEmpty()) {
		QFile named(QStringLiteral("/etc/zacos9/disk-name"));
		if (named.open(QIODevice::ReadOnly)) {
			g_volumeName = QString::fromUtf8(named.readLine()).trimmed();
		}
	}
	if (g_volumeName.isEmpty()) {
		g_volumeName = QStringLiteral("Zacintosh HD");
	}
	g_showUnix = root.value("showUnixVolume").toBool(false);

	const QJsonObject overrides = root.value("overrides").toObject();
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		const QJsonObject o = it.value().toObject();
		Override ov;
		ov.hasName = o.contains("name");
		ov.name = o.value("name").toString();
		ov.hasLabel = o.contains("label");
		ov.label = o.value("label").toInt();
		ov.hasHidden = o.contains("hidden");
		ov.hidden = o.value("hidden").toBool();
		g_overrides.insert(it.key(), ov);
	}

	VNode volume;
	volume.name = g_volumeName;
	volume.kind = VKind::Volume;
	volume.icon = PL_ICON_DISK;
	addNode(volume);

	std::vector<VNode> generators;
	for (const QJsonValue &v : root.value("nodes").toArray()) {
		const QJsonObject o = v.toObject();
		VNode node;
		node.id = o.value("id").toString();
		if (node.id.isEmpty()) {
			continue;
		}
		node.kind = kindFromName(o.value("kind").toString());
		node.name = o.value("name").toString(node.id);
		node.backing = expand(o.value("backing").toString());
		node.icon = iconFromName(o.value("icon").toString(), node.kind);
		node.visible = o.value("visible").toBool(true);
		node.user = o.value("user").toBool(false);
		node.order = o.value("order").toInt();
		node.categories = stringList(o.value("categories"));
		node.excludeCategories = stringList(o.value("excludeCategories"));
		node.resourceProvider = o.value("provider").toString();
		if (node.kind == VKind::Resource && !resourceProviderKnown(node.resourceProvider)) {
			continue;
		}
		node.resourceDirectory = node.kind == VKind::Resource;
		node.builtinAction = o.value("action").toString();
		if (node.kind == VKind::Launcher && !node.builtinAction.isEmpty() &&
				node.builtinAction != "system-information" &&
				node.builtinAction != "extensions-manager" &&
				node.builtinAction != "clipboard" &&
				node.builtinAction != "toy-box" &&
				node.builtinAction != "system" &&
				node.builtinAction != "sniffer") {
			node.builtinAction.clear();
		}
		addNode(node);
		if (node.kind == VKind::Apps || node.kind == VKind::Panels ||
				node.kind == VKind::PanelsDisabled || node.kind == VKind::CollarModules ||
				node.kind == VKind::CollarModulesDisabled || node.kind == VKind::HelpBooks ||
				node.kind == VKind::Voices || node.kind == VKind::KeyboardLayouts ||
				node.kind == VKind::Keychains) {
			generators.push_back(node);
		}
	}
	/* Generated children are added after every registry node exists, so a
	 * generator's own settings are already in place. */
	for (const VNode &node : generators) {
		if (node.kind == VKind::Apps) {
			generateApps(node);
		} else if (node.kind == VKind::PanelsDisabled) {
			generateDisabledPanels(node);
		} else if (node.kind == VKind::CollarModules) {
			generateCollarModules(node);
		} else if (node.kind == VKind::CollarModulesDisabled) {
			generateDisabledCollarModules(node);
		} else if (node.kind == VKind::HelpBooks) {
			generateHelpBooks(node);
		} else if (node.kind == VKind::Voices) {
			generateVoices(node);
		} else if (node.kind == VKind::KeyboardLayouts) {
			generateKeyboardLayouts(node);
		} else if (node.kind == VKind::Keychains) {
			generateKeychains(node);
		} else {
			generatePanels(node);
		}
	}
	VNode systemFolder;
	systemFolder.id = "system-folder";
	generateResourceLaunchers(systemFolder);

	/* Renames and labels are applied last: they override everything. */
	for (auto it = g_overrides.begin(); it != g_overrides.end(); ++it) {
		auto node = g_nodes.find(it.key());
		if (node != g_nodes.end()) {
			if (it.value().hasName && !it.value().name.isEmpty()) {
				node->name = it.value().name;
			}
			if (it.value().hasHidden) {
				node->visible = !it.value().hidden;
			}
		}
	}
	for (const VfsZacosVolume &volume : g_zacosVolumes) {
		addExternalVolume(volume);
	}
	g_loaded = true;
}

void ensureLoaded() {
	if (!g_loaded) {
		load();
	}
}

void notifyChanged() {
	/* Over a copy: a callback may reload a window and so add or drop one. */
	const auto callbacks = g_callbacks;
	for (const auto &f : callbacks) {
		f();
	}
}

/* Write one override field back to the registry. */
bool setOverride(const QString &id, const QString &field, const QJsonValue &value) {
	QJsonObject root = readRegistryFile();
	QJsonObject overrides = root.value("overrides").toObject();
	QJsonObject entry = overrides.value(id).toObject();
	entry.insert(field, value);
	overrides.insert(id, entry);
	root.insert("overrides", overrides);
	writeRegistryFile(root);
	load();
	return true;
}

} // namespace

/* ---- paths -------------------------------------------------------------- */

bool vfsIsVirtual(const QString &path) {
	return path.startsWith(SCHEME);
}

QString vfsRoot() {
	return SCHEME;
}

QString vfsPathFor(const QString &id) {
	return SCHEME + id;
}

QString vfsIdOf(const QString &path) {
	return vfsIsVirtual(path) ? path.mid(static_cast<int>(strlen(SCHEME))) : QString();
}

const VNode *vfsNode(const QString &path) {
	if (!vfsIsVirtual(path)) {
		return nullptr;
	}
	ensureLoaded();
	auto it = g_nodes.find(vfsIdOf(path));
	return it == g_nodes.end() ? nullptr : &*it;
}

QString vfsName(const QString &path) {
	const VNode *node = vfsNode(path);
	return node ? node->name : QString();
}

QString vfsVolumeName() {
	ensureLoaded();
	return g_volumeName;
}

void vfsSetZacosVolumes(const std::vector<VfsZacosVolume> &volumes) {
	std::vector<VfsZacosVolume> normalized;
	for (VfsZacosVolume volume : volumes) {
		if (volume.mountPath.isEmpty() || volume.name.isEmpty()) {
			continue;
		}
		volume.mountPath = QDir::cleanPath(volume.mountPath);
		normalized.push_back(std::move(volume));
	}
	const auto same = [](const VfsZacosVolume &a, const VfsZacosVolume &b) {
		return a.mountPath == b.mountPath && a.name == b.name && a.version == b.version;
	};
	if (normalized.size() == g_zacosVolumes.size() &&
			std::equal(normalized.begin(), normalized.end(), g_zacosVolumes.begin(), same)) {
		return;
	}
	g_zacosVolumes = std::move(normalized);
	if (g_loaded) {
		load();
	} else {
		ensureLoaded();
	}
	notifyChanged();
}

QString vfsZacosVolumeRoot(const QString &mountPath) {
	ensureLoaded();
	const QString wanted = QDir::cleanPath(mountPath);
	for (const VfsZacosVolume &volume : g_zacosVolumes) {
		if (volume.mountPath == wanted) {
			return vfsPathFor(externalRootId(wanted));
		}
	}
	return {};
}

bool vfsIsZacosVolumeRoot(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::ExternalVolume;
}

static QString resourceIdComponent(const QString &key) {
	return QString::fromLatin1(key.toUtf8().toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

static void refreshResourceChildren(const VNode &parent) {
	const auto resources = resourceChildren(parent.resourceProvider, parent.resourceKey);
	QSet<QString> keep;
	for (const SystemResource &resource : resources) {
		keep.insert(parent.id + '/' + resourceIdComponent(resource.key));
	}
	QStringList stale;
	for (auto it = g_nodes.cbegin(); it != g_nodes.cend(); ++it) {
		if (it->parentId() == parent.id && it->resourceProvider == parent.resourceProvider &&
				!keep.contains(it->id)) {
			stale << it->id;
		}
	}
	for (const QString &id : stale) {
		const QString prefix = id + '/';
		for (auto it = g_nodes.begin(); it != g_nodes.end();) {
			if (it->id == id || (it->id.startsWith(prefix) &&
					it->resourceProvider == parent.resourceProvider)) {
				it = g_nodes.erase(it);
			} else {
				++it;
			}
		}
	}
	for (const SystemResource &resource : resources) {
		VNode child;
		child.id = parent.id + '/' + resourceIdComponent(resource.key);
		child.name = resource.name;
		child.kind = VKind::Resource;
		child.icon = resource.directory ? PL_ICON_FOLDER : resource.icon;
		child.resourceProvider = parent.resourceProvider;
		child.resourceKey = resource.key;
		child.resourceDirectory = resource.directory;
		child.resourceMetadata = resource.metadata;
		child.backing = resource.source;
		addNode(child);
	}
}

/* ---- listing ------------------------------------------------------------ */

std::vector<std::unique_ptr<Item>> vfsList(const QString &path) {
	std::vector<std::unique_ptr<Item>> items;
	const VNode *parent = vfsNode(path);
	if (!parent) {
		return items;
	}
	/* A Backed node is a real directory; its contents are real files. */
	if (parent->kind == VKind::Backed || parent->kind == VKind::Unix ||
			(parent->kind == VKind::Resource && !parent->resourceDirectory &&
				!parent->resourceKey.isEmpty())) {
		return items;
	}
	const QString parentId = parent->id;
	const VNode parentCopy = *parent;
	if (parentCopy.kind == VKind::Fonts) {
		for (auto it = g_nodes.begin(); it != g_nodes.end();) {
			if (it->kind == VKind::Font && it->parentId() == parentId) {
				it = g_nodes.erase(it);
			} else {
				++it;
			}
		}
		const bool languageKits = parentId.endsWith(QLatin1String("/language-kits"));
		for (const FontFace &face : fontFolderFaces(languageKits)) {
			VNode node;
			node.id = parentId + '/' + face.id();
			node.name = face.name;
			node.kind = VKind::Font;
			node.icon = PL_ICON_FONT;
			node.backing = face.file;
			node.fontFaceIndex = face.index;
			addNode(node);
		}
	}
	if (parentCopy.kind == VKind::Resource) {
		refreshResourceChildren(parentCopy);
	}
	std::vector<const VNode *> children;
	for (auto it = g_nodes.cbegin(); it != g_nodes.cend(); ++it) {
		/* Shared Documents shows once the package has made it. */
		const bool missing = it->id == QLatin1String("shared-documents") &&
			!QDir(it->backing).exists();
		if (it->parentId() == parentId && it->visible && !it->id.isEmpty() && !missing) {
			children.push_back(&*it);
		}
	}
	std::sort(children.begin(), children.end(), [](const VNode *a, const VNode *b) {
		if (a->order != b->order) {
			return a->order < b->order;
		}
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	});
	for (const VNode *node : children) {
		auto item = std::make_unique<Item>();
		item->name = node->name;
		item->path = vfsPathFor(node->id);
		/* Positions are remembered by id, so a rename keeps the icon
		 * where the user put it. */
		item->stateKey = node->id;
		item->isVirtual = true;
		/* A folder is plain unless the registry gives it an icon (Home's
		 * doghouse, the System Folder's emblems). */
		item->kind = node->icon;
		item->isDir = node->kind != VKind::Launcher && node->kind != VKind::Font &&
			(node->kind != VKind::Resource || node->resourceDirectory);
		item->labelIndex = vfsLabel(item->path);
		if (node->kind == VKind::Font) {
			item->fontFile = node->backing;
			item->fontFaceIndex = node->fontFaceIndex;
			item->kindText = fontCanRemove(node->backing) ? "user font" : "system font (read-only)";
			const QFileInfo source(node->backing);
			item->size = source.size();
			item->modified = source.lastModified();
		} else if (node->kind == VKind::Launcher) {
			item->kindText = node->actionId.isEmpty()
				? QStringLiteral("application program")
				: QStringLiteral("application command");
		} else if (node->kind == VKind::Resource && node->resourceProvider == "startup-items") {
			item->kindText = node->resourceKey.isEmpty() ?
				QStringLiteral("folder") : QStringLiteral("startup item");
		} else if (node->kind == VKind::Resource) {
			item->kindText = node->resourceDirectory ?
				QStringLiteral("read-only folder") : QStringLiteral("system information");
		} else {
			item->kindText = QStringLiteral("folder");
		}
		if (node->kind == VKind::Launcher && node->actionId.isEmpty() && !node->appId.isEmpty()) {
			/* The application itself, not one of its Desktop Actions:
			 * show its own icon when one was resolved. */
			if (const AppEntry *app = appById(node->appId)) {
				item->customIcon32 = app->icon32;
				item->customIcon16 = app->icon16;
			}
		}
		if (node->kind == VKind::Resource && node->resourceProvider == "startup-items" &&
				!node->resourceKey.isEmpty()) {
			/* A startup item looks like the application it opens. */
			if (const AppEntry *app = appById(node->resourceKey)) {
				item->customIcon32 = app->icon32;
				item->customIcon16 = app->icon16;
			}
		}
		items.push_back(std::move(item));
	}
	return items;
}

QString vfsOpensAs(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return QString();
	}
	if (node->kind == VKind::Unix) {
		return QStringLiteral("/");
	}
	if (node->kind != VKind::Backed || node->backing.isEmpty()) {
		return QString();
	}
	/* Appearance and Fonts may not exist yet; the Mac's System Folder
	 * always had them, so make them on first use. A mounted disk is
	 * read-only, so never create anything on it. */
	if (!node->readOnly) {
		QDir().mkpath(node->backing);
	}
	return node->backing;
}

/* ---- launching ---------------------------------------------------------- */

bool vfsLaunch(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node || node->kind != VKind::Launcher) {
		return false;
	}
	if (node->builtinAction == "system-information" ||
			node->builtinAction == "extensions-manager" || node->builtinAction == "toy-box" ||
			node->builtinAction == "clipboard" || node->builtinAction == "system" ||
			node->builtinAction == "sniffer") {
		return true;
	}
	if (node->builtinAction == "speak-sample") {
		pl_speak(node->panelId.isEmpty()
				? "Hello. This is how I sound."
				: ("Hello. This is " + node->panelId + ".").toUtf8().constData());
		return true;
	}
	if (node->builtinAction == "keyboard-layout" ||
			node->builtinAction == "keychain") {
		const QString program = node->builtinAction == "keyboard-layout"
			? QStringLiteral("zacos9-keyboard") : QStringLiteral("zacos9-keychain");
		return platinumStartApplication(siblingProgram(program), {});
	}
	if (!node->appId.isEmpty()) {
		return appLaunch(node->appId, node->actionId);
	}
	if (!node->program.isEmpty()) {
		return platinumStartApplication(siblingProgram(node->program), node->args);
	}
	return false;
}

QString vfsPathBacking(const QString &directory) {
	ensureLoaded();
	const QString want = QDir::cleanPath(directory);
	QString best;
	for (const VNode &node : std::as_const(g_nodes)) {
		if (node.kind == VKind::Backed && node.visible && !node.backing.isEmpty() &&
				QDir::cleanPath(node.backing) == want &&
				(best.isEmpty() || node.id.size() < best.size())) {
			best = node.id;
		}
	}
	return best.isEmpty() ? QString() : vfsPathFor(best);
}

QString vfsRealCounterpart(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return QString();
	}
	if (node->kind == VKind::Backed || node->kind == VKind::Unix) {
		return vfsOpensAs(path);
	}
	if (node->kind == VKind::Fonts) {
		const QString directory = userFontsPath();
		if (!QDir().mkpath(directory)) {
			qWarning() << "Cannot create the user Fonts folder:" << directory;
			return {};
		}
		return directory;
	}
	if (!node->appId.isEmpty()) {
		const AppEntry *app = appById(node->appId);
		return app ? app->file : QString();
	}
	return QString();
}

bool vfsResourceOpenPath(const QString &path, QString *sourcePath) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Resource && !node->resourceDirectory &&
		resourceOpenPath(node->resourceProvider, node->resourceKey, sourcePath);
}

bool vfsResourceDetails(const QString &path, QStringList *details, QString *kind,
		QString *location, QString *source, QString *access) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return false;
	}
	ResourceDetails info;
	if (node->kind == VKind::Font) {
		info.name = node->name;
		info.kind = "installed font";
		info.location = "System Folder / Fonts";
		info.sources = node->backing;
		info.access = fontCanRemove(node->backing) ?
			"Move out to uninstall; Option-drag exports a copy without uninstalling." :
			"System font: may be copied, but not removed through Finder.";
		info.metadata = { "Face index: " + QString::number(node->fontFaceIndex),
			"Removing a collection file removes every face in that file.",
			"Applications already using this font may need to be restarted after removal." };
	} else if (node->kind == VKind::Resource) {
		info = resourceDetails(node->resourceProvider, node->resourceKey);
		QString parentKey;
		if (const VNode *parent = vfsNode(vfsPathFor(node->parentId()))) {
			parentKey = parent->resourceKey;
		}
		for (const SystemResource &child :
				resourceChildren(node->resourceProvider, parentKey)) {
			if (child.key == node->resourceKey) {
				info.name = child.name;
				info.kind = child.kind;
				info.description = child.description;
				info.metadata = child.metadata;
				if (!child.source.isEmpty()) {
					info.sources = child.source;
				}
				if (child.type == ResourceType::Information) {
					info.access = "Informational only; no source files are changed.";
				}
				break;
			}
		}
	} else {
		info.name = node->name;
		info.kind = node->kind == VKind::Launcher ? "application launcher" :
			node->kind == VKind::Apps ? "virtual application collection" :
			node->kind == VKind::Panels ? "virtual control-panel collection" :
			node->kind == VKind::PanelsDisabled ? "virtual control-panel collection" :
			node->kind == VKind::Volume ? "startup disk" :
			node->kind == VKind::ExternalVolume ? "disk" :
			node->kind == VKind::Backed ? "folder" : "curated folder";
		if (node->kind == VKind::Volume || node->kind == VKind::ExternalVolume) {
			info.location = "Desktop";
		} else {
			QStringList parents;
			QString parentId = node->parentId();
			while (!parentId.isEmpty()) {
				const VNode *parent = vfsNode(vfsPathFor(parentId));
				if (!parent || parent->kind == VKind::Volume ||
						parent->kind == VKind::ExternalVolume) {
					break;
				}
				parents.prepend(parent->name);
				parentId = parent->parentId();
			}
			const QString volumeName = node->externalVolumeName.isEmpty() ?
				g_volumeName : node->externalVolumeName;
			info.location = volumeName + (parents.isEmpty() ? QString() :
				':' + parents.join(':'));
		}
		info.sources = vfsRealCounterpart(path);
		if (node->kind == VKind::Launcher && !node->appId.isEmpty()) {
			if (const AppEntry *app = appById(node->appId)) {
				info.description = app->comment;
				info.version = app->version;
				info.ownership = app->origin;
			}
			info.access = "Application is not modified by Finder Get Info.";
			info.kind = "application program";
		} else if (node->kind == VKind::ExternalVolume) {
			info.version = node->externalVersion;
			info.ownership = "ZacOS 9";
			info.sources = node->externalMount;
			info.description = "A ZacOS 9 disk mounted from outside this computer. Its "
				"Applications, System Folder and Home are shown here; Debian's Unix "
				"folders stay out of the disk's root.";
			info.access = "Read-only presentation; nothing on the mounted disk is changed.";
		} else if (node->kind == VKind::Backed) {
			const QFileInfo backing(node->backing);
			info.access = node->readOnly || !backing.isWritable() ?
				"Read-only under the current user's ordinary filesystem permissions." :
				"Ordinary filesystem permissions apply; writes are allowed by the source folder.";
			info.description = node->readOnly ?
				"A real folder on a mounted ZacOS 9 disk, shown through the Macintosh view." :
				"A real folder shown in the Macintosh view.";
		} else if (node->id == "system-folder") {
			info.kind = "system folder";
			info.version = installedZacOSVersion();
			info.ownership = "ZacOS 9";
			info.description = "The System Folder collects ZacOS 9's extensions, control panels, "
				"fonts and startup items. It is built from several places and has no single "
				"directory on disk; to keep a file, save it in Home or Documents.";
			info.access = "Virtual Finder metadata only; this item has no single file source.";
		} else {
			info.access = "Virtual Finder metadata only; this item has no single file source.";
			info.description = "A virtual item with no physical equivalent on disk; Finder "
				"builds it from several places.";
		}
	}
	if (details) {
		*details = info.metadata;
		if (!info.description.isEmpty()) {
			details->prepend("Description: " + info.description);
		}
		if (!info.version.isEmpty()) {
			details->append("Version: " + info.version);
		}
		if (!info.ownership.isEmpty()) {
			details->append("Package ownership: " + info.ownership);
		}
		if (!info.status.isEmpty()) {
			details->append("Status: " + info.status);
		}
		if (!info.startup.isEmpty()) {
			details->append("Startup: " + info.startup);
		}
		details->append("Access: " + info.access);
	}
	if (kind) {
		*kind = info.kind;
	}
	if (location) {
		*location = info.location;
	}
	if (source) {
		*source = info.sources;
	}
	if (access) {
		*access = info.access;
	}
	return true;
}

QString vfsBuiltinAction(const QString &path) {
	const VNode *node = vfsNode(path);
	return node ? node->builtinAction : QString();
}

/* ---- metadata ----------------------------------------------------------- */

bool vfsCanRename(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return false;
	}
	/* A Desktop Action isn't a thing of its own; everything else the user
	 * sees may carry a name of their choosing. */
	return !node->readOnly && node->actionId.isEmpty() && node->builtinAction.isEmpty() &&
		node->kind != VKind::Resource && node->kind != VKind::Font;
}

/* The folder user-made nodes keep their contents in. */
static QString userFoldersDir() {
	return dataDir() + "/folders";
}

QString vfsNewFolder() {
	ensureLoaded();
	QStringList taken;
	for (auto it = g_nodes.cbegin(); it != g_nodes.cend(); ++it) {
		if (it->parentId().isEmpty() && !it->id.isEmpty()) {
			taken << it->name.toLower();
		}
	}
	QString name = "untitled folder";
	for (int i = 2; taken.contains(name.toLower()); i++) {
		name = "untitled folder " + QString::number(i);
	}
	int n = 1;
	while (g_nodes.contains("folder-" + QString::number(n)) ||
			QFileInfo::exists(userFoldersDir() + "/folder-" + QString::number(n))) {
		n++;
	}
	const QString id = "folder-" + QString::number(n);
	const QString backing = userFoldersDir() + "/" + id;
	if (!QDir().mkpath(backing)) {
		return QString();
	}
	QJsonObject root = readRegistryFile();
	QJsonArray nodes = root.value("nodes").toArray();
	QJsonObject node;
	node.insert("id", id);
	node.insert("name", name);
	node.insert("kind", "backed");
	node.insert("backing", backing);
	node.insert("user", true);
	node.insert("order", 10);
	nodes.append(node);
	root.insert("nodes", nodes);
	writeRegistryFile(root);
	load();
	notifyChanged();
	return vfsPathFor(id);
}

bool vfsIsUserFolder(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->user;
}

bool vfsDeleteUserFolder(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node || !node->user) {
		return false;
	}
	/* Only an empty one: a folder that holds files goes to the Trash by
	 * emptying it first, so nothing is lost with a stray gesture. */
	if (QDir(node->backing).exists() && !QDir().rmdir(node->backing)) {
		return false;
	}
	const QString id = node->id;
	QJsonObject root = readRegistryFile();
	QJsonArray nodes = root.value("nodes").toArray();
	for (int i = nodes.size() - 1; i >= 0; i--) {
		if (nodes.at(i).toObject().value("id").toString() == id) {
			nodes.removeAt(i);
		}
	}
	root.insert("nodes", nodes);
	QJsonObject overrides = root.value("overrides").toObject();
	overrides.remove(id);
	root.insert("overrides", overrides);
	writeRegistryFile(root);
	load();
	notifyChanged();
	return true;
}

bool vfsCanDelete(const QString &path) {
	/* Nothing in the curated hierarchy can be thrown away: the folders
	 * are this Finder's structure, and the applications belong to dpkg. */
	(void)path;
	return false;
}

bool vfsIsAppFolder(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Launcher &&
		!node->appId.isEmpty() && node->actionId.isEmpty();
}

bool vfsHideApplication(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node || node->kind != VKind::Launcher ||
			node->appId.isEmpty() || !node->actionId.isEmpty()) {
		return false;
	}
	setOverride(node->id, "hidden", true);
	notifyChanged();
	return true;
}

void vfsShowAllHidden() {
	QJsonObject root = readRegistryFile();
	QJsonObject overrides = root.value("overrides").toObject();
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		QJsonObject entry = it.value().toObject();
		if (entry.contains("hidden")) {
			entry.remove("hidden");
			it.value() = entry;
		}
	}
	root.insert("overrides", overrides);
	writeRegistryFile(root);
	load();
	notifyChanged();
}

bool vfsAcceptsDrops(const QString &path) {
	const VNode *node = vfsNode(path);
	/* The Control Panels folders take a control panel dragged in, and
	 * nothing else; the drop itself decides which. */
	return node && !node->readOnly && (node->kind == VKind::Backed ||
		node->kind == VKind::Unix || node->kind == VKind::Fonts ||
		(node->kind == VKind::Apps && !node->id.startsWith("system-folder/")) ||
		vfsIsStartupItems(path) || vfsIsControlPanels(path) ||
		vfsIsControlPanelsDisabled(path) || vfsIsCollarModules(path) ||
		vfsIsCollarModulesDisabled(path) || vfsIsExtensions(path) ||
		vfsIsExtensionsDisabled(path) || vfsIsSounds(path));
}

bool vfsIsSounds(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Resource && node->resourceProvider == "sounds" &&
		(node->resourceKey.isEmpty() || node->resourceKey == "user:");
}

bool vfsIsFonts(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Fonts;
}

QString vfsFontFile(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Font ? node->backing : QString();
}

void vfsFontsChanged() {
	fontInvalidate();
	fontWriteOwnAccepts();
	notifyChanged();
}

bool vfsIsStartupItems(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Resource && node->resourceProvider == "startup-items" &&
		node->resourceKey.isEmpty();
}

QString vfsStartupItemId(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Resource && node->resourceProvider == "startup-items" &&
		!node->resourceKey.isEmpty() ? node->resourceKey : QString();
}

QString vfsStartupItemsPath() {
	for (const VNode &node : g_nodes) {
		if (node.kind == VKind::Resource && node.resourceProvider == "startup-items" &&
				node.resourceKey.isEmpty()) {
			return vfsPathFor(node.id);
		}
	}
	return QString();
}

bool vfsIsApplications(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Apps && !node->id.startsWith("system-folder/");
}

bool vfsIsControlPanels(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Panels;
}

bool vfsIsControlPanelsDisabled(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::PanelsDisabled;
}

bool vfsIsCollarModules(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::CollarModules;
}

bool vfsIsCollarModulesDisabled(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::CollarModulesDisabled;
}

QString vfsPanelId(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Launcher ? node->panelId : QString();
}

QString vfsCollarModuleId(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Launcher ? node->panelId : QString();
}

static bool extensionsFolder(const QString &path, const char *provider) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Resource &&
		node->resourceProvider == QLatin1String(provider) && node->resourceKey.isEmpty();
}

bool vfsIsExtensions(const QString &path) {
	return extensionsFolder(path, "extensions");
}

bool vfsIsExtensionsDisabled(const QString &path) {
	return extensionsFolder(path, "extensions-disabled");
}

QString vfsModuleKey(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node || node->kind != VKind::Resource || node->resourceKey.isEmpty()) {
		return QString();
	}
	return node->resourceProvider == "extensions" ||
		node->resourceProvider == "extensions-disabled" ? node->resourceKey : QString();
}

bool vfsRename(const QString &path, const QString &newName) {
	const VNode *node = vfsNode(path);
	if (!node || !vfsCanRename(path) || newName.trimmed().isEmpty()) {
		return false;
	}
	const QString id = node->id;
	if (id.isEmpty()) {
		/* The startup disk: its name lives with the volume. */
		QJsonObject root = readRegistryFile();
		QJsonObject volume = root.value("volume").toObject();
		volume.insert("name", newName.trimmed());
		root.insert("volume", volume);
		writeRegistryFile(root);
		load();
		notifyChanged();
		return true;
	}
	setOverride(id, "name", newName.trimmed());
	notifyChanged();
	return true;
}

bool vfsNameTaken(const QString &path, const QString &name) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return false;
	}
	const QString parent = node->parentId();
	for (auto it = g_nodes.cbegin(); it != g_nodes.cend(); ++it) {
		if (it->id != node->id && it->parentId() == parent &&
				it->name.compare(name, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

int vfsLabel(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return 0;
	}
	auto it = g_overrides.find(node->id);
	return it != g_overrides.end() && it->hasLabel ? it->label : 0;
}

bool vfsSetLabel(const QString &path, int label) {
	const VNode *node = vfsNode(path);
	if (!node || node->readOnly || node->kind == VKind::Resource ||
			!node->builtinAction.isEmpty()) {
		return false;
	}
	setOverride(node->id, "label", label);
	notifyChanged();
	return true;
}

/* ---- the real filesystem ------------------------------------------------ */

bool vfsUnixVolumeShown() {
	ensureLoaded();
	return g_showUnix;
}

void vfsSetUnixVolumeShown(bool shown) {
	QJsonObject root = readRegistryFile();
	root.insert("showUnixVolume", shown);
	writeRegistryFile(root);
	load();
	notifyChanged();
}

/* ---- refreshing --------------------------------------------------------- */

QString vfsRegistryPath() {
	return dataDir() + "/vfs.json";
}

void vfsRefresh() {
	if (g_refreshing) {
		return;
	}
	g_refreshing = true;
	appRefresh();
	load();
	notifyChanged();
	g_refreshing = false;
}

/* Resource folders that change outside Finder (autostart entries, the
 * user's systemd units): watched without recursion, each through its
 * nearest existing ancestor so a folder created later is noticed. Events
 * are debounced; Finder re-lists lazily, so nothing is rescanned here. */
static void watchResourceSources() {
	static QFileSystemWatcher *watcher = nullptr;
	static QTimer *settle = nullptr;
	auto arm = [] {
		const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
		const AutostartContext autostart = autostartDefaultContext();
		QList<QPair<QString, QStringList>> sources = {
			{ config + "/autostart", { "*.desktop" } },
			{ config + "/systemd/user", { "*.service" } },
		};
		for (const QString &dir : autostart.systemDirs) {
			sources << qMakePair(dir, QStringList{ "*.desktop" });
		}
		int fileCount = watcher->files().size();
		for (const auto &source : sources) {
			QString existing = source.first;
			while (!QDir(existing).exists() && existing != "/" && !existing.isEmpty()) {
				existing = QFileInfo(existing).absolutePath();
			}
			if (!existing.isEmpty() && watcher->directories().size() < 64 &&
					!watcher->directories().contains(existing)) {
				watcher->addPath(existing);
			}
			if (existing != source.first || !QDir(source.first).exists()) {
				continue;
			}
			QDirIterator it(source.first, source.second, QDir::Files | QDir::Readable,
				QDirIterator::NoIteratorFlags);
			while (it.hasNext() && fileCount < 512) {
				const QString path = it.next();
				if (!watcher->files().contains(path) && watcher->addPath(path)) {
					++fileCount;
				}
			}
		}
	};
	if (!watcher) {
		watcher = new QFileSystemWatcher;
		settle = new QTimer;
		settle->setSingleShot(true);
		settle->setInterval(500);
		QTimer *debounce = settle;
		QObject::connect(debounce, &QTimer::timeout, [arm] {
			arm();
			notifyChanged();
		});
		QObject::connect(watcher, &QFileSystemWatcher::directoryChanged,
			[debounce](const QString &) { debounce->start(); });
		QObject::connect(watcher, &QFileSystemWatcher::fileChanged,
			[debounce](const QString &) { debounce->start(); });
	}
	arm();
}

int vfsOnChange(std::function<void()> f) {
	const int token = g_nextToken++;
	g_callbacks.insert(token, std::move(f));
	static bool watching = false;
	if (!watching) {
		watching = true;
		/* An application installed or removed changes Applications. */
		appOnChange([] {
			load();
			notifyChanged();
		});
		watchResourceSources();
		/* Programs see the user's own fonts too (fonts.h). */
		fontWriteOwnAccepts();
		fontWatchChanges([] {
			fontWriteOwnAccepts();
			notifyChanged();
		});
	}
	return token;
}

void vfsOffChange(int token) {
	g_callbacks.remove(token);
}
