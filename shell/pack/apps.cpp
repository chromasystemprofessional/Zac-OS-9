/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GLib's headers use it as a name. */
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include "apps.h"

#include <QIcon>
#include <QPainter>
#include <algorithm>

#include "icons.h"
#include "logo.h"

bool lookUpApp(const QString &appId, AppDetails *details) {
	if (appId.isEmpty()) {
		return false;
	}
	for (const QString &id : { appId + ".desktop", appId.toLower() + ".desktop" }) {
		GDesktopAppInfo *info = g_desktop_app_info_new(id.toUtf8().constData());
		if (!info) {
			continue;
		}
		details->name = QString::fromUtf8(g_app_info_get_name(G_APP_INFO(info)));
		details->desktopFile = QString::fromUtf8(g_desktop_app_info_get_filename(info));
		char *icon = g_desktop_app_info_get_string(info, "Icon");
		details->iconName = icon ? QString::fromUtf8(icon) : QString();
		g_free(icon);
		g_object_unref(info);
		return true;
	}
	return false;
}

std::vector<PackApp> packApps(const std::vector<WindowInfo> &windows, const AppLookup &lookup) {
	std::vector<PackApp> apps;
	auto find = [&apps](const QString &id) {
		return std::find_if(apps.begin(), apps.end(), [&id](const PackApp &a) { return a.appId == id; });
	};
	bool anyActive = false;
	for (const WindowInfo &w : windows) {
		auto it = find(w.appId);
		if (it == apps.end()) {
			PackApp app;
			app.appId = w.appId;
			app.hidden = true;
			AppDetails d;
			if (lookup(w.appId, &d)) {
				app.name = d.name;
				app.iconName = d.iconName;
				app.desktopFile = d.desktopFile;
			} else {
				app.name = w.title.isEmpty() ? w.appId : w.title;
			}
			apps.push_back(app);
			it = apps.end() - 1;
		}
		it->active |= w.active;
		it->hidden &= w.minimized;
		anyActive |= w.active;
	}
	auto sniffer = find(QString::fromLatin1(SNIFFER_APP_ID));
	if (sniffer == apps.end()) {
		PackApp app;
		app.appId = SNIFFER_APP_ID;
		apps.push_back(app);
		sniffer = apps.end() - 1;
	}
	sniffer->name = QStringLiteral("Sniffer");
	sniffer->hidden = false;
	sniffer->active |= !anyActive;
	std::sort(apps.begin(), apps.end(), [](const PackApp &a, const PackApp &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return apps;
}

QImage appIcon(const PackApp &app, int size) {
	if (app.appId == QLatin1String(SNIFFER_APP_ID)) {
		const QImage logo(reinterpret_cast<const uchar *>(logo_pixels_hq()), PL_LOGO_SIZE_HQ,
			PL_LOGO_SIZE_HQ, QImage::Format_ARGB32);
		return logo.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	QIcon icon = app.iconName.startsWith('/') ? QIcon(app.iconName) : QIcon::fromTheme(app.iconName);
	if (!icon.isNull()) {
		return icon.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32)
			.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	const int s = size >= 32 ? 32 : 16;
	const QImage generic(reinterpret_cast<const uchar *>(pl_icon(PL_ICON_APPLICATION, s)), s, s,
		QImage::Format_ARGB32);
	return generic.copy().scaled(size, size);
}

bool openWithApp(const PackApp &app, const QStringList &files) {
	if (app.desktopFile.isEmpty() || files.isEmpty()) {
		return false;
	}
	GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(app.desktopFile.toUtf8().constData());
	if (!info) {
		return false;
	}
	GList *uris = nullptr;
	for (const QString &f : files) {
		GFile *file = g_file_new_for_path(f.toUtf8().constData());
		uris = g_list_append(uris, g_file_get_uri(file));
		g_object_unref(file);
	}
	const bool ok = g_app_info_launch_uris(G_APP_INFO(info), uris, nullptr, nullptr);
	g_list_free_full(uris, g_free);
	g_object_unref(info);
	return ok;
}
