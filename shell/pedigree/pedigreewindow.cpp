#include "pedigreewindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QWheelEvent>

#include "alert.h"
#include "panelkit.h"
#include "pixels.h"
#include "settings.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD), PANE = GRAY(0xE);
constexpr int M = 10, INSET = 10, ROW_H = PL_LIST_ROW_H, INDENT = 16;
const char *const TAB_NAMES[2] = { "System Profile", "Devices and Volumes" };

} // namespace

/* The tabs and the outline, drawn as the Platinum controls are. */
class PedigreeWindow::View : public QWidget {
public:
	explicit View(PedigreeWindow *w) : QWidget(w), m_w(w) {
		setFocusPolicy(Qt::StrongFocus);
		for (const char *name : TAB_NAMES) {
			m_tabLabels.emplace_back(name, 300, PL_FONT_SYSTEM);
		}
	}

	std::vector<const plat_text *> labels() const {
		std::vector<const plat_text *> out;
		for (const Text &t : m_tabLabels) {
			out.push_back(t.t);
		}
		return out;
	}

	pl_scrollbar scrollbar() const {
		const QRect r = m_w->outline();
		const int max = std::max(0, static_cast<int>(m_w->m_rows.size()) - m_w->visibleCount());
		pl_scrollbar sb{};
		sb.vertical = true;
		sb.length = r.height();
		sb.enabled = max > 0;
		sb_smart(&sb, m_w->visibleCount(), static_cast<int>(m_w->m_rows.size()));
		sb.thumb = max > 0 ? m_w->m_top * sb_thumb_span(&sb) / max : 0;
		return sb;
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, width() - 1, height() - 1, FACE);
		const auto l = labels();
		pl_tabs_paint(c, M, M, width() - M - 1, height() - M - 1, l.data(), 2, m_w->m_tab);
		const QRect r = m_w->outline();
		// A white box in a black frame, the scroll bar sharing its right edge.
		pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), C_BLACK);
		const int sbx = r.right() - SB_WIDTH + 1;
		pl_fill(c, r.left() + 1, r.top() + 1, sbx - 1, r.bottom() - 1, C_WHITE);
		const int valueX = r.left() + std::max(180, (sbx - r.left()) * 2 / 5);
		const uint32_t highlight = pl_highlight_current();
		for (int i = 0; i < m_w->visibleCount(); ++i) {
			const int index = m_w->m_top + i;
			if (index >= m_w->m_rows.size()) {
				break;
			}
			const Row &row = m_w->m_rows[index];
			const int top = r.top() + 1 + i * ROW_H;
			if (index == m_w->m_selected) {
				pl_fill(c, r.left() + 1, top, sbx - 1, top + ROW_H - 1, highlight);
			}
			const int x = r.left() + 6 + row.depth * INDENT;
			if (!row.item->children.isEmpty()) {
				panelDisclosure(c, x, top + 2, m_w->m_open[m_w->m_tab].contains(row.key));
			}
			const pl_font font = row.depth == 0 ? PL_FONT_SYSTEM : PL_FONT_VIEWS;
			const int labelX = x + 14;
			Text label(row.item->label, std::max(10, (row.item->value.isEmpty() ? sbx : valueX) - labelX - 6), font);
			pl_text(c, label.t, labelX, top + 12, C_BLACK);
			if (!row.item->value.isEmpty()) {
				const int vx = std::max(valueX, labelX + label.inkWidth() + 10);
				Text value(row.item->value, std::max(10, sbx - vx - 6), PL_FONT_VIEWS);
				pl_text(c, value.t, vx, top + 12, C_BLACK);
			}
		}
		const pl_scrollbar sb = scrollbar();
		pl_scrollbar_paint(c, sbx, r.top(), &sb, pl_accent_current());
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		const auto l = labels();
		const int tab = pl_tabs_hit(M, M, l.data(), 2, pos.x(), pos.y());
		if (tab >= 0) {
			m_w->setTab(tab);
			return;
		}
		const QRect r = m_w->outline();
		const int sbx = r.right() - SB_WIDTH + 1;
		if (pos.x() >= sbx && pos.x() <= r.right() && pos.y() >= r.top() && pos.y() <= r.bottom()) {
			pl_scrollbar sb = scrollbar();
			switch (sb_hit(&sb, pos.y() - r.top())) {
			case SB_DEC_ARROW: m_w->scrollTo(m_w->m_top - 1); break;
			case SB_INC_ARROW: m_w->scrollTo(m_w->m_top + 1); break;
			case SB_DEC_PAGE: m_w->scrollTo(m_w->m_top - m_w->visibleCount() + 1); break;
			case SB_INC_PAGE: m_w->scrollTo(m_w->m_top + m_w->visibleCount() - 1); break;
			case SB_THUMB_PART:
				m_w->m_dragThumb = true;
				m_w->m_dragOffset = pos.y() - r.top() - sb.thumb;
				break;
			default: break;
			}
			return;
		}
		if (!r.adjusted(1, 1, -SB_WIDTH, -1).contains(pos)) {
			return;
		}
		const int index = m_w->m_top + (pos.y() - r.top() - 1) / ROW_H;
		if (index >= m_w->m_rows.size()) {
			m_w->m_selected = -1;
			update();
			return;
		}
		m_w->m_selected = index;
		// The triangle (and a little around it) opens and closes the row.
		const int x = r.left() + 6 + m_w->m_rows[index].depth * INDENT;
		if (pos.x() >= x - 3 && pos.x() < x + 12 && !m_w->m_rows[index].item->children.isEmpty()) {
			m_w->toggle(index);
			return;
		}
		update();
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (!m_w->m_dragThumb) {
			return;
		}
		const QRect r = m_w->outline();
		const pl_scrollbar sb = scrollbar();
		const int range = sb_thumb_span(&sb);
		const int max = std::max(0, static_cast<int>(m_w->m_rows.size()) - m_w->visibleCount());
		const int thumb = std::clamp(static_cast<int>(e->position().y()) - r.top() - m_w->m_dragOffset, 0, range);
		m_w->scrollTo(range > 0 ? (thumb * max + range / 2) / range : 0);
	}

	void mouseReleaseEvent(QMouseEvent *) override {
		m_w->m_dragThumb = false;
	}

	void mouseDoubleClickEvent(QMouseEvent *e) override {
		const QRect r = m_w->outline();
		if (r.adjusted(1, 1, -SB_WIDTH, -1).contains(e->position().toPoint())) {
			const int index = m_w->m_top + (static_cast<int>(e->position().y()) - r.top() - 1) / ROW_H;
			if (index < m_w->m_rows.size() && !m_w->m_rows[index].item->children.isEmpty()) {
				m_w->toggle(index);
				return;
			}
		}
		mousePressEvent(e);
	}

	void wheelEvent(QWheelEvent *e) override {
		m_wheel += e->angleDelta().y();
		const int steps = m_wheel / 40;
		m_wheel -= steps * 40;
		if (steps) {
			m_w->scrollTo(m_w->m_top - steps);
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		const int n = m_w->m_rows.size();
		int &sel = m_w->m_selected;
		switch (e->key()) {
		case Qt::Key_Up: sel = sel <= 0 ? 0 : sel - 1; break;
		case Qt::Key_Down: sel = std::min(n - 1, sel + 1); break;
		case Qt::Key_Home: sel = 0; break;
		case Qt::Key_End: sel = n - 1; break;
		case Qt::Key_Right:
		case Qt::Key_Left:
			// Right opens the selected row, Left closes it, as the Sniffer's list does.
			if (sel >= 0 && sel < n && !m_w->m_rows[sel].item->children.isEmpty() &&
					m_w->m_open[m_w->m_tab].contains(m_w->m_rows[sel].key) == (e->key() == Qt::Key_Left)) {
				m_w->toggle(sel);
			}
			return;
		default: QWidget::keyPressEvent(e); return;
		}
		if (n > 0) {
			m_w->ensureVisible(sel);
		}
		update();
	}

private:
	PedigreeWindow *m_w;
	std::vector<Text> m_tabLabels;
	int m_wheel = 0;
};

