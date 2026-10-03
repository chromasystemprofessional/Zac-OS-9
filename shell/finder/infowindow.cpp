#include "infowindow.h"

#include "vfs.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QSysInfo>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "alert.h"
#include "finder.h"
#include "items.h"
#include "logo.h"
#include "sharingclient.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int LABEL_RIGHT = 74; /* right edge of "Kind:" etc. */
static constexpr int VALUE_X = 82;
static constexpr int ROW_H = 16;
static constexpr int COMMENT_MAX = 200; /* Mac OS Finder comment limit */
static const char *const COMMENT_ATTR = "user.xdg.comment";

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

static const char *const PRIVILEGES[] = { "Read & Write", "Read only", "Write only (Drop Box)", "None" };
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
}

InfoWindow::InfoWindow(const QString &path, pl_icon_kind kind, const QString &name)
	: m_path(path), m_name(name), m_kind(kind) {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(name + " Info");

	QFileInfo info(path);
	if (kind == PL_ICON_DISK) {
		m_kindText = "disk";
	} else if (info.isDir()) {
		m_kindText = "folder";
	} else {
		static QMimeDatabase db;
		m_kindText = db.mimeTypeForFile(info).comment();
		if (kind == PL_ICON_APPLICATION) {
			m_kindText = "application program";
		}
	}
	m_where = macPath(path == "/" ? "/" : info.absolutePath());

	/* Show: folders can be shared; files, aliases and disks can't. */
	const bool folder = info.isDir() && !info.isSymLink() && kind != PL_ICON_DISK;
	m_show.rect = QRect(SHOW_X, SHOW_Y, SHOW_W, PL_POPUP_H);
	m_show.items = { PopupItem{ "General Information" }, PopupItem{ "Sharing", folder } };
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
		m_size = finderSize(info.size()) + " on disk (" + en.toString(info.size()) + " bytes)";
		return;
	}
	/* Folders: add it up off the main thread. */
	m_size = "calculating…";
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
		QString text = finderSize(bytes) + QStringLiteral(" on disk, for %1 %2")
			.arg(QLocale(QLocale::English).toString(items), items == 1 ? "item" : "items");
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
		if (Alert::ask("Folders cannot be shared until file sharing is turned on using the "
				"File Sharing control panel. Do you want the control panel opened now?",
				"OK", "Cancel")) {
			QProcess::startDetached("zacos9-filesharing", {});
		}
		view = m_view;
		m_show.selected = view;
		update();
		return false;
	}
	m_view = view;
	m_show.selected = view;
	m_host.popups = { &m_show };
	m_host.checks.clear();
	m_host.buttons.clear();
	if (view == 1) {
		m_editingComment = false;
		m_caretTimer.stop();
		layoutSharing();
		m_host.popups.insert(m_host.popups.end(),
			{ &m_ownerPopup, &m_groupPopup, &m_privilege[0], &m_privilege[1], &m_privilege[2] });
		if (m_enclosingShare.isEmpty()) {
			m_host.checks = { &m_shareBox };
		}
		m_host.buttons = { &m_copy };
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

/* The item's icon (framed) and name, and the group box the Show pop-up
 * titles. */
void InfoWindow::paintHeader(pl_canvas *c) {
	pl_outline(c, ICON_BOX_X, ICON_BOX_Y, ICON_BOX_X + 37, ICON_BOX_Y + 37, C_BLACK);
	pl_icon_paint(c, ICON_BOX_X + 3, ICON_BOX_Y + 3,
		m_kind == PL_ICON_FOLDER && !m_sharedAs.isEmpty() ? PL_ICON_SHARED_FOLDER : m_kind,
		PL_ICON_LARGE, false);
	panelText(c, m_name, NAME_X, NAME_BASELINE, PL_FONT_SYSTEM, C_BLACK, INFO_W - NAME_X - 10);
	pl_group_box_paint(c, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, nullptr, FACE);
	/* The pop-up and its label stand in the box's top line. */
	pl_fill(c, BOX_X0 + 12, BOX_Y0, SHOW_X + SHOW_W + 1, BOX_Y0 + 1, FACE);
	panelLabel(c, "Show:", SHOW_LABEL_R, SHOW_Y + PL_POPUP_BASELINE(PL_POPUP_H));
}

/* A label ending at LABEL_R and its value in the small font. */
static void infoRow(pl_canvas *c, int baseline, const QString &label, const QString &value) {
	panelLabel(c, label, LABEL_R, baseline);
	panelText(c, value, VALUE_X2, baseline, PL_FONT_VIEWS, C_BLACK, BOX_X1 - VALUE_X2 - 8);
}

void InfoWindow::paintGeneral(pl_canvas *c) {
	int y = 92;
	infoRow(c, y, "Kind:", m_kindText);
	infoRow(c, y += ROW_H, "Size:", m_size);
	infoRow(c, y += ROW_H + 8, "Where:", m_where);
	infoRow(c, y += ROW_H + 8, "Created:", m_created);
	infoRow(c, y += ROW_H, "Modified:", m_modified);
	y += 12;

	/* Comments box: click to type. */
	panelText(c, "Comments:", INSIDE_X, y + 16);
	m_commentBox = QRect(QPoint(INSIDE_X, y + 22), QPoint(BOX_X1 - 9, BOX_Y1 - 10));
	pl_fill(c, m_commentBox.left(), m_commentBox.top(), m_commentBox.right(), m_commentBox.bottom(), C_WHITE);
	pl_outline(c, m_commentBox.left(), m_commentBox.top(), m_commentBox.right(), m_commentBox.bottom(),
		m_editingComment ? C_BLACK : GRAY(0x8));

	/* Wrap the comment to the box, word by word. */
	const int textW = m_commentBox.width() - 8;
	QStringList lines;
	for (const QString &para : m_comment.split('\n')) {
		QString line;
		for (const QString &word : para.split(' ')) {
			QString candidate = line.isEmpty() ? word : line + ' ' + word;
			Text probe(candidate, 100000, PL_FONT_VIEWS);
			if (!line.isEmpty() && probe.inkWidth() > textW) {
				lines << line;
				line = word;
			} else {
				line = candidate;
			}
		}
		lines << line;
	}
	int baseline = m_commentBox.top() + 12, caretX = m_commentBox.left() + 4;
	for (const QString &line : lines) {
		if (baseline > m_commentBox.bottom() - 2) {
			break;
		}
		Text t(line, textW, PL_FONT_VIEWS);
		if (t.t && t.t->ink_l >= 0) {
			pl_text(c, t.t, m_commentBox.left() + 4 + t.t->ink_l - 1, baseline, C_BLACK);
		}
		caretX = m_commentBox.left() + 4 + (t.t ? t.t->advance : 0);
		baseline += 12;
	}
	if (m_editingComment && m_caretOn) {
		pl_vline(c, caretX, baseline - 12 - 9, baseline - 12 + 1, C_BLACK);
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
			PopupItem item{ PRIVILEGES[i] };
			item.icon = privilegeIcon(i);
			m_privilege[k].items.push_back(item);
		}
		m_privilege[k].chosen = [this, k](int i) { privilegeChosen(k, i); };
	}

	m_shareBox = PanelCheckbox("Share this item and its contents", QPoint(INSIDE_X, SHARE_BOX_Y));
	m_shareBox.toggled = [this](bool on) {
		m_share = on;
		update();
	};
	m_copy = PanelButton("Copy", QRect(254, 324, 60, PL_BUTTON_H));
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
		return QString("Only %1, the owner, can change these.").arg(m_origOwner);
	}
	if (!m_sharedInside.isEmpty()) {
		return QString("This folder can’t be shared, because the folder “%1” inside it is "
			"shared.").arg(m_sharedInside);
	}
	if ((m_share || !m_enclosingShare.isEmpty()) && !sharingOn()) {
		return "File Sharing is off. Turn it on in the File Sharing control panel so others "
		       "can connect.";
	}
	if (m_share && !m_sharedAs.isEmpty()) {
		return QString("Shared on the network as “%1”.").arg(m_sharedAs);
	}
	if (m_share) {
		return "This folder will be shared when you close this window.";
	}
	return QString();
}

