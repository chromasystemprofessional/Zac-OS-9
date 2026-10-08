#pragma once

#include <QDateTime>
#include <QRect>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "panelkit.h"
#include "pixels.h"
#include "resources.h"

/*
 * "<name> Info": what Get Info shows for a file, folder or disk, chosen
 * with the Show pop-up as in Mac OS 8.5 and later.
 *   General Information  kind, size, where, dates, comments. Folder sizes
 *                        are added up in the background, as the Finder did.
 *   Sharing (folders)    "Share this item and its contents", and the
 *                        Owner, User/Group and Everyone privileges: the
 *                        folder's Unix owner, group and permissions, which
 *                        is what Netatalk and Samba go by. Changes are made
 *                        when the window closes, through
 *                        zacos9-sharing-helper; Copy works at once.
 * The window and the Sharing view are measured from Mac OS 9's Get Info
 * (running in Classic); General Information's rows are estimates.
 */
class InfoWindow : public QWidget {
public:
	enum View { General, Sharing };
	/* One Info window per item; brings back an open one, showing `view`
	 * (if the item has it). */
	static void open(const QString &path, pl_icon_kind kind, const QString &name, int view = General);
	static void openVirtual(const QString &path, pl_icon_kind kind, const QString &name,
		const ResourceDetails &details);
	~InfoWindow() override;

protected:
	void paintEvent(QPaintEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	InfoWindow(const QString &path, pl_icon_kind kind, const QString &name,
		const ResourceDetails *details = nullptr);
	void saveComment();
	bool showView(int view);
	void paintHeader(pl_canvas *c);
	void paintGeneral(pl_canvas *c);
	void paintSharing(pl_canvas *c);

	PanelHost m_host{ this };
	PanelPopup m_show;
	int m_view = 0; /* 0 General Information, 1 Sharing */

	/* Sharing: as found, and as chosen. */
	void loadSharing();
	void layoutSharing();
	void privilegeChosen(int who, int choice);
	bool applyPrivileges(bool all);
	bool saveSharing();
	QString sharingNote() const;
	bool m_mine = false;     /* we own the folder, so we may change it */
	QString m_sharedAs;      /* its name on the network, if shared */
	QString m_enclosingShare;/* the shared folder it is inside */
	QString m_sharedInside;  /* a shared folder inside it */
	bool m_share = false;
	QString m_owner, m_group, m_origOwner, m_origGroup;
	int m_mode = 0, m_origMode = 0; /* rwx bits for owner, group, others */
	PanelCheckbox m_shareBox;
	PanelPopup m_ownerPopup, m_groupPopup, m_privilege[3];
	PanelButton m_copy;

	/* Comments live in the file's "user.xdg.comment" extended attribute
	 * (the freedesktop convention), at most 200 characters like the Mac's. */
	QString m_comment;
	bool m_editingComment = false;
	bool m_caretOn = true;
	QRect m_commentBox;
	QTimer m_caretTimer;

	QString m_path, m_name;
	pl_icon_kind m_kind;
	QString m_kindText, m_where, m_created, m_modified, m_size, m_original;
	ResourceDetails m_resourceDetails;
	bool m_isResourceInfo = false;
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
/* "Zacintosh HD:home:root:" as the Mac wrote paths. */
QString macPath(const QString &dir);
/* "12K", "1.4 MB", "2.3 GB" */
QString finderSize(qint64 bytes);