PedigreeWindow::PedigreeWindow() : m_view(new View(this)) {
	setWindowTitle("Pedigree");
	setCentralWidget(m_view);
	setMinimumSize(420, 320);
	resize(560, 480);
	buildMenus();
	reload();
	m_view->setFocus();
}

void PedigreeWindow::buildMenus() {
	/* Exported to ZacOS 9's menu bar, as other programs' are. */
	QMenu *file = menuBar()->addMenu("File");
	file->addAction("Save Report...", QKeySequence::Save, this, [this] { saveReportAs(); })
		->setMenuRole(QAction::NoRole);
	file->addAction("Close", QKeySequence::Close, this, [this] { close(); });
	file->addSeparator();
	file->addAction("Quit", QKeySequence::Quit, this, [this] { close(); })->setMenuRole(QAction::NoRole);
	QMenu *edit = menuBar()->addMenu("Edit");
	edit->addAction("Copy", QKeySequence::Copy, this, [this] { copySelected(); });
	QMenu *view = menuBar()->addMenu("View");
	view->addAction("Refresh", QKeySequence::Refresh, this, [this] { reload(); });
	view->addSeparator();
	view->addAction("Open All", this, [this] {
		std::function<void(const QList<ProfileItem> &, const QString &)> openAll =
			[&](const QList<ProfileItem> &items, const QString &prefix) {
				for (const ProfileItem &i : items) {
					if (!i.children.isEmpty()) {
						m_open[m_tab].insert(prefix + i.label);
						openAll(i.children, prefix + i.label + "\n");
					}
				}
			};
		openAll(m_tabs[m_tab], QString());
		layoutRows();
	});
	view->addAction("Close All", this, [this] {
		m_open[m_tab].clear();
		m_top = 0;
		layoutRows();
	});
}

