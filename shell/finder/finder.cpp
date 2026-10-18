#include "finder.h"

#include <sys/xattr.h>

#include "clippings.h"
#include "cryptofiles.h"
#include "desktopprinter.h"
#include "printers.h"
#include "printwindow.h"
#include "findindex.h"
#include "platinumshell.h"
#include "settings.h"

#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QProgressDialog>
#include <QProcess>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <QWidget>
#include <QTimer>

#include <QDirIterator>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>

#include "appdb.h"
#include "apptrash.h"
#include "../store/storeclient.h"
#include <QScopedValueRollback>
#include "fileops.h"
#include "alias.h"
#include <QFileDialog>
#include "localvolumes.h"
#include "diskinit.h"
#include "netvolumes.h"
#include "vfs.h"

#include "alert.h"
#include "desktop.h"
#include "findwindow.h"
#include "folderpath.h"
#include "foldertricks.h"
#include "folderwindow.h"
#include "toybox.h"
#include "viewoptionsdialog.h"
#include "infowindow.h"
#include "resourcewindows.h"

QStringList webSearchChannels() {
	return { "DuckDuckGo", "Google", "Bing" };
}

bool openWebSearch(const QString &channel, const QString &words) {
	const QString baseUrl = channel == "Google" ? "https://www.google.com/search"
		: channel == "Bing" ? "https://www.bing.com/search"
		: "https://duckduckgo.com/";
	QUrl url(baseUrl);
	QUrlQuery query;
	query.addQueryItem("q", words);
	url.setQuery(query);
	if (!appOpenUrl(url.toString(QUrl::FullyEncoded))) {
		Alert::ask("The web search could not be opened. Check that a web browser is available.",
			"OK", QString());
		return false;
	}
	return true;
}

/* Printing's messages don't stop you (Mac OS 9's didn't either). */
static void printProblem(const QString &message) {
	auto *alert = new Alert(message, "OK", QString());
	alert->setAttribute(Qt::WA_DeleteOnClose);
	alert->open();
}

Finder &Finder::instance() {
	static Finder finder;
	return finder;
}

QString Finder::socketPath() {
	QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
	QString display = qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
	return runtime + "/zacos9-finder." + display + ".sock";
}

/* Spring-loaded folders: on or off, and the delay (Mac OS 8's default
 * "medium", 900 ms; TODO: measure), from Preferences > General. */
void Finder::springHover(const QString &folder) {
	if (folder == m_springPath || (!folder.isEmpty() && !springLoadedFolders())) {
		return;
	}
	m_springPath = folder;
	if (!m_springTimer) {
		m_springTimer = new QTimer;
		m_springTimer->setSingleShot(true);
		QObject::connect(m_springTimer, &QTimer::timeout, [this] { springNow(); });
	}
	if (folder.isEmpty()) {
		m_springTimer->stop();
	} else {
		m_springTimer->start(springDelayMs());
	}
}

void Finder::springNow() {
	springOpenNow();
}

bool Finder::springOpenNow() {
	if (m_springPath.isEmpty() || !springLoadedFolders()) {
		return false;
	}
	if (m_springTimer) {
		m_springTimer->stop();
	}
	const QString folder = m_springPath;
	m_springPath.clear();
	const bool wasOpen = FolderWindow::isOpen(folder);
	FolderWindow *window = FolderWindow::open(folder);
	if (!wasOpen) {
		m_sprung.push_back(window);
	}
	return true;
}

void Finder::dragEnded() {
	springHover(QString());
	for (auto &w : m_sprung) {
		if (w) {
			w->close();
		}
	}
	m_sprung.clear();
}

void Finder::start(Desktop *desktop) {
	m_desktop = desktop;
	m_front = desktop;
	platinumOnDragSpring([this] { springNow(); });
	FolderTricks::instance(); /* watches the folders with tricks attached */
	QLocalServer::removeServer(socketPath());
	m_server.listen(socketPath());
	QObject::connect(&m_server, &QLocalServer::newConnection, [this] {
		while (QLocalSocket *s = m_server.nextPendingConnection()) {
			m_clients.push_back(s);
			const auto receive = [this, s] {
				while (s->canReadLine()) {
					QString line = QString::fromUtf8(s->readLine()).trimmed();
					if (line.startsWith("cmd ")) {
						command(line.mid(4));
					} else if (line.startsWith("afp-disconnected ")) {
						const QByteArray encoded = line.mid(17).toLatin1();
						const QByteArray bytes = QByteArray::fromHex(encoded);
						const QString path = QFile::decodeName(bytes);
						if (encoded.isEmpty() || bytes.toHex() != encoded ||
								bytes.contains('\0') || !QDir::isAbsolutePath(path)) {
							qWarning() << "Invalid AFP disconnection notification";
							continue;
						}
						if (m_desktop) {
							m_desktop->networkDisconnected(path);
						}
						const QString message = QStringLiteral(
							"The server disk \"%1\" has been disconnected. "
							"Connect to the server again to use it. "
							"Unsaved changes may have been lost.").arg(QFileInfo(path).fileName());
						auto *alert = new Alert(message, "OK", QString());
						alert->setAttribute(Qt::WA_DeleteOnClose);
						alert->setWindowTitle("Server Disconnected");
						alert->setAccessibleDescription(message);
						alert->open();
					}
				}
			};
			QObject::connect(s, &QLocalSocket::readyRead, receive);
			QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
			s->write((stateLine() + "\n").toUtf8());
			receive();
		}
	});
}

void Finder::replaceDesktop(Desktop *desktop) {
	if (m_front == m_desktop) {
		m_front = desktop;
	}
	m_desktop = desktop;
	notifyState();
}

FinderView *Finder::front() {
	return m_front ? m_front : m_desktop;
}

void Finder::setFront(FinderView *view) {
	if (m_front != view) {
		m_front = view;
		notifyState();
	}
}

void Finder::viewClosed(FinderView *view) {
	if (m_front == view) {
		m_front = m_desktop;
		notifyState();
	}
}

void Finder::closeAllWindows() {
	QList<QPointer<QWidget>> windows;
	for (QWidget *window : QApplication::topLevelWidgets()) {
		windows.append(window);
	}
	QWidget *desktop = m_desktop ? m_desktop->widget() : nullptr;
	for (const QPointer<QWidget> &window : windows) {
		if (window && window != desktop && window->windowType() == Qt::Window) {
			window->close();
		}
	}
}

/* A document File > Print can send somewhere: not a folder, a printer or
 * one of the Macintosh view's items. */
static bool isPrintable(const Item *item) {
	return !item->isVirtual && !item->isDir && item->kind != PL_ICON_PRINTER;
}

QString Finder::stateLine() {
	FinderView *v = front();
	int selection = v ? static_cast<int>(v->selectedItems().size()) : 0;
	bool window = v && v != m_desktop;
	QDir trash(trashFilesPath());
	bool full = trash.exists() && !trash.isEmpty();
	auto *fw = dynamic_cast<FolderWindow *>(v);
	int list = !fw ? 0 : fw->viewMode() == FolderWindow::ViewMode::List ? 1
		: fw->viewMode() == FolderWindow::ViewMode::Buttons ? 2 : 0;
	int label = -1;
	if (v) {
		for (Item *item : v->selectedItems()) {
			if (label == -1) {
				label = item->labelIndex;
			} else if (label != item->labelIndex) {
				label = -1;
				break;
			}
		}
	}
	const auto items = v ? v->selectedItems() : std::vector<Item *>{};
	const bool erase = items.size() == 1 && items[0]->isLocalVolume &&
		diskCanEraseVolume(items[0]->path);
	/* Any selected disk, for Special > Eject. */
	const bool network = std::any_of(items.begin(), items.end(),
		[](const Item *i) { return i->isNetworkVolume; });
	const bool unmount = std::any_of(items.begin(), items.end(),
		[](const Item *i) { return i->isLocalVolume; });
	/* View > Sort List: the list's sort column and the columns shown. */
	const int sort = fw ? fw->sortColumn() : 0;
	const bool custom = fw && fw->columnsCustomized();
	unsigned columns = 0;
	if (fw) {
		for (int c : fw->columns()) {
			columns |= columnBit(c);
		}
	}
	/* View > as Window / as Pop-up Window, where zacos9-wm has pop-ups. */
	const bool popup = fw && fw->isPopup();
	/* Documents File > Print would print, for the contextual menu's Print. */
	const bool documents = std::any_of(items.begin(), items.end(), isPrintable);
	return QStringLiteral("state selection=%1 window=%2 trash=%3 view=%4 label=%5 erase=%6 network=%7 unmount=%8 sort=%9 columns=%10 custom=%11 popup=%12 popups=%13 documents=%14")
		.arg(selection).arg(window ? 1 : 0).arg(full ? 1 : 0).arg(list).arg(label).arg(erase ? 1 : 0)
		.arg(network ? 1 : 0).arg(unmount ? 1 : 0).arg(sort).arg(columns).arg(custom ? 1 : 0)
		.arg(popup ? 1 : 0).arg(platinumHasPopupWindows() ? 1 : 0).arg(documents ? 1 : 0);
}

