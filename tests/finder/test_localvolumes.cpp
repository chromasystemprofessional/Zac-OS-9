#include <gio/gio.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cassert>
#include <functional>

#include "localvolumes.h"
#include "netvolumes.h"

std::vector<NetVolume> netVolumes() { return {}; }

struct TestVolume {
	GObject parent;
	bool ready, mounted, fail;
	int attempts;
};
struct TestVolumeClass { GObjectClass parent; };
static void volumeInterface(GVolumeIface *iface);
G_DEFINE_TYPE_WITH_CODE(TestVolume, test_volume, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(G_TYPE_VOLUME, volumeInterface))
static void test_volume_init(TestVolume *) {}
static void test_volume_class_init(TestVolumeClass *) {}

static void volumeInterface(GVolumeIface *iface) {
	iface->get_name = [](GVolume *) { return g_strdup("Test USB"); };
	iface->get_identifier = [](GVolume *, const char *kind) -> char * {
		return g_strcmp0(kind, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE) == 0
			? g_strdup("/dev/sdz1") : nullptr;
	};
	iface->can_mount = [](GVolume *volume) -> gboolean {
		return reinterpret_cast<TestVolume *>(volume)->ready;
	};
	iface->get_mount = [](GVolume *) -> GMount * { return nullptr; };
	iface->mount_fn = [](GVolume *volume, GMountMountFlags, GMountOperation *,
			GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
		auto *test = reinterpret_cast<TestVolume *>(volume);
		++test->attempts;
		GTask *task = g_task_new(volume, cancel, callback, data);
		g_idle_add([](gpointer value) -> gboolean {
			auto *task = G_TASK(value);
			auto *volume = reinterpret_cast<TestVolume *>(g_task_get_source_object(task));
			if (volume->fail) {
				g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Test unreadable disk");
			} else {
				volume->mounted = true;
				g_task_return_boolean(task, TRUE);
			}
			g_object_unref(task);
			return G_SOURCE_REMOVE;
		}, task);
	};
	iface->mount_finish = [](GVolume *, GAsyncResult *result, GError **error) -> gboolean {
		return g_task_propagate_boolean(G_TASK(result), error);
	};
}

struct TestMonitor { GVolumeMonitor parent; };
struct TestMonitorClass { GVolumeMonitorClass parent; };
static TestVolume *volume;
static TestMonitor *monitor;
G_DEFINE_TYPE(TestMonitor, test_monitor, G_TYPE_VOLUME_MONITOR)
static void test_monitor_init(TestMonitor *) {}
static void test_monitor_class_init(TestMonitorClass *type) {
	auto *klass = G_VOLUME_MONITOR_CLASS(type);
	klass->get_volumes = [](GVolumeMonitor *) -> GList * {
		return g_list_append(nullptr, g_object_ref(volume));
	};
	klass->get_mounts = [](GVolumeMonitor *) -> GList * { return nullptr; };
}

/* Override discovery only in this test executable: no real storage is touched. */
extern "C" GVolumeMonitor *g_volume_monitor_get(void) {
	return G_VOLUME_MONITOR(g_object_ref(monitor));
}

static bool waitFor(const std::function<bool()> &ready) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < 2000) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return ready();
}

int main(int argc, char **argv) {
	qputenv("QT_NO_GLIB", "1");
	QCoreApplication app(argc, argv);
	assert(localVolumePathShown("/run/media/adam/Untitled"));
	assert(localVolumePathShown("/media/adam/Untitled"));
	assert(localVolumePathShown("/runner/files"));
	assert(!localVolumePathShown("/"));
	assert(!localVolumePathShown("/run/user/1000/gvfs"));
	assert(!localVolumePathShown("/proc"));
	assert(!localVolumePathShown("/sys/devices"));
	assert(!localVolumePathShown("/dev"));
	assert(!localVolumePathShown("/snap/app"));
	volume = static_cast<TestVolume *>(g_object_new(test_volume_get_type(), nullptr));
	monitor = static_cast<TestMonitor *>(g_object_new(test_monitor_get_type(), nullptr));
	int failures = 0, refreshes = 0;
	localVolumesOnChange([&] { ++refreshes; });
	localVolumesOnMountFailed([&](const QString &path) {
		assert(path == "/dev/sdz1");
		++failures;
	});
	localVolumesMountAll();
	assert(volume->attempts == 0);
	g_signal_emit_by_name(monitor, "volume-added", volume);
	assert(volume->attempts == 0);
	volume->ready = true;
	g_signal_emit_by_name(monitor, "volume-changed", volume);
	g_signal_emit_by_name(monitor, "volume-changed", volume);
	assert(volume->attempts == 1);
	assert(waitFor([] { return volume->mounted; }));
	assert(waitFor([&] { return refreshes == 1; }));
	g_signal_emit_by_name(monitor, "volume-changed", volume);
	assert(volume->attempts == 1);
	g_signal_emit_by_name(monitor, "volume-removed", volume);
	volume->mounted = false;
	volume->fail = true;
	g_signal_emit_by_name(monitor, "volume-added", volume);
	assert(waitFor([&] { return failures == 1; }));
	assert(volume->attempts == 2);
	g_signal_emit_by_name(monitor, "volume-changed", volume);
	assert(volume->attempts == 2);
	g_signal_emit_by_name(monitor, "volume-removed", volume);
	volume->fail = false;
	g_signal_emit_by_name(monitor, "volume-added", volume);
	assert(waitFor([] { return volume->mounted; }));
	assert(volume->attempts == 3);
	return 0;
}
