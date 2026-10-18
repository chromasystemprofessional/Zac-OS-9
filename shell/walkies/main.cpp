/*
 * zacos9-walkies: Walkies (Mac OS 9's Location Manager, gap plan 8.5).
 * Places you take the computer, each remembering the network, sound,
 * time zone, default printer and startup set (location.h); Switch To
 * brings them all back at once. Commands run one at a time, never
 * waited on (CLAUDE.md).
 */

#include <QApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTime>
#include <QWheelEvent>
#include <memory>

#include "alert.h"
#include "autostart.h"
#include "location.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"
#include "tricksets.h"

namespace {

constexpr int W = 440, H = 330, M = 12;
constexpr uint32_t FACE = GRAY(0xD);

class Walkies : public QWidget {
public:
	Walkies() : m_host(this) {
		setWindowTitle("Walkies");
		setFixedSize(W, H);
		m_list.frame = QRect(M, 30, W - 2 * M, 8 * PL_LIST_ROW_H + 2);
		m_list.picked = [this](int) { updateButtons(); };
		m_host.hintAreas.push_back({ &m_list.frame, "Places you take this computer. • marks the one it's set up for." });
		const int y = m_list.frame.bottom() + 10;
		m_save = PanelButton("Save Current…", QRect(M, y, 120, PL_BUTTON_H));
		m_save.hint = "Remembers this computer's network, sound, time zone, printer and startup set as a place.";
		m_save.clicked = [this] { saveCurrent(); };
		m_delete = PanelButton("Delete", QRect(M + 128, y, 80, PL_BUTTON_H));
		m_delete.hint = "Forgets the chosen place. Nothing on the computer changes.";
		m_delete.clicked = [this] { deleteChosen(); };
		m_switch = PanelButton("Switch To", QRect(W - M - 4 - 100, y, 100, PL_BUTTON_H), true);
		m_switch.hint = "Sets the computer up for the chosen place.";
		m_switch.clicked = [this] { switchTo(); };
		m_host.buttons = { &m_save, &m_delete, &m_switch };
		m_host.defaultButton = &m_switch;
		fill();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		panelText(c, "Places:", M, 22);
		m_list.paint(c, true);
		int y = m_save.rect.bottom() + 22;
		Location l;
		if (const QString name = chosen(); !name.isEmpty() && readLocation(name, &l)) {
			for (const QString &line : panelWrap(describe(l), W - 2 * M, PL_FONT_VIEWS)) {
				panelText(c, line, M, y, PL_FONT_VIEWS);
				y += 12;
			}
		}
		y += 8;
		for (const QString &line : m_log.mid(std::max(0, int(m_log.size()) - 5))) {
			panelText(c, line, M, y, PL_FONT_VIEWS, GRAY(0x5), W - 2 * M);
			y += 12;
		}
		m_host.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (!m_list.press(e->position().toPoint())) {
			m_host.hostPress(e);
		}
		updateButtons();
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		m_list.move(e->position().toPoint());
		m_host.hostMove(e);
		update();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		m_list.release();
		m_host.hostRelease(e);
		update();
	}
	void wheelEvent(QWheelEvent *e) override {
		if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}
	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if (!m_list.key(e->key(), e->text())) {
			m_host.hostKey(e);
		}
		updateButtons();
		update();
	}

private:
	PanelHost m_host;
	PanelList m_list;
	PanelButton m_save, m_delete, m_switch;
	QStringList m_names, m_log;
	bool m_busy = false;

	QString chosen() const {
		const int row = m_list.state.selected;
		return row >= 0 && row < m_names.size() ? m_names[row] : QString();
	}

	void log(const QString &text) {
		m_log << QTime::currentTime().toString("h:mm AP") + "  " + text;
		update();
	}

