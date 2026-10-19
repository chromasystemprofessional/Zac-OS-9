#pragma once

/*
 * Memory (Mac OS 9's control panel, Missing Manual ch. 7): the disk cache,
 * virtual memory and a RAM Disk, as Linux has them.
 *
 *   Disk Cache      what the system keeps of recently used files, which it
 *                   sizes by itself: shown, not set.
 *   Virtual Memory  on or off and how much: /swapfile, on the startup disk.
 *                   Its checkbox turns on the out-of-memory guard (earlyoom),
 *                   which quits the program using the most memory before the
 *                   computer runs out. Changes apply at once, not at restart.
 *   RAM Disk        a disk made of memory, on the desktop; saved at shut
 *                   down and put back at startup if asked.
 *
 * zacos9-memory-helper reads all this (without a password) and changes it
 * (as root, through pkexec); ZACOS9_MEMORY_HELPER points at another one,
 * run without pkexec, for tests.
 */

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>

struct MemoryStatus {
	qint64 total = 0, available = 0, cache = 0;
	qint64 swapSize = 0, swapUsed = 0, swapRoom = 0;
	bool swapOn = false;
	QStringList otherSwap; /* "/dev/zram0, 4 GB", left alone */
	bool guardInstalled = false, guardOn = false;
	bool ramDiskOn = false, ramDiskSave = false;
	qint64 ramDiskSize = 0, ramDiskUsed = 0;
	bool valid = false;
};

/* The helper's "status" output. */
MemoryStatus parseMemoryStatus(const QString &output);
/* "16 GB", "512 MB", "1.5 GB". */
QString memorySizeText(qint64 bytes);
/* The sizes the sliders offer, in megabytes: virtual memory up to what
 * the disk has room for (keeping 2 GB free), a RAM Disk up to half of
 * memory; each with the current size among them. */
QList<int> memorySwapSteps(qint64 roomBytes, qint64 currentBytes);
QList<int> memoryRamDiskSteps(qint64 totalBytes, qint64 currentBytes);

class MemoryPanel : public QWidget {
public:
	MemoryPanel();
	~MemoryPanel() override;

	/* As the controls do, for tests. */
	const MemoryStatus &status() const;
	QString problem() const;
	void setVirtualMemory(bool on);
	void setVirtualMemorySize(int megabytes);
	void setGuard(bool on);
	void setRamDisk(bool on);
	void setRamDiskSize(int megabytes);
	void setRamDiskSave(bool on);
	void useDefaults();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	struct Parts;
	std::unique_ptr<Parts> d;
	void reload();
	bool apply(const QStringList &args);
};
