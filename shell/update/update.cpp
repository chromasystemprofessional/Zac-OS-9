#include "update.h"
#include "platinumshell.h"
#include <cstring>
#include <QApplication>

#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>
#include <memory>

#include "icons.h"
#include "pixels.h"
#include "settings.h"
#include "storeclient.h"

static constexpr int W = 420, H = 150;
static constexpr int TEXT_X = 60, TEXT_W = W - TEXT_X - 14;
static constexpr int BUTTON_W = 80;
static constexpr uint32_t FACE = GRAY(0xD);
static const QString PACKAGE = QStringLiteral("zacos9");

/* What's New: the release notes in a scrolling list, Platinum style.
 * `actions` adds the post-restart buttons that open Theme Files and the
 * Appearance panel's Themes tab. */
class NotesDialog : public QDialog {
public:
	NotesDialog(QWidget *parent, const QString &title, const QString &notes, bool actions)
		: QDialog(parent), m_title(title) {
		setWindowTitle(title);
		setFixedSize(NW, NH);
		m_list.frame = QRect(14, 36, NW - 28, 13 * PL_LIST_ROW_H + 2);
		m_host.hintAreas.push_back({ &m_list.frame, "What changed in this version. Arrow keys scroll." });
		m_list.setItems(panelWrap(notes.trimmed(), NW - 28 - SB_WIDTH - 8, PL_FONT_VIEWS));
		m_list.state.selected = -1;
		const int by = NH - 14 - PL_BUTTON_H;
		m_ok = PanelButton("OK", QRect(NW - 14 - BUTTON_W, by, BUTTON_W, PL_BUTTON_H), true);
		m_ok.hint = "Closes this window.";
		m_ok.clicked = [this] { accept(); };
		m_host.buttons.push_back(&m_ok);
		m_host.defaultButton = &m_ok;
		m_host.cancelButton = &m_ok;
		if (actions) {
			m_files = PanelButton("Open Theme Files",
				QRect(NW - 14 - BUTTON_W - 12 - 130, by, 130, PL_BUTTON_H));
			m_files.hint = "Opens Appearance's Theme Files folder in the Sniffer.";
			m_files.clicked = [] {
				QProcess::startDetached("zacos9-finder", { "--open", appearanceThemesDir() });
			};
			m_appearance = PanelButton("Appearance…",
				QRect(NW - 14 - BUTTON_W - 12 - 130 - 12 - 110, by, 110, PL_BUTTON_H));
			m_appearance.hint = "Opens the Appearance control panel on its Themes tab.";
			m_appearance.clicked = [] {
				QProcess::startDetached("zacos9-appearance", { "themes" });
			};
			m_host.buttons.push_back(&m_files);
			m_host.buttons.push_back(&m_appearance);
		}
	}

	static QString appearanceThemesDir() {
		return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
			"/zacos9/appearance/Theme Files";
	}

protected:
	void showEvent(QShowEvent *e) override {
		QDialog::showEvent(e);
		platinumSetFrameStyle(this, FrameStyle::MovableModal);
	}
	void paintEvent(QPaintEvent *) override {
		Pixels px(NW, NH);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, NW - 1, NH - 1, FACE);
		panelText(c, m_title, 14, 26, PL_FONT_SYSTEM, C_BLACK, NW - 28);
		m_list.paint(c, true);
		m_host.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (!m_list.press(e->position().toPoint())) {
			m_host.hostPress(e);
		}
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		m_list.move(e->position().toPoint());
		m_host.hostMove(e);
		update();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		m_list.release();
		m_host.hostRelease(e);
		update();
	}
	void wheelEvent(QWheelEvent *e) override {
		m_list.wheel(e->position().toPoint(), e->angleDelta().y());
		update();
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_list.key(e->key(), e->text()) && !m_host.hostKey(e)) {
			QDialog::keyPressEvent(e);
		}
		update();
	}

private:
	QString m_title;
	static constexpr int NW = 480, NH = 320;
	PanelList m_list;
	PanelButton m_ok, m_files, m_appearance;
	PanelHost m_host{ this };

public:
	static void show(QWidget *parent, const QString &title, const QString &notes, bool actions) {
		NotesDialog dlg(parent, title, notes, actions);
		dlg.exec();
	}
};

void showWhatsNew(QWidget *parent, const QString &version, const QString &notes) {
	NotesDialog::show(parent, "What's New in ZacOS 9 " + version, notes, true);
}

