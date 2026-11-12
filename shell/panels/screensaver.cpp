/* The Screen Saver control panel (zacos9-controlpanel screensaver). */
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "afterdark.h"
#include "controlpanel.h"

/* "Start after" stops, in minutes. */
static const int SAVER_MINUTES[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static constexpr int SAVER_STEPS = sizeof(SAVER_MINUTES) / sizeof(SAVER_MINUTES[0]);

/* The panel's fixed geometry. The radio group has None, the four built-in
 * savers and After Dark; the After Dark group has a scrolling module list and
 * the import button. */
static constexpr int G1 = 26;
static constexpr int G1_H = ROWS(6);
static constexpr int AD_TOP = G1 + G1_H + 12;
static constexpr int LIST_H = 96;
static constexpr int AD_H = 1 + PL_GROUP_MARGIN_TOP + LIST_H + 8 + PL_BUTTON_H + PL_GROUP_MARGIN;
static constexpr int G3 = AD_TOP + AD_H + 12;
static constexpr int PANEL_H = G3 + SLIDER_GROUP_H + 12 + PL_CHECKBOX_SIZE + 36;
static constexpr int AFTERDARK_ROW = 5; /* None + four built-ins */

/* Which screen saver, with a live preview, and when it starts; zacos9-wm
 * starts zacos9-screensaver then (compositor/src/idle.c, lib/savers.c).
 *
 * After Dark is a helper process (emulation/afterdark): the panel lists the
 * user's imported modules, runs the helper for the preview, and writes the
 * choices to desktop.conf. No modules or fonts ship with ZacOS. */
class ScreenSaverPanel : public ControlPanel {
public:
	ScreenSaverPanel() : ControlPanel(_("Screen Saver"), PANEL_H) {
		m_available = pl_afterdark_available();
		if (m_available) {
			pl_afterdark_program(m_helper, sizeof(m_helper));
		}
		m_groups = { { G1, G1 + G1_H, "Screen Saver" },
			{ AD_TOP, AD_TOP + AD_H, "After Dark Modules" },
			{ G3, G3 + SLIDER_GROUP_H, "Start the screen saver when inactive for" } };

		const int y = G1 + 1 + PL_GROUP_MARGIN_TOP;
		m_which.add(QStringLiteral("None"), QPoint(M + IN, y));
		for (int i = 0; i < pl_saver_count(); i++) {
			m_which.add(QString::fromUtf8(pl_saver_name(i)),
				QPoint(M + IN, y + 20 * (i + 1)));
		}
		m_which.add(QStringLiteral("After Dark"),
			QPoint(M + IN, y + 20 * AFTERDARK_ROW));
		char id[64];
		const bool set = pl_setting("screensaver", id, sizeof(id));
		m_which.selected = !set ? 1
			: strcmp(id, "none") == 0 ? 0
			: strcmp(id, "afterdark") == 0 ? AFTERDARK_ROW
			: pl_saver_find(id) + 1;
		m_which.changed = [this](int i) {
			if (i == 0) {
				pl_setting_set("screensaver", "none");
			} else if (i == AFTERDARK_ROW) {
				pl_setting_set("screensaver", "afterdark");
				char chosen[256];
				if (!pl_setting("screensaver-afterdark", chosen, sizeof(chosen)) ||
						!*chosen) {
					chooseFirstModule();
				}
			} else {
				pl_setting_set("screensaver", pl_saver_id(i - 1));
			}
			m_after.enabled = i != 0;
			restartPreview();
		};
		m_radios.push_back(&m_which);
		m_preview = QRect(W - M - IN - 2 - 130, y - 2, 130, 100);

		m_modules.frame = QRect(M + IN, AD_TOP + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 2, LIST_H);
		m_modules.state.selected = -1;
		m_modules.hint = "The After Dark modules you have imported. Choose one to use.";
		m_modules.picked = [this](int row) {
			if (row < 0 || row >= m_titles.size()) {
				return;
			}
			pl_setting_set("screensaver-afterdark",
				m_titles[row].toUtf8().constData());
			pl_setting_set("screensaver", "afterdark");
			m_which.selected = AFTERDARK_ROW;
			m_after.enabled = true;
			m_listFocus = true;
			restartPreview();
			update();
		};

		m_import = PanelButton(QStringLiteral("Import…"),
			QRect(W - M - IN - 4 - 90, m_modules.frame.bottom() + 8, 90, PL_BUTTON_H));
		m_import.hint = "Imports an After Dark disk image or archive. Drop a folder of modules on the panel instead.";
		m_import.enabled = m_available;
		m_import.clicked = [this] { chooseImport(); };

		m_hintRects.push_back({ &m_modules.frame, m_modules.hint });
		m_hintRects.push_back({ &m_import.rect, m_import.hint });
		m_hintRects.push_back({ &m_preview, "A preview of the chosen screen saver." });

		int minutes = settingInt("screensaver-after", 5), step = 0;
		for (int i = 0; i < SAVER_STEPS; i++) {
			if (std::abs(SAVER_MINUTES[i] - minutes) <
					std::abs(SAVER_MINUTES[step] - minutes)) {
				step = i;
			}
		}
		placeSlider(m_after, G3, SAVER_STEPS, step, "1 min", "1 hour");
		m_after.changed = [](int v) { setInt("screensaver-after", SAVER_MINUTES[v]); };
		m_after.enabled = m_which.selected != 0;
		/* Locks once every screen shows it or sleeps, and before the system
		 * sleeps (compositor/src/lock.c, shell/lock/). */
		m_password = std::make_unique<PanelCheckbox>(
			QStringLiteral("Ask for my password to wake"),
			QPoint(M + 2, G3 + SLIDER_GROUP_H + 12));
		char v[8];
		m_password->on = pl_setting("require-password", v, sizeof(v)) &&
			strcmp(v, "1") == 0;
		m_password->toggled = [](bool on) {
			pl_setting_set("require-password", on ? "1" : nullptr);
		};
		m_checks.push_back(m_password.get());
		m_notes = { "It shows until the display sleeps (Nap Time).",
			"A window filling a screen keeps it away from that one." };

		m_which.hint = "Chooses what the screen shows while you aren't using the computer.";
		m_after.hint = "Sets how long the computer waits, while you aren't using it, before the screen saver starts.";
		m_password->hint = "Asks for your login password before the screen saver or sleep gives the screen back.";
		if (!m_available) {
			m_note = "After Dark is not installed.";
		}

		connect(&m_listProcess, &QProcess::finished, this,
			[this](int code, QProcess::ExitStatus status) { listFinished(code, status); });
		connect(&m_listProcess, &QProcess::errorOccurred, this,
			[this](QProcess::ProcessError e) {
				if (e == QProcess::FailedToStart) {
					m_note = "After Dark could not start.";
					update();
				}
			});
		connect(&m_importProcess, &QProcess::finished, this,
			[this](int code, QProcess::ExitStatus status) { importFinished(code, status); });
		connect(&m_importProcess, &QProcess::errorOccurred, this,
			[this](QProcess::ProcessError e) {
				if (e == QProcess::FailedToStart) {
					m_importTimeout.stop();
					m_import.enabled = m_available;
					m_note = "The importer could not start.";
					update();
				}
			});
		m_importTimeout.setSingleShot(true);
		m_importTimeout.setInterval(120000);
		connect(&m_importTimeout, &QTimer::timeout, this, [this] {
			if (m_importProcess.state() != QProcess::NotRunning) {
				m_importProcess.kill();
				m_import.enabled = m_available;
				m_note = "The import took too long.";
				update();
			}
		});

		setAcceptDrops(true);
		refreshModules();
		restartPreview();
		m_clock.start();
		m_tick.callOnTimeout([this] { update(m_preview); });
		m_tick.start(40);
	}

	~ScreenSaverPanel() override {
		pl_saver_free(m_saver);
		ad_renderer_stop(m_previewAfterdark);
		if (m_listProcess.state() != QProcess::NotRunning) {
			m_listProcess.kill();
		}
		if (m_importProcess.state() != QProcess::NotRunning) {
			m_importProcess.kill();
		}
		m_importTimeout.stop();
	}

protected:
	void paintExtra(pl_canvas *c) override {
		const QRect r = m_preview;
		pl_outline(c, r.left() - 1, r.top() - 1, r.right() + 1, r.bottom() + 1, C_BLACK);
		pl_canvas inner = { c->px + r.top() * c->stride + r.left(), c->stride, 0, 0,
			r.width(), r.height(), c->log };
		if (m_previewAfterdark) {
			ad_renderer_take_frame(m_previewAfterdark);
			if (!ad_renderer_frame(m_previewAfterdark) && m_clock.elapsed() > 5000) {
				/* A module that hangs in Initialize leaves the preview
				 * black and the emulator spinning; stop it. */
				ad_renderer_stop(m_previewAfterdark);
				m_previewAfterdark = nullptr;
				m_note = "This module did not start.";
			}
		}
		if (m_previewAfterdark) {
			pl_fill(&inner, 0, 0, r.width() - 1, r.height() - 1, C_BLACK);
			ad_renderer_draw(m_previewAfterdark, &inner, 0, 0, r.width(), r.height());
		} else if (m_saver) {
			pl_saver_draw(m_saver, &inner, m_clock.elapsed() / 1000.0);
		} else {
			pl_fill(&inner, 0, 0, r.width() - 1, r.height() - 1, GRAY(0x8));
		}
		if (m_after.value > 0 && m_after.value < SAVER_STEPS - 1) {
			const int m = SAVER_MINUTES[m_after.value];
			Text t(m == 1 ? QStringLiteral("1 minute")
				: QStringLiteral("%1 minutes").arg(m), 120, PL_FONT_VIEWS);
			pl_text(c, t.t, m_after.pos.x() + (m_after.width - t.inkWidth()) / 2,
				m_after.pos.y() + PL_SLIDER_H + 11,
				m_after.enabled ? C_BLACK : GRAY(0x8));
		}
		m_modules.paint(c, m_listFocus);
		m_import.paint(c);
		if (!m_note.isEmpty()) {
			Text t(m_note, m_import.rect.left() - M - IN - 8, PL_FONT_VIEWS);
			pl_text(c, t.t, M + IN, m_import.rect.top() + 14, GRAY(0x5));
		}
	}

	bool pressExtra(QPoint pos, bool doubleClick) override {
		(void)doubleClick;
		m_focus = nullptr;
		if (m_import.press(pos)) {
			m_listFocus = false;
			return true;
		}
		if (m_modules.press(pos)) {
			m_listFocus = true;
			return true;
		}
		return false;
	}

	void moveExtra(QPoint pos) override {
		m_modules.move(pos);
		m_import.move(pos);
	}

	void releaseExtra(QPoint pos) override {
		m_modules.release();
		m_import.release(pos);
	}

	void keyExtra(QKeyEvent *e) override {
		if (m_listFocus && m_modules.key(e->key(), e->text())) {
			update();
		}
	}

	void wheelEvent(QWheelEvent *e) override {
		if (m_modules.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		} else {
			ControlPanel::wheelEvent(e);
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (!m_modules.frame.contains(e->position().toPoint())) {
			m_listFocus = false;
		}
		ControlPanel::mousePressEvent(e);
	}

	void dragEnterEvent(QDragEnterEvent *e) override {
		if (m_available && e->mimeData()->hasUrls()) {
			e->acceptProposedAction();
		}
	}

	void dropEvent(QDropEvent *e) override {
		if (!m_available) {
			return;
		}
		for (const QUrl &url : e->mimeData()->urls()) {
			if (url.isLocalFile()) {
				importModules(url.toLocalFile());
				e->acceptProposedAction();
				return;
			}
		}
	}

private:
	void chooseFirstModule() {
		if (m_titles.isEmpty()) {
			return;
		}
		pl_setting_set("screensaver-afterdark", m_titles[0].toUtf8().constData());
		m_modules.select(0, false);
	}

	void refreshModules() {
		if (!m_available || m_listProcess.state() != QProcess::NotRunning) {
			return;
		}
		m_listProcess.setProgram(QString::fromUtf8(m_helper));
		m_listProcess.setArguments({ QStringLiteral("--list") });
		m_listProcess.start();
	}

	void listFinished(int code, QProcess::ExitStatus status) {
		if (code != 0 || status != QProcess::NormalExit) {
			m_note = "The After Dark module list could not be read.";
			update();
			return;
		}
		m_titles = QString::fromUtf8(m_listProcess.readAllStandardOutput())
			.split('\n', Qt::SkipEmptyParts);
		m_modules.setItems(m_titles);
		char saved[256];
		const bool has = pl_setting("screensaver-afterdark", saved, sizeof(saved)) &&
			*saved;
		int row = has ? m_titles.indexOf(QString::fromUtf8(saved)) : -1;
		if (row < 0 && m_which.selected == AFTERDARK_ROW && !m_titles.isEmpty()) {
			row = 0;
		}
		m_modules.state.selected = row;
		m_modules.scrollTo(std::max(0, row - 2));
		if (m_which.selected == AFTERDARK_ROW && row >= 0 &&
				(!has || m_titles[row] != QString::fromUtf8(saved))) {
			pl_setting_set("screensaver-afterdark",
				m_titles[row].toUtf8().constData());
		}
		if (m_which.selected == AFTERDARK_ROW) {
			restartPreview();
		}
		if (!has && m_titles.isEmpty() && m_which.selected == AFTERDARK_ROW) {
			m_note = "Drop a disk image here.";
		}
		update();
	}

	void chooseImport() {
		if (!m_available || m_importProcess.state() != QProcess::NotRunning) {
			return;
		}
		const QString path = QFileDialog::getOpenFileName(this,
			QStringLiteral("Import After Dark Modules"),
			QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
			QStringLiteral("After Dark disks, archives and modules (*.img *.dsk *.iso "
				"*.toast *.cdr *.image *.rsrc *.zip *.sit *.sitx *.sea *.cpt *.hqx);;"
				"All files (*)"));
		if (!path.isEmpty()) {
			importModules(path);
		}
	}

	void importModules(const QString &path) {
		if (!m_available || m_importProcess.state() != QProcess::NotRunning) {
			return;
		}
		char library[4096];
		if (pl_afterdark_library_dir(library, sizeof(library)) &&
				!QDir(library).entryList({ "*.rsrc" }, QDir::Files).isEmpty() &&
				!Alert::ask("Importing replaces the After Dark modules already imported. "
					"Continue?", "Import", "Cancel")) {
			return;
		}
		m_import.enabled = false;
		m_note = "Importing…";
		m_importTimeout.start();
		m_importProcess.setProgram(QString::fromUtf8(m_helper));
		m_importProcess.setArguments({ QStringLiteral("--import"), path });
		m_importProcess.start();
		update();
	}

	void importFinished(int code, QProcess::ExitStatus status) {
		m_importTimeout.stop();
		m_import.enabled = m_available;
		const QString out = QString::fromUtf8(m_importProcess.readAllStandardOutput()).trimmed();
		const QString err = QString::fromUtf8(m_importProcess.readAllStandardError()).trimmed();
		if (code != 0 || status != QProcess::NormalExit) {
			m_note = "The import did not work.";
			Alert::ask(err.isEmpty() ? m_note : err, "OK", QString());
		} else {
			m_note = "Drop a disk image here.";
			Alert::ask(out.isEmpty() ? QStringLiteral("The import finished.") : out,
				"OK", QString());
			refreshModules();
		}
		update();
	}

	void restartPreview() {
		pl_saver_free(m_saver);
		m_saver = nullptr;
		ad_renderer_stop(m_previewAfterdark);
		m_previewAfterdark = nullptr;
		if (m_which.selected == AFTERDARK_ROW) {
			char module[256];
			if (m_available && pl_afterdark_module(module, sizeof(module))) {
				m_previewAfterdark = ad_renderer_start(module, 30);
			}
		} else if (m_which.selected > 0) {
			m_saver = pl_saver_new(m_which.selected - 1, m_preview.width(),
				m_preview.height(), 9);
		}
		m_clock.restart();
		update(m_preview);
	}

	PanelRadios m_which;
	PanelSlider m_after;
	std::unique_ptr<PanelCheckbox> m_password;
	PanelList m_modules;
	PanelButton m_import;
	QRect m_preview;
	QStringList m_titles;
	QString m_note = "Drop a disk image here.";
	bool m_available = false;
	bool m_listFocus = false;
	char m_helper[4096] = "";
	QProcess m_listProcess, m_importProcess;
	QTimer m_importTimeout;
	pl_saver *m_saver = nullptr;
	struct ad_renderer *m_previewAfterdark = nullptr;
	QElapsedTimer m_clock;
	QTimer m_tick;
};

PANEL("screensaver", N_("Screen Saver"), false, ScreenSaverPanel);
