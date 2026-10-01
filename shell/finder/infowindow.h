#pragma once

#include <QDateTime>
#include <QRect>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "pixels.h"

/*
 * "<name> Info": what Get Info shows for a file, folder or disk. Folder
 * sizes are added up in the background, as the Finder did.
 * TODO: Mac OS 8's Get Info layout isn't in the HIG; positions are estimates.
 */
class InfoWindow : public QWidget {
public:
	/* One Info window per item; brings back an open one. */
	static void open(const QString &path, pl_icon_kind kind, const QString &name);
	~InfoWindow() override;

protected:
	void paintEvent(QPaintEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mousePressEvent(QMouseEvent *) override;

private:
	InfoWindow(const QString &path, pl_icon_kind kind, const QString &name);
	void saveComment();

	/* Comments live in the file's "user.xdg.comment" extended attribute
	 * (the freedesktop convention), at most 200 characters like the Mac's. */
	QString m_comment;
	bool m_editingComment = false;
	bool m_caretOn = true;
	QRect m_commentBox;
	QTimer m_caretTimer;

	QString m_path, m_name;
	pl_icon_kind m_kind;
	QString m_kindText, m_where, m_created, m_modified, m_size;
	QThread *m_sizer = nullptr;
};

/* About This Computer: the logo, version, system and memory. */
class AboutWindow : public QWidget {
public:
	static void open();

protected:
	void paintEvent(QPaintEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	AboutWindow();
	QStringList m_lines;
};

/* Mac-style time stamp: "Thu, Oct 1, 2026, 9:41 AM". */
QString finderDate(const QDateTime &t);
/* "Hard Disk:home:root:" as the Mac wrote paths. */
QString macPath(const QString &dir);
/* "12K", "1.4 MB", "2.3 GB" */
QString finderSize(qint64 bytes);