/* zacos9-update --whatsnew: the post-restart window, once per version.
 * The notes ship in the package (/usr/share/zacos9/whatsnew/<version>.txt);
 * the seen version lives in ~/.config/zacos9/whatsnew-seen. */
int whatsnewRun() {
	const QString installed = installedVersion(PACKAGE);
	QString seen;
	QFile seenFile(whatsnewSeenFile());
	if (seenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		seen = QString::fromUtf8(seenFile.readAll()).trimmed();
	}
	if (!whatsnewShouldShow(installed, seen)) {
		return 0;
	}
	QFile notes(whatsnewNotesFile(installed));
	if (notes.open(QIODevice::ReadOnly | QIODevice::Text)) {
		showWhatsNew(nullptr, installed, QString::fromUtf8(notes.readAll()));
	}
	QDir().mkpath(QFileInfo(whatsnewSeenFile()).absolutePath());
	if (seenFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		seenFile.write(installed.toUtf8());
	}
	return 0;
}

UpdateWindow::UpdateWindow() {
	setWindowTitle("Software Update");
	setFixedSize(W, H);
	m_daily = PanelCheckbox("Check daily", QPoint(14, H - 14 - PL_BUTTON_H + 4));
	char v[16];
	m_daily.on = !(pl_setting("update-check", v, sizeof(v)) && strcmp(v, "never") == 0);
	m_daily.hint = "Looks for updates every day, and shows this window only if there are some.";
	m_daily.toggled = [](bool on) { pl_setting_set("update-check", on ? nullptr : "never"); };
	m_host.checks = { &m_daily };
	m_sweep.setInterval(30);
	m_sweep.callOnTimeout([this] {
		m_sweepPos += 0.03;
		if (m_sweepPos > 1.3) {
			m_sweepPos = -0.3;
		}
		update();
	});
}

void UpdateWindow::setText(const QString &headline, const QString &detail) {
	m_headline = headline;
	m_lines = panelWrap(detail, TEXT_W, PL_FONT_VIEWS);
	update();
}

/* The default button at the right (Return), the other beside it (Escape);
 * an empty label leaves that button out. */
void UpdateWindow::setButtons(const QString &primary, const QString &secondary) {
	const int y = H - 14 - PL_BUTTON_H;
	const QRect right(W - 14 - BUTTON_W, y, BUTTON_W, PL_BUTTON_H);
	const QRect left(right.x() - 12 - BUTTON_W, y, BUTTON_W, PL_BUTTON_H);
	m_host.buttons.clear();
	m_host.defaultButton = m_host.cancelButton = nullptr;
	if (!primary.isEmpty()) {
		m_primary = PanelButton(primary, right, true);
		m_primary.clicked = [this] { primaryClicked(); };
		m_host.buttons.push_back(&m_primary);
		m_host.defaultButton = &m_primary;
	}
	if (!secondary.isEmpty()) {
		m_secondary = PanelButton(secondary, primary.isEmpty() ? right : left);
		m_secondary.clicked = [this] { secondaryClicked(); };
		m_host.buttons.push_back(&m_secondary);
		m_host.cancelButton = &m_secondary;
	}
	update();
}

void UpdateWindow::showNotes() {
	if (!m_release.valid || m_release.notes.trimmed().isEmpty()) {
		return;
	}
	NotesDialog::show(this, "What's New in ZacOS 9 " + m_release.version, m_release.notes, false);
}

/* ---- running the helper and dpkg ----------------------------------------------------- */

void UpdateWindow::run(const QString &program, const QStringList &args,
		std::function<void(int)> done, std::function<void(const QString &)> line) {
	m_procErr.clear();
	m_proc = new QProcess(this);
	QProcess *p = m_proc;
	connect(p, &QProcess::readyReadStandardOutput, this, [p, line] {
		while (p->canReadLine()) {
			const QString l = QString::fromUtf8(p->readLine()).trimmed();
			if (line && !l.isEmpty()) {
				line(l);
			}
		}
	});
	connect(p, &QProcess::readyReadStandardError, this, [this, p] {
		for (const QByteArray &raw : p->readAllStandardError().split('\n')) {
			const QString l = QString::fromUtf8(raw).trimmed();
			/* apt's warnings about third-party repositories aren't why it failed. */
			if (!l.isEmpty() && !l.startsWith("W:") && !l.startsWith("N:")) {
				m_procErr = l;
			}
		}
	});
	connect(p, &QProcess::finished, this, [this, p, done](int code, QProcess::ExitStatus status) {
		if (m_proc == p) {
			m_proc = nullptr;
		}
		p->deleteLater();
		done(status == QProcess::NormalExit ? code : -1);
	});
	connect(p, &QProcess::errorOccurred, this, [this, p, done](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart) {
			return;
		}
		if (m_proc == p) {
			m_proc = nullptr;
		}
		p->deleteLater();
		done(-1);
	});
	if (program.isEmpty()) {
		appstoreHelperCommand(p, args); /* as root, through pkexec */
	} else {
		p->start(program, args);
	}
}