void Finder::notifyState() {
	QString line = stateLine();
	if (line == m_lastState) {
		return;
	}
	m_lastState = line;
	for (auto &c : m_clients) {
		if (c) {
			c->write((line + "\n").toUtf8());
		}
	}
}

void Finder::command(const QString &name) {
	if (!pl_puppy_allows(name.toUtf8().constData())) {
		pl_beep(); /* Puppy Mode leaves it out (Sniffer Preferences) */
		return;
	}
	if (name.startsWith("force-quit ")) {
		/* "force-quit APP-ID NAME", from the Apple menu: Mac OS's own
		 * question first, as the menu bar can't ask it. */
		const QString rest = name.mid(11);
		const QString appId = rest.section(' ', 0, 0);
		const QString app = rest.section(' ', 1).trimmed();
		if (!appId.isEmpty() && Alert::ask("Force “" + (app.isEmpty() ? appId : app) +
				"” to quit? Any unsaved changes in it will be lost.", "Force Quit")) {
			if (!platinumForceQuit(appId)) {
				Alert::ask("“" + app + "” couldn't be forced to quit.", "OK", QString());
			}
		}
	} else if (name == "add-to-favorites") {
		addToFavorites();
	} else if (name.startsWith("attach-trick ")) {
		const QString trick = name.mid(13);
		if (m_trickFolder.isEmpty() || !FolderTricks::instance().attach(m_trickFolder, trick)) {
			Alert::ask("“" + trick + "” couldn't be attached to “" +
				displayName(m_trickFolder) + "”.", "OK", QString());
		}
	} else if (name.startsWith("remove-trick ")) {
		FolderTricks::instance().remove(m_trickFolder, name.mid(13));
	} else if (name == "open-folder-tricks") {
		const QString dir = FolderTricks::instance().tricksDir();
		QDir().mkpath(dir);
		openFolder(dir);
	} else if (name.startsWith("contextual ")) {
		bool ok = false;
		const int index = name.mid(11).toInt(&ok);
		QString error;
		if (ok && index >= 0 && index < m_contextCommands.size() &&
				!runContextCommand(m_contextCommands[index], m_contextPaths, &error)) {
			Alert::ask(error, "OK", QString());
		}
	} else if (name.startsWith("open-path ")) {
		openPath(name.mid(10));
	} else if (name.startsWith("reveal-path ")) {
		/* zacos9-finder-ctl: the folder it's in, opened, with it selected. */
		const QFileInfo info(name.mid(12));
		if (info.exists() || info.isSymLink()) {
			FolderWindow::open(info.absolutePath())->selectByName(info.fileName());
		}
	} else if (name.startsWith("label-path ")) {
		/* zacos9-finder-ctl: "label-path N<TAB>PATH". */
		const QString rest = name.mid(11);
		const int label = rest.section('\t', 0, 0).toInt();
		const QString path = rest.section('\t', 1);
		if (label >= 0 && label < PL_LABEL_COUNT && QFileInfo::exists(path) && writeLabel(path, label)) {
			folderChanged(QFileInfo(path).absolutePath());
		}
	} else if (name.startsWith("comment-path ")) {
		/* zacos9-finder-ctl: "comment-path PATH<TAB>TEXT" (Get Info's comment). */
		const QString rest = name.mid(13);
		const QByteArray path = QFile::encodeName(rest.section('\t', 0, 0));
		const QByteArray text = rest.section('\t', 1).toUtf8();
		if (QFileInfo::exists(QString::fromUtf8(path))) {
			if (text.isEmpty()) {
				removexattr(path.constData(), "user.xdg.comment");
			} else {
				setxattr(path.constData(), "user.xdg.comment", text.constData(), text.size(), 0);
			}
			folderChanged(QFileInfo(QString::fromUtf8(path)).absolutePath());
		}
	} else if (name.startsWith("path-menu ")) {
		/* The folder-path menu's choice; the first is the window's own. */
		const int i = name.mid(10).toInt();
		if (i > 0 && i < m_pathMenu.folders.size()) {
			QPointer<QWidget> from = m_pathMenu.from;
			openFolder(m_pathMenu.folders[i]);
			if (m_pathMenu.option && from) {
				from->close();
			}
		}
	} else if (name == "eject") {
		ejectSelection();
	} else if (name == "log-out") {
		logOut();
	} else if (name == "log-out-cancelled") {
		Alert::ask("Log Out was cancelled because a program didn't quit.", "OK", QString());
	} else if (name == "new-folder") {
		newFolder();
	} else if (name == "open") {
		openSelection();
	} else if (name == "close-window") {
		closeWindow();
	} else if (name == "move-to-trash") {
		moveSelectionToTrash();
	} else if (name == "empty-trash" || name == "empty-trash-option") {
		emptyTrash(name == "empty-trash-option");
	} else if (name == "erase-disk") {
		const auto items = front() ? front()->selectedItems() : std::vector<Item *>{};
		if (items.size() != 1 || !items[0]->isLocalVolume) {
			Alert::ask("Select one USB disk on the desktop to erase.", "OK", QString());
		} else {
			diskEraseVolume(items[0]->path, items[0]->name);
		}
	} else if (name == "get-info") {
		getInfo();
	} else if (name == "get-info-sharing") {
		getInfo(true);
	} else if (name.startsWith("info /")) {
		/* Get Info for a path (scripts and other apps). */
		const QString path = QDir::cleanPath(name.mid(5));
		InfoWindow::open(path, path == "/" ? PL_ICON_DISK
			: QFileInfo(path).isDir() ? PL_ICON_FOLDER : PL_ICON_DOCUMENT, displayName(path));
	} else if (name == "show-clipboard") {
		showClipboardWindow();
	} else if (name == "select-all") {
		if (FinderView *v = front()) {
			v->selectAll();
		}
	} else if (name == "duplicate") {
		duplicate();
	} else if (name == "make-alias") {
		makeAlias();
	} else if (name == "put-away") {
		putAway();
	} else if (name == "disconnect-network") {
		unmountSelection(true);
	} else if (name == "unmount-disk") {
		unmountSelection(false);
	} else if (name == "show-original") {
		showOriginal();
	} else if (name.startsWith("label ")) {
		setLabel(name.mid(6).toInt());
	} else if (name == "classic") {
		QStringList disks;
		for (Item *item : front()->selectedItems()) {
			if (item->kind == PL_ICON_DISK_IMAGE && !item->isDir) {
				disks << item->path;
			}
		}
		launchClassic(disks);
	} else if (name == "refresh") {
		/* The fallback when a desktop entry appears without the
		 * application directories changing in a way we can see. */
		vfsRefresh();
	} else if (name == "show-unix-volume") {
		vfsSetUnixVolumeShown(!vfsUnixVolumeShown());
	} else if (name == "show-hidden-applications") {
		vfsShowAllHidden();
	} else if (name == "print") {
		printSelection();
	} else if (name == "page-setup") {
		showPageSetup();
	} else if (name == "print-window") {
		printWindow();
	} else if (name == "encrypt") {
		encryptSelection();
	} else if (name == "decrypt") {
		decryptSelection();
	} else if (name == "find") {
		/* A folder window in front: offered as the place to look. */
		auto *folder = dynamic_cast<FolderWindow *>(front());
		FindDialog::open(folder && !vfsIsVirtual(folder->folderPath()) ? folder->folderPath() : QString());
	} else if (name == "search-internet") {
		bool accepted = false;
		const QStringList searchChannels = webSearchChannels();
		const QString channel = QInputDialog::getItem(nullptr, "Search Internet",
			"Search the web with:", searchChannels, 0, false, &accepted);
		if (!accepted) {
			return;
		}
		const QString words = QInputDialog::getText(nullptr, "Search Internet",
			"Search for:", QLineEdit::Normal, QString(), &accepted).trimmed();
		if (accepted && !words.isEmpty()) {
			openWebSearch(channel, words);
		}
	} else if (name == "find-similar") {
		const std::vector<Item *> sel = front() ? front()->selectedItems() : std::vector<Item *>{};
		if (sel.size() == 1 && !sel[0]->isVirtual && !sel[0]->isDir) {
			FoundWindow::similarTo(sel[0]->path);
		} else {
			Alert::ask("Select one document to find items similar to it.", "OK", QString());
		}
	} else if (name == "index-selection") {
		indexSelection();
	} else if (name == "about") {
		AboutWindow::open();
	} else if (name == "clean-up" || name.startsWith("arrange ")) {
		static const QHash<QString, Arrange> by = {
			{ "clean-up", Arrange::CleanUp }, { "arrange name", Arrange::Name },
			{ "arrange modified", Arrange::Modified }, { "arrange created", Arrange::Created },
			{ "arrange size", Arrange::Size }, { "arrange kind", Arrange::Kind },
			{ "arrange label", Arrange::Label },
		};
		if (by.contains(name)) {
			front()->arrange(by.value(name));
		}
	} else if (name == "view-options") {
		/* Its dialog runs its own event loop: not inside the socket's
		 * read loop, whose socket may go away meanwhile. */
		QPointer<QWidget> view = front() ? front()->widget() : nullptr;
		QTimer::singleShot(0, [this, view] {
			if (view && front() && front()->widget() == view) {
				showViewOptions(front());
			}
		});
	} else if (name == "toy-box") {
		ToyBox::open();
	} else if (name == "reset-columns") {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			fw->resetColumns();
		}
	} else if (name == "preferences") {
		showPreferences();
	} else if (name.startsWith("sort ")) {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			for (int c = 0; c < COL_COUNT; c++) {
				if (name.mid(5) == QLatin1String(columnInfo(c).key)) {
					fw->sortBy(c);
				}
			}
		}
	} else if (name == "view-icons" || name == "view-list" || name == "view-buttons") {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			fw->setViewMode(name == "view-list" ? FolderWindow::ViewMode::List
				: name == "view-buttons" ? FolderWindow::ViewMode::Buttons
				: FolderWindow::ViewMode::Icons);
		}
	} else if (name == "view-window" || name == "view-popup") {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			fw->setPopup(name == "view-popup");
		}
	}
}

