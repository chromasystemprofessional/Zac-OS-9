#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QString>

class QLabel;
class QPushButton;
class QCloseEvent;

class BarkRecorder final : public QMainWindow {
public:
	explicit BarkRecorder(QWidget *parent = nullptr);

	bool startRecording(const QString &path);
	void stopRecording();
	bool playRecording(const QString &path);
	bool isRecording() const;
	bool isPlaying() const;

private:
	void chooseRecording();
	void choosePlayback();
	void reportProcessError(const QString &action, QProcess *process);
	bool hasRecording() const;
	void closeEvent(QCloseEvent *event) override;

	QLabel *m_status = nullptr;
	QPushButton *m_record = nullptr;
	QPushButton *m_stop = nullptr;
	QPushButton *m_play = nullptr;
	QPushButton *m_open = nullptr;
	QProcess m_recorder;
	QProcess m_player;
	QString m_recordingPath;
	bool m_closeAfterRecording = false;
	bool m_closeAfterPlayback = false;
	bool m_stoppingPlayback = false;
};
