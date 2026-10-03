#include "setup.h"

#include <QCloseEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTimeZone>
#include <QWheelEvent>
#include <algorithm>
#include <memory>

#include "logo.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 520, H = 340;
static constexpr int MARGIN = 16;
static constexpr int X0 = 128;                 /* the page's left edge; the logo sits left of it */
static constexpr int PAGE_W = W - MARGIN - X0; /* the page's width */
static constexpr int RULE_Y = H - 46;          /* the line above the arrows */
static constexpr int BTN_Y = H - MARGIN - PL_BUTTON_H;
static constexpr int ARROW_W = 40;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr uint32_t RED = RGB(0x99, 0x00, 0x00);

static const char *const TITLES[] = {
	"Introduction", "Name", "Password", "Computer Name", "Time Zone", "Conclusion",
};

static const QRegularExpression SHORT_NAME(QStringLiteral("^[a-z][a-z0-9_-]{0,31}$"));

/* "Adam Lawson" -> "adam": the first word, in the letters a short name may use. */
static QString shortNameFor(const QString &fullName) {
	QString out;
	const QString first = fullName.trimmed().section(' ', 0, 0).toLower();
	for (QChar ch : first.normalized(QString::NormalizationForm_KD)) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out += ch;
		}
	}
	if (!out.isEmpty() && out[0].isDigit()) {
		out.prepend('u');
	}
	return out.left(32);
}

static bool accountExists(const QString &name) {
	QFile f(QStringLiteral("/etc/passwd"));
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray prefix = name.toUtf8() + ':';
	for (const QByteArray &line : f.readAll().split('\n')) {
		if (line.startsWith(prefix)) {
			return true;
		}
	}
	return false;
}

/* The regions people live in, without the aliases and Etc/ zones. */
static QStringList timeZones() {
	static const QStringList regions = {
		"Africa", "America", "Antarctica", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific",
	};
	QStringList ids;
	for (const QByteArray &raw : QTimeZone::availableTimeZoneIds()) {
		const QString id = QString::fromUtf8(raw);
		if (regions.contains(id.section('/', 0, 0)) && id.contains('/')) {
			ids << id;
		}
	}
	ids.removeDuplicates();
	ids.sort();
	ids.prepend(QStringLiteral("UTC"));
	return ids;
}

/* ---- construction ------------------------------------------------------------------ */

SetupWindow::SetupWindow() {
	setWindowTitle("ZacOS Setup Assistant");
	setFixedSize(W, H);

	m_fullName.rect = QRect(X0, 82, 300, PL_EDIT_H);
	m_fullName.accepts = [](QChar ch) { return ch != ':' && ch != ','; };
	m_fullName.edited = [this] {
		if (!m_shortNameTyped) {
			m_shortName.setText(shortNameFor(m_fullName.text));
		}
		updateButtons();
	};
	m_shortName.rect = QRect(X0, 140, 160, PL_EDIT_H);
	m_shortName.accepts = [](QChar ch) {
		return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
	};
	m_shortName.edited = [this] {
		m_shortNameTyped = !m_shortName.text.isEmpty();
		updateButtons();
	};

	m_password.rect = QRect(X0 + 92, 104, 200, PL_EDIT_H);
	m_password.password = true;
	m_password.edited = [this] { updateButtons(); };
	m_verify.rect = QRect(X0 + 92, 132, 200, PL_EDIT_H);
	m_verify.password = true;
	m_verify.edited = [this] { updateButtons(); };
	m_autoLogin = PanelCheckbox("Log in automatically when the computer starts up",
		QPoint(X0, 172));
	m_autoLogin.on = true;

	m_computerName.rect = QRect(X0, 82, 300, PL_EDIT_H);
	m_computerName.accepts = [](QChar ch) { return ch.isPrint(); };
	m_computerName.edited = [this] { updateButtons(); };

	m_zoneIds = timeZones();
	m_zones.frame = QRect(X0, 82, PAGE_W, 11 * PL_LIST_ROW_H + 2);
	QStringList names;
	for (const QString &z : m_zoneIds) {
		names << QString(z).replace('_', ' ');
	}
	m_zones.setItems(names);
	const int current = static_cast<int>(
		m_zoneIds.indexOf(QString::fromUtf8(QTimeZone::systemTimeZoneId())));
	m_zones.select(std::max(0, current), false);
	m_zones.scrollTo(std::max(0, m_zones.state.selected - 5));
	m_zones.picked = [this](int) { updateButtons(); };

	m_back = PanelButton(QString(), QRect(W - MARGIN - 2 * ARROW_W - 10, BTN_Y, ARROW_W, PL_BUTTON_H));
	m_next = PanelButton(QString(), QRect(W - MARGIN - ARROW_W, BTN_Y, ARROW_W, PL_BUTTON_H), true);
	m_goAhead = PanelButton("Go Ahead", QRect(W - MARGIN - 90, BTN_Y, 90, PL_BUTTON_H), true);
	m_back.clicked = [this] { showPage(static_cast<Page>(m_page - 1)); };
	m_next.clicked = [this] { showPage(static_cast<Page>(m_page + 1)); };
	m_goAhead.clicked = [this] { goAhead(); };

	showPage(Intro);
}

