/*
 * Page Setup's choices, kept and read back, and a window's picture laid
 * out on PDF pages: across, down, shrunk to fit, landscape.
 */
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "printwindow.h"

static int failures;
static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
	failures += !ok;
}

/* The page count Qt's PDF writer records in the page tree. */
static int pdfPages(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return -1;
	}
	const QByteArray pdf = f.readAll();
	if (!pdf.startsWith("%PDF-")) {
		return -1;
	}
	const int at = pdf.indexOf("/Count ");
	return at < 0 ? -1 : pdf.mid(at + 7, 6).split('\n').first().split(' ').first()
		.split('/').first().toInt();
}

int main(int argc, char **argv) {
	QTemporaryDir config;
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	QApplication app(argc, argv);

	PageSetup setup = loadPageSetup();
	check(pageSetupPapers().contains(setup.paper) && !setup.landscape && setup.scale == 100,
		"Page Setup starts with the local paper, portrait, 100%");
	setup.paper = "US Legal";
	setup.landscape = true;
	setup.scale = 75;
	savePageSetup(setup);
	const PageSetup again = loadPageSetup();
	check(again.paper == "US Legal" && again.landscape && again.scale == 75,
		"and keeps what's chosen");
	setup = PageSetup{ "US Letter", false, 100 };
	savePageSetup(setup);
	check(loadPageSetup().scale == 100 && !loadPageSetup().landscape, "back to the defaults");

	QTemporaryDir out;
	/* US Letter: 612 x 792 points; half-inch margins, a header and a footer
	 * leave 540 across and 680 down. */
	QImage tall(200, 1500, QImage::Format_RGB32);
	tall.fill(Qt::white);
	for (int y = 0; y < tall.height(); y += 40) {
		for (int x = 0; x < tall.width(); x++) {
			tall.setPixel(x, y, 0);
		}
	}
	const QString one = out.path() + "/tall.pdf";
	check(writeWindowPdf("Documents", tall, setup, one) == 3 && pdfPages(one) == 3,
		"a tall window goes on as many pages as it takes down");
	setup.scale = 50;
	check(writeWindowPdf("Documents", tall, setup, out.path() + "/half.pdf") == 2,
		"at 50% it takes fewer");
	setup.scale = 100;
	QImage wide(1080, 1360, QImage::Format_RGB32);
	wide.fill(Qt::white);
	check(writeWindowPdf("Wide", wide, setup, out.path() + "/wide.pdf") == 1,
		"a wide window is shrunk to the page's width (and so its height)");
	const QImage shorter = tall.copy(0, 0, 200, 1100);
	check(writeWindowPdf("Documents", shorter, setup, out.path() + "/portrait.pdf") == 2,
		"portrait: two pages");
	setup.landscape = true;
	check(writeWindowPdf("Documents", shorter, setup, out.path() + "/landscape.pdf") == 3,
		"landscape: shorter pages, more of them");
	check(writeWindowPdf("Nothing", QImage(), setup, out.path() + "/none.pdf") == 0,
		"nothing to print, no pages");
	check(writeWindowPdf("Nowhere", tall, setup, out.path() + "/missing/x.pdf") == 0,
		"a PDF that can't be written says so");
	return failures ? 1 : 0;
}
