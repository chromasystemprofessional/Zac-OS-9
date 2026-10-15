#pragma once

/*
 * Startup Disk (Mac OS 9's control panel, Missing Manual p. 268): the
 * systems this computer can start up with, as GRUB lists them (ZacOS, other
 * systems os-prober found, the firmware's settings), the one it starts with
 * selected. Choosing another makes it the startup system from then on;
 * Restart starts it now. zacos9-startupdisk-helper (as root, through
 * pkexec) reads and sets GRUB's default; ZACOS9_STARTUPDISK_HELPER points
 * at another one, for tests.
 *
 * Below the list, the disk Old Dog (Classic) starts up with: of the disks
 * zacos9-classic attaches, those with a System Folder. The choice goes to
 * ~/.config/zacos9/classic.conf, which the launcher reads; ZACOS9_CLASSIC
 * points at another launcher, for tests.
 */

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>

struct StartupEntry {
	QString id, title, kind; /* kind: linux, windows, mac, firmware, other */
	int depth = 0;           /* 1 inside a submenu such as Advanced options */
};

/* The helper's "list" output: the entries, and the current one's id. */
QList<StartupEntry> parseStartupEntries(const QString &output, QString *current);
/* An entry's icon (a pl_icon_kind). */
int startupEntryIcon(const StartupEntry &entry);

/* A Classic disk image: its volume's name and whether it has a System
 * Folder (a blessed folder on its HFS or HFS+ volume, bare or in an Apple
 * partition map). */
struct ClassicDisk {
	QString path, name;
	bool cd = false, system = false;
};
ClassicDisk classicDisk(const QString &path);
/* zacos9-classic --check's output: the disks, and the chosen one. */
QStringList parseClassicCheck(const QString &output, QString *startup);
/* Where the choice is kept, and keeping it there. */
QString classicConfPath();
bool setClassicStartupDisk(const QString &path);

class StartupDiskPanel : public QWidget {
public:
	StartupDiskPanel();
	~StartupDiskPanel() override;

	/* What the list shows, for tests. */
	QStringList rows() const;
	int selectedRow() const;
	QString current() const;
	void setShowEveryVersion(bool on);
	/* As a click on a row. */
	void pick(int row);
	QStringList classicRows() const;
	int classicSelected() const;
	QString classicProblem() const;
	void pickClassic(int row);

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
	void reload();
	void fill();
	void choose(int row);
	void restart();
	void lookForOthers();
	void reloadClassic();
	void chooseClassic(int row);
};