/* "untitled folder", then "untitled folder 2", ... as the Finder names them. */
static QString uniqueName(const QDir &dir, const QString &base) {
	if (!dir.exists(base)) {
		return base;
	}
	for (int i = 2;; i++) {
		QString name = base + " " + QString::number(i);
		if (!dir.exists(name)) {
			return name;
		}
	}
}

void Finder::newFolder() {
	FinderView *v = front();
	if (v->folderPath() == vfsRoot()) {
		/* The startup disk takes folders of the user's own. */
		const QString path = vfsNewFolder();
		if (!path.isEmpty()) {
			v->selectByName(vfsName(path));
			notifyState();
		}
		return;
	}
	if (vfsIsVirtual(v->folderPath())) {
		Alert::ask("New folders can't be made here. Home, and the "
			"folders inside it, hold your own files.", "OK", QString());
		return;
	}
	QDir dir(v->folderPath());
	dir.mkpath(".");
	QString name = uniqueName(dir, "untitled folder");
	if (dir.mkdir(name)) {
		folderChanged(dir.absolutePath());
		v->selectByName(name);
		notifyState();
	} else {
		Alert::ask("The folder could not be created in " + dir.absolutePath() +
			". Check folder permissions.", "OK", QString());
	}
}

static bool isExpandable(const QString &path);
static void resolveAliasForUser(const QString &aliasPath,
		std::function<void(const QFileInfo &)> done);

void Finder::openItem(Item *item) {
	if (!item->isVirtual && item->isAlias) {
		const QString aliasPath = item->path;
		const QString name = item->visibleName();
		resolveAliasForUser(aliasPath, [name](const QFileInfo &target) {
			if (target.filePath().isEmpty()) {
				return;
			}
			const QString path = target.absoluteFilePath();
			if (path.endsWith(QLatin1String(".desktop"))) {
				if (!appLaunchFile(path)) {
					Alert::ask("“" + name + "” could not be opened. Its application may have been removed.",
						"OK", QString());
				}
				return;
			}
			auto resolved = makeItem(QFileInfo(path));
			if (resolved) {
				Finder::instance().openItem(resolved.get());
			}
		});
		return;
	}
	if (appTrashMarker(item->path)) {
		Alert::ask("This application is queued for uninstall when Trash is emptied. "
			"Choose Put Away to cancel removal and return it to Applications.", "OK", QString());
		return;
	}
	{
		if (item->isVirtual) {
			const VNode *node = vfsNode(item->path);
			if (node && node->kind == VKind::Launcher) {
				const QString action = vfsBuiltinAction(item->path);
				if (action == "system-information") {
					openSystemInformation();
				} else if (action == "extensions-manager") {
					openExtensionsManager();
				} else if (action == "toy-box") {
					ToyBox::open();
				} else if (!vfsLaunch(item->path)) {
					Alert::ask(item->visibleName() + " could not be opened. Its "
						"application may have been removed.", "OK", QString());
				}
				return;
			}
			if (node && node->kind == VKind::Resource) {
				if (node->resourceDirectory) {
					FolderWindow::open(item->path);
					return;
				}
				QString source;
				if (vfsResourceOpenPath(item->path, &source)) {
					if (!appOpenFile(source)) {
						Alert::ask(item->visibleName() + " could not be opened.", "OK", QString());
					}
					return;
				}
				QStringList metadata;
				QString kind, location, sources, access;
				vfsResourceDetails(item->path, &metadata, &kind, &location, &sources, &access);
				ResourceDetails details;
				details.kind = kind;
				details.location = location;
				details.sources = sources;
				details.access = access;
				details.metadata = metadata;
				InfoWindow::openVirtual(item->path, item->kind, item->name, details);
				return;
			}
			const QString real = vfsOpensAs(item->path);
			FolderWindow::open(real.isEmpty() ? item->path : real);
			return;
		}
		if (item->kind == PL_ICON_FOLDER || item->kind == PL_ICON_DISK ||
				item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			QDir().mkpath(item->path);
			FolderWindow::open(item->path);
		} else if (item->kind == PL_ICON_DISK_IMAGE) {
			mountMacImage(item->path);
		} else if (item->kind == PL_ICON_WINDOWS) {
			launchWindows(item->path);
		} else if (item->kind == PL_ICON_CLASSIC) {
			/* TODO: open the application itself inside the Mac. */
			launchClassic();
		} else if (isExpandable(item->path)) {
			expandArchive(item->path);
		} else if (item->kind == PL_ICON_CLIPPING) {
			openClippingWindow(item->path);
		} else if (item->kind == PL_ICON_PRINTER) {
			openPrintQueue(desktopPrinterQueue(item->path));
		} else if (item->kind == PL_ICON_INTERNET_LOCATION) {
			const QString url = internetLocationUrl(item->path);
			if (url.isEmpty() || !appOpenUrl(url)) {
				Alert::ask(item->visibleName() + " could not be opened." +
					(url.isEmpty() ? QString(" It holds no address.") : QString()), "OK", QString());
			}
		} else {
			if (!appOpenFile(item->path)) {
				Alert::ask(item->visibleName() + " could not be opened.", "OK", QString());
			}
		}
	}
}

bool Finder::closeApplicationWindow(Item *item) {
	if (!item || item->kind != PL_ICON_APPLICATION) {
		return false;
	}
	QString appId;
	if (item->isVirtual) {
		const VNode *node = vfsNode(item->path);
		if (node && node->kind == VKind::Launcher) {
			appId = node->appId;
		}
	} else if (const AppEntry *app = appByFile(item->path)) {
		appId = app->id;
	}
	if (appId.isEmpty()) {
		return false;
	}
	return platinumCloseFrontAppWindow(appId);
}

void Finder::mountMacImage(const QString &image) {
	auto *progress = new QProgressDialog("Mounting " + QFileInfo(image).fileName() + " read-only...",
		QString(), 0, 0);
	progress->setWindowTitle("Mounting Disk");
	progress->setCancelButton(nullptr);
	progress->setWindowModality(Qt::ApplicationModal);
	progress->setMinimumDuration(0);
	progress->show();
	platinumSetFrameStyle(progress, FrameStyle::MovableModal);
	localVolumeMountMacImage(image, [progress](const QString &path, const QString &error) {
		progress->hide();
		progress->deleteLater();
		if (!error.isEmpty()) {
			Alert::ask("The disk image could not be mounted. " + error, "OK", QString());
		} else {
			FolderWindow::open(path);
		}
	});
}

