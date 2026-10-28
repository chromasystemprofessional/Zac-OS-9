/* The Monitors control panel (zacos9-controlpanel monitors). */
#include "controlpanel.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QFont>
#include <QTimer>
#include <QVBoxLayout>
#include <LayerShellQt/Window>

/* One screen, as zacos9-wm publishes it. Positions and sizes of the screens in the
 * layout are in logical pixels (the screen's pixels divided by the pixel size). */
struct Screen {
	QString name;
	QSize px;
	double scale = 1;
	bool nested = false;
	QPoint pos;
	bool main = false;
	QList<QSize> modes;

	QSize logical() const {
		const double s = std::max(scale, 1.0);
		return QSize(qRound(px.width() / s), qRound(px.height() / s));
	}
	QRect rect() const { return QRect(pos, logical()); }
};

/* A large number shown on a screen while the Monitors panel is open, so
 * the person can tell which screen is which. Uses a layer surface so the
 * compositor places it on the correct screen. */
class ScreenNumberOverlay : public QWidget {
public:
	ScreenNumberOverlay(int number, QScreen *screen)
		: QWidget(nullptr), m_number(number) {
		setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_ShowWithoutActivating);
		/* The same dance as the secondary desktops (secondarydesktop.cpp):
		 * create the native window, put it on the screen, then make it a
		 * layer surface. The scope is deliberately not "zacos9-…", so the
		 * compositor leaves the surface where it was put instead of pinning
		 * it to the main display. */
		winId();
		if (screen) {
			windowHandle()->setScreen(screen);
		}
		if (auto *layer = LayerShellQt::Window::get(windowHandle())) {
			layer->setScope("monitors-number");
			layer->setLayer(LayerShellQt::Window::LayerOverlay);
			layer->setAnchors(LayerShellQt::Window::Anchors(
				LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
				LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
			layer->setExclusiveZone(0);
			layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
		}
	}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRect r = rect();
		/* A large, semi-transparent number centred on the screen. */
		QFont f = p.font();
		f.setPointSize(std::max(r.height() / 4, 48));
		f.setBold(true);
		p.setFont(f);
		p.setPen(QColor(0, 0, 0, 180));
		p.drawText(r, Qt::AlignCenter, QString::number(m_number));
	}
private:
	int m_number;
};

class MonitorsPanel : public ControlPanel {
public:
	MonitorsPanel() : ControlPanel("Monitors", 236) {
		readOutputs();
		build();
		/* Hint Bubbles. */
		m_scale.hint = "Sets how big everything on this screen is drawn; each screen has its own. "
			"At 1.25×, 1.5× and 1.75× the pixel art stays sharp, but some of its dots are a screen "
			"pixel bigger than others; at Normal, 2× and 3× every dot is the same.";
		if (m_mirror) {
			m_mirror->hint = "Shows the same picture on every screen.";
		}
		m_hintRects.push_back({ &m_list.frame, "The resolutions this screen can show. Click one to use it." });
	}

protected:
	void hideEvent(QHideEvent *e) override {
		if (m_flashOverlay) {
			m_flashOverlay->hide();
			m_flashOverlay->deleteLater();
			m_flashOverlay = nullptr;
		}
		ControlPanel::hideEvent(e);
	}

	void paintExtra(pl_canvas *c) override {
		m_list.paint(c, true);
		Text pixels("Pixel size:", 200, PL_FONT_SYSTEM);
		pl_text(c, pixels.t, M + IN + 190, m_resTop + 1 + PL_GROUP_MARGIN_TOP + 9, C_BLACK);
		if (multiple()) {
			paintArrangement(c);
		}
	}

	bool pressExtra(QPoint p, bool) override {
		if (multiple() && m_area.contains(p)) {
			return pressArrangement(p);
		}
		return m_list.press(p);
	}

	void moveExtra(QPoint p) override {
		if (m_drag == Drag::None) {
			return;
		}
		m_dragNow = p;
		if (m_drag == Drag::Display) {
			m_dragPos = wantFor(p - m_dragGrab);
			m_dragSnap = snapFor(m_dragPos);
		}
		update();
	}

