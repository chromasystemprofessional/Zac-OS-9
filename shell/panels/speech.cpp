/* The Speech control panel (zacos9-controlpanel speech). */
#include "controlpanel.h"

#include "speechpiper.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>

/* Mac OS 9's Speech control panel: the voice and its rate (speech-dispatcher,
 * as Scribble and Talking Alerts use them), and Talking Alerts. Three lists:
 * Classic (espeak-ng's base voices plus feminine variants, the 1999 feel),
 * Modern (RHVoice's speakers) and Neural (Piper's Amy and Ryan, fetched on
 * demand), with a choice of which one speaks. */
class SpeechPanel : public ControlPanel {
	/* A voice list: a pop-up in a group of its own. */
	static constexpr int LIST_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + PL_GROUP_MARGIN;
	static constexpr int TRY_W = 96;
	static constexpr int G0 = 12, G1 = G0 + LIST_H + 20, G2 = G1 + LIST_H + 20;
	static constexpr int NEURAL_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + 8 +
		PL_BUTTON_H + PL_GROUP_MARGIN;
	static constexpr int G3 = G2 + NEURAL_H + 20;
	static constexpr int KIND_H = ROWS(3);
	static constexpr int G4 = G3 + KIND_H + 20, G5 = G4 + SLIDER_GROUP_H + 20;
	static constexpr int ALERTS_H = 1 + PL_GROUP_MARGIN_TOP + PL_CHECKBOX_SIZE + PL_GROUP_MARGIN;
	static constexpr int PANEL_H = G5 + ALERTS_H + 16;

public:
	SpeechPanel() : ControlPanel(_("Speech"), PANEL_H) {
		m_groups = { { G0, G0 + LIST_H, "Classic Voice" }, { G1, G1 + LIST_H, "Modern Voice" },
			{ G2, G2 + NEURAL_H, "Neural Voice" }, { G3, G3 + KIND_H, "Speak With" },
			{ G4, G4 + SLIDER_GROUP_H, "Rate" }, { G5, G5 + ALERTS_H, "Talking Alerts" } };
		m_classic.rect = QRect(M + IN, G0 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 2, PL_POPUP_H);
		m_classic.items = { PopupItem("Reading voices…", false) };
		m_classic.enabled = false;
		m_classic.chosen = [this](int row) {
			if (row >= 0 && row < m_classicValues.size()) {
				m_classic.selected = row;
				pl_setting_set("speech-voice-classic",
					row == 0 ? nullptr : m_classicValues[row].toUtf8().constData());
				update();
			}
		};
		m_modern.rect = QRect(M + IN, G1 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 2, PL_POPUP_H);
		m_modern.items = { PopupItem("Reading voices…", false) };
		m_modern.enabled = false;
		m_modern.chosen = [this](int row) {
			if (row >= 0 && row < m_modernValues.size()) {
				m_modern.selected = row;
				pl_setting_set("speech-voice-modern",
					m_modernValues[row].toUtf8().constData());
				update();
			}
		};
		m_neural.rect = QRect(M + IN, G2 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 2, PL_POPUP_H);
		m_neural.items = { PopupItem("Reading voices…", false) };
		m_neural.enabled = false;
		m_neural.chosen = [this](int row) {
			if (row >= 0 && row < m_neuralValues.size()) {
				m_neural.selected = row;
				pl_setting_set("speech-voice-neural",
					m_neuralValues[row].toUtf8().constData());
				update();
			}
		};
		m_download = PanelButton("Download…",
			QRect(W - M - IN - 4 - TRY_W, G2 + 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + 8,
				TRY_W, PL_BUTTON_H));
		m_download.clicked = [this] { startDownload(); };
		QObject::connect(&m_unpack, &QProcess::finished, this, [this](int code) {
			finishUnpack(code);
		});
		const int ky = G3 + 1 + PL_GROUP_MARGIN_TOP;
		m_kind.add("Classic voice", QPoint(M + IN, ky));
		m_kind.add("Modern voice", QPoint(M + IN, ky + 20));
		m_kind.add("Neural voice", QPoint(M + IN, ky + 40));
		m_kind.selected = pl_speech_neural() ? 2 : pl_speech_modern() ? 1 : 0;
		m_kind.hint = "Which list speaks for Talking Alerts and reading.";
		m_kind.changed = [](int i) {
			pl_setting_set("speech-kind", i == 2 ? "neural" : i == 1 ? "modern" : nullptr);
		};
		m_radios.push_back(&m_kind);
		m_try = PanelButton("Try Voice",
			QRect(W - M - IN - 4 - TRY_W, ky + (KIND_H - 1 - PL_GROUP_MARGIN_TOP -
				PL_GROUP_MARGIN - PL_BUTTON_H) / 2, TRY_W, PL_BUTTON_H));
		m_try.clicked = [] { pl_speak("Hello. This is how I sound."); };
		/* -100..100 in nine steps of 25; the middle is speech-dispatcher's own. */
		placeSlider(m_rate, G4, 9, pl_speech_rate() / 25 + 4, "Slow", "Fast");
		m_rate.changed = [](int step) {
			pl_setting_set("speech-rate", step == 4 ? nullptr
				: QByteArray::number((step - 4) * 25).constData());
		};
		m_alerts = PanelCheckbox("Speak the text of alerts", QPoint(M + IN, G5 + 1 + PL_GROUP_MARGIN_TOP));
		m_alerts.on = pl_talking_alerts();
		m_alerts.toggled = [](bool on) { pl_setting_set("talking-alerts", on ? "true" : nullptr); };
		m_checks.push_back(&m_alerts);

		/* Hint Bubbles. */
		m_hintRects.push_back({ &m_classic.rect, "Chooses the Classic voice ZacOS speaks with." });
		m_hintRects.push_back({ &m_modern.rect, "Chooses the Modern voice ZacOS speaks with." });
		m_hintRects.push_back({ &m_neural.rect, "Chooses the Neural voice ZacOS speaks with." });
		m_hintRects.push_back({ &m_try.rect, "Speaks a sentence in the chosen list's voice and rate." });
		m_rate.hint = "Sets how fast the voice speaks.";
		m_alerts.hint = "When an alert isn't answered after a moment, it speaks its message.";

		migrateLegacyVoice();
		readModernInfos();
		readVoices();
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_classic.paint(c);
		m_modern.paint(c);
		m_neural.paint(c);
		if (m_showDownload) {
			m_download.paint(c);
		} else if (piperVoicesPresent()) {
			panelText(c, "Amy and Ryan are installed.", M + IN,
				G2 + 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + 8, PL_FONT_VIEWS,
				GRAY(0x5), W - 2 * M);
		}
		m_try.paint(c);
		if (!m_error.isEmpty()) {
			panelText(c, m_error, M, height() - 6, PL_FONT_VIEWS, GRAY(0x5), W - 2 * M);
		}
	}
	bool pressExtra(QPoint pos, bool) override {
		return m_classic.press(this, pos) || m_modern.press(this, pos) ||
			m_neural.press(this, pos) || (m_showDownload && m_download.press(pos)) ||
			m_try.press(pos);
	}
	void moveExtra(QPoint pos) override {
		if ((m_showDownload && m_download.move(pos)) || m_try.move(pos)) {
			update();
		}
	}
	void releaseExtra(QPoint pos) override {
		if (m_showDownload) {
			m_download.release(pos);
		}
		m_try.release(pos);
	}

private:
	PanelPopup m_classic, m_modern, m_neural;
	PanelRadios m_kind;
	PanelButton m_try, m_download;
	PanelSlider m_rate;
	PanelCheckbox m_alerts;
	QStringList m_classicValues; /* [0] is the default ("") */
	QStringList m_modernValues; /* RHVoice speakers; empty until known */
	QStringList m_neuralValues; /* Piper voices; empty until fetched */
	QString m_error;
	bool m_noSpeechd = false;
	bool m_showDownload = true;
	QProcess m_list, m_modules;

