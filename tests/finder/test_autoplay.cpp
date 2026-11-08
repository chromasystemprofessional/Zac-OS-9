#include <gio/gio.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <cassert>
#include <functional>

#include "autoplay.h"
#include "settings.h"

#ifndef ZACOS9_MIMEAPPS
#error "ZACOS9_MIMEAPPS must point to share/xdg/zacos9-mimeapps.list"
#endif

struct TestVolume {
	GObject parent;
	const char *device;
};
struct TestVolumeClass { GObjectClass parent; };
static void volumeInterface(GVolumeIface *iface);
G_DEFINE_TYPE_WITH_CODE(TestVolume, test_volume, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(G_TYPE_VOLUME, volumeInterface))
static void test_volume_init(TestVolume *) {}
static void test_volume_class_init(TestVolumeClass *) {}

static void volumeInterface(GVolumeIface *iface) {
	iface->get_identifier = [](GVolume *volume, const char *kind) -> char * {
		auto *test = reinterpret_cast<TestVolume *>(volume);
		return g_strcmp0(kind, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE) == 0
			? g_strdup(test->device) : nullptr;
	};
}

struct TestMount {
	GObject parent;
	char **types;
	char *uuid;
	char *root;
	GVolume *volume;
};
struct TestMountClass { GObjectClass parent; };
static void mountInterface(GMountIface *iface);
G_DEFINE_TYPE_WITH_CODE(TestMount, test_mount, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(G_TYPE_MOUNT, mountInterface))
static void test_mount_init(TestMount *) {}
static void test_mount_class_init(TestMountClass *type) {
	G_OBJECT_CLASS(type)->finalize = [](GObject *object) {
		auto *mount = reinterpret_cast<TestMount *>(object);
		g_strfreev(mount->types);
		g_free(mount->uuid);
		g_free(mount->root);
		g_clear_object(&mount->volume);
		G_OBJECT_CLASS(test_mount_parent_class)->finalize(object);
	};
}

static char **contentTypes(GMount *mount) {
	return g_strdupv(reinterpret_cast<TestMount *>(mount)->types);
}

static void mountInterface(GMountIface *iface) {
	iface->get_name = [](GMount *) -> char * { return g_strdup("Test Disc"); };
	iface->get_uuid = [](GMount *mount) -> char * {
		return g_strdup(reinterpret_cast<TestMount *>(mount)->uuid);
	};
	iface->get_root = [](GMount *mount) -> GFile * {
		return g_file_new_for_path(reinterpret_cast<TestMount *>(mount)->root);
	};
	iface->get_volume = [](GMount *mount) -> GVolume * {
		TestMount *test = reinterpret_cast<TestMount *>(mount);
		return test->volume ? G_VOLUME(g_object_ref(test->volume)) : nullptr;
	};
	iface->guess_content_type = [](GMount *mount, gboolean, GCancellable *cancel,
			GAsyncReadyCallback callback, gpointer data) {
		GTask *task = g_task_new(mount, cancel, callback, data);
		g_task_return_pointer(task, contentTypes(mount),
			reinterpret_cast<GDestroyNotify>(g_strfreev));
		g_object_unref(task);
	};
	iface->guess_content_type_finish = [](GMount *, GAsyncResult *result,
			GError **error) -> char ** {
		return static_cast<char **>(g_task_propagate_pointer(G_TASK(result), error));
	};
	iface->guess_content_type_sync = [](GMount *mount, gboolean, GCancellable *,
			GError **) -> char ** {
		return contentTypes(mount);
	};
}

static TestMount *makeMount(const QString &root, const char *uuid,
		const char *const *types, GVolume *volume = nullptr) {
	auto *mount = static_cast<TestMount *>(g_object_new(test_mount_get_type(), nullptr));
	mount->root = g_strdup(root.toUtf8().constData());
	mount->uuid = g_strdup(uuid);
	GPtrArray *array = g_ptr_array_new();
	for (const char *const *type = types; type && *type; type++) {
		g_ptr_array_add(array, g_strdup(*type));
	}
	g_ptr_array_add(array, nullptr);
	mount->types = reinterpret_cast<char **>(g_ptr_array_free(array, FALSE));
	if (volume) {
		mount->volume = G_VOLUME(g_object_ref(volume));
	}
	return mount;
}

