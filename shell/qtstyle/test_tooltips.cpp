#include "zacos9style.h"

#include <QApplication>
#include <QEvent>
#include <QHelpEvent>
#include <QImage>
#include <QPainter>
#include <QStyleOption>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include "settings.h"
}

class TestStyle : public Zacos9Style {
public:
	using Zacos9Style::eventFilter;
};

static bool check(bool ok, const char *message) {
	std::puts(ok ? message : (QString("FAIL: ") + message).toLocal8Bit().constData());
	return ok;
}

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
	QApplication app(argc, argv);
	TestStyle style;
	QWidget widget;
	style.polish(&widget);
	QHelpEvent tooltip(QEvent::ToolTip, QPoint(1, 1), QPoint(1, 1));
	bool ok = true;

	if (!pl_setting_set("hint-bubbles", "0")) {
		return 1;
	}
	ok &= check(style.eventFilter(&widget, &tooltip),
		"disabled Hint Bubbles consume tooltip events");
	ok &= check(style.styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, &widget, nullptr) >= 600000,
		"disabled Hint Bubbles do not wake after a normal hover");

	if (!pl_setting_set("hint-bubbles", "1")) {
		return 1;
	}
	ok &= check(!style.eventFilter(&widget, &tooltip),
		"enabled Hint Bubbles pass tooltip events to Qt");

	QStyleOption option;
	option.rect = QRect(0, 0, 5, 5);
	option.palette.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xCC));
	option.palette.setColor(QPalette::ToolTipText, Qt::black);
	QImage image(5, 5, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	style.drawPrimitive(QStyle::PE_PanelTipLabel, &option, &painter, &widget);
	painter.end();
	ok &= check(image.pixelColor(2, 2) == QColor(0xFF, 0xFF, 0xCC) &&
		image.pixelColor(0, 0) == Qt::black,
		"Hint Bubbles use a pale yellow face and a one-pixel dark frame");

	style.unpolish(&widget);
	return ok ? 0 : 1;
}
