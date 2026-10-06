#include "vfs.h"
#include "platinumshell.h"
#include "custompatterns.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>

#include "appdb.h"
#include "apptrash.h"

/*
 * The default mapping. Written to the registry the first time the Finder
 * runs, then owned by the user: edit it to rename the disk, add folders,
 * point Home somewhere else, or show Utilities.
 * Folders the user makes on the startup disk are nodes with "user": true.
 *
 * Changing REGISTRY_VERSION migrates an older file: fields the old file
 * doesn't have take their default, and nodes it doesn't know about are
 * added, while the user's own nodes and overrides are kept.
 */
static constexpr int REGISTRY_VERSION = 9;

static const char *DEFAULT_REGISTRY = R"JSON({
  "version": 9,
  "volume": {},
  "showUnixVolume": false,
  "nodes": [
    { "id": "system-folder", "name": "System Folder", "kind": "folder",
      "icon": "system-folder", "order": 0 },
    { "id": "system-folder/appearance", "name": "Appearance", "kind": "backed",
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
    { "id": "system-folder/extensions", "name": "Extensions", "kind": "folder" },
    { "id": "system-folder/fonts", "name": "Fonts", "kind": "backed",
      "backing": "$XDG_DATA_HOME/fonts" },
    { "id": "system-folder/preferences", "name": "Preferences", "kind": "backed",
      "backing": "$XDG_CONFIG_HOME" },
    { "id": "system-folder/utilities", "name": "Utilities", "kind": "apps" },
    { "id": "applications", "name": "Applications", "kind": "apps", "order": 1,
      "excludeCategories": ["X-ZacOS9-Utility"] },
    { "id": "home", "name": "Home", "kind": "backed",
      "backing": "$HOME", "order": 2 },
    { "id": "utilities", "name": "Utilities", "kind": "apps", "order": 4,
      "visible": false,
      "categories": ["Utility", "System", "Settings"] }
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
bool g_loaded = false;
/* Keyed by token so a window can stop listening when it closes. */
QHash<int, std::function<void()>> g_callbacks;
int g_nextToken = 1;

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
	if (name == "backed") {
		return VKind::Backed;
	}
	if (name == "apps") {
		return VKind::Apps;
	}
	if (name == "panels") {
		return VKind::Panels;
	}
	if (name == "unix") {
		return VKind::Unix;
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
	if (name == "disk") {
		return PL_ICON_DISK;
	}
	if (name == "application") {
		return PL_ICON_APPLICATION;
	}
	if (name == "document") {
		return PL_ICON_DOCUMENT;
	}
	return kind == VKind::Panels ? PL_ICON_CONTROL_PANELS : PL_ICON_FOLDER;
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
		if (n.value("id").toString() == QLatin1String("applications/utilities")) {
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
		if (key == "applications/utilities") {
			destination = "system-folder/utilities";
		} else if (key.startsWith("applications/")) {
			const AppEntry *app = appById(key.section('/', -1));
			if (app && app->systemUtility) {
				destination = "system-folder/utilities/" + app->id;
			}
		}
		if (!destination.isEmpty() && !overrides.contains(destination)) {
			overrides.insert(destination, overrides.take(key));
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

/* A program of ours, from next to this binary in a build tree, else $PATH. */
QString siblingProgram(const QString &name) {
	const QString local = QCoreApplication::applicationDirPath() + "/" + name;
	return QFileInfo(local).isExecutable() ? local : name;
}

struct PanelDef {
	const char *name, *program, *arg;
};
/* Alphabetical, as the Mac's Control Panels folder was. */
const PanelDef PANELS[] = {
	{ "Appearance", "zacos9-appearance", nullptr },
	{ "Date & Time", "zacos9-datetime", nullptr },
	{ "File Sharing", "zacos9-filesharing", nullptr },
	{ "Keyboard", "zacos9-controlpanel", "keyboard" },
	{ "Monitors", "zacos9-controlpanel", "monitors" },
	{ "Mouse", "zacos9-controlpanel", "mouse" },
	{ "Sound", "zacos9-controlpanel", "sound" },
	{ "TCP/IP", "zacos9-tcpip", nullptr },
};

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
		const bool systemFolder = parent.id.startsWith("system-folder/");
		if ((systemFolder ? !app.systemUtility : (app.systemUtility || !app.userInstalled)) ||
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

void generatePanels(const VNode &parent) {
	for (const PanelDef &panel : PANELS) {
		VNode node;
		node.id = parent.id + "/" + QString::fromUtf8(panel.program) +
			(panel.arg ? QString("-") + panel.arg : QString());
		node.name = QString::fromUtf8(panel.name);
		node.kind = VKind::Launcher;
		node.icon = PL_ICON_APPLICATION;
		node.program = QString::fromUtf8(panel.program);
		if (panel.arg) {
			node.args << QString::fromUtf8(panel.arg);
		}
		addNode(node);
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
		addNode(node);
		if (node.kind == VKind::Apps || node.kind == VKind::Panels) {
			generators.push_back(node);
		}
	}
	/* Generated children are added after every registry node exists, so a
	 * generator's own settings are already in place. */
	for (const VNode &node : generators) {
		if (node.kind == VKind::Apps) {
			generateApps(node);
		} else {
			generatePanels(node);
		}
	}

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

/* ---- listing ------------------------------------------------------------ */

std::vector<std::unique_ptr<Item>> vfsList(const QString &path) {
	std::vector<std::unique_ptr<Item>> items;
	const VNode *parent = vfsNode(path);
	if (!parent) {
		return items;
	}
	/* A Backed node is a real directory; its contents are real files. */
	if (parent->kind == VKind::Backed || parent->kind == VKind::Unix) {
		return items;
	}
	const QString parentId = parent->id;
	std::vector<const VNode *> children;
	for (auto it = g_nodes.cbegin(); it != g_nodes.cend(); ++it) {
		if (it->parentId() == parentId && it->visible && !it->id.isEmpty()) {
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
		item->kind = node->kind == VKind::Backed ? PL_ICON_FOLDER : node->icon;
		item->isDir = node->kind != VKind::Launcher;
		item->labelIndex = vfsLabel(item->path);
		if (node->kind == VKind::Launcher) {
			item->kindText = node->actionId.isEmpty()
				? QStringLiteral("application program")
				: QStringLiteral("application command");
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
	 * always had them, so make them on first use. */
	QDir().mkpath(node->backing);
	return node->backing;
}

/* ---- launching ---------------------------------------------------------- */

bool vfsLaunch(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node || node->kind != VKind::Launcher) {
		return false;
	}
	if (!node->appId.isEmpty()) {
		return appLaunch(node->appId, node->actionId);
	}
	if (!node->program.isEmpty()) {
		return platinumStartApplication(siblingProgram(node->program), node->args);
	}
	return false;
}

QString vfsRealCounterpart(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return QString();
	}
	if (node->kind == VKind::Backed || node->kind == VKind::Unix) {
		return vfsOpensAs(path);
	}
	if (!node->appId.isEmpty()) {
		const AppEntry *app = appById(node->appId);
		return app ? app->file : QString();
	}
	return QString();
}

/* ---- metadata ----------------------------------------------------------- */

bool vfsCanRename(const QString &path) {
	const VNode *node = vfsNode(path);
	if (!node) {
		return false;
	}
	/* A Desktop Action isn't a thing of its own; everything else the user
	 * sees may carry a name of their choosing. */
	return node->actionId.isEmpty();
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
	return node && (node->kind == VKind::Backed || node->kind == VKind::Unix ||
		(node->kind == VKind::Apps && !node->id.startsWith("system-folder/")));
}

bool vfsIsApplications(const QString &path) {
	const VNode *node = vfsNode(path);
	return node && node->kind == VKind::Apps && !node->id.startsWith("system-folder/");
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
	if (!node) {
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
	appRefresh();
	load();
	notifyChanged();
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
	}
	return token;
}

void vfsOffChange(int token) {
	g_callbacks.remove(token);
}
