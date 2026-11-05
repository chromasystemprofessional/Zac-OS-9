/* The Neural list's downloads and speech-dispatcher text, with a fake home. */
#include "speechpiper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <cassert>

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir home;
	assert(home.isValid());
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());

	const QList<PiperVoice> voices = piperVoices();
	check(voices.size() == 2 && voices[0].id == "en_US-amy-medium" &&
		voices[0].gender == "female" && voices[1].id == "en_US-ryan-medium" &&
		voices[1].gender == "male", "Amy (female) and Ryan (male), medium");
	check(piperBaseDir() == home.path() + "/data/zacos9/piper",
		"voice files live under this account's data");
	check(piperVoiceDir() == piperBaseDir() + "/voices" &&
		piperModel("en_US-amy-medium") == piperVoiceDir() + "/en_US-amy-medium.onnx",
		"models sit in voices/");
	check(!piperTarballName().isEmpty() && piperTarballName().endsWith(".tar.gz"),
		"a static binary for this CPU");

	const QList<PiperDownload> downloads = piperDownloads();
	bool tarball = false, amy = false, ryan = false, amyJson = false;
	qint64 total = 0;
	for (const PiperDownload &d : downloads) {
		total += d.size;
		tarball |= d.dest == piperTarballPath();
		amy |= d.dest == piperModel("en_US-amy-medium") && d.size == 63201294;
		ryan |= d.dest == piperModel("en_US-ryan-medium");
		amyJson |= d.dest == piperModel("en_US-amy-medium") + ".json";
	}
	check(downloads.size() == 5 && tarball && amy && ryan && amyJson &&
		total == piperDownloadBytes() && total > 100000000,
		"five downloads: the binary, two voices and their configs");
	check(!piperVoicesPresent(), "nothing installed yet");

	const QString conf = piperConfText();
	check(conf.contains("GenericExecuteSynth \"" + piperWrapper() +
		" \\\"$DATA\\\"") &&
		conf.contains("AddVoice \"en-US\" \"female1\" \"en_US-amy-medium\"") &&
		conf.contains("AddVoice \"en-US\" \"male1\" \"en_US-ryan-medium\"") &&
		conf.contains("DefaultVoice \"en_US-amy-medium\"") &&
		!conf.contains("DefaultCharset"), "the piper module, in trixie's dialect");
	const QString wrapper = piperWrapperText();
	check(wrapper.startsWith("#!/bin/sh") && wrapper.contains("PIPER=\"" + piperBinary() + "\"") &&
		wrapper.contains("--length-scale") && wrapper.contains("pw-play") &&
		wrapper.contains("mktemp") && wrapper.contains("XDG_RUNTIME_DIR") &&
		wrapper.contains("exit 0") && wrapper.contains("r=${r%%.*}"),
		"the wrapper: rate, a WAV file, playback, a bare environment");
	const QString sys = synthSystemModules(
		{ "espeak-ng.conf", "mary-generic.conf", "voxin.conf", "piper.conf",
			"espeak-ng-mbrola.conf" },
		{ "sd_espeak-ng", "sd_espeak-ng-mbrola", "sd_generic" },
		"/etc/speech-dispatcher/modules");
	check(sys.contains("AddModule \"espeak-ng\" \"sd_espeak-ng\" "
		"\"/etc/speech-dispatcher/modules/espeak-ng.conf\"") &&
		!sys.contains("espeak-ng-mbrola") && !sys.contains("mary-generic") &&
		!sys.contains("voxin") && !sys.contains("piper"),
		"stock engines spelled out; templates and side data skipped");
	const QString merged = mergePiperSpeechdConf("DefaultModule espeak-ng\n", sys);
	check(merged.contains("DefaultModule espeak-ng") &&
		merged.contains("AddModule \"espeak-ng\" \"sd_espeak-ng\"") &&
		merged.contains("AddModule \"piper\" \"sd_generic\" ") &&
		mergePiperSpeechdConf(merged, sys) == merged,
		"speechd.conf keeps its default and the others, once");
	const QString custom = mergePiperSpeechdConf(
		"AddModule \"espeak-ng\" \"sd_espeak-ng\" \"espeak-ng.conf\"\n", sys);
	check(custom.contains("AddModule \"piper\"") && !custom.contains("mary-generic"),
		"a custom module list is left alone, plus piper");

	/* A finished install reads back as present. */
	assert(QDir().mkpath(QFileInfo(piperBinary()).absolutePath()));
	assert(QDir().mkpath(piperVoiceDir()));
	assert(QDir().mkpath(piperModulesDir()));
	QFile binary(piperBinary());
	assert(binary.open(QIODevice::WriteOnly) && binary.write("#!/bin/sh\n") > 0);
	binary.close();
	assert(QFile::setPermissions(piperBinary(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	QFile script(piperWrapper());
	assert(script.open(QIODevice::WriteOnly) && script.write("#!/bin/sh\n") > 0);
	script.close();
	assert(QFile::setPermissions(piperWrapper(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	for (const PiperVoice &v : voices) {
		QFile model(piperModel(v.id));
		assert(model.open(QIODevice::WriteOnly) && model.write("x") > 0);
		model.close();
		QFile config(piperModel(v.id) + ".json");
		assert(config.open(QIODevice::WriteOnly) && config.write("{}") > 0);
		config.close();
	}
	check(piperVoicesPresent(), "a finished install reads back as present");

	QTextStream(stdout) << fails << " failure" << (fails == 1 ? "" : "s") << "\n";
	return fails ? 1 : 0;
}
