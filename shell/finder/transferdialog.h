#pragma once

#include <QDialog>
#include <atomic>
#include "pixels.h"
#include "widgets.h"

class TransferDialog : public QDialog {
public:
	explicit TransferDialog(std::atomic_bool &stopped);
	void setProgress(const QString &operation, const QString &file, const QString &destination,
		quint64 completed, quint64 total, bool preparing);
	void reject() override;
	quint64 completed() const { return m_completed; }
	quint64 total() const { return m_total; }
	QString currentFile() const { return m_file; }

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;
	void closeEvent(QCloseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	std::atomic_bool &m_stopped;
	QString m_operation, m_file, m_destination;
	quint64 m_completed = 0, m_total = 0;
	bool m_preparing = true, m_tracking = false, m_inside = false;
	QRect m_stop{340, 135, 70, PL_BUTTON_H};
};
