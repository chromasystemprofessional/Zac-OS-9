#include "diskinit.h"

#include <QApplication>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>
#include <QCloseEvent>

#include "alert.h"
#include "localvolumes.h"
#include "pixels.h"
#include "platinumshell.h"

#ifndef ZACOS9_DISK_HELPER
#define ZACOS9_DISK_HELPER "/usr/libexec/zacos9/zacos9-disk-helper"
#endif

bool parseInitializationDisks(const QByteArray &data,
		std::vector<InitializationDisk> *disks, QString *error) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
	const QJsonObject root = document.object();
	if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
			root.value("version").toInt() != 1 || !root.value("disks").isArray()) {
		*error = "Invalid disk helper response: " + parseError.errorString();
		return false;
	}
	std::vector<InitializationDisk> result;
	QSet<QString> devices;
	const QRegularExpression devicePattern("\\A/dev/[A-Za-z0-9_-]+\\z");
	for (const QJsonValue &value : root.value("disks").toArray()) {
		const QJsonObject object = value.toObject();
		InitializationDisk disk;
		disk.device = object.value("device").toString();
		disk.identity = object.value("identity").toString();
		disk.name = object.value("name").toString();
		const double size = object.value("size").toDouble();
		if (!value.isObject() || !devicePattern.match(disk.device).hasMatch() ||
				devices.contains(disk.device) ||
				(object.value("eligible").toBool() && disk.identity.isEmpty()) ||
				disk.identity.size() > 1024 || disk.name.isEmpty() || size < 0 ||
				size > 9007199254740991.0 || size != static_cast<quint64>(size) ||
				!object.value("unreadable").isBool() || !object.value("eligible").isBool() ||
				!object.value("children").isArray()) {
			*error = "Invalid disk identity or properties in disk helper response.";
			return false;
		}
		disk.size = static_cast<quint64>(size);
		disk.unreadable = object.value("unreadable").toBool();
		disk.eligible = object.value("eligible").toBool();
		for (const QJsonValue &child : object.value("children").toArray()) {
			if (!child.isString() || !devicePattern.match(child.toString()).hasMatch()) {
				*error = "Invalid disk child in disk helper response.";
				return false;
			}
			disk.children << child.toString();
		}
		devices.insert(disk.device);
		result.push_back(std::move(disk));
	}
	*disks = std::move(result);
	return true;
}

DiskInitialization::DiskInitialization(Interface interface, QString helper,
		QString authorizer, QString ejector, QObject *parent)
	: QObject(parent), m_interface(std::move(interface)), m_helper(std::move(helper)),
	m_authorizer(std::move(authorizer)), m_ejector(std::move(ejector)) {
	m_scanTimer.setInterval(3000);
	connect(&m_scanTimer, &QTimer::timeout, this, &DiskInitialization::scan);
}

void DiskInitialization::start() {
	m_scanTimer.start();
	QTimer::singleShot(3000, this, &DiskInitialization::scan);
}

void DiskInitialization::mountFailed(const QString &device) {
	m_failed.insert(device);
	QTimer::singleShot(500, this, &DiskInitialization::scan);
}

void DiskInitialization::report(const QString &message) {
	qWarning().noquote() << "zacos9-finder:" << message;
	m_interface.error(message);
}

void DiskInitialization::run(const QString &program, const QStringList &args, int timeout,
		std::function<void(const QByteArray &, const QString &)> finished) {
	auto *process = new QProcess(this);
	auto *timer = new QTimer(process);
	timer->setSingleShot(true);
	connect(timer, &QTimer::timeout, process, [process] {
		process->setProperty("timedOut", true);
		process->kill();
	});
	connect(process, &QProcess::errorOccurred, this,
		[process, finished](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				finished({}, "Could not start disk operation: " + process->errorString());
				process->deleteLater();
			}
		});
	connect(process, &QProcess::finished, this,
		[process, finished](int code, QProcess::ExitStatus status) {
			QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
			if (process->property("timedOut").toBool()) {
				error = "The disk operation timed out. The disk may be partially initialized.";
			} else if (status != QProcess::NormalExit || code != 0) {
				if (error.isEmpty()) {
					error = "The disk operation failed (exit " + QString::number(code) + ").";
				}
			} else {
				error.clear();
			}
			finished(process->readAllStandardOutput(), error);
			process->deleteLater();
		});
	process->start(program, args);
	timer->start(timeout);
}

void DiskInitialization::scan() {
	if (m_scanning || m_presenting || m_operating) {
		return;
	}
	m_scanning = true;
	run(m_helper, { "list" }, 15000, [this](const QByteArray &out, const QString &failure) {
		m_scanning = false;
		QString error = failure;
		std::vector<InitializationDisk> disks;
		if (error.isEmpty()) {
			parseInitializationDisks(out, &disks, &error);
		}
		if (!error.isEmpty()) {
			if (error != m_lastScanError) {
				qWarning().noquote() << "zacos9-finder: Could not check inserted USB disks:" << error;
			}
			m_lastScanError = error;
			return;
		}
		m_lastScanError.clear();
		acceptDisks(disks);
	});
}

