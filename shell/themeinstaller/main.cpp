/* zacos9-themeinstaller: build a ZacOS theme from a Mac OS 9 install.
 *
 * Runs tools/extract-macos9-theme.py over the user's own Mac OS 9 tree
 * (default ~/Classic, where Old Dog keeps it) and installs the resulting
 * theme bundle into ~/.config/zacos9/themes/, then makes it the appearance
 * theme. Nothing from Apple ships: it only reads the user's own copy.
 */

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cstdio>

#include "programs.h"
#include "settings.h"
#include "theme.h"
#include "items.h"

static QString extractorPath() {
	/* From a build tree, the source tree's copy; installed, under data dir. */
	return themeExtractorPath();
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

/* A resource fork, not a System file's data fork: the Apple menu icon and
 * every other theme asset live in the fork. */
static bool isResourceFork(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray head = file.read(4);
	return head == QByteArray("\x00\x00\x01\x00", 4) ||
		head == QByteArray("\x00\x05\x16\x07", 4);
}

/* A folder holding a usable System resource fork in any copy layout: raw,
 * Netatalk `.rsrc`, AppleDouble, or the kernel HFS/Old Dog Unix volume's
 * NAME/rsrc. A data-only System copy is incomplete and needs its forks. */
static bool hasSystemFile(const QString &dir) {
	return isResourceFork(dir + "/System") ||
		QFileInfo(dir + "/.rsrc/System").isFile() ||
		QFileInfo(dir + "/._System").isFile() ||
		QFileInfo(dir + "/System/rsrc").isFile();
}

/* The Mac OS 9 System Folder the user copied out of Old Dog. Old Dog
 * shares the home folder as the Mac's "Unix" volume, so a copy dragged
 * onto it lands at ~/System Folder; the launcher keeps its Mac files in
 * ~/Classic, and older installs used ~/Classic/System. The Theme
 * Installer reads the System file, Desktop Pictures and Fonts from it. */
static QString macSystemFolder() {
	const QString home = QDir::homePath();
	const QString classic = qEnvironmentVariable("ZACOS9_CLASSIC_DIR");
	QStringList candidates { home + "/Classic/System Folder" };
	if (!classic.isEmpty()) {
		candidates << classic + "/System Folder";
	}
	candidates << home + "/System Folder" << home + "/Classic/System";
	/* A System file copied straight into Classic, or kept as its
	 * AppleDouble companion, is found as well. */
	candidates << home + "/Classic";
	if (!classic.isEmpty() && classic != home + "/Classic") {
		candidates << classic;
	}
	for (const QString &dir : candidates) {
		if (hasSystemFile(dir)) {
			return dir;
		}
	}
	return QString();
}

/* zacos9-mac-disks: the Finder's read-only Mac volume mounter (installed
 * in bindir, or its copy in the tree this program was built from). */
static QString macDisksTool() {
	const QString installed = QStandardPaths::findExecutable("zacos9-mac-disks");
	if (!installed.isEmpty()) {
		return installed;
	}
	for (const QString &candidate : { sourceTreePath("shell/finder/zacos9-mac-disks"),
			buildTreePath("shell/finder/zacos9-mac-disks") }) {
		if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
			return candidate;
		}
	}
	return QString();
}

/* Run a program to completion with processEvents() (never waitForFinished). */
static bool runTool(const QString &program, const QStringList &arguments,
		QByteArray *output, QString *error) {
	QProcess process;
	process.setProgram(program);
	process.setArguments(arguments);
	process.start();
	QEventLoop loop;
	QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
	QObject::connect(&process, &QProcess::errorOccurred, [&](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			*error = "Couldn't start " + QFileInfo(program).fileName() + ".";
			loop.quit();
		}
	});
	loop.exec();
	if (output) {
		*output = process.readAllStandardOutput();
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		const QString why = QString::fromUtf8(process.readAllStandardError()).trimmed();
		if (error->isEmpty()) {
			*error = why.isEmpty() ? "The Mac disk tool failed." : why;
		}
		return false;
	}
	return true;
}

