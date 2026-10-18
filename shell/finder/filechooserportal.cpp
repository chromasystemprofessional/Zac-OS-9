#include <gio/gio.h>

#include "filechooserportal.h"
#include "items.h"
#include "alias.h"
#include "localvolumes.h"
#include "netvolumes.h"
#include "vfs.h"
#include "pixels.h"
#include "settings.h"

#include <QDialog>
#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QIcon>
#include <QPixmap>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPushButton>
#include <QRegularExpression>
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
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QDBusArgument>
#include <QPointer>
#include <QSaveFile>
#include <QSharedPointer>
#include <QTimer>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QToolButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <QDebug>

namespace {

/* The Finder's application scan asks dpkg about every launcher; requests
 * that arrive meanwhile wait rather than re-entering it. A request closed
 * while waiting stops waiting at once. */
bool g_warming = false;

void whenReady(QObject *context, std::function<bool()> abandoned, std::function<void()> work) {
	QTimer::singleShot(g_warming ? 50 : 0, context, [context, abandoned, work]() {
		if (g_warming && !abandoned()) {
			whenReady(context, abandoned, work);
		} else {
			work();
		}
	});
}

bool hiddenSystemMount(const QString &path) {
	if (path.startsWith("/run/media/")) {
		return false;
	}
	for (const QString prefix : { "/proc", "/sys", "/dev", "/run", "/snap" }) {
		if (path == prefix || path.startsWith(prefix + '/')) {
			return true;
		}
	}
	return false;
}

/* A Recent button: its menu lists fileChooserRecentItems(); choosing one
 * calls `chosen` with its path. */
QPushButton *recentButton(QWidget *parent, std::function<void(const QString &)> chosen) {
	auto *button = new QPushButton("Recent", parent);
	button->setObjectName("recentButton");
	button->setAutoDefault(false);
	auto *menu = new QMenu(button);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, chosen] {
		menu->clear();
		const QStringList items = fileChooserRecentItems();
		for (const QString &path : items) {
			menu->addAction(QFileInfo(path).fileName(), menu, [chosen, path] { chosen(path); });
		}
		if (items.isEmpty()) {
			menu->addAction("No recent items")->setEnabled(false);
		}
	});
	button->setMenu(menu);
	return button;
}

/* The local folder or file a drag carries, if just one. */
QString droppedPath(const QMimeData *mime) {
	const QList<QUrl> urls = mime ? mime->urls() : QList<QUrl>{};
	return urls.size() == 1 && urls[0].isLocalFile() && QFileInfo::exists(urls[0].toLocalFile()) ?
		QDir::cleanPath(urls[0].toLocalFile()) : QString();
}

class DriveFileDialog : public QFileDialog {
public:
	/* What exec() returns for Other Locations..., and for a Recent item or
	 * a dropped folder (see jumpTo()). */
	static constexpr int OtherLocations = 2;
	static constexpr int JumpElsewhere = 3;

	/* `otherLocations`: it opened straight in a folder (General Controls'
	 * Documents), so it offers the list of disks, too. */
	DriveFileDialog(QWidget *parent, const QString &title, const FileChooserLocation &drive,
			bool otherLocations = false)
		: QFileDialog(parent, title, drive.path), m_root(QFileInfo(drive.path).canonicalFilePath()),
		  m_realRoots(drive.realRoots) {
		prepareFileChooserDialog(*this);
		setAcceptDrops(true);
		if (auto *locations = findChild<QComboBox *>("lookInCombo")) {
			locations->hide();
		}
		m_location = new QLabel(this);
		m_error = new QLabel(this);
		m_error->setWordWrap(true);
		layout()->addWidget(m_location);
		m_recent = new QComboBox(this);
		m_recent->addItem("Recent Folders", QString());
		const FileChooserLocation recentLocation{ {}, m_root, false, m_realRoots };
		for (const QString &path : fileChooserRecentFolders(recentLocation)) {
			m_recent->addItem(QDir::home().relativeFilePath(path), path);
		}
		m_recent->setEnabled(m_recent->count() > 1);
		layout()->addWidget(m_recent);
		layout()->addWidget(m_error);
		connect(m_recent, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
			const QString path = m_recent->itemData(index).toString();
			if (path.isEmpty()) {
				return;
			}
			if (!QFileInfo(path).isDir() ||
					!fileChooserPathWithinDisk(path, m_root, m_realRoots, true)) {
				m_error->setText("That recent folder is no longer available in this location.");
			} else {
				setDirectory(path);
				m_error->clear();
				updateLocation();
			}
			m_recent->setCurrentIndex(0);
		});
		if (otherLocations) {
			auto *other = new QPushButton("Other Locations...", this);
			other->setObjectName("otherLocationsButton");
			other->setAutoDefault(false);
			layout()->addWidget(other);
			connect(other, &QPushButton::clicked, this, [this] { done(OtherLocations); });
		}
		layout()->addWidget(recentButton(this, [this](const QString &path) { jump(path); }));
		/* A folder dragged onto the dialog opens there (Navigation Services). */
		setAcceptDrops(true);
		for (const char *name : { "backButton", "forwardButton" }) {
			if (auto *button = findChild<QToolButton *>(name)) {
				button->hide();
			}
		}
		connect(this, &QFileDialog::directoryEntered, this, [this](const QString &path) {
			if (!fileChooserPathWithinDisk(path, m_root, m_realRoots, true)) {
				setDirectory(m_root);
				m_error->setText("Choose a folder inside this location. Cancel to choose another location.");
			}
			updateLocation();
		});
		updateLocation();
	}

	/* The Recent item or dropped folder to go to, once exec() has returned
	 * JumpElsewhere. */
	QString jumpTo() const { return m_jumpTo; }

	/* Go to `path` (a folder, or a file to select in its folder): the
	 * dialog ends with JumpElsewhere and opens again in the location that
	 * holds it, through the startup disk's links if that's where it is
	 * (finding them rebuilds the tree this dialog is showing). */
	void jump(const QString &path) {
		m_jumpTo = path;
		done(JumpElsewhere);
	}

