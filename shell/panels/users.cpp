/* GIO before Qt: its headers use the name `signals`. */
#include "appfilter.h"
#include "users.h"
#include "panelregistry.h"

#include <QDBusConnection>
#include <QDir>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <crypt.h>
#include <unistd.h>

#include "alert.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "sharingclient.h"

namespace {

const QString SERVICE = QStringLiteral("org.freedesktop.Accounts");
const QString ROOT = QStringLiteral("/org/freedesktop/Accounts");
const QString IFACE = QStringLiteral("org.freedesktop.Accounts");
const QString USER_IFACE = QStringLiteral("org.freedesktop.Accounts.User");
/* malcontent's, which accountsservice serves for each person. */
const QString MCT_IFACE = QStringLiteral("com.endlessm.ParentalControls.AppFilter");
const QString PROPS = QStringLiteral("org.freedesktop.DBus.Properties");

constexpr uint32_t FACE = GRAY(0xD);
constexpr int PICTURE = 48;

QDBusConnection bus() {
	return qEnvironmentVariable("ZACOS9_ACCOUNTS_BUS") == "session" ? QDBusConnection::sessionBus()
		: QDBusConnection::systemBus();
}

/* A call that may wait for an administrator's password: polkit only asks
 * when the caller allows it, and the asking can take a while. */
void send(QObject *context, const QString &path, const QString &iface, const QString &method,
		const QVariantList &args, std::function<void(const QDBusMessage &)> done) {
	QDBusMessage message = QDBusMessage::createMethodCall(SERVICE, path, iface, method);
	message.setArguments(args);
	message.setInteractiveAuthorizationAllowed(true);
	auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(message, 10 * 60 * 1000), context);
	QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, done] {
		watcher->deleteLater();
		done(watcher->reply());
	});
}

QString errorOf(const QDBusMessage &reply) {
	return reply.type() == QDBusMessage::ErrorMessage
		? accountsErrorText(reply.errorName(), reply.errorMessage()) : QString();
}

} // namespace

bool validShortName(const QString &name) {
	static const QRegularExpression re("\\A[a-z][a-z0-9_-]{0,31}\\z");
	return re.match(name).hasMatch();
}

QString shortNameFor(const QString &realName) {
	QString first = realName.trimmed().section(' ', 0, 0).toLower();
	QString out;
	for (const QChar c : first.normalized(QString::NormalizationForm_KD)) {
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
			out += c;
		}
	}
	while (!out.isEmpty() && !(out[0] >= 'a' && out[0] <= 'z')) {
		out.remove(0, 1);
	}
	return out.left(32);
}

QString hashPassword(const QString &password) {
	/* The system's preferred method and a fresh salt (NULL prefix). */
	char *setting = crypt_gensalt_ra(nullptr, 0, nullptr, 0);
	if (!setting) {
		return QString();
	}
	struct crypt_data data = {};
	const char *hashed = crypt_r(password.toUtf8().constData(), setting, &data);
	free(setting);
	/* crypt(3) fails with a string starting '*', never a valid hash. */
	if (!hashed || hashed[0] == '*') {
		return QString();
	}
	return QString::fromLatin1(hashed);
}

QString accountsErrorText(const QString &dbusError, const QString &message) {
	if (dbusError.endsWith(".PermissionDenied") || dbusError.endsWith(".AccessDenied") ||
			dbusError.endsWith(".NotAuthorized")) {
		return "You need an administrator's name and password for that.";
	}
	if (dbusError.endsWith(".ServiceUnknown") || dbusError.endsWith(".NameHasNoOwner")) {
		return "The Users control panel needs accountsservice, which isn't installed or running.";
	}
	if (dbusError.endsWith(".UserExists")) {
		return "There is already an account with that short name.";
	}
	return message.isEmpty() ? dbusError : message;
}

/* ---- accountsservice ------------------------------------------------------ */

AccountsClient::AccountsClient(QObject *parent) : QObject(parent) {
}

void AccountsClient::list(std::function<void(const QList<Account> &, const QString &)> done) {
	send(this, ROOT, IFACE, "ListCachedUsers", {}, [this, done](const QDBusMessage &reply) {
		const QString error = errorOf(reply);
		if (!error.isEmpty() || reply.arguments().isEmpty()) {
			done({}, error.isEmpty() ? "accountsservice gave no list of accounts." : error);
			return;
		}
		const auto paths = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().first());
		if (paths.isEmpty()) {
			done({}, QString());
			return;
		}
		struct Pending {
			QList<Account> accounts;
			int left;
		};
		auto pending = std::make_shared<Pending>(Pending{ {}, int(paths.size()) });
		for (const QDBusObjectPath &path : paths) {
			send(this, path.path(), "org.freedesktop.DBus.Properties", "GetAll", { USER_IFACE },
					[pending, path, done](const QDBusMessage &props) {
				if (props.type() != QDBusMessage::ErrorMessage && !props.arguments().isEmpty()) {
					const QVariantMap p = qdbus_cast<QVariantMap>(props.arguments().first());
					/* People, not the system's own accounts. */
					if (!p.value("SystemAccount").toBool()) {
						Account a;
						a.path = path.path();
						a.userName = p.value("UserName").toString();
						a.realName = p.value("RealName").toString();
						a.iconFile = p.value("IconFile").toString();
						a.home = p.value("HomeDirectory").toString();
						a.uid = p.value("Uid").toLongLong();
						a.admin = p.value("AccountType").toInt() == 1;
						a.locked = p.value("Locked").toBool();
						pending->accounts << a;
					}
				}
				if (--pending->left == 0) {
					std::sort(pending->accounts.begin(), pending->accounts.end(),
						[](const Account &a, const Account &b) {
							return a.shownName().localeAwareCompare(b.shownName()) < 0;
						});
					done(pending->accounts, QString());
				}
			});
		}
	});
}