/* ---- archives ------------------------------------------------------------------------ */

static bool isExpandable(const QString &path) {
	const QString lower = path.toLower();
	for (const char *s : { ".zip", ".7z", ".sit", ".tar", ".tgz", ".tar.gz", ".tbz2", ".tar.bz2", ".txz", ".tar.xz" }) {
		if (lower.endsWith(QLatin1String(s))) {
			return true;
		}
	}
	return false;
}

void Finder::expandArchive(const QString &archive) {
	/* Beside us: installed in the same bin directory, or the build tree's copy. */
	const QString tool = QCoreApplication::applicationDirPath() + "/zacos9-expand";
	auto *progress = new QProgressDialog("Expanding “" + QFileInfo(archive).fileName() + "”…",
		QString(), 0, 0);
	progress->setWindowTitle("Expanding");
	progress->setCancelButton(nullptr);
	progress->setWindowModality(Qt::ApplicationModal);
	progress->setMinimumDuration(0);
	progress->setAutoClose(false);
	progress->setAutoReset(false);
	progress->show();
	platinumSetFrameStyle(progress, FrameStyle::MovableModal);
	auto *p = new QProcess;
	QObject::connect(p, &QProcess::finished, [this, p, progress, archive](int code, QProcess::ExitStatus status) {
		progress->hide();
		progress->deleteLater();
		p->deleteLater();
		const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
		const QString why = QString::fromUtf8(p->readAllStandardError()).trimmed().section('\n', -1);
		if (status != QProcess::NormalExit || code != 0 || out.isEmpty()) {
			Alert::ask("“" + QFileInfo(archive).fileName() + "” couldn’t be expanded. " +
				(why.isEmpty() ? QString("The expanding tool stopped.") : why), "OK", QString());
			return;
		}
		const QFileInfo result(out);
		const QString folder = result.absolutePath();
		folderChanged(folder);
		if (QDir(folder) == QDir(m_desktop->folderPath())) {
			m_desktop->selectByName(result.fileName());
		} else if (FolderWindow::isOpen(folder)) {
			FolderWindow::open(folder)->selectByName(result.fileName());
		}
	});
	QObject::connect(p, &QProcess::errorOccurred, [p, progress, archive](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			progress->hide();
			progress->deleteLater();
			p->deleteLater();
			Alert::ask("“" + QFileInfo(archive).fileName() + "” couldn’t be expanded: zacos9-expand "
				"isn't installed.", "OK", QString());
		}
	});
	p->start(tool, { archive });
}

void Finder::openSelection() {
	for (Item *item : front()->selectedItems()) {
		openItem(item);
	}
}

void Finder::copySelectionNames() {
	FinderView *view = front();
	if (!view) {
		return;
	}
	QStringList names;
	for (const Item *item : view->selectedItems()) {
		names.push_back(item->visibleName());
	}
	if (!names.isEmpty()) {
		QGuiApplication::clipboard()->setText(names.join('\n'));
	}
}

static QString classicLauncher() {
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-classic";
	return QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-classic");
}

/* Windows programs open in the Windows Installer (shell/wininstall), which
 * installs them, or runs one that needs no installing. */
void Finder::launchWindows(const QString &exe) {
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-wininstall";
	if (!platinumStartApplication(
			QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-wininstall"),
			QStringList{ exe })) {
		Alert::ask("The Windows Installer could not be opened.", "OK", QString());
	}
}

/* The printer to use: `queue`, else yours; with none chosen, says so
 * and offers Printer Picker. */
static QString printerFor(const QString &queue) {
	const QString printer = queue.isEmpty() ? defaultPrinter() : queue;
	if (printer.isEmpty() &&
			Alert::ask("No printer has been chosen. Choose one in Printer Picker, then print again.",
				"Printer Picker", "Cancel")) {
		platinumStartApplication(siblingProgram("zacos9-printerpicker"), {});
	}
	return printer;
}

void Finder::printSelection() {
	QStringList files;
	for (Item *item : front()->selectedItems()) {
		if (isPrintable(item)) {
			files << QFileInfo(item->path).canonicalFilePath(); /* through aliases */
		}
	}
	files.removeAll(QString());
	if (!files.isEmpty()) {
		printDocuments(files);
	}
}

void Finder::printWindow() {
	FinderView *v = front();
	auto *fw = dynamic_cast<FolderWindow *>(v);
	if (!v || (!fw && v != m_desktop)) {
		return;
	}
	const QString printer = printerFor(QString());
	if (printer.isEmpty()) {
		return;
	}
	const QString title = fw ? fw->windowTitle() : QStringLiteral("Desktop");
	const QImage image = fw ? fw->printImage() : m_desktop->printImage();
	if (image.isNull()) {
		printProblem("There's nothing in “" + title + "” to print.");
		return;
	}
	/* A PDF named for the window (the print job's name), gone once lp
	 * has taken it. */
	QTemporaryDir dir;
	const QString pdf = dir.path() + "/" + QString(title).replace('/', '-') + ".pdf";
	if (!dir.isValid() || writeWindowPdf(title, image, loadPageSetup(), pdf) == 0) {
		printProblem("“" + title + "” couldn't be prepared for printing.");
		return;
	}
	QString error;
	if (printFiles(printer, { pdf }, &error).isEmpty()) {
		printProblem("“" + title + "” couldn't be printed. " + error);
	}
}

static QString cryptoSelectionPath(FinderView *view) {
	if (!view) {
		return {};
	}
	const std::vector<Item *> selected = view->selectedItems();
	if (selected.size() != 1 || selected.front()->isVirtual || selected.front()->isDir ||
			selected.front()->kind == PL_ICON_DISK || selected.front()->kind == PL_ICON_PRINTER) {
		Alert::ask("Select one regular file.", "OK", QString());
		return {};
	}
	const QString path = QFileInfo(selected.front()->path).canonicalFilePath();
	if (path.isEmpty() || !QFileInfo(path).isFile() || !QFileInfo(path).isReadable()) {
		Alert::ask("The selected file is not readable.", "OK", QString());
		return {};
	}
	return path;
}

static QString cryptoPassword(bool confirm) {
	bool accepted = false;
	QString password = QInputDialog::getText(nullptr, confirm ? "Encrypt File" : "Decrypt File",
		confirm ? "Enter a passphrase (at least 8 characters):" : "Enter the passphrase:",
		QLineEdit::Password, QString(), &accepted);
	if (!accepted) {
		return {};
	}
	if (password.contains('\n') || password.contains('\r') ||
			(confirm && password.size() < 8)) {
		Alert::ask(confirm
			? "Use a passphrase of at least 8 characters without line breaks."
			: "The passphrase cannot contain a line break.", "OK", QString());
		password.fill(QChar('\0'));
		return {};
	}
	if (confirm) {
		QString repeated = QInputDialog::getText(nullptr, "Encrypt File", "Re-enter the passphrase:",
			QLineEdit::Password, QString(), &accepted);
		if (!accepted || repeated != password) {
			password.fill(QChar('\0'));
			repeated.fill(QChar('\0'));
			if (accepted) {
				Alert::ask("The passphrases did not match.", "OK", QString());
			}
			return {};
		}
		repeated.fill(QChar('\0'));
	}
	return password;
}

static QProgressDialog *cryptoProgress(const QString &text) {
	auto *progress = new QProgressDialog(text, QString(), 0, 0);
	progress->setWindowTitle("File Encryption");
	progress->setCancelButton(nullptr);
	progress->setWindowModality(Qt::ApplicationModal);
	progress->setMinimumDuration(0);
	progress->show();
	platinumSetFrameStyle(progress, FrameStyle::MovableModal);
	return progress;
}

