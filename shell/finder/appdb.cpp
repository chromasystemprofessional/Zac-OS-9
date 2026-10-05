/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GIO's D-Bus headers have a struct field of that name. */
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include "appdb.h"
#include "platinumshell.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace {

std::vector<AppEntry> g_apps;
bool g_loaded = false;
std::vector<std::function<void()>> g_callbacks;

QString fromUtf8(const char *s) {
	return s ? QString::fromUtf8(s) : QString();
}

/* Finder names can't hold '/'; desktop entries occasionally do. */
QString finderName(const QString &name) {
	QString out = name;
	out.replace('/', '-');
	return out.trimmed();
}

void readActions(GDesktopAppInfo *desktop, AppEntry *entry) {
	const char *const *ids = g_desktop_app_info_list_actions(desktop);
	for (int i = 0; ids && ids[i]; i++) {
		char *name = g_desktop_app_info_get_action_name(desktop, ids[i]);
		entry->actions.push_back({ fromUtf8(ids[i]),
			finderName(name ? fromUtf8(name) : fromUtf8(ids[i])) });
		g_free(name);
	}
}

/* ---- icons --------------------------------------------------------------- */

/* QIcon::fromTheme resolves nothing until it is told where to look: the
 * platform theme plugin usually does that from the desktop's own
 * settings, but nothing here assumes one is running. "hicolor" is the
 * spec's always-present fallback theme, and almost every icon not in a
 * desktop's own theme is installed there directly. */
void ensureIconTheme() {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	QStringList search = QIcon::themeSearchPaths();
	for (const QString &base :
			QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
		const QString icons = base + "/icons";
		if (QDir(icons).exists() && !search.contains(icons)) {
			search << icons;
		}
	}
	QIcon::setThemeSearchPaths(search);
	if (QIcon::themeName().isEmpty()) {
		QIcon::setThemeName("hicolor");
	}
}

/* `size` pixels of straight-alpha ARGB from `pixmap`, scaled if it came
 * back a different size (an SVG rasterized at a nearby size, say). */
std::vector<uint32_t> toArgb(const QPixmap &pixmap, int size) {
	if (pixmap.isNull()) {
		return {};
	}
	QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
	if (img.width() != size || img.height() != size) {
		img = img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}
	std::vector<uint32_t> out(static_cast<size_t>(size) * size);
	for (int y = 0; y < size; y++) {
		memcpy(out.data() + static_cast<size_t>(y) * size, img.constScanLine(y),
			static_cast<size_t>(size) * sizeof(uint32_t));
	}
	return out;
}

/* Candidate icon names in GIO's own preference order (the application's
 * icon, then its symbolic fallback), tried against the icon theme until
 * one resolves. Nothing is guessed beyond what the desktop entry named. */
void resolveIcon(GAppInfo *info, AppEntry *entry) {
	/* QPixmap needs a QPA platform (even the offscreen one): skip rather
	 * than risk the abort a headless QCoreApplication-only process
	 * would get from allocating one. Real Finder processes are always a
	 * QApplication, so this only affects a tool with no GUI at all. */
	if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
		return;
	}
	GIcon *icon = g_app_info_get_icon(info);
	if (!icon) {
		return;
	}
	ensureIconTheme();
	/* Icon= a file path (AppImages and unpacked programs the App
	 * Installer set up, for one): load the file itself. */
	if (G_IS_FILE_ICON(icon)) {
		char *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
		const QIcon qicon(fromUtf8(path));
		g_free(path);
		if (!qicon.isNull()) {
			entry->icon32 = toArgb(qicon.pixmap(32, 32), 32);
			entry->icon16 = toArgb(qicon.pixmap(16, 16), 16);
		}
		return;
	}
	QStringList names;
	if (G_IS_THEMED_ICON(icon)) {
		const char *const *themed = g_themed_icon_get_names(G_THEMED_ICON(icon));
		for (int i = 0; themed && themed[i]; i++) {
			names << fromUtf8(themed[i]);
		}
	}
	for (const QString &name : names) {
		QIcon qicon = QIcon::fromTheme(name);
		if (qicon.isNull()) {
			continue;
		}
		entry->icon32 = toArgb(qicon.pixmap(32, 32), 32);
		entry->icon16 = toArgb(qicon.pixmap(16, 16), 16);
		if (!entry->icon32.empty()) {
			return;
		}
	}
}

/* ---- where an application came from --------------------------------- */

/* dpkg -S and dpkg-query, cached by resolved executable path: run once
 * per path for the life of the process, not on every reload. */
