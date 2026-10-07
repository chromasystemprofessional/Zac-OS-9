#include "resourcewindows.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWheelEvent>
#include <algorithm>

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
		m_explanation = page == Page::System
			? "Current system details; viewing does not change this computer."
			: "Read-only list of loaded kernel modules. No load or remove controls are provided.";
		m_rows = page == Page::System ? systemInformation() : extensionRows();
		m_list.frame = page == Page::System ? QRect(12, 78, 496, 326) :
			QRect(12, 96, 240, 352);
		m_list.setItems(m_rows);
		if (!m_rows.isEmpty()) {
			m_list.select(0, false);
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
		if (m_page == Page::Extensions && !m_rows.isEmpty()) {
			const int row = m_list.state.selected;
			if (row >= 0 && row < static_cast<int>(m_resources.size())) {
				const SystemResource &resource = m_resources[static_cast<size_t>(row)];
				text(&pixels.c, resource.name, 270, 118, width() - 286, PL_FONT_SYSTEM);
				pl_hline(&pixels.c, 264, width() - 20, 129, GRAY(0x8));
				pl_hline(&pixels.c, 265, width() - 19, 130, C_WHITE);
				int y = 151;
				for (const QString &line : resource.description.split('\n', Qt::SkipEmptyParts)) {
					text(&pixels.c, line, 270, y, width() - 286);
					y += 18;
				}
				for (const QString &line : resource.metadata) {
					if (y > height() - 18) {
						break;
					}
					text(&pixels.c, line, 270, y, width() - 286);
					y += 18;
				}
			}
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
		if (m_list.press(event->position().toPoint())) {
			update();
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (m_list.move(event->position().toPoint())) {
			update();
		}
	}

	void mouseReleaseEvent(QMouseEvent *) override {
		if (m_list.release()) {
			update();
		}
	}

	void wheelEvent(QWheelEvent *event) override {
		if (m_list.wheel(event->position().toPoint(), event->angleDelta().y())) {
			update();
		}
	}

private:
	QStringList extensionRows() {
		m_resources = resourceChildren("extensions");
		QStringList rows;
		for (const SystemResource &resource : m_resources) {
			rows << resource.name;
		}
		if (rows.isEmpty()) {
			rows << "No loaded kernel modules found";
		}
		return rows;
	}

	Page m_page;
	QString m_title, m_explanation;
	QStringList m_rows;
	PanelList m_list;
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