protected:
	void dragEnterEvent(QDragEnterEvent *e) override {
		if (!droppedPath(e->mimeData()).isEmpty()) {
			e->acceptProposedAction();
		}
	}
	void dragMoveEvent(QDragMoveEvent *e) override {
		if (!droppedPath(e->mimeData()).isEmpty()) {
			e->acceptProposedAction();
		}
	}
	void dropEvent(QDropEvent *e) override {
		const QString path = droppedPath(e->mimeData());
		if (!path.isEmpty()) {
			e->acceptProposedAction();
			jump(path);
		}
	}

	void accept() override {
		for (const QString &path : selectedFiles()) {
			if (!fileChooserPathWithinDisk(path, m_root, m_realRoots, false)) {
				m_error->setText(fileChooserPathWithinDisk(path, m_root, m_realRoots, true) ?
					QStringLiteral("Choose an item inside a folder such as Home.") :
					QStringLiteral("This item is outside the selected location. Choose an item inside it."));
				/* A stale selection would keep rejecting later valid choices. */
				for (QAbstractItemView *view : findChildren<QAbstractItemView *>()) {
					if (view->selectionModel()) {
						view->selectionModel()->clearSelection();
					}
				}
				if (auto *name = findChild<QLineEdit *>("fileNameEdit")) {
					name->clear();
				}
				setDirectory(m_root);
				updateLocation();
				return;
			}
		}
		QFileDialog::accept();
	}

public:
	/* What the application receives: real files, not the chooser's links. */
	QStringList chosenFiles() const {
		QStringList files;
		for (const QString &path : selectedFiles()) {
			files << fileChooserRealPath(path);
		}
		return files;
	}

private:
	void updateLocation() {
		const QString current = QDir::cleanPath(directory().absolutePath());
		m_location->setText(QDir(m_root).relativeFilePath(current));
		if (auto *up = findChild<QToolButton *>("toParentButton")) {
			up->setEnabled(current != m_root);
		}
	}

	QString m_root;
	QStringList m_realRoots;
	QString m_jumpTo;
	QLabel *m_location = nullptr;
	QComboBox *m_recent = nullptr;
	QLabel *m_error = nullptr;
};

class ChooserRequest : public QDBusVirtualObject {
public:
	QString introspect(const QString &) const override {
		return QStringLiteral("<interface name='org.freedesktop.impl.portal.Request'>"
			"<method name='Close'/></interface>");
	}

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		if (message.interface() != "org.freedesktop.impl.portal.Request" ||
				message.member() != "Close") {
			return false;
		}
		cancelled = true;
		if (dialog) {
			dialog->reject();
		}
		bus.send(message.createReply());
		return true;
	}

	int run(QDialog &picker) {
		if (cancelled) {
			return QDialog::Rejected;
		}
		dialog = &picker;
		const int result = picker.exec();
		dialog.clear();
		return result;
	}

	bool cancelled = false;
	QPointer<QDialog> dialog;
};

} // namespace

QImage fileChooserDriveIcon(bool usb, bool selected) {
	Pixels icon(PL_ICON_LARGE, PL_ICON_LARGE);
	pl_icon_paint_label(&icon.c, 0, 0, PL_ICON_DISK, PL_ICON_LARGE, selected, pl_labels[0].color);
	if (usb) {
		const int badgeSize = PL_ICON_LARGE / 2;
		Pixels badge(PL_ICON_SMALL, PL_ICON_SMALL);
		pl_icon_paint_label(&badge.c, 0, 0, PL_ICON_EXT_USB, PL_ICON_SMALL, selected,
			pl_labels[0].color);
		const QImage scaled = badge.img.scaled(badgeSize, badgeSize,
			Qt::IgnoreAspectRatio, Qt::FastTransformation);
		pl_image(&icon.c, PL_ICON_LARGE - badgeSize, PL_ICON_LARGE - badgeSize,
			reinterpret_cast<const uint32_t *>(scaled.constBits()), badgeSize, badgeSize);
	}
	return icon.img;
}

