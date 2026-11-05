/* Piper neural voices: paths, downloads and speech-dispatcher text. */
#include "speechpiper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

QList<PiperVoice> piperVoices() {
	/* Same graph, same byte count: the medium US English voices are all
	 * 63,201,294 bytes upstream. Completion is by transfer finish, never
	 * by size, so a few bytes' drift would only tint the progress note. */
	return {
		{ "en_US-amy-medium", "Amy", "female", 63201294 },
		{ "en_US-ryan-medium", "Ryan", "male", 63201294 },
	};
}

QString piperTarballName() {
	const QString arch = QSysInfo::currentCpuArchitecture();
	if (arch == "x86_64") {
		return "piper_linux_x86_64.tar.gz";
	}
	if (arch == "aarch64" || arch == "arm64") {
		return "piper_linux_aarch64.tar.gz";
	}
	return QString();
}

qint64 piperTarballSize() {
	return piperTarballName() == "piper_linux_x86_64.tar.gz" ? 26460462 : 0;
}

static QString piperReleaseBase() {
	return "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/";
}

static QString piperVoicesBase() {
	return "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/";
}

static QString piperVoiceSubdir(const QString &id) {
	/* en_US-amy-medium lives in amy/medium/. */
	const QStringList parts = id.split('-');
	if (parts.size() < 3) {
		return QString();
	}
	return parts[1] + "/" + parts[2] + "/";
}

QList<PiperDownload> piperDownloads() {
	QList<PiperDownload> out;
	if (!piperTarballName().isEmpty()) {
		out.append({ piperReleaseBase() + piperTarballName(), piperTarballPath(),
			piperTarballSize() });
	}
	for (const PiperVoice &v : piperVoices()) {
		const QString base = piperVoicesBase() + piperVoiceSubdir(v.id);
		out.append({ base + v.id + ".onnx", piperModel(v.id), v.onnxSize });
		out.append({ base + v.id + ".onnx.json", piperModel(v.id) + ".json", 0 });
	}
	return out;
}

qint64 piperDownloadBytes() {
	qint64 total = 0;
	for (const PiperDownload &d : piperDownloads()) {
		total += d.size;
	}
	return total;
}

QString piperBaseDir() {
	/* Explicit XDG homes, not QStandardPaths' app locations: the panel
	 * and the tests agree whatever the application name is. */
	const QByteArray data = qgetenv("XDG_DATA_HOME");
	const QString home = data.isEmpty() ? QDir::homePath() + "/.local/share"
		: QString::fromUtf8(data);
	return home + "/zacos9/piper";
}

QString piperTarballPath() {
	return piperBaseDir() + "/piper.tar.gz";
}

QString piperBinary() {
	return piperBaseDir() + "/piper/piper";
}

QString piperWrapper() {
	return piperBaseDir() + "/piper-wrapper";
}

QString piperVoiceDir() {
	return piperBaseDir() + "/voices";
}

QString piperModel(const QString &id) {
	return piperVoiceDir() + "/" + id + ".onnx";
}

QString piperModulesDir() {
	const QByteArray config = qgetenv("XDG_CONFIG_HOME");
	const QString home = config.isEmpty() ? QDir::homePath() + "/.config"
		: QString::fromUtf8(config);
	return home + "/speech-dispatcher/modules";
}

QString piperModuleConf() {
	return piperModulesDir() + "/piper.conf";
}

QString piperSpeechdConf() {
	const QByteArray config = qgetenv("XDG_CONFIG_HOME");
	const QString home = config.isEmpty() ? QDir::homePath() + "/.config"
		: QString::fromUtf8(config);
	return home + "/speech-dispatcher/speechd.conf";
}

QString piperSystemSpeechdConf() {
	return "/etc/speech-dispatcher/speechd.conf";
}

bool piperVoicesPresent() {
	if (!QFileInfo(piperBinary()).isExecutable() || !QFileInfo(piperWrapper()).isExecutable()) {
		return false;
	}
	for (const PiperVoice &v : piperVoices()) {
		if (!QFileInfo::exists(piperModel(v.id)) ||
				!QFileInfo::exists(piperModel(v.id) + ".json")) {
			return false;
		}
	}
	return true;
}

QString piperConfText() {
	/* NOTE: the whole command is a single quoted string with the inner
	 * quotes escaped. sd_generic's option parser takes the first quoted
	 * string as the value: separate quotes would store the bare program
	 * path, every synthesis would fail for want of arguments, and the
	 * module would kill itself on the first utterance. */
	QString text = "# Written by the Speech control panel: Piper neural voices.\n"
		"# Removing this file (and the ZacOS block in speechd.conf) uninstalls them.\n"
		"Debug 0\n"
		"GenericExecuteSynth \"" + piperWrapper() + " \\\"$DATA\\\" $LANGUAGE $VOICE $PITCH $PITCH_RANGE $RATE\"\n"
		"GenericLanguage \"en-US\" \"en-US\" \"utf-8\"\n"
		"GenericLanguage \"en-us\" \"en-US\" \"utf-8\"\n"
		"GenericLanguage \"en-gb\" \"en-GB\" \"utf-8\"\n";
	for (const PiperVoice &v : piperVoices()) {
		const QString type = v.gender == "female" ? "female1" : "male1";
		text += "AddVoice \"en-US\" \"" + type + "\" \"" + v.id + "\"\n";
	}
	text += "DefaultVoice \"" + piperVoices().first().id + "\"\n";
	return text;
}