void InfoWindow::paintSharing(pl_canvas *c) {
	infoRow(c, 92, "Where:", m_where);
	int y = NOTE_BASELINE;
	if (!m_enclosingShare.isEmpty()) {
		/* Inside a shared folder: shared with it, under its own privileges. */
		panelText(c, QString("Shared as part of “%1”.").arg(m_enclosingShare), INSIDE_X,
			SHARE_BOX_Y + PL_CHECKBOX_BASELINE);
	}
	for (const QString &line : panelWrap(sharingNote(), PRIV_X1 - INSIDE_X, PL_FONT_VIEWS)) {
		panelText(c, line, INSIDE_X, y, PL_FONT_VIEWS);
		y += 13;
	}

	pl_group_box_paint(c, PRIV_X0, PRIV_Y0, PRIV_X1, PRIV_Y1, nullptr, FACE);
	const uint32_t ink = m_mine ? C_BLACK : GRAY(0x8);
	panelText(c, "Name", NAME_COL, HEAD_BASELINE, PL_FONT_VIEWS, ink);
	panelText(c, "Privilege", PRIV_COL, HEAD_BASELINE, PL_FONT_VIEWS, ink);
	const int base = PL_POPUP_BASELINE(PL_POPUP_H);
	panelLabel(c, "Owner:", WHO_LABEL_R, ROW1_Y + base, ink);
	panelLabel(c, "User/Group:", WHO_LABEL_R, ROW1_Y + ROW_STEP + base, ink);
	panelText(c, "Everyone", NAME_COL, ROW1_Y + 2 * ROW_STEP + base, PL_FONT_VIEWS, ink);
	pl_hline(c, PRIV_X0 + 8, PRIV_X1 - 13, SEP_Y, GRAY(0x8));
	pl_hline(c, PRIV_X0 + 9, PRIV_X1 - 12, SEP_Y + 1, C_WHITE);
	panelText(c, "Copy these privileges to all enclosed folders", COPY_TEXT_X, COPY_BASELINE,
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
		Alert::ask(QString("The privileges of “%1” couldn’t be changed. %2").arg(m_name, err), "OK", "");
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
			Alert::ask(QString("“%1” couldn’t be %2. %3")
				.arg(m_name, m_share ? "shared" : "unshared", err), "OK", "");
			return false;
		}
		/* Its icon changes wherever it shows. */
		Finder::instance().folderChanged(QFileInfo(m_path).absolutePath());
	}
	return true;
}

