#include "functionkeys.h"

#include <QDialog>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>
#include <array>

#include "defaultapps.h"
#include "panelkit.h"
#include "settings.h"

namespace {

constexpr uint32_t BG = GRAY(0xD);
/* What the Sniffer puts on a drag of the Macintosh view's applications
 * and folders: their names and what they stand for (fileops.h). */
constexpr char ALIAS_ITEMS_MIME[] = "application/x-zacos9-alias-items";

/* Choosing an application from a list, or a document or folder. */
class ChooseItemDialog : public QDialog {
public:
	explicit ChooseItemDialog(QWidget *parent) : QDialog(parent) {
		setWindowTitle("Choose an Item");
		setFixedSize(W, H);
		m_apps = installedApps();
		QStringList names;
		for (const InstalledApp &a : m_apps) {
			names << a.name;
		}
		m_list.frame = QRect(16, 36, W - 32, 12 * PL_LIST_ROW_H + 2);
		m_host.hintAreas.push_back({ &m_list.frame, "Your programs. Choose the one the F-key opens." });
		m_list.setItems(names);
		m_list.state.selected = m_apps.isEmpty() ? -1 : 0;
		const int by = H - 14 - PL_BUTTON_H;
		m_document = PanelButton("Document…", QRect(16, by, 96, PL_BUTTON_H));
		m_document.hint = "Chooses a document for the F-key to open instead.";
		m_folder = PanelButton("Folder…", QRect(120, by, 80, PL_BUTTON_H));
		m_folder.hint = "Chooses a folder for the F-key to open instead.";
		m_cancel = PanelButton("Cancel", QRect(W - 16 - 2 * 76 - 10, by, 76, PL_BUTTON_H));
		m_choose = PanelButton("Choose", QRect(W - 16 - 76, by, 76, PL_BUTTON_H), true);
		m_document.clicked = [this] { pickFile(false); };
		m_folder.clicked = [this] { pickFile(true); };
		m_cancel.clicked = [this] { reject(); };
		m_choose.clicked = [this] {
			if (m_list.state.selected >= 0) {
				m_path = m_apps[m_list.state.selected].path;
				accept();
			}
		};
		m_host.buttons = { &m_document, &m_folder, &m_cancel, &m_choose };
		m_host.defaultButton = &m_choose;
		m_host.cancelButton = &m_cancel;
	}
	QString path() const { return m_path; }

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_fill(&px.c, 0, 0, W - 1, H - 1, BG);
		panelText(&px.c, "Choose an application:", 16, 24);
		m_list.paint(&px.c, true);
		m_host.paintControls(&px.c, BG);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (!m_list.press(e->position().toPoint())) {
			m_host.hostPress(e);
		}
		update();
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override {
		if (m_list.frame.contains(e->position().toPoint()) && m_list.state.selected >= 0) {
			m_choose.clicked();
		}
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
		m_list.wheel(e->position().toPoint(), e->angleDelta().y());
		update();
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_list.key(e->key(), e->text()) && !m_host.hostKey(e)) {
			QDialog::keyPressEvent(e);
		}
		update();
	}

private:
	void pickFile(bool folder) {
		const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
		const QString path = folder ? QFileDialog::getExistingDirectory(this, "Choose a Folder", home) :
			QFileDialog::getOpenFileName(this, "Choose a Document", home);
		if (!path.isEmpty()) {
			m_path = path;
			accept();
		}
	}

	static constexpr int W = 380, H = 300;
	QList<InstalledApp> m_apps;
	PanelList m_list;
	PanelButton m_document, m_folder, m_cancel, m_choose;
	QString m_path;
	PanelHost m_host{ this };
};