std::function<void(const QString &)> UpdateWindow::aptProgress(double from, double to) {
	auto dl = std::make_shared<double>(-1), pm = std::make_shared<double>(-1);
	return [this, from, to, dl, pm](const QString &l) {
		/* APT::Status-Fd: "dlstatus:N:PERCENT:MESSAGE", "pmstatus:PKG:PERCENT:MESSAGE". */
		const QStringList f = l.split(':');
		if (f.size() < 4) {
			return;
		}
		const double pct = f[2].toDouble();
		const QString msg = f.mid(3).join(':');
		if (f[0] == "dlstatus") {
			*dl = pct;
			m_doing = msg;
		} else if (f[0] == "pmstatus") {
			*pm = pct;
			m_doing = msg;
		} else if (f[0] == "pmerror") {
			m_procErr = msg;
		}
		double frac = -1;
		if (*pm >= 0) {
			frac = *dl >= 0 ? 0.4 + *pm / 100 * 0.6 : *pm / 100;
		} else if (*dl >= 0) {
			frac = *dl / 100 * 0.4;
		}
		if (frac >= 0) {
			m_progress = from + (to - from) * frac;
		}
		update();
	};
}

/* ---- checking ------------------------------------------------------------------------- */

void UpdateWindow::check() {
	m_state = State::Checking;
	m_progress = -1;
	m_sweep.start();
	m_release = Release();
	m_releaseError.clear();
	m_debian.clear();
	m_debianProblem.clear();
	m_installedExtras = installedExtras();
	m_extras.clear();
	m_extrasError.clear();
	m_current = installedVersion(PACKAGE);
	QString looking = "Looking for a new version of ZacOS 9 and for updates to the Debian software it runs on.";
	if (!m_installedExtras.empty()) {
		looking += " It also looks for new versions of your ZacOS Extras.";
	}
	setText("Checking for updates…", looking);
	setButtons(QString(), "Cancel");
	m_pendingChecks = 2 + (m_installedExtras.empty() ? 0 : 1);
	checkGitHub();
	checkDebian();
	if (!m_installedExtras.empty()) {
		checkExtras();
	}
}

void UpdateWindow::checkGitHub() {
	QNetworkRequest req{ QUrl(updateReleaseUrl()) };
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setRawHeader("User-Agent", "zacos9-update");
	req.setTransferTimeout(20000);
	m_reply = m_net.get(req);
	QNetworkReply *reply = m_reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (m_reply != reply) {
			return; /* cancelled */
		}
		m_reply = nullptr;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			/* No release published yet: nothing newer, not a failure. */
		} else if (reply->error() != QNetworkReply::NoError) {
			m_releaseError = reply->errorString();
		} else {
			const Release r = parseRelease(reply->readAll(), debArchitecture());
			if (!r.valid) {
				m_releaseError = "the newest release has no ZacOS 9 package for this computer";
			} else if (versionNewer(r.version, m_current)) {
				m_release = r;
			}
		}
		checkDone();
	});
}

