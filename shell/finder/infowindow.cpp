#include "infowindow.h"
#include "netvolumes.h"
#include "thumbnails.h"
#include "fileassoc.h"
#include "gpuinfo.h"
#include "alias.h"
#include "platinumshell.h"

#include "vfs.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QStorageInfo>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "alert.h"
#include "tr.h"
#include "finder.h"
#include "items.h"
#include "logo.h"
#include "resources.h"
#include "sharingclient.h"
#include "viewoptions.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int LABEL_RIGHT = 74; /* right edge of "Kind:" etc. */
static constexpr int VALUE_X = 82;
static constexpr int ROW_H = 16;
static constexpr int COMMENT_MAX = 200; /* Mac OS Finder comment limit */
static const char *const COMMENT_ATTR = "user.xdg.comment";

static int textAdvance(const QString &s) {
	Text t(s, 100000, PL_FONT_VIEWS);
	return t.t ? t.t->advance : 0;
}

QString finderDate(const QDateTime &t) {
	if (!t.isValid()) {
		return "--";
	}
	return QLocale(QLocale::English).toString(t, "ddd, MMM d, yyyy, h:mm AP");
}

QString finderSize(qint64 bytes) {
	if (bytes < 1024 * 1024) {
		return QString::number(std::max<qint64>(bytes > 0 ? 1 : 0, (bytes + 1023) / 1024)) + "K";
	}
	if (bytes < 1024LL * 1024 * 1024) {
		return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
	}
	return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

QString macPath(const QString &dir) {
	QStringList parts = QDir::cleanPath(dir).split('/', Qt::SkipEmptyParts);
	/* The startup disk the user sees, not the Unix root: their files are
	 * on the disk the desktop shows them on. */
	parts.prepend(vfsVolumeName());
	return parts.join(':') + ':';
}

/* The 2 px engraved separator (HIG chapter 2): a line and its highlight. */
static void separator(pl_canvas *c, int x0, int x1, int y) {
	pl_hline(c, x0, x1, y, GRAY(0x8));
	pl_hline(c, x0, x1, y + 1, C_WHITE);
}

/* ---- Get Info ------------------------------------------------------------ */

/* Measured from Mac OS 9's Get Info window (Sharing), in content pixels:
 * the icon and name on top, then a group box titled by the Show pop-up. */
static constexpr int INFO_W = 340, INFO_H = 371;
static constexpr int ICON_BOX_X = 23, ICON_BOX_Y = 7; /* 38 px square frame */
static constexpr int NAME_X = 72, NAME_BASELINE = 29;
static constexpr int SHOW_X = 67, SHOW_Y = 55, SHOW_W = 170, SHOW_LABEL_R = 62;
static constexpr int BOX_X0 = 13, BOX_Y0 = 67, BOX_X1 = 330, BOX_Y1 = 359;
static constexpr int LABEL_R = 96, VALUE_X2 = 104; /* "Where:" and its value */
static constexpr int INSIDE_X = 21;                /* left edge of what's inside */
/* Sharing: "Share this item...", then the privileges in a box of their own. */
static constexpr int SHARE_BOX_Y = 114, NOTE_BASELINE = 140;
static constexpr int PRIV_X0 = 21, PRIV_Y0 = 196, PRIV_X1 = 321, PRIV_Y1 = 349;
static constexpr int HEAD_BASELINE = 213, NAME_COL = 118, PRIV_COL = 255;
static constexpr int ROW1_Y = 220, ROW_STEP = 30;
static constexpr int WHO_X = 110, WHO_W = 133, WHO_LABEL_R = 104;
static constexpr int WELL_X = 253;
static constexpr int SEP_Y = 313, COPY_TEXT_X = 30, COPY_BASELINE = 336;

static const char *const PRIVILEGES[] = { N_("Read & Write"), N_("Read only"), N_("Write only (Drop Box)"), N_("None") };
static const int PRIVILEGE_BITS[] = { 7, 5, 3, 0 };

/* The privilege icons: glasses (see), a pencil (make changes), both, or
 * dashes (neither). Original pixel art. */
static QImage art(const std::vector<const char *> &rows) {
	QImage img(static_cast<int>(strlen(rows[0])), static_cast<int>(rows.size()), QImage::Format_ARGB32);
	img.fill(0);
	for (int y = 0; y < img.height(); y++) {
		for (int x = 0; rows[y][x]; x++) {
			const uint32_t colors[] = { 0xFF000000, 0xFFB8D0F0, 0xFFF0C828, 0xFFE890A8, 0xFFE0B080,
				0xFF505050 };
			const char *keys = "#lyptk";
			if (const char *k = strchr(keys, rows[y][x]); k && *k) {
				img.setPixel(x, y, colors[k - keys]);
			}
		}
	}
	return img;
}

static QImage privilegeIcon(int privilege) {
	static const std::vector<const char *> glasses = {
		".###.....###.",
		"#lll#####lll#",
		"#lll#...#lll#",
		"#lll#...#lll#",
		".###.....###.",
	};
	static const std::vector<const char *> pencil = {
		"......##.",
		".....#pp#",
		"....#yy#.",
		"...#yy#..",
		"..#yy#...",
		".#tt#....",
		"#kt#.....",
		"#k#......",
		"##.......",
	};
	switch (privilege) {
	case 0: { /* both: the pencil leaning on the glasses */
		QImage both(19, 11, QImage::Format_ARGB32);
		both.fill(0);
		QPainter p(&both);
		p.drawImage(0, 6, art(glasses));
		p.drawImage(10, 0, art(pencil));
		return both;
	}
	case 1: return art(glasses);
	case 2: return art(pencil);
	default: return art({ "####..####", "####..####" });
	}
}

/* A class's rwx bits as the nearest Mac privilege. */
static int privilegeOf(int bits) {
	if ((bits & 5) == 5) {
		return bits & 2 ? 0 : 1;
	}
	return (bits & 3) == 3 ? 2 : 3;
}

static QHash<QString, QPointer<InfoWindow>> &infoWindows() {
	static QHash<QString, QPointer<InfoWindow>> windows;
	return windows;
}

void InfoWindow::open(const QString &path, pl_icon_kind kind, const QString &name, int view) {
	InfoWindow *w = infoWindows().value(path);
	if (!w) {
		w = new InfoWindow(path, kind, name);
		infoWindows().insert(path, w);
	}
	if (view == Sharing && w->m_show.items[Sharing].enabled) {
		/* Not when file sharing is off: then the alert says so instead. */
		if (!w->showView(Sharing) && !w->isVisible()) {
			w->close();
			return;
		}
	}
	w->show();
	w->raise();
	w->activateWindow();
}

void InfoWindow::openVirtual(const QString &path, pl_icon_kind kind, const QString &name,
		const ResourceDetails &details, const QString &iconPath) {
	InfoWindow *w = infoWindows().value(path);
	if (!w) {
		w = new InfoWindow(path, kind, name, &details, iconPath);
		infoWindows().insert(path, w);
	} else if (!iconPath.isEmpty()) {
		w->m_iconPath = iconPath;
	}
	w->show();
	w->raise();
	w->activateWindow();
}

InfoWindow::InfoWindow(const QString &path, pl_icon_kind kind, const QString &name,
		const ResourceDetails *details, const QString &iconPath)
	: m_path(path), m_name(name), m_iconPath(iconPath), m_kind(kind),
	  m_isResourceInfo(details != nullptr) {
	if (!details) {
		m_iconPath = path;
	}
	/* A network volume shows as one (its kind stays a disk's). */
	for (const NetVolume &v : netVolumes()) {
		m_network = m_network || v.path == path;
	}
	setAttribute(Qt::WA_DeleteOnClose);
	setAcceptDrops(true);
	watchThumbnails(this);
	watchFileAssociations(this);
	setWindowTitle(tr9("Info for %1").arg(name));
	if (details) {
		m_resourceDetails = *details;
		m_kindText = details->kind;
		m_where = details->location;
		m_size = "--";
		m_isVolumeInfo = kind == PL_ICON_DISK && path == vfsRoot();
		if (m_isVolumeInfo) {
			m_kindText = tr9("startup disk");
			m_where = tr9("Desktop");
			m_resourceDetails.kind = m_kindText;
			m_resourceDetails.location = m_where;
			QStorageInfo storage(QDir::homePath());
			storage.refresh();
			if (storage.isValid() && storage.isReady()) {
				m_size = storage.bytesTotal() > 0 ? finderSize(storage.bytesTotal()) :
					tr9("Not reported");
				m_available = storage.bytesAvailable() >= 0 ?
					finderSize(storage.bytesAvailable()) : tr9("Not reported");
			} else {
				m_size = m_available = tr9("Not reported");
			}
		}
		addIconHint();
		m_show.rect = QRect(SHOW_X, SHOW_Y, SHOW_W, PL_POPUP_H);
		m_show.items = { PopupItem{ tr9("General Information") }, PopupItem{ tr9("Sharing"), false } };
		m_show.chosen = [this](int i) { showView(i); };
		showView(0);
		return;
	}

	QFileInfo info(path);
	m_isTrash = kind == PL_ICON_TRASH_EMPTY || kind == PL_ICON_TRASH_FULL;
	addIconHint();
	if (m_isTrash) {
		m_kindText = tr9("trash");
		m_trashWarn = PanelCheckbox(tr9("Warn before emptying"), QPoint(INSIDE_X, BOX_Y1 - 26));
		m_trashWarn.on = trashWarns();
		m_trashWarn.toggled = [](bool on) { setTrashWarns(on); };
	} else if (kind == PL_ICON_DISK) {
		m_kindText = tr9("disk");
	} else if (kind == PL_ICON_ROM_PS3) {
		m_kindText = tr9("PlayStation 3 game");
	} else if (info.isDir()) {
		m_kindText = tr9("folder");
	} else {
		static QMimeDatabase db;
		m_kindText = db.mimeTypeForFile(info).comment();
		if (kind == PL_ICON_APPLICATION) {
			m_kindText = tr9("application program");
		}
	}
	m_where = m_isTrash ? tr9("Desktop") : macPath(path == "/" ? "/" : info.absolutePath());
	if (info.isSymLink()) {
		m_kindText = tr9("alias");
		const AliasResolution r = aliasResolve(path, false);
		QString virtualTarget;
		if (r.state == AliasState::Ok && aliasVirtualTarget(path, &virtualTarget)) {
			m_original = vfsNode(virtualTarget) ? vfsName(virtualTarget) : tr9("(missing)");
		} else {
			m_original = r.state == AliasState::Ok ? r.target :
				r.state == AliasState::Changed ? r.target + tr9(" (replaced)") :
				r.state == AliasState::Loop ? tr9("(alias loop)") :
				tr9("(missing)");
		}
	}

	/* Show: folders can be shared; files, aliases and disks can't. */
	const bool folder = info.isDir() && !info.isSymLink() && kind != PL_ICON_DISK && !m_isTrash;
	m_show.rect = QRect(SHOW_X, SHOW_Y, SHOW_W, PL_POPUP_H);
	m_show.items = { PopupItem{ tr9("General Information") }, PopupItem{ tr9("Sharing"), folder } };
	m_show.chosen = [this](int i) { showView(i); };
	if (folder) {
		loadSharing();
	}
	showView(0);

	char buf[1024];
	ssize_t n = getxattr(QFile::encodeName(path).constData(), COMMENT_ATTR, buf, sizeof(buf));
	if (n > 0) {
		m_comment = QString::fromUtf8(buf, static_cast<int>(n));
	}
	m_caretTimer.callOnTimeout([this] {
		m_caretOn = !m_caretOn;
		update();
	});
	m_created = finderDate(info.birthTime().isValid() ? info.birthTime() : info.metadataChangeTime());
	m_modified = finderDate(info.lastModified());

	if (!info.isDir()) {
		QLocale en(QLocale::English);
		m_size = tr9("%1 on disk (%2 bytes)").arg(finderSize(info.size()), en.toString(info.size()));
		return;
	}
	/* Folders: add it up off the main thread. */
	m_size = tr9("calculating…");
	QPointer<InfoWindow> self(this);
	m_sizer = QThread::create([self, path] {
		qint64 bytes = 0;
		int items = 0;
		QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
			QDir::System, QDirIterator::Subdirectories);
		while (it.hasNext() && !QThread::currentThread()->isInterruptionRequested()) {
			it.next();
			items++;
			if (it.fileInfo().isFile() && !it.fileInfo().isSymLink()) {
				bytes += it.fileInfo().size();
			}
		}
		QString text = items == 1
			? tr9("%1 on disk, for 1 item").arg(finderSize(bytes))
			: tr9("%1 on disk, for %2 items").arg(finderSize(bytes)).arg(items);
		QMetaObject::invokeMethod(qApp, [self, text] {
			if (self) {
				self->m_size = text;
				self->update();
			}
		});
	});
	m_sizer->start();
}

