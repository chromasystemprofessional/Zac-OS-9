#include "appearance.h"

#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>

#include "patterns.h"
#include "settings.h"

/* The window, its tab control, and the layout inside a pane following
 * the HIG's group box measurements (figure 3-29). */
static constexpr int W = 380, H = 262;
static constexpr int MARGIN = 10;
static constexpr int TABS_Y = 10;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr uint32_t PANE = GRAY(0xE);
static constexpr int PANE_TOP = TABS_Y + PL_TAB_H;     /* the pane's top line */
static constexpr int BOX_L = MARGIN + 3 + PL_GROUP_MARGIN; /* group boxes inside the pane */
static constexpr int BOX_R = W - MARGIN - 3 - PL_GROUP_MARGIN - 2;
static constexpr int BOX_TOP = PANE_TOP + 3 + 16;     /* room for the title above */
static constexpr int ITEM_TOP = BOX_TOP + 1 + PL_GROUP_MARGIN_TOP;
static constexpr int ITEM_INSET = 1 + PL_GROUP_MARGIN;
static constexpr int MID = (BOX_L + BOX_R) / 2;

static QString setting(const char *key) {
	char v[128];
	return pl_setting(key, v, sizeof(v)) ? QString::fromUtf8(v) : QString();
}

static void setSetting(const char *key, const QString &value) {
	pl_setting_set(key, value.isEmpty() ? nullptr : value.toUtf8().constData());
}

/* ---- the panel ---------------------------------------------------------------- */