	void releaseExtra(QPoint p) override {
		if (m_drag == Drag::None) {
			return;
		}
		m_dragNow = p;
		const Drag mode = m_drag;
		m_drag = Drag::None;
		if (mode == Drag::Bar) {
			const int to = displayAt(p);
			if (to >= 0 && !m_displays[to].main) {
				for (Screen &d : m_displays) {
					d.main = false;
				}
				m_displays[to].main = true;
				saveArrangement();
			}
		} else {
			dropDisplay();
		}
		update();
	}

	void keyExtra(QKeyEvent *e) override {
		if (m_list.key(e->key(), e->text())) {
			update();
		}
	}

private:
	enum class Drag { None, Display, Bar };

	ScreenNumberOverlay *m_flashOverlay = nullptr;

	void flashScreenNumber(int displayIndex) {
		if (!multiple() || displayIndex < 0 || displayIndex >= m_displays.size()) {
			return;
		}
		if (m_flashOverlay) {
			m_flashOverlay->hide();
			m_flashOverlay->deleteLater();
			m_flashOverlay = nullptr;
		}
		/* Match the screen by its output name first, then by geometry
		 * (the compositor publishes each screen's position and size in
		 * logical pixels), then by index. This ensures the flash appears
		 * on the correct physical screen, not always the primary one. */
		QScreen *screen = nullptr;
		const Screen &d = m_displays[displayIndex];

		/* 1. Match by name. */
		for (QScreen *s : QGuiApplication::screens()) {
			if (s->name() == d.name) {
				screen = s;
				break;
			}
		}

		/* 2. Match by geometry: the compositor's pos/size should match
		 * the QScreen's geometry (possibly scaled). */
		if (!screen) {
			for (QScreen *s : QGuiApplication::screens()) {
				const QRect g = s->geometry();
				/* Compare logical positions (compositor uses logical pixels). */
				if (g.x() == d.pos.x() && g.y() == d.pos.y()) {
					screen = s;
					break;
				}
			}
		}

		/* 3. Match by size (logical). */
		if (!screen) {
			for (QScreen *s : QGuiApplication::screens()) {
				const QSize logSize = d.logical();
				if (s->size() == logSize || s->size() == d.px) {
					screen = s;
					break;
				}
			}
		}

		/* 4. Fall back to index. */
		if (!screen) {
			const QList<QScreen *> screens = QGuiApplication::screens();
			if (displayIndex < screens.size()) {
				screen = screens.value(displayIndex);
			}
		}

		/* 5. Last resort: primary screen. */
		if (!screen) {
			screen = QGuiApplication::primaryScreen();
		}

		if (!screen) {
			return;
		}
		m_flashOverlay = new ScreenNumberOverlay(displayIndex + 1, screen);
		m_flashOverlay->show();
		QTimer::singleShot(1400, this, [this]() {
			if (m_flashOverlay) {
				m_flashOverlay->hide();
				m_flashOverlay->deleteLater();
				m_flashOverlay = nullptr;
			}
		});
	}

	bool multiple() const { return m_displays.size() > 1; }
	static bool everyBar() {
		char v[8];
		return pl_setting("menubar-every-display", v, sizeof(v)) && strcmp(v, "1") == 0;
	}
	const Screen *selected() const { return m_displays.isEmpty() ? nullptr : &m_displays[m_sel]; }