namespace {

/* Pixel art is scaled whole-number with nearest-neighbour, as on the desktop. */
QIcon driveIcon(bool usb, bool folderAlias) {
	QIcon icon;
	for (int scale = 1; scale <= 3; scale++) {
		for (bool selected : { false, true }) {
			QImage image;
			if (folderAlias) {
				Pixels folder(PL_ICON_LARGE, PL_ICON_LARGE);
				pl_icon_paint_label(&folder.c, 0, 0, PL_ICON_FOLDER, PL_ICON_LARGE,
					selected, pl_labels[0].color);
				image = folder.img;
			} else {
				image = fileChooserDriveIcon(usb, selected);
			}
			icon.addPixmap(QPixmap::fromImage(image.scaled(image.size() * scale,
				Qt::IgnoreAspectRatio, Qt::FastTransformation)),
				selected ? QIcon::Selected : QIcon::Normal);
		}
	}
	return icon;
}

class LocationDialog : public QDialog {
public:
	explicit LocationDialog(const QString &title) {
		setWindowTitle(title.isEmpty() ? QStringLiteral("Choose a Location") : title);
		setModal(true);
		resize(520, 350);
		auto *layout = new QVBoxLayout(this);
		auto *heading = new QLabel("Choose a disk or desktop folder alias", this);
		layout->addWidget(heading);
		m_error = new QLabel(this);
		m_error->setWordWrap(true);
		layout->addWidget(m_error);

		m_locations = new QListWidget(this);
		m_locations->setViewMode(QListView::IconMode);
		m_locations->setMovement(QListView::Static);
		m_locations->setResizeMode(QListView::Adjust);
		m_locations->setIconSize(QSize(PL_ICON_LARGE, PL_ICON_LARGE));
		m_locations->setGridSize(QSize(132, 108));
		m_locations->setSpacing(8);
		m_drives = fileChooserLocations();
		for (int i = 0; i < m_drives.size(); i++) {
			auto *item = new QListWidgetItem(
				driveIcon(m_drives[i].usb, m_drives[i].folderAlias), m_drives[i].name, m_locations);
			if (m_drives[i].folderAlias) {
				QFont font = item->font();
				font.setItalic(true);
				item->setFont(font);
			}
			item->setData(Qt::UserRole, i);
		}
		m_locations->setCurrentRow(0);
		layout->addWidget(m_locations, 1);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open |
			QDialogButtonBox::Cancel, this);
		buttons->addButton(recentButton(this, [this](const QString &path) {
			m_recent = path;
			done(DriveFileDialog::JumpElsewhere);
		}), QDialogButtonBox::ActionRole);
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