void DiskInitialization::acceptDisks(const std::vector<InitializationDisk> &disks) {
	QSet<QString> present, devices;
	for (const InitializationDisk &disk : disks) {
		if (!disk.identity.isEmpty()) {
			present.insert(disk.identity);
		}
		devices.insert(disk.device);
		for (const QString &child : disk.children) {
			devices.insert(child);
		}
	}
	m_seen.intersect(present);
	m_failed.intersect(devices);
	for (const InitializationDisk &disk : disks) {
		bool failed = m_failed.contains(disk.device);
		for (const QString &child : disk.children) {
			failed = failed || m_failed.contains(child);
		}
		if (!disk.eligible || (!disk.unreadable && !failed) || m_seen.contains(disk.identity)) {
			continue;
		}
		m_seen.insert(disk.identity);
		m_presenting = true;
		const Choice choice = m_interface.choose(disk);
		bool confirmed = choice != Initialize || m_interface.confirm(disk);
		m_presenting = false;
		if (choice != Ignore && confirmed) {
			revalidate(disk, choice);
			return;
		}
	}
}

void DiskInitialization::revalidate(const InitializationDisk &disk, Choice choice) {
	m_operating = true;
	run(m_helper, { "list" }, 15000,
		[this, disk, choice](const QByteArray &out, const QString &failure) {
			QString error = failure;
			std::vector<InitializationDisk> disks;
			if (error.isEmpty()) {
				parseInitializationDisks(out, &disks, &error);
			}
			for (const InitializationDisk &current : disks) {
				if (current.device == disk.device && current.identity == disk.identity && current.eligible) {
					operate(disk, choice);
					return;
				}
			}
			m_operating = false;
			report(error.isEmpty() ? "The disk was removed, changed, or is now in use. Nothing was erased." : error);
		});
}

void DiskInitialization::operate(const InitializationDisk &disk, Choice choice) {
	m_interface.busy(true);
	const QString program = choice == Initialize ? m_authorizer : m_ejector;
	const QStringList args = choice == Initialize
		? QStringList{ m_helper, "initialize", disk.device, disk.identity }
		: QStringList{ "power-off", "--block-device", disk.device };
	run(program, args, 300000,
		[this, choice, disk](const QByteArray &out, const QString &failure) {
			m_interface.busy(false);
			m_operating = false;
			QString error = failure;
			if (error.isEmpty() && choice == Initialize) {
				const QJsonDocument response = QJsonDocument::fromJson(out);
				const QJsonObject object = response.object();
				if (!response.isObject() || object.value("version").toInt() != 1 ||
						object.value("device").toString() != disk.device ||
						object.value("initialized") != QJsonValue(true)) {
					error = "The disk helper did not confirm successful initialization. Check the disk before using it.";
				}
			}
			if (!error.isEmpty()) {
				report(error);
			} else if (choice == Initialize) {
				m_failed.clear();
				m_interface.initialized(disk.device);
			}
		});
}

namespace {
class DiskBusy : public QDialog {
public:
	DiskBusy() {
		setWindowTitle("Disk Initialization");
		setFixedSize(390, 90);
		setWindowModality(Qt::ApplicationModal);
	}
protected:
	void reject() override {}
	void closeEvent(QCloseEvent *event) override;
	void showEvent(QShowEvent *event) override {
		QDialog::showEvent(event);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}
	void paintEvent(QPaintEvent *) override {
		Pixels pixels(width(), height());
		pl_fill(&pixels.c, 0, 0, width() - 1, height() - 1, GRAY(0xD));
		Text message("Working on the disk. Please do not remove it.", 370, PL_FONT_SYSTEM);
		pl_text(&pixels.c, message.t, 10, 35, C_BLACK);
		QPainter painter(this);
		pixels.blit(painter);
	}
};
}

void DiskBusy::closeEvent(QCloseEvent *event) {
	event->ignore();
}

void diskInitializationStart() {
	auto *busy = new DiskBusy;
	busy->setParent(nullptr);
	DiskInitialization::Interface interface;
	interface.choose = [](const InitializationDisk &disk) {
		const QString size = QString::number(disk.size / 1000000000.0, 'f', 1) + " GB";
		const auto choice = Alert::choose(
			"This USB disk cannot be read: " + disk.name.left(80) + " (" + size + ", " + disk.device +
			"). Initialize will erase the entire disk and format it as FAT32.",
			"Eject", "Ignore", "Initialize");
		return choice == Alert::Ok ? DiskInitialization::Eject :
			choice == Alert::Other ? DiskInitialization::Initialize : DiskInitialization::Ignore;
	};
	interface.confirm = [](const InitializationDisk &disk) {
		return Alert::choose("Erase ALL files and partitions on " + disk.name.left(80) +
			" (" + disk.device + ")? This cannot be undone. The disk will become Untitled (FAT32).",
			"Cancel", QString(), "Erase") == Alert::Other;
	};
	interface.error = [](const QString &error) { Alert::ask(error, "OK", QString()); };
	interface.busy = [busy](bool active) { active ? busy->show() : busy->hide(); };
	interface.initialized = [](const QString &device) {
		localVolumeMountDevice(device);
		QTimer::singleShot(1500, qApp, [device] { localVolumeMountDevice(device); });
		Alert::ask("The disk has been initialized as Untitled (FAT32). It will appear on the desktop when mounted.",
			"OK", QString());
	};
	auto *controller = new DiskInitialization(std::move(interface),
		ZACOS9_DISK_HELPER, "pkexec", "udisksctl", qApp);
	localVolumesOnMountFailed([controller](const QString &device) { controller->mountFailed(device); });
	controller->start();
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, busy, &QObject::deleteLater);
}