void SetupWindow::showPage(Page p) {
	if (p < Intro || p >= PageCount) {
		return;
	}
	m_page = p;
	m_error.clear();
	if (p == Computer && m_computerName.text.isEmpty()) {
		const QString first = m_fullName.text.trimmed().section(' ', 0, 0);
		m_computerName.setText(first.isEmpty() ? QStringLiteral("ZacOS 9")
		                                       : first + QStringLiteral("'s ZacOS 9"));
		m_computerName.selectAll();
	}

	m_host.edits.clear();
	m_host.checks.clear();
	switch (p) {
	case Name: m_host.edits = { &m_fullName, &m_shortName }; break;
	case Password:
		m_host.edits = { &m_password, &m_verify };
		m_host.checks = { &m_autoLogin };
		break;
	case Computer: m_host.edits = { &m_computerName }; break;
	default: break;
	}
	m_back.rect.moveLeft(p == Conclusion ? W - MARGIN - 90 - 10 - ARROW_W
	                                     : W - MARGIN - 2 * ARROW_W - 10);
	m_host.buttons = { &m_back, p == Conclusion ? &m_goAhead : &m_next };
	m_host.defaultButton = p == Conclusion ? &m_goAhead : &m_next;
	m_host.setFocus(m_host.edits.empty() ? nullptr : m_host.edits.front());
	updateButtons();
	update();
}

/* ---- the answers ------------------------------------------------------------------- */

QString SetupWindow::problem(bool *blocked) const {
	*blocked = false;
	switch (m_page) {
	case Name: {
		const QString s = m_shortName.text;
		*blocked = m_fullName.text.trimmed().isEmpty() || s.isEmpty();
		if (s.isEmpty()) {
			return QString();
		}
		if (!SHORT_NAME.match(s).hasMatch()) {
			*blocked = true;
			return "The short name must start with a letter, then a-z, 0-9, - or _.";
		}
		if (s == "root" || s == "zacos9-setup" || accountExists(s)) {
			*blocked = true;
			return "There is already an account named “" + s + "”. Try another short name.";
		}
		return QString();
	}
	case Password:
		*blocked = m_password.text.isEmpty() || m_password.text != m_verify.text;
		if (!m_verify.text.isEmpty() && m_password.text != m_verify.text) {
			return "The two passwords don't match.";
		}
		return QString();
	case Computer: *blocked = m_computerName.text.trimmed().isEmpty(); return QString();
	case TimeZone: *blocked = m_zones.state.selected < 0; return QString();
	default: return QString();
	}
}

void SetupWindow::updateButtons() {
	bool blocked = false;
	problem(&blocked);
	const bool asking = m_state == State::Asking;
	m_back.enabled = asking && m_page > Intro;
	m_next.enabled = asking && !blocked;
	m_goAhead.enabled = asking;
	update();
}

/* "Adam's ZacOS 9" -> "adams-zacos-9": the name on the network. */
QString SetupWindow::hostName() const {
	QString out;
	for (QChar ch : m_computerName.text.trimmed().toLower().normalized(QString::NormalizationForm_KD)) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out += ch;
		} else if (ch == '\'' || ch == QChar(0x2019) || ch.isMark()) {
			continue; /* "Adam's" -> "adams", "é" -> "e" */
		} else if (!out.endsWith('-')) {
			out += '-';
		}
	}
	while (out.startsWith('-')) {
		out.remove(0, 1);
	}
	out = out.left(63);
	while (out.endsWith('-')) {
		out.chop(1);
	}
	return out.isEmpty() ? QStringLiteral("zacos9") : out;
}

QString SetupWindow::zoneId() const {
	const int i = m_zones.state.selected;
	return i >= 0 && i < m_zoneIds.size() ? m_zoneIds[i] : QString();
}