	FileChooserLocation selectedDrive() const {
		const QListWidgetItem *item = m_locations->currentItem();
		return item ? m_drives.value(item->data(Qt::UserRole).toInt()) : FileChooserLocation{};
	}
	/* The Recent item chosen instead, after exec() returned JumpElsewhere. */
	QString recent() const { return m_recent; }

protected:
	void accept() override {
		FileChooserLocation location = selectedDrive();
		if (location.folderAlias) {
			const AliasResolution resolved = aliasResolve(location.path);
			if (!resolved.error.isEmpty() ||
					(resolved.state != AliasState::Ok && resolved.state != AliasState::Reconnected)) {
				QString problem = aliasProblemText(resolved.state);
				if (problem.isEmpty()) {
					problem = "This desktop item is no longer an alias.";
				}
				m_error->setText(resolved.error.isEmpty() ? problem +
					" Reconnect the server or use Finder's Fix Alias." : resolved.error);
				return;
			}
			location.path = QFileInfo(resolved.target).canonicalFilePath();
		}
		const QFileInfo root(location.path);
		if (!root.isDir() || !root.isReadable() || root.canonicalFilePath().isEmpty() ||
				(!location.folderAlias && root.canonicalFilePath() == "/")) {
			m_error->setText("This location is not accessible. Reconnect it or choose another location.");
			return;
		}
		m_drives[m_locations->currentItem()->data(Qt::UserRole).toInt()] = location;
		QDialog::accept();
	}

private:
	QString m_recent;
	QList<FileChooserLocation> m_drives;
	QListWidget *m_locations = nullptr;
	QPushButton *m_open = nullptr;
	QLabel *m_error = nullptr;
};

static QStringList decodeFiles(const QVariant &value) {
	if (!value.canConvert<QDBusArgument>()) {
		return {};
	}
	const QDBusArgument array = qvariant_cast<QDBusArgument>(value);
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
	static QStringList openFiles(QWidget *parent, const QString &title, const QString &appId,
			const QVariantMap &options, ChooserRequest &request) {
		const bool directory = options.value("directory").toBool();
		const bool multiple = options.value("multiple").toBool();
		return run(parent, title, appId, request, [&](DriveFileDialog &dialog) {
			dialog.setWindowTitle(title.isEmpty() ? "Open" : title);
			dialog.setAcceptMode(QFileDialog::AcceptOpen);
			dialog.setFileMode(directory ? QFileDialog::Directory :
				(multiple ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile));
			dialog.setOption(QFileDialog::ShowDirsOnly, directory);
			applyFilters(dialog, options);
			const QString acceptLabel = options.value("accept_label").toString();
			dialog.setLabelText(QFileDialog::Accept,
				acceptLabel.isEmpty() ? "Open" : acceptLabel);
		});
	}

	static QString saveFile(QWidget *parent, const QString &title, const QString &appId,
			const QVariantMap &options, ChooserRequest &request) {
		return run(parent, title, appId, request, [&](DriveFileDialog &dialog) {
			dialog.setWindowTitle(title.isEmpty() ? "Save" : title);
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
		}).value(0);
	}

private:
	/* The dialog opens in General Controls' Documents choice when there
	 * is one, or else on the disk chosen from the list; Other Locations...
	 * goes back to the list. */
	static QStringList run(QWidget *parent, const QString &title, const QString &appId,
			ChooserRequest &request, const std::function<void(DriveFileDialog &)> &setUp) {
		QString folder, select;
		FileChooserLocation start;
		const QString startFolder = fileChooserStartFolder(appId);
		if (!startFolder.isEmpty()) {
			start = fileChooserLocationFor(fileChooserLocations(), startFolder, &folder);
		}
		/* A Recent item or dropped folder: the location holding it, opened
		 * there (a document selected). Somewhere we can't go: the list. */
		auto jump = [&](const QString &path) {
			const QFileInfo info(path);
			select = info.isDir() ? QString() : info.fileName();
			start = fileChooserLocationFor(fileChooserLocations(),
				info.isDir() ? path : info.absolutePath(), &folder);
		};
		for (;;) {
			if (start.path.isEmpty()) {
				folder.clear();
				select.clear();
				QString recent;
				start = chooseLocation(parent, title, request, &recent);
				if (!recent.isEmpty()) {
					jump(recent);
					if (start.path.isEmpty()) {
						continue;
					}
				}
				if (start.path.isEmpty()) {
					return {};
				}
			}
			DriveFileDialog dialog(parent, title, start, !folder.isEmpty());
			setUp(dialog);
			if (!folder.isEmpty()) {
				dialog.setDirectory(folder);
			}
			if (!select.isEmpty()) {
				dialog.selectFile(select);
			}
			const int result = request.run(dialog);
			if (result == DriveFileDialog::OtherLocations && !request.cancelled) {
				start = {};
				continue;
			}
			if (result == DriveFileDialog::JumpElsewhere && !request.cancelled) {
				jump(dialog.jumpTo());
				continue;
			}
			if (result != QDialog::Accepted) {
				return {};
			}
			fileChooserRememberFolder(appId, fileChooserRealPath(dialog.directory().absolutePath()));
			fileChooserRememberRecentFolder(fileChooserRealPath(dialog.directory().absolutePath()));
			return dialog.chosenFiles();
		}
	}

	static FileChooserLocation chooseLocation(QWidget *parent, const QString &title,
			ChooserRequest &request, QString *recent) {
		LocationDialog locations(title);
		if (parent) {
			locations.setParent(parent, Qt::Dialog);
		}
		const int result = request.run(locations);
		if (result == DriveFileDialog::JumpElsewhere && !request.cancelled) {
			*recent = locations.recent();
		}
		return result == QDialog::Accepted ? locations.selectedDrive() : FileChooserLocation{};
	}

	static void applyFilters(QFileDialog &dialog, const QVariantMap &options) {
		const QVariant value = options.value("filters");
		if (!value.canConvert<QDBusArgument>()) {
			return;
		}
		const QDBusArgument array = qvariant_cast<QDBusArgument>(value);
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
				const QDBusArgument filter = qvariant_cast<QDBusArgument>(requested);
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
		if (message.signature() != "osssa{sv}" || args.size() != 5) {
			bus.send(message.createErrorReply(QDBusError::InvalidArgs,
				"Malformed FileChooser request."));
			return true;
		}
		const QVariantMap options = qdbus_cast<QVariantMap>(args.at(4));
		if (m_busy) {
			bus.send(message.createErrorReply("org.freedesktop.portal.Error.Failed",
				"Another file chooser is already open."));
			return true;
		}
		const QString handle = qdbus_cast<QDBusObjectPath>(args.at(0)).path();
		auto request = QSharedPointer<ChooserRequest>::create();
		QDBusConnection connection(bus);
		if (!connection.registerVirtualObject(handle, request.data(), QDBusConnection::SingleNode)) {
			bus.send(message.createErrorReply(QDBusError::Failed,
				"Could not register file chooser request: " + connection.lastError().message()));
			return true;
		}
		m_busy = true;
		message.setDelayedReply(true);
		whenReady(this, [request]() { return request->cancelled; },
				[this, message, connection, request, handle, options]() {
			if (request->cancelled) {
				connection.send(message.createReply({ 1u, QVariant::fromValue(QVariantMap()) }));
			} else {
				respond(message, connection, options, *request);
			}
			QDBusConnection bus(connection);
			bus.unregisterObject(handle);
			m_busy = false;
		});
		return true;
	}

private:
	void respond(const QDBusMessage &message, const QDBusConnection &bus,
			const QVariantMap &options, ChooserRequest &request) {
		const QString method = message.member();
		const QVariantList args = message.arguments();
		const QString appId = args.at(1).toString();
		const QString title = args.at(3).toString();
		QStringList paths;
		if (method == "OpenFile") {
			paths = PortalFileDialog::openFiles(nullptr, title, appId, options, request);
		} else if (method == "SaveFile") {
			const QString path = PortalFileDialog::saveFile(nullptr, title, appId, options, request);
			if (!path.isEmpty()) {
				paths << path;
			}
		} else {
			if (!options.contains("files") ||
					!options.value("files").canConvert<QDBusArgument>()) {
				bus.send(message.createErrorReply(QDBusError::InvalidArgs,
					"SaveFiles requires an array of file names."));
				return;
			}
			const QDBusArgument fileNames = qvariant_cast<QDBusArgument>(
				options.value("files"));
			if (fileNames.currentSignature() != QStringLiteral("aay")) {
				bus.send(message.createErrorReply(QDBusError::InvalidArgs,
					"SaveFiles requires an array of byte-array file names."));
				return;
			}
			const QStringList names = decodeFiles(options.value("files"));
			for (const QString &name : names) {
				if (name.isEmpty() || name == "." || name == ".." ||
						QFileInfo(name).fileName() != name) {
					bus.send(message.createErrorReply(QDBusError::InvalidArgs,
						"SaveFiles requires simple file names."));
					return;
				}
			}
			/* SaveFiles has a folder and filenames; return one generated URI
			 * per requested name, matching the portal's ordered results. */
			QVariantMap folderOptions(options);
			folderOptions.insert("directory", true);
			folderOptions.insert("multiple", false);
			const QString folder = PortalFileDialog::openFiles(
				nullptr, title, appId, folderOptions, request).value(0);
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
	}

	bool m_busy = false;
};

} // namespace

static QString canonicalTarget(const QString &path) {
	const QFileInfo info(path);
	QString target = info.canonicalFilePath();
	if (target.isEmpty() && !info.exists() && !info.isSymLink()) {
		const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
		if (!parent.isEmpty()) {
			target = QDir(parent).filePath(info.fileName());
		}
	}
	return target;
}

static bool under(const QString &path, const QString &root) {
	return !path.isEmpty() && !root.isEmpty() &&
		(path == root || path.startsWith(root == "/" ? root : root + '/'));
}

bool fileChooserPathWithinDrive(const QString &path, const QString &root) {
	const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
	if (canonicalRoot.isEmpty()) {
		return false;
	}
	return under(canonicalTarget(path), canonicalRoot);
}

bool fileChooserPathWithinDisk(const QString &path, const QString &root,
		const QStringList &realRoots, bool browsing) {
	if (realRoots.isEmpty()) {
		return fileChooserPathWithinDrive(path, root);
	}
	const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
	if (canonicalRoot.isEmpty() || canonicalRoot == "/" ||
			!under(QDir::cleanPath(QFileInfo(path).absoluteFilePath()), canonicalRoot)) {
		return false;
	}
	const QString target = canonicalTarget(path);
	if (under(target, canonicalRoot)) {
		return browsing;
	}
	return std::any_of(realRoots.begin(), realRoots.end(),
		[&target](const QString &real) { return under(target, real); });
}

QString fileChooserDroppedFolder(const QList<QUrl> &urls, const QString &root,
		const QStringList &realRoots) {
	if (urls.size() != 1 || !urls.front().isLocalFile()) {
		return {};
	}
	const QString path = QFileInfo(urls.front().toLocalFile()).canonicalFilePath();
	if (path.isEmpty() || !QFileInfo(path).isDir() ||
			!fileChooserPathWithinDisk(path, root, realRoots, true)) {
		return {};
	}
	return path;
}

QString fileChooserRealPath(const QString &path) {
	const QString target = canonicalTarget(path);
	return target.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : target;
}

static QString lastFoldersPath() {
	QString state = qEnvironmentVariable("XDG_STATE_HOME");
	if (state.isEmpty()) {
		state = QDir::homePath() + "/.local/state";
	}
	return state + "/zacos9/open-save-folders";
}

/* "app id<TAB>folder" lines, newest first. */
static QStringList lastFolders() {
	QFile f(lastFoldersPath());
	return f.open(QIODevice::ReadOnly) ?
		QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts) : QStringList();
}

