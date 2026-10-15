#pragma once

/*
 * A printer's queue (Mac OS 9's desktop printer window and PrintMonitor):
 * the printer and how it is, the documents waiting, Hold/Resume and
 * Remove for the one selected, Stop Printing/Start Printing for the
 * printer. Documents dropped on the window are printed there. It looks
 * again every two seconds.
 */

#include <QWidget>
#include <memory>

#include "printers.h"

class QueueWindow : public QWidget {
public:
	explicit QueueWindow(const QString &printer);
	~QueueWindow() override;

	/* For tests. */
	void refresh();
	QStringList rows() const;
	QString printerStatus() const;
	QString jobDetails() const;
	void selectJob(int row);
	void holdOrResume();
	void removeJob();
	void stopOrStart();
	void printDropped(const QStringList &files);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dropEvent(QDropEvent *) override;

private:
	struct Parts;
	std::unique_ptr<Parts> d;
	void updateButtons();
	void problem(const QString &what, const QString &error);
};
