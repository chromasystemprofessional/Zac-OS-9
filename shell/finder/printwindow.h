#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

/*
 * File > Page Setup… and Print Window… / Print Desktop… (Missing Manual
 * Appendix A): the front window's whole contents, or the desktop's icons,
 * drawn as on screen and laid out on pages with the window's name, the
 * date and the page number, then printed as a PDF on the chosen printer.
 */

/* Page Setup's choices, kept in desktop.conf (page-paper,
 * page-orientation, page-scale). */
struct PageSetup {
	QString paper;          /* one of pageSetupPapers() */
	bool landscape = false;
	int scale = 100;        /* percent; one of pageSetupScales() */
};
QStringList pageSetupPapers();
QList<int> pageSetupScales();
PageSetup loadPageSetup();
void savePageSetup(const PageSetup &setup);

/* Lays `content` (screen pixels, a point each at 100%) out on pages and
 * writes them to `pdf`: shrunk to the page's width if it's wider, cut
 * into as many pages as it takes down. The number of pages, 0 if the
 * file couldn't be written. */
int writeWindowPdf(const QString &title, const QImage &content, const PageSetup &setup,
	const QString &pdf);

/* Page Setup…: true if OK was chosen (and the choices saved). */
bool showPageSetup();