InfoWindow::~InfoWindow() {
	if (m_sizer) {
		m_sizer->requestInterruption();
		m_sizer->wait();
		delete m_sizer;
	}
}

/* False if Sharing was asked for while file sharing is off: as on a Mac,
 * an alert offers the File Sharing control panel instead. */
bool InfoWindow::showView(int view) {
	if (view == Sharing && !sharingOn()) {
		if (Alert::ask(tr9("Folders cannot be shared until file sharing is turned on using the "
				"File Sharing control panel. Do you want the control panel opened now?"),
				"OK", "Cancel")) {
			platinumStartApplication("zacos9-filesharing", {});
		}
		view = m_view;
		m_show.selected = view;
		update();
		return false;
	}
	m_view = view;
	if (view != General) {
		m_iconFocused = false;
	}
	m_show.selected = view;
	m_host.popups = { &m_show };
	m_host.checks.clear();
	m_host.buttons.clear();
	if (view == 1) {
		setEditingComment(false);
		layoutSharing();
		m_host.popups.insert(m_host.popups.end(),
			{ &m_ownerPopup, &m_groupPopup, &m_privilege[0], &m_privilege[1], &m_privilege[2] });
		if (m_enclosingShare.isEmpty()) {
			m_host.checks = { &m_shareBox };
		}
		m_host.buttons = { &m_copy };
	} else if (m_isTrash) {
		m_host.checks = { &m_trashWarn };
	}
	setFixedSize(INFO_W, INFO_H);
	update();
	return true;
}