	void build() {
		const int g0 = 26;
		int resTop = g0;
		const int listH = 7 * PL_LIST_ROW_H + 1;
		int height;
		m_groups.clear();
		m_checks.clear();
		if (multiple()) {
			m_area = QRect(M + IN, g0 + 1 + PL_GROUP_MARGIN_TOP, W - 2 * M - 2 * IN - 2, 92);
			const int arrBottom = m_area.bottom() + 8 + 18 + 14 + PL_GROUP_MARGIN;
			m_groups.push_back({ g0, arrBottom, "Arrangement" });
			m_mirror = std::make_unique<PanelCheckbox>("Mirror Displays",
				QPoint(M + IN, m_area.bottom() + 8));
			m_mirror->on = m_mirrored;
			m_mirror->toggled = [this](bool on) {
				pl_setting_set("mirror", on ? "1" : "0");
				refreshSoon();
			};
			m_checks.push_back(m_mirror.get());
			m_everyBar = std::make_unique<PanelCheckbox>("Menu Bar on Every Display",
				QPoint(M + IN + 130, m_area.bottom() + 8));
			m_everyBar->on = everyBar();
			m_everyBar->hint = "Shows the menu bar at the top of every screen, not only the main one. "
				"Its menus open on the screen whose menu bar you click.";
			m_everyBar->toggled = [this](bool on) {
				pl_setting_set("menubar-every-display", on ? "1" : "0");
				update();
			};
			m_checks.push_back(m_everyBar.get());
			m_mainDisplay = std::make_unique<PanelCheckbox>("Main Display",
				QPoint(M + IN, m_area.bottom() + 8 + 18));
			m_mainDisplay->on = selected() ? selected()->main : false;
			m_mainDisplay->hint = "Where the desktop icons live. The menu bar's home is here too.";
			m_mainDisplay->toggled = [this](bool on) {
				if (on) {
					for (Screen &d : m_displays) {
						d.main = false;
					}
					if (m_sel < m_displays.size()) {
						m_displays[m_sel].main = true;
					}
					saveArrangement();
				}
				/* There is always one main display; keep the box truthful. */
				m_mainDisplay->on = m_sel < m_displays.size() && m_displays[m_sel].main;
				update();
			};
			m_checks.push_back(m_mainDisplay.get());
			resTop = arrBottom + 12;
		}
		m_resTop = resTop;
		const int resBottom = resTop + 1 + PL_GROUP_MARGIN_TOP + listH + PL_GROUP_MARGIN;
		m_title = multiple() ? QByteArray("Resolution of Display ") + QByteArray::number(m_sel + 1)
			: QByteArray("Resolution");
		m_groups.push_back({ resTop, resBottom, m_title.constData() });
		m_list.frame = QRect(QPoint(M + IN, resTop + 1 + PL_GROUP_MARGIN_TOP),
			QPoint(M + IN + 170, resTop + 1 + PL_GROUP_MARGIN_TOP + listH));
		fillModes();
		m_list.picked = [this](int i) {
			const Screen *d = selected();
			const QSize s = d ? d->modes.value(i) : m_modes.value(i);
			const QByteArray value = QString("%1x%2").arg(s.width()).arg(s.height()).toUtf8();
			pl_setting_set(d ? ("display." + d->name.toUtf8() + ".resolution").constData() : "resolution",
				value.constData());
			refreshSoon();
		};

		/* Pixel size, of the selected screen. At 1.25, 1.5 and 1.75 programs
		 * draw at exactly that (wp_fractional_scale_v1); ZacOS's own pixel
		 * art too, each dot one or two whole screen pixels (lib/draw.c pl_snap). */
		static const struct { double scale; const char *value, *label; } scales[] = {
			{ 1, "1", "Normal" }, { 1.25, "1.25", "1.25×" }, { 1.5, "1.5", "1.5×" },
			{ 1.75, "1.75", "1.75×" }, { 2, "2", "Large (2×)" }, { 3, "3", "Larger (3×)" },
		};
		m_scale = PanelRadios();
		const int rx = M + IN + 190, ry = resTop + 1 + PL_GROUP_MARGIN_TOP + 18;
		const double current = selected() ? selected()->scale : m_currentScale;
		m_scale.selected = 0;
		for (int i = 0; i < 6; i++) {
			m_scale.add(scales[i].label, QPoint(rx, ry + 18 * i));
			if (qAbs(scales[i].scale - current) < 0.01) {
				m_scale.selected = i;
			}
		}
		m_scale.changed = [this](int i) {
			const Screen *d = selected();
			pl_setting_set(d ? ("display." + d->name.toUtf8() + ".scale").constData() : "scale",
				scales[i].value);
			refreshSoon();
		};
		m_radios.clear();
		m_radios.push_back(&m_scale);
		m_notes = { "Changes take effect at once." };
		if (m_nested) {
			m_notes.insert(m_notes.begin(),
				"In a window (nested), the resolution is the window's size.");
		}
		height = resBottom + 14 + 12 * static_cast<int>(m_notes.size());
		setFixedSize(W, height);
	}

