#pragma once

/*
 * Printer Picker (Mac OS 9's Chooser): the printers CUPS knows and has
 * found on the network. Clicking one makes it your printer, the one
 * File > Print uses; Create Desktop Printer puts it on the desktop; Print
 * Queue opens its window; Printer Settings adds and sets up printers
 * (system-config-printer, if installed).
 */

#include <QWidget>
#include <memory>

#include "printers.h"

class PickerWindow : public QWidget {
public:
	PickerWindow();
	~PickerWindow() override;

	/* For tests. */
	void reload();
	QStringList rows() const;
	int selectedRow() const;
	QString details() const;
	void pick(int row);
	QString createDesktopPrinterForSelected(); /* its path */

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	struct Parts;
	std::unique_ptr<Parts> d;
	void choose(int row);
	void openQueue();
	void updateButtons();
};

/* The folder desktop printers go in: the Desktop (ZACOS9_DESKTOP_DIR in tests). */
QString desktopFolder();
