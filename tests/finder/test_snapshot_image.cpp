#include <cassert>
#include <climits>
#include <sys/mman.h>

#define main capture_main
#include "../../shell/snapshot/capture.cpp"
#undef main

static int imageFd(bool seal = true) {
	int fd = memfd_create("snapshot-image-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	assert(fd >= 0);
	uint32_t pixels[6];
	for (uint32_t i = 0; i < 6; i++) {
		pixels[i] = 0xff000001 + i;
	}
	assert(write(fd, pixels, sizeof(pixels)) == sizeof(pixels));
	if (seal) {
		assert(fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) == 0);
	}
	return fd;
}

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	const int expected[8][6] = {
		{0, 1, 2, 3, 4, 5},
		{2, 5, 1, 4, 0, 3},
		{5, 4, 3, 2, 1, 0},
		{3, 0, 4, 1, 5, 2},
		{2, 1, 0, 5, 4, 3},
		{5, 2, 4, 1, 3, 0},
		{3, 4, 5, 0, 1, 2},
		{0, 3, 1, 4, 2, 5},
	};
	for (uint32_t transform = 0; transform < 8; transform++) {
		Capture capture;
		int fd = imageFd();
		const bool rotated = transform & 1;
		image(&capture, nullptr, fd, 3, 2, 12, transform, -20, 10,
			rotated ? 2 : 3, rotated ? 3 : 2);
		assert(capture.finished && capture.error.isEmpty());
		assert(capture.image.size() == QSize(rotated ? 2 : 3, rotated ? 3 : 2));
		assert(capture.box.topLeft() == QPoint(-20, 10));
		for (int y = 0, i = 0; y < capture.image.height(); y++) {
			for (int x = 0; x < capture.image.width(); x++, i++) {
				assert(capture.image.pixel(x, y) == 0xff000001u + expected[transform][i]);
			}
		}
		assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
	}
	for (int reason = 0; reason < 5; reason++) {
		Capture capture;
		int fd = imageFd(reason != 0);
		image(&capture, nullptr, fd, reason == 1 ? 0 : 3, 2,
			reason == 2 ? 8 : 12, reason == 3 ? 8 : 0,
			reason == 4 ? INT_MAX : 0, 0, 3, 2);
		assert(capture.finished && !capture.error.isEmpty() && capture.image.isNull());
		assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
	}
	QString error;
	assert(!captureImage("invalid", "unused.png", error));
	assert(error.contains("geometry"));
	return 0;
}
