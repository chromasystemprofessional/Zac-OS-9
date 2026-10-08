#include "zacos9style.h"

#include <QAbstractScrollArea>
#include <QDir>
#include <QFileDialog>
#include <QEvent>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QImage>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleOption>
#include <algorithm>

extern "C" {
#include "settings.h"
#include "widgets.h"
}

namespace {

/* A canvas over a QImage, for the C drawing code in lib/. */
struct Canvas {
	QImage img;
	pl_canvas c;

	Canvas(int w, int h)
		: img(std::max(w, 1), std::max(h, 1), QImage::Format_ARGB32_Premultiplied) {
		img.fill(Qt::transparent);
		c = { reinterpret_cast<uint32_t *>(img.bits()), static_cast<int>(img.bytesPerLine() / 4),
			0, 0, img.width(), img.height() };
	}

	void blit(QPainter *p, const QPoint &at) const {
		p->save();
		p->setRenderHint(QPainter::SmoothPixmapTransform, false);
		p->drawImage(at, img);
		p->restore();
	}
};

QColor fromArgb(uint32_t c) {
	return QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* Scroll bar geometry, along the bar, from lib/widgets.c: an arrow box and
 * its separator line at each end, and a fixed 17-pixel thumb (SB_THUMB and
 * the two lines either side) that travels between the separators. */
constexpr int SB_END = SB_ARROW + 2;
constexpr int SB_SLIDER = SB_THUMB + 2;

struct SbGeom {
	int length;  /* along the bar */
	int groove0; /* where the groove starts */
	int grooveLen;
	int range;   /* how far the thumb may travel */
};

SbGeom sbGeom(int length) {
	SbGeom g;
	g.length = length;
	g.groove0 = SB_END - 1;
	g.grooveLen = std::max(0, length - 2 * (SB_END - 1));
	g.range = sb_thumb_range(length);
	return g;
}

} // namespace

Zacos9Style::Zacos9Style() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {
}

void Zacos9Style::polish(QPalette &p) {
	QProxyStyle::polish(p);
	const QColor face(0xDD, 0xDD, 0xDD), dim(0x88, 0x88, 0x88);
	const QColor highlight = fromArgb(pl_highlight_current());
	p.setColor(QPalette::Window, face);
	p.setColor(QPalette::Button, face);
	p.setColor(QPalette::Base, Qt::white);
	p.setColor(QPalette::AlternateBase, QColor(0xEE, 0xEE, 0xEE));
	p.setColor(QPalette::WindowText, Qt::black);
	p.setColor(QPalette::ButtonText, Qt::black);
	p.setColor(QPalette::Text, Qt::black);
	p.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xCC));
	p.setColor(QPalette::ToolTipText, Qt::black);
	p.setColor(QPalette::Highlight, highlight);
	p.setColor(QPalette::HighlightedText, Qt::black);
	p.setColor(QPalette::Light, Qt::white);
	p.setColor(QPalette::Dark, dim);
	p.setColor(QPalette::Mid, QColor(0xBB, 0xBB, 0xBB));
	p.setColor(QPalette::Shadow, Qt::black);
	for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::ButtonText, QPalette::Text }) {
		p.setColor(QPalette::Disabled, role, dim);
	}
}

void Zacos9Style::polish(QWidget *widget) {
	QProxyStyle::polish(widget);
	if (qobject_cast<QFileDialog *>(widget)) {
		widget->installEventFilter(this);
	}
}

void Zacos9Style::unpolish(QWidget *widget) {
	widget->removeEventFilter(this);
	QProxyStyle::unpolish(widget);
}

