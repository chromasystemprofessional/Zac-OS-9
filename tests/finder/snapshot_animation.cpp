#include <QApplication>
#include <QPainter>
#include <QTimer>
#include <QWidget>

class Animation : public QWidget {
public:
	Animation() {
		setWindowTitle("Snapshot regression animation");
		m_timer.setInterval(120);
		connect(&m_timer, &QTimer::timeout, this, [this] {
			m_red = !m_red;
			update();
		});
		m_timer.start();
	}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this);
		painter.fillRect(rect(), m_red ? Qt::red : Qt::green);
	}
private:
	QTimer m_timer;
	bool m_red = false;
};

int main(int argc, char **argv) {
	QApplication application(argc, argv);
	Animation animation;
	animation.showFullScreen();
	return application.exec();
}
