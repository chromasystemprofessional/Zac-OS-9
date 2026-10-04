#include "update.h"

#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

UpdateWindow::UpdateWindow() {
	setWindowTitle("Software Update");
	setFixedSize(W, H);
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
	m_current = installedVersion(PACKAGE);
	setText("Checking for updates…", "Looking for a new version of ZacOS 9 and for updates "
		"to the Debian software it runs on.");
	setButtons(QString(), "Cancel");
	m_pendingChecks = 2;
	checkGitHub();
	checkDebian();
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

void UpdateWindow::checkDebian() {
	/* apt-get update needs root; without it (not an administrator) the
	 * simulation still reads the lists from the last time. */
	run(QString(), { "refresh" }, [this](int) {
		run("apt-get", { "-s", "-o", "Debug::NoLocking=1", "--with-new-pkgs", "upgrade" },
			[this](int) { checkDone(); },
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
	const int n = static_cast<int>(m_debian.size());
	QString detail;
	if (zacos) {
		detail += "ZacOS 9 " + m_release.version + " is available (this computer has " +
			(m_current.isEmpty() ? QString("an unknown version") : m_current) + "). ";
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
		detail += "Couldn’t look for a new ZacOS 9: " + m_releaseError + ".";
	}
	if (zacos || n > 0) {
		m_state = State::Available;
		setText("Updates are available for this computer.", detail.trimmed());
		setButtons("Update", "Later");
	} else {
		m_state = State::UpToDate;
		setText(m_releaseError.isEmpty() ? "Your software is up to date." : "No updates were found.",
			m_releaseError.isEmpty()
				? "ZacOS 9 " + m_current + " is the newest version, and Debian has no updates for it."
				: detail.trimmed());
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
	if (m_release.valid) {
		downloadDeb();
	} else {
		upgradeDebian();
	}
}

/* The ZacOS 9 share of the bar: all of it unless Debian updates follow. */
static double zacosShare(bool debianToo) {
	return debianToo ? 0.5 : 1.0;
}

void UpdateWindow::downloadDeb() {
	const double share = zacosShare(!m_debian.empty());
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
			m_progress = share * 0.4 * got / total;
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
	const double share = zacosShare(!m_debian.empty());
	/* It must really be the release we offered, not some other package. */
	auto fields = std::make_shared<QStringList>();
	run("dpkg-deb", { "-f", m_debPath, "Package", "Version" }, [this, fields, share](int code) {
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
			if (m_debian.empty()) {
				finished();
			} else {
				upgradeDebian();
			}
		}, aptProgress(share * 0.4, share));
	}, [fields](const QString &l) { *fields << l; });
}

void UpdateWindow::upgradeDebian() {
	const double from = m_release.valid ? zacosShare(true) : 0;
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
	}, aptProgress(from, 1.0));
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
		setText("The updates are installed.", "Some programs may need to be quit and opened "
			"again before they use the new versions.");
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
	if (m_proc) {
		m_proc->disconnect();
		m_proc->kill(); /* apt-get update or the simulation: safe to stop */
		m_proc = nullptr;
	}
	e->accept();
}