/* Old Dog's startup disk: classic.conf's startup-disk entry, else the
 * first Mac HFS/HFS+ image in the Classic folder. */
static QString oldDogImage() {
	const QString conf = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/classic.conf";
	QFile file(conf);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&file);
		QString line;
		while (in.readLineInto(&line)) {
			if (line.startsWith("startup-disk=")) {
				const QString path = line.mid(13).trimmed();
				if (QFileInfo(path).isFile()) {
					return path;
				}
			}
		}
	}
	QString dir = qEnvironmentVariable("ZACOS9_CLASSIC_DIR");
	if (dir.isEmpty()) {
		dir = QDir::homePath() + "/Classic";
	}
	QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
	for (int seen = 0; it.hasNext() && seen < 200; seen++) {
		const QString path = it.next();
		QFile image(path);
		if (!image.open(QIODevice::ReadOnly)) {
			continue;
		}
		const QByteArray head = image.read(1026);
		if (head.size() < 1026) {
			continue;
		}
		/* HFS at 1024 ('BD'), HFS+ ('H+'), HFSX ('HX'), or a partition
		 * map's driver descriptor ('ER' in block zero). */
		const QByteArray signature = head.mid(1024, 2);
		if (signature == "BD" || signature == "H+" || signature == "HX" ||
				head.left(2) == "ER") {
			return path;
		}
	}
	return QString();
}

/* Copy a Mac folder tree, keeping each file's resource fork as the
 * .rsrc/NAME companion the extractor reads. The kernel's hfs driver puts
 * a file's fork at NAME/rsrc; a SheepShaver extfs copy keeps .rsrc/. */
