/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GIO uses it as an identifier. */
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include "fileassoc.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMimeDatabase>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

namespace {

QString fromUtf8(const char *s) {
	return s ? QString::fromUtf8(s) : QString();
}

QMimeDatabase &mimeDb() {
	static QMimeDatabase db;
	return db;
}

/* The directories whose files decide associations (XDG MIME Applications
 * spec: mimeapps.list in the config dirs and the applications dirs, and
 * the desktop entries' MimeType= gathered into mimeinfo.cache). */
QStringList configDirs() {
	QStringList dirs;
	dirs << QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	const QString sys = qEnvironmentVariable("XDG_CONFIG_DIRS", "/etc/xdg");
	dirs << sys.split(':', Qt::SkipEmptyParts);
	return dirs;
}

QStringList applicationDirs() {
	QStringList dirs;
	dirs << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/applications";
	const QString sys = qEnvironmentVariable("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
	for (const QString &d : sys.split(':', Qt::SkipEmptyParts)) {
		dirs << d + "/applications";
	}
	return dirs;
}

QStringList associationFiles() {
	QStringList names = { "mimeapps.list" };
	for (const QString &desktop :
			qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':', Qt::SkipEmptyParts)) {
		names.prepend(desktop.toLower() + "-mimeapps.list");
	}
	QStringList files;
	for (const QStringList &dirs : { configDirs(), applicationDirs() }) {
		for (const QString &dir : dirs) {
			for (const QString &name : names) {
				files << dir + "/" + name;
			}
		}
	}
	for (const QString &dir : applicationDirs()) {
		files << dir + "/mimeinfo.cache";
	}
	return files;
}

class Associations : public QObject {
public:
	Associations() {
		m_settle.setSingleShot(true);
		m_settle.setInterval(250);
		m_settle.callOnTimeout(this, [this] { recheck(); });
		connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
			[this] { m_settle.start(); });
		connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
			[this] { m_settle.start(); });
		m_signature = signature();
		rearm();
	}

	QString defaultFor(const QString &mime) {
		auto it = m_defaults.constFind(mime);
		if (it != m_defaults.constEnd()) {
			return *it;
		}
		QString id;
		if (GAppInfo *info = g_app_info_get_default_for_type(mime.toUtf8().constData(), FALSE)) {
			id = fromUtf8(g_app_info_get_id(info));
			g_object_unref(info);
		}
		m_defaults.insert(mime, id);
		return id;
	}

	void invalidate() {
		m_defaults.clear();
		m_generation++;
	}

	void changed() {
		invalidate();
		for (const QPointer<QWidget> &w : std::as_const(m_widgets)) {
			if (w) {
				w->update();
			}
		}
		for (const auto &f : m_callbacks) {
			f();
		}
	}

	void watch(QWidget *w) {
		m_widgets.removeAll(QPointer<QWidget>());
		m_widgets << w;
	}

	unsigned m_generation = 1;
	std::vector<std::function<void()>> m_callbacks;