	struct ModernInfo {
		QString name, gender, language;
	};
	QList<ModernInfo> m_modernInfos; /* from RHVoice's voice files */
	QStringList m_synthNames; /* every NAME `spd-say -L` lists */
	bool m_gotVoices = false, m_gotModules = false;
	bool m_hasRhvoice = false, m_hasPiper = false, m_bounced = false;

	/* The Neural fetch: queued downloads, then unpacking and setup. */
	QNetworkAccessManager m_net;
	QList<PiperDownload> m_queue;
	QNetworkReply *m_reply = nullptr;
	qint64 m_doneBytes = 0; /* finished files, for the progress note */
	bool m_downloading = false;
	QString m_progress;

	/* The single "speech-voice" from before the lists becomes the
	 * Classic voice, so nobody's choice is lost. */
	void migrateLegacyVoice() {
		char classic[96] = "", legacy[96] = "";
		if (!pl_setting("speech-voice-classic", classic, sizeof(classic)) &&
				pl_setting("speech-voice", legacy, sizeof(legacy))) {
			pl_setting_set("speech-voice-classic", legacy);
			pl_setting_set("speech-voice", nullptr);
		}
	}

	/* RHVoice's installed speakers and their genders, from its voice
	 * files (synchronous: plain files, no speech-dispatcher round trip). */
	void readModernInfos() {
		const QDir dir("/usr/share/RHVoice/voices");
		if (!dir.isReadable()) {
			return;
		}
		for (const QString &voice :
				dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			QFile info(dir.filePath(voice + "/voice.info"));
			if (!info.open(QIODevice::ReadOnly)) {
				continue;
			}
			ModernInfo found;
			for (const QByteArray &raw : info.readAll().split('\n')) {
				const QString line = QString::fromUtf8(raw).trimmed();
				if (line.startsWith("name=")) {
					found.name = line.mid(5).trimmed();
				} else if (line.startsWith("gender=")) {
					found.gender = line.mid(7).trimmed();
				} else if (line.startsWith("language=")) {
					found.language = line.mid(9).trimmed();
				}
			}
			if (!found.name.isEmpty()) {
				m_modernInfos.append(found);
			}
		}
		std::sort(m_modernInfos.begin(), m_modernInfos.end(),
			[](const ModernInfo &a, const ModernInfo &b) { return a.name < b.name; });
	}

