#include "cryptofiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <utility>

namespace {

void transformFile(bool encrypt, const QString &source, const QString &destination,
		QString password, CryptoFileCompletion completion) {
	const QString gpg = QStandardPaths::findExecutable("gpg");
	const QString sourcePath = QFileInfo(source).canonicalFilePath();
	const QString destinationPath = QFileInfo(destination).absoluteFilePath();
	if (gpg.isEmpty()) {
		completion(false, "GPG is not installed.");
		return;
	}
	if (sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile() ||
			sourcePath == destinationPath) {
		completion(false, "Choose a readable file and a different destination.");
		return;
	}
	const QFileInfo destinationInfo(destinationPath);
	if (!destinationInfo.dir().exists() ||
			!QFileInfo(destinationInfo.dir().absolutePath()).isWritable()) {
		completion(false, "The destination folder is not writable.");
		return;
	}

	auto temporary = QSharedPointer<QTemporaryFile>::create(
		destinationInfo.dir().filePath(".zacos9-crypto-XXXXXX"));
	temporary->setAutoRemove(false);
	if (!temporary->open()) {
		completion(false, "A temporary output file could not be created.");
		return;
	}
	temporary->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	const QString temporaryPath = temporary->fileName();
	temporary->close();

	auto *process = new QProcess(QCoreApplication::instance());
	QByteArray encodedPassword = password.toUtf8();
	password.fill(QChar('\0'));
	auto passphrase = QSharedPointer<QByteArray>::create(std::move(encodedPassword));
	process->setProgram(gpg);
	if (encrypt) {
		process->setArguments({ "--batch", "--yes", "--pinentry-mode", "loopback",
			"--passphrase-fd", "0", "--symmetric", "--cipher-algo", "AES256",
			"--output", temporaryPath, sourcePath });
	} else {
		process->setArguments({ "--batch", "--yes", "--pinentry-mode", "loopback",
			"--passphrase-fd", "0", "--decrypt", "--output", temporaryPath, sourcePath });
	}

	auto finished = QSharedPointer<bool>::create(false);
	QObject::connect(process, &QProcess::started, process, [process, passphrase] {
		QByteArray input = *passphrase + '\n';
		passphrase->fill('\0');
		passphrase->clear();
		if (process->write(input) != input.size()) {
			input.fill('\0');
			process->kill();
			return;
		}
		input.fill('\0');
		process->closeWriteChannel();
	});
	QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		process, [process, encrypt, destinationPath, temporaryPath, completion, finished](
				int exitCode, QProcess::ExitStatus status) {
			if (*finished) {
				return;
			}
			*finished = true;
			bool ok = status == QProcess::NormalExit && exitCode == 0;
			QString problem;
			if (ok) {
				QFile input(temporaryPath);
				QSaveFile output(destinationPath);
				if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
					ok = false;
					problem = "The result could not be written to the chosen destination.";
				} else {
					output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
					QByteArray buffer(1024 * 1024, Qt::Uninitialized);
					while (!input.atEnd()) {
						const qint64 bytes = input.read(buffer.data(), buffer.size());
						if (bytes <= 0 || output.write(buffer.constData(), bytes) != bytes) {
							ok = false;
							problem = "The result could not be copied to the chosen destination.";
							break;
						}
					}
					if (ok && !output.commit()) {
						ok = false;
						problem = "The result could not be saved to the chosen destination.";
					}
					if (!ok) {
						output.cancelWriting();
					}
				}
			} else {
				problem = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
				if (problem.isEmpty()) {
					problem = encrypt ? "GPG could not encrypt this file."
						: "GPG could not decrypt this file. Check the passphrase and file.";
				}
			}
			QFile::remove(temporaryPath);
			completion(ok, problem);
			process->deleteLater();
		});
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, temporaryPath, completion, finished, passphrase](QProcess::ProcessError error) {
			if (error != QProcess::FailedToStart || *finished) {
				return;
			}
			*finished = true;
			passphrase->fill('\0');
			QFile::remove(temporaryPath);
			completion(false, "GPG could not be started.");
			process->deleteLater();
		});
	process->start();
}

} // namespace

void encryptFile(const QString &source, const QString &destination, const QString &password,
		CryptoFileCompletion completion) {
	transformFile(true, source, destination, QString(password), std::move(completion));
}

void decryptFile(const QString &source, const QString &destination, const QString &password,
		CryptoFileCompletion completion) {
	transformFile(false, source, destination, QString(password), std::move(completion));
}

/* ---- the keychain ------------------------------------------------------- */

static QString secretTool() {
	const QString env = qEnvironmentVariable("ZACOS9_SECRET_TOOL");
	return env.isEmpty() ? QStandardPaths::findExecutable("secret-tool") : env;
}

static QString keychainKey(const QString &file) {
	const QString canonical = QFileInfo(file).canonicalFilePath();
	return canonical.isEmpty() ? QFileInfo(file).absoluteFilePath() : canonical;
}

/* secret-tool, its answer handed to `done` (never waitForFinished: see
 * CLAUDE.md). */
static void runSecretTool(const QStringList &args, const QByteArray &input,
		std::function<void(bool, const QByteArray &)> done) {
	const QString tool = secretTool();
	if (tool.isEmpty()) {
		done(false, {});
		return;
	}
	auto *p = new QProcess;
	QObject::connect(p, &QProcess::finished, [p, done](int code, QProcess::ExitStatus status) {
		done(status == QProcess::NormalExit && code == 0, p->readAllStandardOutput());
		p->deleteLater();
	});
	QObject::connect(p, &QProcess::errorOccurred, [p, done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done(false, {});
			p->deleteLater();
		}
	});
	p->start(tool, args);
	p->write(input);
	p->closeWriteChannel();
}

void keychainStorePassphrase(const QString &file, const QString &password, std::function<void(bool)> done) {
	runSecretTool({ "store", "--label", "Encrypted file “" + QFileInfo(file).fileName() + "”",
			"zacos9-encrypted-file", keychainKey(file) }, password.toUtf8(),
		[done](bool ok, const QByteArray &) { done(ok); });
}

void keychainFindPassphrase(const QString &file, std::function<void(const QString &)> done) {
	runSecretTool({ "lookup", "zacos9-encrypted-file", keychainKey(file) }, QByteArray(),
		[done](bool ok, const QByteArray &out) {
			QByteArray secret = out;
			if (secret.endsWith('\n')) {
				secret.chop(1);
			}
			done(ok ? QString::fromUtf8(secret) : QString());
		});
}
