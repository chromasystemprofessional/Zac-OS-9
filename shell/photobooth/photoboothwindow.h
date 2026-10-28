/*
 * zacos9-photobooth: Photo Booth, the Apple menu's camera. A live picture
 * from the built-in camera or a connected webcam; Take Photo saves a
 * snapshot to ~/Pictures as "Photo N.png". GStreamer runs the camera and
 * hands frames to the window as RGB.
 */
#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QString>

#include <gst/gst.h>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

/* One camera, by its device name and /dev/video* path. */
struct Camera {
	QString name;
	QString path;
};

/* The live picture, drawn scaled to fit while keeping its shape. */
class PhotoView : public QWidget {
public:
	explicit PhotoView(QWidget *parent = nullptr);
	void setFrame(const QImage &frame);
	QImage frame() const { return m_frame; }
	bool hasFrame() const { return !m_frame.isNull(); }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QImage m_frame;
};

class PhotoBoothWindow : public QMainWindow {
public:
	explicit PhotoBoothWindow(QWidget *parent = nullptr);
	~PhotoBoothWindow() override;

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	/* The cameras, from GStreamer's device monitor (Video/Source), else a
	 * plain /dev/video* scan. */
	static QList<Camera> cameras();

	void startCamera(const QString &path);
	void stopCamera();
	void pullFrame();
	void takePhoto();
	void showMessage(const QString &text);

	PhotoView *m_view = nullptr;
	QComboBox *m_cameras = nullptr;
	QPushButton *m_take = nullptr;
	QLabel *m_status = nullptr;
	QTimer *m_timer = nullptr;

	GstElement *m_pipeline = nullptr;
	GstElement *m_sink = nullptr;
	GstBus *m_bus = nullptr;
};