static bool copyMacTree(const QString &source, const QString &destination, QString *error) {
	if (!QDir().mkpath(destination)) {
		*error = "Couldn't create " + destination + ".";
		return false;
	}
	const QFileInfoList entries = QDir(source).entryInfoList(
		QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const QFileInfo &entry : entries) {
		const QString name = entry.fileName();
		if (name == ".rsrc" || name == ".finf" || name == ".DS_Store") {
			continue;
		}
		const QString target = destination + "/" + name;
		if (entry.isDir()) {
			if (!copyMacTree(entry.filePath(), target, error)) {
				return false;
			}
			continue;
		}
		QFile::remove(target);
		if (!QFile::copy(entry.filePath(), target)) {
			*error = "Couldn't copy " + name + " out of the Mac disk.";
			return false;
		}
	}
	/* Resource forks beside the data fork (the kernel's hfs driver). */
	const QFileInfoList files = QDir(source).entryInfoList(
		QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
	for (const QFileInfo &file : files) {
		const QString fork = file.filePath() + "/rsrc";
		if (!QFileInfo(fork).isFile()) {
			continue;
		}
		QDir().mkpath(destination + "/.rsrc");
		const QString target = destination + "/.rsrc/" + file.fileName();
		QFile::remove(target);
		if (!QFile::copy(fork, target)) {
			*error = "Couldn't copy " + file.fileName() + "'s resource fork.";
			return false;
		}
	}
	/* Or in a .rsrc folder (a copy made through Old Dog's Unix volume). */
	const QDir forks(source + "/.rsrc");
	if (forks.exists()) {
		QDir().mkpath(destination + "/.rsrc");
		for (const QFileInfo &fork : forks.entryInfoList(QDir::Files | QDir::Hidden, QDir::Name)) {
			const QString target = destination + "/.rsrc/" + fork.fileName();
			QFile::remove(target);
			if (!QFile::copy(fork.filePath(), target)) {
				*error = "Couldn't copy " + fork.fileName() + "'s resource fork.";
				return false;
			}
		}
	}
	return true;
}

/* Copy Old Dog's System Folder into ~/Classic, where the Theme Installer
 * looks for it. The image is read directly first, because the kernel's HFS
 * driver drops resource forks and the System file's Apple menu icon lives
 * only in its resource fork. A mounted copy is the fallback for images the
 * small reader cannot handle. */
static bool importSystemFolder(QString *copiedTo, QString *error, bool replaceExisting = true) {
	const QString destination = QDir::homePath() + "/Classic/System Folder";
	const QString image = oldDogImage();
	if (image.isEmpty()) {
		*error = "Old Dog's startup disk wasn't found. Choose it in the "
			"Startup Disk control panel first.";
		return false;
	}
	QByteArray ignored;
	if (QFileInfo::exists(destination)) {
		/* An older copy has data forks only: add or repair every resource
		 * fork in place, keeping the user's data. */
		QString repairError;
		if (runTool("python3", { macHfsToolPath(), "--image", image,
				"--source", "System Folder", "--destination", destination,
				"--merge" }, &ignored, &repairError)) {
			*copiedTo = destination;
			return true;
		}
		if (!replaceExisting) {
			*error = destination + " already exists but has no readable System file; "
				"leaving it alone.";
			return false;
		}
	}
	/* Build the new copy beside the old one, so a failure cannot leave a
	 * half-copied System Folder where ZacOS will read it. */
	const QString staging = destination + ".new";
	QDir(staging).removeRecursively();
	QString readerError;
	if (runTool("python3", { macHfsToolPath(), "--image", image,
			"--source", "System Folder", "--destination", staging },
			&ignored, &readerError)) {
		QDir(destination).removeRecursively();
		if (QDir().rename(staging, destination)) {
			*copiedTo = destination;
			return true;
		}
		*error = "Couldn't move the copied System Folder into place.";
		QDir(staging).removeRecursively();
		return false;
	}
	QDir(staging).removeRecursively();

	const QString tool = macDisksTool();
	if (tool.isEmpty()) {
		*error = "The Mac disk tool (zacos9-mac-disks) isn't installed.";
		return false;
	}
	QByteArray output;
	if (!runTool(tool, { "mount", image }, &output, error)) {
		return false;
	}
	const QJsonObject mounted = QJsonDocument::fromJson(output).object();
	const QJsonArray paths = mounted.value("paths").toArray();
	const QJsonArray devices = mounted.value("devices").toArray();
	const auto release = [&] {
		for (const QJsonValue &device : devices) {
			QByteArray ignored;
			QString why;
			runTool(tool, { "unmount", device.toString() }, &ignored, &why);
		}
	};
	QString folder;
	for (const QJsonValue &path : paths) {
		const QFileInfoList entries = QDir(path.toString()).entryInfoList(
			QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo &entry : entries) {
			if (entry.fileName().compare("System Folder", Qt::CaseInsensitive) == 0) {
				folder = entry.filePath();
				break;
			}
		}
		if (!folder.isEmpty()) {
			break;
		}
	}
	if (folder.isEmpty()) {
		*error = "The Mac disk has no System Folder on it.";
		release();
		return false;
	}
	bool ok = copyMacTree(folder, staging, error);
	if (ok) {
		QDir(destination).removeRecursively();
		ok = QDir().rename(staging, destination);
		if (!ok) {
			*error = "Couldn't move the copied System Folder into place.";
		}
	}
	if (!ok) {
		QDir(staging).removeRecursively();
	}
	release();
	if (ok) {
		*copiedTo = destination;
	}
	return ok;
}

/* Run the extractor and wait with processEvents() (never waitForFinished). */
static bool runExtractor(const QString &file, const QString &name, const QString &systemFile,
		const QString &systemFolder, QString *error) {
	const QString tool = extractorPath();
	if (!QFileInfo(tool).isFile()) {
		*error = "The theme extractor couldn't be found.";
		return false;
	}
	QProcess process;
	process.setProgram("python3");
	QStringList args { tool, "--file", file, "--output", outputDir(name), "--name", name,
		"--auto-colours" };
	if (!systemFile.isEmpty()) {
		args << "--system" << systemFile;
	}
	if (!systemFolder.isEmpty()) {
		/* Everything the theme takes from the user's own Mac OS 9
		 * install: the Apple menu icon, the busy pointers, the alert
		 * sounds, the desktop pictures and the fonts. */
		const QString pictures = systemFolder + "/Appearance/Desktop Pictures";
		if (QDir(pictures).exists()) {
			args << "--pictures" << pictures;
		}
		const QString fonts = systemFolder + "/Fonts";
		if (QDir(fonts).exists()) {
			args << "--fonts" << fonts;
		}
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

/* The theme name from a file: the basename without AppleDouble prefix,
 * known file extension, or " copy" (preserving version numbers like 1.0.2). */
static QString nameFromFile(const QString &file) {
	QString base = QFileInfo(file).fileName();
	if (base.startsWith("._")) {
		base = base.mid(2);
	}
	base.replace(QRegularExpression("\\.(rsrc|bin|theme)$",
		QRegularExpression::CaseInsensitiveOption), "");
	base.replace(QRegularExpression(" copy$", QRegularExpression::CaseInsensitiveOption), "");
	return base.trimmed();
}

/* The Appearance control panel lists JSON presets from this folder. */
static QString appearanceThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Theme Files";
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
 * shows, so the extracted theme appears there and applies its settings.
 * It lives beside the source file when that sits in Theme Files (each
 * theme keeps its paperwork in its own folder); a file picked from
 * elsewhere lands its preset in Theme Files itself. */
static QString writeAppearancePreset(const QString &file, const QString &name, const QString &wallpaper,
		const QString &pattern, const QString &alert, const QString &fontFamily,
		const QString &soundTheme, QString *error) {
	QString folder = QFileInfo(file).absolutePath();
	if (!folder.startsWith(appearanceThemesFolder())) {
		folder = appearanceThemesFolder();
	}
	if (!QDir().mkpath(folder)) {
		*error = "Couldn't create the appearance Theme Files folder.";
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
	/* Always, even empty: applying the preset has to drop a previous
	 * theme's wallpaper and pattern instead of leaving them underneath. */
	settings.insert("pattern", pattern);
	settings.insert("wallpaper", wallpaper);
	settings.insert("background", !wallpaper.isEmpty() ? "wallpaper" : "pattern");
	settings.insert("alert-sound", alert);
	put("font-family", fontFamily);
	put("sound-theme", soundTheme);
	put("theme-bundle", name);
	QSaveFile output(path);
	const QByteArray bytes = QJsonDocument(QJsonObject{
		{ "version", 1 }, { "name", name }, { "settings", settings } }).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
		*error = output.errorString();
		return QString();
	}
	/* The id the Appearance panel lists it under: the path relative to
	 * Theme Files, so same-named presets in different folders stay
	 * distinct (a root preset's id is unchanged). */
	const QString rel = QDir(appearanceThemesFolder()).relativeFilePath(path);
	return "theme:" + QString::fromLatin1(QCryptographicHash::hash(
		rel.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

/* Copy the decoded TrueType faces into the user's fonts and register
 * them, returning the theme's preferred program font: its system font
 * when decoded, else its views font (""). */
static QString installThemeFonts(const QString &name) {
	const QString src = outputDir(name) + "/fonts";
	const QFileInfoList files = QDir(src).entryInfoList({ "*.ttf", "*.TTF" }, QDir::Files, QDir::Name);
	if (files.isEmpty()) {
		return QString();
	}
	const QString dest = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/fonts/zacos9";
	QDir().mkpath(dest);
	for (const QFileInfo &file : files) {
		QFile::remove(dest + "/" + file.fileName());
		QFile::copy(file.filePath(), dest + "/" + file.fileName());
	}
	/* Never waitForFinished(): pump events until fc-cache exits. */
	QProcess process;
	process.setProgram("fc-cache");
	process.setArguments({ "-f", dest });
	process.start();
	QEventLoop loop;
	QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
	QObject::connect(&process, &QProcess::errorOccurred, [&](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			loop.quit();
		}
	});
	loop.exec();
	const QStringList families = themeConfValue(name, "font-families").split(',', Qt::SkipEmptyParts);
	const QString system = themeConfValue(name, "system-font").trimmed();
	if (!system.isEmpty() && families.contains(system)) {
		return system;
	}
	const QString views = themeConfValue(name, "views-font").trimmed();
	if (!views.isEmpty() && families.contains(views)) {
		return views;
	}
	return QString();
}

/* The id the Appearance panel gives a PNG in the Desktop Patterns (user) or
 * Wallpaper folder: the prefix, the SHA-256 of the file name, then "image"
 * (see shell/panels/zacos9-patterns). */
static QString catalogId(const char *prefix, const QString &fileName) {
	const QByteArray digest = QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha256);
	return QString::fromLatin1(prefix) + ":" + QString::fromLatin1(digest.toHex()) + ":image";
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
		QFileInfoList all = QDir(src).entryInfoList(QDir::Files, QDir::Name);
		/* Only the decoded assets install: the bundle keeps the raw
		 * resource dumps (*.bin) alongside for reference, and a preset
		 * pointing at one fails to apply ("pattern is missing"). A
		 * desktop picture stays whatever the extractor wrote: a PICT is
		 * decoded to PNG, while a JPEG data fork is copied as it is. */
		const QStringList keptExt = (f.sub == QStringLiteral("sounds"))
			? QStringList{ "wav" }
			: (f.sub == QStringLiteral("wallpapers"))
			? QStringList{ "png", "jpg", "jpeg", "gif", "tif", "tiff", "bmp" }
			: QStringList{ "png" };
		QFileInfoList files;
		for (const QFileInfo &file : all) {
			if (keptExt.contains(file.suffix(), Qt::CaseInsensitive)) {
				files << file;
			}
		}
		if (f.sub == QStringLiteral("patterns")) {
			// Only the desktop pattern (scen patt) and mono PAT tiles are
			// desktop art; the colour-pattern files are widget skins.
			QFileInfoList kept;
			for (const QFileInfo &file : files) {
				if (file.fileName() == "desktop-pattern.png" ||
						file.fileName().startsWith("pattern-")) {
					kept << file;
				}
			}
			files = kept;
			// Prefer the theme's own desktop pattern.
			std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
				const bool ad = a.fileName() == "desktop-pattern.png";
				const bool bd = b.fileName() == "desktop-pattern.png";
				return ad != bd ? ad : a.fileName() < b.fileName();
			});
		}
		if (f.sub == QStringLiteral("wallpapers")) {
			// Prefer the dpnm desktop wallpaper over stray pictures.
			std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
				const bool ad = a.fileName().startsWith("desktop-wallpaper.");
				const bool bd = b.fileName().startsWith("desktop-wallpaper.");
				return ad != bd ? ad : a.fileName() < b.fileName();
			});
		}
		QStringList installed;
		for (const QFileInfo &file : files) {
			const QString id = prefix + "-" + file.completeBaseName();
			QString name = id + "." + file.suffix();
			bool converted = false;
			if (f.sub == QStringLiteral("wallpapers") &&
					file.suffix().compare("png", Qt::CaseInsensitive) != 0) {
				/* The desktop loads a wallpaper as PNG (cairo): convert
				 * whatever Mac OS 9 held (JPEG, GIF, TIFF, BMP) with
				 * Qt, so a picture is not skipped for its format. */
				QImage image(file.filePath());
				if (!image.isNull()) {
					const QString png = id + ".png";
					QFile::remove(dest + "/" + png);
					if (image.save(dest + "/" + png, "PNG")) {
						name = png;
						converted = true;
					}
				}
			}
			if (!converted) {
				QFile::remove(dest + "/" + name);
				QFile::copy(file.filePath(), dest + "/" + name);
			}
			/* A previous import may have left the same picture under
			 * another extension. */
			for (const char *other : { "png", "jpg", "jpeg", "gif", "tif", "tiff", "bmp" }) {
				const QString candidate = id + "." + other;
				if (candidate != name) {
					QFile::remove(dest + "/" + candidate);
				}
			}
			installed << name;
		}
		/* The System's own alert sounds are offered, never chosen as the theme's. */
		for (int i = 0; i < files.size(); i++) {
			if (f.sub == QStringLiteral("sounds") &&
					files[i].fileName().startsWith("system-")) {
				continue;
			}
			const QString copied = installed[i];
			const QString id = QFileInfo(copied).completeBaseName();
			if (f.sub == QStringLiteral("wallpapers")) *wallpaper = catalogId("wallpaper", copied);
			else if (f.sub == QStringLiteral("patterns")) *pattern = catalogId("user", copied);
			else *alert = id;
			break;
		}
	}
}