static QString recentFoldersPath() {
	QString state = qEnvironmentVariable("XDG_STATE_HOME");
	if (state.isEmpty()) {
		state = QDir::homePath() + "/.local/state";
	}
	return state + "/zacos9/open-save-recent";
}

static QStringList recentFolders() {
	QFile f(recentFoldersPath());
	if (!f.open(QIODevice::ReadOnly)) {
		if (f.exists()) {
			qWarning() << "Could not read Open/Save recent folders:" << f.errorString();
		}
		return {};
	}
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(f.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
		qWarning() << "Could not parse Open/Save recent folders:"
			<< parseError.errorString();
		return {};
	}
	QStringList folders;
	for (const QJsonValue &value : document.array()) {
		if (!value.isString()) {
			continue;
		}
		const QString path = value.toString();
		if (QFileInfo(path).isDir() && !folders.contains(path)) {
			folders << path;
			if (folders.size() == 10) {
				break;
			}
		}
	}
	return folders;
}

QStringList fileChooserRecentFolders() {
	return recentFolders();
}

QStringList fileChooserRecentFolders(const FileChooserLocation &location) {
	const QString root = QFileInfo(location.path).canonicalFilePath();
	if (root.isEmpty()) {
		return {};
	}
	QStringList folders;
	for (const QString &path : recentFolders()) {
		if (QFileInfo(path).isDir() &&
				fileChooserPathWithinDisk(path, root, location.realRoots, true)) {
			folders << path;
		}
	}
	return folders;
}

