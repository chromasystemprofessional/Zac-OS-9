/* The Energy Saver control panel (zacos9-controlpanel energysaver). */
#include "controlpanel.h"

/* The slider's stops, in minutes; 0 is Never. */
static const int SLEEP_MINUTES[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120, 0 };
static constexpr int SLEEP_STEPS = sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0]);

static int sleepStep(int minutes) {
	if (minutes <= 0) {
		return SLEEP_STEPS - 1;
	}
	int best = 0;
	for (int i = 0; i < SLEEP_STEPS - 1; i++) {
		if (std::abs(SLEEP_MINUTES[i] - minutes) < std::abs(SLEEP_MINUTES[best] - minutes)) {
			best = i;
		}
	}
	return best;
}

static QString sleepText(int step) {
	const int m = SLEEP_MINUTES[step];
	return m == 0 ? QStringLiteral("Never") : m == 1 ? QStringLiteral("1 minute") :
		m < 60 || m % 60 ? QStringLiteral("%1 minutes").arg(m) :
		m == 60 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(m / 60);
}

/* When the display and the system sleep; zacos9-wm applies them
 * (compositor/src/idle.c). */
class EnergySaverPanel : public ControlPanel {
public:
	static constexpr int SCHEDULE_H = 1 + PL_GROUP_MARGIN_TOP + 2 * (PL_POPUP_H + 6) + PL_BUTTON_H + PL_GROUP_MARGIN;

	EnergySaverPanel() : ControlPanel("Nap Time", 202 + SCHEDULE_H + 20) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12, g3 = g2 + SLIDER_GROUP_H + 20;
		m_groups = { { g1, g1 + SLIDER_GROUP_H, "Put the display to sleep when inactive for" },
			{ g2, g2 + SLIDER_GROUP_H, "Put the system to sleep when inactive for" },
			{ g3, g3 + SCHEDULE_H, "Schedule" } };
		setupSchedule(g3 + 1 + PL_GROUP_MARGIN_TOP);
		placeSlider(m_display, g1, SLEEP_STEPS, sleepStep(settingInt("display-sleep", 10)),
			"1 min", "Never");
		m_display.changed = [](int v) { setInt("display-sleep", SLEEP_MINUTES[v]); };
		placeSlider(m_system, g2, SLEEP_STEPS, sleepStep(settingInt("system-sleep", 0)),
			"1 min", "Never");
		m_system.changed = [](int v) { setInt("system-sleep", SLEEP_MINUTES[v]); };
		m_notes = { "A window filling a screen keeps that screen awake,",
			"and the system too. Any key or the mouse wakes them." };
		/* Hint Bubbles. */
		m_display.hint = "Sets how long the computer waits, while you aren't using it, before it turns the screen off.";
		m_system.hint = "Sets how long the computer waits, while you aren't using it, before it goes to sleep.";
	}

protected:
	void paintExtra(pl_canvas *c) override {
		paintSchedule(c);
		/* The chosen time, between the slider's end labels (at an end,
		 * that end's label says it already). */
		for (const PanelSlider *s : { &m_display, &m_system }) {
			if (s->value == 0 || s->value == SLEEP_STEPS - 1) {
				continue;
			}
			Text t(sleepText(s->value), 120, PL_FONT_VIEWS);
			pl_text(c, t.t, s->pos.x() + (s->width - t.inkWidth()) / 2,
				s->pos.y() + PL_SLIDER_H + 11, C_BLACK);
		}
	}

	bool pressExtra(QPoint pos, bool) override {
		for (Row &r : m_rows) {
			if (r.days.press(this, pos) || r.hour.press(this, pos) || r.minute.press(this, pos)) {
				return true;
			}
		}
		return m_set.press(pos);
	}
	void moveExtra(QPoint pos) override {
		if (m_set.move(pos)) {
			update();
		}
	}
	void releaseExtra(QPoint pos) override {
		m_set.release(pos);
	}

