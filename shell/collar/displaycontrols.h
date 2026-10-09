#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSize>
#include <QStringList>
#include <functional>

class QFileSystemWatcher;

struct CollarDisplay {
	QString name;
	QSize size;
	bool main = false, nested = false;
	QList<QSize> modes;
	enum Backend { NoBrightness, Backlight, Ddc } backend = NoBrightness;
	QString backlight;
	int maxBrightness = 0, bus = -1;
	int brightness = -1;
	QString brightnessError = "Reading display brightness...";
	bool internal() const;
};

class DisplayControls : public QObject {
public:
	explicit DisplayControls(QObject *parent = nullptr, const QString &outputsPath = defaultOutputsPath(),
		const QString &drmRoot = "/sys/class/drm", const QString &backlightRoot = "/sys/class/backlight",
		const QString &ddcutil = "ddcutil");
	static QString defaultOutputsPath();
	QList<CollarDisplay> displays;
	bool mirrored = false;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	/* Rereads the outputs and backlights; hardware=true also queries DDC/CI monitors. */
	void refresh(bool hardware = true);
	const CollarDisplay *display(const QString &name) const;
	QString label(const QString &name) const;
	bool setResolution(const QString &name, QSize size, QString *error);
	void setBrightness(const QString &name, int percent);
	bool busy(const QString &name) const { return m_busy.contains(name); }

private:
	struct Job {
		QStringList args;
		std::function<void(const QString &, const QString &)> done;
	};
	QString m_outputsPath, m_drmRoot, m_backlightRoot, m_ddcutil;
	QFileSystemWatcher *m_watcher;
	QStringList m_busy, m_ddcOutputs;
	QMap<QString, int> m_buses;
	QMap<QString, QPair<int, int>> m_ddcValues;
	QMap<QString, QString> m_ddcErrors;
	QList<Job> m_queue;
	bool m_running = false, m_detecting = false, m_detected = false, m_hardware = false;
	qint64 m_lastDdcRead = 0, m_lastDetect = 0;
	void readOutputs();
	void readBacklight(CollarDisplay &display);
	void applyDdc(CollarDisplay &display);
	void detect();
	void readDdc(const QString &name);
	void run(const QStringList &args, std::function<void(const QString &, const QString &)> done);
	void next();
	void finished(const QString &name, const QString &error);
	void notify();
};