static QString soundThemeId(const QString &soundThemes, const QString &setName) {
	if (setName.isEmpty()) {
		return QString();
	}
	const QDir base(soundThemes);
	for (const QFileInfo &top : base.entryInfoList(
			QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name)) {
		if (top.fileName() == ".DS_Store") {
			continue;
		}
		if (top.isDir()) {
			for (const QFileInfo &child : QDir(top.filePath()).entryInfoList(QDir::Files, QDir::Name)) {
				QString leaf = child.fileName();
				if (leaf.startsWith("._")) {
					leaf = leaf.mid(2);
				}
				if (leaf != setName) {
					continue;
				}
				const QString identity = top.fileName() + "/" + leaf;
				return "user:" + QString::fromLatin1(QCryptographicHash::hash(
					identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
			}
			continue;
		}
		QString leaf = top.fileName();
		if (leaf.startsWith("._")) {
			leaf = leaf.mid(2);
		}
		if (leaf != setName) {
			continue;
		}
		return "user:" + QString::fromLatin1(QCryptographicHash::hash(
			leaf.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
	}
	return QString();
}

static void setThemeConfValue(const QString &name, const char *key, const QString &value) {
	const QString path = outputDir(name) + "/theme.conf";
	QFile f(path);
	QStringList lines;
	if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&f);
		QString line;
		while (in.readLineInto(&line)) {
			if (!line.startsWith(QString::fromLatin1(key) + "=")) {
				lines << line;
			}
		}
		f.close();
	}
	if (!value.isEmpty()) {
		lines << QString::fromLatin1(key) + "=" + value;
	}
	if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		QTextStream out(&f);
		for (const QString &l : lines) {
			out << l << "\n";
		}
	}
}

static bool copyTree(const QString &src, const QString &dst) {
	if (!QDir().mkpath(dst)) {
		return false;
	}
	bool ok = true;
	const QFileInfoList entries = QDir(src).entryInfoList(
		QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
	for (const QFileInfo &e : entries) {
		if (e.fileName() == ".DS_Store") {
			continue;
		}
		const QString target = dst + "/" + e.fileName();
		if (e.isDir()) {
			ok = copyTree(e.filePath(), target) && ok;
		} else {
			QFile::remove(target);
			ok = QFile::copy(e.filePath(), target) && ok;
		}
	}
	return ok;
}

static int copyMacSoundSets(const QString &src, const QString &dst) {
	int count = 0;
	const QFileInfoList entries = QDir(src).entryInfoList(
		QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
	for (const QFileInfo &e : entries) {
		const QString name = e.fileName();
		if (name == ".DS_Store" || name.startsWith("._")) {
			continue;
		}
		bool ok;
		if (e.isDir()) {
			ok = copyTree(e.filePath(), dst + "/" + name);
		} else {
			QDir().mkpath(dst);
			QFile::remove(dst + "/" + name);
			ok = QFile::copy(e.filePath(), dst + "/" + name);
		}
		if (!ok) {
			continue;
		}
		count++;
		const QString companion = "._" + name;
		if (QFileInfo::exists(src + "/" + companion)) {
			QFile::remove(dst + "/" + companion);
			QFile::copy(src + "/" + companion, dst + "/" + companion);
		}
	}
	return count;
}

static void stampThemeFileIcon(const QString &file, const QString &name) {
	const QImage icon(outputDir(name) + "/file-icon.png");
	if (icon.isNull()) {
		return;
	}
	QString target = file;
	const QString base = QFileInfo(file).fileName();
	if (base.startsWith("._")) {
		const QString sibling = QFileInfo(file).absolutePath() + "/" + base.mid(2);
		if (QFileInfo::exists(sibling)) {
			target = sibling;
		}
	}
	if (!writeCustomIconImage(target, icon)) {
		qWarning() << "Could not stamp the theme file's icon onto" << target;
	}
	/* The folder the theme was filed into wears the same icon, so the
	 * theme is recognisable before its file is opened. */
	const QString folder = QFileInfo(target).absolutePath();
	if (folder.startsWith(appearanceThemesFolder() + "/") &&
			!writeCustomIconImage(folder, icon)) {
		qWarning() << "Could not stamp the theme's icon onto" << folder;
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
	bool copyFolder = false;
	for (int i = 1; i < argc; i++) {
		const QString a = QString::fromLocal8Bit(argv[i]);
		if (a == "--file" && i + 1 < argc) { file = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (a == "--name" && i + 1 < argc) { name = QString::fromLocal8Bit(argv[++i]); batch = true; }
		else if (a == "--system" && i + 1 < argc) { system = QString::fromLocal8Bit(argv[++i]); }
		else if (a == "--copy-system-folder") { copyFolder = true; batch = true; }
		else if (!a.startsWith("--")) { file = a; }
	}

	/* Copy Old Dog's System Folder and stop: no theme file needed. */
	if (copyFolder && file.isEmpty()) {
		QString copied, why;
		if (!importSystemFolder(&copied, &why)) {
			fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(why));
			return 2;
		}
		printf("%s\n", qPrintable(copied));
		return 0;
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
	/* The user's own Mac OS 9 System Folder supplies the Apple menu icon,
	 * the busy pointers, the alert sounds, the desktop pictures and the
	 * fonts. Old Dog keeps it at ~/Classic/System Folder; a copy dragged
	 * onto the Mac's Unix volume (the home folder) lands at
	 * ~/System Folder. --system names a System file directly. */
	QString systemFolder;
	if (system.isEmpty()) {
		systemFolder = macSystemFolder();
		if (systemFolder.isEmpty()) {
			/* Copy it out of Old Dog's disk, which is the only lossless
			 * source of the resource forks themes need. The desktop's
			 * drop import is a batch run, so it must do this without a
			 * question; otherwise themes such as Apple Platinum silently
			 * lose their Apple menu icon. A terminal batch import behaves
			 * the same as the interactive installer. */
			bool want = copyFolder || batch;
			if (!batch) {
				want = QMessageBox::question(nullptr, "Theme Installer",
					"Old Dog's Mac OS 9 System Folder wasn't found.\n\n"
					"Copy it from Old Dog's disk into your Classic folder? The theme\n"
					"uses its Apple menu icon, desktop pictures, sounds and fonts.\n"
					"The disk is mounted read-only; you may be asked for an\n"
					"administrator's password.",
					QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) ==
					QMessageBox::Yes;
			}
			if (want) {
				QString copied, why;
				/* A batch import must not replace a partial System Folder
				 * the user may be filling in; only an explicit copy or the
				 * interactive Yes may do that. */
				if (importSystemFolder(&copied, &why, copyFolder || !batch)) {
					systemFolder = macSystemFolder();
				} else if (batch) {
					fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(why));
				} else {
					QMessageBox::warning(nullptr, "Theme Installer",
						"The System Folder couldn't be copied:\n\n" + why +
						"\n\nThe theme installs without its Apple menu icon, "
						"pictures, sounds and fonts.");
				}
			}
		}
		if (!systemFolder.isEmpty()) {
			for (const QString &candidate : { systemFolder + "/System",
					systemFolder + "/.rsrc/System", systemFolder + "/._System",
					systemFolder + "/System/rsrc" }) {
				if (QFileInfo(candidate).isFile()) {
					system = candidate;
					break;
				}
			}
		}
	} else {
		systemFolder = QFileInfo(system).absolutePath();
	}
	const QString systemFork = system.isEmpty() ? QString() : resolveResourceFork(system);
	QString error;
	if (!runExtractor(fork, name, systemFork, systemFolder, &error)) {
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
	const QString soundThemes = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Sound Themes";
	QString soundSetFolder = systemFolder + "/Appearance/Sound Sets";
	if (!QDir(soundSetFolder).exists()) {
		soundSetFolder = QDir::homePath() + "/Classic/Appearance add-ons/Sound Sets";
	}
	const int soundSets = copyMacSoundSets(soundSetFolder, soundThemes);
	const QString soundId = soundThemeId(soundThemes, themeConfValue(name, "sound-set"));
	const QString fontFamily = installThemeFonts(name);
	const QString presetId = writeAppearancePreset(file, name, wallpaper, pattern, alert, fontFamily, soundId, &error);
	if (presetId.isEmpty()) {
		if (batch) {
			fprintf(stderr, "zacos9-themeinstaller: %s\n", qPrintable(error));
			return 2;
		}
		QMessageBox::critical(nullptr, "Theme Installer", error);
		return 2;
	}
	// The preset named the desktop art; apply it now, not just when the user
	// re-clicks the theme in Appearance. Wallpaper wins over pattern.
	setThemeConfValue(name, "pattern", pattern);
	setThemeConfValue(name, "wallpaper", wallpaper);
	setThemeConfValue(name, "background", !wallpaper.isEmpty() ? "wallpaper" :
		!pattern.isEmpty() ? "pattern" : "");
	setThemeConfValue(name, "alert-sound", alert);
	setThemeConfValue(name, "sound-theme", soundId);
	/* Drop whatever the previous theme put here when this one supplies
	 * nothing, so a theme with no desktop picture does not keep the last
	 * one's wallpaper and pattern underneath its chrome. */
	const QByteArray patternBytes = pattern.toUtf8();
	const QByteArray wallpaperBytes = wallpaper.toUtf8();
	const QByteArray alertBytes = alert.toUtf8();
	/* A theme's own sound set is applied when it names one; without one,
	 * the interface sound set the user chose stays. */
	if (!soundId.isEmpty()) {
		pl_setting_set("sound-theme", soundId.toUtf8().constData());
	}
	pl_setting_set("pattern", patternBytes.isEmpty() ? nullptr : patternBytes.constData());
	pl_setting_set("wallpaper", wallpaperBytes.isEmpty() ? nullptr : wallpaperBytes.constData());
	pl_setting_set("background", !wallpaper.isEmpty() ? "wallpaper" : "pattern");
	pl_setting_set("alert-sound", alertBytes.isEmpty() ? nullptr : alertBytes.constData());
	pl_setting_set("appearance-theme", presetId.toUtf8().constData());
	pl_setting_set("theme-bundle", name.toUtf8().constData());
	stampThemeFileIcon(file, name);
	if (!fontFamily.isEmpty()) {
		pl_setting_set("font-family", fontFamily.toUtf8().constData());
	}
	/* The desktop background and alert sound live in the preset, not
	 * theme.conf: apply them here so installing a theme repaints the
	 * desktop, not just its window chrome. */
	/* What the theme asked for but the user's Mac OS 9 install did not
	 * supply, so a partial import says so instead of looking broken. */
	QStringList missing;
	if (!QFileInfo::exists(outputDir(name) + "/apple-menu.png")) {
		missing << "Apple menu icon";
	}
	if (wallpaper.isEmpty()) {
		missing << "desktop picture";
	}
	if (!QFileInfo::exists(outputDir(name) + "/icons/disk-32.png")) {
		missing << "hard disk icon";
	}
	if (QDir(outputDir(name) + "/fonts").entryList({ "*.ttf" }, QDir::Files).isEmpty()) {
		missing << "fonts";
	}
	zac_theme_apply();
	if (batch) {
		printf("installed %s (%d sound sets", qPrintable(outputDir(name)), soundSets);
		if (!missing.isEmpty()) {
			printf("; not found: %s", qPrintable(missing.join(", ")));
		}
		printf(")\n");
		return 0;
	}
	QString message = "Theme \"" + name + "\" was installed from your Mac OS 9 folder.";
	if (!missing.isEmpty()) {
		message += "\n\nYour Mac OS 9 System Folder did not supply its " +
			missing.join(", ") + "; the theme keeps ZacOS's own.";
	}
	QMessageBox::information(nullptr, "Theme Installer", message);
	return 0;
}
