/*
 * zacos9-login: the login window greetd shows (session/zacos9-greeter
 * runs it in a zacos9-wm of its own). Like Mac OS 9's Multiple Users
 * login: pick your name, type your password, Log In; or Restart and
 * Shut Down. greetd checks the password (PAM) and, once we exit, starts
 * zacos9-session as that user — see greetd.h.
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <pwd.h>

#include "greetd.h"
#include "panelkit.h"
#include "pixels.h"
#include "platinumshell.h"

static constexpr int W = 320;
static constexpr int MARGIN = 16;
static constexpr int FIELD_X = MARGIN + 72;
static constexpr int PICTURE = 48, PICTURE_Y = 12; /* at the top right */

struct Account {
	QString user, name;
};

/* /etc/login.defs's UID_MIN and UID_MAX: people, not system accounts. */
static std::pair<uid_t, uid_t> personUids() {
	uid_t lo = 1000, hi = 60000;
	QFile defs("/etc/login.defs");
	if (defs.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : defs.readAll().split('\n')) {
			const QList<QByteArray> f = line.simplified().split(' ');
			if (f.size() == 2 && f[0] == "UID_MIN") {
				lo = f[1].toUInt();
			} else if (f.size() == 2 && f[0] == "UID_MAX") {
				hi = f[1].toUInt();
			}
		}
	}
	return { lo, hi };
}

static std::vector<Account> accounts() {
	const auto [lo, hi] = personUids();
	std::vector<Account> list;
	setpwent();
	while (struct passwd *pw = getpwent()) {
		const QString shell = QString::fromLocal8Bit(pw->pw_shell);
		if (pw->pw_uid < lo || pw->pw_uid > hi || shell.endsWith("/nologin") ||
				shell.endsWith("/false")) {
			continue;
		}
		const QString user = QString::fromLocal8Bit(pw->pw_name);
		const QString name = QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0).trimmed();
		list.push_back({ user, name.isEmpty() ? user : name });
	}
	endpwent();
	std::sort(list.begin(), list.end(), [](const Account &a, const Account &b) {
		return a.name.localeAwareCompare(b.name) < 0;
	});
	return list;
}

/* Who logged in last, so the window comes up with them chosen. */
static QString lastUserFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
		"/zacos9/last-user";
}