bool Zacos9Style::eventFilter(QObject *object, QEvent *event) {
	if (event->type() == QEvent::Show) {
		if (auto *dialog = qobject_cast<QFileDialog *>(object)) {
			if (dialog->property("zacos9-file-portal-dialog").toBool()) {
				return QProxyStyle::eventFilter(object, event);
			}
			auto places = dialog->sidebarUrls();
			auto addPlace = [&places](const QString &path) {
				if (path.isEmpty()) {
					return;
				}
				const QUrl url = QUrl::fromLocalFile(path);
				if (!places.contains(url)) {
					places.append(url);
				}
			};
			const QString desktopPath = QStandardPaths::writableLocation(
				QStandardPaths::DesktopLocation);
			addPlace(desktopPath);
			addPlace(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
				"/zacos9/Zacintosh HD");
			for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
				const QString path = volume.rootPath();
				if (!volume.isValid() || !volume.isReady() ||
						path == "/proc" || path.startsWith("/proc/") ||
						path == "/sys" || path.startsWith("/sys/") ||
						path == "/dev" || path.startsWith("/dev/") ||
						path == "/run" || path.startsWith("/run/") ||
						path == "/snap" || path.startsWith("/snap/")) {
					continue;
				}
				addPlace(path);
			}
			dialog->setSidebarUrls(places);
			if (dialog->acceptMode() == QFileDialog::AcceptSave) {
				const QString current = dialog->directory().absolutePath();
				const QString home = QDir::homePath();
				const QString working = QDir::currentPath();
				if (current.isEmpty() || QDir::cleanPath(current) == QDir::cleanPath(home) ||
						QDir::cleanPath(current) == QDir::cleanPath(working)) {
					dialog->setDirectory(desktopPath);
				}
			}
		}
	}
	return QProxyStyle::eventFilter(object, event);
}

int Zacos9Style::pixelMetric(PixelMetric metric, const QStyleOption *option,
		const QWidget *widget) const {
	switch (metric) {
	case PM_ScrollBarExtent:
		return SB_WIDTH;
	case PM_ScrollBarSliderMin:
		return SB_SLIDER;
	case PM_IndicatorWidth:
	case PM_IndicatorHeight:
		return PL_CHECKBOX_SIZE;
	case PM_ExclusiveIndicatorWidth:
	case PM_ExclusiveIndicatorHeight:
		return PL_RADIO_SIZE;
	default:
		return QProxyStyle::pixelMetric(metric, option, widget);
	}
}

QSize Zacos9Style::sizeFromContents(ContentsType type, const QStyleOption *option,
		const QSize &size, const QWidget *widget) const {
	QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
	if (type == CT_PushButton) {
		s.setHeight(std::max(s.height(), PL_BUTTON_H));
		s.setWidth(std::max(s.width(), PL_BUTTON_MIN_W));
	}
	return s;
}

