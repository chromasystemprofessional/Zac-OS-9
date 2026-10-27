#include "pickerwindow.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>
#include <QTimer>

#include "alert.h"
#include "desktopprinter.h"
#include "icons.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "programs.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD);
constexpr int W = 460, H = 340, M = 12;
constexpr int LIST_TOP = 34, LIST_BOTTOM = 214;

QString settingsProgram() {
	const QString env = qEnvironmentVariable("ZACOS9_PRINTER_SETTINGS");
	if (!env.isEmpty()) {
		return env;
	}
	return QStandardPaths::findExecutable("system-config-printer");
}

} // namespace

QString desktopFolder() {
	const QString env = qEnvironmentVariable("ZACOS9_DESKTOP_DIR");
	return env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) : env;
}

struct PickerWindow::Parts {
	PanelHost host;
	PanelList list;
	PanelButton desktop, queue, settings;
	QList<Printer> printers;
	bool loading = true;
	explicit Parts(QWidget *w) : host(w) {}
};

PickerWindow::PickerWindow() : d(std::make_unique<Parts>(this)) {
	setWindowTitle("Printer Picker");
	setFixedSize(W, H);
	d->list.frame = QRect(M, LIST_TOP, W - 2 * M, LIST_BOTTOM - LIST_TOP);
	d->list.picked = [this](int row) { choose(row); };
	const int y = H - M - PL_BUTTON_H;
	d->desktop = PanelButton("Create Desktop Printer", QRect(M, y, 170, PL_BUTTON_H));
	d->desktop.clicked = [this] {
		const QString path = createDesktopPrinterForSelected();
		if (!path.isEmpty()) {
			update();
		}
	};
	d->queue = PanelButton("Print Queue", QRect(M + 178, y, 100, PL_BUTTON_H));
	d->queue.clicked = [this] { openQueue(); };
	d->settings = PanelButton("Settings…", QRect(W - M - 4 - 100, y, 100, PL_BUTTON_H));
	d->settings.clicked = [] { platinumStartApplication(settingsProgram(), {}); };
	d->settings.enabled = !settingsProgram().isEmpty();
	d->host.buttons = { &d->desktop, &d->queue, &d->settings };
	QTimer::singleShot(0, this, [this] { reload(); });
}

PickerWindow::~PickerWindow() = default;

void PickerWindow::reload() {
	d->printers = listPrinters();
	d->loading = false;
	QStringList rows;
	std::vector<int> icons;
	int selected = -1;
	for (int i = 0; i < d->printers.size(); ++i) {
		rows << d->printers[i].shownName();
		icons.push_back(PL_ICON_PRINTER);
		if (d->printers[i].isDefault) {
			selected = i;
		}
	}
	d->list.setItems(rows, icons);
	d->list.select(selected, false);
	if (selected >= 0) {
		d->list.ensureVisible(selected);
	}
	updateButtons();
}

QStringList PickerWindow::rows() const {
	return d->list.items;
}

int PickerWindow::selectedRow() const {
	return d->list.state.selected;
}

QString PickerWindow::details() const {
	if (d->loading) {
		return "Looking for printers…";
	}
	if (d->printers.isEmpty()) {
		return "No printers were found. Turn your printer on and connect it to this computer or its "
			"network; Settings adds one by hand.";
	}
	const int row = d->list.state.selected;
	if (row < 0 || row >= d->printers.size()) {
		return "Click a printer to print with it.";
	}
	const Printer &p = d->printers[row];
	QStringList lines;
	lines << "Documents you print go to this printer.";
	if (!p.model.isEmpty() && p.model != p.shownName()) {
		lines << "Model: " + p.model;
	}
	if (!p.location.isEmpty()) {
		lines << "Location: " + p.location;
	}
	lines << "Status: " + p.status();
	if (p.temporary) {
		lines << "Found on the network; it's set up the first time you print.";
	}
	return lines.join('\n');
}

void PickerWindow::pick(int row) {
	d->list.select(row, true);
}

void PickerWindow::choose(int row) {
	if (row < 0 || row >= d->printers.size() || d->printers[row].isDefault) {
		updateButtons();
		return;
	}
	QString error;
	if (!setDefaultPrinter(d->printers[row].name, &error)) {
		Alert::ask("That printer couldn't be chosen. " + error, "OK", QString());
	}
	reload();
}

QString PickerWindow::createDesktopPrinterForSelected() {
	const int row = d->list.state.selected;
	if (row < 0 || row >= d->printers.size()) {
		return QString();
	}
	const Printer &p = d->printers[row];
	QString error;
	const QString path = createDesktopPrinter(desktopFolder(), p.shownName(), p.name, &error);
	if (path.isEmpty()) {
		Alert::ask("The desktop printer couldn't be made. " + error, "OK", QString());
	}
	return path;
}

void PickerWindow::openQueue() {
	const int row = d->list.state.selected;
	if (row >= 0 && row < d->printers.size()) {
		platinumStartApplication(siblingProgram("zacos9-printqueue"), { d->printers[row].name });
	}
}

void PickerWindow::updateButtons() {
	const bool sel = d->list.state.selected >= 0 && d->list.state.selected < d->printers.size();
	d->desktop.enabled = sel;
	d->queue.enabled = sel;
	update();
}

void PickerWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	panelText(c, "Select a printer:", M, LIST_TOP - 10, PL_FONT_SYSTEM);
	d->list.paint(c, true);
	int y = LIST_BOTTOM + 18;
	for (const QString &para : details().split('\n')) {
		for (const QString &line : panelWrap(para, W - 2 * M, PL_FONT_VIEWS)) {
			if (y < H - M - PL_BUTTON_H - 6) {
				panelText(c, line, M, y, PL_FONT_VIEWS, C_BLACK, W - 2 * M);
			}
			y += 12;
		}
	}
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void PickerWindow::mousePressEvent(QMouseEvent *e) {
	if (d->list.press(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostPress(e);
	update();
}

void PickerWindow::mouseMoveEvent(QMouseEvent *e) {
	if (d->list.move(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostMove(e);
	update();
}

void PickerWindow::mouseReleaseEvent(QMouseEvent *e) {
	d->list.release();
	d->host.hostRelease(e);
	update();
}

void PickerWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	// Double-clicking a printer opens its queue.
	if (d->list.frame.contains(e->position().toPoint()) && d->list.state.selected >= 0) {
		openQueue();
		return;
	}
	mousePressEvent(e);
}

void PickerWindow::wheelEvent(QWheelEvent *e) {
	if (d->list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void PickerWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && (e->key() == Qt::Key_W || e->key() == Qt::Key_Q)) {
		close();
		return;
	}
	if (d->list.key(e->key(), e->text())) {
		update();
		return;
	}
	d->host.hostKey(e);
	update();
}
