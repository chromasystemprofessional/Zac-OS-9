#pragma once

/*
 * zacos9-picture: Picture Viewer, what a double-clicked picture file opens
 * in. The window opens fitted to the screen (a picture smaller than the
 * screen shows at actual size) and zooms in and out in steps from the
 * View menu, the buttons at its foot, ⌘+/⌘− or ⌘ and the scroll wheel.
 */

#include <QImage>
#include <QMainWindow>
#include <QPixmap>

class QLabel;
class QPushButton;
class QScrollArea;

class PictureView : public QWidget {
public:
	explicit PictureView(QWidget *parent = nullptr);
	void setImage(const QImage &image);
	void setScale(double scale);
	double scale() const { return m_scale; }
	const QImage &image() const { return m_image; }
	/* Where the picture is drawn within the view (centred if the view is larger). */
	QRect pictureRect() const;

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QImage m_image;
	QPixmap m_scaled; /* the reduced picture, smoothed once rather than on every paint */
	double m_scale = 1;
	void rescale();
};

class PictureWindow : public QMainWindow {
public:
	explicit PictureWindow(QWidget *parent = nullptr);
	bool open(const QString &path, QString *error = nullptr);
	QString path() const { return m_path; }
	double scale() const;
	/* The largest scale at which the whole picture fits on the screen. */
	double fitScale() const;
	/* The window's content size for the picture at `scale`, clamped to the screen. */
	QSize sizeFor(double scale) const;
	void zoomIn();
	void zoomOut();
	void actualSize();
	void fitToScreen();
	QString zoomText() const;
	/* File > Open: the chosen picture opens here if this window is empty, else in a new one. */
	bool chooseFile();
	QPushButton *zoomInButton() const { return m_in; }
	QPushButton *zoomOutButton() const { return m_out; }
	QScrollArea *scrollArea() const { return m_scroll; }

	static constexpr double MinScale = 1.0 / 16, MaxScale = 16;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QScrollArea *m_scroll;
	PictureView *m_view;
	QWidget *m_bar;
	QLabel *m_zoom;
	QPushButton *m_out, *m_in;
	QString m_path;
	QPoint m_grab;
	QSize available() const;
	/* Zooms to `scale`, keeping the picture point under `anchor` (viewport coordinates) still. */
	void zoomTo(double scale, QPoint anchor = QPoint(-1, -1), bool resizeWindow = true);
};