void PedigreeWindow::reload() {
	const bool first = m_tabs[0].isEmpty() && m_tabs[1].isEmpty();
	QApplication::setOverrideCursor(Qt::WaitCursor);
	m_tabs[0] = systemProfile();
	m_tabs[1] = devicesAndVolumes();
	QApplication::restoreOverrideCursor();
	if (first) {
		// The sections start open, the things in them closed.
		for (int t = 0; t < 2; ++t) {
			for (const ProfileItem &i : m_tabs[t]) {
				m_open[t].insert(i.label);
			}
		}
	}
	layoutRows();
}

void PedigreeWindow::layoutRows() {
	const QString selectedKey = m_selected >= 0 && m_selected < m_rows.size() ? m_rows[m_selected].key : QString();
	m_rows.clear();
	std::function<void(const QList<ProfileItem> &, const QString &, int)> add =
		[&](const QList<ProfileItem> &items, const QString &prefix, int depth) {
			for (const ProfileItem &i : items) {
				const QString key = prefix + i.label;
				m_rows << Row{ &i, key, depth };
				if (!i.children.isEmpty() && m_open[m_tab].contains(key)) {
					add(i.children, key + "\n", depth + 1);
				}
			}
		};
	add(m_tabs[m_tab], QString(), 0);
	m_selected = -1;
	for (int i = 0; i < m_rows.size(); ++i) {
		if (m_rows[i].key == selectedKey) {
			m_selected = i;
		}
	}
	scrollTo(m_top);
}

QRect PedigreeWindow::outline() const {
	const int top = M + PL_TAB_H + INSET;
	return QRect(M + INSET, top, m_view->width() - 2 * (M + INSET), m_view->height() - top - M - INSET);
}

int PedigreeWindow::visibleCount() const {
	return std::max(1, (outline().height() - 2) / ROW_H);
}

void PedigreeWindow::scrollTo(int top) {
	m_top = std::clamp(top, 0, std::max(0, static_cast<int>(m_rows.size()) - visibleCount()));
	m_view->update();
}

void PedigreeWindow::ensureVisible(int row) {
	if (row < m_top) {
		scrollTo(row);
	} else if (row >= m_top + visibleCount()) {
		scrollTo(row - visibleCount() + 1);
	}
}

QStringList PedigreeWindow::visibleRows() const {
	QStringList out;
	for (const Row &r : m_rows) {
		out << QString(r.depth * 2, ' ') + r.item->label + (r.item->value.isEmpty() ? QString() : ": " + r.item->value);
	}
	return out;
}

void PedigreeWindow::setTab(int tab) {
	if (tab == m_tab || tab < 0 || tab > 1) {
		return;
	}
	m_tab = tab;
	m_selected = -1;
	m_top = 0;
	layoutRows();
}

void PedigreeWindow::toggle(int row) {
	if (row < 0 || row >= m_rows.size()) {
		return;
	}
	const QString key = m_rows[row].key;
	if (!m_open[m_tab].remove(key)) {
		m_open[m_tab].insert(key);
	}
	m_selected = row;
	layoutRows();
	// What opened comes into view, as much of it as fits.
	int last = row;
	while (last + 1 < m_rows.size() && m_rows[last + 1].depth > m_rows[row].depth) {
		++last;
	}
	ensureVisible(last);
	ensureVisible(row);
}

bool PedigreeWindow::saveReport(const QString &path) {
	QSaveFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(profileReport(m_tabs[0], m_tabs[1]).toUtf8()) >= 0 && f.commit();
}

void PedigreeWindow::saveReportAs() {
	const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Pedigree Report.txt";
	const QString path = QFileDialog::getSaveFileName(this, "Save Report", start);
	if (path.isEmpty()) {
		return;
	}
	if (!saveReport(path)) {
		Alert::ask(QString::fromUtf8("The report couldn't be saved as “%1”.").arg(QFileInfo(path).fileName()),
			"OK", QString());
	}
}

void PedigreeWindow::copySelected() {
	if (m_selected < 0 || m_selected >= m_rows.size()) {
		return;
	}
	const ProfileItem *i = m_rows[m_selected].item;
	QApplication::clipboard()->setText(profileText({ *i }).trimmed());
}
