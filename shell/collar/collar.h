#pragma once

#include <QSettings>
#include <QWidget>
#include "soundclient.h"
#include "systemcontrols.h"

class Collar : public QWidget {
public:
	enum Module { Volume, Network, Bluetooth, Brightness, Power, ModuleCount };
	static constexpr int Height = 24, Cap = 17, Scroll = 14, Cell = 32, Tab = 14;
	explicit Collar(bool monitor = true);
	bool collapsed() const { return m_collapsed; }
	int visibleModules() const { return m_visible; }
	QRect moduleRect(int module) const;
	int moduleAt(QPoint point) const;
	void setCollapsed(bool collapsed);
	void setVisibleModules(int count);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	SoundClient m_sound;
	SystemControls m_controls;
	QSettings m_settings;
	bool m_collapsed = false, m_moving = false, m_resizing = false, m_dragged = false;
	int m_visible = ModuleCount, m_first = 0, m_focus = 0, m_pressed = -1;
	int m_bottom = 4, m_startBottom = 0, m_startVisible = ModuleCount;
	QPoint m_start, m_pressPoint;
	QString m_soundError;
	void applyGeometry();
	void save();
	void openModule(int module);
	void launch(const QString &program, const QStringList &args = {});
	QString description(int module) const;
};
