#include "cryptofiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static bool run(bool encrypt, const QString &source, const QString &destination,
		const QString &password, QString *problem) {
	QEventLoop loop;
	bool done = false;
	bool ok = false;
	auto callback = [&](bool success, const QString &error) {
		done = true;
		ok = success;
		*problem = error;
		loop.quit();
	};
	if (encrypt) {
		encryptFile(source, destination, password, callback);
	} else {
		decryptFile(source, destination, password, callback);
	}
	QTimer::singleShot(20000, &loop, &QEventLoop::quit);
	loop.exec();
	if (!done) {
		*problem = "encryption process timed out";
		return false;
	}
	return ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid()) {
		std::fprintf(stderr, "FAIL: create temporary test folder\n");
		return 1;
	}
	const QString gnupg = dir.path() + "/gnupg";
	QDir().mkpath(gnupg);
	QFile::setPermissions(gnupg, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("GNUPGHOME", QFile::encodeName(gnupg));
	const QString source = dir.path() + "/document.txt";
	const QString encrypted = dir.path() + "/document.txt.gpg";
	const QString restored = dir.path() + "/restored.txt";
	const QString wrong = dir.path() + "/wrong.txt";
	QFile input(source);
	if (!input.open(QIODevice::WriteOnly) || input.write("private test data\n") < 0) {
		std::fprintf(stderr, "FAIL: create source file\n");
		return 1;
	}
	input.close();

	QString problem;
	if (!run(true, source, encrypted, "correct horse battery staple", &problem)) {
		std::fprintf(stderr, "FAIL: encrypt: %s\n", qPrintable(problem));
		return 1;
	}
	QFile encryptedFile(encrypted);
	if (!encryptedFile.open(QIODevice::ReadOnly) ||
			encryptedFile.readAll() == QByteArray("private test data\n")) {
		std::fprintf(stderr, "FAIL: encrypted output is not distinct from plaintext\n");
		return 1;
	}
	if (QFileInfo(encrypted).permissions() &
			(QFileDevice::ReadGroup | QFileDevice::WriteGroup |
			 QFileDevice::ReadOther | QFileDevice::WriteOther)) {
		std::fprintf(stderr, "FAIL: encrypted file is accessible to other users\n");
		return 1;
	}
	if (!run(false, encrypted, restored, "correct horse battery staple", &problem)) {
		std::fprintf(stderr, "FAIL: decrypt: %s\n", qPrintable(problem));
		return 1;
	}
	QFile restoredFile(restored);
	if (!restoredFile.open(QIODevice::ReadOnly) ||
			restoredFile.readAll() != QByteArray("private test data\n")) {
		std::fprintf(stderr, "FAIL: round trip changed the plaintext\n");
		return 1;
	}
	if (run(false, encrypted, wrong, "not the right passphrase", &problem) ||
			QFileInfo::exists(wrong)) {
		std::fprintf(stderr, "FAIL: wrong passphrase succeeded or left an output file\n");
		return 1;
	}
	/* The keychain (a stand-in secret-tool keeping KEY<TAB>SECRET lines). */
	const QString tool = dir.path() + "/secret-tool";
	QFile script(tool);
	script.open(QIODevice::WriteOnly);
	script.write("#!/bin/sh\ndb=\"$(dirname \"$0\")/secrets\"\n"
		"case $1 in\n"
		"store) secret=$(cat); printf '%s\\t%s\\n' \"$5\" \"$secret\" >>\"$db\" ;;\n"
		"lookup) grep -F \"$3\t\" \"$db\" 2>/dev/null | tail -1 | cut -f2- ;"
		" grep -qF \"$3\t\" \"$db\" 2>/dev/null ;;\n"
		"esac\n");
	script.close();
	script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("ZACOS9_SECRET_TOOL", QFile::encodeName(tool));
	QString found = "unset";
	QEventLoop wait;
	keychainFindPassphrase(encrypted, [&](const QString &p) { found = p; wait.quit(); });
	wait.exec();
	if (!found.isEmpty()) {
		std::fprintf(stderr, "FAIL: a passphrase found before one was added\n");
		return 1;
	}
	bool stored = false;
	keychainStorePassphrase(encrypted, "correct horse battery", [&](bool ok) { stored = ok; wait.quit(); });
	wait.exec();
	keychainFindPassphrase(encrypted, [&](const QString &p) { found = p; wait.quit(); });
	wait.exec();
	if (!stored || found != "correct horse battery") {
		std::fprintf(stderr, "FAIL: keychain round trip gave \"%s\"\n", qPrintable(found));
		return 1;
	}
	std::puts("ok: authenticated GPG encryption, secure output permissions, round-trip, and wrong-passphrase rejection");
	std::puts("ok: an encrypted file's passphrase kept in, and found in, the keychain");
	return 0;
}