void InfoWindow::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, width() - 1, height() - 1, FACE);
	paintHeader(c);
	if (m_view == 1) {
		paintSharing(c);
	} else {
		paintGeneral(c);
	}
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

/* What can be copied to and pasted from the icon: the item itself, or for
 * a virtual item such as an application, the file whose icon stands for it. */
bool InfoWindow::canChangeIcon() const {
	return !m_iconPath.isEmpty() && !m_isVolumeInfo && !m_isTrash;
}

/* The icon's Hint Bubble: copy it, or paste a picture from the Clipboard
 * as this item's icon. Only where the icon can be changed. */
void InfoWindow::addIconHint() {
	if (!canChangeIcon()) {
		return;
	}
	m_iconHintArea = QRect(ICON_BOX_X, ICON_BOX_Y, 38, 38);
	m_host.hintAreas.push_back({ &m_iconHintArea,
		tr9("⌘-C copies this icon. ⌘-V pastes a picture from the Clipboard as this item's icon.") });
}

pl_icon_kind InfoWindow::shownKind() const {
	return m_kind == PL_ICON_FOLDER && !m_sharedAs.isEmpty() ? PL_ICON_SHARED_FOLDER
		: m_kind == PL_ICON_DISK && m_network ? PL_ICON_NETWORK_DISK : m_kind;
}

/* The icon as the Finder shows it, without the window behind it: a custom
 * icon, a document's thumbnail or document icon, or the built-in art. */
QImage InfoWindow::currentIconImage() const {
	QImage custom;
	if (canChangeIcon()) {
		custom = customIconImage(m_iconPath);
	}
	if (!custom.isNull()) {
		return custom;
	}
	auto fromPixels = [](const std::vector<uint32_t> &pixels) {
		if (pixels.size() < PL_ICON_LARGE * PL_ICON_LARGE) {
			return QImage();
		}
		QImage image(PL_ICON_LARGE, PL_ICON_LARGE, QImage::Format_ARGB32);
		for (int y = 0; y < PL_ICON_LARGE; y++) {
			memcpy(image.scanLine(y), pixels.data() + y * PL_ICON_LARGE,
				PL_ICON_LARGE * sizeof(uint32_t));
		}
		return image;
	};
	if (m_kind == PL_ICON_DOCUMENT && !m_isResourceInfo) {
		std::vector<uint32_t> preview, small;
		if (thumbnailFor(m_path, &preview, &small) == ThumbnailState::Ready) {
			const QImage image = fromPixels(preview);
			if (!image.isNull()) {
				return image;
			}
		}
		if (const std::vector<uint32_t> *document = documentIcon(fileMimeType(m_path),
				PL_ICON_LARGE)) {
			const QImage image = fromPixels(*document);
			if (!image.isNull()) {
				return image;
			}
		}
	}
	const uint32_t *pixels = pl_icon(shownKind(), PL_ICON_LARGE);
	if (!pixels) {
		return QImage();
	}
	QImage image(PL_ICON_LARGE, PL_ICON_LARGE, QImage::Format_ARGB32);
	for (int y = 0; y < PL_ICON_LARGE; y++) {
		memcpy(image.scanLine(y), pixels + y * PL_ICON_LARGE,
			PL_ICON_LARGE * sizeof(uint32_t));
	}
	return image;
}

/* The folder whose view shows m_path's icon. A virtual path is not an
 * absolute filesystem path, so QFileInfo would resolve it against the
 * current directory and miss the window. */
void InfoWindow::refreshIconFolder() {
	QString folder;
	if (vfsIsVirtual(m_path)) {
		const int slash = m_path.lastIndexOf('/');
		folder = slash > 0 ? m_path.left(slash) : vfsRoot();
	} else {
		folder = QFileInfo(m_path).absolutePath();
	}
	Finder::instance().folderChanged(folder);
}

/* The item's icon (framed) and name, and the group box the Show pop-up
 * titles. */
