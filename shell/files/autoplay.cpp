#include <gio/gio.h>

#include "autoplay.h"

#include <QDebug>
#include <QSet>

#include <cstring>

#include "settings.h"

namespace {

QSet<QString> g_seen;

QString mountKey(GMount *mount) {
	char *uuid = g_mount_get_uuid(mount);
	QString key = uuid ? QString::fromUtf8(uuid) : QString();
	g_free(uuid);
	if (!key.isEmpty()) {
		return key;
	}
	GFile *root = g_mount_get_root(mount);
	if (root) {
		char *uri = g_file_get_uri(root);
		key = uri ? QString::fromUtf8(uri) : QString();
		g_free(uri);
		g_object_unref(root);
	}
	if (!key.isEmpty()) {
		return key;
	}
	char *name = g_mount_get_name(mount);
	key = name ? QString::fromUtf8(name) : QString();
	g_free(name);
	return key;
}

QString mountDevice(GMount *mount) {
	GVolume *volume = g_mount_get_volume(mount);
	if (!volume) {
		return {};
	}
	char *raw = g_volume_get_identifier(volume,
		G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	const QString device = raw ? QString::fromUtf8(raw) : QString();
	g_free(raw);
	g_object_unref(volume);
	return device;
}

QByteArray preferredHandler(const QString &type) {
	if (type == QLatin1String("x-content/audio-cdda")) {
		return QByteArrayLiteral("vlc-opencda.desktop");
	}
	if (type == QLatin1String("x-content/video-dvd") ||
			type == QLatin1String("x-content/audio-dvd")) {
		return QByteArrayLiteral("vlc-opendvd.desktop");
	}
	if (type == QLatin1String("x-content/video-vcd") ||
			type == QLatin1String("x-content/video-svcd")) {
		return QByteArrayLiteral("vlc-openvcd.desktop");
	}
	if (type == QLatin1String("x-content/video-bluray")) {
		return QByteArrayLiteral("vlc-openbd.desktop");
	}
	return {};
}

GAppInfo *autoplayHandler(const QString &type) {
	const QByteArray typeName = type.toUtf8();
	const QByteArray wanted = preferredHandler(type);
	GList *all = g_app_info_get_all_for_type(typeName.constData());
	GAppInfo *chosen = nullptr;
	if (!wanted.isEmpty()) {
		for (GList *item = all; item; item = item->next) {
			const char *id = g_app_info_get_id(G_APP_INFO(item->data));
			if (id && wanted == id) {
				chosen = G_APP_INFO(g_object_ref(item->data));
				break;
			}
		}
	}
	if (!chosen) {
		chosen = g_app_info_get_default_for_type(typeName.constData(), FALSE);
	}
	if (!chosen && all) {
		chosen = G_APP_INFO(g_object_ref(all->data));
	}
	if (all) {
		g_list_free_full(all, g_object_unref);
	}
	return chosen;
}

bool launchTarget(GAppInfo *player, GFile *target, const QString &type) {
	bool ok = false;
	GError *error = nullptr;
	if (g_app_info_supports_uris(player) && !g_app_info_supports_files(player)) {
		char *uri = g_file_get_uri(target);
		if (!uri) {
			return false;
		}
		GList uris = { uri, nullptr, nullptr };
		ok = g_app_info_launch_uris(player, &uris, nullptr, &error);
		g_free(uri);
	} else {
		GList files = { target, nullptr, nullptr };
		ok = g_app_info_launch(player, &files, nullptr, &error);
	}
	if (!ok) {
		qWarning().noquote() << "zacos9-finder: could not start"
			<< QString::fromUtf8(g_app_info_get_name(player)) << "for"
			<< type << ":" << (error ? QString::fromUtf8(error->message) : QStringLiteral("unknown error"));
	}
	g_clear_error(&error);
	return ok;
}

bool launchDisc(GAppInfo *player, GMount *mount, const QString &type,
		const QString &device) {
	GFile *target = nullptr;
	/* VLC's "vlc-opencda.desktop" adds cdda:// to a file argument, so
	 * give it the device. A handler that takes URIs gets the GIO mount
	 * root (cdda://... for an audio CD). */
	if (type == QLatin1String("x-content/audio-cdda") && !device.isEmpty() &&
			g_app_info_supports_files(player)) {
		target = g_file_new_for_path(device.toUtf8().constData());
	} else {
		target = g_mount_get_root(mount);
	}
	if (!target) {
		return false;
	}
	const bool ok = launchTarget(player, target, type);
	g_object_unref(target);
	return ok;
}

struct GuessContext {
	GMount *mount;
	QString key;
	std::function<void(const QString &)> missing;
};

void finishGuess(GuessContext *context, char **types) {
	g_strfreev(types);
	g_object_unref(context->mount);
	delete context;
}

void onGuess(GObject *source, GAsyncResult *result, gpointer data) {
	auto *context = static_cast<GuessContext *>(data);
	GError *error = nullptr;
	char **types = g_mount_guess_content_type_finish(G_MOUNT(source), result, &error);
	if (error) {
		qWarning().noquote() << "zacos9-finder: could not read a disc's content type:"
			<< QString::fromUtf8(error->message);
		g_error_free(error);
		/* A transient probe failure may be worth another try on the next
		 * mount-added signal. */
		g_seen.remove(context->key);
		finishGuess(context, types);
		return;
	}
	/* The mount went away while the (asynchronous) probe was running. */
	if (!g_seen.contains(context->key)) {
		finishGuess(context, types);
		return;
	}
	const QString type = mediaAutoplayType(types);
	if (!type.isEmpty()) {
		const QString device = mountDevice(context->mount);
		GAppInfo *player = autoplayHandler(type);
		if (player) {
			launchDisc(player, context->mount, type, device);
			g_object_unref(player);
		} else if (context->missing) {
			context->missing(type);
		}
	}
	finishGuess(context, types);
}

} // namespace

bool mediaAutoplayEnabled() {
	char value[16];
	return !(pl_setting("media-autoplay", value, sizeof(value)) &&
		strcmp(value, "false") == 0);
}

QString mediaAutoplayType(char **types) {
	static const char *const handled[] = {
		"x-content/video-dvd",
		"x-content/audio-cdda",
		"x-content/video-vcd",
		"x-content/video-svcd",
		"x-content/audio-dvd",
		"x-content/video-bluray",
	};
	for (const char *wanted : handled) {
		for (char **type = types; type && *type; type++) {
			if (strcmp(*type, wanted) == 0) {
				return QString::fromUtf8(*type);
			}
		}
	}
	return {};
}

bool mediaPlayAudioCd(const QString &device) {
	if (!device.startsWith(QLatin1String("/dev/"))) {
		return false;
	}
	GAppInfo *player = autoplayHandler(QStringLiteral("x-content/audio-cdda"));
	if (!player) {
		return false;
	}
	GFile *target = nullptr;
	if (g_app_info_supports_files(player)) {
		target = g_file_new_for_path(device.toUtf8().constData());
	} else {
		/* A handler that only takes URIs expects cdda:///dev/sr0. */
		target = g_file_new_for_uri(("cdda://" + device).toUtf8().constData());
	}
	const bool ok = target && launchTarget(player, target,
		QStringLiteral("x-content/audio-cdda"));
	if (target) {
		g_object_unref(target);
	}
	g_object_unref(player);
	return ok;
}

bool mediaAutoplayMount(GMount *mount, MediaKind kind,
		const std::function<void(const QString &)> &missing) {
	if (!mediaAutoplayEnabled()) {
		return false;
	}
	if (!mount || (kind != MediaKind::OpticalData &&
			kind != MediaKind::OpticalAudio &&
			kind != MediaKind::OpticalVideo)) {
		return false;
	}
	const QString key = mountKey(mount);
	if (key.isEmpty() || g_seen.contains(key)) {
		return false;
	}
	g_seen.insert(key);
	auto *context = new GuessContext{ G_MOUNT(g_object_ref(mount)), key, missing };
	g_mount_guess_content_type(mount, FALSE, nullptr, onGuess, context);
	return true;
}

void mediaAutoplayForget(GMount *mount) {
	if (mount) {
		g_seen.remove(mountKey(mount));
	}
}
