/* The Sound control panel (zacos9-controlpanel sound). */
#include "controlpanel.h"

#include "customthemes.h"

/* The alert sound, stored as the Appearance panel stores it: the built-in
 * Chime is no setting at all. */
static QString alertSoundSetting() {
	char value[64];
	return pl_setting("alert-sound", value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

static void setAlertSoundSetting(const QString &id) {
	pl_setting_set("alert-sound", id == "platinum" ? nullptr : id.toUtf8().constData());
}

/* PulseAudio or PipeWire's pulse server: output and input selection, volume
 * and mute, plus the independent alert volume. */
class SoundPanel : public ControlPanel {
public:
	SoundPanel() : ControlPanel("Sound", PANEL_H), m_sound(this) {
		const int g0 = 26, g1 = g0 + OUTPUT_GROUP_H + 12 + 16;
		const int g2 = g1 + VOLUME_GROUP_H + 12 + 16 + 20;
		const int g3 = g2 + SLIDER_GROUP_H + 12 + 16;
		const int g4 = g3 + ALERT_SOUND_GROUP_H + 12 + 16;
		m_groups = { { g0, g0 + OUTPUT_GROUP_H, "Sound Devices" },
			{ g1, g1 + VOLUME_GROUP_H, "Volume" },
			{ g2, g2 + SLIDER_GROUP_H, "Alert Volume" },
			{ g3, g3 + ALERT_SOUND_GROUP_H, "Alert Sound" },
			{ g4, g4 + INPUT_GROUP_H, "Sound Input" } };
		m_output.rect = QRect(M + IN + 80, g0 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 84, PL_POPUP_H);
		m_output.label = "Output:";
		m_output.items = { PopupItem("Reading sound outputs...", false) };
		m_output.enabled = false;
		m_output.chosen = [this](int row) {
			if (row < 0 || row >= m_sound.outputs.size()) {
				return;
			}
			m_error.clear();
			m_sound.selectOutput(m_sound.outputs[row]);
		};
		m_input.rect = QRect(M + IN + 80,
			g0 + 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + 6,
			W - 2 * M - 2 * IN - 84, PL_POPUP_H);
		m_input.label = "Input:";
		m_input.items = { PopupItem("Reading sound inputs...", false) };
		m_input.enabled = false;
		m_input.chosen = [this](int row) {
			if (row < 0 || row >= m_sound.inputs.size()) {
				return;
			}
			m_error.clear();
			m_sound.selectInput(m_sound.inputs[row]);
		};

		/* Volume in eight steps, 0..7, as on the Mac. */
		placeSlider(m_volume, g1, 8, 0, "Quiet", "Loud");
		m_volume.enabled = false;
		m_volume.changed = [this](int v) {
			m_error.clear();
			m_volumeFeedbackReady = false;
			m_sound.setVolume(m_currentSink, v * 100 / 7);
		};
		m_sound.volumeApplied = [this] {
			if (m_volume.dragging) {
				m_volumeFeedbackReady = true;
			} else {
				pl_beep();
			}
		};
		m_mute = PanelCheckbox("Mute", QPoint(M + IN, g1 + SLIDER_GROUP_H + 2));
		m_mute.enabled = false;
		m_mute.toggled = [this](bool on) {
			m_error.clear();
			m_sound.setMuted(m_currentSink, on);
		};
		m_checks.push_back(&m_mute);
		placeSlider(m_balance, g1 + SLIDER_GROUP_H + 20, 11, 5, "Left", "Right");
		m_balance.changed = [this](int value) {
			if (m_currentOutput.stereo) {
				m_error.clear();
				m_sound.setBalance(m_currentOutput, m_volume.value * 100 / 7, value * 10);
			}
		};

		placeSlider(m_alert, g2, 8, settingInt("alert-volume", 7), "Off", "Loud");
		m_alert.changed = [](int v) {
			setInt("alert-volume", v);
		};

		/* Which sound an alert plays: Chime, the sounds on this computer,
		 * the ones added, and None -- the same list the Appearance panel
		 * shows. It is read again whenever the menu opens, so a recording
		 * dropped into the System Folder's Sounds is offered without
		 * reopening the panel. */
		m_alertSound.rect = QRect(M + IN + 80, g3 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 84, PL_POPUP_H);
		m_alertSound.label = "Sound:";
		loadAlertSounds();
		m_alertSound.chosen = [this](int row) {
			if (row < 0 || row >= m_alertIds.size()) {
				return;
			}
			const QString id = m_alertIds[row];
			setAlertSoundSetting(id);
			pl_sound_play(id.toUtf8().constData());
			update();
		};
		m_sound.changed = [this] { syncOutputs(); };
		m_sound.failed = [this](const QString &error) {
			m_error = error;
			syncOutputs();
		};
		m_refresh.callOnTimeout([this] { m_sound.refresh(); });
		m_refresh.start(2000);
		m_sound.refresh();
		/* Hint Bubbles. */
		m_volume.hint = "Sets how loud the computer plays sound.";
		m_balance.hint = "Sets how much sound comes from the left and right speakers.";
		m_alert.hint = "Sets how loud alert sounds are, compared with other sound.";
		m_hintRects.push_back({ &m_alertSound.rect,
			"Chooses the sound the computer plays for an alert." });
		m_mute.hint = "Turns all sound off. Click again to turn it back on.";
		m_hintRects.push_back({ &m_output.rect, "Chooses the speakers or headphones that play sound." });
		m_hintRects.push_back({ &m_input.rect, "Chooses the microphone or line input that records sound." });
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_output.paint(c);
		m_input.paint(c);
		m_alertSound.paint(c);
	}

	bool pressExtra(QPoint p, bool) override {
		m_focus = nullptr;
		/* Before the menu opens: the Sounds folder may have gained or
		 * lost a recording since the panel was opened. */
		loadAlertSounds();
		return m_output.press(this, p) || m_input.press(this, p) || m_alertSound.press(this, p);
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		const bool alert = m_alert.dragging;
		const bool volume = m_volume.dragging && m_volumeFeedbackReady;
		ControlPanel::mouseReleaseEvent(e);
		m_volumeFeedbackReady = false;
		if (alert || volume) {
			pl_beep();
		}
	}

private:
	void loadAlertSounds() {
		const QString stored = alertSoundSetting();
		const QString chosen = stored.isEmpty() ? QStringLiteral("platinum") : stored;
		m_alertIds = alertSoundIds();
		m_alertSound.items.clear();
		m_alertSound.selected = 0;
		for (int i = 0; i < m_alertIds.size(); i++) {
			m_alertSound.items.emplace_back(alertSoundDisplayName(m_alertIds[i]));
			if (m_alertIds[i] == chosen) {
				m_alertSound.selected = i;
			}
		}
	}

	void syncOutputs() {
		m_output.items.clear();
		int selected = -1;
		for (int i = 0; i < m_sound.outputs.size(); i++) {
			const SoundOutput &output = m_sound.outputs[i];
			m_output.items.emplace_back(output.label);
			if (output.current) {
				selected = i;
			}
		}
		if (m_output.items.empty()) {
			m_output.items.emplace_back("No sound outputs available", false);
		} else if (selected < 0) {
			m_output.items.emplace_back("Choose a sound output", false);
		}
		m_output.selected = selected < 0 ? static_cast<int>(m_output.items.size()) - 1 : selected;
		m_output.enabled = !m_sound.switching && !m_sound.outputs.isEmpty();
		m_input.items.clear();
		int selectedInput = -1;
		for (int i = 0; i < m_sound.inputs.size(); i++) {
			const SoundInput &input = m_sound.inputs[i];
			m_input.items.emplace_back(input.label);
			if (input.current) {
				selectedInput = i;
			}
		}
		if (m_input.items.empty()) {
			m_input.items.emplace_back("No sound inputs available", false);
		} else if (selectedInput < 0) {
			m_input.items.emplace_back("Choose a sound input", false);
		}
		m_input.selected = selectedInput < 0 ? static_cast<int>(m_input.items.size()) - 1 : selectedInput;
		m_input.enabled = !m_sound.inputSwitching && !m_sound.inputs.isEmpty();
		m_volume.enabled = m_mute.enabled = !m_sound.switching && selected >= 0;
		m_balance.enabled = !m_sound.switching && selected >= 0 && m_sound.outputs[selected].stereo;
		m_currentSink.clear();
		if (selected >= 0) {
			const SoundOutput &output = m_sound.outputs[selected];
			m_currentOutput = output;
			m_currentSink = output.sink;
			if (!m_volume.dragging) {
				m_volume.value = (output.volume * 7 + 50) / 100;
			}
			if (!m_balance.dragging) {
				m_balance.value = (output.balance + 5) / 10;
			}
			if (!m_mute.down) {
				m_mute.on = output.muted;
			}
		}
		const QString note = !m_error.isEmpty() ? m_error
			: m_sound.switching ? "Changing sound output..."
			: m_sound.outputs.isEmpty() ? "No sound outputs are available."
			: "Choose a sound output and an input source.";
		m_notes.clear();
		const QStringList lines = panelWrap(note, W - 2 * M, PL_FONT_VIEWS);
		for (const QString &line : lines.mid(0, 3)) {
			m_notes.push_back(line);
		}
		update();
	}

	SoundClient m_sound;
	QTimer m_refresh;
	PanelPopup m_output, m_input;
	PanelPopup m_alertSound;
	QStringList m_alertIds;
	QString m_currentSink, m_error;
	bool m_volumeFeedbackReady = false;
	PanelSlider m_volume, m_balance, m_alert;
	PanelCheckbox m_mute;
	SoundOutput m_currentOutput;
	static constexpr int OUTPUT_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + 2 * PL_POPUP_H + 6 +
		PL_GROUP_MARGIN;
	static constexpr int INPUT_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + PL_GROUP_MARGIN;
	static constexpr int G0 = 26, G1 = G0 + OUTPUT_GROUP_H + 12 + 16;
	static constexpr int VOLUME_GROUP_H = 2 * SLIDER_GROUP_H + 20;
	static constexpr int G2 = G1 + VOLUME_GROUP_H + 12 + 16 + 20;
	static constexpr int G3 = G2 + SLIDER_GROUP_H + 12 + 16;
	static constexpr int ALERT_SOUND_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H +
		PL_GROUP_MARGIN;
	static constexpr int G4 = G3 + ALERT_SOUND_GROUP_H + 12 + 16;
	static constexpr int PANEL_H = G4 + INPUT_GROUP_H + 12 + 3 * 12 + 8;
};

PANEL("sound", "Sound", false, SoundPanel);
