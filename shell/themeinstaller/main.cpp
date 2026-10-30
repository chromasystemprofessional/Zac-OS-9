/* zacos9-themeinstaller: build a ZacOS theme from a Mac OS 9 install.
 *
 * Runs tools/extract-macos9-theme.py over the user's own Mac OS 9 tree
 * (default ~/Classic, where Old Dog keeps it) and installs the resulting
 * theme bundle into ~/.config/zacos9/themes/, then makes it the appearance
 * theme. Nothing from Apple ships: it only reads the user's own copy.
 */

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <cstdio>

#include "programs.h"
#include "settings.h"
#include "theme.h"

static QString extractorPath() {
	/* From a build tree, the source tree's copy; installed, under data dir. */
	const QString src = sourceTreePath("tools/extract-macos9-theme.py");
	if (!src.isEmpty() && QFileInfo(src).isFile()) {
		return src;
	}
	return QString::fromUtf8(pl_data_dir()) + "/tools/extract-macos9-theme.py";
}

static QString outputDir(const QString &name) {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes/" + name;
}

/* Run the extractor and wait with processEvents() (never waitForFinished). */
static bool runExtractor(const QString &source, const QString &name, QString *error) {
	const QString tool = extractorPath();
	if (!QFileInfo(tool).isFile()) {
		*error = "The theme extractor couldn't be found.";
		return false;
	}
	QProcess process;
	process.setProgram("python3");
	process.setArguments({ tool, "--source", source, "--output", outputDir(name), "--name", name,
		"--auto-colours" });
	process.start();
	/* Qt/Wayland deadlocks on waitForFinished(): pump events until it exits. */
	QEventLoop loop;
	QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
	QObject::connect(&process, &QProcess::errorOccurred, [&](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			*error = "Couldn't start python3.";
			loop.quit();
		}
	});
	loop.exec();
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		*error = QString::fromUtf8(process.readAllStandardError()).trimmed();
		if (error->isEmpty()) {
			*error = "The theme extractor failed.";
		}
		return false;
	}
	return true;
}

/* The Appearance control panel lists JSON presets from this folder. */
static QString appearanceThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Themes";
}

/* Read a key=value from the extracted theme.conf ("" when unset). */
static QString themeConfValue(const QString &name, const QString &key) {
	QFile f(outputDir(name) + "/theme.conf");
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return QString();
	}
	QString value;
	QTextStream in(&f);
	QString line;
	while (in.readLineInto(&line)) {
		const int eq = line.indexOf('=');
		if (eq > 0 && line.left(eq).trimmed() == key) {
			value = line.mid(eq + 1).trimmed();
		}
	}
	return value;
}

/* Write the appearance preset JSON that the Appearance panel's Themes list
 * shows, so the extracted theme appears there and applies its settings. */
static QString writeAppearancePreset(const QString &name, QString *error) {
	const QString folder = appearanceThemesFolder();
	if (!QDir().mkpath(folder)) {
		*error = "Couldn't create the appearance Themes folder.";
		return QString();
	}
	const QString filename = QString::fromLatin1(QCryptographicHash::hash(
		name.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)) + ".json";
	const QString path = folder + "/" + filename;
	QJsonObject settings;
	auto put = [&](const char *key, const QString &value) {
		if (!value.isEmpty()) {
			settings.insert(key, value);
		}
	};
	put("accent", themeConfValue(name, "accent"));
	put("highlight", themeConfValue(name, "highlight"));
	put("pattern", themeConfValue(name, "pattern"));
	put("theme-bundle", name);
	QSaveFile output(path);
	const QByteArray bytes = QJsonDocument(QJsonObject{
		{ "version", 1 }, { "name", name }, { "settings", settings } }).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
		*error = output.errorString();
		return QString();
	}
	return "theme:" + QString::fromLatin1(QCryptographicHash::hash(
		filename.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Theme Installer");
	QGuiApplication::setDesktopFileName("zacos9-themeinstaller");

	/* Non-interactive (and testable): --source DIR --name NAME. */
	QString source, name;
	bool batch = false;
	QStringList args;
	for (int i = 1; i < argc; i++) {
		const QString a = QString::fromLocal8Bit(argv[i]);
		if (a == "--source" && i + 1 < argc) { source = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (a == "--name" && i + 1 < argc) { name = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (!a.startsWith("--")) { source = a; }
	}

	if (source.isEmpty()) {
		source = QDir::homePath() + "/Classic";
	}
	if (!QFileInfo::exists(source) || !QFileInfo(source).isDir()) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: no such Mac OS 9 folder: %s\n", qPrintable(source));
			return 2;
		}
		QMessageBox::information(nullptr, "Theme Installer",
			"Set up Old Dog first: put your Mac OS 9 install in your Classic folder.\n"
			"Then choose that folder.");
		source = QFileDialog::getExistingDirectory(nullptr, "Choose the Mac OS 9 folder",
			QDir::homePath());
		if (source.isEmpty()) {
			return 0;
		}
	}

	if (name.isEmpty()) {
		bool ok = false;
		name = QInputDialog::getText(nullptr, "Theme Installer",
			"Name for the theme:", QLineEdit::Normal, "Mac OS 9", &ok);
		if (!ok || name.trimmed().isEmpty()) {
			return 0;
		}
	}
	name = name.trimmed();

	QString error;
	if (!runExtractor(source, name, &error)) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(error));
			return 2;
		}
		QMessageBox::critical(nullptr, "Theme Installer", error);
		return 2;
	}
	const QString presetId = writeAppearancePreset(name, &error);
	if (presetId.isEmpty()) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(error));
			return 2;
		}
		QMessageBox::critical(nullptr, "Theme Installer", error);
		return 2;
	}
	pl_setting_set("appearance-theme", presetId.toUtf8().constData());
	pl_setting_set("theme-bundle", name.toUtf8().constData());
	zac_theme_apply();
	if (batch) {
		printf("installed %s\n", qPrintable(outputDir(name)));
		return 0;
	}
	QMessageBox::information(nullptr, "Theme Installer",
		"Theme \"" + name + "\" was installed from your Mac OS 9 folder.");
	return 0;
}