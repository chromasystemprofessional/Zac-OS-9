#include "printwindow.h"

#include <QDateTime>
#include <QDialog>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <cmath>

#include "panelkit.h"
#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

namespace {

struct Paper {
	const char *name;
	QPageSize::PageSizeId id;
};
const Paper PAPERS[] = {
	{ "US Letter", QPageSize::Letter },
	{ "US Legal", QPageSize::Legal },
	{ "A4", QPageSize::A4 },
	{ "A5", QPageSize::A5 },
	{ "B5", QPageSize::B5 },
};
const int SCALES[] = { 50, 75, 100, 125, 150, 200 };

QPageSize::PageSizeId paperId(const QString &name) {
	for (const Paper &p : PAPERS) {
		if (name == QLatin1String(p.name)) {
			return p.id;
		}
	}
	return QPageSize::A4;
}

/* Letter where the Imperial system is used (the US), A4 elsewhere. */
QString defaultPaper() {
	return QLocale::system().measurementSystem() == QLocale::ImperialUSSystem ? "US Letter" : "A4";
}

QString setting(const char *key) {
	char value[64];
	return pl_setting(key, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

constexpr double MARGIN = 36;   /* points: half an inch */
constexpr double HEADER = 22;   /* title and date, and a rule */
constexpr double FOOTER = 18;   /* the page number */

} // namespace

QStringList pageSetupPapers() {
	QStringList names;
	for (const Paper &p : PAPERS) {
		names << p.name;
	}
	return names;
}

QList<int> pageSetupScales() {
	return QList<int>(std::begin(SCALES), std::end(SCALES));
}

PageSetup loadPageSetup() {
	PageSetup s;
	s.paper = setting("page-paper");
	if (!pageSetupPapers().contains(s.paper)) {
		s.paper = defaultPaper();
	}
	s.landscape = setting("page-orientation") == "landscape";
	const int scale = setting("page-scale").toInt();
	s.scale = pageSetupScales().contains(scale) ? scale : 100;
	return s;
}

void savePageSetup(const PageSetup &setup) {
	pl_setting_set("page-paper", setup.paper.toUtf8().constData());
	pl_setting_set("page-orientation", setup.landscape ? "landscape" : nullptr);
	pl_setting_set("page-scale", setup.scale == 100 ? nullptr
		: QByteArray::number(setup.scale).constData());
}

int writeWindowPdf(const QString &title, const QImage &content, const PageSetup &setup,
		const QString &pdf) {
	if (content.isNull()) {
		return 0;
	}
	QPdfWriter writer(pdf);
	writer.setTitle(title);
	writer.setCreator("ZacOS 9 Sniffer");
	writer.setResolution(72); /* a unit is a point */
	writer.setPageSize(QPageSize(paperId(setup.paper)));
	writer.setPageOrientation(setup.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
	writer.setPageMargins(QMarginsF(MARGIN, MARGIN, MARGIN, MARGIN), QPageLayout::Point);
	const QRectF area = writer.pageLayout().paintRect(QPageLayout::Point);
	const double width = area.width(), body = area.height() - HEADER - FOOTER;

	/* Screen pixels at the chosen scale, shrunk to fit across. */
	double scale = setup.scale / 100.0;
	scale = std::min(scale, width / content.width());
	const int slice = std::max(1, int(std::floor(body / scale)));
	const int pages = std::max(1, (content.height() + slice - 1) / slice);

	QPainter p;
	if (!p.begin(&writer)) {
		return 0;
	}
	QFont font = p.font();
	font.setPointSizeF(9);
	const QString date = QLocale::system().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
	for (int page = 0; page < pages; page++) {
		if (page > 0) {
			writer.newPage();
		}
		p.setFont(font);
		p.setPen(Qt::black);
		const QRectF head(0, 0, width, HEADER - 6);
		p.drawText(head, Qt::AlignLeft | Qt::AlignBottom, title);
		p.drawText(head, Qt::AlignRight | Qt::AlignBottom, date);
		p.drawLine(QPointF(0, HEADER - 3), QPointF(width, HEADER - 3));
		p.drawText(QRectF(0, HEADER + body, width, FOOTER), Qt::AlignHCenter | Qt::AlignBottom,
			QString("Page %1 of %2").arg(page + 1).arg(pages));
		const int top = page * slice;
		const int rows = std::min(slice, content.height() - top);
		/* Pixel art stays sharp: no smoothing. */
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
		p.drawImage(QRectF(0, HEADER, content.width() * scale, rows * scale), content,
			QRect(0, top, content.width(), rows));
	}
	return p.end() ? pages : 0;
}

namespace {

constexpr uint32_t BG = GRAY(0xD);

/* Page Setup: paper, orientation and scale, as the Mac's LaserWriter
 * Page Setup asked. */
class PageSetupDialog : public QDialog {
public:
	PageSetupDialog() {
		setWindowTitle("Page Setup");
		m_setup = loadPageSetup();
		setFixedSize(W, H);
		m_paper.rect = QRect(LABEL + 8, 20, 150, PL_POPUP_H);
		for (const QString &name : pageSetupPapers()) {
			m_paper.items.emplace_back(name);
		}
		m_paper.selected = pageSetupPapers().indexOf(m_setup.paper);
		m_paper.chosen = [this](int row) {
			m_paper.selected = row;
			m_setup.paper = pageSetupPapers().at(row);
		};
		m_orientation.add("Portrait", QPoint(LABEL + 8, 60));
		m_orientation.add("Landscape", QPoint(LABEL + 98, 60));
		m_orientation.selected = m_setup.landscape ? 1 : 0;
		m_orientation.changed = [this](int i) { m_setup.landscape = i == 1; };
		m_scale.rect = QRect(LABEL + 8, 88, 80, PL_POPUP_H);
		for (int scale : pageSetupScales()) {
			m_scale.items.emplace_back(QString("%1%").arg(scale));
		}
		m_scale.selected = pageSetupScales().indexOf(m_setup.scale);
		m_scale.chosen = [this](int row) {
			m_scale.selected = row;
			m_setup.scale = pageSetupScales().at(row);
		};
		const int by = H - 14 - PL_BUTTON_H;
		m_cancel = PanelButton("Cancel", QRect(W - 12 - 2 * 72 - 10, by, 72, PL_BUTTON_H));
		m_cancel.clicked = [this] { reject(); };
		m_ok = PanelButton("OK", QRect(W - 12 - 72, by, 72, PL_BUTTON_H), true);
		m_ok.clicked = [this] {
			savePageSetup(m_setup);
			accept();
		};
		m_host.buttons = { &m_cancel, &m_ok };
		m_host.radios = { &m_orientation };
		m_host.popups = { &m_paper, &m_scale };
		m_host.defaultButton = &m_ok;
		m_host.cancelButton = &m_cancel;
	}

protected:
	void showEvent(QShowEvent *e) override {
		QDialog::showEvent(e);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_fill(&px.c, 0, 0, width() - 1, height() - 1, BG);
		panelLabel(&px.c, "Paper:", LABEL, 34);
		panelLabel(&px.c, "Orientation:", LABEL, 70);
		panelLabel(&px.c, "Scale:", LABEL, 102);
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
	static constexpr int W = 320, H = 168, LABEL = 96;
	PageSetup m_setup;
	PanelPopup m_paper, m_scale;
	PanelRadios m_orientation;
	PanelButton m_cancel, m_ok;
	PanelHost m_host{ this };
};

} // namespace

bool showPageSetup() {
	PageSetupDialog dialog;
	return dialog.exec() == QDialog::Accepted;
}