void Finder::encryptSelection() {
	const QString source = cryptoSelectionPath(front());
	if (source.isEmpty()) {
		return;
	}
	const QString destination = QFileDialog::getSaveFileName(nullptr, "Encrypt File",
		source + ".gpg", "GPG encrypted files (*.gpg);;All files (*)");
	if (destination.isEmpty()) {
		return;
	}
	if (QFileInfo(destination).absoluteFilePath() == source) {
		Alert::ask("The encrypted copy must be saved as a different file.", "OK", QString());
		return;
	}
	QString password = cryptoPassword(true);
	if (password.isEmpty()) {
		return;
	}
	auto *progress = cryptoProgress("Encrypting “" + QFileInfo(source).fileName() + "”…");
	encryptFile(source, destination, password, [progress, destination, password](bool ok,
			const QString &problem) mutable {
		progress->hide();
		progress->deleteLater();
		if (ok) {
			/* Mac OS 9's "Add to keychain": Decrypt then needn't ask. */
			if (Alert::ask("Encrypted copy saved to “" + QFileInfo(destination).fileName() +
					"”. Add its passphrase to your keychain, so Decrypt can open it without asking?",
					"Add to Keychain", "Don't Add")) {
				keychainStorePassphrase(destination, password, [](bool stored) {
					if (!stored) {
						Alert::ask("The passphrase couldn't be added to your keychain. "
							"Is the keychain unlocked?", "OK", QString());
					}
				});
			}
			password.fill(QChar('\0'));
		} else {
			Alert::ask("The file could not be encrypted. " + problem, "OK", QString());
		}
	});
	password.fill(QChar('\0'));
}

void Finder::decryptSelection() {
	const QString source = cryptoSelectionPath(front());
	if (source.isEmpty()) {
		return;
	}
	QString suggested = source;
	if (suggested.endsWith(".gpg", Qt::CaseInsensitive)) {
		suggested.chop(4);
	} else {
		suggested += ".decrypted";
	}
	const QString destination = QFileDialog::getSaveFileName(nullptr, "Decrypt File",
		suggested, "All files (*)");
	if (destination.isEmpty()) {
		return;
	}
	if (QFileInfo(destination).absoluteFilePath() == source) {
		Alert::ask("The decrypted file must be saved as a different file.", "OK", QString());
		return;
	}
	/* The keychain's passphrase if it has one, else ask. */
	keychainFindPassphrase(source, [source, destination](QString password) {
		if (password.isEmpty()) {
			password = cryptoPassword(false);
		}
		if (password.isEmpty()) {
			return;
		}
		auto *progress = cryptoProgress("Decrypting “" + QFileInfo(source).fileName() + "”…");
		decryptFile(source, destination, password, [progress, destination](bool ok, const QString &problem) {
			progress->hide();
			progress->deleteLater();
			if (ok) {
				Alert::ask("Decrypted file saved to “" + QFileInfo(destination).fileName() + "”.",
					"OK", QString());
			} else {
				Alert::ask("The file could not be decrypted. " + problem, "OK", QString());
			}
		});
		password.fill(QChar('\0'));
	});
}

void Finder::printDocuments(const QStringList &files, const QString &queue) {
	const QString printer = printerFor(queue);
	if (printer.isEmpty()) {
		return;
	}
	QStringList printable, others;
	for (const QString &f : files) {
		(canPrintDirectly(f) ? printable : others) << f;
	}
	QString error;
	if (!printable.isEmpty() && printFiles(printer, printable, &error).size() < printable.size()) {
		printProblem("Not everything could be printed. " + error);
	}
	QStringList failedToOpen;
	for (const QString &file : others) {
		if (!appOpenFile(file)) {
			failedToOpen << QFileInfo(file).fileName();
		}
	}
	if (!others.isEmpty() && failedToOpen.isEmpty()) {
		Alert::ask(others.size() == 1
			? QString::fromUtf8("“%1” is open in its application. Choose Print there to print it.")
				.arg(QFileInfo(others.first()).fileName())
			: QString("%1 documents are open in their applications. Choose Print in each application to print them.")
				.arg(others.size()), "OK", QString());
	} else if (!failedToOpen.isEmpty()) {
		Alert::ask("Some documents could not be opened in their applications: " +
			failedToOpen.join(", ") + ". " + (others.size() > failedToOpen.size()
				? "The others were opened; choose Print in their applications." : ""),
			"OK", QString());
	}
}

void Finder::openPrintQueue(const QString &queue) {
	if (!platinumStartApplication(siblingProgram("zacos9-printqueue"), queue.isEmpty() ? QStringList()
			: QStringList{ queue })) {
		Alert::ask("The print queue couldn't be opened.", "OK", QString());
	}
}

void Finder::launchClassic(const QStringList &disks) {
	QStringList args;
	for (const QString &d : disks) {
		args << "--disk" << d;
	}
	const uint32_t cookie = platinumBeginLaunch();
	auto *check = new QProcess(QCoreApplication::instance());
	auto *timeout = new QTimer(check);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, check, [check] {
		check->setProperty("launchTimedOut", true);
		check->kill();
	});
	QObject::connect(check, &QProcess::errorOccurred, check, [check, cookie](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			platinumCancelLaunch(cookie);
			Alert::ask("Old Dog could not be started: " + check->errorString(), "OK", QString());
			check->deleteLater();
		}
	});
	QObject::connect(check, &QProcess::finished, check,
		[check, timeout, args, cookie](int code, QProcess::ExitStatus status) {
			timeout->stop();
			platinumCancelLaunch(cookie);
			if (status != QProcess::NormalExit || code != 0) {
				QString why = QString::fromUtf8(check->readAllStandardError()).trimmed();
				if (why.isEmpty()) {
					why = check->property("launchTimedOut").toBool()
						? "Old Dog's startup check timed out." : "Old Dog could not be started.";
				}
				Alert::ask(why, "OK", QString());
			} else if (!platinumStartApplication(classicLauncher(), args)) {
				Alert::ask("Old Dog could not be started.", "OK", QString());
			}
			check->deleteLater();
		});
	check->start(classicLauncher(), QStringList{ "--check" } + args);
	timeout->start(15000);
}

static bool isSpecial(const Item *item) {
	/* Virtual items stand for the Finder's own structure or for
	 * applications dpkg owns: duplicating or aliasing them would mean
	 * copying package files about, so the Finder leaves them alone. */
	return item->isVirtual || item->kind == PL_ICON_DISK ||
		item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL;
}

/* "Report copy" next to "Report" (Mac OS 8 naming). */
void Finder::duplicate() {
	FinderView *v = front();
	QStringList paths;
	for (Item *item : v->selectedItems()) {
		if (!isSpecial(item)) {
			paths << item->path;
		}
	}
	if (paths.isEmpty()) {
		return;
	}
	const QString folder = QFileInfo(paths.first()).absolutePath();
	for (const QString &d : transferItems(paths, folder, true)) {
		folderChanged(d);
	}
}

/* Labels live in an extended attribute on the item itself (an alias's
 * own, not its original's). The disk and Trash can't be labelled. */
void Finder::setLabel(int label) {
	if (label < 0 || label >= PL_LABEL_COUNT) {
		return;
	}
	QSet<QString> folders;
	for (Item *item : front()->selectedItems()) {
		if (item->isVirtual) {
			/* Kept as metadata, so it survives package upgrades. */
			if (!vfsSetLabel(item->path, label)) {
				Alert::ask("Labels aren't available for this read-only system resource.",
					"OK", QString());
			}
			continue;
		}
		if (isSpecial(item)) {
			continue;
		}
		if (writeLabel(item->path, label)) {
			folders.insert(QFileInfo(item->path).absolutePath());
		}
	}
	for (const QString &folder : folders) {
		folderChanged(folder);
	}
}

/* An alias is a symbolic link named "<name> alias"; its name shows in
 * italics. */
void Finder::makeAlias() {
	FinderView *v = front();
	QString lastName;
	QString folder;
	const QString desktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	bool onDesktop = false;
	for (Item *item : v->selectedItems()) {
		if (item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			continue;
		}
		if (item->isVirtual) {
			/* Nothing can be put in the Macintosh view's own folders, so -
			 * as the Mac did when it couldn't write an alias beside its
			 * original - the alias goes on the desktop. An application's
			 * links to its desktop file; a folder standing for a real one,
			 * to that folder. */
			const VNode *node = vfsNode(item->path);
			const QString target = node && node->actionId.isEmpty()
				? vfsRealCounterpart(item->path) : QString();
			if (target.isEmpty()) {
				Alert::ask("An alias of “" + item->visibleName() + "” can't be made.", "OK", QString());
				continue;
			}
			QDir().mkpath(desktopDir);
			if (!createAlias(target, desktopDir, item->name).isEmpty()) {
				folderChanged(desktopDir);
				onDesktop = true;
			}
			continue;
		}
		QFileInfo info(item->path);
		folder = item->kind == PL_ICON_DISK ? v->folderPath() : info.absolutePath();
		const QString name = createAlias(item->path, folder,
			item->kind == PL_ICON_DISK ? item->name : info.fileName());
		if (!name.isEmpty()) {
			lastName = name;
		}
	}
	if (!lastName.isEmpty()) {
		folderChanged(folder);
		v->selectByName(lastName);
		notifyState();
	} else if (onDesktop) {
		notifyState();
	}
}

