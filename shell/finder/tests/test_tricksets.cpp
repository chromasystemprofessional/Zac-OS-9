/* Tricks Manager's sets (tricksets.cpp). */
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

#include "tricksets.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
	QTemporaryDir dir;
	qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
	QCoreApplication app(argc, argv);
	CHECK(trickSetNames().isEmpty());
	TrickSet base;
	base.loginItems = { { "notes.desktop", true }, { "chat.desktop", false } };
	base.services = { { "syncthing.service", false } };
	CHECK(saveTrickSet("Base", base));
	CHECK(saveTrickSet("All On", TrickSet()));
	CHECK(!saveTrickSet("a/b", base));
	CHECK(trickSetNames() == QStringList({ "All On", "Base" }));
	TrickSet read;
	CHECK(readTrickSet("Base", &read));
	CHECK(read.loginItems == base.loginItems && read.services == base.services);
	CHECK(!readTrickSet("Missing", &read));
	setCurrentTrickSet("Base");
	CHECK(currentTrickSet() == "Base");
	CHECK(deleteTrickSet("Base") && trickSetNames() == QStringList({ "All On" }));
	std::puts("ok: Tricks Manager's sets are saved, read and deleted");
	return 0;
}
