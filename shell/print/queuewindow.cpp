#include "queuewindow.h"

#include <QDateTime>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#include "alert.h"
#include "icons.h"
#include "panelkit.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD);
constexpr int W = 440, H = 360, M = 12;
constexpr int LIST_TOP = 74, LIST_BOTTOM = 262;

QString when(qint64 seconds) {
	if (seconds <= 0) {
		return QString();
	}
	const QDateTime t = QDateTime::fromSecsSinceEpoch(seconds);
	return t.date() == QDate::currentDate() ? t.toString("h:mm AP") : t.toString("MMM d, h:mm AP");
}

} // namespace

struct QueueWindow::Parts {
	QString queue;
	Printer printer;
	bool known = false;
	QList<PrintJob> jobs;
	PanelHost host;
	PanelList list;
	PanelButton hold, remove, stop;
	QTimer timer;
	bool busy = false;
	explicit Parts(QWidget *w) : host(w) {}
};

QueueWindow::QueueWindow(const QString &printer) : d(std::make_unique<Parts>(this)) {
	d->queue = printer;
	setWindowTitle(QString(printer).replace('_', ' '));
	setFixedSize(W, H);
	setAcceptDrops(true);
	d->list.frame = QRect(M, LIST_TOP, W - 2 * M, LIST_BOTTOM - LIST_TOP);
	d->list.picked = [this](int) { updateButtons(); };
	const int y = H - M - PL_BUTTON_H;
	d->hold = PanelButton("Hold", QRect(M, y, 80, PL_BUTTON_H));
	d->hold.clicked = [this] { holdOrResume(); };
	d->remove = PanelButton("Remove", QRect(M + 88, y, 80, PL_BUTTON_H));
	d->remove.clicked = [this] { removeJob(); };
	d->stop = PanelButton("Stop Printing", QRect(W - M - 4 - 120, y, 120, PL_BUTTON_H));
	d->stop.clicked = [this] { stopOrStart(); };
	d->host.buttons = { &d->hold, &d->remove, &d->stop };
	d->timer.setInterval(2000);
	d->timer.callOnTimeout(this, [this] { refresh(); });
	d->timer.start();
	QTimer::singleShot(0, this, [this] { refresh(); });
}

QueueWindow::~QueueWindow() = default;

void QueueWindow::refresh() {
	if (d->busy) {
		return; /* still asking from last time */
	}
	d->busy = true;
	d->known = false;
	for (const Printer &p : listPrinters()) {
		if (p.name == d->queue) {
			d->printer = p;
			d->known = true;
		}
	}
	const int selectedId = d->list.state.selected >= 0 && d->list.state.selected < d->jobs.size()
		? d->jobs[d->list.state.selected].id : 0;
	d->jobs = d->known ? listJobs(d->queue) : QList<PrintJob>();
	d->busy = false;
	if (d->known) {
		setWindowTitle(d->printer.shownName());
	}
	QStringList rows;
	std::vector<int> icons;
	int selected = -1;
	for (int i = 0; i < d->jobs.size(); ++i) {
		const PrintJob &j = d->jobs[i];
		rows << (j.title.isEmpty() ? QString("Job %1").arg(j.id) : j.title) + "  –  " + j.status();
		icons.push_back(PL_ICON_DOCUMENT);
		if (j.id == selectedId) {
			selected = i;
		}
	}
	if (rows != d->list.items) {
		const int top = d->list.state.top;
		d->list.setItems(rows, icons);
		d->list.scrollTo(top);
	}
	d->list.select(selected, false);
	updateButtons();
}

QStringList QueueWindow::rows() const {
	return d->list.items;
}

QString QueueWindow::printerStatus() const {
	if (!d->known) {
		return "This printer isn't set up on this computer any more.";
	}
	QString s = d->printer.status();
	if (d->jobs.isEmpty()) {
		s += ". No documents waiting.";
	} else {
		s += QString(". %1 document%2.").arg(d->jobs.size()).arg(d->jobs.size() == 1 ? "" : "s");
	}
	return s;
}

QString QueueWindow::jobDetails() const {
	const int row = d->list.state.selected;
	if (row < 0 || row >= d->jobs.size()) {
		return d->jobs.isEmpty() ? QString("Drop documents here to print them.") : QString();
	}
	const PrintJob &j = d->jobs[row];
	QStringList parts{ j.status() };
	if (!j.owner.isEmpty()) {
		parts << "from " + j.owner;
	}
	if (j.kilobytes > 0) {
		parts << QString("%1 KB").arg(j.kilobytes);
	}
	if (!when(j.created).isEmpty()) {
		parts << "sent " + when(j.created);
	}
	return parts.join(", ") + ".";
}

void QueueWindow::selectJob(int row) {
	d->list.select(row, true);
	update();
}