void Finder::ejectVolume(QString path, QString name, bool network) {
	if (!network) {
		for (const LocalVolume &volume : localVolumes()) {
			if (volume.path == path) {
				if (!localVolumeEject(volume)) {
					Alert::ask("The disk \"" + name + "\" could not be unmounted. "
						"It may no longer be mounted.", "OK", QString());
				}
				return;
			}
		}
	} else {
		for (const NetVolume &volume : netVolumes()) {
			if (volume.path == path) {
				QString error;
				if (!netVolumeEject(volume, &error)) {
					Alert::ask(error, "OK", QString());
				} else if (m_desktop) {
					m_desktop->networkDisconnected(path);
				}
				return;
			}
		}
	}
	Alert::ask("The disk \"" + name + "\" is no longer mounted.", "OK", QString());
}

void Finder::showContextMenu(FinderView *view, Item *hit) {
	setFront(view);
	const char *kind = !hit ? (view == m_desktop ? "desktop" : "window")
		: hit->kind == PL_ICON_TRASH_EMPTY || hit->kind == PL_ICON_TRASH_FULL ? "trash"
		: hit->isLocalVolume || hit->isNetworkVolume || hit->kind == PL_ICON_DISK ? "disk"
		: "icon";
	notifyState(); /* the menu's items follow this state, sent first */
	sendContextExtras(view, hit, kind);
	for (auto &c : m_clients) {
		if (c) {
			c->write(QByteArray("context ") + kind + "\n");
		}
	}
}

/* Before a contextual menu: the Folder Tricks for what was clicked
 * ("tricks <can attach>", then TAB "+name" for each attached and TAB
 * "*name" for each in the Folder Tricks folder) and the Contextual Menu
 * Items' commands that apply ("commands", then TAB "name" each). */
void Finder::sendContextExtras(FinderView *view, Item *hit, const char *kind) {
	const auto real = [](const QString &path) {
		return vfsIsVirtual(path) ? vfsRealCounterpart(path) : path;
	};
	m_contextPaths.clear();
	m_trickFolder.clear();
	if (!hit) {
		/* A window's background or the desktop: its folder. */
		const QString folder = real(view->folderPath());
		if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
			m_contextPaths << folder;
			m_trickFolder = folder;
		}
	} else if (strcmp(kind, "icon") == 0 || strcmp(kind, "disk") == 0) {
		for (Item *item : view->selectedItems()) {
			const QString path = real(item->path);
			if (path.isEmpty()) {
				m_contextPaths.clear(); /* a command has to apply to them all */
				break;
			}
			m_contextPaths << path;
		}
		if (strcmp(kind, "icon") == 0 && m_contextPaths.size() == 1 &&
				QFileInfo(m_contextPaths.first()).isDir()) {
			m_trickFolder = m_contextPaths.first();
		}
	}
	m_contextCommands = contextCommands(contextItemsDir(), m_contextPaths);

	FolderTricks &tricks = FolderTricks::instance();
	QString line = m_trickFolder.isEmpty() ? "tricks 0" : "tricks 1";
	if (!m_trickFolder.isEmpty()) {
		for (const QString &name : tricks.attached(m_trickFolder)) {
			line += "\t+" + name;
		}
		for (const QString &name : tricks.available()) {
			line += "\t*" + name;
		}
	}
	QString commands = "commands";
	for (const ContextCommand &command : m_contextCommands) {
		commands += "\t" + command.name;
	}
	for (auto &c : m_clients) {
		if (c) {
			c->write((line + "\n" + commands + "\n").toUtf8());
		}
	}
}

void Finder::showPathMenu(FolderWindow *window, bool option) {
	setFront(window);
	m_pathMenu.from = window;
	m_pathMenu.folders = folderChain(window->folderPath());
	m_pathMenu.option = option;
	QStringList names;
	for (const QString &folder : m_pathMenu.folders) {
		names << folderTitle(folder).replace('\t', ' ').replace('\n', ' ');
	}
	for (auto &c : m_clients) {
		if (c) {
			c->write(("pathmenu " + names.join('\t') + "\n").toUtf8());
		}
	}
}

void Finder::openFolder(const QString &path) {
	const QString real = vfsIsVirtual(path) ? vfsOpensAs(path) : QString();
	FolderWindow::open(real.isEmpty() ? path : real);
}

void Finder::openEnclosing(bool closeThis) {
	FinderView *v = front();
	if (!v || v == m_desktop) {
		return;
	}
	const QString parent = enclosingFolder(v->folderPath());
	if (parent.isEmpty()) {
		pl_sound_event("alert");
		return;
	}
	QWidget *was = v->widget();
	openFolder(parent);
	if (closeThis && was) {
		was->close();
	}
}

void Finder::addToFavorites() {
	FinderView *v = front();
	if (!v) {
		return;
	}
	const QString favorites = favoritesPath();
	QDir().mkpath(favorites);
	for (Item *item : v->selectedItems()) {
		if (item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			continue;
		}
		/* The Macintosh view's folders and applications through what they
		 * stand for; an alias, through its original. */
		QString target = item->path;
		if (item->isVirtual) {
			const VNode *node = vfsNode(item->path);
			target = node && node->actionId.isEmpty() ? vfsRealCounterpart(item->path) : QString();
		} else if (item->isAlias) {
			target = QFileInfo(item->path).symLinkTarget();
		}
		if (target.isEmpty()) {
			Alert::ask("“" + item->visibleName() + "” can't be added to Favorites.", "OK", QString());
			continue;
		}
		createAlias(target, favorites, item->visibleName(), QString());
	}
	folderChanged(favorites);
}

void Finder::indexSelection() {
	QStringList folders;
	for (Item *item : front() ? front()->selectedItems() : std::vector<Item *>{}) {
		const QString path = item->isVirtual ? vfsRealCounterpart(item->path) : item->path;
		if (!path.isEmpty() && QFileInfo(path).isDir()) {
			folders << path;
		}
	}
	if (folders.isEmpty() && front() && !vfsIsVirtual(front()->folderPath()) &&
			QFileInfo(front()->folderPath()).isDir()) {
		folders << front()->folderPath();
	}
	if (folders.isEmpty()) {
		Alert::ask("Select a folder or disk to index.", "OK", QString());
		return;
	}
	auto left = std::make_shared<int>(folders.size());
	auto errors = std::make_shared<QStringList>();
	for (const QString &folder : folders) {
		indexAddFolder(folder, [left, errors, folders](const QString &error) {
			if (!error.isEmpty()) {
				*errors << error;
			}
			if (--*left == 0) {
				Alert::ask(errors->isEmpty()
					? QString("%1 will be indexed in the background. Find can search its contents "
						"once that's done.").arg(folders.size() == 1 ? "“" + displayName(folders.first()) + "”"
						: QString("The %1 folders").arg(folders.size()))
					: errors->first(), "OK", QString());
			}
		});
	}
}

void Finder::openPath(const QString &path) {
	const QFileInfo info(path);
	if (!info.exists() && !info.isSymLink()) {
		Alert::ask("“" + info.fileName() + "” could not be found.", "OK", QString());
		return;
	}
	auto item = makeItem(info);
	if (item) {
		openItem(item.get());
	}
}

void Finder::ejectSelection() {
	struct Disk {
		QString path, name;
		bool network;
	};
	QList<Disk> disks;
	for (const Item *item : front() ? front()->selectedItems() : std::vector<Item *>{}) {
		if (item->isLocalVolume || item->isNetworkVolume) {
			disks.append({ item->path, item->name, item->isNetworkVolume });
		}
	}
	if (disks.isEmpty()) {
		Alert::ask("Select a disk on the desktop to eject.", "OK", QString());
		return;
	}
	for (const Disk &disk : disks) {
		ejectVolume(disk.path, disk.name, disk.network);
	}
}