	/* The selected screen's resolutions. */
	void fillModes() {
		const Screen *d = selected();
		const QList<QSize> &modes = d ? d->modes : m_modes;
		const QSize size = d ? d->px : m_size;
		QStringList names;
		int current = 0;
		for (int i = 0; i < modes.size(); i++) {
			names << QString("%1 × %2").arg(modes[i].width()).arg(modes[i].height());
			if (modes[i] == size) {
				current = i;
			}
		}
		m_list.setItems(names);
		m_list.select(current, false);
	}

	/* ---- the arrangement -------------------------------------------------------- */

	/* The layout's extent, with half a screen of room round it to drag into. */
	QRect world() const {
		QRect all;
		int maxW = 0, maxH = 0;
		for (const Screen &d : m_displays) {
			all = all.isNull() ? d.rect() : all.united(d.rect());
			maxW = std::max(maxW, d.logical().width());
			maxH = std::max(maxH, d.logical().height());
		}
		return all.adjusted(-maxW / 2, -maxH / 2, maxW / 2, maxH / 2);
	}

	double factor() const {
		const QRect w = world();
		return std::min(double(m_area.width()) / std::max(w.width(), 1),
			double(m_area.height()) / std::max(w.height(), 1));
	}

	QPoint toPanel(QPoint wp) const {
		const QRect w = world();
		const double f = factor();
		const QPointF centre = QPointF(m_area.center()) - QPointF(w.width() * f / 2, w.height() * f / 2);
		return QPoint(int(centre.x() + (wp.x() - w.x()) * f), int(centre.y() + (wp.y() - w.y()) * f));
	}

	QRect miniRect(const Screen &d, QPoint pos) const {
		const double f = factor();
		const QPoint tl = toPanel(pos);
		return QRect(tl, QSize(std::max(int(d.logical().width() * f), 12),
			std::max(int(d.logical().height() * f), 10)));
	}

	QRect miniRect(int i) const {
		const Screen &d = m_displays[i];
		QPoint pos = d.pos;
		if (m_drag == Drag::Display && i == m_sel) {
			pos = m_dragPos;
		}
		return miniRect(d, pos);
	}

	/* The menu bar's strip along the top of the main screen. */
	QRect barRect(int i) const {
		const QRect r = miniRect(i);
		return QRect(r.left() + 1, r.top() + 1, r.width() - 2, 6);
	}

	int displayAt(QPoint p) const {
		for (int i = m_displays.size() - 1; i >= 0; i--) {
			if (miniRect(i).contains(p)) {
				return i;
			}
		}
		return -1;
	}

