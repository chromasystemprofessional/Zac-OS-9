#include "filechooserportal.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMimeDatabase>
#include <QPushButton>
#include <QSet>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDBusArgument>
#include <algorithm>
#include <QDebug>

namespace {

bool hiddenSystemMount(const QString &path) {
	for (const QString prefix : { "/proc", "/sys", "/dev", "/run", "/snap" }) {
		if (path == prefix || path.startsWith(prefix + '/')) {
			return true;
		}
	}
	return false;
}

class LocationDialog : public QDialog {
public:
	explicit LocationDialog(const QString &title) {
		setWindowTitle(title.isEmpty() ? QStringLiteral("Choose a Location") : title);
		setModal(true);
		resize(520, 350);
		auto *layout = new QVBoxLayout(this);
		auto *heading = new QLabel("Choose a disk", this);
		layout->addWidget(heading);

		m_locations = new QListWidget(this);
		m_locations->setViewMode(QListView::IconMode);
		m_locations->setMovement(QListView::Static);
		m_locations->setResizeMode(QListView::Adjust);
		m_locations->setIconSize(QSize(64, 64));
		m_locations->setGridSize(QSize(132, 108));
		m_locations->setSpacing(8);
		for (const FileChooserLocation &location : fileChooserLocations()) {
			auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_DriveHDIcon),
				location.name, m_locations);
			item->setData(Qt::UserRole, location.path);
		}
		m_locations->setCurrentRow(0);
		layout->addWidget(m_locations, 1);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open |
			QDialogButtonBox::Cancel, this);
		m_open = buttons->button(QDialogButtonBox::Open);
		m_open->setEnabled(m_locations->count() > 0);
		layout->addWidget(buttons);
		connect(m_locations, &QListWidget::itemDoubleClicked, this,
			[this](QListWidgetItem *) { accept(); });
		connect(m_locations, &QListWidget::currentRowChanged, this,
			[this](int row) { m_open->setEnabled(row >= 0); });
		connect(m_open, &QPushButton::clicked, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	}

	QString selectedPath() const {
		const QListWidgetItem *item = m_locations->currentItem();
		return item ? item->data(Qt::UserRole).toString() : QString();
	}

private:
	QListWidget *m_locations = nullptr;
	QPushButton *m_open = nullptr;
};

static QStringList decodePath(const QVariantMap &options, const QString &key) {
	const QByteArray bytes = options.value(key).toByteArray();
	if (bytes.isEmpty()) {
		return {};
	}
	QByteArray path = bytes;
	if (path.endsWith('\0')) {
		path.chop(1);
	}
	const QString decoded = QFile::decodeName(path);
	return QFileInfo(decoded).isAbsolute() ? QStringList{ QDir::cleanPath(decoded) } :
		QStringList{};
}

static QString decodeDirectory(const QVariantMap &options, const QString &key) {
	const QString path = decodePath(options, key).value(0);
	return !path.isEmpty() && QFileInfo(path).isDir() ? path : QString();
}

static QStringList decodeFiles(const QVariant &value) {
	if (!value.canConvert<QDBusArgument>()) {
		return {};
	}
	QDBusArgument array = qvariant_cast<QDBusArgument>(value);
	if (array.currentSignature() != QStringLiteral("aay")) {
		return {};
	}
	QStringList files;
	array.beginArray();
	while (!array.atEnd()) {
		QByteArray bytes;
		array >> bytes;
		if (bytes.endsWith('\0')) {
			bytes.chop(1);
		}
		files << QFile::decodeName(bytes);
	}
	array.endArray();
	return files;
}

class PortalFileDialog {
public:
	static QStringList openFiles(QWidget *parent, const QString &title,
			const QVariantMap &options) {
		const bool directory = options.value("directory").toBool();
		const bool multiple = options.value("multiple").toBool();
		QString start = decodeDirectory(options, "current_folder");
		if (start.isEmpty()) {
			start = chooseLocation(parent, title);
		}
		if (start.isEmpty()) {
			return {};
		}

		QFileDialog dialog(parent, title.isEmpty() ? "Open" : title, start);
		prepare(dialog);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(directory ? QFileDialog::Directory :
			(multiple ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile));
		dialog.setOption(QFileDialog::ShowDirsOnly, directory);
		applyFilters(dialog, options);
		const QString acceptLabel = options.value("accept_label").toString();
		dialog.setLabelText(QFileDialog::Accept,
			acceptLabel.isEmpty() ? "Open" : acceptLabel);
		return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles() : QStringList{};
	}