void QueueWindow::updateButtons() {
	const int row = d->list.state.selected;
	const bool job = row >= 0 && row < d->jobs.size();
	const bool held = job && d->jobs[row].held();
	*d->hold.label = Text(held ? "Resume" : "Hold", 80, PL_FONT_SYSTEM);
	d->hold.enabled = job && (held || d->jobs[row].state == "pending");
	d->remove.enabled = job;
	*d->stop.label = Text(d->printer.state == 5 ? "Start Printing" : "Stop Printing", 120, PL_FONT_SYSTEM);
	d->stop.enabled = d->known;
	update();
}

void QueueWindow::problem(const QString &what, const QString &error) {
	Alert::ask(what + (error.isEmpty() ? QString() : " " + error), "OK", QString());
}

void QueueWindow::holdOrResume() {
	const int row = d->list.state.selected;
	if (row < 0 || row >= d->jobs.size()) {
		return;
	}
	const PrintJob j = d->jobs[row];
	QString error;
	if (!holdJob(d->queue, j.id, !j.held(), &error)) {
		problem(QString("“%1” couldn't be %2.").arg(j.title, j.held() ? "resumed" : "held"), error);
	}
	refresh();
}

void QueueWindow::removeJob() {
	const int row = d->list.state.selected;
	if (row < 0 || row >= d->jobs.size()) {
		return;
	}
	const PrintJob j = d->jobs[row];
	QString error;
	if (!cancelJob(d->queue, j.id, &error)) {
		problem(QString("“%1” couldn't be removed.").arg(j.title), error);
	}
	refresh();
}

void QueueWindow::stopOrStart() {
	const bool start = d->printer.state == 5;
	QString error;
	if (!setPrinterEnabled(d->queue, start, &error)) {
		problem(QString("The printer couldn't be %1.").arg(start ? "started" : "stopped"), error);
	}
	refresh();
}

void QueueWindow::printDropped(const QStringList &files) {
	QStringList printable, others;
	for (const QString &f : files) {
		(canPrintDirectly(f) ? printable : others) << f;
	}
	QString error;
	if (!printable.isEmpty() && printFiles(d->queue, printable, &error).size() < printable.size()) {
		problem("Some documents couldn't be printed.", error);
	}
	if (!others.isEmpty()) {
		QStringList names;
		for (const QString &f : others) {
			names << QFileInfo(f).fileName();
		}
		problem(QString("%1 can't be printed from here. Open %2 in %3 program and choose Print.")
			.arg(names.size() == 1 ? "“" + names[0] + "”" : QString("%1 of the documents").arg(names.size()),
				names.size() == 1 ? "it" : "them", names.size() == 1 ? "its" : "their"), QString());
	}
	refresh();
}

void QueueWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	pl_icon_paint(c, M, M, PL_ICON_PRINTER, PL_ICON_LARGE, false);
	panelText(c, d->known ? d->printer.shownName() : QString(d->queue).replace('_', ' '), M + 44, M + 13,
		PL_FONT_SYSTEM, C_BLACK, W - M - 56);
	int y = M + 28;
	for (const QString &line : panelWrap(printerStatus(), W - M - 56, PL_FONT_VIEWS).mid(0, 2)) {
		panelText(c, line, M + 44, y, PL_FONT_VIEWS, C_BLACK, W - M - 56);
		y += 12;
	}
	panelText(c, "Documents:", M, LIST_TOP - 6, PL_FONT_SYSTEM);
	d->list.paint(c, true);
	y = LIST_BOTTOM + 16;
	for (const QString &line : panelWrap(jobDetails(), W - 2 * M, PL_FONT_VIEWS).mid(0, 2)) {
		panelText(c, line, M, y, PL_FONT_VIEWS, C_BLACK, W - 2 * M);
		y += 12;
	}
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void QueueWindow::mousePressEvent(QMouseEvent *e) {
	if (d->list.press(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostPress(e);
	update();
}

void QueueWindow::mouseMoveEvent(QMouseEvent *e) {
	if (d->list.move(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostMove(e);
	update();
}

void QueueWindow::mouseReleaseEvent(QMouseEvent *e) {
	d->list.release();
	d->host.hostRelease(e);
	update();
}

void QueueWindow::wheelEvent(QWheelEvent *e) {
	if (d->list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void QueueWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && (e->key() == Qt::Key_W || e->key() == Qt::Key_Q)) {
		close();
		return;
	}
	if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) {
		removeJob();
		return;
	}
	if (d->list.key(e->key(), e->text())) {
		update();
		return;
	}
	d->host.hostKey(e);
	update();
}

void QueueWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void QueueWindow::dropEvent(QDropEvent *e) {
	QStringList files;
	for (const QUrl &url : e->mimeData()->urls()) {
		if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
			files << url.toLocalFile();
		}
	}
	e->acceptProposedAction();
	if (!files.isEmpty()) {
		printDropped(files);
	}
}