	/* `spd-say -L`: a heading, then NAME LANGUAGE VARIANT lines; `-O` lists
	 * the loaded output modules (RHVoice's is "rhvoice", Neural's own is
	 * "piper"). Never waitForFinished (CLAUDE.md). */
	void readVoices() {
		QObject::connect(&m_list, &QProcess::finished, this, [this] {
			m_synthNames = QStringList();
			const QStringList lines = QString::fromUtf8(m_list.readAllStandardOutput()).split('\n');
			for (const QString &line : lines.mid(1)) {
				const QStringList f = line.simplified().split(' ');
				if (f.size() >= 3) {
					m_synthNames << f.mid(0, f.size() - 2).join(' ');
				}
			}
			m_gotVoices = true;
			fillClassic(lines);
			fillModern();
			fillNeural();
		});
		QObject::connect(&m_list, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				m_noSpeechd = true;
				m_error = "Speech needs speech-dispatcher, which isn't installed.";
				fillClassic({});
				m_gotVoices = true;
				fillModern();
				fillNeural();
			}
		});
		QObject::connect(&m_modules, &QProcess::finished, this, [this] {
			const QStringList lines = QString::fromUtf8(m_modules.readAllStandardOutput()).split('\n');
			m_hasRhvoice = std::any_of(lines.cbegin(), lines.cend(),
				[](const QString &line) { return line.trimmed() == "rhvoice"; });
			m_hasPiper = std::any_of(lines.cbegin(), lines.cend(),
				[](const QString &line) { return line.trimmed() == "piper"; });
			m_gotModules = true;
			fillModern();
			fillNeural();
		});
		QObject::connect(&m_modules, &QProcess::errorOccurred, this, [this] {
			m_gotModules = true;
			fillModern();
			fillNeural();
		});
		relist();
	}

	void relist() {
		m_list.start("spd-say", { "-L" });
		m_modules.start("spd-say", { "-O" });
	}

	void fillClassic(const QStringList &lines) {
		char current[96] = "";
		if (!pl_setting("speech-voice-classic", current, sizeof(current))) {
			pl_setting("speech-voice", current, sizeof(current));
		}
		m_classicValues = { QString() };
		m_classic.items = { PopupItem("Default voice") };
		/* NAME (which may have spaces) LANGUAGE VARIANT, right-aligned: the
		 * base voices ("none") only, in the person's language if any are. */
		struct Voice {
			QString name, language;
		};
		QList<Voice> all;
		for (const QString &line : lines) {
			const QStringList f = line.simplified().split(' ');
			if (f.size() >= 3 && f.last() == "none") {
				all.append({ f.mid(0, f.size() - 2).join(' '), f[f.size() - 2] });
			}
		}
		const QString language = QLocale().name().section('_', 0, 0);
		const bool any = std::any_of(all.cbegin(), all.cend(),
			[&](const Voice &v) { return v.language.section('-', 0, 0) == language; });
		QStringList kept;
		for (const Voice &v : all) {
			if (any && v.language.section('-', 0, 0) != language && v.name != QString::fromUtf8(current)) {
				continue;
			}
			m_classicValues << v.name;
			m_classic.items.emplace_back(v.name);
			kept << v.name;
		}
		/* The feminine variants of the voices above (their !v files say
		 * "gender female", and spd-say lists them under their voice
		 * name): Alicia and Annie, the named female personae, and
		 * female1/female2, the female pitch shifts. More exist (anika,
		 * belinda, grandma, linda, steph, female3/4…); this keeps each
		 * language short. */
		static const QSet<QString> feminine = { "Alicia", "Annie", "female1", "female2" };
		for (const QString &line : lines) {
			const QStringList f = line.simplified().split(' ');
			if (f.size() < 3 || !feminine.contains(f.last())) {
				continue;
			}
			/* NAME is "Base+Variant"; the base alone decides. */
			QString name = f.mid(0, f.size() - 2).join(' ');
			const QString suffix = "+" + f.last();
			if (!name.endsWith(suffix)) {
				continue;
			}
			name.chop(suffix.size());
			if (!kept.contains(name)) {
				continue;
			}
			const QString value = name + suffix;
			QString person = f.last();
			if (person == "female1" || person == "female2") {
				person = person == "female1" ? "Feminine 1" : "Feminine 2";
			}
			m_classicValues << value;
			m_classic.items.emplace_back(name + ", " + person + " (female)");
		}
		m_classic.selected = 0;
		for (int i = 1; i < m_classicValues.size(); i++) {
			if (m_classicValues[i] == QString::fromUtf8(current)) {
				m_classic.selected = i;
				break;
			}
		}
		m_classic.enabled = true;
		update();
	}

	void fillModern() {
		if (!m_gotVoices || !m_gotModules) {
			return;
		}
		m_modernValues.clear();
		m_modern.items.clear();
		if (!m_modernInfos.isEmpty() && m_hasRhvoice && !m_noSpeechd) {
			char current[96] = "";
			pl_setting("speech-voice-modern", current, sizeof(current));
			for (const ModernInfo &info : m_modernInfos) {
				/* The name speech-dispatcher takes: its own listing when
				 * it knows the speaker, else RHVoice's name for it. */
				QString value = info.name;
				for (const QString &synth : m_synthNames) {
					if (synth.compare(info.name, Qt::CaseInsensitive) == 0) {
						value = synth;
						break;
					}
				}
				m_modernValues << value;
				QString text = info.name + " (" + info.gender.toLower() + ")";
				if (!info.language.isEmpty() && info.language != "English") {
					text += " " + info.language;
				}
				m_modern.items.emplace_back(text);
			}
			m_modern.selected = 0;
			for (int i = 0; i < m_modernValues.size(); i++) {
				if (m_modernValues[i] == QString::fromUtf8(current)) {
					m_modern.selected = i;
					break;
				}
			}
			if (m_modernValues[m_modern.selected] != QString::fromUtf8(current)) {
				/* No choice yet, or the speaker went away: speak the
				 * first one, and remember it so Try agrees. */
				m_modern.selected = 0;
				pl_setting_set("speech-voice-modern",
					m_modernValues[0].toUtf8().constData());
			}
			m_modern.enabled = true;
		} else {
			m_modern.items = { PopupItem("No modern voices", false) };
			m_modern.enabled = false;
		}
		refreshNotes();
		update();
	}

	void fillNeural() {
		if (!m_gotVoices || !m_gotModules) {
			return;
		}
		m_neuralValues.clear();
		m_neural.items.clear();
		const bool present = piperVoicesPresent();
		m_showDownload = !present && !m_downloading;
		if (present && (m_hasPiper || m_noSpeechd) && !m_downloading) {
			char current[96] = "";
			pl_setting("speech-voice-neural", current, sizeof(current));
			for (const PiperVoice &voice : piperVoices()) {
				QString value = voice.id;
				for (const QString &synth : m_synthNames) {
					if (synth.compare(voice.id, Qt::CaseInsensitive) == 0) {
						value = synth;
						break;
					}
				}
				m_neuralValues << value;
				m_neural.items.emplace_back(voice.label + " (" + voice.gender + ")");
			}
			m_neural.selected = 0;
			for (int i = 0; i < m_neuralValues.size(); i++) {
				if (m_neuralValues[i] == QString::fromUtf8(current)) {
					m_neural.selected = i;
					break;
				}
			}
			if (m_neuralValues[m_neural.selected] != QString::fromUtf8(current)) {
				m_neural.selected = 0;
				pl_setting_set("speech-voice-neural",
					m_neuralValues[0].toUtf8().constData());
			}
			m_neural.enabled = true;
		} else if (present && !m_noSpeechd && !m_downloading) {
			/* Fetched but speech-dispatcher hasn't loaded the module:
			 * once per opening, restart it and look again. */
			if (!m_bounced) {
				m_bounced = true;
				m_neural.items = { PopupItem("Starting neural voices…", false) };
				m_neural.enabled = false;
				restartSpeech();
			} else {
				m_neural.items = { PopupItem("No neural voices", false) };
				m_neural.enabled = false;
			}
		} else if (!m_downloading) {
			m_neural.items = { PopupItem("No neural voices", false) };
			m_neural.enabled = false;
		}
		refreshNotes();
		update();
	}

	void refreshNotes() {
		m_notes.clear();
		if (m_noSpeechd) {
			return;
		}
		if (m_gotModules && (m_modernInfos.isEmpty() || !m_hasRhvoice)) {
			m_notes.push_back(QString("Modern voices need the rhvoice-english and "
				"speech-dispatcher-rhvoice packages."));
		}
		if (m_downloading) {
			m_notes.push_back(m_progress);
		} else if (!piperVoicesPresent()) {
			const double mb = piperDownloadBytes() / 1048576.0;
			m_notes.push_back(QString("Neural voices fetch about %1 MB: Piper plus Amy "
				"(female) and Ryan (male).").arg(QString::number(mb, 'f', 0)));
			m_notes.push_back(QString("Amy and Ryan come from rhasspy/piper-voices; "
				"Ryan's dataset is CC BY-NC-SA 4.0."));
		}
	}

	/* The Download button's label and state (PanelButton keeps no
	 * settable text, so it is rebuilt with the same rectangle). */
	void setDownload(const QString &text, bool enabled) {
		m_download = PanelButton(text, m_download.rect);
		m_download.enabled = enabled;
		m_download.clicked = [this] { startDownload(); };
	}

	/* The Neural fetch, then unpacking and setup. Files already present
	 * at their expected size are skipped, so closing mid-download and
	 * pressing Download again resumes where it left off. */
	void startDownload() {
		if (m_downloading) {
			return;
		}
		if (piperTarballName().isEmpty()) {
			m_error = "There is no Piper build for this computer.";
			update();
			return;
		}
		m_queue.clear();
		m_doneBytes = 0;
		for (const PiperDownload &d : piperDownloads()) {
			if (d.size > 0 && QFileInfo(d.dest).size() == d.size) {
				m_doneBytes += d.size;
				continue;
			}
			m_queue.append(d);
		}
		if (m_queue.isEmpty()) {
			finishDownload();
			return;
		}
		m_downloading = true;
		m_showDownload = true;
		setDownload("Downloading…", false);
		nextDownload();
		update();
	}

	void nextDownload() {
		if (m_queue.isEmpty()) {
			finishDownload();
			return;
		}
		const PiperDownload current = m_queue.first();
		QNetworkRequest request(current.url);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::NoLessSafeRedirectPolicy);
		m_reply = m_net.get(request);
		QObject::connect(m_reply, &QNetworkReply::downloadProgress, this,
			[this](qint64 received, qint64) {
				const double mb = (m_doneBytes + received) / 1048576.0;
				const double total = piperDownloadBytes() / 1048576.0;
				m_progress = QString("Downloading… %1 of %2 MB")
					.arg(QString::number(mb, 'f', 0), QString::number(total, 'f', 0));
				refreshNotes();
				update();
			});
		QObject::connect(m_reply, &QNetworkReply::finished, this, [this] {
			QNetworkReply *reply = m_reply;
			m_reply = nullptr;
			const PiperDownload current = m_queue.takeFirst();
			if (reply->error() != QNetworkReply::NoError) {
				const QString detail = "The download failed: " +
					reply->errorString() + ". Try again later.";
				reply->deleteLater();
				failed(detail);
				return;
			}
			QDir().mkpath(QFileInfo(current.dest).absolutePath());
			QFile out(current.dest);
			if (!out.open(QIODevice::WriteOnly) || out.write(reply->readAll()) < 0) {
				reply->deleteLater();
				failed("The download could not be saved. Try again later.");
				return;
			}
			m_doneBytes += current.size;
			reply->deleteLater();
			nextDownload();
		});
		m_progress = QString("Downloading… %1 of %2 MB")
			.arg(QString::number(m_doneBytes / 1048576.0, 'f', 0),
				QString::number(piperDownloadBytes() / 1048576.0, 'f', 0));
		refreshNotes();
	}

	void finishDownload() {
		/* Unpack the binary, write the wrapper, the module and the
		 * speechd.conf block, then restart speech and look again. */
		QDir().mkpath(piperBaseDir());
		QDir().mkpath(piperVoiceDir());
		QDir().mkpath(piperModulesDir());
		m_unpack.start("tar", { "xzf", piperTarballPath(), "-C", piperBaseDir() });
	}

	void finishUnpack(int code) {
		if (!m_downloading) {
			return; /* a stale repeat; the install already moved on */
		}
		if (code != 0 || !QFileInfo(piperBinary()).isFile()) {
			failed("The Piper archive would not unpack. Try again later.");
			return;
		}
		QFile::setPermissions(piperBinary(),
			QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		QFile wrapper(piperWrapper());
		if (!wrapper.open(QIODevice::WriteOnly) ||
				wrapper.write(piperWrapperText().toUtf8()) < 0) {
			failed("The helper script could not be saved. Try again later.");
			return;
		}
		wrapper.close();
		QFile::setPermissions(piperWrapper(),
			QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		QFile conf(piperModuleConf());
		if (!conf.open(QIODevice::WriteOnly) ||
				conf.write(piperConfText().toUtf8()) < 0) {
			failed("The speech module could not be saved. Try again later.");
			return;
		}
		conf.close();
		if (!installSpeechdConf()) {
			failed("speechd.conf could not be updated. Try again later.");
			return;
		}
		QFile::remove(piperTarballPath());
		m_downloading = false;
		setDownload("Download…", true);
		restartSpeech();
	}

	void failed(const QString &message) {
		m_error = message;
		m_downloading = false;
		setDownload("Try Again", true);
		refreshNotes();
		update();
	}

	/* The user's speechd.conf with the panel's block, seeded from the
	 * system's when there is none (a user file replaces it wholesale,
	 * so it must start as a copy, never as the block alone). */
	bool installSpeechdConf() {
		const QString path = piperSpeechdConf();
		QString existing;
		QFile user(path);
		if (user.exists()) {
			if (!user.open(QIODevice::ReadOnly)) {
				return false;
			}
			existing = QString::fromUtf8(user.readAll());
		} else {
			QFile system(piperSystemSpeechdConf());
			if (!system.open(QIODevice::ReadOnly)) {
				return false;
			}
			existing = QString::fromUtf8(system.readAll());
		}
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return false;
		}
		/* The installed modules, spelled out: any AddModule line turns
		 * off the every-module default, so the seed names them all. */
		const QStringList binaries =
			QDir(piperModuleBinaryDir()).entryList(QDir::Files);
		QString systemBlock = synthSystemModules(
			QDir(piperSystemModulesDir()).entryList({ "*.conf" }, QDir::Files),
			binaries, piperSystemModulesDir());
		systemBlock += synthSystemModules(
			QDir(piperModulesDir()).entryList({ "*.conf" }, QDir::Files),
			binaries, piperModulesDir());
		return out.write(mergePiperSpeechdConf(existing, systemBlock).toUtf8()) >= 0;
	}

	/* speech-dispatcher loads modules once, at startup: restart it so a
	 * new module takes effect. It respawns on the next utterance. */
	void restartSpeech() {
		QProcess::startDetached("pkill", { "-f", "speech-dispatch[e]r" });
		QTimer::singleShot(2000, this, [this] { relist(); });
	}

	QProcess m_unpack;
};

PANEL("speech", N_("Speech"), false, SpeechPanel);