void Finder::logOut() {
	if (!Alert::ask("Are you sure you want to quit all programs and log out now?", "Log Out")) {
		return;
	}
	/* The menu bar sees every program's windows: it asks them to close
	 * (each can still ask to save) and ends the session once they have. */
	for (auto &c : m_clients) {
		if (c) {
			c->write("logout\n");
		}
	}
}

void Finder::unmountSelection(bool network) {
	const auto items = front() ? front()->selectedItems() : std::vector<Item *>{};
	if (items.size() != 1 || (network ? !items[0]->isNetworkVolume : !items[0]->isLocalVolume)) {
		Alert::ask(network ? "Select one network disk on the desktop to disconnect." :
			"Select one mounted disk on the desktop to unmount.", "OK", QString());
		return;
	}
	ejectVolume(items[0]->path, items[0]->name, network);
}

/* Put Away: Trash items go back where they came from (freedesktop
 * trashinfo "Path="). */
void Finder::putAway() {
	const QString trash = trashFilesPath();
	const QString info = QFileInfo(trash).absolutePath() + "/info";
	QStringList changed;
	bool restoredApplication = false;
	struct Selected {
		QString path, name;
		bool local, network;
	};
	QList<Selected> selected;
	for (const Item *item : front()->selectedItems()) {
		selected.append({ item->path, item->name, item->isLocalVolume, item->isNetworkVolume });
	}
	for (const auto &item : selected) {
		if (item.local || item.network) {
			ejectVolume(item.path, item.name, item.network);
			continue;
		}
		if (QFileInfo(item.path).absolutePath() != QDir(trash).absolutePath()) {
			continue; /* only things in the Trash can be put away */
		}
		if (appTrashMarker(item.path)) {
			QString error;
			if (!appTrashRestore(item.path, &error)) {
				Alert::ask(error, "OK", QString());
			}
			restoredApplication = true;
			continue;
		}
		QFile f(info + "/" + item.name + ".trashinfo");
		if (!f.open(QIODevice::ReadOnly)) {
			continue;
		}
		QString original;
		for (const QByteArray &line : f.readAll().split('\n')) {
			if (line.startsWith("Path=")) {
				original = QUrl::fromPercentEncoding(line.mid(5).trimmed());
			}
		}
		f.close();
		if (original.isEmpty() || QFileInfo::exists(original) ||
				!QDir().mkpath(QFileInfo(original).absolutePath())) {
			continue;
		}
		if (QDir().rename(item.path, original)) {
			QFile::remove(f.fileName());
			changed << QFileInfo(original).absolutePath();
		}
	}
	changed.removeDuplicates();
	if (restoredApplication) {
		vfsRefresh();
	}
	for (const QString &d : changed) {
		folderChanged(d);
	}
	folderChanged(trash);
}

/* Resolves an alias, reconnecting by identity; on failure explains and offers
 * to choose a new original. An empty result means nothing to open. */
static QFileInfo resolveAliasChoice(const QString &aliasPath, const AliasResolution &r) {
	const QString name = QFileInfo(aliasPath).fileName();
	if (!r.error.isEmpty()) {
		Alert::ask(r.error, "OK", QString());
	}
	if (r.state == AliasState::Ok || r.state == AliasState::Reconnected) {
		return QFileInfo(r.target);
	}
	if (r.state == AliasState::Changed) {
		if (Alert::ask("The original for “" + name + "” has been replaced by a different item. "
				"Use the new item?", "Use New Item", "Cancel")) {
			QString error;
			if (aliasReconnect(aliasPath, r.target, &error)) {
				return QFileInfo(r.target);
			}
			Alert::ask(error, "OK", QString());
		}
		return QFileInfo();
	}
	const Alert::Choice choice = Alert::choose(aliasProblemText(r.state) +
		" Choose a new original for “" + name + "”?", "Choose File…", "Cancel", "Choose Folder…");
	if (choice == Alert::Cancel) {
		return QFileInfo();
	}
	const QString chosen = choice == Alert::Other ?
		QFileDialog::getExistingDirectory(nullptr, "Fix Alias", QFileInfo(aliasPath).absolutePath()) :
		QFileDialog::getOpenFileName(nullptr, "Fix Alias", QFileInfo(aliasPath).absolutePath());
	QString error;
	if (!chosen.isEmpty()) {
		if (aliasReconnect(aliasPath, chosen, &error)) {
			return QFileInfo(chosen);
		}
		Alert::ask(error, "OK", QString());
	}
	return QFileInfo();
}

static void resolveAliasForUser(const QString &aliasPath,
		std::function<void(const QFileInfo &)> done) {
	auto *worker = QThread::create([aliasPath, done = std::move(done)]() mutable {
		const AliasResolution result = aliasResolve(aliasPath);
		QCoreApplication *app = QCoreApplication::instance();
		if (!app) {
			return;
		}
		QMetaObject::invokeMethod(app, [aliasPath, result, done = std::move(done)]() mutable {
			done(resolveAliasChoice(aliasPath, result));
		}, Qt::QueuedConnection);
	});
	QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	worker->start();
}

/* Show Original: the folder holding an alias's target, with it selected. */
void Finder::showOriginal() {
	for (Item *item : front()->selectedItems()) {
		if (!item->isAlias) {
			continue;
		}
		const QString path = item->path;
		resolveAliasForUser(path, [](const QFileInfo &target) {
			if (target.filePath().isEmpty()) {
				return;
			}
			FolderWindow *w = FolderWindow::open(target.absolutePath());
			w->selectByName(target.fileName());
			Finder::instance().notifyState();
		});
		return;
	}
}

/* Info on each selected item, or on the front window's folder; `sharing`
 * shows Sharing rather than General Information. */
void Finder::getInfo(bool sharing) {
	FinderView *v = front();
	std::vector<Item *> items = v->selectedItems();
	const int view = sharing ? InfoWindow::Sharing : InfoWindow::General;
	if (items.empty() && v != m_desktop) {
		if (vfsIsVirtual(v->folderPath())) {
			const QString path = v->folderPath();
			QStringList metadata;
			ResourceDetails details;
			vfsResourceDetails(path, &metadata, &details.kind, &details.location,
				&details.sources, &details.access);
			details.name = vfsName(path);
			details.metadata = metadata;
			InfoWindow::openVirtual(path, PL_ICON_FOLDER, details.name, details);
		} else {
			InfoWindow::open(v->folderPath(), PL_ICON_FOLDER, displayName(v->folderPath()), view);
		}
	}
	for (Item *item : items) {
		if (item->isVirtual) {
			/* Get Info reads a file. Show the one the item stands for:
			 * a folder's real directory, or an application's desktop
			 * entry. The curated folders stand for nothing on disk.
			 * TODO: a Get Info of their own. */
			const QString real = vfsRealCounterpart(item->path);
			const VNode *node = vfsNode(item->path);
			if (!real.isEmpty() && !(node && node->kind == VKind::Launcher &&
					!node->appId.isEmpty())) {
				InfoWindow::open(real, item->kind, item->visibleName(), view);
			} else {
				QStringList metadata;
				ResourceDetails details;
				vfsResourceDetails(item->path, &metadata, &details.kind, &details.location,
					&details.sources, &details.access);
				details.name = item->name;
				details.metadata = metadata;
				InfoWindow::openVirtual(item->path, item->kind, item->name, details);
			}
			continue;
		}
		InfoWindow::open(item->path, item->kind, item->visibleName(), view);
	}
}

void Finder::closeWindow() {
	FinderView *v = front();
	if (v && v != m_desktop) {
		v->widget()->close();
	}
}

void Finder::moveSelectionToTrash() {
	FinderView *v = front();
	QStringList changed;
	QStringList paths;
	for (Item *item : v->selectedItems()) {
		if (item->kind != PL_ICON_DISK && item->kind != PL_ICON_TRASH_EMPTY &&
				item->kind != PL_ICON_TRASH_FULL) {
			paths << item->path;
		}
	}
	QStringList startupIds;
	for (const QString &path : paths) {
		const QString startupId = vfsStartupItemId(path);
		if (!startupId.isEmpty()) {
			startupIds << startupId;
			continue;
		}
		if (vfsIsAppFolder(path)) {
			const QString id = vfsNode(path)->appId;
			QString error;
			if (appTrashQueue(id, &error)) {
				changed << trashFilesPath();
				vfsRefresh();
			} else {
				Alert::ask(error, "OK", QString());
			}
			continue;
		}
		/* A folder the user made on the startup disk: gone if empty. */
		if (vfsIsUserFolder(path)) {
			if (!vfsDeleteUserFolder(path)) {
				Alert::ask(QStringLiteral("“%1” isn't empty. Move what's inside it to the "
					"Trash first.").arg(displayName(path)), "OK", QString());
			}
			continue;
		}
		/* The disk and the Trash itself can't go in the Trash, and
		 * neither can anything else in the Macintosh view: there is no
		 * file to move. */
		if (vfsIsVirtual(path) || path == trashFilesPath() ||
				QDir(path).isRoot()) {
			continue;
		}
		if (QFile::moveToTrash(path)) {
			changed << QFileInfo(path).absolutePath();
		}
	}
	if (!startupIds.isEmpty()) {
		removeFromStartupItems(startupIds);
	}
	changed.removeDuplicates();
	for (const QString &folder : changed) {
		folderChanged(folder);
	}
	if (!changed.isEmpty()) {
		pl_sound_event("trash-move");
	}
	folderChanged(trashFilesPath());
}

