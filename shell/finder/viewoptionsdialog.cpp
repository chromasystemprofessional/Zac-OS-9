#include "viewoptionsdialog.h"

#include <QDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <array>
#include <cstring>

#include "desktop.h"
#include "folderwindow.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"
#include "viewoptions.h"

namespace {

constexpr uint32_t BG = GRAY(0xD);
constexpr int IN = 1 + PL_GROUP_MARGIN;  /* group box line to its items */
constexpr int TOP = 1 + PL_GROUP_MARGIN_TOP;
constexpr int ROW = 20;
constexpr int GAP = 28;                  /* between groups: room for a title */

/* A group's height for `rows` rows of radio buttons or checkboxes. */
constexpr int groupH(int rows) {
	return TOP + (rows - 1) * ROW + 12 + PL_GROUP_MARGIN;
}

enum class ViewKind { Icons, Buttons, List, Desktop };

const Arrange KEEP_BY[] = { Arrange::Name, Arrange::Modified, Arrange::Created, Arrange::Size,
	Arrange::Kind, Arrange::Label };
const char *const KEEP_NAMES[] = { "by Name", "by Date Modified", "by Date Created", "by Size",
	"by Kind", "by Label" };
/* The optional columns, as the Mac's dialog listed them. */
const int OPTIONAL_COLUMNS[] = { COL_MODIFIED, COL_CREATED, COL_SIZE, COL_KIND, COL_LABEL,
	COL_COMMENTS, COL_VERSION };

struct Group {
	int top, bottom;
	const char *title;
};

/* The controls for one view's options, shared by View Options and
 * Preferences > Views. */
class ViewOptionsEditor {
public:
	std::function<void()> changed;

	void layout(ViewKind kind, int x0, int y0, int x1) {
		m_kind = kind;
		m_x0 = x0;
		m_x1 = x1;
		m_groups.clear();
		m_arrange = PanelRadios();
		m_size = PanelRadios();
		m_keepBy = PanelPopup();
		m_relative = PanelCheckbox();
		m_folderSizes = PanelCheckbox();
		const int x = x0 + IN;
		int y = y0;
		if (kind == ViewKind::List) {
			m_groups.push_back({ y, y + groupH(4), "Show Columns" });
			for (int i = 0; i < 7; i++) {
				const int c = OPTIONAL_COLUMNS[i];
				m_columns[i] = PanelCheckbox(columnInfo(c).title,
					QPoint(x + (i < 4 ? 0 : 150), y + TOP + (i % 4) * ROW));
				m_columns[i].toggled = [this, c](bool on) {
					m_options.list.columns = on ? m_options.list.columns | columnBit(c) :
						m_options.list.columns & ~columnBit(c);
					notify();
				};
			}
			y += groupH(4) + GAP;
			m_groups.push_back({ y, y + groupH(2), "Options" });
			m_relative = PanelCheckbox("Use relative date", QPoint(x, y + TOP));
			m_relative.toggled = [this](bool on) {
				m_options.list.relativeDate = on;
				notify();
			};
			m_folderSizes = PanelCheckbox("Calculate folder sizes", QPoint(x, y + TOP + ROW));
			m_folderSizes.toggled = [this](bool on) {
				m_options.list.folderSizes = on;
				notify();
			};
			y += groupH(2) + GAP;
		} else {
			const bool keepOnly = kind == ViewKind::Buttons;
			const int rows = keepOnly ? 1 : 3;
			const int h = keepOnly ? TOP + PL_POPUP_H + PL_GROUP_MARGIN - 4 : groupH(rows) + 4;
			m_groups.push_back({ y, y + h, "Icon Arrangement" });
			const int keepY = y + TOP + (rows - 1) * ROW;
			if (!keepOnly) {
				m_arrange.add("None", QPoint(x, y + TOP));
				m_arrange.add("Always snap to grid", QPoint(x, y + TOP + ROW));
				m_arrange.add("Keep arranged:", QPoint(x, keepY));
				m_arrange.changed = [this](int i) {
					icons().arrangement = static_cast<IconArrangement>(i);
					m_keepBy.enabled = i == 2;
					notify();
				};
			}
			m_keepBy.rect = QRect(x + (keepOnly ? 104 : 124), keepY - 4, 150, PL_POPUP_H);
			for (const char *name : KEEP_NAMES) {
				m_keepBy.items.emplace_back(name);
			}
			m_keepBy.chosen = [this](int row) {
				m_keepBy.selected = row;
				icons().keepBy = KEEP_BY[row];
				notify();
			};
			y += h + GAP;
		}
		if (kind != ViewKind::Desktop) {
			m_groups.push_back({ y, y + groupH(1), "Icon Size" });
			m_size.add("Standard", QPoint(x, y + TOP));
			m_size.add("Small", QPoint(x + 100, y + TOP));
			if (kind == ViewKind::List) {
				m_size.add("Tiny", QPoint(x + 200, y + TOP));
			}
			m_size.changed = [this](int i) {
				if (m_kind == ViewKind::List) {
					m_options.list.size = static_cast<IconSize>(i);
				} else {
					icons().size = static_cast<IconSize>(i);
				}
				notify();
			};
			y += groupH(1);
		} else {
			y -= GAP;
		}
		m_bottom = y;
		setOptions(m_options);
	}

