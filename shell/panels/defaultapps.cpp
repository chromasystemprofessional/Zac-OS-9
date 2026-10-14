/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GLib's headers use it as a name. */
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include "defaultapps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

static QStringList typesFor(AppRole role) {
	return role == AppRole::Mail ? QStringList{ "x-scheme-handler/mailto" } :
		QStringList{ "x-scheme-handler/http", "x-scheme-handler/https", "text/html" };
}

/* Does the desktop file say it opens `type`? (GIO's own index can miss
 * entries in the user's applications folder.) */
static bool declares(GAppInfo *info, const QByteArray &type) {
	if (!G_IS_DESKTOP_APP_INFO(info)) {
		return false;
	}
	gchar **types = g_desktop_app_info_get_string_list(G_DESKTOP_APP_INFO(info), "MimeType", nullptr);
	const bool found = types && g_strv_contains(types, type.constData());
	g_strfreev(types);
	return found;
}

QList<DefaultApp> defaultAppChoices(AppRole role) {
	QList<DefaultApp> apps;
	const QByteArray type = typesFor(role).first().toUtf8();
	const auto add = [&apps](GAppInfo *info) {
		const char *id = g_app_info_get_id(info);
		if (id && g_app_info_should_show(info) &&
				std::none_of(apps.begin(), apps.end(), [id](const DefaultApp &a) { return a.id == id; })) {
			apps.append({ QString::fromUtf8(id), QString::fromUtf8(g_app_info_get_name(info)) });
		}
	};
	GList *forType = g_app_info_get_all_for_type(type.constData());
	for (GList *l = forType; l; l = l->next) {
		add(G_APP_INFO(l->data));
	}
	g_list_free_full(forType, g_object_unref);
	GList *all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		if (declares(G_APP_INFO(l->data), type)) {
			add(G_APP_INFO(l->data));
		}
	}
	g_list_free_full(all, g_object_unref);
	std::sort(apps.begin(), apps.end(), [](const DefaultApp &a, const DefaultApp &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return apps;
}

QString defaultApp(AppRole role) {
	GAppInfo *info = g_app_info_get_default_for_type(typesFor(role).first().toUtf8().constData(), FALSE);
	if (!info) {
		return {};
	}
	const QString id = QString::fromUtf8(g_app_info_get_id(info));
	g_object_unref(info);
	return id;
}

bool setDefaultApp(AppRole role, const QString &id) {
	GDesktopAppInfo *info = g_desktop_app_info_new(id.toUtf8().constData());
	if (!info) {
		return false;
	}
	bool ok = true;
	for (const QString &type : typesFor(role)) {
		GError *error = nullptr;
		if (!g_app_info_set_as_default_for_type(G_APP_INFO(info), type.toUtf8().constData(), &error)) {
			ok = false;
			g_clear_error(&error);
		}
	}
	g_object_unref(info);
	return ok;
}

QList<InstalledApp> installedApps() {
	QList<InstalledApp> apps;
	GList *all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		GAppInfo *info = G_APP_INFO(l->data);
		const char *file = G_IS_DESKTOP_APP_INFO(info) ?
			g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info)) : nullptr;
		if (file && g_app_info_should_show(info)) {
			apps.append({ QString::fromUtf8(g_app_info_get_id(info)),
				QString::fromUtf8(g_app_info_get_name(info)), QString::fromUtf8(file) });
		}
	}
	g_list_free_full(all, g_object_unref);
	std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return apps;
}

QString itemDisplayName(const QString &path) {
	if (path.endsWith(".desktop")) {
		GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(path.toUtf8().constData());
		if (info) {
			const QString name = QString::fromUtf8(g_app_info_get_name(G_APP_INFO(info)));
			g_object_unref(info);
			return name;
		}
	}
	return QFileInfo(path).fileName();
}

static QString userDirsFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/user-dirs.dirs";
}

QString downloadFolder() {
	QFile f(userDirsFile());
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &raw : f.readAll().split('\n')) {
			const QString line = QString::fromUtf8(raw).trimmed();
			if (line.startsWith("XDG_DOWNLOAD_DIR=")) {
				QString value = line.mid(17);
				if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
					value = value.mid(1, value.size() - 2);
				}
				value.replace("$HOME", QDir::homePath());
				if (!value.isEmpty()) {
					return value;
				}
			}
		}
	}
	return QDir::homePath() + "/Downloads";
}

bool setDownloadFolder(const QString &path) {
	const QString clean = QDir::cleanPath(path);
	if (clean.isEmpty() || !QDir().mkpath(clean)) {
		return false;
	}
	/* "$HOME/..." when it's in Home, as xdg-user-dirs writes it. */
	const QString home = QDir::homePath();
	const QString value = clean == home ? "$HOME" :
		clean.startsWith(home + "/") ? "$HOME" + clean.mid(home.size()) : clean;
	QStringList lines;
	QFile f(userDirsFile());
	if (f.open(QIODevice::ReadOnly)) {
		lines = QString::fromUtf8(f.readAll()).split('\n');
		f.close();
	}
	while (!lines.isEmpty() && lines.last().isEmpty()) {
		lines.removeLast();
	}
	const QString entry = "XDG_DOWNLOAD_DIR=\"" + value + "\"";
	bool replaced = false;
	for (QString &line : lines) {
		if (line.trimmed().startsWith("XDG_DOWNLOAD_DIR=")) {
			line = entry;
			replaced = true;
		}
	}
	if (!replaced) {
		lines << entry;
	}
	QDir().mkpath(QFileInfo(userDirsFile()).absolutePath());
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	f.write((lines.join('\n') + '\n').toUtf8());
	return true;
}