struct PackageInfo {
	QString name, version;
};

QHash<QString, PackageInfo> &packageCache() {
	static QHash<QString, PackageInfo> cache;
	return cache;
}

PackageInfo lookupPackage(const QString &exePath) {
	auto &cache = packageCache();
	auto it = cache.find(exePath);
	if (it != cache.end()) {
		return it.value();
	}
	PackageInfo info;
	QProcess owner;
	owner.start("dpkg", { "-S", exePath });
	if (owner.waitForFinished(3000) && owner.exitCode() == 0) {
		const QString out = QString::fromUtf8(owner.readAllStandardOutput());
		const int colon = out.indexOf(':');
		if (colon > 0) {
			info.name = out.left(colon).trimmed();
		}
	}
	if (!info.name.isEmpty()) {
		QProcess version;
		version.start("dpkg-query", { "-W", "-f=${Version}", info.name });
		if (version.waitForFinished(3000) && version.exitCode() == 0) {
			info.version = QString::fromUtf8(version.readAllStandardOutput()).trimmed();
		}
	}
	cache.insert(exePath, info);
	return info;
}

/* The application's origin, for Get Info: never guessed, and never
 * attributed to a package that merely runs it. A Flatpak export's
 * "Exec=flatpak run ..." would otherwise be read as belonging to the
 * "flatpak" package itself, which is wrong, so that case is read from
 * the desktop entry's own X-Flatpak key instead of dpkg. */
void resolveOrigin(GDesktopAppInfo *desktop, AppEntry *entry) {
	char *flatpakId = g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(desktop), "X-Flatpak");
	if (flatpakId) {
		entry->origin = "flatpak:" + fromUtf8(flatpakId);
		g_free(flatpakId);
		return;
	}
	/* Owned by the GAppInfo itself: not g_free'd. */
	const char *executable = g_app_info_get_executable(G_APP_INFO(desktop));
	if (!executable) {
		return;
	}
	QString exePath = fromUtf8(executable);
	if (!exePath.startsWith('/')) {
		exePath = QStandardPaths::findExecutable(exePath);
	}
	if (exePath.isEmpty()) {
		return;
	}
	/* dpkg's file database has the packaged path, which on a merged-/usr
	 * system is /usr/bin/x even though /bin/x (what most Exec= lines
	 * name) is a symlink to it; dpkg -S on the symlink path itself finds
	 * nothing. */
	const QString canonical = QFileInfo(exePath).canonicalFilePath();
	if (!canonical.isEmpty()) {
		exePath = canonical;
	}
	const PackageInfo pkg = lookupPackage(exePath);
	if (!pkg.name.isEmpty()) {
		entry->origin = "dpkg:" + pkg.name;
		entry->version = pkg.version;
	}
}

/* Append " (stem)" to the names that more than one entry shares, so each
 * application folder has a name of its own. */
void disambiguate(std::vector<AppEntry> &apps) {
	QHash<QString, int> count;
	for (const AppEntry &a : apps) {
		count[a.name.toCaseFolded()]++;
	}
	for (AppEntry &a : apps) {
		if (count.value(a.name.toCaseFolded()) > 1) {
			QString stem = a.id;
			if (stem.endsWith(".desktop")) {
				stem.chop(8);
			}
			a.folderName = finderName(a.name + " (" + stem + ")");
		} else {
			a.folderName = a.name;
		}
	}
}

/* The desktop entries the system came with, listed when the ISO is built
 * (iso/config/hooks/normal/9000-zacos9.hook.chroot) as
 * <data dir>/zacos9/base-applications. Applications holds only what was
 * installed since: from the Software window, apt, or a Windows installer
 * run through Wine. */
const QSet<QString> &baseApplications() {
	static QSet<QString> ids;
	static bool read = false;
	if (!read) {
		read = true;
		for (const QString &path : QStandardPaths::locateAll(
				QStandardPaths::GenericDataLocation, QStringLiteral("zacos9/base-applications"))) {
			QFile f(path);
			if (f.open(QIODevice::ReadOnly)) {
				for (const QByteArray &line : f.readAll().split('\n')) {
					if (!line.trimmed().isEmpty()) {
						ids.insert(QString::fromUtf8(line.trimmed()));
					}
				}
			}
		}
	}
	return ids;
}

