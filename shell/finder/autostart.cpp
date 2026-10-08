#include "autostart.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <glib.h>
#include <algorithm>
#include <memory>

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

/* First word of Exec, honoring quotes; "env VAR=x cmd" looks through env. */
QString execProgram(const QString &exec) {
	QStringList words;
	gint argc = 0;
	gchar **argv = nullptr;
	if (g_shell_parse_argv(exec.toUtf8().constData(), &argc, &argv, nullptr)) {
		for (int i = 0; i < argc; i++) {
			words << QString::fromUtf8(argv[i]);
		}
		g_strfreev(argv);
	}
	if (!words.isEmpty() && QFileInfo(words.first()).fileName() == "env") {
		words.removeFirst();
		while (!words.isEmpty() && (words.first().contains('=') || words.first().startsWith('-'))) {
			words.removeFirst();
		}
	}
	return words.value(0);
}

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

QString disabledReason(const KeyFile &file, const QStringList &desktops) {
	if (file.has("Type") && file.str("Type") != "Application") {
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
	if (!executableAvailable(execProgram(file.str("Exec")))) {
		return "Program not found";
	}
	return {};
}

} // namespace

AutostartContext autostartDefaultContext() {
	AutostartContext c;
	c.userDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
	const QString xdg = qEnvironmentVariable("XDG_CONFIG_DIRS", "/etc/xdg");
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
			e.reason = disabledReason(file, context.desktops);
			e.enabled = e.reason.isEmpty();
			out.push_back(e);
		}
	}
	/* Different IDs starting the same command would start it twice. */
	QSet<QString> commands;
	for (AutostartEntry &e : out) {
		if (e.enabled && !commands.contains(e.command.simplified())) {
			commands << e.command.simplified();
		} else if (e.enabled) {
			e.enabled = false;
			e.reason = "Duplicate";
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
	if (!QFileInfo(source).isFile()) {
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
