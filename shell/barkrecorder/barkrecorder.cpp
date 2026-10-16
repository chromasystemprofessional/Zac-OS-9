#include "barkrecorder.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

BarkRecorder::BarkRecorder(QWidget *parent) : QMainWindow(parent) {
	setWindowTitle("Bark Recorder");
	setAttribute(Qt::WA_DeleteOnClose);
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	m_status = new QLabel("Choose Record to make a sound recording.", page);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	auto *buttons = new QHBoxLayout;
	m_record = new QPushButton("Record…", page);
	m_record->setObjectName("record");
	m_stop = new QPushButton("Stop", page);
	m_stop->setObjectName("stop");
	m_stop->setEnabled(false);
	m_play = new QPushButton("Play", page);
	m_play->setObjectName("play");
	m_play->setEnabled(false);
	m_open = new QPushButton("Open…", page);
	m_open->setObjectName("open");
	buttons->addWidget(m_record);
	buttons->addWidget(m_stop);
	buttons->addWidget(m_play);
	buttons->addWidget(m_open);
	layout->addLayout(buttons);
	setCentralWidget(page);
	resize(360, 110);

	m_recorder.setProcessChannelMode(QProcess::MergedChannels);
	connect(m_record, &QPushButton::clicked, this, [this] { chooseRecording(); });
	connect(m_stop, &QPushButton::clicked, this, &BarkRecorder::stopRecording);
	connect(m_play, &QPushButton::clicked, this, [this] {
		if (!m_recordingPath.isEmpty()) {
			playRecording(m_recordingPath);
		}
	});
	connect(m_open, &QPushButton::clicked, this, [this] { choosePlayback(); });
	connect(&m_recorder, &QProcess::started, this, [this] {
		m_status->setText("Recording to " + QFileInfo(m_recordingPath).fileName() + "…");
		m_record->setEnabled(false);
		m_stop->setEnabled(true);
		m_play->setEnabled(false);
		m_open->setEnabled(false);
	});
	connect(&m_recorder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			m_record->setEnabled(true);
			m_stop->setEnabled(false);
			m_play->setEnabled(hasRecording());
			m_open->setEnabled(true);
			reportProcessError("start recording", &m_recorder);
			if (m_closeAfterRecording) {
				close();
			}
		}
	});
	connect(&m_recorder, &QProcess::finished, this,
		[this](int exitCode, QProcess::ExitStatus exitStatus) {
			m_record->setEnabled(true);
			m_stop->setEnabled(false);
			m_play->setEnabled(hasRecording());
			m_open->setEnabled(true);
			if (exitStatus == QProcess::NormalExit && exitCode == 0 && hasRecording()) {
				m_status->setText("Recording saved to " + QFileInfo(m_recordingPath).fileName() + ".");
			} else {
				reportProcessError("finish recording", &m_recorder);
			}
			if (m_closeAfterRecording) {
				close();
			}
		});

	m_player.setProcessChannelMode(QProcess::MergedChannels);
	connect(&m_player, &QProcess::started, this, [this] {
		m_status->setText("Playing " + QFileInfo(m_recordingPath).fileName() + "…");
		m_record->setEnabled(false);
		m_play->setEnabled(false);
		m_open->setEnabled(false);
		m_stop->setEnabled(true);
	});
	connect(&m_player, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			m_record->setEnabled(true);
			m_play->setEnabled(hasRecording());
			m_open->setEnabled(true);
			m_stop->setEnabled(false);
			reportProcessError("play recording", &m_player);
			if (m_closeAfterPlayback) {
				close();
			}
		}
	});
	connect(&m_player, &QProcess::finished, this,
		[this](int exitCode, QProcess::ExitStatus exitStatus) {
			m_record->setEnabled(true);
			m_play->setEnabled(hasRecording());
			m_open->setEnabled(true);
			m_stop->setEnabled(false);
			if (m_stoppingPlayback) {
				m_stoppingPlayback = false;
				m_status->setText("Playback stopped.");
			} else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
				m_status->setText("Ready. Recording: " + QFileInfo(m_recordingPath).fileName());
			} else {
				reportProcessError("play recording", &m_player);
			}
			if (m_closeAfterPlayback) {
				close();
			}
		});
}