void InfoWindow::paintHeader(pl_canvas *c) {
	pl_outline(c, ICON_BOX_X, ICON_BOX_Y, ICON_BOX_X + 37, ICON_BOX_Y + 37,
		m_iconFocused ? GRAY(0x5) : C_BLACK);
	std::vector<uint32_t> preview, small;
	const std::vector<uint32_t> *document = nullptr;
	const QImage customIcon = canChangeIcon() ? customIconImage(m_iconPath) : QImage();
	if (!customIcon.isNull()) {
		pl_image_blend(c, ICON_BOX_X + 3, ICON_BOX_Y + 3,
			reinterpret_cast<const uint32_t *>(customIcon.constBits()),
			PL_ICON_LARGE, PL_ICON_LARGE, false);
	} else if (m_kind == PL_ICON_DOCUMENT && !m_isResourceInfo &&
			thumbnailFor(m_path, &preview, &small) == ThumbnailState::Ready) {
		pl_image_blend(c, ICON_BOX_X + 3, ICON_BOX_Y + 3, preview.data(),
			PL_ICON_LARGE, PL_ICON_LARGE, false);
	} else if (m_kind == PL_ICON_DOCUMENT && !m_isResourceInfo &&
			(document = documentIcon(fileMimeType(m_path), PL_ICON_LARGE))) {
		pl_image_blend(c, ICON_BOX_X + 3, ICON_BOX_Y + 3, document->data(),
			PL_ICON_LARGE, PL_ICON_LARGE, false);
	} else {
		pl_icon_paint(c, ICON_BOX_X + 3, ICON_BOX_Y + 3, shownKind(),
			PL_ICON_LARGE, false);
	}
	panelText(c, m_name, NAME_X, NAME_BASELINE, PL_FONT_SYSTEM, C_BLACK, INFO_W - NAME_X - 10);
	pl_group_box_paint(c, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, nullptr, FACE);
	/* The pop-up and its label stand in the box's top line. */
	pl_fill(c, BOX_X0 + 12, BOX_Y0, SHOW_X + SHOW_W + 1, BOX_Y0 + 1, FACE);
	panelLabel(c, tr9("Show:"), SHOW_LABEL_R, SHOW_Y + PL_POPUP_BASELINE(PL_POPUP_H));
}

/* A label ending at LABEL_R and its value in the small font. */
static void infoRow(pl_canvas *c, int baseline, const QString &label, const QString &value) {
	panelLabel(c, label, LABEL_R, baseline);
	panelText(c, value, VALUE_X2, baseline, PL_FONT_VIEWS, C_BLACK, BOX_X1 - VALUE_X2 - 8);
}

void InfoWindow::paintGeneral(pl_canvas *c) {
	if (m_isResourceInfo) {
		infoRow(c, 92, tr9("Kind:"), m_resourceDetails.kind);
		infoRow(c, 92 + ROW_H, tr9("Where:"), m_resourceDetails.location);
		if (m_isVolumeInfo) {
			infoRow(c, 92 + ROW_H * 2, tr9("Size:"), m_size);
			infoRow(c, 92 + ROW_H * 3, tr9("Available:"), m_available);
			int y = 92 + ROW_H * 4 + 8;
			for (const QString &detail : m_resourceDetails.metadata) {
				for (const QString &line : panelWrap(detail, BOX_X1 - VALUE_X2 - 8,
						PL_FONT_VIEWS)) {
					if (y > BOX_Y1 - 8) {
						return;
					}
					panelText(c, line, VALUE_X2, y, PL_FONT_VIEWS);
					y += 13;
				}
			}
			return;
		}
		infoRow(c, 92 + ROW_H * 2, tr9("Source:"), m_resourceDetails.sources.isEmpty()
			? tr9("None (virtual; no physical equivalent)") : m_resourceDetails.sources);
		infoRow(c, 92 + ROW_H * 3, tr9("Access:"), m_resourceDetails.access);
		int y = 92 + ROW_H * 4 + 8;
		QStringList lines;
		if (!m_resourceDetails.description.isEmpty()) {
			lines << tr9("Description: %1").arg(m_resourceDetails.description);
		}
		lines << m_resourceDetails.metadata;
		for (const QString &detail : lines) {
			for (const QString &line : panelWrap(detail, BOX_X1 - VALUE_X2 - 8,
					PL_FONT_VIEWS)) {
				if (y > BOX_Y1 - 8) {
					return;
				}
				panelText(c, line, VALUE_X2, y, PL_FONT_VIEWS);
				y += 13;
			}
		}
		return;
	}
	int y = 92;
	infoRow(c, y, tr9("Kind:"), m_kindText);
	infoRow(c, y += ROW_H, tr9("Size:"), m_size);
	infoRow(c, y += ROW_H + 8, tr9("Where:"), m_where);
	infoRow(c, y += ROW_H + 8, tr9("Created:"), m_created);
	infoRow(c, y += ROW_H, tr9("Modified:"), m_modified);
	if (!m_original.isEmpty()) {
		infoRow(c, y += ROW_H, tr9("Original:"), m_original);
	}
	y += 12;

	/* Comments box: click to type. */
	panelText(c, tr9("Comments:"), INSIDE_X, y + 16);
	m_commentBox = QRect(QPoint(INSIDE_X, y + 22), QPoint(BOX_X1 - 9, BOX_Y1 - (m_isTrash ? 34 : 10)));
	pl_fill(c, m_commentBox.left(), m_commentBox.top(), m_commentBox.right(), m_commentBox.bottom(), C_WHITE);
	pl_outline(c, m_commentBox.left(), m_commentBox.top(), m_commentBox.right(), m_commentBox.bottom(),
		m_editingComment ? C_BLACK : GRAY(0x8));

	/* Wrap the comment to the box at Unicode line-break opportunities:
	 * Japanese has no spaces, but still breaks between characters. */
	const int textW = m_commentBox.width() - 8;
	int baseline = m_commentBox.top() + 12;
	for (const QString &line : commentLines()) {
		if (baseline > m_commentBox.bottom() - 2) {
			break;
		}
		Text t(line, textW, PL_FONT_VIEWS);
		if (t.t && t.t->ink_l >= 0) {
			pl_text(c, t.t, m_commentBox.left() + 4 + t.t->ink_l - 1, baseline, C_BLACK);
		}
		baseline += 12;
	}
	if (!m_commentPreedit.isEmpty()) {
		const QPoint at = commentPreeditOrigin();
		Text t(m_commentPreedit, 100000, PL_FONT_VIEWS);
		if (t.t && t.t->ink_l >= 0) {
			pl_text(c, t.t, at.x() + t.t->ink_l - 1, at.y(), C_BLACK);
			pl_hline(c, at.x(), at.x() + t.t->advance, at.y() + 1, C_BLACK);
		}
	}
	if (m_editingComment && m_caretOn) {
		const QPoint caret = commentCaretPos();
		pl_vline(c, caret.x(), caret.y() - 9, caret.y() + 1, C_BLACK);
	}
}

/* ---- Get Info: Sharing ---- */

