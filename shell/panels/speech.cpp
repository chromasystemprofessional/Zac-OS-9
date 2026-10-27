/* The Speech control panel (zacos9-controlpanel speech). */
#include "controlpanel.h"

/* Mac OS 9's Speech control panel: the voice and its rate (speech-dispatcher,
 * as Scribble and Talking Alerts use them), and Talking Alerts. */
class SpeechPanel : public ControlPanel {
	static constexpr int VOICE_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + 8 + PL_BUTTON_H + PL_GROUP_MARGIN;
	static constexpr int G0 = 12, G1 = G0 + VOICE_H + 20, G2 = G1 + SLIDER_GROUP_H + 20;
	static constexpr int PANEL_H = G2 + 1 + PL_GROUP_MARGIN_TOP + PL_CHECKBOX_SIZE + PL_GROUP_MARGIN + 16;

public:
	SpeechPanel() : ControlPanel("Speech", PANEL_H) {
		m_groups = { { G0, G0 + VOICE_H, "Voice" }, { G1, G1 + SLIDER_GROUP_H, "Rate" },
			{ G2, G2 + 1 + PL_GROUP_MARGIN_TOP + PL_CHECKBOX_SIZE + PL_GROUP_MARGIN, "Talking Alerts" } };
		m_voice.rect = QRect(M + IN, G0 + 1 + PL_GROUP_MARGIN_TOP, W - 2 * M - 2 * IN - 4, PL_POPUP_H);
		m_voice.items = { PopupItem("Reading voices…", false) };
		m_voice.enabled = false;
		m_voice.chosen = [this](int row) {
			if (row >= 0 && row < m_voices.size()) {
				m_voice.selected = row;
				pl_setting_set("speech-voice", row == 0 ? nullptr : m_voices[row].toUtf8().constData());
				update();
			}
		};
		m_try = PanelButton("Try Voice", QRect(W - M - IN - 4 - 96, G0 + 1 + PL_GROUP_MARGIN_TOP +
			PL_POPUP_H + 8, 96, PL_BUTTON_H));
		m_try.clicked = [] { pl_speak("Hello. This is how I sound."); };
		/* -100..100 in nine steps of 25; the middle is speech-dispatcher's own. */
		placeSlider(m_rate, G1, 9, pl_speech_rate() / 25 + 4, "Slow", "Fast");
		m_rate.changed = [](int step) {
			pl_setting_set("speech-rate", step == 4 ? nullptr
				: QByteArray::number((step - 4) * 25).constData());
		};
		m_alerts = PanelCheckbox("Speak the text of alerts", QPoint(M + IN, G2 + 1 + PL_GROUP_MARGIN_TOP));
		m_alerts.on = pl_talking_alerts();
		m_alerts.toggled = [](bool on) { pl_setting_set("talking-alerts", on ? "true" : nullptr); };
		m_checks.push_back(&m_alerts);

		/* Hint Bubbles. */
		m_hintRects.push_back({ &m_voice.rect, "Chooses the voice ZacOS speaks with." });
		m_hintRects.push_back({ &m_try.rect, "Speaks a sentence in the chosen voice and rate." });
		m_rate.hint = "Sets how fast the voice speaks.";
		m_alerts.hint = "When an alert isn't answered after a moment, it speaks its message.";

		readVoices();
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_voice.paint(c);
		m_try.paint(c);
		if (!m_error.isEmpty()) {
			panelText(c, m_error, M, height() - 6, PL_FONT_VIEWS, GRAY(0x5), W - 2 * M);
		}
	}
	bool pressExtra(QPoint pos, bool) override {
		return m_voice.press(this, pos) || m_try.press(pos);
	}
	void moveExtra(QPoint pos) override {
		if (m_try.move(pos)) {
			update();
		}
	}
	void releaseExtra(QPoint pos) override {
		m_try.release(pos);
	}

private:
	PanelPopup m_voice;
	PanelButton m_try;
	PanelSlider m_rate;
	PanelCheckbox m_alerts;
	QStringList m_voices; /* [0] is the default ("") */
	QString m_error;
	QProcess m_list;

	/* `spd-say -L`: a heading, then NAME LANGUAGE VARIANT lines. Never
	 * waitForFinished (CLAUDE.md). */
	void readVoices() {
		QObject::connect(&m_list, &QProcess::finished, this, [this] {
			const QStringList lines = QString::fromUtf8(m_list.readAllStandardOutput()).split('\n');
			fillVoices(lines.mid(1));
		});
		QObject::connect(&m_list, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				m_error = "Speech needs speech-dispatcher, which isn't installed.";
				fillVoices({});
			}
		});
		m_list.start("spd-say", { "-L" });
	}

	void fillVoices(const QStringList &lines) {
		char current[96] = "";
		pl_setting("speech-voice", current, sizeof(current));
		m_voices = { QString() };
		m_voice.items = { PopupItem("Default voice") };
		m_voice.selected = 0;
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
		for (const Voice &v : all) {
			if (any && v.language.section('-', 0, 0) != language && v.name != QString::fromUtf8(current)) {
				continue;
			}
			m_voices << v.name;
			m_voice.items.emplace_back(v.name);
			if (v.name == QString::fromUtf8(current)) {
				m_voice.selected = m_voices.size() - 1;
			}
		}
		m_voice.enabled = true;
		update();
	}
};

PANEL("speech", "Speech", false, SpeechPanel);
