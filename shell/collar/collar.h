#pragma once

#include <QList>
#include <QSettings>
#include <QWidget>
#include <functional>
#include <vector>
#include "collarart.h"
#include "displaycontrols.h"
#include "kdeconnect.h"
#include "keychain.h"
#include "panelkit.h"
#include "soundclient.h"
#include "systemcontrols.h"

class Collar : public QWidget, public CollarParts {
public:
	struct Module {
		Kind kind;
		QString device;
		bool operator==(const Module &o) const { return kind == o.kind && device == o.device; }
	};
	/* The Collar control panel's choice (desktop.conf "collar-visibility"). */
	enum Visibility { Show, Hide, HotKey };

	explicit Collar(bool monitor = true);
	bool collapsed() const { return m_collapsed; }
	int visibleModules() const;
	int bottomOffset() const { return m_bottom; }
	const QList<Module> &modules() const { return m_modules; }
	int moduleIndex(Kind kind, const QString &device = QString()) const;
	QRect moduleRect(int index) const;
	int moduleAt(QPoint point) const;
	QString menuLabel(int index) const;
	void setCollapsed(bool collapsed);
	void setVisibleModules(int count);
	DisplayControls &displays() { return m_displays; }
	KdeConnect &kdeConnect() { return m_kdeConnect; }
	Keychains &keychains() { return m_keychains; }
	void rebuildModules();
	/* Shows or hides The Collar in hot-key mode (zacos9-collar --toggle). */
	void toggle();
	/* Rereads the Collar control panel's settings from desktop.conf. */
	void applySettings();
	Visibility visibility() const { return m_visibility; }
	bool hiddenByHotKey() const { return m_hidden; }
	static Visibility visibilitySetting();
	static pl_font menuFont();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void hideEvent(QHideEvent *) override;

private:
	enum Part { None, CloseBox, LeftArrow, RightArrow, GripPart, ModulePart };
	SoundClient m_sound;
	SystemControls m_controls;
	DisplayControls m_displays;
	KdeConnect m_kdeConnect;
	Keychains m_keychains;
	QSettings m_settings;
	Visibility m_visibility = Show;
	QList<Module> m_modules;
	bool m_collapsed = false, m_hidden = false, m_applied = false, m_moving = false, m_resizing = false, m_dragged = false, m_dragSound = false;
	int m_wanted = 0, m_first = 0, m_focus = 0, m_pressed = -1;
	Part m_part = None;
	int m_bottom = 0, m_startBottom = 0, m_startVisible = 0;
	QPoint m_start, m_pressPoint;
	QString m_soundError;
	Part partAt(QPoint point) const;
	void applyGeometry();
	void save();
	void stopMoving(bool cancel);
	void openModule(int index);
	void addPhoneItems(const QString &device, std::vector<PopupItem> &items,
		std::vector<std::function<void()>> &actions);
	void addKeychainItems(std::vector<PopupItem> &items, std::vector<std::function<void()>> &actions, int &checked);
	void launch(const QString &program, const QStringList &args = {});
	QString description(int index) const;
	bool active(int index) const;
};