private:
	PanelSlider m_display, m_system;
	/* Nap Time's schedule (/etc/zacos9/schedule, through
	 * zacos9-accounts-helper): start up and shut down, on chosen days at
	 * a chosen time. */
	struct Row {
		PanelCheckbox on;
		PanelPopup days, hour, minute;
	};
	Row m_rows[2];
	PanelButton m_set;
	QString m_scheduleError;

	void setupSchedule(int y) {
		const char *names[2] = { "Start up", "Shut down" };
		const char *keys[2] = { "startup", "shutdown" };
		QFile f(qEnvironmentVariable("ZACOS9_ACCOUNTS_CONF", "/etc/zacos9") + "/schedule");
		const QString saved = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
		for (int i = 0; i < 2; i++) {
			Row &r = m_rows[i];
			const int ry = y + i * (PL_POPUP_H + 6);
			r.on = PanelCheckbox(names[i], QPoint(M + IN, ry + 4));
			r.days.rect = QRect(M + IN + 92, ry, 100, PL_POPUP_H);
			r.days.items = { PopupItem("Every Day"), PopupItem("Weekdays"), PopupItem("Weekends") };
			r.hour.rect = QRect(M + IN + 200, ry, 56, PL_POPUP_H);
			for (int h = 0; h < 24; h++) {
				r.hour.items.emplace_back(QString::asprintf("%02d", h));
			}
			r.minute.rect = QRect(M + IN + 262, ry, 56, PL_POPUP_H);
			for (int m = 0; m < 60; m += 5) {
				r.minute.items.emplace_back(QString::asprintf("%02d", m));
			}
			r.hour.selected = i == 0 ? 7 : 23;
			r.minute.selected = 0;
			r.days.selected = i == 0 ? 1 : 0;
			/* "startup weekdays 07:30" */
			for (const QString &line : saved.split('\n')) {
				const QStringList f = line.split(' ');
				if (f.size() == 3 && f[0] == keys[i]) {
					r.on.on = true;
					r.days.selected = f[1] == "weekdays" ? 1 : f[1] == "weekends" ? 2 : 0;
					r.hour.selected = f[2].section(':', 0, 0).toInt();
					r.minute.selected = f[2].section(':', 1).toInt() / 5;
				}
			}
			for (PanelPopup *p : { &r.days, &r.hour, &r.minute }) {
				p->chosen = [this, p](int row) { p->selected = row; update(); };
			}
			m_checks.push_back(&r.on);
		}
		m_rows[0].on.hint = "Turns the computer on by itself at the time chosen, if it is shut down "
			"(not every computer can).";
		m_rows[1].on.hint = "Shuts the computer down by itself at the time chosen. Save your work first.";
		m_set = PanelButton("Set Schedule", QRect(W - M - IN - 4 - 110, y + 2 * (PL_POPUP_H + 6), 110, PL_BUTTON_H));
		m_set.hint = "Saves the schedule. An administrator's password may be asked for.";
		m_set.clicked = [this] { saveSchedule(); };
	}

	void saveSchedule() {
		const char *keys[2] = { "startup", "shutdown" };
		const char *days[3] = { "daily", "weekdays", "weekends" };
		QByteArray input;
		for (int i = 0; i < 2; i++) {
			const Row &r = m_rows[i];
			if (r.on.on) {
				input += QString::asprintf("%s %s %02d:%02d\n", keys[i], days[r.days.selected],
					r.hour.selected, r.minute.selected * 5).toUtf8();
			}
		}
		bool ok = false;
		QString error;
		runPrivilegedHelper("accounts", { "schedule" }, input, &ok, &error);
		m_scheduleError = ok ? QString("Schedule saved.") : error;
		update();
	}

	void paintSchedule(pl_canvas *c) {
		for (Row &r : m_rows) {
			r.days.enabled = r.hour.enabled = r.minute.enabled = r.on.on;
			r.days.paint(c);
			r.hour.paint(c);
			r.minute.paint(c);
		}
		m_set.paint(c);
		if (!m_scheduleError.isEmpty()) {
			panelText(c, m_scheduleError, M + IN, m_set.rect.y() + 14, PL_FONT_VIEWS, GRAY(0x5),
				m_set.rect.x() - M - IN - 8);
		}
	}
};

PANEL("energysaver", "Nap Time", false, EnergySaverPanel);
