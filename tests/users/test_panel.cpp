/* The Users panel (shell/panels/users.cpp) against a stand-in accountsservice
 * on the session bus (fake_accounts.py): it lists people but not system
 * accounts, you marked and first chosen; New User makes an account with a
 * hashed password; Open renames, locks and changes the kind; your own
 * administrator rights and account can't be taken away; a refusal
 * (no administrator's password) changes nothing; Delete removes.
 *
 * usage: test-users-panel FAKE_ACCOUNTS.PY  (under dbus-run-session) */
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <crypt.h>
#include <functional>
#include <unistd.h>

#include "users.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static bool waitFor(const std::function<bool()> &done, int ms = 5000) {
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents();
		if (done()) {
			return true;
		}
		QThread::msleep(5);
	}
	return done();
}

static QString logPath;
static QString calls() {
	QFile f(logPath);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static int rowOf(const UsersPanel &panel, const QString &start) {
	const QStringList rows = panel.rows();
	for (int i = 0; i < rows.size(); ++i) {
		if (rows[i].startsWith(start)) {
			return i;
		}
	}
	return -1;
}

int main(int argc, char **argv) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	qputenv("ZACOS9_ACCOUNTS_BUS", "session");
	QApplication app(argc, argv);
	if (argc < 2) {
		return 2;
	}

	/* The names and the password, without accountsservice. */
	check(validShortName("ada") && validShortName("a-b_9") && !validShortName("Ada") &&
		!validShortName("9lives") && !validShortName("") && !validShortName(QString(33, 'a')),
		"short names: lowercase, starting with a letter, 32 at most");
	check(shortNameFor("Ada Lovelace") == "ada" && shortNameFor("Émile Zola") == "emile" &&
		shortNameFor("  2Pac Shakur") == "pac", "a short name is made from the first name");
	const QString hashed = hashPassword("secret");
	struct crypt_data data = {};
	check(hashed.startsWith('$') && hashed != hashPassword("secret") &&
		QString::fromLatin1(crypt_r("secret", hashed.toLatin1().constData(), &data)) == hashed,
		"passwords go hashed by crypt(3), salted afresh");
	check(accountsErrorText("org.freedesktop.Accounts.Error.PermissionDenied", "x").contains("administrator"),
		"a refusal is explained");

	QTemporaryDir dir;
	logPath = dir.filePath("calls.log");
	QProcess fake;
	fake.setProcessChannelMode(QProcess::ForwardedChannels);
	fake.start("python3", { argv[1], logPath });
	if (!waitFor([] { return calls().contains("ready"); }, 10000)) {
		check(false, "the stand-in accountsservice starts");
		return 1;
	}

	UsersPanel panel;
	panel.show();
	check(waitFor([&] { return panel.loaded(); }) && panel.lastError().isEmpty(), "the accounts load");
	check(panel.rows().size() == 2 && rowOf(panel, "Bob Builder  –  Standard") == 0 &&
		panel.rows().value(1) == "You Yourself  –  Administrator (you)",
		"people listed by name, kind shown, you marked, no system accounts");

	/* Yourself: not deleted, and your administrator rights stay. */
	panel.select(rowOf(panel, "You"));
	panel.deleteSelected(true, false);
	AccountWindow *own = panel.openSelected();
	own->setAdmin(false);
	own->setCanLogIn(false);
	check(own->save(), "your own account saves");
	waitFor([] { return false; }, 400);
	check(!calls().contains("DeleteUser") && !calls().contains("SetAccountType you") &&
		!calls().contains("SetLocked you"), "you can't delete, demote or lock yourself");

	/* New User: names, matching passwords, kind. */
	AccountWindow *w = panel.newUser();
	w->setRealName("Ada Lovelace");
	w->setPassword("one", "two");
	QString problem;
	check(!w->save(&problem) && problem.contains("aren't the same"), "different passwords are refused");
	w->setPassword("secret", "secret");
	w->setAdmin(true);
	check(w->save(&problem), "the new account saves");
	check(waitFor([&] { return rowOf(panel, "Ada Lovelace  –  Administrator") >= 0; }),
		"the new account is listed as an administrator");
	const QString log = calls();
	check(log.contains("CreateUser ada Ada Lovelace 1") && log.contains("SetPassword ada $"),
		"made with its short name from the name, then its password hashed");

	/* Open: rename, can't log in. */
	panel.select(rowOf(panel, "Bob"));
	w = panel.openSelected();
	w->setRealName("Robert Builder");
	w->setCanLogIn(false);
	check(w->save(), "Bob's changes save");
	check(waitFor([&] { return rowOf(panel, "Robert Builder  –  Standard, can't log in") >= 0; }),
		"renamed and locked");
	check(calls().contains("SetRealName bob Robert Builder") && calls().contains("SetLocked bob True") &&
		!calls().contains("SetPassword bob"), "only what changed is sent; a blank password keeps it");

	/* Refused (no administrator's password): nothing changes. */
	QFile deny(logPath + ".deny");
	deny.open(QIODevice::WriteOnly);
	deny.write("DeleteUser");
	deny.close();
	panel.select(rowOf(panel, "Robert"));
	panel.deleteSelected(false, false);
	check(waitFor([] { return calls().contains("DeleteUser"); }), "Delete asks accountsservice");
	waitFor([] { return false; }, 400);
	check(rowOf(panel, "Robert") >= 0, "a refused delete leaves the account");

	/* Delete Everything. */
	QFile::remove(logPath + ".deny");
	panel.select(rowOf(panel, "Robert"));
	panel.deleteSelected(true, false);
	check(waitFor([&] { return rowOf(panel, "Robert") < 0; }), "the account is deleted");
	check(calls().contains(QStringLiteral("DeleteUser %1 True").arg(getuid() + 1)),
		"with its home folder when asked");

	fake.kill(); /* never waitForFinished() here: see CLAUDE.md */
	waitFor([&] { return fake.state() == QProcess::NotRunning; }, 2000);
	return fails ? 1 : 0;
}
