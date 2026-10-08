#include <gio/gdesktopappinfo.h>

#include "autostart.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <glib.h>
#include <algorithm>
#include <memory>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

struct KeyFile {
	GKeyFile *kf = g_key_file_new();
	~KeyFile() { g_key_file_free(kf); }
	bool load(const QString &path) {
		QFileInfo info(path);
		if (!info.isFile() || info.size() > 64 * 1024) {
			return false;
		}
		return g_key_file_load_from_file(kf, QFile::encodeName(path).constData(),
			G_KEY_FILE_KEEP_TRANSLATIONS, nullptr);
	}
	QString str(const char *key) const {
		gchar *v = g_key_file_get_string(kf, "Desktop Entry", key, nullptr);
		const QString out = QString::fromUtf8(v);
		g_free(v);
		return out;
	}
	QString localized(const char *key, const QString &locale) const {
		if (locale.isEmpty()) {
			gchar *v = g_key_file_get_locale_string(kf, "Desktop Entry", key, nullptr, nullptr);
			const QString out = QString::fromUtf8(v);
			g_free(v);
			return out;
		}
		gchar **variants = g_get_locale_variants(locale.toUtf8().constData());
		for (int i = 0; variants && variants[i]; i++) {
			const QString name = QString("%1[%2]").arg(key, QString::fromUtf8(variants[i]));
			const QString v = str(name.toUtf8().constData());
			if (!v.isEmpty()) {
				g_strfreev(variants);
				return v;
			}
		}
		g_strfreev(variants);
		return str(key);
	}
	bool has(const char *key) const {
		return g_key_file_has_key(kf, "Desktop Entry", key, nullptr);
	}
	bool boolean(const char *key) const {
		return g_key_file_get_boolean(kf, "Desktop Entry", key, nullptr);
	}
	QStringList list(const char *key) const {
		QStringList out;
		gsize n = 0;
		gchar **v = g_key_file_get_string_list(kf, "Desktop Entry", key, &n, nullptr);
		for (gsize i = 0; v && i < n; i++) {
			out << QString::fromUtf8(v[i]);
		}
		g_strfreev(v);
		return out;
	}
};

bool executableAvailable(const QString &program) {
	if (program.isEmpty()) {
		return false;
	}
	if (program.contains('/')) {
		const QFileInfo info(program);
		return info.isFile() && info.isExecutable();
	}
	return !QStandardPaths::findExecutable(program).isEmpty();
}

QString execProgram(const QString &exec) {
	gint argc = 0;
	gchar **argv = nullptr;
	if (!g_shell_parse_argv(exec.toUtf8().constData(), &argc, &argv, nullptr)) {
		return {};
	}
	const QString program = argc > 0 ? QString::fromUtf8(argv[0]) : QString();
	g_strfreev(argv);
	return program;
}

QString disabledReason(const QString &path, const KeyFile &file, const QStringList &desktops) {
	if (!file.has("Type") || file.str("Type") != "Application") {
		return "Not an application";
	}
	if (file.boolean("Hidden")) {
		return "Disabled";
	}
	if (file.has("X-GNOME-Autostart-enabled") && !file.boolean("X-GNOME-Autostart-enabled")) {
		return "Disabled";
	}
	const QStringList only = file.list("OnlyShowIn");
	if (!only.isEmpty()) {
		bool match = false;
		for (const QString &d : desktops) {
			match = match || only.contains(d, Qt::CaseSensitive);
		}
		if (!match) {
			return "Not for this desktop";
		}
	}
	for (const QString &d : file.list("NotShowIn")) {
		if (desktops.contains(d)) {
			return "Not for this desktop";
		}
	}
	const QString tryExec = file.str("TryExec");
	if (!tryExec.isEmpty() && !executableAvailable(tryExec)) {
		return "Program not found";
	}
	GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(QFile::encodeName(path).constData());
	if (!app) {
		return "Invalid desktop entry";
	}
	const bool dbusActivatable = g_desktop_app_info_get_boolean(app, "DBusActivatable");
	const QString executable = execProgram(file.str("Exec"));
	const bool available = dbusActivatable ||
		executableAvailable(executable);
	g_object_unref(app);
	if (!available) {
		return "Program not found";
	}
	return {};
}

} // namespace

AutostartContext autostartDefaultContext() {
	AutostartContext c;
	c.userDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
	QString xdg = qEnvironmentVariable("XDG_CONFIG_DIRS");
	if (xdg.isEmpty()) {
		xdg = "/etc/xdg";
	}
	for (const QString &d : xdg.split(':', Qt::SkipEmptyParts)) {
		c.systemDirs << d + "/autostart";
	}
	c.desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':', Qt::SkipEmptyParts);
	return c;
}

std::vector<AutostartEntry> autostartEntries(const AutostartContext &context) {
	std::vector<AutostartEntry> out;
	QSet<QString> seen;
	QStringList dirs = { context.userDir };
	dirs += context.systemDirs;
	QSet<QString> systemIds;
	for (const QString &dir : context.systemDirs) {
		for (const QString &f : QDir(dir).entryList({ "*.desktop" }, QDir::Files | QDir::Readable)) {
			systemIds << f;
		}
	}
	for (int i = 0; i < dirs.size(); i++) {
		const QStringList files = QDir(dirs.at(i)).entryList({ "*.desktop" },
			QDir::Files | QDir::Readable, QDir::Name);
		for (const QString &id : files) {
			if (seen.contains(id)) {
				continue; /* masked by a higher-priority directory */
			}
			seen << id;
			KeyFile file;
			const QString path = dirs.at(i) + "/" + id;
			if (!file.load(path)) {
				continue;
			}
			AutostartEntry e;
			e.id = id;
			e.path = path;
			e.userEntry = i == 0;
			e.overridesSystem = i == 0 && systemIds.contains(id);
			e.name = file.localized("Name", context.locale).trimmed();
			if (e.name.isEmpty()) {
				e.name = QFileInfo(id).completeBaseName();
			}
			e.description = file.localized("Comment", context.locale).trimmed();
			e.command = file.str("Exec");
			e.reason = disabledReason(path, file, context.desktops);
			e.enabled = e.reason.isEmpty();
			out.push_back(e);
		}
	}
	std::sort(out.begin(), out.end(), [](const AutostartEntry &a, const AutostartEntry &b) {
		return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
	});
	return out;
}