void InfoWindow::closeEvent(QCloseEvent *e) {
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

void InfoWindow::mousePressEvent(QMouseEvent *e) {
	if (m_host.hostPress(e) || m_view != 0) {
		return;
	}
	const bool inBox = m_commentBox.contains(e->position().toPoint());
	if (inBox != m_editingComment) {
		m_editingComment = inBox;
		m_caretOn = true;
		inBox ? m_caretTimer.start(QApplication::cursorFlashTime() / 2) : m_caretTimer.stop();
		update();
	}
}

void InfoWindow::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
}

void InfoWindow::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
}

void InfoWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (!m_editingComment) {
		return;
	}
	if (e->key() == Qt::Key_Backspace) {
		m_comment.chop(1);
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		m_comment += '\n';
	} else if (e->key() == Qt::Key_Escape) {
		m_editingComment = false;
		m_caretTimer.stop();
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

static QString gpuName() {
	/* NVIDIA: the nvidia driver exposes a "Model:" line per GPU. */
	const QDir nvgpus("/proc/driver/nvidia/gpus");
	if (nvgpus.exists()) {
		for (const QString &gpu : nvgpus.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			QFile info("/proc/driver/nvidia/gpus/" + gpu + "/information");
			if (!info.open(QIODevice::ReadOnly)) { continue; }
			for (const QByteArray &line : info.readAll().split('\n')) {
				if (line.startsWith("Model:")) {
					return QString::fromUtf8(line.mid(6)).trimmed();
				}
			}
		}
	}
	/* Other drivers: probe DRM card nodes via sysfs uevent. */
	static const QStringList virtualDrivers = {
		"virtio_gpu", "virtio-pci", "bochs-drm", "qxl",
		"cirrusfb", "simpledrm", "vboxvideo", "vmwgfx"
	};
	for (const char *card : {"card0", "card1"}) {
		QFile ue(QStringLiteral("/sys/class/drm/") + card + "/device/uevent");
		if (!ue.open(QIODevice::ReadOnly)) { continue; }
		QString driver;
		for (const QString &line : QString::fromUtf8(ue.readAll()).split('\n')) {
			if (line.startsWith("DRIVER=")) { driver = line.mid(7); break; }
		}
		if (driver.isEmpty() || virtualDrivers.contains(driver)) { continue; }
		if (driver == "i915" || driver == "xe") { return "Intel GPU"; }
		if (driver == "amdgpu" || driver == "radeon") { return "AMD GPU"; }
		return driver + " GPU";
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

static QString megabytes(qint64 kb) {
	return QLocale(QLocale::English).toString(kb / 1024) + " MB";
}

AboutWindow::AboutWindow() {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("About This Computer");
	const QString cpu = cpuName();
	const QString gpu = gpuName();
	if (!cpu.isEmpty()) { m_lines << "Processor:\t" + cpu; }
	if (!gpu.isEmpty()) { m_lines << "Graphics:\t" + gpu; }
	const qint64 swap = meminfoKB("SwapTotal");
	m_lines << "Built-in Memory:\t" + megabytes(meminfoKB("MemTotal"))
	        << "Virtual Memory:\t" + (swap > 0 ? megabytes(swap) + " used on disk"
	                                           : QStringLiteral("Off"))
	        << "Largest Unused Block:\t" + megabytes(meminfoKB("MemAvailable"));
	/* 94px to first line, 20px per line, 22px bottom margin. */
	setFixedSize(360, 94 + m_lines.size() * 20 + 22);
}

void AboutWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* The logo, 3x, like the big Mac OS logo of the original. */
	const uint32_t *logo = logo_pixels();
	for (int y = 0; y < PL_LOGO_SIZE * 3; y++) {
		for (int x = 0; x < PL_LOGO_SIZE * 3; x++) {
			uint32_t v = logo[(y / 3) * PL_LOGO_SIZE + x / 3];
			if (v >> 24) {
				pl_put(c, 16 + x, 10 + y, v);
			}
		}
	}
	Text title("ZacOS 9", 300, PL_FONT_SYSTEM);
	pl_text(c, title.t, 80, 30, C_BLACK);
	QString kernel = QSysInfo::kernelType();
	kernel[0] = kernel[0].toUpper(); /* "Linux" */
	Text version("Version 0.3 — " + kernel + " " +
		QSysInfo::kernelVersion().section('-', 0, 0), 260, PL_FONT_VIEWS);
	pl_text(c, version.t, 80, 46, C_BLACK);
	separator(c, 10, W - 11, 70);

	int y = 94;
	for (const QString &line : m_lines) {
		Text label(line.section('\t', 0, 0), 200, PL_FONT_SYSTEM);
		pl_text(c, label.t, 180 - label.inkWidth(), y, C_BLACK);
		Text value(line.section('\t', 1), 160, PL_FONT_VIEWS);
		pl_text(c, value.t, 190, y, C_BLACK);
		y += 20;
	}
	/* TODO: Mac OS 8 also charted each running program's memory. */
	QPainter p(this);
	px.blit(p);
}

void AboutWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
	}
}