class FunctionKeysDialog : public QDialog {
public:
	explicit FunctionKeysDialog(QWidget *parent) : QDialog(parent) {
		setWindowTitle("Function Keys");
		setFixedSize(W, H);
		setAcceptDrops(true);
		char v[1024];
		for (int i = 0; i < 12; i++) {
			const QByteArray key = QByteArray("fkey-") + QByteArray::number(i + 1);
			m_items[i] = pl_setting(key.constData(), v, sizeof(v)) ? QString::fromUtf8(v) : QString();
			const int y = rowY(i);
			m_choose[i] = PanelButton("Choose…", QRect(W - 16 - 64 - 8 - 84, y, 84, PL_BUTTON_H));
			m_choose[i].hint = "Chooses the program, document or folder this F-key opens.";
			m_clear[i] = PanelButton("Clear", QRect(W - 16 - 64, y, 64, PL_BUTTON_H));
			m_clear[i].hint = "Makes this F-key an ordinary key again.";
			m_choose[i].clicked = [this, i] {
				ChooseItemDialog choose(this);
				if (choose.exec() == QDialog::Accepted && !choose.path().isEmpty()) {
					m_items[i] = choose.path();
					update();
				}
			};
			m_clear[i].clicked = [this, i] {
				m_items[i].clear();
				update();
			};
			m_host.buttons.push_back(&m_choose[i]);
			m_host.buttons.push_back(&m_clear[i]);
		}
		m_command = PanelCheckbox("Use F1 through F12 as function keys (hold ⌘ for these)",
			QPoint(16, rowY(12) + 6));
		m_command.hint = "Leaves F1 to F12 to programs; hold ⌘ with an F-key to open what it is set to.";
		m_command.on = pl_setting("fkeys-command", v, sizeof(v)) && strcmp(v, "true") == 0;
		m_host.checks = { &m_command };
		const int by = H - 14 - PL_BUTTON_H;
		m_cancel = PanelButton("Cancel", QRect(W - 16 - 2 * 76 - 10, by, 76, PL_BUTTON_H));
		m_ok = PanelButton("OK", QRect(W - 16 - 76, by, 76, PL_BUTTON_H), true);
		m_cancel.clicked = [this] { reject(); };
		m_ok.clicked = [this] {
			for (int i = 0; i < 12; i++) {
				const QByteArray key = QByteArray("fkey-") + QByteArray::number(i + 1);
				pl_setting_set(key.constData(), m_items[i].isEmpty() ? nullptr : m_items[i].toUtf8().constData());
			}
			pl_setting_set("fkeys-command", m_command.on ? "true" : nullptr);
			accept();
		};
		m_host.buttons.push_back(&m_cancel);
		m_host.buttons.push_back(&m_ok);
		m_host.defaultButton = &m_ok;
		m_host.cancelButton = &m_cancel;
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, BG);
		panelText(c, "Choose an item for each key to open, or drop one onto its row.", 16, 24);
		for (int i = 0; i < 12; i++) {
			const int y = rowY(i);
			panelText(c, QString("F%1").arg(i + 1), 16, y + 14);
			const QRect box = itemBox(i);
			pl_fill(c, box.left(), box.top(), box.right(), box.bottom(), i == m_dropRow ? GRAY(0xB) : C_WHITE);
			pl_outline(c, box.left(), box.top(), box.right(), box.bottom(), GRAY(0x8));
			if (m_items[i].isEmpty()) {
				panelText(c, "Nothing assigned", box.left() + 6, y + 14, PL_FONT_VIEWS, GRAY(0x8));
			} else {
				const bool app = m_items[i].endsWith(".desktop");
				pl_icon_paint(c, box.left() + 3, box.top() + 1,
					app ? PL_ICON_APPLICATION : QFileInfo(m_items[i]).isDir() ? PL_ICON_FOLDER : PL_ICON_DOCUMENT,
					PL_ICON_SMALL, false);
				panelText(c, itemDisplayName(m_items[i]), box.left() + 24, y + 14, PL_FONT_VIEWS, C_BLACK,
					box.width() - 30);
			}
		}
		m_host.paintControls(c, BG);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override { m_host.hostPress(e); update(); }
	void mouseMoveEvent(QMouseEvent *e) override { m_host.hostMove(e); update(); }
	void mouseReleaseEvent(QMouseEvent *e) override { m_host.hostRelease(e); update(); }
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_host.hostKey(e)) {
			QDialog::keyPressEvent(e);
		}
		update();
	}

	/* An icon dropped on a row: that key opens it. */
	void dragEnterEvent(QDragEnterEvent *e) override {
		if (!droppedPath(e->mimeData()).isEmpty()) {
			e->acceptProposedAction();
		}
	}
	void dragMoveEvent(QDragMoveEvent *e) override {
		m_dropRow = rowAt(e->position().toPoint());
		update();
		if (m_dropRow >= 0) {
			e->acceptProposedAction();
		} else {
			e->ignore();
		}
	}
	void dragLeaveEvent(QDragLeaveEvent *) override {
		m_dropRow = -1;
		update();
	}
	void dropEvent(QDropEvent *e) override {
		const int row = rowAt(e->position().toPoint());
		m_dropRow = -1;
		const QString path = droppedPath(e->mimeData());
		if (row >= 0 && !path.isEmpty()) {
			m_items[row] = path;
			e->setDropAction(Qt::LinkAction);
			e->accept();
		}
		update();
	}

private:
	static int rowY(int i) { return 40 + i * 26; }
	QRect itemBox(int i) const { return QRect(52, rowY(i), m_choose[i].rect.left() - 52 - 10, 20); }
	int rowAt(QPoint p) const {
		for (int i = 0; i < 12; i++) {
			if (p.y() >= rowY(i) - 3 && p.y() < rowY(i) + 23) {
				return i;
			}
		}
		return -1;
	}
	/* The first item dropped: an alias's original, an application's
	 * desktop file. */
	static QString droppedPath(const QMimeData *mime) {
		if (mime->hasFormat(ALIAS_ITEMS_MIME)) {
			const QJsonArray items = QJsonDocument::fromJson(mime->data(ALIAS_ITEMS_MIME)).array();
			if (!items.isEmpty()) {
				return items.first().toObject().value("target").toString();
			}
		}
		for (const QUrl &url : mime->urls()) {
			if (url.isLocalFile()) {
				const QFileInfo info(url.toLocalFile());
				return info.isSymLink() ? info.canonicalFilePath() : info.absoluteFilePath();
			}
		}
		return {};
	}

	static constexpr int W = 480, H = 40 + 12 * 26 + 6 + 20 + 18 + PL_BUTTON_H + 14;
	std::array<QString, 12> m_items;
	std::array<PanelButton, 12> m_choose, m_clear;
	PanelCheckbox m_command;
	PanelButton m_cancel, m_ok;
	int m_dropRow = -1;
	PanelHost m_host{ this };
};

} // namespace

void showFunctionKeys(QWidget *parent) {
	FunctionKeysDialog dialog(parent);
	dialog.exec();
}