void AccountsClient::create(const QString &userName, const QString &realName, bool admin,
		std::function<void(const QString &, const QString &)> done) {
	send(this, ROOT, IFACE, "CreateUser", { userName, realName, admin ? 1 : 0 },
			[done](const QDBusMessage &reply) {
		const QString error = errorOf(reply);
		done(error.isEmpty() && !reply.arguments().isEmpty()
			? qdbus_cast<QDBusObjectPath>(reply.arguments().first()).path() : QString(), error);
	});
}

void AccountsClient::remove(qint64 uid, bool removeFiles, Done done) {
	send(this, ROOT, IFACE, "DeleteUser", { QVariant::fromValue(uid), removeFiles },
		[done](const QDBusMessage &reply) { done(errorOf(reply)); });
}

void AccountsClient::callUser(const QString &path, const QString &method, const QVariantList &args,
		Done done) {
	send(this, path, USER_IFACE, method, args, [done](const QDBusMessage &reply) { done(errorOf(reply)); });
}

void AccountsClient::appFilter(const QString &path,
		std::function<void(bool, bool, const QStringList &)> done) {
	send(this, path, PROPS, "Get", { MCT_IFACE, QStringLiteral("AppFilter") },
			[done](const QDBusMessage &reply) {
		if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
			done(false, false, {});
			return;
		}
		const QVariant value = reply.arguments().first().value<QDBusVariant>().variant();
		if (!value.canConvert<QDBusArgument>()) {
			done(false, false, {});
			return;
		}
		const QDBusArgument arg = value.value<QDBusArgument>();
		bool allowlist = false;
		QStringList entries;
		arg.beginStructure();
		arg >> allowlist >> entries;
		arg.endStructure();
		/* A blocklist is "everything but"; Limited is an allowlist. */
		done(true, allowlist, allowlist ? entries : QStringList());
	});
}

void AccountsClient::setAppFilter(const QString &path, bool limited, const QStringList &allowed,
		Done done) {
	/* Not limited: an empty blocklist, malcontent's "no filter". */
	QDBusArgument arg;
	arg.beginStructure();
	arg << limited << (limited ? allowed : QStringList());
	arg.endStructure();
	send(this, path, PROPS, "Set",
		{ MCT_IFACE, QStringLiteral("AppFilter"), QVariant::fromValue(QDBusVariant(QVariant::fromValue(arg))) },
		[done](const QDBusMessage &reply) {
			if (reply.type() == QDBusMessage::ErrorMessage &&
					reply.errorName() == "org.freedesktop.DBus.Error.UnknownInterface") {
				done("Limited accounts need malcontent, which isn't installed.");
				return;
			}
			done(errorOf(reply));
		});
}

/* ---- the Users window ---------------------------------------------------- */

namespace {
constexpr int PW = 440, PH = 330, M = 12;
constexpr int LIST_TOP = 34, LIST_BOTTOM = 214;
}

struct UsersPanel::Parts {
	PanelHost host;
	PanelList list;
	PanelButton add, open, remove, options;
	AccountsClient client;
	QList<Account> accounts;
	bool loaded = false;
	QString error;
	QList<QPointer<AccountWindow>> windows;
	explicit Parts(QWidget *w) : host(w) {}
};

UsersPanel::UsersPanel() : d(std::make_unique<Parts>(this)) {
	setWindowTitle("Users");
	setFixedSize(PW, PH);
	d->list.frame = QRect(M, LIST_TOP, PW - 2 * M, LIST_BOTTOM - LIST_TOP);
	d->host.hintAreas.push_back({ &d->list.frame, "The people who can use this computer. Double-click one to change it." });
	d->list.picked = [this](int) { updateButtons(); };
	const int y = PH - M - PL_BUTTON_H;
	d->add = PanelButton("New User…", QRect(M, y, 96, PL_BUTTON_H));
	d->add.hint = "Makes an account for a new person.";
	d->add.clicked = [this] { newUser(); };
	d->open = PanelButton("Open…", QRect(M + 104, y, 80, PL_BUTTON_H));
	d->open.hint = "Changes the selected person's name, picture, password or kind of account.";
	d->open.clicked = [this] { openSelected(); };
	d->remove = PanelButton("Delete…", QRect(PW - M - 4 - 80, y, 80, PL_BUTTON_H));
	d->remove.hint = "Deletes the selected person's account.";
	d->remove.clicked = [this] { deleteSelected(false); };
	d->options = PanelButton("Options…", QRect(M + 192, y, 84, PL_BUTTON_H));
	d->options.hint = "Sets up the Guest account and the login window's welcome message.";
	d->options.clicked = [this] { openOptions(); };
	d->host.buttons = { &d->add, &d->open, &d->options, &d->remove };
	updateButtons();
	QTimer::singleShot(0, this, [this] { reload(); });
}

