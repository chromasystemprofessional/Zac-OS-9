/* GIO before anything of Qt's: Qt defines `signals` as a macro. */
#include <gio/gdesktopappinfo.h>

#include "contextitems.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <algorithm>

namespace {

/* Whether a file of this type is one of the entry's MimeType= kinds. */
bool matches(const QMimeType &type, bool folder, const QStringList &wanted) {
	if (wanted.isEmpty()) {
		return true;
	}
	for (const QString &w : wanted) {
		if (w == "all/all" || (w == "all/allfiles" && !folder) ||
				(w.endsWith("/*") && type.name().startsWith(w.chopped(1))) ||
				type.inherits(w)) {
			return true;
		}
	}
	return false;
}

QStringList supportedTypes(GAppInfo *app) {
	QStringList types;
	const char **list = g_app_info_get_supported_types(app);
	for (int i = 0; list && list[i]; i++) {
		types << QString::fromUtf8(list[i]);
	}
	return types;
}

} // namespace

QString contextItemsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/Contextual Menu Items";
}

QList<ContextCommand> contextCommands(const QString &dir, const QStringList &paths) {
	QList<ContextCommand> commands;
	if (paths.isEmpty()) {
		return commands;
	}
	const QMimeDatabase mimes;
	const QFileInfoList files = QDir(dir).entryInfoList({ "*.desktop" }, QDir::Files);
	for (const QFileInfo &file : files) {
		/* GIO reads the entry: null for broken ones or a missing TryExec. */
		GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(
			QFile::encodeName(file.absoluteFilePath()).constData());
		if (!app) {
			continue;
		}
		const QString name = QString::fromUtf8(g_app_info_get_name(G_APP_INFO(app)))
			.replace('\t', ' ').replace('\n', ' ').trimmed();
		const QStringList wanted = supportedTypes(G_APP_INFO(app));
		const bool hidden = g_desktop_app_info_get_is_hidden(app);
		g_object_unref(app);
		if (hidden || name.isEmpty()) {
			continue;
		}
		const bool all = std::all_of(paths.begin(), paths.end(), [&](const QString &path) {
			const QFileInfo info(path);
			return matches(mimes.mimeTypeForFile(info), info.isDir(), wanted);
		});
		if (all) {
			commands.append({ name, file.absoluteFilePath() });
		}
	}
	std::sort(commands.begin(), commands.end(), [](const ContextCommand &a, const ContextCommand &b) {
		return a.name.localeAwareCompare(b.name) < 0;
	});
	return commands;
}

bool runContextCommand(const ContextCommand &command, const QStringList &paths, QString *error) {
	GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(
		QFile::encodeName(command.file).constData());
	if (!app) {
		*error = "“" + command.name + "” is no longer in the Contextual Menu Items folder.";
		return false;
	}
	GList *files = nullptr;
	for (const QString &path : paths) {
		files = g_list_append(files, g_file_new_for_path(QFile::encodeName(path).constData()));
	}
	GError *gerror = nullptr;
	const bool ok = g_app_info_launch(G_APP_INFO(app), files, nullptr, &gerror);
	if (!ok) {
		*error = "“" + command.name + "” couldn't be started" +
			(gerror ? QString(": ") + QString::fromUtf8(gerror->message) : QString("."));
		g_clear_error(&gerror);
	}
	g_list_free_full(files, g_object_unref);
	g_object_unref(app);
	return ok;
}