void Zacos9Style::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
		QPainter *painter, const QWidget *widget) const {
	const QRect r = option->rect;
	const bool enabled = option->state & State_Enabled;
	const bool pressed = option->state & State_Sunken;
	switch (element) {
	case PE_IndicatorCheckBox: {
		Canvas cv(PL_CHECKBOX_SIZE + 2, PL_CHECKBOX_SIZE);
		pl_checkbox_paint(&cv.c, 0, 0,
			option->state & State_NoChange ? PL_CHECK_MIXED
				: option->state & State_On ? PL_CHECK_ON : PL_CHECK_OFF,
			pressed, enabled, nullptr);
		cv.blit(painter, r.topLeft());
		return;
	}
	case PE_IndicatorRadioButton: {
		Canvas cv(PL_RADIO_SIZE, PL_RADIO_SIZE);
		pl_radio_paint(&cv.c, 0, 0, option->state & State_On, pressed, enabled, nullptr);
		cv.blit(painter, r.topLeft());
		return;
	}
	case PE_PanelLineEdit:
	case PE_FrameLineEdit: {
		/* A line edit inside a combo box or spin box is framed by that. */
		if (widget && widget->parentWidget() && qobject_cast<const QAbstractScrollArea *>(widget)) {
			break;
		}
		/* The frame is drawn one pixel outside the rectangle it is given. */
		Canvas cv(r.width() + 2, r.height() + 2);
		pl_edit_frame_paint(&cv.c, 1, 1, r.width(), r.height());
		cv.blit(painter, r.topLeft() - QPoint(1, 1));
		return;
	}
	default:
		break;
	}
	QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void Zacos9Style::drawControl(ControlElement element, const QStyleOption *option,
		QPainter *painter, const QWidget *widget) const {
	if (element == CE_PushButtonBevel) {
		const auto *b = qstyleoption_cast<const QStyleOptionButton *>(option);
		const QRect r = option->rect;
		unsigned flags = 0;
		if (b && (b->features & QStyleOptionButton::DefaultButton)) {
			flags |= PL_BUTTON_DEFAULT;
		}
		if (option->state & State_Sunken) {
			flags |= PL_BUTTON_PRESSED;
		}
		if (!(option->state & State_Enabled)) {
			flags |= PL_BUTTON_DISABLED;
		}
		const int pad = (flags & PL_BUTTON_DEFAULT) ? PL_BUTTON_RING : 0;
		const int y = std::max((r.height() - PL_BUTTON_H) / 2, pad);
		Canvas cv(r.width(), r.height());
		pl_button_paint(&cv.c, pad, y, std::max(r.width() - 2 * pad, 8), nullptr, flags);
		cv.blit(painter, r.topLeft());
		return;
	}
	QProxyStyle::drawControl(element, option, painter, widget);
}

QRect Zacos9Style::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
		SubControl sub, const QWidget *widget) const {
	const auto *sb = control == CC_ScrollBar ? qstyleoption_cast<const QStyleOptionSlider *>(option)
		: nullptr;
	if (!sb) {
		return QProxyStyle::subControlRect(control, option, sub, widget);
	}
	const bool vertical = sb->orientation == Qt::Vertical;
	const QRect r = sb->rect;
	const SbGeom g = sbGeom(vertical ? r.height() : r.width());
	/* A span along the bar, as a rectangle. */
	auto along = [&](int a0, int len) {
		return vertical ? QRect(r.x(), r.y() + a0, r.width(), len)
			: QRect(r.x() + a0, r.y(), len, r.height());
	};
	const int travel = QStyle::sliderPositionFromValue(sb->minimum, sb->maximum,
		sb->sliderPosition, g.range, sb->upsideDown);
	const int thumb0 = g.groove0 + travel;
	switch (sub) {
	case SC_ScrollBarSubLine:
		return along(0, SB_END);
	case SC_ScrollBarAddLine:
		return along(g.length - SB_END, SB_END);
	case SC_ScrollBarGroove:
		return along(g.groove0, g.grooveLen);
	case SC_ScrollBarSlider:
		return along(thumb0, SB_SLIDER);
	case SC_ScrollBarSubPage:
		return along(g.groove0, std::max(0, thumb0 - g.groove0));
	case SC_ScrollBarAddPage:
		return along(thumb0 + SB_SLIDER,
			std::max(0, g.groove0 + g.grooveLen - (thumb0 + SB_SLIDER)));
	default:
		return QRect();
	}
}

void Zacos9Style::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
		QPainter *painter, const QWidget *widget) const {
	const auto *sb = control == CC_ScrollBar ? qstyleoption_cast<const QStyleOptionSlider *>(option)
		: nullptr;
	if (!sb) {
		QProxyStyle::drawComplexControl(control, option, painter, widget);
		return;
	}
	const bool vertical = sb->orientation == Qt::Vertical;
	const QRect r = sb->rect;
	const SbGeom g = sbGeom(vertical ? r.height() : r.width());
	pl_scrollbar bar;
	bar.vertical = vertical;
	bar.length = g.length;
	bar.enabled = (option->state & State_Enabled) && sb->maximum > sb->minimum;
	bar.thumb = QStyle::sliderPositionFromValue(sb->minimum, sb->maximum, sb->sliderPosition,
		g.range, sb->upsideDown);
	Canvas cv(r.width(), r.height());
	pl_scrollbar_paint(&cv.c, 0, 0, &bar, pl_accent_current());
	cv.blit(painter, r.topLeft());
}

QStyle *Zacos9StylePlugin::create(const QString &key) {
	return key.compare(QLatin1String("zacos9"), Qt::CaseInsensitive) == 0 ? new Zacos9Style : nullptr;
}