void UpdateWindow::checkExtras() {
	QNetworkRequest req{ QUrl(extrasReleasesUrl()) };
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setRawHeader("User-Agent", "zacos9-update");
	req.setTransferTimeout(20000);
	m_extrasReply = m_net.get(req);
	QNetworkReply *reply = m_extrasReply;
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (m_extrasReply != reply) {
			return; /* cancelled */
		}
		m_extrasReply = nullptr;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			/* The extras repository has no releases: nothing to update. */
		} else if (reply->error() != QNetworkReply::NoError) {
			m_extrasError = reply->errorString();
		} else {
			const QByteArray json = reply->readAll();
			if (!QJsonDocument::fromJson(json).isArray()) {
				m_extrasError = "the list of releases couldn’t be read";
			} else {
				/* Only extras the catalog still offers are updated: one
				 * removed from it (Night Mode) is left alone. */
				const bool testing = !qEnvironmentVariableIsEmpty("ZACOS9_EXTRAS_URL");
				const QString arch = debArchitecture();
				const std::vector<StoreItem> catalog = storeItems();
				for (const StoreItem &item : catalog) {
					if (item.source != "extra" || item.packages.size() != 1) {
						continue;
					}
					const QString package = item.packages.first();
					QString from;
					for (const InstalledExtra &installed : m_installedExtras) {
						if (installed.package == package) {
							from = installed.version;
						}
					}
					if (from.isEmpty()) {
						continue;
					}
					const ExtraRelease release = parseExtraReleases(json, item.extra, package, arch, testing);
					if (!release.valid || !versionNewer(release.version, from)) {
						continue;
					}
					ExtraUpdate u;
					u.package = package;
					u.extra = item.extra;
					u.name = item.name;
					u.start = item.extraStart;
					u.from = from;
					u.to = release.version;
					u.debName = QFileInfo(QUrl(release.debUrl).path()).fileName();
					u.debUrl = release.debUrl;
					u.sha256 = release.sha256;
					m_extras.push_back(u);
				}
			}
		}
		checkDone();
	});
}

void UpdateWindow::checkDebian() {
	/* apt-get update needs root; without it (not an administrator) the
	 * simulation still reads the lists from the last time. */
	run(QString(), { "refresh" }, [this](int refreshed) {
		run("apt-get", { "-s", "-o", "Debug::NoLocking=1", "--with-new-pkgs", "upgrade" },
			[this, refreshed](int code) {
				m_debianProblem = debianCheckProblem(refreshed, code, m_procErr);
				checkDone();
			},
			[this](const QString &l) {
				for (const DebianUpdate &u : parseAptSimulation(l)) {
					m_debian.push_back(u);
				}
			});
	});
}

void UpdateWindow::checkDone() {
	if (--m_pendingChecks > 0 || m_state != State::Checking) {
		return;
	}
	m_sweep.stop();
	showResults();
}

void UpdateWindow::showResults() {
	const bool zacos = m_release.valid;
	const bool extras = !m_extras.empty();
	const int n = static_cast<int>(m_debian.size());
	QString detail;
	if (zacos) {
		detail += "ZacOS 9 " + m_release.version + " is available (this computer has " +
			(m_current.isEmpty() ? QString("an unknown version") : m_current) + "). ";
	}
	if (extras) {
		QStringList names;
		const int count = static_cast<int>(m_extras.size());
		for (int i = 0; i < count && i < 4; i++) {
			names << m_extras[i].name + " " + m_extras[i].to;
		}
		detail += QString("%1 ZacOS Extra update%2: %3%4. ").arg(count).arg(count == 1 ? "" : "s")
			.arg(names.join(", ")).arg(count > 4 ? ", …" : "");
	}
	if (n > 0) {
		QStringList names;
		for (int i = 0; i < n && i < 4; i++) {
			names << m_debian[i].package;
		}
		detail += QString("%1 Debian update%2: %3%4. ").arg(n).arg(n == 1 ? "" : "s")
			.arg(names.join(", ")).arg(n > 4 ? ", …" : "");
	}
	if (!m_releaseError.isEmpty()) {
		detail += "Couldn’t look for a new ZacOS 9: " + m_releaseError + ". ";
	}
	if (!m_extrasError.isEmpty()) {
		detail += "Couldn’t look for ZacOS Extras updates: " + m_extrasError + ". ";
	}
	if (!m_debianProblem.isEmpty()) {
		detail += m_debianProblem;
	}
	if (zacos || extras || n > 0) {
		m_state = State::Available;
		setText("Updates are available for this computer.", detail.trimmed());
		setButtons("Update", "Later");
		if (zacos && !m_release.notes.trimmed().isEmpty()) {
			const int y = H - 14 - PL_BUTTON_H;
			m_notes = PanelButton("What's New…", QRect(14, y, 110, PL_BUTTON_H));
			m_notes.hint = "Shows what changed in ZacOS 9 " + m_release.version + ".";
			m_notes.clicked = [this] { showNotes(); };
			m_host.buttons.push_back(&m_notes);
			update();
		}
		if (background && !isVisible()) {
			show();
			platinumSetFrameStyle(this, FrameStyle::MovableModal);
		}
	} else {
		if (background) {
			qApp->quit(); /* nothing to install: nothing to say */
			return;
		}
		/* Up to date only if every check really worked. */
		const bool sure = m_releaseError.isEmpty() && m_extrasError.isEmpty() && m_debianProblem.isEmpty();
		QString upToDate = "ZacOS 9 " + m_current + " is the newest version, and Debian has no updates for it.";
		if (!m_installedExtras.empty()) {
			upToDate += " Your ZacOS Extras are up to date.";
		}
		m_state = State::UpToDate;
		setText(sure ? "Your software is up to date." : "No updates were found.",
			sure ? upToDate : detail.trimmed());
		setButtons("OK", QString());
	}
}