	void setOptions(const ViewOptions &o) {
		m_options = o;
		if (m_kind == ViewKind::List) {
			for (int i = 0; i < 7; i++) {
				m_columns[i].on = o.list.columns & columnBit(OPTIONAL_COLUMNS[i]);
			}
			m_relative.on = o.list.relativeDate;
			m_folderSizes.on = o.list.folderSizes;
			m_size.selected = static_cast<int>(o.list.size);
			return;
		}
		const IconViewOptions &io = icons();
		m_arrange.selected = static_cast<int>(io.arrangement);
		m_keepBy.enabled = m_kind == ViewKind::Buttons || io.arrangement == IconArrangement::KeepArranged;
		for (int i = 0; i < 6; i++) {
			if (KEEP_BY[i] == io.keepBy) {
				m_keepBy.selected = i;
			}
		}
		m_size.selected = static_cast<int>(io.size) > 1 ? 1 : static_cast<int>(io.size);
	}

	const ViewOptions &options() const { return m_options; }
	int bottom() const { return m_bottom; }

	void addTo(PanelHost &host) {
		if (m_kind == ViewKind::List) {
			for (PanelCheckbox &c : m_columns) {
				host.checks.push_back(&c);
			}
			host.checks.push_back(&m_relative);
			host.checks.push_back(&m_folderSizes);
		} else {
			if (m_kind != ViewKind::Buttons) {
				host.radios.push_back(&m_arrange);
			}
			host.popups.push_back(&m_keepBy);
		}
		if (m_kind != ViewKind::Desktop) {
			host.radios.push_back(&m_size);
		}
	}

	void paint(pl_canvas *c, uint32_t bg = BG) const {
		for (const Group &g : m_groups) {
			panelGroup(c, m_x0, g.top, m_x1, g.bottom, g.title, bg);
		}
		if (m_kind == ViewKind::Buttons) {
			panelText(c, "Keep arranged:", m_x0 + IN, m_keepBy.rect.top() + 14);
		}
	}

private:
	IconViewOptions &icons() {
		return m_kind == ViewKind::Buttons ? m_options.buttons : m_options.icons;
	}
	const IconViewOptions &icons() const {
		return m_kind == ViewKind::Buttons ? m_options.buttons : m_options.icons;
	}
	void notify() {
		if (changed) {
			changed();
		}
	}