bool BarkRecorder::startRecording(const QString &path) {
	if (isRecording() || isPlaying() || path.isEmpty()) {
		return false;
	}
	QFileInfo destination(path);
	if (destination.suffix().compare(QLatin1String("wav"), Qt::CaseInsensitive) != 0) {
		return false;
	}
	m_recordingPath = destination.absoluteFilePath();
	m_status->setText("Starting recording…");
	m_record->setEnabled(false);
	m_open->setEnabled(false);
	m_recorder.start("pw-record", { m_recordingPath });
	return true;
}

void BarkRecorder::stopRecording() {
	if (isRecording()) {
		m_recorder.terminate();
	} else if (isPlaying()) {
		m_stoppingPlayback = true;
		m_player.terminate();
	}
}

bool BarkRecorder::playRecording(const QString &path) {
	if (isRecording() || isPlaying() || path.isEmpty()) {
		return false;
	}
	const QFileInfo source(path);
	if (!source.isFile() || source.size() <= 0 ||
			source.suffix().compare(QLatin1String("wav"), Qt::CaseInsensitive) != 0) {
		return false;
	}
	m_recordingPath = source.absoluteFilePath();
	m_status->setText("Starting playback…");
	m_record->setEnabled(false);
	m_play->setEnabled(false);
	m_open->setEnabled(false);
	m_player.start("pw-play", { m_recordingPath });
	return true;
}

bool BarkRecorder::isRecording() const {
	return m_recorder.state() != QProcess::NotRunning;
}

bool BarkRecorder::isPlaying() const {
	return m_player.state() != QProcess::NotRunning;
}

bool BarkRecorder::hasRecording() const {
	const QFileInfo recording(m_recordingPath);
	return recording.isFile() && recording.size() > 0 &&
		recording.suffix().compare(QLatin1String("wav"), Qt::CaseInsensitive) == 0;
}

void BarkRecorder::chooseRecording() {
	const QString path = QFileDialog::getSaveFileName(this, "Record a Sound",
		QDir::homePath() + "/Recording.wav", "WAVE audio (*.wav)");
	if (!path.isEmpty() && !startRecording(path)) {
		QMessageBox::warning(this, "Bark Recorder",
			"Choose a .wav file, and stop playback before recording.");
	}
}

void BarkRecorder::choosePlayback() {
	const QString path = QFileDialog::getOpenFileName(this, "Open a Sound Recording",
		m_recordingPath.isEmpty() ? QDir::homePath() : QFileInfo(m_recordingPath).absolutePath(),
		"WAVE audio (*.wav)");
	if (!path.isEmpty() && !playRecording(path)) {
		QMessageBox::warning(this, "Bark Recorder",
			"Choose a readable .wav file, and stop recording before playback.");
	}
}

void BarkRecorder::reportProcessError(const QString &action, QProcess *process) {
	const QString details = process->readAll().trimmed();
	const QString reason = process->error() == QProcess::UnknownError
		? QString("The audio command exited with status %1.").arg(process->exitCode())
		: process->errorString();
	QMessageBox::critical(this, "Bark Recorder", "Could not " + action + ". " + reason +
		(details.isEmpty() ? QString() : "\n" + details));
}

void BarkRecorder::closeEvent(QCloseEvent *event) {
	if (isRecording()) {
		const auto answer = QMessageBox::question(this, "Bark Recorder",
			"Stop the recording and close Bark Recorder?",
			QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Yes) {
			event->ignore();
			return;
		}
		m_closeAfterRecording = true;
		stopRecording();
		event->ignore();
		return;
	}
	if (isPlaying()) {
		m_closeAfterPlayback = true;
		stopRecording();
		event->ignore();
		return;
	}
	event->accept();
}