QString piperWrapperText() {
	return "#!/bin/sh\n"
		"# Written by the Speech control panel: speaks $1 with Piper's $3 voice\n"
		"# at $6's rate (-100..100). A WAV file (whose header carries the voice's\n"
		"# own sample rate) is synthesized, then played with whatever is around.\n"
		"# speech-dispatcher's generic backend kills a module whose command\n"
		"# fails, so playback is best-effort (exit 0 once the WAV exists): a\n"
		"# hiccup stays silent instead of taking Neural down. Only a failed\n"
		"# synthesis itself exits non-zero.\n"
		"PIPER=\"" + piperBinary() + "\"\n"
		"VOICEDIR=\"" + piperVoiceDir() + "\"\n"
		"# speech-dispatcher modules run with a bare environment; PipeWire\n"
		"# needs the session runtime to find the sound server.\n"
		": \"${XDG_RUNTIME_DIR:=/run/user/$(id -u)}\"\n"
		"export XDG_RUNTIME_DIR\n"
		"out=$(mktemp \"${TMPDIR:-/tmp}/zacos9-piper-XXXXXX.wav\") || exit 1\n"
		"trap 'rm -f \"$out\"' EXIT INT TERM\n"
		"scale=1.0\n"
		"# speech-dispatcher formats numbers with decimals (0.00); sh needs\n"
		"# integers for the comparisons below.\n"
		"r=${6:-0}\n"
		"r=${r%%.*}\n"
		"if [ \"$r\" -ge 75 ]; then scale=0.5\n"
		"elif [ \"$r\" -ge 50 ]; then scale=0.6\n"
		"elif [ \"$r\" -ge 25 ]; then scale=0.7\n"
		"elif [ \"$r\" -gt -25 ]; then scale=1.0\n"
		"elif [ \"$r\" -gt -50 ]; then scale=1.2\n"
		"elif [ \"$r\" -gt -75 ]; then scale=1.4\n"
		"else scale=2.0\n"
		"fi\n"
		"printf '%s' \"$1\" | \"$PIPER\" --model \"$VOICEDIR/$3.onnx\" --length-scale \"$scale\" --output_file \"$out\" >/dev/null 2>&1 || exit 1\n"
		"pw-play \"$out\" 2>/dev/null || paplay \"$out\" 2>/dev/null || aplay -q \"$out\" 2>/dev/null || true\n"
		"exit 0\n";
}

QString piperSpeechdBlock() {
	return "# ZacOS 9 neural voices - start (the Speech control panel writes this)\n"
		"AddModule \"piper\" \"sd_generic\" \"" + piperModuleConf() + "\"\n"
		"# ZacOS 9 neural voices - end\n";
}

QLatin1String piperSpeechdMarker() {
	return QLatin1String("# ZacOS 9 neural voices");
}

QString piperSystemModulesDir() {
	return "/etc/speech-dispatcher/modules";
}

QString piperModuleBinaryDir() {
	return "/usr/lib/speech-dispatcher-modules";
}

QString synthSystemModules(const QStringList &confs, const QStringList &binaries,
	const QString &confDir) {
	QString out;
	QStringList seen;
	for (const QString &conf : confs) {
		if (!conf.endsWith(".conf") || conf == "piper.conf" || seen.contains(conf)) {
			continue;
		}
		seen.append(conf);
		const QString name = conf.chopped(5); /* minus ".conf" */
		if (name.contains("generic") || name.contains("mbrola") ||
				!binaries.contains("sd_" + name)) {
			continue;
		}
		out += "AddModule \"" + name + "\" \"sd_" + name + "\" \"" +
			confDir + "/" + conf + "\"\n";
	}
	return out;
}

QString mergePiperSpeechdConf(const QString &existing, const QString &systemBlock) {
	if (existing.contains(piperSpeechdMarker())) {
		return existing;
	}
	bool namesModule = false;
	for (const QString &line : existing.split('\n')) {
		if (line.trimmed().startsWith("AddModule")) {
			namesModule = true;
			break;
		}
	}
	QString out = existing;
	if (!out.isEmpty() && !out.endsWith("\n")) {
		out += "\n";
	}
	if (!namesModule) {
		out += systemBlock;
	}
	return out + piperSpeechdBlock();
}