	ViewKind m_kind = ViewKind::Icons;
	ViewOptions m_options;
	int m_x0 = 0, m_x1 = 0, m_bottom = 0;
	std::vector<Group> m_groups;
	PanelRadios m_arrange, m_size;
	PanelPopup m_keepBy;
	std::array<PanelCheckbox, 7> m_columns;
	PanelCheckbox m_relative, m_folderSizes;
};

/* ---- View Options ---------------------------------------------------------------- */

class ViewOptionsDialog : public QDialog {
public:
	explicit ViewOptionsDialog(FinderView *view) : m_view(view->widget()) {
		auto *fw = dynamic_cast<FolderWindow *>(view);
		m_kind = !fw ? ViewKind::Desktop :
			fw->viewMode() == FolderWindow::ViewMode::List ? ViewKind::List :
			fw->viewMode() == FolderWindow::ViewMode::Buttons ? ViewKind::Buttons : ViewKind::Icons;
		m_name = fw ? fw->windowTitle() : QStringLiteral("Desktop");
		setWindowTitle("View Options");
		m_editor.layout(m_kind, 12, 48, W - 14);
		m_editor.setOptions(fw ? fw->viewOptions() : static_cast<Desktop *>(view)->viewOptions());
		const int h = m_editor.bottom() + 16 + PL_BUTTON_H + 14;
		setFixedSize(W, h);
		const int by = h - 14 - PL_BUTTON_H;
		m_standard = PanelButton("Set to Standard Views", QRect(12, by, 170, PL_BUTTON_H));
		m_standard.clicked = [this] {
			const ViewOptions standard = standardViews();
			ViewOptions o = m_editor.options();
			if (m_kind == ViewKind::List) {
				o.list = standard.list;
			} else if (m_kind == ViewKind::Buttons) {
				o.buttons = standard.buttons;
			} else {
				o.icons = standard.icons;
			}
			m_editor.setOptions(o);
			update();
		};
		m_cancel = PanelButton("Cancel", QRect(W - 12 - 2 * 72 - 10, by, 72, PL_BUTTON_H));
		m_cancel.clicked = [this] { reject(); };
		m_ok = PanelButton("OK", QRect(W - 12 - 72, by, 72, PL_BUTTON_H), true);
		m_ok.clicked = [this] { accept(); };
		m_editor.changed = [this] { update(); };
		m_host.buttons = { &m_cancel, &m_ok };
		if (m_kind != ViewKind::Desktop) {
			m_host.buttons.insert(m_host.buttons.begin(), &m_standard);
		}
		m_editor.addTo(m_host);
		m_host.defaultButton = &m_ok;
		m_host.cancelButton = &m_cancel;
	}

	ViewOptions options() const { return m_editor.options(); }

protected:
	void showEvent(QShowEvent *e) override {
		QDialog::showEvent(e);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_fill(&px.c, 0, 0, width() - 1, height() - 1, BG);
		/* The window's name, as the Mac's dialog had it. */
		panelText(&px.c, m_name, 12, 24, PL_FONT_SYSTEM, C_BLACK, W - 24);
		pl_hline(&px.c, 12, W - 13, 32, GRAY(0x8));
		pl_hline(&px.c, 12, W - 13, 33, C_WHITE);
		m_editor.paint(&px.c);
		m_host.paintControls(&px.c, BG);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override { m_host.hostPress(e); update(); }
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); update(); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); update(); }
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_host.hostKey(e)) {
			QDialog::keyPressEvent(e);
		}
		update();
	}

private:
	static constexpr int W = 360;
	QPointer<QWidget> m_view;
	ViewKind m_kind;
	QString m_name;
	ViewOptionsEditor m_editor;
	PanelButton m_standard, m_cancel, m_ok;
	PanelHost m_host{ this };
};

/* ---- Preferences ------------------------------------------------------------------ */

const char *const COLOR_NAMES[PL_PALETTE16_COUNT] = { "White", "Yellow", "Orange", "Red",
	"Magenta", "Purple", "Blue", "Cyan", "Green", "Dark Green", "Brown", "Tan", "Light Gray",
	"Medium Gray", "Dark Gray", "Black" };
const int SPRING_STEPS[] = { 300, 600, 900, 1200, 1500 };