/* ---- installing ------------------------------------------------------------------------ */

void UpdateWindow::install() {
	m_state = State::Working;
	m_progress = 0;
	m_zacosUpdated = false;
	setText("Installing updates…", QString());
	setButtons(QString(), QString());
	/* Each part gets an equal share of the bar: the new ZacOS 9, the
	 * extras, then Debian's updates. */
	const int phases = (m_release.valid ? 1 : 0) + (m_extras.empty() ? 0 : 1) +
		(m_debian.empty() ? 0 : 1);
	const double span = phases > 0 ? 1.0 / phases : 1.0;
	int at = 0;
	m_zacosFrom = at * span;
	m_zacosSpan = span;
	if (m_release.valid) {
		at++;
	}
	m_extrasFrom = at * span;
	m_extrasSpan = span;
	if (!m_extras.empty()) {
		at++;
	}
	m_debianFrom = at * span;
	if (m_release.valid) {
		downloadDeb();
	} else if (!m_extras.empty()) {
		installExtras();
	} else {
		upgradeDebian();
	}
}

void UpdateWindow::downloadDeb() {
	const double share = m_zacosSpan;
	const QString dir =
		QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/zacos9/updates";
	QDir().mkpath(dir);
	m_debPath = dir + "/" + QFileInfo(m_release.debName).fileName();
	m_doing = "Downloading ZacOS 9 " + m_release.version;
	QNetworkRequest req{ QUrl(m_release.debUrl) };
	req.setRawHeader("User-Agent", "zacos9-update");
	req.setTransferTimeout(60000);
	m_reply = m_net.get(req);
	QNetworkReply *reply = m_reply;
	connect(reply, &QNetworkReply::downloadProgress, this, [this, share](qint64 got, qint64 total) {
		if (total > 0) {
			m_progress = m_zacosFrom + share * 0.4 * got / total;
			update();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (m_reply != reply) {
			return;
		}
		m_reply = nullptr;
		if (reply->error() != QNetworkReply::NoError) {
			failed("ZacOS 9 " + m_release.version + " couldn’t be downloaded: " + reply->errorString() + ".");
			return;
		}
		const QByteArray data = reply->readAll();
		if (!m_release.sha256.isEmpty() &&
				QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != m_release.sha256.toLatin1()) {
			failed("The downloaded ZacOS 9 package is damaged (its checksum doesn’t match). Nothing was installed.");
			return;
		}
		QFile f(m_debPath);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
			failed("The downloaded package couldn’t be saved in " + QFileInfo(m_debPath).path() + ".");
			return;
		}
		f.close();
		installDeb();
	});
}

void UpdateWindow::installDeb() {
	/* It must really be the release we offered, not some other package. */
	auto fields = std::make_shared<QStringList>();
	run("dpkg-deb", { "-f", m_debPath, "Package", "Version" }, [this, fields](int code) {
		QString pkg, version;
		for (const QString &l : *fields) {
			if (l.startsWith("Package:")) {
				pkg = l.section(':', 1).trimmed();
			} else if (l.startsWith("Version:")) {
				version = l.section(':', 1).trimmed();
			}
		}
		if (code != 0 || pkg != PACKAGE || version != m_release.version) {
			failed("The downloaded file isn’t ZacOS 9 " + m_release.version + ". Nothing was installed.");
			return;
		}
		m_doing = "Installing ZacOS 9 " + m_release.version;
		update();
		run(QString(), { "install-deb", m_debPath }, [this](int code) {
			if (code != 0) {
				failed("ZacOS 9 " + m_release.version + " couldn’t be installed" +
					(m_procErr.isEmpty() ? QString(".") : ": " + m_procErr));
				return;
			}
			QFile::remove(m_debPath);
			m_zacosUpdated = true;
			afterZacos();
		}, aptProgress(m_zacosFrom + m_zacosSpan * 0.4, m_zacosFrom + m_zacosSpan));
	}, [fields](const QString &l) { *fields << l; });
}