/* Locked as the Mac meant it (Find's "is locked"): it can't be changed.
 * Removing an alias doesn't touch its original, so links never are. */
static bool trashLocked(const QFileInfo &info) {
	return !info.isSymLink() && !info.isWritable();
}

/* Removes what isn't locked under `path`: true if all of it went. A
 * locked folder keeps all it holds (its entries can't be removed). */
static bool removeUnlocked(const QString &path) {
	const QFileInfo info(path);
	if (trashLocked(info)) {
		return false;
	}
	if (info.isSymLink() || !info.isDir()) {
		return QFile::remove(path);
	}
	bool all = true;
	for (const QFileInfo &child : QDir(path).entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
		all = removeUnlocked(child.absoluteFilePath()) && all;
	}
	return all && QDir().rmdir(path);
}

/* Option-Empty Trash: locked items are unlocked (they're ours) and go too. */
static bool removeForced(const QString &path) {
	const QFileInfo info(path);
	if (info.isSymLink() || !info.isDir()) {
		return QFile::remove(path);
	}
	QFile::setPermissions(path, info.permissions() | QFileDevice::ReadOwner |
		QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	bool all = true;
	for (const QFileInfo &child : QDir(path).entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
		all = removeForced(child.absoluteFilePath()) && all;
	}
	return all && QDir().rmdir(path);
}

void Finder::emptyTrash(bool option) {
	static bool emptying = false;
	if (emptying) {
		return;
	}
	QScopedValueRollback<bool> guard(emptying, true);
	const QString files = trashFilesPath();
	QString queueError;
	const auto applications = appTrashEntries(&queueError);
	if (!queueError.isEmpty()) {
		Alert::ask(queueError, "OK", QString());
		return;
	}
	int count = 0, locked = 0;
	qint64 bytes = 0;
	QDirIterator it(files, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
		QDir::System, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		count++;
		locked += trashLocked(it.fileInfo()) ? 1 : 0;
		bytes += it.fileInfo().isFile() ? it.fileInfo().size() : 0;
	}
	if (count == 0) {
		return;
	}
	/* The Mac asks first. */
	QString message = QStringLiteral(
		"The Trash contains %1 %2, which %3 %4 of disk space. Are you sure you "
		"want to permanently remove %5?")
		.arg(count).arg(count == 1 ? "item" : "items").arg(count == 1 ? "uses" : "use")
		.arg(finderSize(bytes)).arg(count == 1 ? "this item" : "these items");
	if (!applications.empty()) {
		QStringList names;
		for (const auto &entry : applications) {
			names << entry.name + " (" + entry.origin + ")";
		}
		message += "\n\nThis will uninstall: " + names.join(", ") +
			". Personal application data is kept. Debian removal may also remove dependent packages.";
	}
	/* Locked items stay, and the Mac says so even without the warning. */
	const bool keepLocked = locked > 0 && !option;
	if (keepLocked) {
		message += QStringLiteral("\n\n%1 locked %2 will stay in the Trash. To remove %3 too, "
			"hold down Option while you choose Empty Trash.")
			.arg(locked).arg(locked == 1 ? "item" : "items").arg(locked == 1 ? "it" : "them");
	}
	/* Without the warning (the Trash's Get Info), or with Option, it
	 * empties at once; uninstalling applications still asks. */
	if (((trashWarns() && !option) || keepLocked || !applications.empty()) &&
			!Alert::ask(message)) {
		return;
	}
	if (!applications.empty()) {
		QProgressDialog progress("Uninstalling applications...", QString(), 0, 0);
		progress.setWindowTitle("Uninstalling Applications");
		progress.setWindowModality(Qt::ApplicationModal);
		progress.setMinimumDuration(0);
		progress.setCancelButton(nullptr);
		progress.show();
		platinumSetFrameStyle(&progress, FrameStyle::MovableModal);
		quint64 completed = 0;
		QStringList returned;
		for (const auto &entry : applications) {
			progress.setLabelText(QString("Uninstalling %1 (%2 of %3). Personal data is kept.")
				.arg(entry.name).arg(completed + 1).arg(applications.size()));
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			appRefresh();
			const AppEntry *app = appById(entry.id);
			bool ok = true;
			QString error;
			/* Nothing to uninstall safely: it goes back to Applications
			 * rather than keep the rest of the Trash from emptying. */
			QString why;
			if (app && (app->systemUtility || !app->userInstalled)) {
				why = "it is now a system utility or an automatically installed dependency";
			} else if (app && app->origin != entry.origin) {
				why = "its install source has changed";
			}
			if (!why.isEmpty()) {
				if (!appTrashRestore(entry.path, &error)) {
					progress.hide();
					Alert::ask(error, "OK", QString());
					vfsRefresh();
					folderChanged(files);
					return;
				}
				returned << QStringLiteral("“%1” was not uninstalled because %2. It is back in "
					"Applications.").arg(entry.name, why);
				++completed;
				vfsRefresh();
				continue;
			}
			const QString source = entry.origin.section(':', 0, 0);
			const QString id = entry.origin.section(':', 1);
			bool installed = source == "flatpak" ? flatpakInstalled(id, &ok, &error)
				: packagesInstalled({ id }, &ok, &error);
			if (ok && installed) {
				if (source == "flatpak") {
					runFlatpak({ "uninstall", "--noninteractive", "--", id }, &ok, &error);
				} else {
					runAppstoreHelper({ "remove", id }, &ok, &error);
				}
				if (ok) {
					installed = source == "flatpak" ? flatpakInstalled(id, &ok, &error)
						: packagesInstalled({ id }, &ok, &error);
					if (ok && installed) {
						ok = false;
						error = "The uninstall command finished, but the application is still installed: " +
							entry.name;
					}
				}
			}
			if (ok) {
				ok = appTrashRemove(entry.path, &error);
			}
			if (!ok) {
				progress.hide();
				qWarning().noquote() << error;
				Alert::ask(error + "\nThe application remains queued in Trash; Put Away cancels removal.",
					"OK", QString());
				vfsRefresh();
				folderChanged(files);
				return;
			}
			++completed;
			vfsRefresh();
		}
		progress.hide();
		if (!returned.isEmpty()) {
			Alert::ask(returned.join("\n\n"), "OK", QString());
		}
	}
	if (!appTrashEntries(&queueError).empty() || !queueError.isEmpty()) {
		Alert::ask(queueError.isEmpty() ? "New applications were queued while emptying Trash. "
			"Empty Trash again to confirm their removal." : queueError, "OK", QString());
		folderChanged(files);
		return;
	}
	/* Each item, and its record for Put Away once it's all gone. */
	const QString info = QFileInfo(files).absolutePath() + "/info";
	bool all = true;
	for (const QFileInfo &entry : QDir(files).entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
		const QString path = entry.absoluteFilePath();
		if (option ? removeForced(path) : removeUnlocked(path)) {
			QFile::remove(info + "/" + entry.fileName() + ".trashinfo");
		} else {
			all = false;
		}
	}
	if (all) {
		QDir(info).removeRecursively();
	} else if (!keepLocked) {
		Alert::ask("The Trash could not be completely emptied.", "OK", QString());
		folderChanged(files);
		return;
	}
	QDir().mkpath(files);
	pl_sound_event("trash-empty");
	folderChanged(files);
}

void Finder::folderChanged(const QString &folder) {
	const QString abs = QDir(folder).absolutePath();
	FolderWindow::reloadAll(abs);
	FoundWindow::reloadAll();
	if (m_desktop) {
		m_desktop->folderChanged(abs);
	}
	notifyState();
}
