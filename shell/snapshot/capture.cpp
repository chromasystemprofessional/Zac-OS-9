#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTransform>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

#include "alert.h"
#include "platinumshell.h"
#include "zacos-snapshot-v1-client-protocol.h"

namespace {
constexpr quint64 MAX_PIXELS = 32u * 1024u * 1024u;
struct Capture {
	wl_display *display = nullptr;
	zacos_snapshot_manager_v1 *manager = nullptr;
	std::vector<wl_output *> outputs;
	QString error;
	QImage image;
	QRect box;
	bool finished = false;
};

void global(void *data, wl_registry *registry, uint32_t name,
		const char *interface, uint32_t) {
	auto &capture = *static_cast<Capture *>(data);
	if (strcmp(interface, zacos_snapshot_manager_v1_interface.name) == 0) {
		capture.manager = static_cast<zacos_snapshot_manager_v1 *>(wl_registry_bind(
			registry, name, &zacos_snapshot_manager_v1_interface, 1));
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (capture.outputs.size() >= 16) {
			capture.error = "Too many displays to capture (maximum 16).";
			return;
		}
		capture.outputs.push_back(static_cast<wl_output *>(wl_registry_bind(
			registry, name, &wl_output_interface, 1)));
	}
}
void removed(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener{ global, removed };

bool dispatchUntil(Capture &capture, const bool &done) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!done) {
		while (wl_display_prepare_read(capture.display) != 0) {
			if (wl_display_dispatch_pending(capture.display) < 0) {
				capture.error = "The compositor disconnected during screenshot capture.";
				return false;
			}
			if (done) {
				return true;
			}
		}
		const int flushed = wl_display_flush(capture.display);
		if (flushed < 0 && errno != EAGAIN) {
			wl_display_cancel_read(capture.display);
			capture.error = "Could not send the screenshot request.";
			return false;
		}
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		pollfd fd{ wl_display_get_fd(capture.display),
			static_cast<short>(POLLIN | (flushed < 0 ? POLLOUT : 0)), 0 };
		const int ready = remaining > 0 ? poll(&fd, 1, static_cast<int>(remaining)) : 0;
		if (ready > 0 && (fd.revents & POLLIN)) {
			if (wl_display_read_events(capture.display) < 0 ||
					wl_display_dispatch_pending(capture.display) < 0) {
				capture.error = "The compositor disconnected during screenshot capture.";
				return false;
			}
		} else {
			wl_display_cancel_read(capture.display);
			if (ready < 0 && errno == EINTR) {
				continue;
			}
			if (ready > 0 && (fd.revents & POLLOUT) &&
					!(fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
				continue;
			}
			capture.error = ready == 0 ? "Screenshot capture timed out." :
				"The compositor connection failed during screenshot capture.";
			return false;
		}
	}
	return true;
}

bool synchronize(Capture &capture) {
	bool done = false;
	wl_callback *callback = wl_display_sync(capture.display);
	static const wl_callback_listener listener{
		[](void *data, wl_callback *, uint32_t) { *static_cast<bool *>(data) = true; }
	};
	wl_callback_add_listener(callback, &listener, &done);
	const bool result = dispatchUntil(capture, done);
	wl_callback_destroy(callback);
	return result;
}

void image(void *data, zacos_snapshot_frame_v1 *, int32_t fd,
		uint32_t width, uint32_t height, uint32_t stride, uint32_t transform,
		int32_t x, int32_t y, uint32_t logicalWidth, uint32_t logicalHeight) {
	auto &capture = *static_cast<Capture *>(data);
	capture.finished = true;
	const quint64 size = static_cast<quint64>(stride) * height;
	struct stat status;
	const int seals = fcntl(fd, F_GET_SEALS);
	if (!width || !height || static_cast<quint64>(width) * height > MAX_PIXELS ||
			stride != static_cast<quint64>(width) * 4 || transform > 7 ||
			!logicalWidth || !logicalHeight || logicalWidth > width + height ||
			logicalHeight > width + height ||
			qAbs(static_cast<qint64>(x)) + logicalWidth > 200000 ||
			qAbs(static_cast<qint64>(y)) + logicalHeight > 200000 ||
			seals < 0 || (seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) !=
				(F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) || fstat(fd, &status) < 0 ||
			!S_ISREG(status.st_mode) || status.st_size != static_cast<off_t>(size)) {
		capture.error = "The compositor returned invalid screenshot dimensions.";
		close(fd);
		return;
	}
	void *pixels = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (pixels == MAP_FAILED) {
		capture.error = "Could not read the screenshot image.";
		return;
	}
	capture.image = QImage(static_cast<const uchar *>(pixels), width, height,
		stride, QImage::Format_ARGB32).copy();
	munmap(pixels, size);
	capture.image = capture.image.transformed(QTransform().rotate(
		-90 * static_cast<int>(transform & 3)));
	if (transform & 4) {
		capture.image = capture.image.mirrored(true, false);
	}
	capture.box = QRect(x, y, logicalWidth, logicalHeight);
	if (capture.image.isNull()) {
		capture.error = "Could not allocate the screenshot image.";
	}
}
void failed(void *data, zacos_snapshot_frame_v1 *, const char *message) {
	auto &capture = *static_cast<Capture *>(data);
	capture.error = QString::fromUtf8(message);
	capture.finished = true;
}
const zacos_snapshot_frame_v1_listener frameListener{ image, failed };

bool captureImage(const QString &geometry, const QString &filename, QString &error) {
	QRect region;
	if (!geometry.isEmpty()) {
		static const QRegularExpression pattern("\\A(-?[0-9]+),(-?[0-9]+) ([0-9]+)x([0-9]+)\\z");
		const auto match = pattern.match(geometry);
		bool valid[4]{};
		int numbers[4]{};
		for (int i = 0; i < 4; ++i) {
			numbers[i] = match.captured(i + 1).toInt(&valid[i]);
		}
		if (!match.hasMatch() || !std::all_of(valid, valid + 4, [](bool v) { return v; }) ||
				numbers[2] <= 0 || numbers[3] <= 0 ||
				qAbs(static_cast<qint64>(numbers[0])) + numbers[2] > 100000 ||
				qAbs(static_cast<qint64>(numbers[1])) + numbers[3] > 100000) {
			error = "Invalid screenshot selection geometry.";
			return false;
		}
		region = QRect(numbers[0], numbers[1], numbers[2], numbers[3]);
	}
	Capture capture;
	capture.display = wl_display_connect(nullptr);
	if (!capture.display) {
		error = "Could not connect to the ZacOS compositor.";
		return false;
	}
	wl_registry *registry = wl_display_get_registry(capture.display);
	wl_registry_add_listener(registry, &registryListener, &capture);
	synchronize(capture);
	if (capture.error.isEmpty() && (!capture.manager || capture.outputs.empty())) {
		capture.error = "Native Screen Snapshot needs the updated ZacOS compositor. "
			"Restart after installing the update. No legacy screencopy was attempted.";
	}
	std::vector<std::pair<QImage, QRect>> images;
	QRect bounds;
	quint64 pixels = 0;
	double scale = 1;
	if (capture.error.isEmpty()) {
		for (wl_output *output : capture.outputs) {
			capture.finished = false;
			capture.image = QImage();
			auto *frame = zacos_snapshot_manager_v1_capture(capture.manager, output);
			zacos_snapshot_frame_v1_add_listener(frame, &frameListener, &capture);
			dispatchUntil(capture, capture.finished);
			zacos_snapshot_frame_v1_destroy(frame);
			if (!capture.error.isEmpty()) {
				break;
			}
			pixels += static_cast<quint64>(capture.image.width()) * capture.image.height();
			if (pixels > MAX_PIXELS) {
				capture.error = "Combined displays exceed the 32-megapixel screenshot limit.";
				break;
			}
			if (region.isNull() || region.intersects(capture.box)) {
				bounds = bounds.united(capture.box);
				scale = std::max(scale, static_cast<double>(capture.image.width()) / capture.box.width());
				images.emplace_back(capture.image, capture.box);
			}
		}
	}
	for (auto *output : capture.outputs) {
		wl_output_destroy(output);
	}
	if (capture.manager) {
		zacos_snapshot_manager_v1_destroy(capture.manager);
	}
	wl_registry_destroy(registry);
	wl_display_disconnect(capture.display);
	if (!capture.error.isEmpty()) {
		error = capture.error;
		return false;
	}
	if (region.isNull()) {
		region = bounds;
	}
	if (!bounds.contains(region) || region.isEmpty()) {
		error = "The selected rectangle is outside the available displays.";
		return false;
	}
	const int width = qRound(region.width() * scale), height = qRound(region.height() * scale);
	if (static_cast<quint64>(width) * height > MAX_PIXELS) {
		error = "The selected screenshot exceeds the 32-megapixel limit.";
		return false;
	}
	QImage result(width, height, QImage::Format_ARGB32);
	if (result.isNull()) {
		error = "Could not allocate the combined screenshot.";
		return false;
	}
	result.fill(Qt::black);
	QPainter painter(&result);
	for (const auto &[img, box] : images) {
		painter.drawImage(QRectF((box.x() - region.x()) * scale, (box.y() - region.y()) * scale,
			box.width() * scale, box.height() * scale), img);
	}
	painter.end();
	QSaveFile file(filename);
	if (!file.open(QIODevice::WriteOnly) || !result.save(&file, "PNG") || !file.commit()) {
		error = "Could not save the screenshot: " + file.errorString();
		return false;
	}
	return true;
}
}

int main(int argc, char **argv) {
	QApplication application(argc, argv);
	platinumShellInit();
	QStringList args = application.arguments().mid(1);
	const bool cli = args.removeAll("--cli") != 0;
	QString geometry, error;
	if (args.size() == 3 && args[0] == "--geometry") {
		geometry = args[1];
		args = { args[2] };
	}
	if (args.size() != 1) {
		error = "Usage: zacos9-capture [--cli] [--geometry 'X,Y WxH'] FILE.png";
	} else if (captureImage(geometry, args[0], error)) {
		return 0;
	}
	fprintf(stderr, "zacos9-capture: %s\n", error.toUtf8().constData());
	if (!cli) {
		Alert::ask(error, "OK", QString());
	}
	return 1;
}