UsersPanel::~UsersPanel() {
	for (const auto &w : d->windows) {
		delete w.data();
	}
}

void UsersPanel::reload() {
	d->client.list([this](const QList<Account> &accounts, const QString &error) {
		const QString chosen = selected() ? selected()->userName : QString();
		d->accounts = accounts;
		d->error = error;
		d->loaded = true;
		fill();
		int row = -1;
		for (int i = 0; i < d->accounts.size(); ++i) {
			if (d->accounts[i].userName == chosen || (chosen.isEmpty() && d->accounts[i].uid == getuid())) {
				row = i;
			}
		}
		d->list.select(row, false);
		updateButtons();
	});
}

void UsersPanel::fill() {
	QStringList rows;
	for (const Account &a : d->accounts) {
		QString row = a.shownName() + "  –  " + (a.admin ? "Administrator" : "Standard");
		if (a.uid == qint64(getuid())) {
			row += " (you)";
		} else if (a.locked) {
			row += ", can't log in";
		}
		rows << row;
	}
	d->list.setItems(rows);
}

const Account *UsersPanel::selected() const {
	const int row = d->list.state.selected;
	return row >= 0 && row < d->accounts.size() ? &d->accounts[row] : nullptr;
}

void UsersPanel::updateButtons() {
	const Account *a = selected();
	const bool up = d->error.isEmpty() && d->loaded;
	d->add.enabled = up;
	d->open.enabled = up && a;
	/* Not yourself: you'd be deleting the account you're using. */
	d->remove.enabled = up && a && a->uid != qint64(getuid());
	update();
}

QStringList UsersPanel::rows() const {
	return d->list.items;
}

bool UsersPanel::loaded() const {
	return d->loaded;
}

void UsersPanel::select(int row) {
	d->list.select(row, true);
}

QString UsersPanel::lastError() const {
	return d->error;
}

void UsersPanel::showError(const QString &error) {
	d->error = error;
	if (!error.isEmpty() && qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") {
		Alert::ask(error, "OK", QString());
	}
	d->error.clear();
	reload();
}

AccountWindow *UsersPanel::newUser() {
	auto *w = new AccountWindow(this, &d->client, nullptr);
	d->windows << w;
	w->show();
	return w;
}

AccountWindow *UsersPanel::openSelected() {
	const Account *a = selected();
	if (!a) {
		return nullptr;
	}
	auto *w = new AccountWindow(this, &d->client, a);
	d->windows << w;
	w->show();
	return w;
}

OptionsWindow *UsersPanel::openOptions() {
	auto *w = new OptionsWindow(this);
	w->show();
	return w;
}

void UsersPanel::deleteSelected(bool removeFiles, bool ask) {
	const Account *a = selected();
	if (!a || a->uid == qint64(getuid())) {
		return;
	}
	if (ask) {
		const Alert::Choice choice = Alert::choose(
			QStringLiteral("Delete the account “%1”? Its home folder (%2) can be kept for an "
				"administrator to look through, or deleted with everything in it.")
				.arg(a->shownName(), a->home),
			"Keep Home Folder", "Cancel", "Delete Everything");
		if (choice == Alert::Cancel) {
			return;
		}
		removeFiles = choice == Alert::Other;
	}
	d->client.remove(a->uid, removeFiles, [this](const QString &error) { showError(error); });
}

