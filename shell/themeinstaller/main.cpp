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
#include <QRegularExpression>
#include <QStringList>
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

/* Whether the extraction produced a theme at all (menu icon written only
 * when the source's resource forks actually held icon resources). */
static bool bundleHasTheme(const QString &name) {
	return QFileInfo::exists(outputDir(name) + "/menu-icon.png");
}

/* Run the extractor and wait with processEvents() (never waitForFinished). */
static bool runExtractor(const QString &file, const QString &name, const QString &system,
		QString *error) {
	const QString tool = extractorPath();
	if (!QFileInfo(tool).isFile()) {
		*error = "The theme extractor couldn't be found.";
		return false;
	}
	QProcess process;
	process.setProgram("python3");
	QStringList args { tool, "--file", file, "--output", outputDir(name), "--name", name,
		"--auto-colours" };
	if (!system.isEmpty()) {
		args << "--system" << system;
	}
	process.setArguments(args);
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

/* The resource fork for a theme file: the file itself when it is one, else
 * the AppleDouble companion (._NAME) or the Netatalk .rsrc/NAME. */
static QString resolveResourceFork(const QString &file) {
	if (QFileInfo(file).size() >= 16) {
		QFile f(file);
		if (f.open(QIODevice::ReadOnly)) {
			const QByteArray head = f.read(4);
			if (head == QByteArray("\x00\x00\x01\x00", 4) ||
					head == QByteArray("\x00\x05\x16\x07", 4)) {
				return file;
			}
		}
	}
	const QFileInfo info(file);
	const QString dir = info.absolutePath();
	const QString base = info.fileName();
	for (const QString &candidate : {
			dir + "/._" + base,
			dir + "/.rsrc/" + base,
			QDir::homePath() + "/.rsrc/" + base,
			QDir::homePath() + "/._" + base }) {
		if (QFileInfo::exists(candidate)) {
			return candidate;
		}
	}
	return file;
}

/* The theme name from a file: the basename without extension or " copy". */
static QString nameFromFile(const QString &file) {
	QString base = QFileInfo(file).completeBaseName();
	base.replace(QRegularExpression(" copy$", QRegularExpression::CaseInsensitiveOption), "");
	return base.trimmed();
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
static QString writeAppearancePreset(const QString &name, const QString &wallpaper,
		const QString &pattern, const QString &alert, QString *error) {
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
	put("pattern", pattern);
	put("wallpaper", wallpaper);
	put("alert-sound", alert);
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

/* Copy the decoded wallpapers, pattern tiles and sounds into the Appearance
 * folders, and return the first of each by id ("" when none). */
static void installThemeAssets(const QString &name, QString *wallpaper,
		QString *pattern, QString *alert) {
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9";
	const struct { const char *sub; const char *dest; } folders[] = {
		{ "wallpapers", "appearance/Wallpaper" },
		{ "patterns", "appearance/Desktop Patterns" },
		{ "sounds", "sounds" },
	};
	QString prefix = name;
	prefix.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
	prefix = prefix.left(24);
	for (const auto &f : folders) {
		const QString src = outputDir(name) + "/" + f.sub;
		const QString dest = base + "/" + f.dest;
		QDir().mkpath(dest);
		const QFileInfoList files = QDir(src).entryInfoList(QDir::Files, QDir::Name);
		for (const QFileInfo &file : files) {
			const QString id = prefix + "-" + file.completeBaseName();
			QFile::copy(file.filePath(), dest + "/" + id + "." + file.suffix());
		}
		if (!files.isEmpty()) {
			const QString id = prefix + "-" + files.first().completeBaseName();
			if (f.sub == QStringLiteral("wallpapers")) *wallpaper = id;
			else if (f.sub == QStringLiteral("patterns")) *pattern = id;
			else *alert = id;
		}
	}
}

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Theme Installer");
	QGuiApplication::setDesktopFileName("zacos9-themeinstaller");

	/* Non-interactive (and testable): --file PATH [--name NAME]. */
	QString file, name, system;
	bool batch = false;
	for (int i = 1; i < argc; i++) {
		const QString a = QString::fromLocal8Bit(argv[i]);
		if (a == "--file" && i + 1 < argc) { file = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (a == "--name" && i + 1 < argc) { name = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (a == "--system" && i + 1 < argc) { system = QString::fromLocal8Bit(argv[++i]); }
		else if (!a.startsWith("--")) { file = a; }
	}

	if (file.isEmpty()) {
		QMessageBox::information(nullptr, "Theme Installer",
			"Choose the Mac OS 9 theme file you copied out of Mac OS 9.\n\n"
			"In Old Dog, copy the theme from Appearance ▸ Theme Files to your\n"
			"home folder, then pick it here. The theme's name comes from the file.");
		file = QFileDialog::getOpenFileName(nullptr, "Choose the Mac OS 9 theme file",
			QDir::homePath());
		if (file.isEmpty()) {
			return 0;
		}
	}
	if (!QFileInfo::exists(file)) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: no such theme file: %s\n", qPrintable(file));
			return 2;
		}
		QMessageBox::critical(nullptr, "Theme Installer",
			"That file doesn't exist. Copy the theme out of Mac OS 9 first.");
		return 1;
	}

	if (name.isEmpty()) {
		name = nameFromFile(file);
	}
	name = name.trimmed();
	const QString fork = resolveResourceFork(file);
	/* The System file supplies the busy, resize, move and hand pointers. */
	if (system.isEmpty()) {
		const QString home = QDir::homePath() + "/Classic/System/System";
		if (QFileInfo::exists(home)) {
			system = home;
		}
	}
	const QString systemFork = system.isEmpty() ? QString() : resolveResourceFork(system);

	QString error;
	if (!runExtractor(fork, name, systemFork, &error)) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(error));
			return 2;
		}
		QMessageBox::critical(nullptr, "Theme Installer", error);
		return 2;
	}
	if (!bundleHasTheme(name)) {
		/* The file held no resource fork — it was the empty data fork. Tell
		 * the user to copy the theme out of Mac OS 9 properly. */
		const QString message =
			"ZacOS found no Mac OS 9 artwork in that file.\n\n"
			"The file must be the theme's resource fork. Copy the theme from\n"
			"Appearance ▸ Theme Files in Mac OS 9 (Old Dog) to your home folder,\n"
			"which materialises its resource fork as a .rsrc/.finf companion.\n"
			"Then choose the file again.";
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: no Mac OS 9 artwork in %s\n", qPrintable(file));
			return 2;
		}
		QMessageBox::warning(nullptr, "Theme Installer", message);
		return 1;
	}
	QString wallpaper, pattern, alert;
	installThemeAssets(name, &wallpaper, &pattern, &alert);
	const QString presetId = writeAppearancePreset(name, wallpaper, pattern, alert, &error);
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