void fileChooserRememberRecentFolder(const QString &folder) {
	const QString path = QFileInfo(folder).canonicalFilePath();
	if (path.isEmpty() || !QFileInfo(path).isDir()) {
		return;
	}
	QJsonArray folders{ path };
	for (const QString &recent : fileChooserRecentFolders()) {
		if (recent != path && folders.size() < 10) {
			folders << recent;
		}
	}
	const QString filePath = recentFoldersPath();
	if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
		qWarning() << "Could not create the Open/Save recent-folders directory.";
		return;
	}
	QSaveFile f(filePath);
	const QByteArray contents = QJsonDocument(folders).toJson();
	if (!f.open(QIODevice::WriteOnly) || f.write(contents) != contents.size() || !f.commit()) {
		qWarning() << "Could not save Open/Save recent folders.";
	}
}

QString fileChooserStartFolder(const QString &appId) {
	char mode[32];
	if (!pl_setting("documents-folder", mode, sizeof(mode))) {
		return {};
	}
	if (strcmp(mode, "documents") == 0) {
		const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
		return QDir().mkpath(documents) ? documents : QString();
	}
	if (strcmp(mode, "last") == 0) {
		for (const QString &line : lastFolders()) {
			const int tab = line.indexOf('\t');
			if (tab >= 0 && line.left(tab) == appId) {
				const QString folder = line.mid(tab + 1);
				return QFileInfo(folder).isDir() ? folder : QString();
			}
		}
	}
	return {};
}

void fileChooserRememberFolder(const QString &appId, const QString &folder) {
	if (appId.contains('\t') || appId.contains('\n') || folder.contains('\n') ||
			!QFileInfo(folder).isDir()) {
		return;
	}
	QStringList lines{ appId + '\t' + folder };
	for (const QString &line : lastFolders()) {
		if (!line.startsWith(appId + '\t') && lines.size() < 100) {
			lines << line;
		}
	}
	const QString path = lastFoldersPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write((lines.join('\n') + '\n').toUtf8());
	}
}

