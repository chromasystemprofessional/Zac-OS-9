#pragma once

#include <QWidget>

class QScreen;

/*
 * The desktop pattern on a screen other than the main one. With several
 * screens only the main one has the menu bar and the desktop's icons; the
 * others show just the pattern, as on a Mac. One layer-shell surface per
 * extra screen, beneath all windows.
 */
class SecondaryDesktop : public QWidget {
public:
	explicit SecondaryDesktop(QScreen *screen);
	QScreen *screen() const { return m_screen; }

	/* One of these on every screen but the main one, kept in step as screens
	 * come and go and the Monitors panel changes which is main. */
	static void keepInStep();

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QScreen *m_screen;
	int m_pattern = -1;
	void loadPattern();
};