void InfoWindow::loadSharing() {
	struct stat st;
	if (stat(QFile::encodeName(m_path).constData(), &st) != 0) {
		return;
	}
	m_mine = st.st_uid == getuid() || getuid() == 0;
	const passwd *pw = getpwuid(st.st_uid);
	m_owner = m_origOwner = pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(st.st_uid);
	const group *gr = getgrgid(st.st_gid);
	m_group = m_origGroup = gr ? QString::fromLocal8Bit(gr->gr_name) : QString::number(st.st_gid);
	m_mode = m_origMode = st.st_mode & 0777;

	const QString real = QFileInfo(m_path).canonicalFilePath();
	m_sharedAs.clear();
	m_enclosingShare.clear();
	m_sharedInside.clear();
	for (const SharedFolder &f : sharedFolders()) {
		if (f.path == real) {
			m_sharedAs = f.name;
		} else if (real.startsWith(f.path + '/')) {
			m_enclosingShare = f.name;
		} else if (f.path.startsWith(real + '/')) {
			m_sharedInside = f.name;
		}
	}
	m_share = !m_sharedAs.isEmpty();

	/* Owner: the people with accounts here. User/Group: the groups people
	 * make (a group of their own each, and those an administrator adds)
	 * and "users". */
	QStringList people, groups;
	setpwent();
	while (const passwd *p = getpwent()) {
		if (p->pw_uid >= 1000 && p->pw_uid < 60000) {
			people << QString::fromLocal8Bit(p->pw_name);
		}
	}
	endpwent();
	setgrent();
	while (const group *g = getgrent()) {
		if ((g->gr_gid >= 1000 && g->gr_gid < 60000) || g->gr_gid == 100) {
			groups << QString::fromLocal8Bit(g->gr_name);
		}
	}
	endgrent();
	people << m_owner;
	groups << m_group;
	people.removeDuplicates();
	groups.removeDuplicates();
	people.sort(Qt::CaseInsensitive);
	groups.sort(Qt::CaseInsensitive);
	m_ownerPopup.items.clear();
	for (const QString &p : people) {
		m_ownerPopup.items.push_back(PopupItem{ p });
	}
	m_ownerPopup.chosen = [this, people](int i) {
		m_owner = people[i];
		m_ownerPopup.selected = i;
	};
	m_groupPopup.items.clear();
	for (const QString &g : groups) {
		m_groupPopup.items.push_back(PopupItem{ g });
	}
	m_groupPopup.chosen = [this, groups](int i) {
		m_group = groups[i];
		m_groupPopup.selected = i;
	};
	for (int k = 0; k < 3; k++) {
		m_privilege[k].items.clear();
		m_privilege[k].iconWell = true;
		for (int i = 0; i < 4; i++) {
			PopupItem item{ tr9(PRIVILEGES[i]) };
			item.icon = privilegeIcon(i);
			m_privilege[k].items.push_back(item);
		}
		m_privilege[k].chosen = [this, k](int i) { privilegeChosen(k, i); };
	}

	m_shareBox = PanelCheckbox(tr9("Share this item and its contents"), QPoint(INSIDE_X, SHARE_BOX_Y));
	m_shareBox.toggled = [this](bool on) {
		m_share = on;
		update();
	};
	const int copyW = std::max(60, Text(tr9("Copy"), 200, PL_FONT_SYSTEM).inkWidth() + 16);
	m_copy = PanelButton(tr9("Copy"),
		QRect(PRIV_X1 - 7 - copyW, 324, copyW, PL_BUTTON_H));
	m_copy.clicked = [this] {
		applyPrivileges(true);
		layoutSharing();
		update();
	};
}

/* Puts the controls where they go and shows the current choices. */
void InfoWindow::layoutSharing() {
	m_shareBox.on = m_share;
	m_shareBox.enabled = m_mine && m_sharedInside.isEmpty();
	m_ownerPopup.rect = QRect(WHO_X, ROW1_Y, WHO_W, PL_POPUP_H);
	m_groupPopup.rect = QRect(WHO_X, ROW1_Y + ROW_STEP, WHO_W, PL_POPUP_H);
	m_ownerPopup.selected = 0;
	for (int i = 0; i < static_cast<int>(m_ownerPopup.items.size()); i++) {
		if (m_ownerPopup.items[i].text == m_owner) {
			m_ownerPopup.selected = i;
		}
	}
	m_groupPopup.selected = 0;
	for (int i = 0; i < static_cast<int>(m_groupPopup.items.size()); i++) {
		if (m_groupPopup.items[i].text == m_group) {
			m_groupPopup.selected = i;
		}
	}
	m_ownerPopup.enabled = m_groupPopup.enabled = m_mine;
	for (int k = 0; k < 3; k++) {
		m_privilege[k].rect = QRect(WELL_X, ROW1_Y + k * ROW_STEP, PANEL_WELL_W + 3 + PANEL_ARROWS_W,
			PL_POPUP_H);
		m_privilege[k].selected = privilegeOf((m_mode >> (3 * (2 - k))) & 7);
		m_privilege[k].enabled = m_mine;
	}
	m_copy.enabled = m_mine;
}

/* A privilege for the owner (0), the group (1) or everyone (2). Choosing
 * the one it has already leaves odd Unix bits alone. */
void InfoWindow::privilegeChosen(int who, int choice) {
	const int shift = 3 * (2 - who);
	const int bits = (m_mode >> shift) & 7;
	if (privilegeOf(bits) != choice) {
		m_mode = (m_mode & ~(7 << shift)) | (PRIVILEGE_BITS[choice] << shift);
	}
	m_privilege[who].selected = choice;
	update();
}

QString InfoWindow::sharingNote() const {
	if (!m_mine) {
		return tr9("Only %1, the owner, can change these.").arg(m_origOwner);
	}
	if (!m_sharedInside.isEmpty()) {
		return tr9("This folder can’t be shared, because the folder “%1” inside it is "
			"shared.").arg(m_sharedInside);
	}
	if ((m_share || !m_enclosingShare.isEmpty()) && !sharingOn()) {
		return tr9("File Sharing is off. Turn it on in the File Sharing control panel so others "
			"can connect.");
	}
	if (m_share && !m_sharedAs.isEmpty()) {
		return tr9("Shared on the network as “%1”.").arg(m_sharedAs);
	}
	if (m_share) {
		return tr9("This folder will be shared when you close this window.");
	}
	return QString();
}