QImage swatch(uint32_t color, int w, int h) {
	QImage img(w, h, QImage::Format_ARGB32);
	img.fill(QColor::fromRgba(color));
	for (int x = 0; x < w; x++) {
		img.setPixel(x, 0, 0xFF000000);
		img.setPixel(x, h - 1, 0xFF000000);
	}
	for (int y = 0; y < h; y++) {
		img.setPixel(0, y, 0xFF000000);
		img.setPixel(w - 1, y, 0xFF000000);
	}
	return img;
}

class PreferencesWindow : public QWidget {
public:
	PreferencesWindow() {
		setWindowTitle("Preferences");
		setAttribute(Qt::WA_DeleteOnClose);
		setFixedSize(W, H);
		for (const char *t : { "General", "Views", "Labels" }) {
			m_tabTexts.emplace_back(t, 200, PL_FONT_SYSTEM);
		}
		for (const Text &t : m_tabTexts) {
			m_tabs.push_back(t.t);
		}

		/* General. */
		m_spring = PanelCheckbox("Spring-loaded folders", QPoint(X + IN, PANE + 16 + TOP));
		m_spring.on = springLoadedFolders();
		m_spring.toggled = [this](bool on) {
			pl_setting_set("spring-loaded", on ? nullptr : "false");
			m_delay.enabled = on;
		};
		m_delay.pos = QPoint(X + IN + 150, PANE + 16 + TOP + ROW + 4);
		m_delay.width = W - 2 * X - 2 * IN - 156;
		m_delay.steps = 5;
		const int ms = springDelayMs();
		for (int i = 0; i < 5; i++) {
			if (std::abs(SPRING_STEPS[i] - ms) < std::abs(SPRING_STEPS[m_delay.value] - ms)) {
				m_delay.value = i;
			}
		}
		m_delay.setLabels("Short", "Long");
		m_delay.enabled = m_spring.on;
		m_delay.changed = [](int i) {
			pl_setting_set("spring-delay", i == 2 ? nullptr :
				QByteArray::number(SPRING_STEPS[i]).constData());
		};
		const int gridTop = PANE + 16 + SPRING_H + GAP;
		m_grid.add("Tight (more items)", QPoint(X + IN, gridTop + TOP));
		m_grid.add("Wide (neater arrangement)", QPoint(X + IN, gridTop + TOP + ROW));
		m_grid.selected = tightGrid() ? 0 : 1;
		m_grid.changed = [](int i) { pl_setting_set("grid-spacing", i == 0 ? "tight" : nullptr); };
		const int trashTop = gridTop + groupH(2) + 8;
		m_warnTrash = PanelCheckbox("Warn before emptying Trash",
			QPoint(X + IN, trashTop + TOP));
		m_warnTrash.on = warnBeforeEmptyingTrash();
		m_warnTrash.toggled = [this](bool warn) {
			if (!setWarnBeforeEmptyingTrash(warn)) {
				m_warnTrash.on = !warn;
			}
		};

		const int puppyTop = trashTop + TOP + PL_CHECKBOX_SIZE + PL_GROUP_MARGIN + GAP;
		m_puppy = PanelCheckbox("Use Puppy Mode", QPoint(X + IN, puppyTop + TOP));
		m_puppy.on = pl_puppy_mode();
		m_puppy.enabled = !pl_puppy_locked(); /* set by an administrator */
		m_puppy.hint = "Shows only the essential commands in the Sniffer's menus, for children "
			"and people new to computers.";
		m_puppy.toggled = [](bool on) { pl_setting_set("puppy-mode", on ? "true" : nullptr); };

		/* Views. */
		m_viewFor.rect = QRect(X + 196, PANE + 14, 110, PL_POPUP_H);
		m_viewFor.items = { PopupItem("Icon"), PopupItem("Button"), PopupItem("List") };
		m_viewFor.chosen = [this](int row) {
			m_viewFor.selected = row;
			showView(row);
		};
		m_views.changed = [this] {
			setStandardViews(m_views.options());
			update();
		};

		/* Labels. */
		for (int i = 0; i < 7; i++) {
			m_names[i].rect = QRect(X + 48, PANE + 20 + i * 30, 220, PL_EDIT_H);
			m_names[i].setText(QString::fromUtf8(pl_label_name(i + 1)));
			m_names[i].accepts = [](QChar ch) { return ch != '|' && ch.isPrint(); };
			m_names[i].edited = [this] { m_saveLabels.start(); };
			m_colors[i] = pl_label_color(i + 1);
		}
		m_saveLabels.setSingleShot(true);
		m_saveLabels.setInterval(400);
		m_saveLabels.callOnTimeout([this] { saveLabels(); });

		showTab(0);
	}