void load() {
	g_apps.clear();
	/* g_app_info_get_all() applies XDG data directory precedence and
	 * drops entries that are Hidden or whose TryExec is missing. */
	GList *all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		auto *info = static_cast<GAppInfo *>(l->data);
		/* NoDisplay, and OnlyShowIn/NotShowIn for this desktop. */
		if (!g_app_info_should_show(info)) {
			continue;
		}
		const QString id = fromUtf8(g_app_info_get_id(info));
		const QString name = finderName(fromUtf8(g_app_info_get_name(info)));
		if (id.isEmpty() || name.isEmpty()) {
			continue;
		}
		/* Applications is empty on a fresh install: not ZacOS 9's own
		 * pieces (the control panels and the Apple menu have those), and
		 * not what the system came with - except the utilities ZacOS 9
		 * ships for Applications > Utilities (balenaEtcher, ...). */
		const bool shippedUtility = G_IS_DESKTOP_APP_INFO(info) &&
			fromUtf8(g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(info)))
				.split(';').contains(QLatin1String("X-ZacOS9-Utility"));
		if (id.startsWith(QLatin1String("zacos9-")) ||
				(baseApplications().contains(id) && !shippedUtility)) {
			continue;
		}
		/* Wine files a Windows program's Start-menu shortcuts as desktop
		 * entries (~/.local/share/applications/wine/Programs/..., ids
		 * "wine-Programs-..."), uninstallers among them. */
		if (id.startsWith(QLatin1String("wine-")) &&
				name.startsWith(QLatin1String("Uninstall"), Qt::CaseInsensitive)) {
			continue;
		}
		AppEntry entry;
		entry.id = id;
		entry.name = name;
		entry.comment = fromUtf8(g_app_info_get_description(info));
		entry.commandLine = fromUtf8(g_app_info_get_commandline(info));
		if (G_IS_DESKTOP_APP_INFO(info)) {
			auto *desktop = G_DESKTOP_APP_INFO(info);
			entry.file = fromUtf8(g_desktop_app_info_get_filename(desktop));
			entry.terminal = g_desktop_app_info_get_boolean(desktop, "Terminal");
			const QString cats = fromUtf8(g_desktop_app_info_get_categories(desktop));
			entry.categories = cats.split(';', Qt::SkipEmptyParts);
			readActions(desktop, &entry);
			resolveOrigin(desktop, &entry);
		}
		resolveIcon(info, &entry);
		g_apps.push_back(std::move(entry));
	}
	g_list_free_full(all, g_object_unref);

	std::sort(g_apps.begin(), g_apps.end(), [](const AppEntry &a, const AppEntry &b) {
		const int c = a.name.compare(b.name, Qt::CaseInsensitive);
		return c != 0 ? c < 0 : a.id < b.id;
	});
	disambiguate(g_apps);
	g_loaded = true;
}

/* Every directory desktop entries can come from, in XDG order. */
QStringList applicationDirs() {
	QStringList dirs;
	for (const QString &base : QStandardPaths::standardLocations(
			QStandardPaths::GenericDataLocation)) {
		dirs << base + "/applications";
	}
	dirs.removeDuplicates();
	return dirs;
}

/* Installing a package writes many files, and GIO only drops its own
 * cached list once its file monitors have been served. Both mean a
 * change is read a moment after it is noticed, not during it. */
constexpr int SETTLE_MS = 400;

/* Watch the application directories, and the folders Wine files Windows
 * programs' shortcuts in (applications/wine/Programs/<program>/): a
 * watcher sees only one level, so a shortcut made three folders down was
 * noticed only when something else changed. Run again after each change,
 * as new folders appear. */
void watchDirs(QFileSystemWatcher *w) {
	for (const QString &dir : applicationDirs()) {
		QStringList want;
		if (QDir(dir).exists()) {
			want << dir;
			QDirIterator it(dir + "/wine", QDir::Dirs | QDir::NoDotAndDotDot,
				QDirIterator::Subdirectories);
			if (QDir(dir + "/wine").exists()) {
				want << dir + "/wine";
			}
			while (it.hasNext()) {
				want << it.next();
			}
		}
		for (const QString &d : want) {
			if (!w->directories().contains(d)) {
				w->addPath(d);
			}
		}
	}
}

QFileSystemWatcher *watcher() {
	static QFileSystemWatcher *w = nullptr;
	if (!w) {
		w = new QFileSystemWatcher;
		/* Asking for the monitor is what makes GIO watch the
		 * application directories, so its list expires with ours. */
		static GAppInfoMonitor *monitor = g_app_info_monitor_get();
		(void)monitor;
		auto *settle = new QTimer;
		settle->setSingleShot(true);
		settle->setInterval(SETTLE_MS);
		QObject::connect(settle, &QTimer::timeout, [] {
			load();
			const auto callbacks = g_callbacks;
			for (const auto &f : callbacks) {
				f();
			}
		});
		QObject::connect(w, &QFileSystemWatcher::directoryChanged,
			[w, settle](const QString &) {
				watchDirs(w);
				settle->start();
			});
	}
	return w;
}

} // namespace

