#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <functional>

#include "barkrecorder.h"

static void check(bool condition, const char *message) {
	if (!condition) {
		qFatal("%s", message);
	}
}

static bool waitFor(const std::function<bool()> &condition, int milliseconds = 5000) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (!condition() && elapsed.elapsed() < milliseconds) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(10);
	}
	return condition();
}

static bool writeExecutable(const QString &path, const QByteArray &contents) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
		return false;
	}
	file.close();
	return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
		QFileDevice::ExeOwner);
}

int main(int argc, char **argv) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	QTemporaryDir temporary;
	check(temporary.isValid(), "temporary directory");
	const QString bin = temporary.path() + "/bin";
	check(QDir().mkpath(bin), "create fake audio tools");
	const QString log = temporary.path() + "/audio.log";
	qputenv("TEST_AUDIO_LOG", log.toUtf8());
	const QByteArray recorder = "#!/bin/sh\nprintf 'record:%s\\n' \"$1\" >> \"$TEST_AUDIO_LOG\"\n"
		"printf x > \"$1\"\ntrap 'exit 0' TERM INT\nwhile :; do /bin/sleep 0.05; done\n";
	const QByteArray player = "#!/bin/sh\nprintf 'play:%s\\n' \"$1\" >> \"$TEST_AUDIO_LOG\"\n";
	check(writeExecutable(bin + "/pw-record", recorder), "create fake pw-record");
	check(writeExecutable(bin + "/pw-play", player), "create fake pw-play");
	qputenv("PATH", bin.toUtf8());

	BarkRecorder window;
	const QString recording = temporary.path() + "/voice.wav";
	check(window.startRecording(recording), "start recording request");
	check(waitFor([&] { return window.isRecording() && QFileInfo(recording).isFile(); }),
		"recording starts and creates a file");
	check(!window.playRecording(recording), "playback cannot race a recording");
	window.stopRecording();
	check(waitFor([&] { return !window.isRecording(); }), "stop recording asynchronously");
	check(QFileInfo(recording).size() > 0, "recording destination contains audio data");
	check(window.playRecording(recording), "start playback request");
	check(waitFor([&] {
		QCoreApplication::processEvents();
		QFile observed(log);
		if (!observed.open(QIODevice::ReadOnly)) {
			return false;
		}
		return !window.isPlaying() && observed.readAll().contains("play:" + recording.toUtf8());
	}), "playback exits asynchronously");

	QFile result(log);
	check(result.open(QIODevice::ReadOnly), "read fake backend log");
	const QByteArray calls = result.readAll();
	check(calls.contains("record:" + recording.toUtf8()), "recorder receives selected path");
	check(calls.contains("play:" + recording.toUtf8()), "player receives selected path");
	check(!window.startRecording(temporary.path() + "/wrong.aiff"),
		"recording only accepts WAVE destinations");
	return 0;
}
