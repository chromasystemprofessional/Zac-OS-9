/* Walkies' locations (location.cpp): saving, capturing, switching. */
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

#include "location.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
	QTemporaryDir dir;
	qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
	QCoreApplication app(argc, argv);
	Location home;
	parseCapture({ "lo:loopback\nHome Wi\\:Fi:802-11-wireless\nWired:802-3-ethernet\n",
		"alsa_output.pci.analog-stereo\n",
		"Volume: front-left: 45875 /  70% / -9.29 dB,   front-right: 45875 /  70% / -9.29 dB\n",
		"Europe/London\n", "system default destination: Brother_MFC\n" }, &home);
	CHECK(home.network == "Home Wi:Fi" && home.sink == "alsa_output.pci.analog-stereo" && home.volume == 70);
	CHECK(home.timezone == "Europe/London" && home.printer == "Brother_MFC");
	home.name = "Home";
	home.trickSet = "Base";
	CHECK(saveLocation(home));
	Location slash;
	slash.name = "a/b";
	CHECK(!saveLocation(slash));
	Location read;
	CHECK(locationNames() == QStringList({ "Home" }) && readLocation("Home", &read));
	CHECK(read.network == home.network && read.volume == 70 && read.trickSet == "Base");
	const QList<WalkiesStep> steps = applySteps(read, { { "syncthing.service", false } });
	CHECK(steps.size() == 6);
	CHECK(steps[0].args == QStringList({ "connection", "up", "id", "Home Wi:Fi" }));
	CHECK(steps[2].args.last() == "70%" && steps[3].args.last() == "Europe/London");
	CHECK(steps[5].args == QStringList({ "--user", "disable", "syncthing.service" }));
	CHECK(describe(read).contains("Printer: Brother MFC"));
	setCurrentLocation("Home");
	CHECK(currentLocation() == "Home");
	CHECK(deleteLocation("Home") && locationNames().isEmpty());
	std::puts("ok: Walkies remembers and switches locations");
	return 0;
}