void UsersPanel::paintEvent(QPaintEvent *) {
	Pixels px(PW, PH);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, PW - 1, PH - 1, FACE);
	panelText(c, "People who use this computer:", M, LIST_TOP - 10, PL_FONT_SYSTEM);
	d->list.paint(c, true);
	int y = LIST_BOTTOM + 16;
	QString about;
	if (!d->loaded) {
		about = "Looking for accounts…";
	} else if (!d->error.isEmpty()) {
		about = d->error;
	} else if (const Account *a = selected()) {
		panelIcon(c, accountPicture(a->iconFile, a->shownName()), M, y - 8);
		const int x = M + PICTURE + 10;
		panelText(c, a->shownName(), x, y + 2, PL_FONT_SYSTEM);
		panelText(c, "Short name: " + a->userName, x, y + 16, PL_FONT_VIEWS);
		panelText(c, a->admin ? "Can administer this computer." : "A standard account.", x, y + 28,
			PL_FONT_VIEWS);
		panelText(c, a->locked ? "Can't log in." : "Home folder: " + a->home, x, y + 40, PL_FONT_VIEWS);
	} else {
		about = "Click an account to see it; New User… adds one.";
	}
	for (const QString &line : panelWrap(about, PW - 2 * M, PL_FONT_VIEWS)) {
		panelText(c, line, M, y, PL_FONT_VIEWS, C_BLACK, PW - 2 * M);
		y += 12;
	}
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void UsersPanel::mousePressEvent(QMouseEvent *e) {
	if (d->list.press(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostPress(e);
	update();
}

void UsersPanel::mouseMoveEvent(QMouseEvent *e) {
	if (d->list.move(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostMove(e);
	update();
}

void UsersPanel::mouseReleaseEvent(QMouseEvent *e) {
	d->list.release();
	d->host.hostRelease(e);
	update();
}

void UsersPanel::mouseDoubleClickEvent(QMouseEvent *e) {
	if (d->list.frame.contains(e->position().toPoint()) && selected()) {
		openSelected();
		return;
	}
	mousePressEvent(e);
}

void UsersPanel::wheelEvent(QWheelEvent *e) {
	if (d->list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void UsersPanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && (e->key() == Qt::Key_W || e->key() == Qt::Key_Q)) {
		close();
		return;
	}
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_O) {
		openSelected();
		return;
	}
	if (d->list.key(e->key(), e->text())) {
		updateButtons();
		return;
	}
	d->host.hostKey(e);
	update();
}

void UsersPanel::changeEvent(QEvent *e) {
	/* Back in front: someone may have changed the accounts meanwhile. */
	if (e->type() == QEvent::ActivationChange && isActiveWindow() && d->loaded) {
		reload();
	}
	QWidget::changeEvent(e);
}

/* ---- one account ---------------------------------------------------------- */

namespace {
constexpr int AW = 450, AH = 340, LABEL = 118, FIELD = 130;
}

struct AccountWindow::Parts {
	PanelHost host;
	QPointer<UsersPanel> panel;
	AccountsClient *client;
	bool creating;
	Account original;
	PanelEdit realName, userName, password, verify;
	PanelCheckbox admin, canLogIn, limited, puppy;
	bool puppyWas = false;
	PanelButton choose, cancel, save, programs;
	QStringList allowed; /* malcontent's entries, for a Limited account */
	bool filterKnown = false;
	void updateLimited() {
		limited.enabled = filterKnown && !admin.on && admin.enabled;
		programs.enabled = limited.enabled && limited.on;
		puppy.enabled = !admin.on && admin.enabled;
	}
	QString picture; /* a new one chosen, to set on Save */
	bool userNameTouched = false;
	bool saving = false;
	explicit Parts(QWidget *w) : host(w) {}
};

AccountWindow::AccountWindow(UsersPanel *panel, AccountsClient *client, const Account *account)
		: d(std::make_unique<Parts>(this)) {
	d->panel = panel;
	d->client = client;
	d->creating = !account;
	if (account) {
		d->original = *account;
	}
	const bool you = account && account->uid == qint64(getuid());
	setWindowTitle(d->creating ? "New User" : d->original.shownName());
	setFixedSize(AW, AH);
	int y = 16;
	d->realName.rect = QRect(FIELD, y, AW - FIELD - M, PL_EDIT_H);
	d->realName.hint = "The person's full name, shown at login.";
	d->realName.setText(d->original.realName);
	d->realName.edited = [this] {
		if (d->creating && !d->userNameTouched) {
			d->userName.setText(shortNameFor(d->realName.text));
		}
		update();
	};
	y += 30;
	d->userName.rect = QRect(FIELD, y, 160, PL_EDIT_H);
	d->userName.hint = "The short name the system uses: lowercase letters, digits and - or _.";
	d->userName.setText(d->original.userName);
	d->userName.enabled = d->creating; /* a home folder and files are named after it */
	d->userName.accepts = [](QChar c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
	};
	d->userName.edited = [this] {
		d->userNameTouched = true;
		update();
	};
	y += 30 + PICTURE + 14;
	d->choose = PanelButton("Choose…", QRect(FIELD + PICTURE + 12, 76 + (PICTURE - PL_BUTTON_H) / 2,
		80, PL_BUTTON_H));
	d->choose.hint = "Chooses the person's picture, shown at login.";
	d->choose.clicked = [this] {
		const QString file = QFileDialog::getOpenFileName(this, "Choose a Picture",
			QDir::homePath() + "/Pictures", "Pictures (*.png *.jpg *.jpeg *.gif *.bmp)");
		if (!file.isEmpty()) {
			setPicture(file);
		}
	};
	d->password.rect = QRect(FIELD, y, 160, PL_EDIT_H);
	d->password.hint = "The person's login password.";
	d->password.password = true;
	y += 30;
	d->verify.rect = QRect(FIELD, y, 160, PL_EDIT_H);
	d->verify.hint = "Type the password again, to be sure it is right.";
	d->verify.password = true;
	y += 36;
	d->admin = PanelCheckbox("Can administer this computer", QPoint(FIELD, y));
	d->admin.hint = "Lets this person change system settings and install software.";
	d->admin.on = d->original.admin;
	/* Your own: you couldn't give it back. */
	d->admin.enabled = !you;
	y += 22;
	d->canLogIn = PanelCheckbox("Can log in", QPoint(FIELD, y));
	d->canLogIn.hint = "Lets this person log in. Off: the account stays, but can't be used.";
	d->canLogIn.on = !d->original.locked;
	d->canLogIn.enabled = !you;
	y += 26;
	d->limited = PanelCheckbox("Use only chosen programs", QPoint(FIELD, y));
	d->limited.hint = "Limits this person to the programs you choose. Not a security boundary: "
		"programs started some other way still run.";
	d->limited.toggled = [this](bool) { d->updateLimited(); update(); };
	d->programs = PanelButton("Programs…", QRect(AW - M - 4 - 96, y - 4, 96, PL_BUTTON_H));
	d->programs.hint = "Chooses the programs this person can use.";
	d->programs.clicked = [this] {
		auto *w = new ProgramsWindow(this, d->realName.text.isEmpty() ? d->userName.text : d->realName.text,
			d->allowed, [this](const QStringList &allowed) {
				d->allowed = allowed;
				update();
			});
		w->show();
	};
	y += 22;
	d->puppy = PanelCheckbox("Puppy Mode", QPoint(FIELD, y));
	d->puppy.hint = "Gives this person only the Sniffer's essential commands, which they can't "
		"turn off. For children and people new to computers.";
	d->puppy.on = d->puppyWas = !d->creating && QFile::exists(accountsConf("puppy/" + d->original.userName));
	d->admin.toggled = [this](bool) { d->updateLimited(); update(); };
	if (d->creating) {
		d->filterKnown = true; /* without malcontent, saving says so */
	} else {
		QPointer<AccountWindow> self(this); /* it may close before the answer */
		d->client->appFilter(d->original.path, [this, self](bool known, bool limited,
				const QStringList &allowed) {
			if (!self) {
				return;
			}
			d->filterKnown = d->original.filterKnown = known;
			d->limited.on = d->original.limited = limited;
			d->allowed = d->original.allowed = allowed;
			d->updateLimited();
			update();
		});
	}
	d->updateLimited();
	const int by = AH - M - PL_BUTTON_H;
	d->save = PanelButton(d->creating ? "Create" : "Save", QRect(AW - M - 4 - 72, by, 72, PL_BUTTON_H), true);
	d->save.clicked = [this] {
		QString why;
		if (!save(&why) && !why.isEmpty() && qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") {
			Alert::ask(why, "OK", QString());
		}
	};
	d->cancel = PanelButton("Cancel", QRect(AW - M - 4 - 72 - 12 - 72, by, 72, PL_BUTTON_H));
	d->cancel.clicked = [this] { close(); };
	d->host.edits = { &d->realName, &d->userName, &d->password, &d->verify };
	d->host.checks = { &d->admin, &d->canLogIn, &d->limited, &d->puppy };
	d->host.buttons = { &d->choose, &d->cancel, &d->save, &d->programs };
	d->host.defaultButton = &d->save;
	d->host.cancelButton = &d->cancel;
	d->host.setFocus(&d->realName);
	setAttribute(Qt::WA_DeleteOnClose);
}

AccountWindow::~AccountWindow() = default;

void AccountWindow::setRealName(const QString &name) {
	d->realName.setText(name);
	if (d->realName.edited) {
		d->realName.edited();
	}
}

void AccountWindow::setUserName(const QString &name) {
	d->userName.setText(name);
	d->userNameTouched = true;
}

void AccountWindow::setPassword(const QString &password, const QString &verify) {
	d->password.setText(password);
	d->verify.setText(verify);
}

void AccountWindow::setAdmin(bool admin) {
	if (d->admin.enabled) {
		d->admin.on = admin;
	}
}

void AccountWindow::setCanLogIn(bool can) {
	if (d->canLogIn.enabled) {
		d->canLogIn.on = can;
	}
}

void AccountWindow::setLimited(bool limited, const QStringList &allowed) {
	d->filterKnown = true;
	d->limited.on = limited;
	d->allowed = allowed;
	d->updateLimited();
}

void AccountWindow::setPuppy(bool puppy) {
	d->puppy.on = puppy;
	d->updateLimited();
}

void AccountWindow::setPicture(const QString &file) {
	d->picture = file;
	update();
}

QString AccountWindow::problem() const {
	if (d->realName.text.trimmed().isEmpty()) {
		return "Type the person's name.";
	}
	if (d->creating && !validShortName(d->userName.text)) {
		return "The short name must start with a lowercase letter and have only lowercase letters, "
			"digits, - and _ (32 at most).";
	}
	if (d->password.text != d->verify.text) {
		return "The two passwords aren't the same. Type the password again in both boxes.";
	}
	if (d->creating && d->password.text.isEmpty()) {
		return "Type a password for the new account, twice.";
	}
	return QString();
}

bool AccountWindow::save(QString *why) {
	const QString p = problem();
	if (why) {
		*why = p;
	}
	if (!p.isEmpty() || d->saving || !d->panel) {
		return false;
	}
	/* What changed, applied one after another (each may ask for a password). */
	QStringList steps;
	if (!d->creating && d->realName.text.trimmed() != d->original.realName) {
		steps << "name";
	}
	if (!d->password.text.isEmpty()) {
		steps << "password";
	}
	if (!d->picture.isEmpty()) {
		steps << "picture";
	}
	if (!d->creating && d->admin.enabled && d->admin.on != d->original.admin) {
		steps << "admin";
	}
	if (d->canLogIn.enabled && d->canLogIn.on == d->original.locked && !(d->creating && d->canLogIn.on)) {
		steps << "locked";
	}
	const bool limited = d->limited.on && !d->admin.on;
	if ((d->puppy.on && !d->admin.on) != d->puppyWas) {
		steps << "puppy";
	}
	if (d->filterKnown && (limited != d->original.limited ||
			(limited && d->allowed != d->original.allowed))) {
		steps << "limited";
	}
	d->saving = true;
	if (d->creating) {
		d->client->create(d->userName.text, d->realName.text.trimmed(), d->admin.on,
				[this, steps](const QString &path, const QString &error) {
			if (!error.isEmpty() || path.isEmpty()) {
				d->saving = false;
				if (d->panel) {
					d->panel->showError(error.isEmpty() ? "The account wasn't made." : error);
				}
				return;
			}
			apply(path, steps);
		});
	} else {
		apply(d->original.path, steps);
	}
	return true;
}

void AccountWindow::apply(const QString &path, QStringList steps) {
	if (steps.isEmpty()) {
		d->saving = false;
		if (d->panel) {
			d->panel->showError(QString()); /* reads the list again */
		}
		close();
		return;
	}
	const QString step = steps.takeFirst();
	const auto next = [this, path, steps](const QString &error) {
		if (!error.isEmpty()) {
			d->saving = false;
			if (d->panel) {
				d->panel->showError(error);
			}
			return;
		}
		apply(path, steps);
	};
	if (step == "limited") {
		d->client->setAppFilter(path, d->limited.on && !d->admin.on, d->allowed, next);
		return;
	}
	if (step == "puppy") {
		bool ok = false;
		QString error;
		const QString user = d->creating ? d->userName.text : d->original.userName;
		runPrivilegedHelper("accounts", { "puppy", user, d->puppy.on && !d->admin.on ? "on" : "off" },
			QByteArray(), &ok, &error);
		next(ok ? QString() : error);
		return;
	}
	QString method;
	QVariantList args;
	if (step == "name") {
		method = "SetRealName";
		args = { d->realName.text.trimmed() };
	} else if (step == "password") {
		const QString hashed = hashPassword(d->password.text);
		if (hashed.isEmpty()) {
			d->saving = false;
			if (d->panel) {
				d->panel->showError("The password couldn't be prepared.");
			}
			return;
		}
		method = "SetPassword";
		args = { hashed, QString() };
	} else if (step == "picture") {
		method = "SetIconFile";
		args = { d->picture };
	} else if (step == "admin") {
		method = "SetAccountType";
		args = { d->admin.on ? 1 : 0 };
	} else {
		method = "SetLocked";
		args = { !d->canLogIn.on };
	}
	d->client->callUser(path, method, args, next);
}

void AccountWindow::paintEvent(QPaintEvent *) {
	Pixels px(AW, AH);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, AW - 1, AH - 1, FACE);
	const int base = 15;
	panelLabel(c, "Name:", LABEL, d->realName.rect.y() + base);
	panelLabel(c, "Short name:", LABEL, d->userName.rect.y() + base);
	panelLabel(c, "Picture:", LABEL, 76 + base);
	panelIcon(c, accountPicture(d->picture.isEmpty() ? d->original.iconFile : d->picture,
		d->realName.text.isEmpty() ? d->userName.text : d->realName.text), FIELD, 76);
	panelLabel(c, d->creating ? "Password:" : "New password:", LABEL, d->password.rect.y() + base);
	panelLabel(c, "Verify:", LABEL, d->verify.rect.y() + base);
	if (!d->creating) {
		panelText(c, "Leave blank to keep it.", d->password.rect.right() + 8,
			d->password.rect.y() + base, PL_FONT_VIEWS);
	}
	const QString why = problem();
	if (!why.isEmpty() && (d->creating ? !d->realName.text.isEmpty() : true) &&
			d->password.text != d->verify.text) {
		panelText(c, "The passwords don't match.", d->verify.rect.right() + 8, d->verify.rect.y() + base,
			PL_FONT_VIEWS);
	}
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void AccountWindow::mousePressEvent(QMouseEvent *e) {
	d->host.hostPress(e);
	update();
}

void AccountWindow::mouseMoveEvent(QMouseEvent *e) {
	d->host.hostMove(e);
	update();
}

void AccountWindow::mouseReleaseEvent(QMouseEvent *e) {
	d->host.hostRelease(e);
	update();
}

void AccountWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	d->host.hostKey(e);
	update();
}

/* ---- a Limited account's programs ----------------------------------------- */

namespace {
constexpr int RW = 340, RH = 340;
}

struct ProgramsWindow::Parts {
	PanelHost host;
	PanelList list;
	PanelButton cancel, ok;
	struct Program {
		QString name, entry;
	};
	std::vector<Program> programs;
	QSet<QString> on;
	QStringList others; /* allowed entries no installed program has: kept */
	QString who;
	std::function<void(const QStringList &)> done;
	explicit Parts(QWidget *w) : host(w) {}
};

ProgramsWindow::ProgramsWindow(QWidget *parent, const QString &who, const QStringList &allowed,
		std::function<void(const QStringList &)> done)
		: QWidget(parent, Qt::Dialog), d(std::make_unique<Parts>(this)) {
	d->who = who;
	d->done = std::move(done);
	setWindowTitle("Programs");
	setWindowModality(Qt::WindowModal);
	setAttribute(Qt::WA_DeleteOnClose);
	setFixedSize(RW, RH);
	/* Installed programs (as the Sniffer's Applications folder lists them),
	 * by name, one row per program; ZacOS's own are always allowed. */
	GList *all = g_app_info_get_all();
	QSet<QString> seen;
	for (GList *l = all; l; l = l->next) {
		auto *info = static_cast<GAppInfo *>(l->data);
		if (!g_app_info_should_show(info) || !G_IS_DESKTOP_APP_INFO(info)) {
			continue;
		}
		char *entry = pl_app_filter_entry(G_DESKTOP_APP_INFO(info));
		const QString e = QString::fromUtf8(entry ? entry : "");
		g_free(entry);
		if (e.isEmpty() || seen.contains(e) || QFileInfo(e).fileName().startsWith("zacos9-")) {
			continue;
		}
		seen.insert(e);
		d->programs.push_back({ QString::fromUtf8(g_app_info_get_name(info)), e });
	}
	g_list_free_full(all, g_object_unref);
	std::sort(d->programs.begin(), d->programs.end(), [](const auto &a, const auto &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	for (const QString &e : allowed) {
		(seen.contains(e) ? d->on : (d->others << e, d->on)).insert(e);
	}
	QStringList names;
	for (const auto &p : d->programs) {
		names << p.name;
	}
	d->list.frame = QRect(12, 34, RW - 24, RH - 34 - 50);
	d->list.setItems(names, {}, PL_CHECKBOX_SIZE + 6);
	d->list.state.selected = -1;
	d->host.hintAreas.push_back({ &d->list.frame, "Check the programs this person can use." });
	const int by = RH - 12 - PL_BUTTON_H;
	d->ok = PanelButton("OK", QRect(RW - 12 - 4 - 72, by, 72, PL_BUTTON_H), true);
	d->ok.clicked = [this] { accept(); };
	d->cancel = PanelButton("Cancel", QRect(RW - 12 - 4 - 72 - 12 - 72, by, 72, PL_BUTTON_H));
	d->cancel.clicked = [this] { close(); };
	d->host.buttons = { &d->cancel, &d->ok };
	d->host.defaultButton = &d->ok;
	d->host.cancelButton = &d->cancel;
}

ProgramsWindow::~ProgramsWindow() = default;

QStringList ProgramsWindow::names() const {
	QStringList out;
	for (const auto &p : d->programs) {
		out << p.name;
	}
	return out;
}

void ProgramsWindow::toggle(int row) {
	if (row < 0 || row >= int(d->programs.size())) {
		return;
	}
	const QString &e = d->programs[row].entry;
	if (!d->on.remove(e)) {
		d->on.insert(e);
	}
	update();
}

void ProgramsWindow::accept() {
	QStringList allowed = d->others;
	for (const auto &p : d->programs) {
		if (d->on.contains(p.entry)) {
			allowed << p.entry;
		}
	}
	if (d->done) {
		d->done(allowed);
	}
	close();
}

void ProgramsWindow::paintEvent(QPaintEvent *) {
	Pixels px(RW, RH);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, RW - 1, RH - 1, FACE);
	panelText(c, QStringLiteral("Programs %1 can use:").arg(d->who), 12, 24, PL_FONT_SYSTEM,
		C_BLACK, RW - 24);
	d->list.paint(c, true);
	const int visible = pl_list_visible_rows(d->list.frame.top(), d->list.frame.bottom());
	for (int r = 0; r < visible && d->list.state.top + r < int(d->programs.size()); r++) {
		const int i = d->list.state.top + r;
		pl_checkbox_paint(c, d->list.frame.left() + 3, d->list.frame.top() + 1 + r * PL_LIST_ROW_H + 2,
			d->on.contains(d->programs[i].entry) ? PL_CHECK_ON : PL_CHECK_OFF, false, true, nullptr);
	}
	panelText(c, "ZacOS's own programs are always available.", 12, RH - 12 - PL_BUTTON_H - 6,
		PL_FONT_VIEWS, GRAY(0x5));
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void ProgramsWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (d->list.frame.contains(pos)) {
		const int before = d->list.state.selected;
		d->list.state.selected = -1;
		d->list.press(pos);
		const int row = d->list.state.selected;
		if (row >= 0) {
			toggle(row);
		} else {
			d->list.state.selected = before; /* the scroll bar */
		}
	} else {
		d->host.hostPress(e);
	}
	update();
}

void ProgramsWindow::mouseMoveEvent(QMouseEvent *e) {
	d->list.move(e->position().toPoint());
	d->host.hostMove(e);
	update();
}

void ProgramsWindow::mouseReleaseEvent(QMouseEvent *e) {
	d->list.release();
	d->host.hostRelease(e);
	update();
}

void ProgramsWindow::wheelEvent(QWheelEvent *e) {
	if (d->list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void ProgramsWindow::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Space && d->list.state.selected >= 0) {
		toggle(d->list.state.selected);
	} else if (!d->list.key(e->key(), e->text())) {
		d->host.hostKey(e);
	}
	update();
}

/* ---- Options: the Guest account and the welcome message ------------------- */

QString accountsConf(const QString &name) {
	const QString env = qEnvironmentVariable("ZACOS9_ACCOUNTS_CONF");
	return (env.isEmpty() ? QString("/etc/zacos9") : env) + '/' + name;
}

namespace {
constexpr int OW = 380, OH = 196;
}

struct OptionsWindow::Parts {
	PanelHost host;
	PanelCheckbox guest;
	PanelEdit welcome;
	PanelButton cancel, ok;
	bool guestWas = false;
	QString welcomeWas;
	explicit Parts(QWidget *w) : host(w) {}
};

OptionsWindow::OptionsWindow(QWidget *parent)
		: QWidget(parent, Qt::Dialog), d(std::make_unique<Parts>(this)) {
	setWindowTitle("Users Options");
	setAttribute(Qt::WA_DeleteOnClose);
	setFixedSize(OW, OH);
	d->guest = PanelCheckbox("Allow guests to log in", QPoint(12, 16));
	d->guest.hint = "Adds a Guest account with no password. Its home folder is emptied each "
		"time someone logs in as Guest.";
	d->guest.on = d->guestWas = QFile::exists(accountsConf("guest-account"));
	QFile welcome(accountsConf("welcome"));
	if (welcome.open(QIODevice::ReadOnly)) {
		d->welcomeWas = QString::fromUtf8(welcome.readAll()).trimmed();
	}
	d->welcome.rect = QRect(12, 74, OW - 24, 2 * PL_EDIT_H);
	d->welcome.multiline = true;
	d->welcome.maxLength = 200;
	d->welcome.hint = "Shown in the login window instead of \"Welcome to ZacOS 9.\" Leave it "
		"empty for the standard one.";
	d->welcome.setText(d->welcomeWas);
	const int by = OH - 12 - PL_BUTTON_H;
	d->ok = PanelButton("OK", QRect(OW - 12 - 4 - 72, by, 72, PL_BUTTON_H), true);
	d->ok.clicked = [this] {
		QString error;
		if (save(&error)) {
			close();
		} else if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") {
			Alert::ask(error, "OK", QString());
		}
	};
	d->cancel = PanelButton("Cancel", QRect(OW - 12 - 4 - 72 - 12 - 72, by, 72, PL_BUTTON_H));
	d->cancel.clicked = [this] { close(); };
	d->host.checks = { &d->guest };
	d->host.edits = { &d->welcome };
	d->host.buttons = { &d->cancel, &d->ok };
	d->host.defaultButton = &d->ok;
	d->host.cancelButton = &d->cancel;
	d->host.setFocus(&d->welcome);
}

OptionsWindow::~OptionsWindow() = default;

void OptionsWindow::setGuest(bool on) {
	d->guest.on = on;
}

void OptionsWindow::setWelcome(const QString &text) {
	d->welcome.setText(text);
}

bool OptionsWindow::save(QString *error) {
	bool ok = true;
	QString why;
	if (d->guest.on != d->guestWas) {
		runPrivilegedHelper("accounts", { "guest", d->guest.on ? "on" : "off" }, QByteArray(), &ok, &why);
		if (ok) {
			d->guestWas = d->guest.on;
		}
	}
	const QString text = d->welcome.text.simplified();
	if (ok && text != d->welcomeWas) {
		runPrivilegedHelper("accounts", { "welcome" }, text.toUtf8(), &ok, &why);
		if (ok) {
			d->welcomeWas = text;
		}
	}
	if (error) {
		*error = why;
	}
	return ok;
}

void OptionsWindow::paintEvent(QPaintEvent *) {
	Pixels px(OW, OH);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, OW - 1, OH - 1, FACE);
	panelText(c, "No password; nothing is kept between logins.", 32, 46, PL_FONT_VIEWS);
	panelText(c, "Welcome message:", 12, 68);
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void OptionsWindow::mousePressEvent(QMouseEvent *e) {
	d->host.hostPress(e);
	update();
}

void OptionsWindow::mouseMoveEvent(QMouseEvent *e) {
	d->host.hostMove(e);
	update();
}

void OptionsWindow::mouseReleaseEvent(QMouseEvent *e) {
	d->host.hostRelease(e);
	update();
}

void OptionsWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	d->host.hostKey(e);
	update();
}

PANEL("users", "Users", true, UsersPanel);