void InfoWindow::paintSharing(pl_canvas *c) {
	infoRow(c, 92, tr9("Where:"), m_where);
	int y = NOTE_BASELINE;
	if (!m_enclosingShare.isEmpty()) {
		/* Inside a shared folder: shared with it, under its own privileges. */
		panelText(c, tr9("Shared as part of “%1”.").arg(m_enclosingShare), INSIDE_X,
			SHARE_BOX_Y + PL_CHECKBOX_BASELINE);
	}
	for (const QString &line : panelWrap(sharingNote(), PRIV_X1 - INSIDE_X, PL_FONT_VIEWS)) {
		panelText(c, line, INSIDE_X, y, PL_FONT_VIEWS);
		y += 13;
	}

	pl_group_box_paint(c, PRIV_X0, PRIV_Y0, PRIV_X1, PRIV_Y1, nullptr, FACE);
	const uint32_t ink = m_mine ? C_BLACK : GRAY(0x8);
	panelText(c, tr9("Name"), NAME_COL, HEAD_BASELINE, PL_FONT_VIEWS, ink);
	panelText(c, tr9("Privilege"), PRIV_COL, HEAD_BASELINE, PL_FONT_VIEWS, ink);
	const int base = PL_POPUP_BASELINE(PL_POPUP_H);
	panelLabel(c, tr9("Owner:"), WHO_LABEL_R, ROW1_Y + base, ink);
	panelLabel(c, tr9("User/Group:"), WHO_LABEL_R, ROW1_Y + ROW_STEP + base, ink);
	panelText(c, tr9("Everyone"), NAME_COL, ROW1_Y + 2 * ROW_STEP + base, PL_FONT_VIEWS, ink);
	pl_hline(c, PRIV_X0 + 8, PRIV_X1 - 13, SEP_Y, GRAY(0x8));
	pl_hline(c, PRIV_X0 + 9, PRIV_X1 - 12, SEP_Y + 1, C_WHITE);
	panelText(c, tr9("Copy these privileges to all enclosed folders"), COPY_TEXT_X, COPY_BASELINE,
		PL_FONT_VIEWS, ink);
}

/* Owner, group and privileges to the helper; with `all`, to everything
 * inside too. */
bool InfoWindow::applyPrivileges(bool all) {
	QStringList args{ "privileges", m_path, m_owner, m_group,
		QString::number(m_mode, 8).rightJustified(3, '0') };
	if (all) {
		args << "all";
	}
	bool ok;
	QString err;
	runSharingHelper(args, QByteArray(), &ok, &err);
	if (!ok) {
		Alert::ask(tr9("The privileges of “%1” couldn’t be changed. %2").arg(m_name, err), "OK", "");
		return false;
	}
	m_origOwner = m_owner;
	m_origGroup = m_group;
	m_origMode = m_mode;
	struct stat st;
	if (stat(QFile::encodeName(m_path).constData(), &st) == 0) {
		m_mine = st.st_uid == getuid() || getuid() == 0;
	}
	return true;
}

/* What was changed under Sharing, as the window closes. */
bool InfoWindow::saveSharing() {
	if (!m_mine) {
		return true;
	}
	if ((m_owner != m_origOwner || m_group != m_origGroup || m_mode != m_origMode) &&
			!applyPrivileges(false)) {
		return false;
	}
	if (m_share == m_sharedAs.isEmpty() && m_enclosingShare.isEmpty()) {
		bool ok;
		QString err;
		runSharingHelper({ m_share ? "share" : "unshare", m_path }, QByteArray(), &ok, &err);
		if (!ok) {
			Alert::ask(m_share
				? tr9("“%1” couldn’t be shared. %2").arg(m_name, err)
				: tr9("“%1” couldn’t be unshared. %2").arg(m_name, err), "OK", "");
			return false;
		}
		/* Its icon changes wherever it shows. */
		refreshIconFolder();
	}
	return true;
}

void InfoWindow::closeEvent(QCloseEvent *e) {
	setEditingComment(false);
	if (m_show.items.size() > 1 && m_show.items[1].enabled) {
		saveSharing();
	}
	e->accept();
}

void InfoWindow::saveComment() {
	const QByteArray name = QFile::encodeName(m_path);
	const QByteArray value = m_comment.toUtf8();
	if (value.isEmpty()) {
		removexattr(name.constData(), COMMENT_ATTR);
	} else {
		setxattr(name.constData(), COMMENT_ATTR, value.constData(), value.size(), 0);
	}
}

/* The comment's lines, with explicit newlines kept. */
QStringList InfoWindow::commentLines() const {
	QStringList lines;
	if (m_commentBox.width() <= 8) {
		return lines;
	}
	const int textW = m_commentBox.width() - 8;
	for (const QString &para : m_comment.split('\n')) {
		if (para.isEmpty()) {
			lines << QString();
			continue;
		}
		lines += wrapText(para, textW, PL_FONT_VIEWS);
	}
	if (lines.isEmpty()) {
		lines << QString();
	}
	return lines;
}

/* Where the input method's preedit begins: after the committed text, on
 * the last line if it fits, else on the next one. */
QPoint InfoWindow::commentPreeditOrigin() const {
	int baseline = m_commentBox.top() + 12;
	int caretX = m_commentBox.left() + 4;
	for (const QString &line : commentLines()) {
		if (baseline > m_commentBox.bottom() - 2) {
			break;
		}
		caretX = m_commentBox.left() + 4 + textAdvance(line);
		baseline += 12;
	}
	baseline -= 12;
	const int textW = m_commentBox.width() - 8;
	if (textAdvance(m_commentPreedit) > 0 &&
			caretX + textAdvance(m_commentPreedit) > m_commentBox.left() + 4 + textW) {
		caretX = m_commentBox.left() + 4;
		baseline += 12;
	}
	return QPoint(caretX, baseline);
}

QPoint InfoWindow::commentCaretPos() const {
	const QPoint at = commentPreeditOrigin();
	const int cursor = std::clamp(m_commentPreeditCursor, 0,
		static_cast<int>(m_commentPreedit.size()));
	return QPoint(at.x() + textAdvance(m_commentPreedit.left(cursor)), at.y());
}

QRect InfoWindow::commentCursorRect() const {
	const QPoint at = commentCaretPos();
	return QRect(at.x(), at.y() - 11, 1, 14);
}

