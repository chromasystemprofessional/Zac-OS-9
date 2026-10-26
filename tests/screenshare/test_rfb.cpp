/* The Screen Sharing viewer's pure parts (shell/screenshare/rfb.cpp,
 * common.cpp): ZRLE's tile kinds decoded, bad data refused; where the
 * other screen goes on this one (one for one, or shrunk evenly, centred);
 * Bonjour's list of ZacOS computers among VNC servers. */
#include <QCoreApplication>
#include <QTextStream>
#include <vector>

#include "common.h"
#include "rfb.h"

using namespace screenshare;

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static const uint32_t A = 0xFF112233u, B = 0xFFAABBCCu;

/* A CPIXEL: 3 bytes, little-endian 0x00RRGGBB. */
static void cp(std::vector<uint8_t> &v, uint32_t px) {
	v.push_back(px & 0xFF);
	v.push_back((px >> 8) & 0xFF);
	v.push_back((px >> 16) & 0xFF);
}

static std::vector<uint32_t> decode(const std::vector<uint8_t> &data, int w, int h, bool *ok) {
	std::vector<uint32_t> out(size_t(w) * h, 0);
	*ok = zrleDecode(data.data(), data.size(), w, h, out.data(), w);
	return out;
}

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	bool ok = false;

	std::vector<uint8_t> solid{ 1 };
	cp(solid, A);
	check(decode(solid, 4, 2, &ok) == std::vector<uint32_t>(8, A) && ok, "a solid tile");

	std::vector<uint8_t> raw{ 0 };
	cp(raw, A);
	cp(raw, B);
	check(decode(raw, 2, 1, &ok) == std::vector<uint32_t>{ A, B } && ok, "a raw tile");

	/* Two colours, a bit each, rows padded to a byte, high bit first. */
	std::vector<uint8_t> packed{ 2 };
	cp(packed, A);
	cp(packed, B);
	packed.push_back(0b10100000);
	packed.push_back(0b01000000);
	check(decode(packed, 3, 2, &ok) == std::vector<uint32_t>{ B, A, B, A, B, A } && ok,
		"a packed-palette tile");

	std::vector<uint8_t> rle{ 128 };
	cp(rle, A);
	rle.push_back(1); /* run of 2 */
	cp(rle, B);
	rle.push_back(0); /* run of 1 */
	check(decode(rle, 3, 1, &ok) == std::vector<uint32_t>{ A, A, B } && ok, "a plain run-length tile");

	std::vector<uint8_t> prle{ 130 };
	cp(prle, A);
	cp(prle, B);
	prle.push_back(0x80);
	prle.push_back(1); /* A twice */
	prle.push_back(1); /* B once */
	check(decode(prle, 3, 1, &ok) == std::vector<uint32_t>{ A, A, B } && ok, "a palette run-length tile");

	std::vector<uint8_t> tiles{ 1 };
	cp(tiles, A);
	tiles.push_back(1);
	cp(tiles, B);
	std::vector<uint32_t> want(65, A);
	want[64] = B;
	check(decode(tiles, 65, 1, &ok) == want && ok, "64-pixel tiles, left to right");

	std::vector<uint8_t> runOver{ 128 };
	cp(runOver, A);
	runOver.push_back(5); /* 6 > 3 */
	decode(runOver, 3, 1, &ok);
	const bool overrun = !ok;
	decode({ 17 }, 1, 1, &ok);
	const bool unused = !ok;
	std::vector<uint8_t> shortRaw{ 0, 1, 2 };
	decode(shortRaw, 2, 1, &ok);
	check(overrun && unused && !ok, "runs past the tile, unused kinds and short data refused");

	check(fitRect(1280, 720, 1280, 720) == QRect(0, 0, 1280, 720), "the same size: one for one");
	check(fitRect(800, 600, 1280, 720) == QRect(240, 60, 800, 600), "smaller: one for one, centred");
	check(fitRect(2560, 1440, 1280, 800) == QRect(0, 40, 1280, 720),
		"bigger: shrunk evenly to fit, centred, not stretched");
	check(fitRect(1920, 1200, 1440, 900) == QRect(0, 0, 1440, 900), "16:10 on 16:10: the whole screen");

	const QString browse =
		"+;eth0;IPv4;Adam\\032Mac;_rfb._tcp;local\n"
		"=;eth0;IPv6;Adam\\032Mac;_rfb._tcp;local;adams-mac.local;fe80::1;5900;\"zacos=1\"\n"
		"=;eth0;IPv4;Adam\\032Mac;_rfb._tcp;local;adams-mac.local;192.168.1.20;5900;\"zacos=1\"\n"
		"=;eth0;IPv4;Some\\032VNC;_rfb._tcp;local;nas.local;192.168.1.30;5900;\n"
		"=;eth0;IPv6;Den;_rfb._tcp;local;den.local;fe80::2;5901;\"zacos=1\"\n"
		"=;eth0;IPv4;This\\032One;_rfb._tcp;local;me.local;192.168.1.10;5900;\"zacos=1\"\n";
	const std::vector<SharedScreen> found = parseScreenBrowse(browse, "This One");
	check(found.size() == 2 && found[0].name == "Adam Mac" && found[0].target() == "192.168.1.20" &&
		found[1].name == "Den" && found[1].target() == "den.local:5901",
		"Bonjour: ZacOS computers only, once each, by IPv4 address, not this one");

	QTextStream(stdout) << (fails ? "FAILED\n" : "all passed\n");
	return fails ? 1 : 0;
}
