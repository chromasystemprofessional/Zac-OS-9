/*
 * zacos9-retriever: Retriever (Mac OS 9's File Synchronization control
 * panel, gap plan 5.8). Folder pairs, each kept alike by Synchronize Now:
 * the newest copy of each file wins, both ways (sync.cpp).
 */

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTime>
#include <QWheelEvent>

#include "alert.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"
#include "sync.h"

namespace {

constexpr int W = 440, H = 330, M = 12;
constexpr uint32_t FACE = GRAY(0xD);

class Retriever : public QWidget {
public:
	Retriever() : m_host(this), m_pairs(loadPairs()) {
		setWindowTitle("Retriever");
		setFixedSize(W, H);
		m_list.frame = QRect(M, 30, W - 2 * M, 9 * PL_LIST_ROW_H + 2);
		m_list.picked = [this](int) { updateButtons(); };
		m_host.hintAreas.push_back({ &m_list.frame, "Folder pairs Retriever keeps alike." });
		const int y = m_list.frame.bottom() + 10;
		m_add = PanelButton("Add Pair…", QRect(M, y, 96, PL_BUTTON_H));
		m_add.hint = "Chooses two folders to keep alike.";
		m_add.clicked = [this] { addPair(); };
		m_remove = PanelButton("Remove", QRect(M + 104, y, 80, PL_BUTTON_H));
		m_remove.hint = "Stops keeping the selected pair alike. Nothing in the folders changes.";
		m_remove.clicked = [this] { removePair(); };
		m_sync = PanelButton("Synchronize Now", QRect(W - M - 4 - 140, H - M - PL_BUTTON_H, 140, PL_BUTTON_H), true);
		m_sync.hint = "Copies each folder's newer files to the other.";
		m_sync.clicked = [this] { syncAll(); };
		m_host.buttons = { &m_add, &m_remove, &m_sync };
		m_host.defaultButton = &m_sync;
		fill();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		panelText(c, "Keep these folders alike:", M, 22);
		m_list.paint(c, true);
		int y = m_list.frame.bottom() + PL_BUTTON_H + 28;
		for (const QString &line : panelWrap("The newer copy of a file replaces the older one in both folders. "
				"Deleting a file in one folder doesn't delete it in the other.", W - 2 * M, PL_FONT_VIEWS)) {
			panelText(c, line, M, y, PL_FONT_VIEWS, GRAY(0x5));
			y += 12;
		}
		if (!m_status.isEmpty()) {
			panelText(c, m_status, M, H - M - 6, PL_FONT_VIEWS, C_BLACK, W - 2 * M - 150);
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
	PanelButton m_add, m_remove, m_sync;
	QList<FolderPair> m_pairs;
	QString m_status;
	bool m_busy = false;

	void fill() {
		QStringList rows;
		for (const FolderPair &p : m_pairs) {
			rows << QFileInfo(p.left).fileName() + "  ↔  " + QFileInfo(p.right).fileName() +
				"   (" + p.left + ", " + p.right + ")";
		}
		m_list.setItems(rows);
		updateButtons();
		update();
	}

	void updateButtons() {
		m_add.enabled = !m_busy;
		m_remove.enabled = !m_busy && m_list.state.selected >= 0;
		m_sync.enabled = !m_busy && !m_pairs.isEmpty();
	}

	void addPair() {
		const QString left = QFileDialog::getExistingDirectory(this, "Choose the first folder");
		if (left.isEmpty()) {
			return;
		}
		const QString right = QFileDialog::getExistingDirectory(this, "Choose the folder to keep like it");
		if (right.isEmpty()) {
			return;
		}
		const QString l = QFileInfo(left).canonicalFilePath(), r = QFileInfo(right).canonicalFilePath();
		if (l == r || l.startsWith(r + '/') || r.startsWith(l + '/')) {
			Alert::ask("Choose two different folders, neither inside the other.", "OK", QString());
			return;
		}
		m_pairs.append({ l, r });
		savePairs(m_pairs);
		fill();
	}

	void removePair() {
		const int row = m_list.state.selected;
		if (row >= 0 && row < m_pairs.size()) {
			m_pairs.removeAt(row);
			savePairs(m_pairs);
			m_list.state.selected = -1;
			fill();
		}
	}

	void syncAll() {
		m_busy = true;
		updateButtons();
		syncFrom(0, 0);
	}

	void syncFrom(int index, int failures) {
		if (index >= m_pairs.size()) {
			m_busy = false;
			m_status = failures ? QString("%1 pair(s) couldn't be synchronized.").arg(failures)
				: QString("Synchronized at %1.").arg(QTime::currentTime().toString("h:mm AP"));
			updateButtons();
			update();
			return;
		}
		m_status = QString("Synchronizing “%1”…").arg(QFileInfo(m_pairs[index].left).fileName());
		update();
		syncPair(m_pairs[index], [this, index, failures](const QString &error) {
			if (!error.isEmpty()) {
				Alert::ask(QString("“%1” couldn't be synchronized. %2")
					.arg(QFileInfo(m_pairs[index].left).fileName(), error), "OK", QString());
			}
			syncFrom(index + 1, failures + (error.isEmpty() ? 0 : 1));
		});
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Retriever");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-retriever"); /* Wayland app_id */
	platinumShellInit();

	Retriever panel;
	panel.show();
	return app.exec();
}
