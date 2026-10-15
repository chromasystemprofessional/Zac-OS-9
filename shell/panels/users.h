#pragma once

/*
 * Users (Mac OS 9's Multiple Users control panel, Missing Manual ch. 17;
 * gap plan 2.14): the people who can use this computer. New User…, Open…
 * and Delete… go through accountsservice (org.freedesktop.Accounts on the
 * system bus), whose polkit rules decide who may: changing your own name
 * and picture needs nothing, anything else an administrator's password,
 * which the session's polkit agent asks for. ZACOS9_ACCOUNTS_BUS=session
 * talks to a stand-in on the session bus instead, for tests.
 */

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QWidget>
#include <functional>
#include <memory>

struct Account {
	QString path; /* its object in accountsservice */
	QString userName, realName, iconFile, home;
	qint64 uid = -1;
	bool admin = false, locked = false;
	/* As the list shows it: the full name, else the short one. */
	QString shownName() const { return realName.isEmpty() ? userName : realName; }
};

/* A short (Unix) name: a lowercase letter, then lowercase letters, digits,
 * '-' or '_', 32 at most. */
bool validShortName(const QString &name);
/* A short name made from a full one: "Ada Lovelace" gives "ada". */
QString shortNameFor(const QString &realName);
/* The password as accountsservice takes it, hashed by crypt(3) with the
 * system's preferred method; empty if that failed. */
QString hashPassword(const QString &password);
/* An accountsservice error, as a sentence for an alert. */
QString accountsErrorText(const QString &dbusError, const QString &message);

/* accountsservice, asynchronously: an administrator's password may be
 * asked for while a call waits. (No change signals: the panel reads the
 * list again after its own changes and whenever it comes forward.) */
class AccountsClient : public QObject {
public:
	explicit AccountsClient(QObject *parent = nullptr);

	using Done = std::function<void(const QString &error)>;
	void list(std::function<void(const QList<Account> &, const QString &error)> done);
	void create(const QString &userName, const QString &realName, bool admin,
		std::function<void(const QString &path, const QString &error)> done);
	void remove(qint64 uid, bool removeFiles, Done done);
	/* A method of org.freedesktop.Accounts.User on `path`. */
	void callUser(const QString &path, const QString &method, const QVariantList &args, Done done);
};

class AccountWindow;

class UsersPanel : public QWidget {
public:
	UsersPanel();
	~UsersPanel() override;

	/* For tests: the rows, choosing one, and what the buttons do. */
	QStringList rows() const;
	bool loaded() const;
	void select(int row);
	AccountWindow *newUser();
	AccountWindow *openSelected();
	/* Delete…: asks first, unless `ask` is false (tests). */
	void deleteSelected(bool removeFiles, bool ask = true);
	QString lastError() const;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void changeEvent(QEvent *) override;

private:
	friend class AccountWindow;
	struct Parts;
	std::unique_ptr<Parts> d;
	void reload();
	void fill();
	void updateButtons();
	void showError(const QString &error);
	const Account *selected() const;
};

/* One account, opened (or a new one): its names, picture, password and
 * what it may do. Save applies what changed. */
class AccountWindow : public QWidget {
public:
	AccountWindow(UsersPanel *panel, AccountsClient *client, const Account *account);
	~AccountWindow() override;

	/* For tests, as typing or clicking would. */
	void setRealName(const QString &name);
	void setUserName(const QString &name);
	void setPassword(const QString &password, const QString &verify);
	void setAdmin(bool admin);
	void setCanLogIn(bool can);
	void setPicture(const QString &file);
	/* Save: false (with the reason) if it can't be saved as it is. */
	bool save(QString *problem = nullptr);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	struct Parts;
	std::unique_ptr<Parts> d;
	QString problem() const;
	void apply(const QString &path, QStringList steps);
};
