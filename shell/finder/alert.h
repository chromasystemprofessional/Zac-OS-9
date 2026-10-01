#pragma once

#include <QDialog>

#include "pixels.h"

/*
 * A Platinum alert: caution icon, message, and a default button with an
 * optional cancel button, framed by platinum-wm as a movable modal dialog.
 * Return chooses the default button; Escape or ⌘. cancels.
 *
 * Layout follows the HIG's spacing rules where it states them (buttons
 * 58 x 20 minimum, 12 px apart); margins are estimates. TODO: measure.
 */
class Alert : public QDialog {
public:
	Alert(const QString &message, const QString &okLabel, const QString &cancelLabel);

	/* Shows the alert modally; true if the default button was chosen. */
	static bool ask(const QString &message, const QString &ok = "OK",
		const QString &cancel = "Cancel");

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	struct Button {
		QRect rect;
		std::unique_ptr<Text> label;
		bool isDefault = false;
	};
	int buttonAt(QPoint p) const;

	std::vector<std::unique_ptr<Text>> m_lines;
	std::vector<Button> m_buttons; /* default last (rightmost) */
	int m_tracking = -1;           /* button held down */
	bool m_inside = false;
};
