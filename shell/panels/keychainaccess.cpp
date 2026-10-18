/*
 * zacos9-keychain: Keychain Access (Mac OS 9's control panel, gap plan
 * 5.6). The person's keychains (the Secret Service's collections, as The
 * Collar's Keychain module shows them), locked or not, which is the
 * default, and the items in the chosen one, by name: Unlock or Lock, Make
 * Default, Remove. Passwords themselves are never shown here.
 */

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "alert.h"
#include "keychain.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

namespace {

constexpr int W = 420, H = 350, M = 12;
constexpr uint32_t FACE = GRAY(0xD);

class KeychainAccess : public QWidget {
public:
	KeychainAccess() : m_host(this) {
		setWindowTitle("Keychain Access");
		setFixedSize(W, H);
		m_keychains.frame = QRect(M, 30, W - 2 * M, 5 * PL_LIST_ROW_H + 2);
		m_keychains.picked = [this](int) { showItems(); };
		m_host.hintAreas.push_back({ &m_keychains.frame,
			"Your keychains. A locked keychain's passwords can't be used until you unlock it." });
		int y = m_keychains.frame.bottom() + 10;
		m_lock = PanelButton("Unlock", QRect(M, y, 80, PL_BUTTON_H));
		m_lock.hint = "Unlocks the keychain (it asks for its password) or locks it.";
		m_lock.clicked = [this] {
			if (const KeychainInfo *k = chosen()) {
				k->locked ? m_keys.unlock(k->path) : m_keys.lock(k->path);
			}
		};
		m_default = PanelButton("Make Default", QRect(M + 88, y, 110, PL_BUTTON_H));
		m_default.hint = "Makes this keychain the one new passwords are kept in.";
		m_default.clicked = [this] {
			if (const KeychainInfo *k = chosen()) {
				m_keys.setDefault(k->path);
			}
		};
		y += PL_BUTTON_H + 30;
		m_items.frame = QRect(M, y, W - 2 * M, 7 * PL_LIST_ROW_H + 2);
		m_host.hintAreas.push_back({ &m_items.frame, "What this keychain keeps, by name." });
		m_remove = PanelButton("Remove…", QRect(W - M - 4 - 90, H - M - PL_BUTTON_H, 90, PL_BUTTON_H));
		m_remove.hint = "Removes the selected item from the keychain, for good.";
		m_remove.clicked = [this] { removeChosen(); };
		m_host.buttons = { &m_lock, &m_default, &m_remove };

		m_keys.changed = [this] { fill(); };
		m_keys.failed = [](const QString &why) { Alert::ask(why, "OK", QString()); };
		m_keys.refresh();
		updateButtons();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		panelText(c, "Keychains:", M, 22);
		m_keychains.paint(c, m_focusItems ? false : true);
		const KeychainInfo *k = chosen();
		panelText(c, k ? QString("Items in “%1”:").arg(k->name) : QString("Items:"), M, m_items.frame.top() - 8);
		m_items.paint(c, m_focusItems);
		QString note;
		if (!m_keys.available) {
			note = "There is no keyring running, so there are no keychains.";
		} else if (k && k->locked) {
			note = "Locked: unlock it to use or change its items.";
		}
		if (!note.isEmpty()) {
			panelText(c, note, M, H - M - 6, PL_FONT_VIEWS, GRAY(0x5), W - 2 * M - 100);
		}
		m_host.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		if (m_keychains.press(pos)) {
			m_focusItems = false;
		} else if (m_items.press(pos)) {
			m_focusItems = true;
		} else {
			m_host.hostPress(e);
		}
		updateButtons();
		update();
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		m_keychains.move(e->position().toPoint());
		m_items.move(e->position().toPoint());
		m_host.hostMove(e);
		update();
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		m_keychains.release();
		m_items.release();
		m_host.hostRelease(e);
		updateButtons();
		update();
	}

	void wheelEvent(QWheelEvent *e) override {
		const QPoint pos = e->position().toPoint();
		if (m_keychains.wheel(pos, e->angleDelta().y()) || m_items.wheel(pos, e->angleDelta().y())) {
			update();
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if (e->key() == Qt::Key_Tab) {
			m_focusItems = !m_focusItems;
		} else if ((m_focusItems ? m_items : m_keychains).key(e->key(), e->text())) {
			if (!m_focusItems) {
				showItems();
			}
		} else {
			m_host.hostKey(e);
		}
		updateButtons();
		update();
	}

private:
	Keychains m_keys;
	PanelHost m_host;
	PanelList m_keychains, m_items;
	PanelButton m_lock, m_default, m_remove;
	QList<KeychainItem> m_itemList;
	bool m_focusItems = false;

	const KeychainInfo *chosen() const {
		const int row = m_keychains.state.selected;
		return row >= 0 && row < m_keys.keychains.size() ? &m_keys.keychains[row] : nullptr;
	}

	void fill() {
		const QString was = chosen() ? chosen()->path : m_keys.defaultPath;
		QStringList rows;
		int select = m_keys.keychains.isEmpty() ? -1 : 0;
		for (int i = 0; i < m_keys.keychains.size(); i++) {
			const KeychainInfo &k = m_keys.keychains[i];
			rows << k.name + (k.locked ? "  (locked)" : "") + (k.path == m_keys.defaultPath ? "  – default" : "");
			if (k.path == was) {
				select = i;
			}
		}
		m_keychains.setItems(rows);
		m_keychains.select(select, false);
		showItems();
	}

	void showItems() {
		const KeychainInfo *k = chosen();
		if (!k) {
			m_itemList.clear();
			m_items.setItems({});
			updateButtons();
			update();
			return;
		}
		const QString path = k->path;
		m_keys.items(path, [this, path](const QList<KeychainItem> &items) {
			if (!chosen() || chosen()->path != path) {
				return; /* another keychain was chosen meanwhile */
			}
			m_itemList = items;
			QStringList rows;
			for (const KeychainItem &i : items) {
				rows << (i.label.isEmpty() ? QString("(no name)") : i.label);
			}
			m_items.setItems(rows);
			m_items.state.selected = -1;
			updateButtons();
			update();
		});
	}

	void removeChosen() {
		const int row = m_items.state.selected;
		if (row < 0 || row >= m_itemList.size()) {
			return;
		}
		const KeychainItem item = m_itemList[row];
		if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen" ||
				Alert::ask(QString("Remove “%1” from the keychain? Its password can't be brought back.")
					.arg(item.label), "Remove", "Cancel")) {
			m_keys.removeItem(item);
		}
	}

	void updateButtons() {
		const KeychainInfo *k = chosen();
		m_lock.enabled = k != nullptr;
		m_lock.label = std::make_unique<Text>(k && !k->locked ? "Lock" : "Unlock", 200, PL_FONT_SYSTEM);
		m_default.enabled = k && k->path != m_keys.defaultPath;
		m_remove.enabled = k && !k->locked && m_items.state.selected >= 0;
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Keychain Access");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-keychain"); /* Wayland app_id */
	platinumShellInit();

	KeychainAccess panel;
	panel.show();
	return app.exec();
}