	~PreferencesWindow() override {
		if (m_saveLabels.isActive()) {
			saveLabels();
		}
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, BG);
		pl_tabs_paint(c, TABS_X, 10, W - TABS_X - 1, H - 12, m_tabs.data(), 3, m_tab);
		if (m_tab == 0) {
			panelGroup(c, X, PANE + 16, W - X - 2, PANE + 16 + SPRING_H, "Spring-loaded Folders", PANE_BG);
			panelText(c, "Delay before opening:", X + IN, m_delay.pos.y() + 9, PL_FONT_SYSTEM,
				m_delay.enabled ? C_BLACK : GRAY(0x8));
			m_delay.paint(c);
			const int gridTop = PANE + 16 + SPRING_H + GAP;
			panelGroup(c, X, gridTop, W - X - 2, gridTop + groupH(2), "Grid Spacing", PANE_BG);
			const int trashTop = gridTop + groupH(2) + 8;
			panelGroup(c, X, trashTop, W - X - 2, trashTop + TOP + PL_CHECKBOX_SIZE +
				PL_GROUP_MARGIN, "Trash", PANE_BG);
			const int puppyTop = trashTop + TOP + PL_CHECKBOX_SIZE + PL_GROUP_MARGIN + GAP;
			panelGroup(c, X, puppyTop, W - X - 2, puppyTop + TOP + PL_CHECKBOX_SIZE + 4 + 12 +
				PL_GROUP_MARGIN, "Puppy Mode", PANE_BG);
			panelText(c, "Only the essential commands and features.", X + IN + 20,
				puppyTop + TOP + PL_CHECKBOX_SIZE + 14, PL_FONT_VIEWS);
		} else if (m_tab == 1) {
			panelText(c, "Standard View Options for:", X, m_viewFor.rect.top() + 14);
			m_views.paint(c, PANE_BG);
		} else {
			for (int i = 0; i < 7; i++) {
				const QRect s = swatchRect(i);
				pl_fill(c, s.left(), s.top(), s.right(), s.bottom(), m_colors[i]);
				pl_outline(c, s.left(), s.top(), s.right(), s.bottom(), C_BLACK);
			}
			panelText(c, "Click a colour to change it.", X, H - 30, PL_FONT_VIEWS, GRAY(0x5));
		}
		m_host.paintControls(c, PANE_BG);
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		const int tab = pl_tabs_hit(TABS_X, 10, m_tabs.data(), 3, pos.x(), pos.y());
		if (tab >= 0 && tab != m_tab) {
			showTab(tab);
		} else if (m_tab == 0 && m_delay.enabled && m_delay.press(pos)) {
			m_sliding = true;
		} else if (m_tab == 2 && swatchAt(pos) >= 0) {
			pickColor(swatchAt(pos));
		} else {
			m_host.hostPress(e);
		}
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (m_sliding) {
			m_delay.move(e->position().toPoint());
		} else {
			m_host.hostMove(e);
		}
		update();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (m_sliding) {
			m_delay.release(e->position().toPoint());
			m_sliding = false;
		} else {
			m_host.hostRelease(e);
		}
		update();
	}
	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if (!m_host.hostKey(e) && e->key() == Qt::Key_Escape) {
			close();
		}
		update();
	}