AppearancePanel::AppearancePanel() {
	setWindowTitle("Appearance");
	setFixedSize(W, H);
	for (const char *label : { "Color", "Desktop", "Sound" }) {
		m_tabLabels.push_back(std::make_unique<Text>(label, 200, PL_FONT_SYSTEM));
	}

	/* Accent colour. */
	m_accents.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(MID - 6 - ITEM_INSET, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	QStringList accents;
	for (int i = 0; i < pl_accent_count(); i++) {
		accents << pl_accent_name(i);
	}
	m_accents.setItems(accents);
	m_accents.select(pl_accent_find(setting("accent").toUtf8().constData()), false);
	m_accents.picked = [this](int i) {
		setSetting("accent", i == 0 ? QString() : QString(pl_accent_id(i)));
		update();
	};

	/* Highlight colour: the accent's own light shade, or a fixed colour. */
	m_highlights.frame = QRect(QPoint(MID + 6 + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_R - ITEM_INSET, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	const QList<QPair<QString, uint32_t>> highlights = {
		{ "Accent Color", 0 }, { "Yellow", RGB(0xFF, 0xFF, 0x00) },
		{ "Gold", RGB(0xFF, 0xCC, 0x66) }, { "Orange", RGB(0xFF, 0xCC, 0x99) },
		{ "Green", RGB(0xCC, 0xFF, 0xCC) }, { "Blue", RGB(0xCC, 0xEE, 0xFF) },
		{ "Pink", RGB(0xFF, 0xCC, 0xEE) }, { "Gray", RGB(0xCC, 0xCC, 0xCC) },
	};
	QStringList hlNames;
	const QString current = setting("highlight").toUpper();
	int hlSelected = 0;
	for (int i = 0; i < highlights.size(); i++) {
		hlNames << highlights[i].first;
		m_highlightColors.push_back(highlights[i].second);
		if (highlights[i].second &&
				QString::asprintf("%06X", highlights[i].second & 0xFFFFFF) == current) {
			hlSelected = i;
		}
	}
	m_highlights.setItems(hlNames);
	m_highlights.select(hlSelected, false);
	m_highlights.picked = [this](int i) {
		uint32_t v = m_highlightColors[i];
		setSetting("highlight", v ? QString::asprintf("%06X", v & 0xFFFFFF) : QString());
		update();
	};

	/* Desktop pattern. */
	m_patterns.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	QStringList patterns;
	for (int i = 0; i < pl_pattern_count(); i++) {
		patterns << pl_pattern_name(i);
	}
	m_patterns.setItems(patterns);
	m_patterns.select(pl_pattern_find(setting("pattern").toUtf8().constData()), false);
	m_patterns.picked = [this](int i) {
		setSetting("pattern", i == 0 ? QString() : QString(pl_pattern_id(i)));
		update();
	};

	/* Alert sound: what's in sounds/, plus None. */
	m_sounds.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	QDir dir(QString::fromUtf8(pl_data_dir()) + "/sounds");
	m_soundIds = { "platinum" };
	for (const QString &f : dir.entryList({ "*.wav" }, QDir::Files, QDir::Name)) {
		const QString id = f.chopped(4);
		if (id != "platinum") {
			m_soundIds << id;
		}
	}
	m_soundIds << "none";
	QStringList soundNames;
	for (const QString &id : m_soundIds) {
		/* The default alert sound's file is still platinum.wav (sound
		 * assets, like the fonts and icons, keep their internal names);
		 * shown as "Chime" rather than capitalizing that into the
		 * product's old name. */
		QString name = id == "platinum" ? "Chime" : id;
		name[0] = name[0].toUpper();
		soundNames << name;
	}
	m_sounds.setItems(soundNames);
	const QString chosen = setting("alert-sound");
	m_sounds.select(std::max<int>(0, m_soundIds.indexOf(chosen.isEmpty() ? "platinum" : chosen)),
		false);
	m_sounds.picked = [this](int i) {
		const QString id = m_soundIds[i];
		setSetting("alert-sound", id == "platinum" ? QString() : id);
		pl_sound_play(id.toUtf8().constData());
		update();
	};

	m_focus = &m_accents;
}

void AppearancePanel::showTab(int tab) {
	m_tab = std::clamp(tab, 0, 2);
	m_focus = visibleLists().front();
	update();
}

std::vector<PanelList *> AppearancePanel::visibleLists() {
	switch (m_tab) {
	case 0: return { &m_accents, &m_highlights };
	case 1: return { &m_patterns };
	default: return { &m_sounds };
	}
}

static void paintList(pl_canvas *c, PanelList &l, bool focused) {
	l.paint(c, focused);
}

/* A group box around `inner` (its items), with the HIG's margins. */
static void groupAround(pl_canvas *c, const QRect &inner, const char *title, int right = -1) {
	Text t(title, 300, PL_FONT_SYSTEM);
	const int x1 = right >= 0 ? right : inner.right() + PL_GROUP_MARGIN + 1;
	pl_group_box_paint(c, inner.left() - ITEM_INSET, BOX_TOP, x1,
		inner.bottom() + PL_GROUP_MARGIN + 1, t.t, PANE);
}

void AppearancePanel::paintColorTab(pl_canvas *c) {
	groupAround(c, m_accents.frame, "Accent Color");
	groupAround(c, m_highlights.frame, "Highlight Color");
	paintList(c, m_accents, m_focus == &m_accents);
	paintList(c, m_highlights, m_focus == &m_highlights);

	/* A sample in the chosen colours: a scroll bar and a progress bar. */
	const int top = m_accents.frame.bottom() + PL_GROUP_MARGIN + 2 + 12 + 16;
	Text sample("Sample", 200, PL_FONT_SYSTEM);
	pl_group_box_paint(c, BOX_L, top, BOX_R, top + 1 + PL_GROUP_MARGIN_TOP + 16 + PL_GROUP_MARGIN,
		sample.t, PANE);
	pl_scrollbar sb = { false, 150, true, 40 };
	pl_scrollbar_paint(c, BOX_L + ITEM_INSET, top + 1 + PL_GROUP_MARGIN_TOP, &sb,
		pl_accent_current());
	pl_progress_paint(c, MID + 6 + ITEM_INSET, top + 1 + PL_GROUP_MARGIN_TOP + 2, 140, 0.6,
		pl_accent_current());
}

void AppearancePanel::paintDesktopTab(pl_canvas *c) {
	groupAround(c, m_patterns.frame, "Patterns", BOX_R);
	paintList(c, m_patterns, m_focus == &m_patterns);

	/* The pattern, tiled in a recessed well beside the list. */
	const int size = 112;
	const int px0 = m_patterns.frame.right() + 24, py0 = ITEM_TOP + 1;
	pl_hline(c, px0 - 1, px0 + size, py0 - 1, GRAY(0x8));
	pl_vline(c, px0 - 1, py0 - 1, py0 + size, GRAY(0x8));
	pl_hline(c, px0, px0 + size, py0 + size, C_WHITE);
	pl_vline(c, px0 + size, py0, py0 + size, C_WHITE);
	pl_pattern_fill(c, std::max(0, m_patterns.state.selected), px0, py0, px0 + size - 1,
		py0 + size - 1);
	Text name(m_patterns.items.value(m_patterns.state.selected), size, PL_FONT_VIEWS);
	pl_text(c, name.t, px0 + (size - name.inkWidth()) / 2, py0 + size + 16, C_BLACK);
}

void AppearancePanel::paintSoundTab(pl_canvas *c) {
	groupAround(c, m_sounds.frame, "Alert Sound", BOX_R);
	paintList(c, m_sounds, m_focus == &m_sounds);
	const int x = m_sounds.frame.right() + 20;
	int y = ITEM_TOP + 11;
	for (const char *line : { "Click a sound to hear it.", "It plays whenever an",
			"alert appears." }) {
		Text t(line, BOX_R - x - 4, PL_FONT_VIEWS);
		pl_text(c, t.t, x, y, C_BLACK);
		y += 13;
	}
}

void AppearancePanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	std::vector<const plat_text *> labels;
	for (auto &t : m_tabLabels) {
		labels.push_back(t->t);
	}
	pl_tabs_paint(c, MARGIN, TABS_Y, W - MARGIN - 1, H - MARGIN - 1, labels.data(),
		static_cast<int>(labels.size()), m_tab);
	switch (m_tab) {
	case 0: paintColorTab(c); break;
	case 1: paintDesktopTab(c); break;
	default: paintSoundTab(c); break;
	}
	QPainter p(this);
	px.blit(p);
}

void AppearancePanel::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	std::vector<const plat_text *> labels;
	for (auto &t : m_tabLabels) {
		labels.push_back(t->t);
	}
	const int tab = pl_tabs_hit(MARGIN, TABS_Y, labels.data(), static_cast<int>(labels.size()),
		pos.x(), pos.y());
	if (tab >= 0 && tab != m_tab) {
		m_tab = tab;
		m_focus = visibleLists().front();
		update();
		return;
	}
	for (PanelList *l : visibleLists()) {
		if (l->press(pos)) {
			m_focus = l;
			update();
			return;
		}
	}
}

void AppearancePanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (!m_focus) {
		return;
	}
	if (e->key() == Qt::Key_Tab) {
		/* Tab moves the focus between the lists on this pane. */
		auto lists = visibleLists();
		auto it = std::find(lists.begin(), lists.end(), m_focus);
		m_focus = (it == lists.end() || it + 1 == lists.end()) ? lists.front() : *(it + 1);
	} else if (!m_focus->key(e->key(), e->text())) {
		return;
	}
	update();
}
