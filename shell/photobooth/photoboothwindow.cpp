/*
 * zacos9-photobooth: Photo Booth, the Apple menu's camera. A live picture
 * from the built-in camera or a connected webcam; Take Photo saves a
 * snapshot to ~/Pictures as "Photo N.png". GStreamer runs the camera and
 * hands frames to the window as RGB.
 */
#include "photoboothwindow.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <gst/app/gstappsink.h>
#include <gst/video/video-frame.h>

/* ---- the live picture ---------------------------------------------------- */

PhotoView::PhotoView(QWidget *parent) : QWidget(parent) {
	setMinimumSize(320, 240);
}

void PhotoView::setFrame(const QImage &frame) {
	m_frame = frame;
	update();
}

void PhotoView::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	if (m_frame.isNull()) {
		p.setPen(Qt::white);
		p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No camera"));
		return;
	}
	/* Scaled to fit the window, keeping the picture's shape, centred. */
	const QSizeF scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
	const QRectF target(QPointF((width() - scaled.width()) / 2, (height() - scaled.height()) / 2),
		scaled);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, m_frame);
}

/* ---- the window ---------------------------------------------------------- */

/* decodebin grows its output pad once it has probed the stream; that is
 * when it can be joined to videoconvert. */
static void on_pad_added(GstElement *, GstPad *pad, gpointer data) {
	auto *convert = static_cast<GstElement *>(data);
	GstPad *sink = gst_element_get_static_pad(convert, "sink");
	if (!gst_pad_is_linked(sink)) {
		gst_pad_link(pad, sink);
	}
	gst_object_unref(sink);
}

PhotoBoothWindow::PhotoBoothWindow(QWidget *parent) : QMainWindow(parent) {
	setWindowTitle(QStringLiteral("Photo Booth"));

	auto *central = new QWidget(this);
	auto *layout = new QVBoxLayout(central);
	layout->setContentsMargins(8, 8, 8, 8);

	m_view = new PhotoView(central);
	layout->addWidget(m_view, 1);

	auto *row = new QHBoxLayout;
	m_cameras = new QComboBox(central);
	for (const Camera &camera : cameras()) {
		m_cameras->addItem(camera.name, camera.path);
	}
	if (m_cameras->count() <= 1) {
		m_cameras->setEnabled(false);
	}
	row->addWidget(m_cameras);

	m_take = new QPushButton(QStringLiteral("Take Photo"), central);
	row->addWidget(m_take);
	layout->addLayout(row);

	m_status = new QLabel(central);
	m_status->setAlignment(Qt::AlignCenter);
	layout->addWidget(m_status);

	setCentralWidget(central);
	resize(640, 520);

	m_timer = new QTimer(this);
	m_timer->setInterval(33);
	connect(m_timer, &QTimer::timeout, this, &PhotoBoothWindow::pullFrame);
	connect(m_take, &QPushButton::clicked, this, &PhotoBoothWindow::takePhoto);
	connect(m_cameras, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index >= 0) {
			startCamera(m_cameras->itemData(index).toString());
		}
	});

	startCamera(m_cameras->count() > 0 ? m_cameras->itemData(0).toString() : QString());
}

PhotoBoothWindow::~PhotoBoothWindow() {
	stopCamera();
}

void PhotoBoothWindow::closeEvent(QCloseEvent *event) {
	stopCamera();
	QMainWindow::closeEvent(event);
}

QList<Camera> PhotoBoothWindow::cameras() {
	QList<Camera> found;
	/* GStreamer's device monitor names the cameras and their paths. */
	GstDeviceMonitor *monitor = gst_device_monitor_new();
	GstCaps *caps = gst_caps_new_empty_simple("video/x-raw");
	gst_device_monitor_add_filter(monitor, "Video/Source", caps);
	gst_caps_unref(caps);
	gst_device_monitor_start(monitor);
	GList *devices = gst_device_monitor_get_devices(monitor);
	for (GList *l = devices; l; l = l->next) {
		auto *device = static_cast<GstDevice *>(l->data);
		GstStructure *props = gst_device_get_properties(device);
		const char *path = gst_structure_get_string(props, "device.path");
		if (path && *path) {
			Camera c;
			c.path = QString::fromUtf8(path);
			char *name = gst_device_get_display_name(device);
			c.name = name && *name ? QString::fromUtf8(name) : QString::fromUtf8(path);
			g_free(name);
			found.append(c);
		}
		gst_structure_free(props);
	}
	g_list_free_full(devices, gst_object_unref);
	gst_device_monitor_stop(monitor);
	gst_object_unref(monitor);

	/* The monitor can be empty before a camera is probed; /dev/video*
	 * never misses one. */
	if (found.isEmpty()) {
		QDir dev(QStringLiteral("/dev"));
		for (const QString &entry : dev.entryList(QStringList() << QStringLiteral("video*"), QDir::System)) {
			Camera c;
			c.path = QStringLiteral("/dev/") + entry;
			c.name = QStringLiteral("Camera %1").arg(entry.mid(5));
			found.append(c);
		}
	}
	return found;
}