	static QString saveFile(QWidget *parent, const QString &title, const QVariantMap &options) {
		QString start = decodeDirectory(options, "current_folder");
		QString file = decodePath(options, "current_file").value(0);
		if (start.isEmpty() && !file.isEmpty()) {
			start = QFileInfo(file).absolutePath();
		}
		if (start.isEmpty()) {
			start = chooseLocation(parent, title);
		}
		if (start.isEmpty()) {
			return {};
		}
		QFileDialog dialog(parent, title.isEmpty() ? "Save" : title,
			file.isEmpty() ? start : file);
		prepare(dialog);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
		dialog.setFileMode(QFileDialog::AnyFile);
		const QString currentName = options.value("current_name").toString();
		if (!currentName.isEmpty() && currentName != "." && currentName != ".." &&
				QFileInfo(currentName).fileName() == currentName) {
			dialog.selectFile(currentName);
		}
		applyFilters(dialog, options);
		dialog.setLabelText(QFileDialog::Accept,
			options.value("accept_label").toString().isEmpty() ? "Save" :
			options.value("accept_label").toString());
		return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
	}

private:
	static QString chooseLocation(QWidget *parent, const QString &title) {
		LocationDialog locations(title);
		if (parent) {
			locations.setParent(parent, Qt::Dialog);
		}
		return locations.exec() == QDialog::Accepted ? locations.selectedPath() : QString();
	}

	static void prepare(QFileDialog &dialog) {
		prepareFileChooserDialog(dialog);
	}

	static void applyFilters(QFileDialog &dialog, const QVariantMap &options) {
		const QVariant value = options.value("filters");
		if (!value.canConvert<QDBusArgument>()) {
			return;
		}
		QDBusArgument array = qvariant_cast<QDBusArgument>(value);
		if (array.currentSignature() != QStringLiteral("a(sa(us))")) {
			return;
		}
		QStringList filters;
		array.beginArray();
		while (!array.atEnd()) {
			QString name;
			QStringList patterns;
			array.beginStructure();
			array >> name;
			array.beginArray();
			while (!array.atEnd()) {
				uint kind = 0;
				QString pattern;
				array.beginStructure();
				array >> kind >> pattern;
				array.endStructure();
				if (kind == 0) {
					patterns << pattern;
				} else if (kind == 1) {
					const QMimeType mime = QMimeDatabase().mimeTypeForName(pattern);
					if (mime.isValid()) {
						patterns << mime.globPatterns();
					}
				}
			}
			array.endArray();
			array.endStructure();
			patterns.removeDuplicates();
			if (!patterns.isEmpty()) {
				filters << QStringLiteral("%1 (%2)").arg(name, patterns.join(' '));
			}
		}
		array.endArray();
		if (!filters.isEmpty()) {
			dialog.setNameFilters(filters);
			const QVariant requested = options.value("current_filter");
			if (requested.canConvert<QDBusArgument>()) {
				QDBusArgument filter = qvariant_cast<QDBusArgument>(requested);
				if (filter.currentSignature() != QStringLiteral("(sa(us))")) {
					return;
				}
				QString name;
				filter.beginStructure();
				filter >> name;
				filter.beginArray();
				while (!filter.atEnd()) {
					uint kind = 0;
					QString pattern;
					filter.beginStructure();
					filter >> kind >> pattern;
					filter.endStructure();
				}
				filter.endArray();
				filter.endStructure();
				for (const QString &candidate : filters) {
					if (candidate.startsWith(name + " (")) {
						dialog.selectNameFilter(candidate);
						break;
					}
				}
			}
		}
	}
};

