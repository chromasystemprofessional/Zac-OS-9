#pragma once

#include <QProxyStyle>
#include <QStylePlugin>

/*
 * ZacOS 9's Qt style: other people's Qt applications drawn with the same
 * Platinum controls as ZacOS 9's own (lib/widgets.c) - push buttons, check
 * boxes, radio buttons, text field frames and scroll bars - on a Platinum
 * palette. Everything else (menus, tabs, sliders, ...) is Qt's Fusion style
 * in those colours. Selected with QT_STYLE_OVERRIDE=zacos9.
 */
class Zacos9Style : public QProxyStyle {
public:
	Zacos9Style();

	void polish(QPalette &palette) override;
	void polish(QWidget *widget) override;
	void unpolish(QWidget *widget) override;
	void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
		const QWidget *widget) const override;
	void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
		const QWidget *widget) const override;
	void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
		QPainter *painter, const QWidget *widget) const override;
	QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
		SubControl subControl, const QWidget *widget) const override;
	int pixelMetric(PixelMetric metric, const QStyleOption *option,
		const QWidget *widget) const override;
	QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
		const QWidget *widget) const override;
protected:
	bool eventFilter(QObject *object, QEvent *event) override;
};

class Zacos9StylePlugin : public QStylePlugin {
	Q_OBJECT
	Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "zacos9style.json")
public:
	QStyle *create(const QString &key) override;
};