void PhotoBoothWindow::startCamera(const QString &path) {
	stopCamera();

	m_pipeline = gst_pipeline_new("photobooth");
	GstElement *src = gst_element_factory_make("v4l2src", "src");
	GstElement *decode = gst_element_factory_make("decodebin", "decode");
	GstElement *convert = gst_element_factory_make("videoconvert", "convert");
	GstElement *filter = gst_element_factory_make("capsfilter", "filter");
	m_sink = gst_element_factory_make("appsink", "sink");

	if (!m_pipeline || !src || !decode || !convert || !filter || !m_sink) {
		stopCamera();
		m_view->setFrame(QImage());
		showMessage(QStringLiteral("The camera couldn't be opened."));
		m_take->setEnabled(false);
		return;
	}
	if (!path.isEmpty()) {
		g_object_set(src, "device", path.toUtf8().constData(), nullptr);
	}
	g_object_set(filter, "caps", gst_caps_from_string("video/x-raw,format=RGB"), nullptr);
	g_object_set(m_sink, "sync", FALSE, "max-buffers", 1, "drop", TRUE, nullptr);

	gst_bin_add_many(GST_BIN(m_pipeline), src, decode, convert, filter, m_sink, nullptr);
	if (!gst_element_link(src, decode) || !gst_element_link_many(convert, filter, m_sink, nullptr)) {
		stopCamera();
		m_view->setFrame(QImage());
		showMessage(QStringLiteral("The camera couldn't be opened."));
		m_take->setEnabled(false);
		return;
	}
	g_signal_connect(decode, "pad-added", G_CALLBACK(on_pad_added), convert);

	m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
	const GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		stopCamera();
		m_view->setFrame(QImage());
		showMessage(QStringLiteral("The camera couldn't be opened."));
		m_take->setEnabled(false);
		return;
	}
	m_take->setEnabled(true);
	m_timer->start();
}

void PhotoBoothWindow::stopCamera() {
	m_timer->stop();
	if (m_pipeline) {
		gst_element_set_state(m_pipeline, GST_STATE_NULL);
	}
	if (m_bus) {
		gst_object_unref(m_bus);
		m_bus = nullptr;
	}
	if (m_sink) {
		gst_object_unref(m_sink);
		m_sink = nullptr;
	}
	if (m_pipeline) {
		gst_object_unref(m_pipeline);
		m_pipeline = nullptr;
	}
}

void PhotoBoothWindow::pullFrame() {
	/* The camera's latest frame, without waiting; errors and EOS come off
	 * the bus the same way, so the window's events keep flowing. */
	while (GstMessage *msg = gst_bus_pop_filtered(m_bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
		if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
			showMessage(QStringLiteral("The camera stopped."));
		}
		gst_message_unref(msg);
	}
	if (!m_sink) {
		return;
	}
	GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(m_sink), 0);
	if (!sample) {
		return;
	}
	GstBuffer *buffer = gst_sample_get_buffer(sample);
	GstVideoInfo info;
	GstVideoFrame frame;
	if (gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
		const QImage img(reinterpret_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
			info.width, info.height,
			GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888);
		m_view->setFrame(img.copy());
		gst_video_frame_unmap(&frame);
	}
	gst_sample_unref(sample);
}

void PhotoBoothWindow::takePhoto() {
	if (!m_view->hasFrame()) {
		return;
	}
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	QDir().mkpath(dir);
	for (int n = 1;; n++) {
		const QString name = QStringLiteral("Photo %1.png").arg(n);
		const QString path = dir + QLatin1Char('/') + name;
		if (!QFileInfo::exists(path)) {
			if (m_view->frame().save(path, "PNG")) {
				showMessage(QStringLiteral("Saved %1").arg(name));
			} else {
				showMessage(QStringLiteral("The photo couldn't be saved."));
			}
			return;
		}
	}
}

void PhotoBoothWindow::showMessage(const QString &text) {
	m_status->setText(text);
	QTimer::singleShot(4000, this, [this] { m_status->clear(); });
}
