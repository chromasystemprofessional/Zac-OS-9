/* The Users panel (shell/panels/users.cpp) against a stand-in accountsservice
 * on the session bus (fake_accounts.py): it lists people but not system
 * accounts, you marked and first chosen; New User makes an account with a
 * hashed password; Open renames, locks and changes the kind; your own
 * administrator rights and account can't be taken away; a refusal
 * (no administrator's password) changes nothing; Delete removes.
 *
 * usage: test-users-panel FAKE_ACCOUNTS.PY  (under dbus-run-session) */
#include <QApplication>
#include <QDir>
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
	/* A user-installed application, a utility, a control panel, a ZacOS
	 * program and a baseline application of the ISO, so Programs… shows
	 * exactly the Applications folder whatever the build machine has. */
	const QString dataHome = dir.filePath("data-home");
	const QString dataSystem = dir.filePath("data-system");
	QDir().mkpath(dataHome + "/applications");
	QDir().mkpath(dataHome + "/zacos9");
	QDir().mkpath(dataSystem + "/applications");
	const QString binDir = dir.filePath("bin");
	QDir().mkpath(binDir);
	QFile helper(binDir + "/zacos9-test-program");
	helper.open(QIODevice::WriteOnly);
	helper.write("#!/bin/sh\nexit 0\n");
	helper.close();
	helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("PATH", (binDir + ':' + qgetenv("PATH")).toUtf8());
	QFile program(dataHome + "/applications/test-program.desktop");
	program.open(QIODevice::WriteOnly);
	program.write("[Desktop Entry]\nType=Application\nName=Test Program\n"
		"Exec=zacos9-test-program\nCategories=Graphics;\n");
	program.close();
	QFile utility(dataHome + "/applications/test-utility.desktop");
	utility.open(QIODevice::WriteOnly);
	utility.write("[Desktop Entry]\nType=Application\nName=Test Utility\nExec=/bin/false\n"
		"Categories=Utility;\n");
	utility.close();
	QFile panelEntry(dataHome + "/applications/test-panel.desktop");
	panelEntry.open(QIODevice::WriteOnly);
	panelEntry.write("[Desktop Entry]\nType=Application\nName=Test Panel\nExec=/bin/false\n"
		"Categories=Settings;X-ZacOS9-ControlPanel;\n");
	panelEntry.close();
	QFile builtin(dataHome + "/applications/zacos9-test-app.desktop");
	builtin.open(QIODevice::WriteOnly);
	builtin.write("[Desktop Entry]\nType=Application\nName=Test ZacOS App\n"
		"Exec=zacos9-test-program\nCategories=Graphics;\n");
	builtin.close();
	QFile baseline(dataSystem + "/applications/baseline.desktop");
	baseline.open(QIODevice::WriteOnly);
	baseline.write("[Desktop Entry]\nType=Application\nName=Baseline App\nExec=/bin/false\n"
		"Categories=Graphics;\n");
	baseline.close();
	QFile baseList(dataHome + "/zacos9/base-applications");
	baseList.open(QIODevice::WriteOnly);
	baseList.write("baseline.desktop\n");
	baseList.close();
	qputenv("XDG_DATA_HOME", dataHome.toUtf8());
	qputenv("XDG_DATA_DIRS", dataSystem.toUtf8());
	/* zacos9-accounts-helper from the sources, its settings here, and a
	 * pkexec that just runs it. */
	const QString conf = dir.filePath("etc");
	QDir().mkpath(conf);
	qputenv("ZACOS9_ACCOUNTS_CONF", conf.toUtf8());
	qputenv("ZACOS9_ACCOUNTS_HELPER", (QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath() +
		"/../../system/accounts/zacos9-accounts-helper").toUtf8());
	QFile pkexec(dir.filePath("pkexec"));
	pkexec.open(QIODevice::WriteOnly);
	pkexec.write("#!/bin/sh\nexec \"$@\"\n");
	pkexec.close();
	pkexec.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("PATH", (dir.path() + ':' + qgetenv("PATH")).toUtf8());
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

	/* Limited: only the chosen programs (malcontent's allowlist). */
	w = panel.openSelected();
	waitFor([] { return calls().contains("GetAppFilter bob"); }, 500); /* the filter read */
	w->setLimited(true, { "/usr/bin/gimp", "app/org.example.Paint/x86_64/stable" });
	check(w->save(), "Bob's programs save");
	check(waitFor([] { return calls().contains(
			"SetAppFilter bob True /usr/bin/gimp app/org.example.Paint/x86_64/stable"); }),
		"a Limited account is an allowlist of its programs");

	/* Programs… itself: the list, a checked program, and Save. */
	const int chosenMark = calls().size();
	w = panel.openSelected();
	check(waitFor([&] { return calls().mid(chosenMark).contains("GetAppFilter bob") &&
			w->limitedEnabled(); }), "Bob's list is read for Programs…");
	w->setLimited(true, {});
	ProgramsWindow *programs = w->openPrograms();
	check(programs && programs->names() == QStringList{ "Test Program" },
		"Programs… lists only Applications-folder programs: no utilities, "
		"control panels, ZacOS programs or baseline apps");
	programs->toggle(0);
	const QString chosen = programs->entries().value(0);
	programs->accept();
	const int saveMark = calls().size();
	check(w->save(), "a program chosen in Programs… saves");
	check(waitFor([&] { return calls().mid(saveMark).contains(
			"SetAppFilter bob True " + chosen); }),
		"the chosen program is the allowlist");

	const int readMark = calls().size();
	w = panel.openSelected();
	check(waitFor([&] { return calls().mid(readMark).contains("GetAppFilter bob") &&
			w->limitedEnabled(); }), "Bob's list is read back");
	w->setLimited(false, {});
	check(w->save(), "unlimiting saves");
	check(waitFor([] { return calls().contains("SetAppFilter bob False\n"); }),
		"not limited is an empty blocklist");

	/* The program limit is offered for every account, including an
	 * administrator's, as Front Desk Setup allows; it waits only for the
	 * old list to be read. */
	int mark = calls().size();
	w = panel.openSelected();
	check(!w->limitedEnabled(), "the program limit waits for the old list");
	check(waitFor([&] { return calls().mid(mark).contains("GetAppFilter bob") &&
			w->limitedEnabled(); }), "the old list is read");
	check(w->limitedEnabled(), "then the program limit can be chosen");
	mark = calls().size();
	w->setAdmin(true);
	w->setLimited(true, { "/usr/bin/gimp" });
	check(w->programsEnabled(), "Programs… is usable for an administrator too");
	check(w->save(), "an administrator's programs save");
	check(waitFor([&] { return calls().mid(mark).contains("SetAppFilter bob True /usr/bin/gimp"); }),
		"an administrator's allowlist is set");
	check(waitFor([&] { return rowOf(panel, "Robert Builder  –  Administrator") >= 0; }),
		"Bob is an administrator again");
	mark = calls().size();
	w = panel.openSelected();
	check(waitFor([&] { return calls().mid(mark).contains("GetAppFilter bob") &&
			w->limitedEnabled(); }), "the administrator's list is read back");
	w->setAdmin(false);
	w->setLimited(false, {});
	mark = calls().size();
	check(w->save(), "the administrator and the limit can be undone");
	check(waitFor([&] { return calls().mid(mark).contains("SetAccountType bob 0") &&
			calls().mid(mark).contains("SetAppFilter bob False\n"); }),
		"both changes are sent");
	check(waitFor([&] { return rowOf(panel, "Robert Builder  –  Standard") >= 0; }),
		"Bob is a standard account again");

	/* Your own account, too: an administrator's password still guards the
	 * change through polkit. */
	panel.select(rowOf(panel, "You"));
	mark = calls().size();
	AccountWindow *self = panel.openSelected();
	check(waitFor([&] { return calls().mid(mark).contains("GetAppFilter you") &&
			self->limitedEnabled(); }), "your own list is read");
	check(self->limitedEnabled() && !self->programsEnabled(),
		"your own programs can be chosen once the limit is on");
	self->setLimited(true, { "/usr/bin/gimp" });
	check(self->programsEnabled(), "your own Programs… is usable");
	mark = calls().size();
	check(self->save(), "your own program limit saves");
	check(waitFor([&] { return calls().mid(mark).contains("SetAppFilter you True /usr/bin/gimp"); }),
		"your own allowlist is set");
	mark = calls().size();
	self = panel.openSelected();
	check(waitFor([&] { return calls().mid(mark).contains("GetAppFilter you") &&
			self->limitedEnabled(); }), "your own list is read back");
	self->setLimited(false, {});
	mark = calls().size();
	check(self->save(), "your own program limit can be undone");
	check(waitFor([&] { return calls().mid(mark).contains("SetAppFilter you False\n"); }),
		"your own filter is cleared");

	/* Puppy Mode, set by an administrator (zacos9-accounts-helper). */
	panel.select(rowOf(panel, "Robert"));
	w = panel.openSelected();
	w->setPuppy(true);
	check(w->save(), "Bob's Puppy Mode saves");
	check(waitFor([&] { return QFile::exists(conf + "/puppy/bob"); }), "Puppy Mode is kept for Bob");
	w = panel.openSelected();
	w->setPuppy(false);
	w->save();
	check(waitFor([&] { return !QFile::exists(conf + "/puppy/bob"); }), "and taken away");

	/* Options: the welcome message. */
	OptionsWindow *options = panel.openOptions();
	options->setWelcome("Hello,\n   family");
	check(options->save(), "the welcome message saves");
	QFile welcome(conf + "/welcome");
	check(welcome.open(QIODevice::ReadOnly) && welcome.readAll() == "Hello, family\n",
		"the login window's welcome message is kept, on one line");
	options->close();

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