	void fill() {
		const QString was = chosen().isEmpty() ? currentLocation() : chosen();
		m_names = locationNames();
		QStringList rows;
		int select = m_names.isEmpty() ? -1 : 0;
		for (int i = 0; i < m_names.size(); i++) {
			rows << (m_names[i] == currentLocation() ? "•  " : "    ") + m_names[i];
			if (m_names[i] == was) {
				select = i;
			}
		}
		m_list.setItems(rows);
		m_list.select(select, false);
		updateButtons();
		update();
	}

	void updateButtons() {
		m_save.enabled = !m_busy;
		m_delete.enabled = m_switch.enabled = !m_busy && !chosen().isEmpty();
	}

	/* Steps one at a time; each one's output to `outputs`. */
	void run(QList<WalkiesStep> steps, std::shared_ptr<QStringList> outputs, bool quietFailures,
			std::function<void()> done) {
		if (steps.isEmpty()) {
			done();
			return;
		}
		const WalkiesStep step = steps.takeFirst();
		auto *p = new QProcess(this);
		auto finish = [this, p, step, steps, outputs, quietFailures, done](bool ok) {
			*outputs << (ok ? QString::fromUtf8(p->readAllStandardOutput()) : QString());
			if (!ok && !quietFailures) {
				const QString why = QString::fromUtf8(p->readAllStandardError()).trimmed().section('\n', -1);
				log("Couldn't set " + step.what + (why.isEmpty() ? QString(".") : ": " + why));
			}
			p->deleteLater();
			run(steps, outputs, quietFailures, done);
		};
		connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
			finish(status == QProcess::NormalExit && code == 0);
		});
		connect(p, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				finish(false);
			}
		});
		p->start(step.program, step.args);
	}

	void saveCurrent() {
		bool ok = false;
		const QString name = QInputDialog::getText(this, "Save Current Place", "Name of this place:",
			QLineEdit::Normal, chosen().isEmpty() ? QString("Home") : chosen(), &ok).trimmed();
		if (!ok || name.isEmpty()) {
			return;
		}
		m_busy = true;
		updateButtons();
		auto outputs = std::make_shared<QStringList>();
		run(captureSteps(), outputs, true, [this, outputs, name] {
			Location l;
			l.name = name;
			parseCapture(*outputs, &l);
			l.trickSet = currentTrickSet();
			QString error;
			if (saveLocation(l, &error)) {
				setCurrentLocation(name);
				log("Saved “" + name + "”.");
			} else {
				log(error);
			}
			m_busy = false;
			fill();
		});
	}

	void deleteChosen() {
		const QString name = chosen();
		if (!name.isEmpty() && Alert::ask("Forget the place “" + name + "”?", "Forget", "Cancel")) {
			deleteLocation(name);
			if (currentLocation() == name) {
				setCurrentLocation(QString());
			}
			fill();
		}
	}

	void switchTo() {
		Location l;
		if (!readLocation(chosen(), &l)) {
			return;
		}
		/* The startup set's login items here; its services with the rest. */
		TrickSet set;
		if (!l.trickSet.isEmpty() && readTrickSet(l.trickSet, &set)) {
			const AutostartContext context = autostartDefaultContext();
			for (const AutostartEntry &e : autostartEntries()) {
				const auto want = set.loginItems.constFind(e.id);
				if (want != set.loginItems.cend() && *want != e.enabled && (e.enabled || e.reason == "Disabled")) {
					QString error;
					if (!autostartSetEnabled(context, e.id, *want, &error)) {
						log(error);
					}
				}
			}
			setCurrentTrickSet(l.trickSet);
		}
		m_busy = true;
		updateButtons();
		log("Switching to “" + l.name + "”…");
		run(applySteps(l, set.services), std::make_shared<QStringList>(), false, [this, l] {
			setCurrentLocation(l.name);
			log("Set up for “" + l.name + "”.");
			m_busy = false;
			fill();
		});
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Walkies");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-walkies"); /* Wayland app_id */
	platinumShellInit();
	Walkies w;
	w.show();
	return app.exec();
}