	void paintArrangement(pl_canvas *c) {
		pl_fill(c, m_area.left(), m_area.top(), m_area.right(), m_area.bottom(), GRAY(0xC));
		pl_outline(c, m_area.left() - 1, m_area.top() - 1, m_area.right() + 1, m_area.bottom() + 1,
			GRAY(0x8));
		const pl_accent accent = pl_accent_current();
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < m_displays.size(); i++) {
				if ((i == m_sel) != (pass == 1)) {
					continue;
				}
				const QRect r = miniRect(i);
				pl_fill(c, r.left() + 2, r.top() + 2, r.right() + 2, r.bottom() + 2, GRAY(0x7));
				pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), C_WHITE);
				pl_outline(c, r.left(), r.top(), r.right(), r.bottom(), C_BLACK);
				if (i == m_sel) {
					pl_outline(c, r.left() + 1, r.top() + 1, r.right() - 1, r.bottom() - 1, accent.dark);
					pl_outline(c, r.left() + 2, r.top() + 2, r.right() - 2, r.bottom() - 2, accent.dark);
				}
				const bool moving = m_drag == Drag::Bar && m_displays[i].main;
				if ((m_displays[i].main && !moving) || (!m_displays[i].main && everyBar())) {
					paintBar(c, barRect(i), m_displays[i].main);
				}
				Text number(QString::number(i + 1), 40, PL_FONT_SYSTEM);
				if (number.t && number.t->ink_l >= 0) {
					const int w = number.t->ink_r - number.t->ink_l + 1;
					pl_text(c, number.t, r.center().x() - w / 2 - number.t->ink_l, r.center().y() + 5,
						C_BLACK);
				}
			}
		}
		if (m_drag == Drag::Display) {
			/* Where it will land when let go. */
			const QRect r = miniRect(m_displays[m_sel], m_dragSnap);
			pl_outline(c, r.left(), r.top(), r.right(), r.bottom(), GRAY(0x5));
			pl_outline(c, r.left() + 1, r.top() + 1, r.right() - 1, r.bottom() - 1, GRAY(0x8));
		}
		if (m_drag == Drag::Bar) {
			paintBar(c, QRect(m_dragNow.x() - 14, m_dragNow.y() - 2, 28, 4), true);
		}
	}

	static void paintBar(pl_canvas *c, QRect r, bool main) {
		/* The main display's bar is solid; the "every display" bars are
		 * ghosted, so it is clear which screen is the main one. */
		pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), main ? C_BLACK : GRAY(0xD));
		pl_hline(c, r.left(), r.right(), r.bottom(), C_BLACK);
		pl_hline(c, r.left(), r.right(), r.top(), C_BLACK);
	}

	bool pressArrangement(QPoint p) {
		const int hit = displayAt(p);
		if (hit < 0) {
			return false;
		}
		/* The menu bar is picked up by its strip; a screen by anywhere else. */
		if (m_displays[hit].main && barRect(hit).adjusted(0, -1, 0, 2).contains(p)) {
			m_drag = Drag::Bar;
		} else {
			/* Where it is now, not where the last drag left m_dragPos: the offset of
			 * the grab inside it decides where it lands. */
			m_dragGrab = p - miniRect(m_displays[hit], m_displays[hit].pos).topLeft();
			m_dragPos = m_dragSnap = m_displays[hit].pos;
			m_drag = Drag::Display;
		}
		if (hit != m_sel) {
			m_sel = hit;
			build();
		}
		m_dragNow = p;
		flashScreenNumber(hit);
		return true;
	}

	/* Where a dragged screen's top left is in the layout, for where it is in the panel. */
	QPoint wantFor(QPoint panelTopLeft) const {
		const double f = factor();
		const QRect w = world();
		const QPoint origin = toPanel(QPoint(w.x(), w.y()));
		return QPoint(int((panelTopLeft.x() - origin.x()) / f) + w.x(),
			int((panelTopLeft.y() - origin.y()) / f) + w.y());
	}

	/* Where the dragged screen lands: touching another screen, like Mac OS's
	 * arrangement, at the nearest such place to where it is. */
	QPoint snapFor(QPoint want) const {
		const Screen &d = m_displays[m_sel];
		const QSize sz = d.logical();
		QPoint best = d.pos;
		long bestDist = -1;
		auto consider = [&](QPoint cand) {
			const QRect me(cand, sz);
			for (int i = 0; i < m_displays.size(); i++) {
				if (i != m_sel && me.intersects(m_displays[i].rect())) {
					return;
				}
			}
			const long dx = cand.x() - want.x(), dy = cand.y() - want.y();
			const long dist = dx * dx + dy * dy;
			if (bestDist < 0 || dist < bestDist) {
				bestDist = dist;
				best = cand;
			}
		};
		for (int i = 0; i < m_displays.size(); i++) {
			if (i == m_sel) {
				continue;
			}
			const QRect o = m_displays[i].rect();
			/* Slide along the touching edge, kept overlapping it, and snap to
			 * the edges' alignments when close. */
			const int xs[] = { want.x(), o.x(), o.right() + 1 - sz.width(),
				o.center().x() - sz.width() / 2 };
			const int ys[] = { want.y(), o.y(), o.bottom() + 1 - sz.height(),
				o.center().y() - sz.height() / 2 };
			for (int y : ys) {
				const int yy = std::clamp(y, o.y() - sz.height() + 40, o.bottom() + 1 - 40);
				consider(QPoint(o.right() + 1, yy));
				consider(QPoint(o.x() - sz.width(), yy));
			}
			for (int x : xs) {
				const int xx = std::clamp(x, o.x() - sz.width() + 40, o.right() + 1 - 40);
				consider(QPoint(xx, o.bottom() + 1));
				consider(QPoint(xx, o.y() - sz.height()));
			}
		}
		return best;
	}

	void dropDisplay() {
		m_displays[m_sel].pos = snapFor(wantFor(m_dragNow - m_dragGrab));
		saveArrangement();
	}

	void saveArrangement() {
		for (Screen &d : m_displays) {
			setInt(("display." + d.name.toUtf8() + ".x").constData(), d.pos.x());
			setInt(("display." + d.name.toUtf8() + ".y").constData(), d.pos.y());
			if (d.main) {
				pl_setting_set("main-display", d.name.toUtf8().constData());
			}
		}
		refreshSoon();
	}

	/* zacos9-wm applies a change and republishes the screens; look again when it has. */
	void refreshSoon() {
		QTimer::singleShot(700, this, [this] {
			if (m_drag != Drag::None) {
				return;
			}
			const QString keep = selected() ? selected()->name : QString();
			readOutputs();
			packScreens();
			m_sel = 0;
			for (int i = 0; i < m_displays.size(); i++) {
				if (m_displays[i].name == keep) {
					m_sel = i;
				}
			}
			/* Update the scale radio selection without rebuilding the whole
			 * panel: rebuilding (setFixedSize) while the window is visible
			 * can make the compositor restart it, leaving duplicate windows. */
			if (m_scale.buttons.size() == 6) {
				const double current = selected() ? selected()->scale : m_currentScale;
				m_scale.selected = 0;
				for (int i = 0; i < 6; i++) {
					static const double scales[] = { 1, 1.25, 1.5, 1.75, 2, 3 };
					if (qAbs(scales[i] - current) < 0.01) {
						m_scale.selected = i;
					}
				}
			}
			if (m_mirror) {
				m_mirror->on = m_mirrored;
			}
			if (m_everyBar) {
				m_everyBar->on = everyBar();
			}
			if (m_mainDisplay) {
				m_mainDisplay->on = selected() ? selected()->main : false;
			}
			fillModes();
			update();
		});
	}

	/* After a scale or resolution change a screen's size changes but its
	 * saved place doesn't, leaving gaps. Pack the screens so neighbours
	 * still touch, in their existing order. */
	void packScreens() {
		if (m_displays.size() < 2) {
			return;
		}
		/* Try to close gaps: for each screen, if it doesn't touch any other,
		 * move it to touch the nearest neighbour. */
		bool changed = true;
		int iterations = 0;
		while (changed && iterations < 10) {
			changed = false;
			iterations++;
			for (int i = 0; i < m_displays.size(); i++) {
				/* Check if screen i touches any other screen. */
				bool touches = false;
				for (int j = 0; j < m_displays.size(); j++) {
					if (i == j) continue;
					const QRect ri = m_displays[i].rect();
					const QRect rj = m_displays[j].rect();
					/* Touching means adjacent (right+1 == left or bottom+1 == top). */
					if (ri.right() + 1 == rj.left() || rj.right() + 1 == ri.left() ||
						ri.bottom() + 1 == rj.top() || rj.bottom() + 1 == ri.top()) {
						touches = true;
						break;
					}
				}
				if (touches) continue;

				/* Find nearest neighbour and snap to it. */
				int bestJ = -1;
				long bestDist = -1;
				for (int j = 0; j < m_displays.size(); j++) {
					if (i == j) continue;
					const QRect ri = m_displays[i].rect();
					const QRect rj = m_displays[j].rect();
					const long dx = ri.center().x() - rj.center().x();
					const long dy = ri.center().y() - rj.center().y();
					const long dist = dx * dx + dy * dy;
					if (bestDist < 0 || dist < bestDist) {
						bestDist = dist;
						bestJ = j;
					}
				}
				if (bestJ < 0) continue;

				/* Snap screen i to touch screen bestJ. */
				const QSize sz = m_displays[i].logical();
				const QRect o = m_displays[bestJ].rect();
				/* Try all four sides. */
				QPoint candidates[] = {
					QPoint(o.right() + 1, m_displays[i].pos.y()),
					QPoint(o.x() - sz.width(), m_displays[i].pos.y()),
					QPoint(m_displays[i].pos.x(), o.bottom() + 1),
					QPoint(m_displays[i].pos.x(), o.y() - sz.height()),
				};
				long bestCandDist = -1;
				QPoint bestCand = m_displays[i].pos;
				for (const QPoint &cand : candidates) {
					/* Check no overlap. */
					QRect me(cand, sz);
					bool overlap = false;
					for (int k = 0; k < m_displays.size(); k++) {
						if (k == i) continue;
						if (me.intersects(m_displays[k].rect())) {
							overlap = true;
							break;
						}
					}
					if (overlap) continue;
					const long dx = cand.x() - m_displays[i].pos.x();
					const long dy = cand.y() - m_displays[i].pos.y();
					const long dist = dx * dx + dy * dy;
					if (bestCandDist < 0 || dist < bestCandDist) {
						bestCandDist = dist;
						bestCand = cand;
					}
				}
				if (bestCand != m_displays[i].pos) {
					m_displays[i].pos = bestCand;
					changed = true;
				}
			}
		}
		/* Save the new arrangement. */
		if (changed) {
			for (Screen &d : m_displays) {
				setInt(("display." + d.name.toUtf8() + ".x").constData(), d.pos.x());
				setInt(("display." + d.name.toUtf8() + ".y").constData(), d.pos.y());
			}
		}
	}

	/* zacos9-wm publishes the screens and their modes. */
	void readOutputs() {
		m_displays.clear();
		m_modes.clear();
		QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
			qEnvironmentVariable("WAYLAND_DISPLAY"));
		if (f.open(QIODevice::ReadOnly)) {
			for (const QByteArray &line : f.readAll().split('\n')) {
				const QList<QByteArray> w = line.split(' ');
				if (w.value(0) == "mirror") {
					m_mirrored = w.value(1) == "1";
				} else if (w.value(0) == "output") {
					Screen d;
					d.name = QString::fromUtf8(w.value(1));
					const QList<QByteArray> wh = w.value(2).split('x');
					d.px = QSize(wh.value(0).toInt(), wh.value(1).toInt());
					d.scale = w.value(4).toDouble(); /* 1, 1.25, 1.5, 1.75, 2, 3 */
					d.nested = w.value(6) == "1";
					d.pos = QPoint(w.value(8).toInt(), w.value(10).toInt());
					d.main = w.value(12) == "1";
					m_displays << d;
				} else if (w.value(0) == "mode" && !m_displays.isEmpty()) {
					const QList<QByteArray> wh = w.value(1).split('@').value(0).split('x');
					const QSize s(wh.value(0).toInt(), wh.value(1).toInt());
					if (!m_displays.last().modes.contains(s)) {
						m_displays.last().modes << s;
					}
				}
			}
		}
		if (!m_displays.isEmpty()) {
			m_size = m_displays.first().px;
			m_currentScale = m_displays.first().scale;
			m_nested = m_displays.first().nested;
		}
		for (Screen &d : m_displays) {
			finishModes(d.modes, d.px);
		}
		/* No screens published (not under zacos9-wm): one window-sized one. */
		finishModes(m_modes, m_size);
		if (m_sel >= m_displays.size()) {
			m_sel = 0;
		}
	}

	void finishModes(QList<QSize> &modes, const QSize &current) {
		if (modes.isEmpty()) {
			/* A window has no modes of its own: offer the usual sizes,
			 * the Mac's own 832 x 624 and 1152 x 870 among them. */
			modes = { { 640, 480 }, { 800, 600 }, { 832, 624 }, { 1024, 768 },
				{ 1152, 870 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1440, 900 },
				{ 1600, 900 }, { 1920, 1080 } };
		}
		if (!current.isEmpty() && !modes.contains(current)) {
			modes.prepend(current);
		}
		std::sort(modes.begin(), modes.end(), [](const QSize &a, const QSize &b) {
			return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height();
		});
	}

	QList<Screen> m_displays;
	QList<QSize> m_modes; /* with no screens published */
	QSize m_size;
	double m_currentScale = 1;
	bool m_nested = false;
	bool m_mirrored = false;
	int m_sel = 0;
	int m_resTop = 0;
	QByteArray m_title;
	QRect m_area;
	Drag m_drag = Drag::None;
	QPoint m_dragNow, m_dragGrab, m_dragPos, m_dragSnap;
	std::unique_ptr<PanelCheckbox> m_mirror, m_everyBar, m_mainDisplay;
	PanelList m_list;
	PanelRadios m_scale;
};

PANEL("monitors", "Monitors", false, MonitorsPanel);
