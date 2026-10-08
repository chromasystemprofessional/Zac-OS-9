#pragma once

#include <QDialog>
#include <QWidget>
#include <vector>

#include "fileassoc.h"
#include "panelkit.h"

/* "Open documents of this kind with": the applications that can. */
class OpenWithDialog : public QDialog {
public:
	OpenWithDialog(const QString &kind, const QStringList &appIds, const QString &current);
	QString chosen() const;

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	QString m_kind;
	QStringList m_ids;
	PanelList m_list;
	PanelButton m_ok, m_cancel;
};

/*
 * The File Exchange control panel (named for the Mac OS 9 panel that
 * mapped PC file extensions to Mac applications): every kind of document
 * an installed application can open, the application that opens it now,
 * and Change… to pick another. The choice is the freedesktop default
 * (fileassoc.h), so the Finder's document icons and Open follow it.
 */
class FileExchangePanel : public QWidget {
public:
	FileExchangePanel();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	void reload();
	void showDetails();
	void change();

	std::vector<FileType> m_types;
	PanelList m_list;
	PanelButton m_change;
	QString m_appId, m_appName;
	std::vector<uint32_t> m_appIcon;
};