private:
	/* What the association files look like now: a directory notice is
	 * only an association change when one of these moved. */
	QString signature() const {
		QString s;
		for (const QString &file : associationFiles()) {
			/* The inode too: a same-size replacement within one
			 * mtime tick is still a different file. */
			struct stat st;
			if (::stat(QFile::encodeName(file).constData(), &st) == 0) {
				s += file + ':' + QString::number(st.st_size) + ':' +
					QString::number(st.st_mtim.tv_sec) + '.' +
					QString::number(st.st_mtim.tv_nsec) + ':' +
					QString::number(st.st_ino) + '\n';
			}
		}
		return s;
	}

	/* The directories (so a file created or swapped in by rename is
	 * seen) and the files that exist (an edit in place). Re-armed after
	 * every change: a replaced file drops out of the watcher. */
	void rearm() {
		QStringList want;
		for (const QString &file : associationFiles()) {
			const QFileInfo info(file);
			if (info.exists()) {
				want << info.absoluteFilePath();
			}
			if (QFileInfo(info.absolutePath()).isDir()) {
				want << info.absolutePath();
			}
		}
		want.removeDuplicates();
		const QStringList have = m_watcher.files() + m_watcher.directories();
		QStringList gone;
		for (const QString &p : have) {
			if (!want.contains(p)) {
				gone << p;
			}
		}
		if (!gone.isEmpty()) {
			m_watcher.removePaths(gone);
		}
		QStringList add;
		for (const QString &p : want) {
			if (!have.contains(p)) {
				add << p;
			}
		}
		if (!add.isEmpty()) {
			m_watcher.addPaths(add);
		}
	}

	void recheck() {
		/* Let GIO's own monitors see the change before it is asked again. */
		for (int i = 0; i < 100 && g_main_context_iteration(nullptr, FALSE); i++) {
		}
		rearm();
		const QString now = signature();
		if (now != m_signature) {
			m_signature = now;
			changed();
		}
	}

	QFileSystemWatcher m_watcher;
	QTimer m_settle;
	QString m_signature;
	QHash<QString, QString> m_defaults;
	QList<QPointer<QWidget>> m_widgets;
};

Associations &assoc() {
	static QPointer<Associations> instance;
	if (!instance) {
		instance = new Associations;
		if (QCoreApplication::instance()) {
			instance->setParent(QCoreApplication::instance());
		}
	}
	return *instance;
}

std::vector<uint32_t> toArgb(const QIcon &icon, int size) {
	const QPixmap pixmap = icon.pixmap(size, size);
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

} // namespace

QString fileMimeType(const QString &path) {
	QFileInfo info(path);
	if (info.isSymLink()) {
		const QString target = info.canonicalFilePath();
		if (target.isEmpty()) {
			return QString();
		}
		info.setFile(target);
	}
	if (!info.exists() || info.isDir()) {
		return QString();
	}
	return mimeDb().mimeTypeForFile(info).name();
}

QString hiddenExtension(const QString &fileName) {
	const QString suffix = mimeDb().suffixForFileName(fileName);
	if (suffix.isEmpty() || fileName.size() <= suffix.size() + 1) {
		return QString();
	}
	const QString hidden = fileName.right(suffix.size() + 1);
	if (hidden.at(0) != '.' || hidden.compare("." + suffix, Qt::CaseInsensitive) != 0) {
		return QString();
	}
	const QString stem = fileName.left(fileName.size() - hidden.size());
	if (stem.trimmed().isEmpty() || stem == QLatin1String(".")) {
		return QString();
	}
	return hidden;
}

QString nameWithoutExtension(const QString &fileName) {
	return fileName.left(fileName.size() - hiddenExtension(fileName).size());
}

QString defaultAppFor(const QString &mimeType) {
	if (mimeType.isEmpty()) {
		return QString();
	}
	return assoc().defaultFor(mimeType);
}

QStringList appsFor(const QString &mimeType) {
	QStringList ids;
	const QString def = defaultAppFor(mimeType);
	if (!def.isEmpty()) {
		ids << def;
	}
	GList *all = g_app_info_get_all_for_type(mimeType.toUtf8().constData());
	for (GList *l = all; l; l = l->next) {
		const QString id = fromUtf8(g_app_info_get_id(G_APP_INFO(l->data)));
		if (!id.isEmpty() && !ids.contains(id)) {
			ids << id;
		}
	}
	g_list_free_full(all, g_object_unref);
	/* GIO's candidates come from mimeinfo.cache; an application installed
	 * without update-desktop-database is only in its own MimeType=. */
	const QByteArray want = mimeType.toUtf8();
	all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		const char **supported = g_app_info_get_supported_types(G_APP_INFO(l->data));
		for (int i = 0; supported && supported[i]; i++) {
			if (want == supported[i]) {
				const QString id = fromUtf8(g_app_info_get_id(G_APP_INFO(l->data)));
				if (!id.isEmpty() && !ids.contains(id)) {
					ids << id;
				}
				break;
			}
		}
	}
	g_list_free_full(all, g_object_unref);
	return ids;
}