class FileChooserPortal : public QDBusVirtualObject {
public:
	QString introspect(const QString &) const override {
		return QStringLiteral(
			"<interface name='org.freedesktop.impl.portal.FileChooser'>"
			"<method name='OpenFile'>"
			"<arg type='o' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='s' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='a{sv}' direction='in'/><arg type='u' direction='out'/>"
			"<arg type='a{sv}' direction='out'/></method>"
			"<method name='SaveFile'>"
			"<arg type='o' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='s' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='a{sv}' direction='in'/><arg type='u' direction='out'/>"
			"<arg type='a{sv}' direction='out'/></method>"
			"<method name='SaveFiles'>"
			"<arg type='o' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='s' direction='in'/><arg type='s' direction='in'/>"
			"<arg type='a{sv}' direction='in'/><arg type='u' direction='out'/>"
			"<arg type='a{sv}' direction='out'/></method>"
			"</interface>");
	}

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		if (message.interface() !=
				QLatin1String("org.freedesktop.impl.portal.FileChooser")) {
			return false;
		}
		const QString method = message.member();
		if (method != "OpenFile" && method != "SaveFile" && method != "SaveFiles") {
			return false;
		}
		const QVariantList args = message.arguments();
		if (args.size() != 5 || !args.at(4).canConvert<QVariantMap>()) {
			bus.send(message.createErrorReply(QDBusError::InvalidArgs,
				"Malformed FileChooser request."));
			return true;
		}
		const QVariantMap options = args.at(4).toMap();
		const QString title = args.at(3).toString();
		QStringList paths;
		if (method == "OpenFile") {
			paths = PortalFileDialog::openFiles(nullptr, title, options);
		} else if (method == "SaveFile") {
			const QString path = PortalFileDialog::saveFile(nullptr, title, options);
			if (!path.isEmpty()) {
				paths << path;
			}
		} else {
			if (!options.contains("files") ||
					!options.value("files").canConvert<QDBusArgument>()) {
				bus.send(message.createErrorReply(QDBusError::InvalidArgs,
					"SaveFiles requires an array of file names."));
				return true;
			}
			const QDBusArgument fileNames = qvariant_cast<QDBusArgument>(
				options.value("files"));
			if (fileNames.currentSignature() != QStringLiteral("aay")) {
				bus.send(message.createErrorReply(QDBusError::InvalidArgs,
					"SaveFiles requires an array of byte-array file names."));
				return true;
			}
			const QStringList names = decodeFiles(options.value("files"));
			for (const QString &name : names) {
				if (name.isEmpty() || name == "." || name == ".." ||
						QFileInfo(name).fileName() != name) {
					bus.send(message.createErrorReply(QDBusError::InvalidArgs,
						"SaveFiles requires simple file names."));
					return true;
				}
			}
			/* SaveFiles has a folder and filenames; return one generated URI
			 * per requested name, matching the portal's ordered results. */
			QString folder = decodeDirectory(options, "current_folder");
			if (folder.isEmpty()) {
				folder = chooseSaveFilesLocation(title);
			}
			if (!folder.isEmpty()) {
				QSet<QString> allocatedNames;
				for (const QString &name : names) {
					QString candidate = name;
					const QFileInfo requested(QDir(folder).filePath(candidate));
					if (requested.exists() || allocatedNames.contains(candidate)) {
						const QString suffix = requested.completeSuffix();
						const QString base = suffix.isEmpty() ? requested.fileName() :
							requested.baseName();
						for (quint64 index = 1; ; ++index) {
							candidate = suffix.isEmpty() ?
								QStringLiteral("%1 (%2)").arg(base).arg(index) :
								QStringLiteral("%1 (%2).%3").arg(base).arg(index).arg(suffix);
							if (!QFileInfo(QDir(folder).filePath(candidate)).exists() &&
									!allocatedNames.contains(candidate)) {
								break;
							}
						}
					}
					allocatedNames.insert(candidate);
					paths << QDir(folder).filePath(candidate);
				}
			}
		}

		QVariantMap results;
		if (!paths.isEmpty()) {
			QStringList uris;
			for (const QString &path : paths) {
				uris << QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
			}
			results.insert("uris", uris);
			if (method == "OpenFile") {
				results.insert("writable", options.value("writable", false).toBool());
			}
		}
		const uint response = paths.isEmpty() ? 1 : 0;
		bus.send(message.createReply({ response, QVariant::fromValue(results) }));
		return true;
	}

private:
	static QString chooseSaveFilesLocation(const QString &title) {
		LocationDialog locations(title);
		return locations.exec() == QDialog::Accepted ? locations.selectedPath() : QString();
	}
};

} // namespace

void prepareFileChooserDialog(QFileDialog &dialog) {
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	dialog.setSidebarUrls({});
	dialog.setViewMode(QFileDialog::Detail);
	dialog.setProperty("zacos9-file-portal-dialog", true);
	if (QWidget *sidebar = dialog.findChild<QWidget *>(QStringLiteral("sidebar"))) {
		sidebar->hide();
	}
}

QList<FileChooserLocation> fileChooserLocations() {
	const QString privateDisk = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + "/zacos9/Zacintosh HD";
	QList<FileChooserLocation> locations = {
		{ QStringLiteral("Zacintosh HD"), privateDisk }
	};
	QSet<QString> seen{ QDir::cleanPath(privateDisk), QStringLiteral("/") };
	for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
		const QString path = QDir::cleanPath(volume.rootPath());
		if (!volume.isValid() || !volume.isReady() || seen.contains(path) ||
				hiddenSystemMount(path)) {
			continue;
		}
		seen.insert(path);
		QString name = volume.name().trimmed();
		if (name.isEmpty()) {
			name = QFileInfo(path).fileName();
		}
		if (name.isEmpty()) {
			name = path;
		}
		locations << FileChooserLocation{ name, path };
	}
	std::sort(locations.begin() + 1, locations.end(),
		[](const FileChooserLocation &a, const FileChooserLocation &b) {
			return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
		});
	return locations;
}

bool registerFileChooserPortal() {
	static FileChooserPortal portal;
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal,
			QDBusConnection::SingleNode) ||
			!bus.registerService("org.freedesktop.impl.portal.desktop.zacos9")) {
		qCritical().noquote() << "zacos9-file-portal: D-Bus registration failed:"
			<< bus.lastError().message();
		return false;
	}
	return true;
}
