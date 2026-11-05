/* Piper neural voices for the Speech panel's Neural list: where they live,
 * what to fetch, and the speech-dispatcher module that speaks them.
 *
 * Everything here is plain file paths and text (no processes, no network),
 * so tests/panels/test_piper.cpp covers it directly. The panel (speech.cpp)
 * does the downloading and restarts speech-dispatcher. */
#pragma once

#include <QList>
#include <QString>

struct PiperVoice {
	QString id; /* en_US-amy-medium: the file stem, speech-dispatcher's name */
	QString label; /* Amy */
	QString gender; /* female */
	qint64 onnxSize; /* expected bytes of the .onnx; 0 when unknown */
};

/* The voices the panel fetches: Amy (female) and Ryan (male), medium
 * quality. Amy's dataset defers to mimic3-voices for its licence; Ryan's
 * dataset is CC BY-NC-SA 4.0 (roholazandie/ryanspeech). Both come from
 * rhasspy/piper-voices, whose per-voice MODEL_CARD files say the same.
 * Personal, opt-in fetching only: nothing here ships on the ISO. */
QList<PiperVoice> piperVoices();

/* The static Piper binary (rhasspy/piper 2023.11.14-2: onnxruntime,
 * espeak-ng and its data ride along, so trixie needs nothing else).
 * Empty on CPUs without a build. */
QString piperTarballName(); /* piper_linux_x86_64.tar.gz, or "" */
qint64 piperTarballSize(); /* expected bytes; 0 when unknown */

/* One download: a URL and where it lands. */
struct PiperDownload {
	QString url;
	QString dest; /* absolute path */
	qint64 size; /* expected bytes; 0 when unknown */
};
QList<PiperDownload> piperDownloads();

/* Everything the downloads add up to, for the panel's progress note. */
qint64 piperDownloadBytes();

/* ~/.local/share/zacos9/piper and below. */
QString piperBaseDir();
QString piperTarballPath(); /* the downloaded archive (removed after unpack) */
QString piperBinary(); /* the unpacked piper program */
QString piperWrapper(); /* the speech-dispatcher wrapper script */
QString piperVoiceDir();
QString piperModel(const QString &id); /* voices/<id>.onnx */

/* speech-dispatcher's user files. */
QString piperModulesDir(); /* ~/.config/speech-dispatcher/modules */
QString piperModuleConf(); /* .../modules/piper.conf */
QString piperSpeechdConf(); /* ~/.config/speech-dispatcher/speechd.conf */
QString piperSystemSpeechdConf(); /* /etc/speech-dispatcher/speechd.conf */

/* True when the binary, the wrapper and every voice (model and config)
 * are in place and executable where needed. */
bool piperVoicesPresent();

/* The module for speech-dispatcher's generic backend: runs the wrapper,
 * whose third argument is the voice (an AddVoice name below). trixie has
 * no GenericDefaultCharset, hence the GenericLanguage lines. The second
 * AddVoice field is restricted to male1-3/female1-3/child_*: the third is
 * what `spd-say -L` lists and `-y` takes. */
QString piperConfText();

/* The wrapper: synthesizes $1 with the $3rd voice at the $6th rate to a
 * WAV file (whose header carries the voice's own sample rate), then plays
 * it with whatever is around. Never waits (CLAUDE.md): speech-dispatcher
 * runs it and moves on when it exits. */
QString piperWrapperText();

/* speech-dispatcher's module search paths. */
QString piperSystemModulesDir(); /* /etc/speech-dispatcher/modules */
QString piperModuleBinaryDir(); /* /usr/lib/speech-dispatcher-modules */

/* AddModule lines spelling out the every-installed-module default (conf
 * basenames with ".conf", module binary basenames): the default loads each
 * module with a binary of its own, in configuration-file order, and `spd-say
 * -L` lists the first one's voices — so nothing may come before the stock
 * engines. The *-generic templates never load by default (they only work
 * once somebody configures voices into them, and then that somebody has
 * their own AddModule lines anyway), and neither does anything needing
 * missing side data (espeak-ng-mbrola without mbrola voices just fails to
 * initialize). Absolute conf paths, so they resolve from a user
 * speechd.conf too. "piper.conf" is never listed here: the panel's own
 * block adds it last. */
QString synthSystemModules(const QStringList &confs, const QStringList &binaries,
	const QString &confDir);

/* The user's speechd.conf with the panel's block. A user file replaces
 * the system's wholesale, and any AddModule line at all turns off the
 * default of loading every installed module: so when the existing file
 * names no module itself, the installed system ones are listed too
 * (systemBlock), keeping Classic and Modern working. */
QString mergePiperSpeechdConf(const QString &existing, const QString &systemBlock);
