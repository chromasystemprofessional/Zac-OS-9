/* The Screen Saver control panel (zacos9-controlpanel screensaver). */
#include "controlpanel.h"

/* "Start after" stops, in minutes. */
static const int SAVER_MINUTES[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static constexpr int SAVER_STEPS = sizeof(SAVER_MINUTES) / sizeof(SAVER_MINUTES[0]);

/* Which screen saver, with a live preview, and when it starts; zacos9-wm
 * starts zacos9-screensaver then (compositor/src/idle.c, lib/savers.c). */
class ScreenSaverPanel : public ControlPanel {
public:
	ScreenSaverPanel() : ControlPanel("Screen Saver", 278) {
		const int g1 = 26, rows = pl_saver_count() + 1;
		const int g1h = 1 + PL_GROUP_MARGIN_TOP + 20 * rows - 8 + PL_GROUP_MARGIN;
		const int g2 = g1 + g1h + 12;
		m_groups = { { g1, g1 + g1h, "Screen Saver" },
			{ g2, g2 + SLIDER_GROUP_H, "Start the screen saver when inactive for" } };
		const int y = g1 + 1 + PL_GROUP_MARGIN_TOP;
		m_which.add("None", QPoint(M + IN, y));
		for (int i = 0; i < pl_saver_count(); i++) {
			m_which.add(QString::fromUtf8(pl_saver_name(i)), QPoint(M + IN, y + 20 * (i + 1)));
		}
		char id[64];
		const bool set = pl_setting("screensaver", id, sizeof(id));
		m_which.selected = !set ? 1 : strcmp(id, "none") == 0 ? 0 : pl_saver_find(id) + 1;
		m_which.changed = [this](int i) {
			pl_setting_set("screensaver", i == 0 ? "none" : pl_saver_id(i - 1));
			m_after.enabled = i != 0;
			restartPreview();
		};
		m_radios.push_back(&m_which);
		m_preview = QRect(W - M - IN - 2 - 160, y - 2, 160, 100);

		int minutes = settingInt("screensaver-after", 5), step = 0;
		for (int i = 0; i < SAVER_STEPS; i++) {
			if (std::abs(SAVER_MINUTES[i] - minutes) < std::abs(SAVER_MINUTES[step] - minutes)) {
				step = i;
			}
		}
		placeSlider(m_after, g2, SAVER_STEPS, step, "1 min", "1 hour");
		m_after.changed = [](int v) { setInt("screensaver-after", SAVER_MINUTES[v]); };
		m_after.enabled = m_which.selected != 0;
		/* Locks once every screen shows it or sleeps, and before the system
		 * sleeps (compositor/src/lock.c, shell/lock/). */
		m_password = std::make_unique<PanelCheckbox>(QStringLiteral("Ask for my password to wake"),
			QPoint(M + 2, g2 + SLIDER_GROUP_H + 12));
		char v[8];
		m_password->on = pl_setting("require-password", v, sizeof(v)) && strcmp(v, "1") == 0;
		m_password->toggled = [](bool on) { pl_setting_set("require-password", on ? "1" : nullptr); };
		m_checks.push_back(m_password.get());
		m_notes = { "It shows until the display sleeps (Nap Time).",
			"A window filling a screen keeps it away from that one." };
		restartPreview();
		m_clock.start();
		m_tick.callOnTimeout([this] { update(m_preview); });
		m_tick.start(40);
		/* Hint Bubbles. */
		m_which.hint = "Chooses what the screen shows while you aren't using the computer.";
		m_after.hint = "Sets how long the computer waits, while you aren't using it, before the screen saver starts.";
		if (m_password) {
			m_password->hint = "Asks for your login password before the screen saver or sleep gives the screen back.";
		}
	}

	~ScreenSaverPanel() override {
		pl_saver_free(m_saver);
	}

protected:
	void paintExtra(pl_canvas *c) override {
		const QRect r = m_preview;
		pl_outline(c, r.left() - 1, r.top() - 1, r.right() + 1, r.bottom() + 1, C_BLACK);
		pl_canvas inner = { c->px + r.top() * c->stride + r.left(), c->stride, 0, 0, r.width(), r.height(), c->log };
		if (m_saver) {
			pl_saver_draw(m_saver, &inner, m_clock.elapsed() / 1000.0);
		} else {
			pl_fill(&inner, 0, 0, r.width() - 1, r.height() - 1, GRAY(0x8));
		}
		if (m_after.value > 0 && m_after.value < SAVER_STEPS - 1) {
			const int m = SAVER_MINUTES[m_after.value];
			Text t(m == 1 ? QStringLiteral("1 minute") : QStringLiteral("%1 minutes").arg(m), 120, PL_FONT_VIEWS);
			pl_text(c, t.t, m_after.pos.x() + (m_after.width - t.inkWidth()) / 2,
				m_after.pos.y() + PL_SLIDER_H + 11, m_after.enabled ? C_BLACK : GRAY(0x8));
		}
	}

private:
	void restartPreview() {
		pl_saver_free(m_saver);
		m_saver = m_which.selected > 0 ?
			pl_saver_new(m_which.selected - 1, m_preview.width(), m_preview.height(), 9) : nullptr;
		m_clock.restart();
	}

	PanelRadios m_which;
	PanelSlider m_after;
	std::unique_ptr<PanelCheckbox> m_password;
	QRect m_preview;
	pl_saver *m_saver = nullptr;
	QElapsedTimer m_clock;
	QTimer m_tick;
};

PANEL("screensaver", "Screen Saver", false, ScreenSaverPanel);