void UpdateWindow::installExtras() {
	m_extraIndex = 0;
	installNextExtra();
}

void UpdateWindow::installNextExtra() {
	if (m_extraIndex >= static_cast<int>(m_extras.size())) {
		restartExtras();
		if (m_debian.empty()) {
			finished();
		} else {
			upgradeDebian();
		}
		return;
	}
	const ExtraUpdate u = m_extras[m_extraIndex];
	const double per = m_extrasSpan / m_extras.size();
	const double from = m_extrasFrom + per * m_extraIndex;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
		"/zacos9/updates";
	QDir().mkpath(dir);
	m_extraPath = dir + "/" + QFileInfo(u.debName).fileName();
	m_doing = "Downloading " + u.name + " " + u.to;
	update();
	QNetworkRequest req{ QUrl(u.debUrl) };
	req.setRawHeader("User-Agent", "zacos9-update");
	req.setTransferTimeout(60000);
	m_reply = m_net.get(req);
	QNetworkReply *reply = m_reply;
	connect(reply, &QNetworkReply::downloadProgress, this, [this, from, per](qint64 got, qint64 total) {
		if (total > 0) {
			m_progress = from + per * 0.4 * got / total;
			update();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, u, from, per] {
		reply->deleteLater();
		if (m_reply != reply) {
			return;
		}
		m_reply = nullptr;
		if (reply->error() != QNetworkReply::NoError) {
			failed("“" + u.name + "” " + u.to + " couldn’t be downloaded: " + reply->errorString() + ".");
			return;
		}
		const QByteArray data = reply->readAll();
		if (!u.sha256.isEmpty() &&
				QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != u.sha256.toLatin1()) {
			failed("The download of “" + u.name + "” is damaged (its checksum doesn’t match). "
				"Nothing was installed.");
			return;
		}
		QFile f(m_extraPath);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
			failed("The download of “" + u.name + "” couldn’t be saved in " +
				QFileInfo(m_extraPath).path() + ".");
			return;
		}
		f.close();
		auto fields = std::make_shared<QStringList>();
		run("dpkg-deb", { "-f", m_extraPath, "Package", "Version" }, [this, fields, u, from, per](int code) {
			QString pkg, version;
			for (const QString &l : *fields) {
				if (l.startsWith("Package:")) {
					pkg = l.section(':', 1).trimmed();
				} else if (l.startsWith("Version:")) {
					version = l.section(':', 1).trimmed();
				}
			}
			if (code != 0 || pkg != u.package || version != u.to) {
				failed("The downloaded file isn’t “" + u.name + "” " + u.to + ". Nothing was installed.");
				return;
			}
			m_doing = "Installing " + u.name + " " + u.to;
			update();
			run(QString(), { "install-deb", m_extraPath }, [this, u](int code) {
				if (code != 0) {
					failed("“" + u.name + "” " + u.to + " couldn’t be installed" +
						(m_procErr.isEmpty() ? QString(".") : ": " + m_procErr));
					return;
				}
				QFile::remove(m_extraPath);
				m_extraIndex++;
				installNextExtra();
			}, aptProgress(from + per * 0.4, from + per));
		}, [fields](const QString &l) { *fields << l; });
	});
}

void UpdateWindow::afterZacos() {
	if (!m_extras.empty()) {
		installExtras();
	} else if (!m_debian.empty()) {
		upgradeDebian();
	} else {
		finished();
	}
}

/* An extra's package can't restart a program that an update replaces
 * (its prerm only stops it on removal), so stop the old copy and start
 * the installed one after a moment. */
