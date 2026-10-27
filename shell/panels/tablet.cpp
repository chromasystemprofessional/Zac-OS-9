/* The Tablet control panel (zacos9-controlpanel tablet). */
#include "controlpanel.h"

/* What zacos9-wm publishes in zacos9-outputs: the screens, the tablets. */
struct TabletInfo {
	struct Screen {
		QString name;
		QSize px;
	};
	QList<Screen> screens;
	QStringList tablets;

	static TabletInfo read() {
		TabletInfo info;
		QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
			qEnvironmentVariable("WAYLAND_DISPLAY"));
		if (!f.open(QIODevice::ReadOnly)) {
			return info;
		}
		for (const QByteArray &line : f.readAll().split('\n')) {
			const QList<QByteArray> w = line.split(' ');
			if (w.value(0) == "output" && !w.value(1).isEmpty()) {
				const QList<QByteArray> wh = w.value(2).split('x');
				info.screens << Screen { QString::fromUtf8(w.value(1)),
					QSize(wh.value(0).toInt(), wh.value(1).toInt()) };
			} else if (line.startsWith("tablet ")) {
				info.tablets << QString::fromUtf8(line.mid(7));
			}
		}
		return info;
	}
};

static constexpr int RADIO_STEP = 20;

/* Which screen a graphics tablet covers; zacos9-wm maps it
 * (compositor/src/tablet.c): "tablet-display" is a screen's name or
 * "all", unset the main display. */
class TabletPanel : public ControlPanel {
public:
	TabletPanel() : TabletPanel(TabletInfo::read()) {}

protected:
	void paintExtra(pl_canvas *c) override {
		const QStringList lines = m_info.tablets.isEmpty() ?
			QStringList { "No tablet is connected." } : m_info.tablets;
		int y = G1 + 1 + PL_GROUP_MARGIN_TOP + 10;
		for (const QString &line : lines) {
			Text t(line, W - 2 * M - 2 * IN, PL_FONT_VIEWS);
			pl_text(c, t.t, M + IN, y, C_BLACK);
			y += 14;
		}
	}

private:
	static constexpr int G1 = 26;

	explicit TabletPanel(const TabletInfo &info)
		: ControlPanel("Tablet", heightFor(info)), m_info(info) {
		const int g1Bottom = G1 + tabletGroupHeight(m_info);
		const int g2 = g1Bottom + 12;
		const int choices = m_info.screens.size() + 2;
		const int g3 = g2 + choicesGroupHeight(m_info) + 12;
		m_groups = { { G1, g1Bottom, "Connected Tablet" },
			{ g2, g2 + choicesGroupHeight(m_info), "Map the Tablet To" },
			{ g3, g3 + HAND_GROUP_H, "Orientation" } };

		/* The main display, each screen by name, every screen together. */
		int y = g2 + 1 + PL_GROUP_MARGIN_TOP;
		m_screen.add("The main display (with the menu bar)", QPoint(M + IN, y));
		for (const TabletInfo::Screen &s : m_info.screens) {
			y += RADIO_STEP;
			m_screen.add(QStringLiteral("%1  (%2 x %3)").arg(s.name).arg(s.px.width())
				.arg(s.px.height()), QPoint(M + IN, y));
		}
		m_screen.add("All screens together", QPoint(M + IN, y + RADIO_STEP));

		char v[128];
		const QString chosen = pl_setting("tablet-display", v, sizeof(v)) ?
			QString::fromUtf8(v) : QString();
		m_screen.selected = 0; /* unset, or a screen not connected now: the main one */
		if (chosen == "all") {
			m_screen.selected = choices - 1;
		}
		for (int i = 0; i < m_info.screens.size(); i++) {
			if (m_info.screens[i].name == chosen) {
				m_screen.selected = i + 1;
			}
		}
		m_screen.changed = [this, choices](int i) {
			if (i == 0) {
				pl_setting_set("tablet-display", nullptr);
			} else if (i == choices - 1) {
				pl_setting_set("tablet-display", "all");
			} else {
				pl_setting_set("tablet-display", m_info.screens[i - 1].name.toUtf8().constData());
			}
		};
		m_radios.push_back(&m_screen);

		/* Left-handed: the tablet turned round, its area rotated a half turn
		 * by zacos9-wm (libinput's left-handed mode). */
		const int hy = g3 + 1 + PL_GROUP_MARGIN_TOP;
		m_hand.add("ExpressKeys on the left (right-handed)", QPoint(M + IN, hy));
		m_hand.add("ExpressKeys on the right (left-handed)", QPoint(M + IN, hy + RADIO_STEP));
		m_hand.selected = settingInt("tablet-left-handed", 0) == 1 ? 1 : 0;
		m_hand.changed = [](int i) {
			if (i == 0) {
				pl_setting_set("tablet-left-handed", nullptr);
			} else {
				setInt("tablet-left-handed", 1);
			}
		};
		m_radios.push_back(&m_hand);
		m_notes = { "The whole tablet covers the screen chosen here." };
	}

	static int groupHeight(int content) {
		return 1 + PL_GROUP_MARGIN_TOP + content + PL_GROUP_MARGIN;
	}
	static int tabletGroupHeight(const TabletInfo &info) {
		return groupHeight(14 * std::max<int>(1, info.tablets.size()) - 2);
	}
	static int choicesGroupHeight(const TabletInfo &info) {
		return groupHeight(RADIO_STEP * (info.screens.size() + 1) + 12);
	}
	static constexpr int HAND_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + RADIO_STEP + 12 + PL_GROUP_MARGIN;
	static int heightFor(const TabletInfo &info) {
		return G1 + tabletGroupHeight(info) + 12 + choicesGroupHeight(info) + 12 + HAND_GROUP_H +
			12 + 12 + 8;
	}

	TabletInfo m_info;
	PanelRadios m_screen;
	PanelRadios m_hand;
};

PANEL("tablet", "Tablet", false, TabletPanel);
