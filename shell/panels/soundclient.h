#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

struct SoundOutput {
	QString sink, port, label;
	int volume = 0;
	bool muted = false;
	bool current = false;
};

bool parseSoundOutputs(const QByteArray &json, const QString &defaultSink,
	QVector<SoundOutput> &outputs, QString &error);

class SoundClient : public QObject {
public:
	explicit SoundClient(QObject *parent = nullptr) : QObject(parent) {}
	QVector<SoundOutput> outputs;
	bool switching = false;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;
	std::function<void()> volumeApplied;

	void refresh();
	void selectOutput(const SoundOutput &output);
	void setVolume(const QString &sink, int percent);
	void setMuted(const QString &sink, bool muted);

private:
	using Done = std::function<void(const QByteArray &, const QString &)>;
	bool m_refreshing = false;
	unsigned m_generation = 0;
	unsigned m_volumeGeneration = 0;
	void run(const QStringList &args, Done done);
	void report(const QString &error);
	void finishSwitch(const QString &error);
	void movePlayback(const QString &sink, QStringList inputs);
	void makeDefault(const QString &sink);
};