static bool waitFor(const std::function<bool()> &ready, int ms = 3000) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < ms) {
		QCoreApplication::processEvents();
		for (int i = 0; i < 16 && g_main_context_iteration(nullptr, FALSE); i++) {
		}
		QThread::msleep(5);
	}
	return ready();
}

static QString readFile(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.readAll()).trimmed();
}

static void writeFile(const QString &path, const QByteArray &contents, bool executable = false) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	assert(file.write(contents) == contents.size());
	file.close();
	if (executable) {
		assert(file.setPermissions(file.permissions() | QFileDevice::ExeOwner |
			QFileDevice::ExeGroup | QFileDevice::ExeOther));
	}
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	assert(home.isValid());
	const QString data = home.path() + "/data";
	const QString config = home.path() + "/config";
	const QString configDirs = home.path() + "/config-dirs";
	assert(QDir().mkpath(configDirs));
	assert(QFile::copy(QString::fromUtf8(ZACOS9_MIMEAPPS),
		configDirs + "/zacos9-mimeapps.list"));
	qputenv("XDG_DATA_HOME", data.toUtf8());
	qputenv("XDG_DATA_DIRS", (home.path() + "/system:/usr/local/share:/usr/share").toUtf8());
	qputenv("XDG_CONFIG_HOME", config.toUtf8());
	qputenv("XDG_CONFIG_DIRS", configDirs.toUtf8());
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	qputenv("QT_NO_GLIB", "1");

	const QString player = home.path() + "/player.sh";
	writeFile(player, "#!/bin/sh\nprintf '%s\\n' \"$2\" > \"$1\"\n", true);
	const QString videoMarker = home.path() + "/video.txt";
	const QString audioMarker = home.path() + "/audio.txt";
	const QString otherMarker = home.path() + "/other.txt";
	writeFile(data + "/applications/vlc-opendvd.desktop",
		QString("[Desktop Entry]\nType=Application\nName=Preferred DVD\n"
			"Exec=%1 %2 %f\nMimeType=x-content/video-dvd;\n"
			"NoDisplay=true\n").arg(player, videoMarker).toUtf8());
	writeFile(data + "/applications/vlc-opencda.desktop",
		QString("[Desktop Entry]\nType=Application\nName=Preferred CD\n"
			"Exec=%1 %2 %f\nMimeType=x-content/audio-cdda;\n"
			"NoDisplay=true\n").arg(player, audioMarker).toUtf8());
	writeFile(data + "/applications/zzz-other.desktop",
		QString("[Desktop Entry]\nType=Application\nName=Other DVD\n"
			"Exec=%1 %2 %f\nMimeType=x-content/video-dvd;\n"
			"NoDisplay=true\n").arg(player, otherMarker).toUtf8());
	writeFile(data + "/applications/mimeinfo.cache",
		"[MIME Cache]\n"
		"x-content/video-dvd=vlc-opendvd.desktop;zzz-other.desktop;\n"
		"x-content/audio-cdda=vlc-opencda.desktop;\n");

	QCoreApplication app(argc, argv);
	/* The shipped default list names VLC's purpose-built disc entries. */
	GAppInfo *videoDefault = g_app_info_get_default_for_type("x-content/video-dvd", FALSE);
	assert(videoDefault && g_strcmp0(g_app_info_get_id(videoDefault),
		"vlc-opendvd.desktop") == 0);
	g_object_unref(videoDefault);
	GAppInfo *audioDefault = g_app_info_get_default_for_type("x-content/audio-cdda", FALSE);
	assert(audioDefault && g_strcmp0(g_app_info_get_id(audioDefault),
		"vlc-opencda.desktop") == 0);
	g_object_unref(audioDefault);

	assert(mediaAutoplayEnabled());
	pl_setting_set("media-autoplay", "false");
	assert(!mediaAutoplayEnabled());
	pl_setting_set("media-autoplay", nullptr);
	assert(mediaAutoplayEnabled());

	/* With the setting off, even an optical mount is not handled. */
	const QString root = home.path() + "/disc";
	assert(QDir().mkpath(root));
	const char *offTypes[] = { "x-content/video-dvd", nullptr };
	pl_setting_set("media-autoplay", "false");
	TestMount *off = makeMount(root, "uuid-off", offTypes);
	assert(!mediaAutoplayMount(G_MOUNT(off), MediaKind::OpticalData));
	g_object_unref(off);
	pl_setting_set("media-autoplay", nullptr);

	/* The content types AutoPlay handles, in its preference order. */
	char *types[] = {
		const_cast<char *>("x-content/audio-cdda"),
		const_cast<char *>("x-content/video-dvd"),
		nullptr,
	};
	assert(mediaAutoplayType(types) == "x-content/video-dvd");
	char *audioOnly[] = { const_cast<char *>("x-content/audio-cdda"), nullptr };
	assert(mediaAutoplayType(audioOnly) == "x-content/audio-cdda");
	char *blank[] = { const_cast<char *>("x-content/blank-cd"), nullptr };
	assert(mediaAutoplayType(blank).isEmpty());

	const char *videoTypes[] = { "x-content/video-dvd", nullptr };
	TestMount *video = makeMount(root, "uuid-video", videoTypes);
	bool missingCalled = false;
	QString missingType;
	assert(mediaAutoplayMount(G_MOUNT(video), MediaKind::OpticalData,
		[&](const QString &type) { missingCalled = true; missingType = type; }));
	assert(waitFor([&] { return QFile::exists(videoMarker); }));
	assert(readFile(videoMarker) == root);
	assert(!missingCalled);
	/* The same mount is only handled once. */
	assert(!mediaAutoplayMount(G_MOUNT(video), MediaKind::OpticalData));
	g_object_unref(video);

	/* An audio CD gives the player the device, not the gvfs mount root. */
	TestVolume *volume = static_cast<TestVolume *>(g_object_new(test_volume_get_type(), nullptr));
	volume->device = "/dev/sr0";
	const char *audioTypes[] = { "x-content/audio-cdda", nullptr };
	TestMount *audio = makeMount(root, "uuid-audio", audioTypes, G_VOLUME(volume));
	assert(mediaAutoplayMount(G_MOUNT(audio), MediaKind::OpticalAudio));
	assert(waitFor([&] { return QFile::exists(audioMarker); }));
	assert(readFile(audioMarker) == "/dev/sr0");
	g_object_unref(audio);
	/* Opening the desktop icon plays it even when AutoPlay is off. */
	QFile::remove(audioMarker);
	assert(mediaPlayAudioCd("/dev/sr0"));
	assert(waitFor([&] { return QFile::exists(audioMarker); }));
	assert(readFile(audioMarker) == "/dev/sr0");
	g_object_unref(volume);

	/* A disc with no handler asks once; it does not try a random app. */
	const char *audioDvdTypes[] = { "x-content/audio-dvd", nullptr };
	TestMount *audioDvd = makeMount(root, "uuid-audio-dvd", audioDvdTypes);
	assert(mediaAutoplayMount(G_MOUNT(audioDvd), MediaKind::OpticalData,
		[&](const QString &type) { missingCalled = true; missingType = type; }));
	assert(waitFor([&] { return missingCalled; }));
	assert(missingType == "x-content/audio-dvd");
	g_object_unref(audioDvd);

	/* USB storage and mounted images are not discs. */
	const char *usbTypes[] = { "x-content/video-dvd", nullptr };
	TestMount *usb = makeMount(root, "uuid-usb", usbTypes);
	assert(!mediaAutoplayMount(G_MOUNT(usb), MediaKind::Usb));
	assert(!QFileInfo::exists(otherMarker));
	g_object_unref(usb);

	return 0;
}