void UpdateWindow::restartExtras() {
	QStringList starts;
	for (const ExtraUpdate &u : m_extras) {
		if (!u.start.isEmpty() && !starts.contains(u.start)) {
			starts << u.start;
		}
	}
	if (starts.isEmpty()) {
		return;
	}
	for (const QString &start : starts) {
		QProcess::startDetached("pkill", QStringList() << "-x" << "-f" << ("(/usr/bin/)?" + start));
	}
	QTimer::singleShot(500, this, [starts] {
		for (const QString &start : starts) {
			QProcess::startDetached(start, QStringList());
		}
	});
}

void UpdateWindow::upgradeDebian() {
	m_doing = "Installing Debian updates";
	update();
	run(QString(), { "upgrade" }, [this](int code) {
		if (code != 0) {
			failed(QString(m_zacosUpdated ? "ZacOS 9 was updated, but the" : "The") +
				" Debian updates couldn’t be installed" +
				(m_procErr.isEmpty() ? QString(".") : ": " + m_procErr));
			return;
		}
		finished();
	}, aptProgress(m_debianFrom, 1.0));
}

void UpdateWindow::finished() {
	m_state = State::Done;
	m_progress = 1;
	if (m_zacosUpdated) {
		setText("ZacOS 9 " + m_release.version + " is installed.",
			"Restart the computer to start using it. Programs that are open keep running the "
			"old version until then.");
		setButtons("Restart", "Later");
	} else {
		QString detail = "Some programs may need to be quit and opened again before they use "
			"the new versions.";
		bool restarted = false;
		for (const ExtraUpdate &u : m_extras) {
			restarted = restarted || !u.start.isEmpty();
		}
		if (restarted) {
			detail = "Your ZacOS Extras were restarted so they use their new versions. " + detail;
		}
		setText("The updates are installed.", detail);
		setButtons("OK", QString());
	}
}

void UpdateWindow::failed(const QString &why) {
	m_state = State::Failed;
	m_sweep.stop();
	setText("The update didn’t finish.", why);
	setButtons("OK", QString());
}

/* ---- buttons and the window ------------------------------------------------------------ */

void UpdateWindow::primaryClicked() {
	switch (m_state) {
	case State::Available:
		install();
		break;
	case State::Done:
		if (m_zacosUpdated) {
			QProcess::startDetached("systemctl", { "reboot" });
		}
		close();
		break;
	default:
		close();
		break;
	}
}

void UpdateWindow::secondaryClicked() {
	close(); /* Cancel while checking, Later */
}

void UpdateWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	pl_image(c, 14, 16, pl_icon(PL_ICON_SYSTEM_FOLDER, PL_ICON_LARGE), PL_ICON_LARGE, PL_ICON_LARGE);
	panelText(c, m_headline, TEXT_X, 26, PL_FONT_SYSTEM, C_BLACK, TEXT_W);
	int y = 44;
	if (m_state == State::Checking || m_state == State::Working) {
		if (m_state == State::Working) {
			panelText(c, m_doing, TEXT_X, y, PL_FONT_VIEWS, C_BLACK, TEXT_W);
			y += 14;
		} else {
			for (const QString &l : m_lines) {
				panelText(c, l, TEXT_X, y, PL_FONT_VIEWS);
				y += 13;
			}
		}
		pl_progress_paint(c, TEXT_X, y - 4, TEXT_W, m_progress < 0 ? m_sweepPos : m_progress,
			pl_accent_current());
	} else {
		for (const QString &l : m_lines) {
			if (y > H - 14 - PL_BUTTON_H - 6) {
				break;
			}
			panelText(c, l, TEXT_X, y, PL_FONT_VIEWS);
			y += 13;
		}
	}
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void UpdateWindow::mousePressEvent(QMouseEvent *e) {
	if (m_host.hostPress(e)) {
		update();
	}
}

void UpdateWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}

void UpdateWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}

void UpdateWindow::keyPressEvent(QKeyEvent *e) {
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QWidget::keyPressEvent(e);
}

/* apt can't be stopped part way through: no closing while installing. */
void UpdateWindow::closeEvent(QCloseEvent *e) {
	if (busy()) {
		e->ignore();
		return;
	}
	if (m_reply) {
		QNetworkReply *r = m_reply;
		m_reply = nullptr;
		r->abort();
	}
	if (m_extrasReply) {
		QNetworkReply *r = m_extrasReply;
		m_extrasReply = nullptr;
		r->abort();
	}
	if (m_proc) {
		m_proc->disconnect();
		m_proc->kill(); /* apt-get update or the simulation: safe to stop */
		m_proc = nullptr;
	}
	e->accept();
}
