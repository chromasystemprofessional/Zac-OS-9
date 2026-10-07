#include "resourcewindows.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWheelEvent>
#include <algorithm>

#include "icons.h"
#include "panelkit.h"
#include "pixels.h"
#include "resources.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD);

void text(pl_canvas *canvas, const QString &value, int x, int y, int width,
		pl_font font = PL_FONT_VIEWS, uint32_t color = C_BLACK) {
	Text rendered(value, width, font);
	if (rendered.t) {
		pl_text(canvas, rendered.t, x, y, color);
	}
}

class ResourceListWindow : public QWidget {
public:
	enum class Page { System, Extensions };

	explicit ResourceListWindow(Page page) : m_page(page) {
		setAttribute(Qt::WA_DeleteOnClose);
		setMouseTracking(true);
		setWindowTitle(page == Page::System ? "System Information" : "Extensions Manager");
		setFixedSize(page == Page::System ? QSize(520, 420) : QSize(600, 460));
		m_title = page == Page::System ? "System Information" : "Extensions Manager";
		m_list.frame = page == Page::System ? QRect(12, 78, 496, 326) :
			QRect(12, 96, 240, 352);
		if (page == Page::Extensions) {
			m_showAll = PanelCheckbox("Show All", QPoint(14, 73));
			m_showAll.toggled = [this](bool) { loadExtensions(); };
			loadExtensions();
		} else {
			m_explanation = "Current system details; viewing does not change this computer.";
			m_rows = systemInformation();
			m_list.setItems(m_rows);
			if (!m_rows.isEmpty()) {
				m_list.select(0, false);
			}
		}
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels pixels(width(), height());
		pl_fill(&pixels.c, 0, 0, width() - 1, height() - 1, FACE);
		text(&pixels.c, m_title, 16, 25, width() - 32, PL_FONT_SYSTEM);
		pl_hline(&pixels.c, 12, width() - 13, 36, GRAY(0x8));
		pl_hline(&pixels.c, 13, width() - 12, 37, C_WHITE);
		text(&pixels.c, m_explanation, 14, 57, width() - 28);
		m_list.paint(&pixels.c, true);
		if (m_page == Page::Extensions) {
			m_showAll.paint(&pixels.c);
			paintDetails(&pixels.c);
		}
		QPainter painter(this);
		pixels.blit(painter);
	}

	void keyPressEvent(QKeyEvent *event) override {
		if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_W) {
			close();
		} else if (m_list.key(event->key(), event->text())) {
			update();
		}
	}

	void mousePressEvent(QMouseEvent *event) override {
		const QPoint pos = event->position().toPoint();
		if (m_list.press(pos) || (m_page == Page::Extensions && m_showAll.press(pos))) {
			update();
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		const QPoint pos = event->position().toPoint();
		if (m_list.move(pos) || (m_page == Page::Extensions && m_showAll.move(pos))) {
			update();
		}
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		const bool list = m_list.release();
		const bool box = m_page == Page::Extensions &&
			m_showAll.release(event->position().toPoint());
		if (list || box) {
			update();
		}
	}

	void wheelEvent(QWheelEvent *event) override {
		if (m_list.wheel(event->position().toPoint(), event->angleDelta().y())) {
			update();
		}
	}

private:
	/* The curated entries, or under Show All every loaded module. */
	void loadExtensions() {
		m_resources = resourceChildren(m_showAll.on ? "device-drivers" : "extensions");
		std::stable_sort(m_resources.begin(), m_resources.end(),
			[](const SystemResource &a, const SystemResource &b) {
				return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
			});
		m_explanation = m_showAll.on
			? "Every loaded kernel module, by its technical name. For viewing only."
			: "Extensions add features to this computer. For viewing only; they can't "
				"be turned off here.";
		m_rows.clear();
		std::vector<int> icons;
		for (const SystemResource &resource : m_resources) {
			m_rows << resource.name;
			icons.push_back(resource.icon);
		}
		m_list.state.top = 0;
		m_list.state.selected = -1;
		m_list.setItems(m_rows, icons);
		if (!m_rows.isEmpty()) {
			m_list.select(0, false);
		}
		update();
	}

	void paintDetails(pl_canvas *c) const {
		const int row = m_list.state.selected;
		if (row < 0 || row >= static_cast<int>(m_resources.size())) {
			return;
		}
		const SystemResource &resource = m_resources[static_cast<size_t>(row)];
		const int x = 270, right = width() - 20;
		pl_icon_paint(c, x, 100, resource.icon, PL_ICON_LARGE, false);
		text(c, resource.name, x + 42, 121, right - x - 42, PL_FONT_SYSTEM);
		pl_hline(c, 264, right, 140, GRAY(0x8));
		pl_hline(c, 265, right + 1, 141, C_WHITE);
		int y = 162;
		for (const QString &line : panelWrap(resource.description, right - x, PL_FONT_VIEWS)) {
			text(c, line, x, y, right - x);
			y += 16;
		}
		y += 10;
		for (const QString &item : resource.metadata) {
			for (const QString &line : panelWrap(item, right - x, PL_FONT_VIEWS)) {
				if (y > height() - 14) {
					return;
				}
				text(c, line, x, y, right - x, PL_FONT_VIEWS, GRAY(0x4));
				y += 16;
			}
		}
	}

	Page m_page;
	QString m_title, m_explanation;
	QStringList m_rows;
	PanelList m_list;
	PanelCheckbox m_showAll;
	std::vector<SystemResource> m_resources;
};

QPointer<ResourceListWindow> &systemWindow() {
	static QPointer<ResourceListWindow> window;
	return window;
}

QPointer<ResourceListWindow> &extensionsWindow() {
	static QPointer<ResourceListWindow> window;
	return window;
}

} // namespace

void openSystemInformation() {
	if (!systemWindow()) {
		systemWindow() = new ResourceListWindow(ResourceListWindow::Page::System);
	}
	systemWindow()->show();
	systemWindow()->raise();
}

void openExtensionsManager() {
	if (!extensionsWindow()) {
		extensionsWindow() = new ResourceListWindow(ResourceListWindow::Page::Extensions);
	}
	extensionsWindow()->show();
	extensionsWindow()->raise();
}