void SetupWindow::goAhead() {
	if (m_state != State::Asking) {
		return;
	}
	m_state = State::Working;
	m_error.clear();
	updateButtons();

	const auto line = [](const char *key, QString value) {
		value.remove('\n').remove('\r');
		return QByteArray(key) + '=' + value.toUtf8() + '\n';
	};
	QByteArray input;
	input += line("fullname", m_fullName.text.trimmed());
	input += line("username", m_shortName.text);
	input += line("password", m_password.text);
	input += line("computername", m_computerName.text.trimmed());
	input += line("hostname", hostName());
	input += line("timezone", zoneId());
	input += line("autologin", m_autoLogin.on ? "1" : "0");

	/* The helper's "done"/"error" lines are on stdout; stderr (pkexec's
	 * own complaints, its tools' warnings) only matters if it fails. */
	m_helper = new QProcess(this);
	auto stderrLast = std::make_shared<QString>();
	connect(m_helper, &QProcess::readyReadStandardError, this, [this, stderrLast] {
		for (const QByteArray &raw : m_helper->readAllStandardError().split('\n')) {
			if (!raw.trimmed().isEmpty()) {
				*stderrLast = QString::fromUtf8(raw).trimmed();
			}
		}
	});
	connect(m_helper, &QProcess::readyReadStandardOutput, this, [this] {
		while (m_helper->canReadLine()) {
			const QString l = QString::fromUtf8(m_helper->readLine()).trimmed();
			if (l == "done") {
				m_state = State::Finished;
			} else if (l.startsWith("error ")) {
				m_error = l.mid(6);
			}
		}
		update();
	});
	connect(m_helper, &QProcess::finished, this, [this, stderrLast](int code, QProcess::ExitStatus) {
		if (m_state != State::Finished) {
			m_state = State::Asking;
			if (m_error.isEmpty()) {
				m_error = !stderrLast->isEmpty()
					? *stderrLast
					: "Setup didn't finish (exit " + QString::number(code) + ").";
			}
		}
		updateButtons();
		m_helper->deleteLater();
		m_helper = nullptr;
	});
	m_helper->start("pkexec", { QStringLiteral("/usr/libexec/zacos9/zacos9-setup-helper") });
	m_helper->write(input);
	m_helper->closeWriteChannel();
}

/* ---- painting ---------------------------------------------------------------------- */

static int wrapped(pl_canvas *c, const QString &s, int y, pl_font font = PL_FONT_SYSTEM,
		uint32_t color = C_BLACK) {
	for (const QString &l : panelWrap(s, PAGE_W, font)) {
		panelText(c, l, X0, y, font, color);
		y += 16;
	}
	return y;
}

void SetupWindow::paintArrow(pl_canvas *c, const PanelButton &b, bool right) const {
	const uint32_t ink = !b.enabled ? GRAY(0x9) : (b.down && b.inside) ? C_WHITE : C_BLACK;
	const int cx = b.rect.center().x(), cy = b.rect.top() + PL_BUTTON_H / 2 - 1;
	for (int i = 0; i < 5; i++) { /* a 5-wide, 9-tall triangle */
		const int x = right ? cx - 2 + i : cx + 2 - i;
		pl_vline(c, x, cy - (4 - i), cy + (4 - i), ink);
	}
}