std::vector<AutostartEntry> autostartEntries() {
	return autostartEntries(autostartDefaultContext());
}

bool autostartSetEnabled(const AutostartContext &context, const QString &id, bool enabled,
		QString *error) {
	auto fail = [&](const QString &why) {
		if (error) {
			*error = why;
		}
		return false;
	};
	if (id.isEmpty() || id.contains('/') || !id.endsWith(".desktop")) {
		return fail("Invalid startup item.");
	}
	QString source = context.userDir + "/" + id;
	const bool userOverride = QFileInfo(source).isFile();
	if (!userOverride) {
		source.clear();
		for (const QString &dir : context.systemDirs) {
			if (QFileInfo(dir + "/" + id).isFile()) {
				source = dir + "/" + id;
				break;
			}
		}
	}
	KeyFile file;
	if (source.isEmpty() || !file.load(source)) {
		return fail("The startup item could not be read.");
	}
	if (enabled) {
		if (userOverride && file.boolean("Hidden")) {
			gsize n = 0;
			gchar **keys = g_key_file_get_keys(file.kf, "Desktop Entry", &n, nullptr);
			const bool hiddenOnly = n == 1 && keys && g_str_equal(keys[0], "Hidden");
			g_strfreev(keys);
			if (hiddenOnly) {
				const bool lowerPriorityExists = std::any_of(context.systemDirs.cbegin(),
					context.systemDirs.cend(), [&](const QString &dir) {
						return QFileInfo(dir + "/" + id).isFile();
					});
				if (!lowerPriorityExists) {
					return fail("There is no lower-priority startup entry to restore.");
				}
				return QFile::remove(context.userDir + "/" + id) ? true :
					fail("The disabled override could not be removed.");
			}
		}
		g_key_file_remove_key(file.kf, "Desktop Entry", "Hidden", nullptr);
		g_key_file_remove_key(file.kf, "Desktop Entry", "X-GNOME-Autostart-enabled", nullptr);
	} else {
		g_key_file_set_boolean(file.kf, "Desktop Entry", "Hidden", true);
	}
	gsize length = 0;
	gchar *data = g_key_file_to_data(file.kf, &length, nullptr);
	QDir().mkpath(context.userDir);
	QSaveFile out(context.userDir + "/" + id);
	const bool ok = out.open(QIODevice::WriteOnly) &&
		out.write(data, qint64(length)) == qint64(length) && out.commit();
	g_free(data);
	return ok ? true : fail("The startup item could not be saved.");
}

bool autostartRunSession(QString *error) {
	return autostartRunSession(autostartDefaultContext(), error);
}

bool autostartRunSession(const AutostartContext &context, QString *error) {
	auto fail = [&](const QString &why) {
		if (error) {
			*error = why;
		}
		return false;
	};
	QString identity = qEnvironmentVariable("XDG_SESSION_ID");
	if (identity.isEmpty()) {
		identity = qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
	}
	const QByteArray key = QCryptographicHash::hash(identity.toUtf8(),
		QCryptographicHash::Sha256).toHex().left(24);
	QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if (runtime.isEmpty()) {
		runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	}
	if (runtime.isEmpty()) {
		runtime = QStandardPaths::writableLocation(QStandardPaths::StateLocation) + "/zacos9";
	}
	if (!QDir().mkpath(runtime)) {
		return fail("The session runtime directory could not be created.");
	}
	const QString base = runtime + "/zacos9-autostart-" + QString::fromLatin1(key);
	const int lock = ::open(QFile::encodeName(base + ".lock").constData(),
		O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0) {
		return fail("The session startup lock could not be opened.");
	}
	if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
		::close(lock);
		return true;
	}
	if (QFileInfo::exists(base + ".started")) {
		::close(lock);
		return true;
	}
	QStringList errors;
	for (const AutostartEntry &entry : autostartEntries(context)) {
		if (!entry.enabled) {
			continue;
		}
		GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(
			QFile::encodeName(entry.path).constData());
		GAppLaunchContext *context = g_app_launch_context_new();
		GError *launchError = nullptr;
		const bool launched = app && g_app_info_launch(G_APP_INFO(app), nullptr, context,
			&launchError);
		if (!launched) {
			errors << entry.id + ": " + (launchError ?
				QString::fromUtf8(launchError->message) : "invalid desktop entry");
		}
		if (launchError) {
			g_error_free(launchError);
		}
		g_object_unref(context);
		if (app) {
			g_object_unref(app);
		}
	}
	QSaveFile started(base + ".started");
	if (!started.open(QIODevice::WriteOnly) || started.write("started\n") != 8 ||
			!started.commit()) {
		errors << "The session startup marker could not be written.";
	}
	::close(lock);
	if (!errors.isEmpty()) {
		return fail(errors.join('\n'));
	}
	return true;
}