class LoginWindow : public QWidget {
public:
	LoginWindow() : m_accounts(accounts()) {
		setWindowTitle("Log In");
		const QByteArray conf = qgetenv("ZACOS9_ACCOUNTS_CONF");
		QFile welcome((conf.isEmpty() ? QString("/etc/zacos9") : QString::fromUtf8(conf)) + "/welcome");
		if (welcome.open(QIODevice::ReadOnly)) {
			m_welcome = QString::fromUtf8(welcome.read(400)).simplified();
		}
		/* The Guest account has no password (zacos9-accounts-helper), and
		 * we can't read /etc/shadow to see who else hasn't. */
		QFile guest((conf.isEmpty() ? QString("/etc/zacos9") : QString::fromUtf8(conf)) +
			"/guest-account");
		if (guest.open(QIODevice::ReadOnly)) {
			m_guest = QString::fromUtf8(guest.read(64)).trimmed();
		}

		/* Under the welcome and the chosen person's picture. */
		int y = PICTURE_Y + PICTURE + 8;
		const int rows = std::clamp(static_cast<int>(m_accounts.size()), 3, 6);
		m_list.frame = QRect(MARGIN, y, W - 2 * MARGIN, rows * PL_LIST_ROW_H + 2);
		QStringList names;
		for (const Account &a : m_accounts) {
			names << a.name;
		}
		m_list.setItems(names);
		y = m_list.frame.bottom() + 12;
		m_password.rect = QRect(FIELD_X, y, W - MARGIN - FIELD_X, PL_EDIT_H);
		m_password.password = true;
		y += PL_EDIT_H + 6 + 16; /* and a line for what went wrong */
		const int H = y + 12 + PL_BUTTON_H + MARGIN;
		setFixedSize(W, H);

		const int by = H - MARGIN - PL_BUTTON_H;
		m_shutDown = PanelButton("Shut Down", QRect(MARGIN, by, 84, PL_BUTTON_H));
		m_restart = PanelButton("Restart", QRect(MARGIN + 92, by, 72, PL_BUTTON_H));
		m_logIn = PanelButton("Log In", QRect(W - MARGIN - 76, by, 76, PL_BUTTON_H), true);
		m_shutDown.clicked = [] { QProcess::startDetached("systemctl", { "poweroff" }); };
		m_restart.clicked = [] { QProcess::startDetached("systemctl", { "reboot" }); };
		m_logIn.clicked = [this] { logIn(); };
		m_list.picked = [this](int) {
			m_error.clear();
			updateEnabled();
		};

		m_host.edits = { &m_password };
		m_host.buttons = { &m_shutDown, &m_restart, &m_logIn };
		m_host.defaultButton = &m_logIn;
		m_host.setFocus(&m_password);

		QFile last(lastUserFile());
		const QString lastUser = last.open(QIODevice::ReadOnly)
			? QString::fromUtf8(last.readAll()).trimmed() : QString();
		int row = m_accounts.empty() ? -1 : 0;
		for (size_t i = 0; i < m_accounts.size(); i++) {
			if (m_accounts[i].user == lastUser) {
				row = static_cast<int>(i);
			}
		}
		if (row >= 0) {
			m_list.select(row, false);
			m_list.ensureVisible(row);
		}
		if (m_accounts.empty()) {
			m_error = "There are no accounts to log in to.";
		}
		updateEnabled();

		m_greetd.started = [] { qApp->quit(); };
		m_greetd.failed = [this](const QString &why) {
			m_error = why;
			m_password.setText(QString());
			updateEnabled();
			update();
		};
	}

protected:
	void showEvent(QShowEvent *e) override {
		QWidget::showEvent(e);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}

	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, width() - 1, height() - 1, GRAY(0xD));
		/* The Users panel's welcome message, if an administrator set one. */
		const QStringList welcome = panelWrap(m_welcome.isEmpty() ? QString("Welcome to ZacOS 9.")
			: m_welcome, W - 2 * MARGIN - PICTURE - 8, PL_FONT_SYSTEM).mid(0, 3);
		for (int i = 0; i < welcome.size(); i++) {
			panelText(c, welcome[i], MARGIN,
				PICTURE_Y + PICTURE / 2 + 5 + (i - (int(welcome.size()) - 1) / 2) * 16);
		}
		/* The chosen person's picture (Users control panel), as Mac OS 9's
		 * Multiple Users showed it. */
		if (const int row = m_list.state.selected; row >= 0) {
			const Account &a = m_accounts[static_cast<size_t>(row)];
			panelIcon(c, accountPicture(accountPictureFile(a.user), a.name, PICTURE),
				W - MARGIN - PICTURE, PICTURE_Y);
		}
		m_list.paint(c, false);
		panelLabel(c, "Password:", FIELD_X - 8, m_password.rect.top() + 15);
		const QString status = m_greetd.busy() ? QString("Logging in…") : !m_error.isEmpty()
			? m_error : m_safeMode ? QString("Safe mode: Startup Items will not run.") : QString();
		if (!status.isEmpty()) {
			panelText(c, status, MARGIN, m_password.rect.bottom() + 20, PL_FONT_VIEWS,
				m_greetd.busy() ? C_BLACK : RGB(0x99, 0x00, 0x00), W - 2 * MARGIN);
		}
		m_host.paintControls(c, GRAY(0xD));
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (!m_greetd.busy() && m_list.press(e->position().toPoint())) {
			update();
			return;
		}
		if (m_host.hostPress(e)) {
			update();
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (m_list.move(e->position().toPoint()) | m_host.hostMove(e)) {
			update();
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (m_list.release() | m_host.hostRelease(e)) {
			update();
		}
	}
	void wheelEvent(QWheelEvent *e) override {
		if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (e->key() == Qt::Key_Shift) {
			m_safeMode = true;
			update();
			return;
		}
		/* Up and Down choose the person; typing goes to the password. */
		if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && !m_greetd.busy() &&
				m_list.key(e->key(), QString())) {
			update();
			return;
		}
		if (m_host.hostKey(e)) {
			if (!m_error.isEmpty() && !m_password.text.isEmpty()) {
				m_error.clear();
			}
			update();
			return;
		}
		QWidget::keyPressEvent(e);
	}
	void keyReleaseEvent(QKeyEvent *e) override {
		if (e->key() == Qt::Key_Shift) {
			m_safeMode = (e->modifiers() & Qt::ShiftModifier) != 0;
			update();
			return;
		}
		QWidget::keyReleaseEvent(e);
	}

private:
	/* The chosen person needs no password: the field is dimmed and empty. */
	bool noPassword() const {
		const int row = m_list.state.selected;
		return row >= 0 && !m_guest.isEmpty() && m_accounts[static_cast<size_t>(row)].user == m_guest;
	}

	void updateEnabled() {
		const bool idle = !m_greetd.busy();
		m_logIn.enabled = idle && m_list.state.selected >= 0;
		m_password.enabled = idle && !noPassword();
		if (noPassword()) {
			m_password.setText(QString());
		}
		m_shutDown.enabled = m_restart.enabled = idle;
	}

	void logIn() {
		const int row = m_list.state.selected;
		if (row < 0 || m_greetd.busy()) {
			return;
		}
		const QString user = m_accounts[static_cast<size_t>(row)].user;
		const QString file = lastUserFile();
		QDir().mkpath(QFileInfo(file).path());
		QFile last(file);
		if (last.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			last.write(user.toUtf8() + '\n');
		}
		m_error.clear();
		m_safeMode = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0 ||
			m_safeMode;
		m_greetd.login(user, m_password.text, { "zacos9-session" },
			m_safeMode ? QStringList{ "ZACOS9_SAFE_MODE=1" } : QStringList{});
		updateEnabled();
		update();
	}

	std::vector<Account> m_accounts;
	QString m_welcome;
	QString m_guest; /* the Guest account's user name, if it's on */
	Greetd m_greetd;
	QString m_error;
	bool m_safeMode = false;
	PanelList m_list;
	PanelEdit m_password;
	PanelButton m_shutDown, m_restart, m_logIn;
	PanelHost m_host{ this };
};

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Log In");
	QGuiApplication::setDesktopFileName("zacos9-login"); /* Wayland app_id */
	platinumShellInit();

	LoginWindow window;
	window.show();
	return app.exec();
}