const std::vector<AppEntry> &appList() {
	if (!g_loaded) {
		load();
	}
	return g_apps;
}

const AppEntry *appById(const QString &id) {
	for (const AppEntry &a : appList()) {
		if (a.id == id) {
			return &a;
		}
	}
	return nullptr;
}

void appRefresh() {
	/* Serve whatever GIO's file monitors have waiting, so a refresh
	 * asked for by hand sees what is installed now rather than what was
	 * installed when GIO last looked. */
	for (int i = 0; i < 1000 && g_main_context_iteration(nullptr, FALSE); i++) {
	}
	load();
}

void appOnChange(std::function<void()> f) {
	g_callbacks.push_back(std::move(f));
	QFileSystemWatcher *w = watcher();
	watchDirs(w);
}

const AppEntry *appByFile(const QString &desktopFile) {
	const QString want = QFileInfo(desktopFile).canonicalFilePath();
	if (want.isEmpty()) {
		return nullptr;
	}
	for (const AppEntry &a : appList()) {
		if (QFileInfo(a.file).canonicalFilePath() == want) {
			return &a;
		}
	}
	return nullptr;
}

static GAppLaunchContext *launchContext(GAppInfo *info, uint32_t &cookie) {
	const char *wmClass = G_IS_DESKTOP_APP_INFO(info)
		? g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(info)) : nullptr;
	cookie = platinumBeginLaunch(fromUtf8(wmClass ? wmClass :
		g_app_info_get_id(info)));
	GAppLaunchContext *context = g_app_launch_context_new();
	g_signal_connect(context, "launched", G_CALLBACK(+[](GAppLaunchContext *,
			GAppInfo *, GVariant *data, gpointer user) {
		gint32 pid = 0;
		if (g_variant_lookup(data, "pid", "i", &pid) && pid > 0) {
			platinumUpdateLaunch(GPOINTER_TO_UINT(user), static_cast<uint32_t>(pid));
		}
	}), GUINT_TO_POINTER(cookie));
	return context;
}

static bool launchInfo(GAppInfo *info, GList *files, const QString &actionId = {}) {
	uint32_t cookie;
	GAppLaunchContext *context = launchContext(info, cookie);
	bool ok = true;
	if (actionId.isEmpty()) {
		GError *error = nullptr;
		ok = g_app_info_launch(info, files, context, &error);
		if (!ok) {
			platinumCancelLaunch(cookie);
			qWarning() << "Could not launch" << fromUtf8(g_app_info_get_name(info)) << ":" <<
				(error ? error->message : "unknown launch error");
		}
		if (error) {
			g_error_free(error);
		}
	} else {
		/* Desktop Actions have no failure report of their own. */
		g_desktop_app_info_launch_action(G_DESKTOP_APP_INFO(info),
			actionId.toUtf8().constData(), context);
	}
	g_object_unref(context);
	return ok;
}

bool appLaunchFile(const QString &desktopFile) {
	const QString real = QFileInfo(desktopFile).canonicalFilePath();
	GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(real.toUtf8().constData());
	if (!info) {
		return false;
	}
	const bool ok = launchInfo(G_APP_INFO(info), nullptr);
	g_object_unref(info);
	return ok;
}

bool appLaunch(const QString &id, const QString &actionId) {
	/* Looked up by ID so the entry is re-read: the application may have
	 * been upgraded, or replaced by one earlier in the XDG search path. */
	GDesktopAppInfo *info = g_desktop_app_info_new(id.toUtf8().constData());
	if (!info) {
		return false;
	}
	const bool ok = launchInfo(G_APP_INFO(info), nullptr, actionId);
	g_object_unref(info);
	return ok;
}

bool appOpenFile(const QString &path) {
	GFile *file = g_file_new_for_path(path.toUtf8().constData());
	GError *error = nullptr;
	GAppInfo *info = g_file_query_default_handler(file, nullptr, &error);
	if (!info) {
		qWarning() << "Could not find an application for" << path << ":" <<
			(error ? error->message : "no default application");
		if (error) {
			g_error_free(error);
		}
		g_object_unref(file);
		return false;
	}
	GList files = { file, nullptr, nullptr };
	const bool ok = launchInfo(info, &files);
	g_object_unref(info);
	g_object_unref(file);
	return ok;
}
