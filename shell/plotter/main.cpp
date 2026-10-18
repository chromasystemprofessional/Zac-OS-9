/*
 * zacos9-plotter: Plotter (Mac OS 9's Graphing Calculator, gap plan 8.6).
 * Type y = f(x) and press Return: the graph is drawn on a grid, and the
 * zoom buttons take you closer or further away.
 */

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <climits>
#include <cmath>

#include "expr.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

namespace {

constexpr int W = 440, H = 400, M = 12, GX = M, GY = 46, GW = W - 2 * M, GH = 300;
constexpr uint32_t FACE = GRAY(0xD);

class Plotter : public QWidget {
public:
	Plotter() : m_host(this) {
		setWindowTitle("Plotter");
		setFixedSize(W, H);
		m_edit.rect = QRect(M + 30, 14, W - 2 * M - 30, PL_EDIT_H);
		m_edit.hint = "A function of x, such as sin(x), x^2 - 3x or 2sqrt(abs x). Press Return to draw it.";
		m_edit.setText("sin(x) + x/4");
		const int by = H - M - PL_BUTTON_H;
		m_in = PanelButton("Zoom In", QRect(M, by, 90, PL_BUTTON_H));
		m_in.clicked = [this] { m_span /= 2; update(); };
		m_out = PanelButton("Zoom Out", QRect(M + 98, by, 90, PL_BUTTON_H));
		m_out.clicked = [this] { m_span *= 2; update(); };
		m_draw = PanelButton("Graph", QRect(W - M - 4 - 80, by, 80, PL_BUTTON_H), true);
		m_draw.clicked = [this] { graph(); };
		m_host.edits = { &m_edit };
		m_host.buttons = { &m_in, &m_out, &m_draw };
		m_host.defaultButton = &m_draw;
		m_host.setFocus(&m_edit);
		graph();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		panelText(c, "y =", M, 29);
		paintGraph(c);
		if (!m_error.isEmpty()) {
			panelText(c, m_error, M + 200, H - M - 6, PL_FONT_VIEWS, RGB(0x99, 0, 0), W - 2 * M - 290);
		}
		m_host.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override { m_host.hostPress(e); update(); }
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); update(); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); update(); }
	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		m_host.hostKey(e);
		update();
	}

private:
	PanelHost m_host;
	PanelEdit m_edit;
	PanelButton m_in, m_out, m_draw;
	Expression m_expr;
	QString m_error;
	double m_span = 20; /* x from -span/2 to span/2; y the same per pixel */

	void graph() {
		Expression e;
		if (e.parse(m_edit.text, &m_error)) {
			m_expr = e;
			m_error.clear();
		}
		update();
	}

	void paintGraph(pl_canvas *c) {
		pl_fill(c, GX, GY, GX + GW - 1, GY + GH - 1, C_WHITE);
		const double perPx = m_span / GW;
		const double x0 = -m_span / 2, y1 = GH * perPx / 2;
		auto sx = [&](double x) { return GX + int(std::lround((x - x0) / perPx)); };
		auto sy = [&](double y) { return GY + int(std::lround((y1 - y) / perPx)); };
		/* Grid lines a power of ten apart, about 30 px or more. */
		double step = std::pow(10, std::ceil(std::log10(30 * perPx)));
		if (step / perPx > 120) step /= 5; else if (step / perPx > 60) step /= 2;
		for (double g = std::ceil(x0 / step) * step; g <= -x0; g += step) {
			pl_vline(c, sx(g), GY, GY + GH - 1, GRAY(0xD));
		}
		for (double g = std::ceil(-y1 / step) * step; g <= y1; g += step) {
			pl_hline(c, GX, GX + GW - 1, sy(g), GRAY(0xD));
		}
		pl_hline(c, GX, GX + GW - 1, sy(0), C_BLACK);
		pl_vline(c, sx(0), GY, GY + GH - 1, C_BLACK);
		/* The curve: one sample per column, joined where it doesn't jump away. */
		const uint32_t ink = RGB(0x33, 0x33, 0x99);
		int lastY = INT_MIN;
		for (int col = 0; col < GW; col++) {
			const double y = m_expr.valid() ? m_expr(x0 + col * perPx) : NAN;
			if (!std::isfinite(y) || std::fabs(y) > 1e6) {
				lastY = INT_MIN;
				continue;
			}
			const int py = sy(y);
			if (lastY != INT_MIN && std::abs(py - lastY) < GH) {
				const int a = std::clamp(std::min(py, lastY), GY, GY + GH - 1);
				const int b = std::clamp(std::max(py, lastY), GY, GY + GH - 1);
				if (std::max(py, lastY) >= GY && std::min(py, lastY) < GY + GH) {
					pl_vline(c, GX + col, a, b, ink);
					pl_vline(c, GX + col + 1 < GX + GW ? GX + col + 1 : GX + col, a, b, ink);
				}
			}
			lastY = py;
		}
		pl_outline(c, GX - 1, GY - 1, GX + GW, GY + GH, C_BLACK);
		panelText(c, QString("x from %1 to %2").arg(x0).arg(-x0), GX + GW - 140, GY + GH + 14, PL_FONT_VIEWS);
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Plotter");
	QGuiApplication::setDesktopFileName("zacos9-plotter"); /* Wayland app_id */
	platinumShellInit();
	Plotter p;
	p.show();
	return app.exec();
}