void InfoWindow::setEditingComment(bool editing) {
	if (editing == m_editingComment) {
		return;
	}
	m_editingComment = editing;
	if (editing) {
		/* The comment, not a control panel field, takes the typing. */
		m_host.setFocus(nullptr);
		setFocus(Qt::MouseFocusReason);
		m_caretTimer.start(QApplication::cursorFlashTime() / 2);
	} else {
		/* Keep what the input method had composed when the focus leaves. */
		if (!m_commentPreedit.isEmpty()) {
			m_comment += m_commentPreedit;
			m_comment = m_comment.left(COMMENT_MAX);
			m_commentPreedit.clear();
			m_commentPreeditCursor = 0;
			saveComment();
		}
		m_caretTimer.stop();
	}
	setAttribute(Qt::WA_InputMethodEnabled, editing);
	setProperty("zacos9TextField", editing);
	if (QInputMethod *ime = QGuiApplication::inputMethod()) {
		ime->update(Qt::ImQueryAll);
	}
	m_caretOn = true;
	update();
}

void InfoWindow::inputMethodEvent(QInputMethodEvent *e) {
	if (!m_editingComment) {
		return;
	}
	int cursor = static_cast<int>(e->preeditString().size());
	for (const QInputMethodEvent::Attribute &a : e->attributes()) {
		if (a.type == QInputMethodEvent::Cursor) {
			cursor = a.start;
		}
	}
	QString commit;
	for (QChar ch : e->commitString()) {
		if (ch.isPrint()) {
			commit += ch;
		}
	}
	if (!commit.isEmpty()) {
		m_comment += commit;
		m_comment = m_comment.left(COMMENT_MAX);
		saveComment();
	}
	m_commentPreedit = e->preeditString();
	m_commentPreeditCursor = std::clamp(cursor, 0, static_cast<int>(m_commentPreedit.size()));
	m_caretOn = true;
	update();
}

QVariant InfoWindow::inputMethodQuery(Qt::InputMethodQuery query) const {
	if (!m_editingComment) {
		return QWidget::inputMethodQuery(query);
	}
	switch (query) {
	case Qt::ImEnabled:
		return true;
	case Qt::ImHints:
		return static_cast<int>(Qt::ImhNoPredictiveText | Qt::ImhMultiLine);
	case Qt::ImSurroundingText:
		return m_comment;
	case Qt::ImCursorPosition:
		return m_comment.size();
	case Qt::ImAnchorPosition:
		return m_comment.size();
	case Qt::ImCurrentSelection:
		return QString();
	case Qt::ImTextBeforeCursor:
		return m_comment;
	case Qt::ImTextAfterCursor:
		return QString();
	case Qt::ImCursorRectangle:
		return commentCursorRect();
	case Qt::ImInputItemClipRectangle:
		return m_commentBox;
	case Qt::ImReadOnly:
		return false;
	case Qt::ImPreferredLanguage:
		return QLocale().name();
	default:
		return {};
	}
}

void InfoWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	/* A click outside the comments box ends the comment before it does
	 * whatever it was aimed at (a button, the icon, the Show pop-up). */
	if (!m_commentBox.contains(pos)) {
		setEditingComment(false);
	}
	if (m_host.hostPress(e) || m_view != 0) {
		return;
	}
	if (canChangeIcon() && QRect(ICON_BOX_X, ICON_BOX_Y, 38, 38).contains(pos)) {
		m_iconFocused = true;
		m_iconPressPos = pos;
		setEditingComment(false);
		setFocus(Qt::MouseFocusReason);
		update();
		return;
	}
	m_iconFocused = false;
	setEditingComment(m_commentBox.contains(pos));
}

void InfoWindow::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
	/* Dragging the icon copies it to another Get Info window, as on a Mac. */
	if (m_iconFocused && (e->buttons() & Qt::LeftButton) && canChangeIcon()) {
		const int dist = (e->position().toPoint() - m_iconPressPos).manhattanLength();
		if (dist >= QApplication::startDragDistance()) {
			const QImage icon = currentIconImage();
			if (!icon.isNull()) {
				QMimeData *mime = new QMimeData;
				mime->setImageData(icon);
				QDrag *drag = new QDrag(this);
				drag->setMimeData(mime);
				drag->setPixmap(QPixmap::fromImage(icon));
				drag->exec(Qt::CopyAction);
				m_iconFocused = false;
				update();
			}
		}
	}
}

void InfoWindow::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
}

void InfoWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (!canChangeIcon()) {
		return;
	}
	if (!QRect(ICON_BOX_X, ICON_BOX_Y, 38, 38).contains(e->position().toPoint())) {
		return;
	}
	const QMimeData *mime = e->mimeData();
	if (mime->hasImage()) {
		e->acceptProposedAction();
		return;
	}
	if (mime->hasUrls()) {
		for (const QUrl &url : mime->urls()) {
			if (url.isLocalFile()) {
				const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
				if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" ||
						suffix == "gif" || suffix == "bmp" || suffix == "tiff" ||
						suffix == "tif" || suffix == "webp" || suffix == "ico") {
					e->acceptProposedAction();
					return;
				}
			}
		}
	}
}

void InfoWindow::dropEvent(QDropEvent *e) {
	if (!canChangeIcon()) {
		return;
	}
	if (!QRect(ICON_BOX_X, ICON_BOX_Y, 38, 38).contains(e->position().toPoint())) {
		return;
	}
	const QMimeData *mime = e->mimeData();
	QImage image;
	if (mime->hasImage()) {
		image = qvariant_cast<QImage>(mime->imageData());
	} else if (mime->hasUrls()) {
		for (const QUrl &url : mime->urls()) {
			if (url.isLocalFile()) {
				const QString path = url.toLocalFile();
				const QString suffix = QFileInfo(path).suffix().toLower();
				if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" ||
						suffix == "gif" || suffix == "bmp" || suffix == "tiff" ||
						suffix == "tif" || suffix == "webp" || suffix == "ico") {
					image = QImage(path);
					if (!image.isNull()) {
						break;
					}
				}
			}
		}
	}
	if (image.isNull()) {
		return;
	}
	if (!writeCustomIconImage(m_iconPath, image)) {
		Alert::ask(tr9("This item's icon couldn't be changed. The file system may not "
			"support custom icons, or the picture may be too large."), "OK", "");
	} else {
		update();
		refreshIconFolder();
	}
	e->acceptProposedAction();
}

void InfoWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	const bool command = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
	/* With no comment being typed, Command-C and Command-V belong to the
	 * icon even before it is clicked: it is the only thing here to copy. */
	if (!m_editingComment && canChangeIcon() && command &&
			(e->key() == Qt::Key_C || e->key() == Qt::Key_V)) {
		if (e->key() == Qt::Key_C) {
			const QImage icon = currentIconImage();
			if (!icon.isNull()) {
				QApplication::clipboard()->setImage(icon);
			}
		} else {
			const QImage image = QApplication::clipboard()->image();
			if (image.isNull()) {
				/* A stray paste of text isn't an error, but an explicit
				 * paste after clicking the icon gets an explanation. */
				if (m_iconFocused) {
					Alert::ask(tr9("The Clipboard doesn’t contain a picture to use as an icon."), "OK", "");
				}
			} else if (!writeCustomIconImage(m_iconPath, image)) {
				Alert::ask(tr9("This item’s icon couldn’t be changed. The file system may not support "
					"custom icons, or the picture may be too large."), "OK", "");
			} else {
				update();
				refreshIconFolder();
			}
		}
		return;
	}
	if (m_iconFocused && canChangeIcon() &&
			(e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete)) {
		if (!clearCustomIconImage(m_iconPath)) {
			Alert::ask(tr9("This item’s custom icon couldn’t be removed."), "OK", "");
		} else {
			update();
			refreshIconFolder();
		}
		return;
	}
	if (!m_editingComment) {
		return;
	}
	/* While composing, the keys belong to the input method. */
	if (!m_commentPreedit.isEmpty()) {
		if (e->key() == Qt::Key_Escape) {
			m_commentPreedit.clear();
			m_commentPreeditCursor = 0;
			update();
		} else if (e->key() == Qt::Key_Backspace && m_commentPreeditCursor > 0) {
			m_commentPreedit.remove(m_commentPreeditCursor - 1, 1);
			--m_commentPreeditCursor;
			update();
		}
		return;
	}
	if (e->key() == Qt::Key_Backspace) {
		m_comment.chop(1);
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		m_comment += '\n';
	} else if (e->key() == Qt::Key_Escape) {
		setEditingComment(false);
		return;
	} else if (!e->text().isEmpty() && e->text().at(0).isPrint() &&
			!(e->modifiers() & Qt::ControlModifier)) {
		m_comment += e->text();
	} else {
		return;
	}
	m_comment = m_comment.left(COMMENT_MAX);
	m_caretOn = true;
	saveComment();
	update();
}

/* ---- About This Computer -------------------------------------------------- */

static QPointer<AboutWindow> &aboutWindow() {
	static QPointer<AboutWindow> window;
	return window;
}

void AboutWindow::open() {
	if (!aboutWindow()) {
		aboutWindow() = new AboutWindow;
	}
	aboutWindow()->show();
	aboutWindow()->raise();
}

static QString cpuName() {
	QFile f("/proc/cpuinfo");
	if (!f.open(QIODevice::ReadOnly)) { return {}; }
	for (const QByteArray &raw : f.readAll().split('\n')) {
		if (!raw.startsWith("model name")) { continue; }
		QString s = QString::fromUtf8(raw.mid(raw.indexOf(':') + 1)).trimmed();
		s.remove("(R)"); s.remove("(TM)");
		while (s.contains("  ")) { s.replace("  ", " "); }
		return s.trimmed();
	}
	return {};
}

static qint64 meminfoKB(const QString &key) {
	QFile f("/proc/meminfo");
	if (!f.open(QIODevice::ReadOnly)) {
		return 0;
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		if (line.startsWith(key.toLatin1() + ":")) {
			return line.mid(key.size() + 1).trimmed().split(' ').value(0).toLongLong();
		}
	}
	return 0;
}

static QString memorySize(qint64 kb) {
	if (kb <= 0) {
		return "Not reported";
	}
	const double gb = kb / (1024.0 * 1024.0);
	return QLocale(QLocale::English).toString(gb, 'f', 1) + " GB";
}

AboutWindow::AboutWindow() {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr9("About This Computer"));
	const QString cpu = cpuName();
	const QString gpu = gpuDescription();
	const QString version = systemInformation().value(0).section(": ", 1);
	m_version = version.isEmpty() || version.startsWith("not reported", Qt::CaseInsensitive) ?
		tr9("System version not reported") : tr9("Version %1").arg(version);
	m_lines << tr9("Processor:") + "\t" + (cpu.isEmpty() ? tr9("Not reported") : cpu)
		<< tr9("Graphics:") + "\t" + (gpu.isEmpty() ? tr9("Not reported") : gpu);
	const qint64 swap = meminfoKB("SwapTotal");
	m_lines << tr9("Memory:") + "\t" + memorySize(meminfoKB("MemTotal"))
		<< tr9("Virtual Memory:") + "\t" + (swap > 0 ?
			tr9("%1 allocated on disk").arg(memorySize(swap)) : tr9("None configured"))
		<< tr9("Available Memory:") + "\t" + memorySize(meminfoKB("MemAvailable"));
	setFixedSize(620, 120 + m_lines.size() * 28 + 18);
}

void AboutWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	pl_image_blend(c, 22, 10, logo_pixels_hq(), PL_LOGO_SIZE_HQ, PL_LOGO_SIZE_HQ, false);
	Text title("ZacOS 9", 450, PL_FONT_SYSTEM);
	pl_text(c, title.t, 104, 34, C_BLACK);
	Text release(m_version, 450, PL_FONT_VIEWS);
	pl_text(c, release.t, 104, 55, GRAY(0x4));
	separator(c, 14, W - 15, 82);

	const int panelTop = 98;
	pl_fill(c, 19, panelTop, W - 20, H - 17, C_WHITE);
	pl_hline(c, 19, W - 20, panelTop, GRAY(0x8));
	pl_vline(c, 19, panelTop, H - 17, GRAY(0x8));
	pl_hline(c, 20, W - 21, H - 17, C_WHITE);
	pl_vline(c, W - 20, panelTop, H - 18, C_WHITE);
	int y = 119;
	for (const QString &line : m_lines) {
		Text label(line.section('\t', 0, 0), 148, PL_FONT_SYSTEM);
		pl_text(c, label.t, 36, y, GRAY(0x3));
		Text value(line.section('\t', 1), W - 220, PL_FONT_VIEWS);
		pl_text(c, value.t, 190, y, C_BLACK);
		y += 28;
	}
	QPainter p(this);
	px.blit(p);
}

void AboutWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
	}
}
