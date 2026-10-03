#pragma once

#include <QDialog>

#include "pixels.h"

/*
 * A Platinum alert: caution icon, message, and a default button with an
 * optional cancel button, framed by zacos9-wm as a movable modal dialog.
 * Return chooses the default button; Escape or ⌘. cancels.
 *
 * Layout follows the HIG's spacing rules where it states them (buttons
 * 58 x 20 minimum, 12 px apart); margins are estimates. TODO: measure.
 */
class Alert : public QDialog {
public:
	/* An optional third button (such as "Don't Save") sits at the left. */
	Alert(const QString &message, const QString &okLabel, const QString &cancelLabel,
		const QString &otherLabel = QString());

	/* Shows the alert modally; true if the default button was chosen. */
	static bool ask(const QString &message, const QString &ok = "OK",
		const QString &cancel = "Cancel");

	/* With a third button: Ok (the default), Cancel or Other. ⌘D also
	 * chooses Other, as "Don't Save" did on the Mac. */
	enum Choice { Cancel = 0, Ok = 1, Other = 2 };
	static Choice choose(const QString &message, const QString &ok, const QString &cancel,
		const QString &other);

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
		int result = 0;
	};
	int buttonAt(QPoint p) const;

	std::vector<std::unique_ptr<Text>> m_lines;
	std::vector<Button> m_buttons; /* default last (rightmost) */
	int m_tracking = -1;           /* button held down */
	bool m_inside = false;
};