bool setDefaultApp(const QString &mimeType, const QString &appId, QString *error) {
	GDesktopAppInfo *info = g_desktop_app_info_new(appId.toUtf8().constData());
	if (!info) {
		if (error) {
			*error = QStringLiteral("The application “%1” isn't installed.").arg(appId);
		}
		return false;
	}
	GError *err = nullptr;
	const bool ok = g_app_info_set_as_default_for_type(G_APP_INFO(info),
		mimeType.toUtf8().constData(), &err);
	g_object_unref(info);
	if (!ok) {
		if (error) {
			*error = err ? fromUtf8(err->message) : QStringLiteral("Unknown error.");
		}
		g_clear_error(&err);
		return false;
	}
	assoc().changed();
	return true;
}

QString appDisplayName(const QString &appId) {
	GDesktopAppInfo *info = g_desktop_app_info_new(appId.toUtf8().constData());
	if (!info) {
		return QString();
	}
	const QString name = fromUtf8(g_app_info_get_name(G_APP_INFO(info)));
	g_object_unref(info);
	return name;
}

std::vector<uint32_t> appIconPixels(const QString &appId, int size) {
	/* QPixmap needs a GUI platform (appdb.cpp's resolveIcon, likewise). */
	if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
		return {};
	}
	GDesktopAppInfo *info = g_desktop_app_info_new(appId.toUtf8().constData());
	if (!info) {
		return {};
	}
	std::vector<uint32_t> out;
	GIcon *icon = g_app_info_get_icon(G_APP_INFO(info));
	if (icon && G_IS_FILE_ICON(icon)) {
		char *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
		out = toArgb(QIcon(fromUtf8(path)), size);
		g_free(path);
	} else if (icon && G_IS_THEMED_ICON(icon)) {
		const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
		for (int i = 0; names && names[i] && out.empty(); i++) {
			const QIcon q = QIcon::fromTheme(fromUtf8(names[i]));
			if (!q.isNull()) {
				out = toArgb(q, size);
			}
		}
	}
	g_object_unref(info);
	return out;
}

std::vector<FileType> openableFileTypes() {
	QSet<QString> seen;
	std::vector<FileType> types;
	GList *all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		const char **supported = g_app_info_get_supported_types(G_APP_INFO(l->data));
		for (int i = 0; supported && supported[i]; i++) {
			const QMimeType mime = mimeDb().mimeTypeForName(fromUtf8(supported[i]));
			/* Aliases fold into their canonical type; unknown names
			 * (x-scheme-handler/..., typos) name no file. */
			if (!mime.isValid() || mime.name().startsWith("x-scheme-handler/") ||
					mime.name() == "inode/directory" || seen.contains(mime.name())) {
				continue;
			}
			seen.insert(mime.name());
			FileType t;
			t.mimeType = mime.name();
			t.description = mime.comment().isEmpty() ? mime.name() : mime.comment();
			if (!t.description.isEmpty()) {
				t.description[0] = t.description[0].toUpper();
			}
			t.extensions = mime.suffixes();
			types.push_back(t);
		}
	}
	g_list_free_full(all, g_object_unref);
	std::sort(types.begin(), types.end(), [](const FileType &a, const FileType &b) {
		const int c = QString::localeAwareCompare(a.description, b.description);
		return c != 0 ? c < 0 : a.mimeType < b.mimeType;
	});
	return types;
}

unsigned fileAssocGeneration() {
	return assoc().m_generation;
}

void watchFileAssociations(QWidget *widget) {
	assoc().watch(widget);
}

void fileAssocOnChange(std::function<void()> f) {
	assoc().m_callbacks.push_back(std::move(f));
}

void fileAssocInvalidate() {
	assoc().changed();
}