/* The startup disk's link that reaches deepest toward `real`. */
static void findLink(const QString &dir, const QString &real, int depth, QString *best,
		qsizetype *bestLength) {
	if (depth > 8) {
		return;
	}
	for (const QFileInfo &entry : QDir(dir).entryInfoList(
			QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
		if (entry.isSymLink()) {
			const QString target = entry.canonicalFilePath();
			if (under(real, target) && target.size() > *bestLength) {
				*bestLength = target.size();
				*best = QDir::cleanPath(QDir(entry.absoluteFilePath()).filePath(
					QDir(target).relativeFilePath(real)));
			}
		} else if (entry.isDir()) {
			findLink(entry.absoluteFilePath(), real, depth + 1, best, bestLength);
		}
	}
}

FileChooserLocation fileChooserLocationFor(const QList<FileChooserLocation> &locations,
		const QString &folder, QString *browse) {
	const QString real = QFileInfo(folder).canonicalFilePath();
	if (real.isEmpty()) {
		return {};
	}
	/* A disk of its own first (the deepest), then the startup disk's links. */
	const FileChooserLocation *found = nullptr;
	for (const FileChooserLocation &location : locations) {
		if (!location.folderAlias && location.realRoots.isEmpty() &&
				fileChooserPathWithinDrive(real, location.path) &&
				(!found || location.path.size() > found->path.size())) {
			found = &location;
		}
	}
	if (found) {
		*browse = real;
		return *found;
	}
	for (const FileChooserLocation &location : locations) {
		if (!location.realRoots.isEmpty()) {
			QString path;
			qsizetype length = 0;
			findLink(location.path, real, 0, &path, &length);
			if (!path.isEmpty()) {
				*browse = path;
				return location;
			}
		}
	}
	return {};
}

QStringList fileChooserRecentItems() {
	char v[16];
	const int max = pl_setting("recent-documents", v, sizeof(v)) ? std::clamp(atoi(v), 0, 50) : 10;
	struct Recent {
		QString path;
		qint64 when;
	};
	QList<Recent> recent;
	char *xbel = g_build_filename(g_get_user_data_dir(), "recently-used.xbel", nullptr);
	GBookmarkFile *bookmarks = g_bookmark_file_new();
	if (g_bookmark_file_load_from_file(bookmarks, xbel, nullptr)) {
		gsize n = 0;
		char **uris = g_bookmark_file_get_uris(bookmarks, &n);
		for (gsize i = 0; i < n; i++) {
			char *file = g_filename_from_uri(uris[i], nullptr, nullptr);
			if (file && g_file_test(file, G_FILE_TEST_EXISTS)) {
				GDateTime *modified = g_bookmark_file_get_modified_date_time(bookmarks, uris[i], nullptr);
				recent << Recent{ QFile::decodeName(file), modified ? g_date_time_to_unix(modified) : 0 };
			}
			g_free(file);
		}
		g_strfreev(uris);
	}
	g_bookmark_file_free(bookmarks);
	g_free(xbel);
	std::stable_sort(recent.begin(), recent.end(),
		[](const Recent &a, const Recent &b) { return a.when > b.when; });
	QStringList paths;
	for (const Recent &r : recent) {
		if (paths.size() < max) {
			paths << r.path;
		}
	}
	return paths;
}

/* Removes a chooser tree without following its links into real folders. */
static void removeChooserTree(const QString &path) {
	const QFileInfo info(path);
	if (info.isSymLink() || !info.isDir()) {
		QFile::remove(path);
		return;
	}
	QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	for (const QFileInfo &entry : QDir(path).entryInfoList(
			QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
		removeChooserTree(entry.absoluteFilePath());
	}
	QDir().rmdir(path);
}

static void addStartupDiskItems(const QString &virtualPath, const QString &folder,
		QStringList *realRoots, QList<QString> *readOnly, int depth) {
	if (depth > 8) {
		return;
	}
	for (const auto &item : vfsList(virtualPath)) {
		const VNode *node = vfsNode(item->path);
		QString name = item->name;
		name.replace('/', ':');
		if (!node || name.isEmpty() || name == "." || name == ".." ||
				QFileInfo(QDir(folder).filePath(name)).exists() ||
				QFileInfo(QDir(folder).filePath(name)).isSymLink()) {
			continue;
		}
		const QString entry = QDir(folder).filePath(name);
		const bool application = node->kind == VKind::Launcher && !node->appId.isEmpty() &&
			node->actionId.isEmpty();
		if (node->kind == VKind::Backed || node->kind == VKind::Fonts || application) {
			const QString real = QFileInfo(vfsRealCounterpart(item->path)).canonicalFilePath();
			if (!real.isEmpty() && real != "/" && QFile::link(real, entry)) {
				*realRoots << real;
			}
		} else if (node->kind == VKind::Folder || node->kind == VKind::Apps ||
				node->kind == VKind::AppFolder) {
			if (QDir().mkdir(entry)) {
				addStartupDiskItems(item->path, entry, realRoots, readOnly, depth + 1);
				*readOnly << entry;
			}
		}
	}
}

void fileChooserWarmUp() {
	g_warming = true;
	vfsList(vfsRoot());
	g_warming = false;
}

FileChooserLocation fileChooserStartupDisk(const QString &parent) {
	removeChooserTree(parent);
	FileChooserLocation disk;
	disk.name = vfsVolumeName();
	disk.name.replace('/', ':');
	const QString root = QDir(parent).filePath(disk.name);
	if (!QDir().mkpath(root)) {
		qWarning() << "Could not create the chooser's startup disk at" << root;
		return disk;
	}
	QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	QList<QString> readOnly;
	addStartupDiskItems(vfsRoot(), root, &disk.realRoots, &readOnly, 0);
	readOnly << root;
	/* Only the real folders behind the links accept new files. */
	for (const QString &folder : readOnly) {
		QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
	}
	disk.realRoots.removeDuplicates();
	disk.path = QFileInfo(root).canonicalFilePath();
	return disk;
}

void prepareFileChooserDialog(QFileDialog &dialog) {
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	dialog.setSidebarUrls({});
	dialog.setViewMode(QFileDialog::Detail);
	dialog.setProperty("zacos9-file-portal-dialog", true);
	if (QWidget *sidebar = dialog.findChild<QWidget *>(QStringLiteral("sidebar"))) {
		sidebar->hide();
	}
}

QList<FileChooserLocation> fileChooserNetworkLocations(
		const QByteArray &mountInfo, const QString &runtime) {
	QList<FileChooserLocation> locations;
	QSet<QString> seen;
	for (const QByteArray &line : mountInfo.split('\n')) {
		const QList<QByteArray> fields = line.split(' ');
		const int separator = fields.indexOf("-");
		if (separator < 6 || separator + 2 >= fields.size()) {
			continue;
		}
		const QByteArray type = fields.at(separator + 1);
		if (type != "fuse.afp" && type != "cifs" && type != "smb3") {
			continue;
		}
		const QString path = netVolumeMountInfoPath(fields.at(4));
		if (path.isEmpty() || path == "/" || seen.contains(path)) {
			continue;
		}
		seen.insert(path);
		locations << FileChooserLocation{ QFileInfo(path).fileName(), path, false, {} };
	}
	/* GIO roots for SMB are URLs; their local paths live under GVFS. */
	if (!runtime.isEmpty()) {
		static const QRegularExpression shareName("^smb-share:server=([^,]+),share=([^,]+)");
		for (const QFileInfo &entry : QDir(runtime + "/gvfs").entryInfoList(
				QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			const auto match = shareName.match(entry.fileName());
			if (match.hasMatch() && !seen.contains(entry.absoluteFilePath())) {
				seen.insert(entry.absoluteFilePath());
				locations << FileChooserLocation{
					match.captured(2), entry.absoluteFilePath(), false, {} };
			}
		}
	}
	return locations;
}

QList<FileChooserLocation> fileChooserLocations() {
	QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if (runtime.isEmpty()) {
		runtime = QDir::tempPath();
	}
	const FileChooserLocation startup = fileChooserStartupDisk(
		QDir(runtime).filePath(QStringLiteral("zacos9-file-chooser")));
	QList<FileChooserLocation> locations = { startup };
	QSet<QString> seen{ QDir::cleanPath(startup.path), QStringLiteral("/") };
	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *mounts = g_volume_monitor_get_mounts(monitor);
	for (GList *item = mounts; item; item = item->next) {
		auto *mount = G_MOUNT(item->data);
		GFile *root = g_mount_get_root(mount);
		char *rawPath = g_file_get_path(root);
		char *rawName = g_mount_get_name(mount);
		const QString path = rawPath ? QDir::cleanPath(QFile::decodeName(rawPath)) : QString();
		if (!path.isEmpty() && !seen.contains(path) && !hiddenSystemMount(path)) {
			seen.insert(path);
			const QString name = rawName && *rawName ? QString::fromUtf8(rawName) :
				QFileInfo(path).fileName();
			locations << FileChooserLocation{ name, path, false, {} };
		}
		g_free(rawPath);
		g_free(rawName);
		g_object_unref(root);
	}
	g_list_free_full(mounts, g_object_unref);
	g_object_unref(monitor);
	for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
		const QString path = QDir::cleanPath(volume.rootPath());
		if (!volume.isValid() || !volume.isReady() || seen.contains(path) ||
				(volume.fileSystemType() != "fuse.afp" && !path.startsWith("/run/media/"))) {
			continue;
		}
		seen.insert(path);
		const QString name = volume.name().isEmpty() ? QFileInfo(path).fileName() : volume.name();
		locations << FileChooserLocation{ name, path, false, {} };
	}
	QSet<QString> mounted;
	QByteArray mountInfo;
	QFile mountTable("/proc/self/mountinfo");
	if (!mountTable.open(QIODevice::ReadOnly)) {
		qWarning() << "Could not read chooser mount table:" << mountTable.errorString();
	} else {
		mountInfo = mountTable.readAll();
		for (const QByteArray &line : mountInfo.split('\n')) {
			const QList<QByteArray> fields = line.split(' ');
			if (fields.size() > 5) {
				mounted.insert(netVolumeMountInfoPath(fields.at(4)));
			}
		}
	}
	for (const FileChooserLocation &network : fileChooserNetworkLocations(mountInfo, runtime)) {
		if (!seen.contains(network.path)) {
			seen.insert(network.path);
			locations << network;
		}
	}
	const QDir records("/run/media/zacos9-mac/" + QString::number(getuid()));
	for (const QString &file : records.entryList({ "*.json" }, QDir::Files | QDir::NoSymLinks)) {
		QFile record(records.filePath(file));
		if (!record.open(QIODevice::ReadOnly)) {
			qWarning() << "Could not read chooser Mac mount record:" << record.errorString();
			continue;
		}
		const QJsonObject info = QJsonDocument::fromJson(record.readAll()).object();
		const QString container = info.value("path").toString();
		if (container != records.filePath(file.chopped(5))) {
			continue;
		}
		for (const QJsonValue &value : info.value("volumes").toArray()) {
			const QJsonObject volume = value.toObject();
			const QString path = volume.value("path").toString();
			const QString name = volume.value("name").toString();
			if (!mounted.contains(path) || QFileInfo(path).absolutePath() != container ||
					name.isEmpty()) {
				continue;
			}
			bool updated = false;
			for (FileChooserLocation &location : locations) {
				if (location.path == path) {
					location.name = name;
					updated = true;
					break;
				}
			}
			if (!updated) {
				locations << FileChooserLocation{ name, path, false, {} };
				seen.insert(path);
			}
		}
	}
	std::sort(locations.begin() + 1, locations.end(),
		[](const FileChooserLocation &a, const FileChooserLocation &b) {
			return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
		});
	for (int i = 1; i < locations.size(); i++) {
		locations[i].usb = !localVolumeUsbDevice(locations[i].path).isEmpty();
	}
	/* Folder aliases on the desktop, then in Favorites (File > Add To
	 * Favorites: Mac OS 9's Open and Save dialogs listed them too). */
	QSet<QString> aliasTargets;
	const QString favorites = favoritesPath();
	for (const QString &aliasFolder : { QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
			favorites }) {
		for (const QFileInfo &entry : QDir(aliasFolder).entryInfoList(
				QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
			if (!entry.isSymLink()) {
				continue;
			}
			const AliasResolution resolved = aliasResolve(entry.absoluteFilePath());
			if (!resolved.target.isEmpty() && !QFileInfo(resolved.target).isDir()) {
				continue;
			}
			if (aliasFolder == favorites) {
				if (!resolved.target.isEmpty() && aliasTargets.contains(resolved.target)) {
					continue; /* a Favorite already listed from the desktop */
				}
			} else {
				aliasTargets.insert(resolved.target);
			}
			FileChooserLocation alias;
			alias.name = entry.fileName();
			alias.path = entry.absoluteFilePath();
			alias.folderAlias = true;
			alias.error = resolved.error;
			if (alias.error.isEmpty() && resolved.state != AliasState::Ok &&
					resolved.state != AliasState::Reconnected) {
				alias.error = aliasProblemText(resolved.state);
			}
			if (alias.error.isEmpty()) {
				locations[0].realRoots << QFileInfo(resolved.target).canonicalFilePath();
			}
			locations << alias;
		}
	}
	locations[0].realRoots.removeDuplicates();
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
