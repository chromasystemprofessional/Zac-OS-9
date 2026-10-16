#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

struct SoundOutput {
	QString sink, port, label;
	int volume = 0;
	int balance = 50;
	bool muted = false;
	bool current = false;
	bool stereo = false;
};

struct SoundInput {
	QString source, label;
	bool current = false;
};

bool parseSoundOutputs(const QByteArray &json, const QString &defaultSink,
	QVector<SoundOutput> &outputs, QString &error);
bool parseSoundInputs(const QByteArray &json, const QString &defaultSource,
	QVector<SoundInput> &inputs, QString &error);

class SoundClient : public QObject {
public:
	explicit SoundClient(QObject *parent = nullptr) : QObject(parent) {}
	QVector<SoundOutput> outputs;
	QVector<SoundInput> inputs;
	bool switching = false;
	bool inputSwitching = false;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;
	std::function<void(const QString &)> commandFailed;
	std::function<void()> volumeApplied;

	void refresh();
	void selectOutput(const SoundOutput &output);
	void selectInput(const SoundInput &input);
	void setVolume(const QString &sink, int percent);
	void setVolume(const SoundOutput &output, int percent);
	void setBalance(const SoundOutput &output, int percent, int balance);
	void setMuted(const QString &sink, bool muted);

private:
	using Done = std::function<void(const QByteArray &, const QString &)>;
	bool m_refreshing = false;
	bool m_inputRefreshing = false;
	unsigned m_generation = 0;
	unsigned m_inputGeneration = 0;
	unsigned m_volumeGeneration = 0;
	void run(const QStringList &args, Done done);
	void applyVolume(const SoundOutput &output, int percent, bool feedback);
	void refreshInputs();
	void report(const QString &error, bool command = false);
	void finishSwitch(const QString &error);
	void movePlayback(const QString &sink, QStringList inputs);
	void makeDefault(const QString &sink);
};