private:
	static constexpr int W = 420, H = 400, TABS_X = 12, X = 28;
	static constexpr uint32_t PANE_BG = GRAY(0xE); /* the tab pane's */
	static constexpr int PANE = 10 + PL_TAB_H;
	static constexpr int SPRING_H = TOP + ROW + 4 + PL_SLIDER_H + 14 + PL_GROUP_MARGIN;

	void showTab(int tab) {
		m_tab = tab;
		m_host.checks.clear();
		m_host.radios.clear();
		m_host.popups.clear();
		m_host.edits.clear();
		m_host.setFocus(nullptr);
		if (tab == 0) {
			m_host.checks = { &m_spring, &m_warnTrash, &m_puppy };
			m_host.radios = { &m_grid };
		} else if (tab == 1) {
			showView(m_viewFor.selected);
		} else {
			for (PanelEdit &e : m_names) {
				m_host.edits.push_back(&e);
			}
			m_host.setFocus(&m_names[0]);
		}
		update();
	}

	void showView(int which) {
		m_views.layout(which == 0 ? ViewKind::Icons : which == 1 ? ViewKind::Buttons : ViewKind::List,
			X, PANE + 50, W - X - 2);
		m_views.setOptions(standardViews());
		m_host.checks.clear();
		m_host.radios.clear();
		m_host.popups = { &m_viewFor };
		m_views.addTo(m_host);
		update();
	}

	QRect swatchRect(int i) const {
		return QRect(X + 8, PANE + 20 + i * 30 + 2, 28, PL_EDIT_H - 4);
	}
	int swatchAt(QPoint p) const {
		for (int i = 0; i < 7; i++) {
			if (swatchRect(i).contains(p)) {
				return i;
			}
		}
		return -1;
	}

	void pickColor(int label) {
		std::vector<PopupItem> items;
		int checked = -1;
		for (int i = 0; i < PL_PALETTE16_COUNT; i++) {
			PopupItem item(COLOR_NAMES[i]);
			item.icon = swatch(pl_palette16[i], 16, 12);
			items.push_back(item);
			if (pl_palette16[i] == m_colors[label]) {
				checked = i;
			}
		}
		const QRect anchor = swatchRect(label);
		panelMenuNear(this, items, checked, anchor, anchor.top(), PL_FONT_SYSTEM, [this, label](int i) {
			m_colors[label] = pl_palette16[i];
			saveLabels();
			update();
		});
	}

	void saveLabels() {
		m_saveLabels.stop();
		QStringList names, colors;
		for (int i = 0; i < 7; i++) {
			names << m_names[i].text.trimmed().left(31);
			colors << QString::asprintf("%06X", m_colors[i] & 0xFFFFFFu);
		}
		pl_setting_set("label-names", names.join('|').toUtf8().constData());
		pl_setting_set("label-colors", colors.join(',').toUtf8().constData());
	}

	int m_tab = 0;
	std::vector<Text> m_tabTexts;
	std::vector<const plat_text *> m_tabs;
	PanelCheckbox m_spring;
	PanelCheckbox m_warnTrash, m_puppy;
	PanelSlider m_delay;
	bool m_sliding = false;
	PanelRadios m_grid;
	PanelPopup m_viewFor;
	ViewOptionsEditor m_views;
	std::array<PanelEdit, 7> m_names;
	std::array<uint32_t, 7> m_colors{};
	QTimer m_saveLabels;
	PanelHost m_host{ this };
};

QPointer<PreferencesWindow> preferences;

} // namespace

void showViewOptions(FinderView *view) {
	if (!view) {
		return;
	}
	QPointer<QWidget> target = view->widget();
	ViewOptionsDialog dialog(view);
	if (dialog.exec() != QDialog::Accepted || !target) {
		return;
	}
	if (auto *fw = dynamic_cast<FolderWindow *>(view)) {
		fw->setViewOptions(dialog.options());
	} else if (auto *desktop = dynamic_cast<Desktop *>(view)) {
		desktop->setViewOptions(dialog.options());
	}
}

void showPreferences() {
	if (!preferences) {
		preferences = new PreferencesWindow;
	}
	preferences->show();
	preferences->raise();
	preferences->activateWindow();
}