void SetupWindow::paintPage(pl_canvas *c) {
	switch (m_page) {
	case Intro: {
		int y = wrapped(c, "Welcome to ZacOS 9.", 72);
		y = wrapped(c, "This assistant asks a few questions to set up your computer: your name, "
		               "a password, a name for the computer and your time zone. You can change "
		               "them later.", y + 10);
		wrapped(c, "Click the right arrow to begin.", y + 10);
		break;
	}
	case Name:
		panelText(c, "What is your name?", X0, 72);
		panelText(c, "Short name, for logging in and your Home folder:", X0, 130);
		break;
	case Password:
		wrapped(c, "Choose a password for your account. You'll need it to install software "
		           "and change settings.", 72);
		panelLabel(c, "Password:", X0 + 84, m_password.rect.top() + 15);
		panelLabel(c, "Verify:", X0 + 84, m_verify.rect.top() + 15);
		wrapped(c, m_autoLogin.on
				? "ZacOS 9 will start up straight to your desktop."
				: "ZacOS 9 will ask for your short name and password when it starts up.",
			m_autoLogin.pos.y() + 40, PL_FONT_VIEWS, GRAY(0x4));
		break;
	case Computer: {
		panelText(c, "What name would you like for this computer?", X0, 72);
		const int y = wrapped(c, "Other computers on your network see this name, for example "
		                         "when you share files.", 130);
		panelText(c, "Network name: " + hostName(), X0, y + 8, PL_FONT_VIEWS, GRAY(0x4));
		break;
	}
	case TimeZone: panelText(c, "What time zone are you in?", X0, 72); break;
	case Conclusion: {
		panelText(c, "You're ready to set up ZacOS 9:", X0, 72);
		const QString rows[][2] = {
			{ "Name:", m_fullName.text.trimmed() },
			{ "Short name:", m_shortName.text },
			{ "Computer name:", m_computerName.text.trimmed() },
			{ "Time zone:", QString(zoneId()).replace('_', ' ') },
			{ "At startup:", m_autoLogin.on ? "Log in automatically" : "Ask for password" },
		};
		int y = 98;
		for (const auto &r : rows) {
			panelLabel(c, r[0], X0 + 104, y);
			panelText(c, r[1], X0 + 112, y, PL_FONT_SYSTEM, C_BLACK, PAGE_W - 112);
			y += 18;
		}
		y += 10;
		switch (m_state) {
		case State::Asking:
			y = wrapped(c, "Click Go Ahead to set up your computer, or the left arrow to change "
			               "your answers.", y);
			break;
		case State::Working: y = wrapped(c, "Setting up your computer…", y); break;
		case State::Finished:
			y = wrapped(c, "All set. Starting ZacOS 9 for " +
				m_fullName.text.trimmed().section(' ', 0, 0) + "…", y);
			break;
		}
		if (!m_error.isEmpty()) {
			wrapped(c, m_error, y + 6, PL_FONT_SYSTEM, RED);
		}
		break;
	}
	default: break;
	}
}

void SetupWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* The logo beside every page, the way the Mac assistant kept its picture there. */
	const int s = PL_LOGO_SIZE_HQ;
	pl_image_blend(c, (X0 - s) / 2, 56, logo_pixels_hq(), s, s, false);

	panelText(c, TITLES[m_page], X0, 36);
	pl_hline(c, X0, W - MARGIN, 44, GRAY(0x8));
	pl_hline(c, X0, W - MARGIN, 45, C_WHITE);

	paintPage(c);
	if (m_page == TimeZone) {
		m_zones.paint(c, true);
	}
	bool blocked = false;
	const QString why = problem(&blocked);
	if (!why.isEmpty()) {
		panelText(c, why, X0, RULE_Y - 10, PL_FONT_SYSTEM, RED, PAGE_W);
	}

	pl_hline(c, 0, W - 1, RULE_Y, GRAY(0x8));
	pl_hline(c, 0, W - 1, RULE_Y + 1, C_WHITE);
	panelText(c, QString("Step %1 of %2").arg(m_page + 1).arg(int(PageCount)), MARGIN, BTN_Y + 14,
		PL_FONT_VIEWS, GRAY(0x4));

	m_host.paintControls(c, FACE);
	paintArrow(c, m_back, false);
	if (m_page != Conclusion) {
		paintArrow(c, m_next, true);
	}

	QPainter p(this);
	px.blit(p);
}

/* ---- input ------------------------------------------------------------------------- */

void SetupWindow::mousePressEvent(QMouseEvent *e) {
	if (m_page == TimeZone && m_state == State::Asking &&
			m_zones.press(e->position().toPoint())) {
		updateButtons();
		return;
	}
	if (m_host.hostPress(e)) {
		update();
	}
}

void SetupWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_zones.move(e->position().toPoint()) || m_host.hostMove(e)) {
		update();
	}
}

void SetupWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_zones.release() | m_host.hostRelease(e)) {
		updateButtons();
	}
}

void SetupWindow::wheelEvent(QWheelEvent *e) {
	if (m_page == TimeZone && m_zones.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void SetupWindow::keyPressEvent(QKeyEvent *e) {
	if (m_page == TimeZone && m_zones.key(e->key(), e->text())) {
		updateButtons();
		return;
	}
	if (m_host.hostKey(e)) {
		updateButtons();
		return;
	}
	QWidget::keyPressEvent(e);
}

/* Closing it would leave the first-run account with nothing to do. */
void SetupWindow::closeEvent(QCloseEvent *e) {
	if (m_state == State::Finished) {
		e->accept();
	} else {
		e->ignore();
	}
}
