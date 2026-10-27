#include "memory.h"
#include "programs.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <algorithm>
#include <cmath>
#include <unistd.h>

#include "panelkit.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 420, H = 586, M = 12;
static constexpr int CACHE_Y = 18, VM_Y = 78, ZRAM_Y = 292, RAM_Y = 398, RAM_BOTTOM = 528;
static constexpr int ZRAM_PERCENTS[] = { 25, 50, 75, 100 };
static constexpr int INSET = M + 16;
static constexpr qint64 MB = 1024 * 1024;

MemoryStatus parseMemoryStatus(const QString &output) {
	MemoryStatus s;
	for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
		const QStringList f = line.split('\t');
		const auto n = [&f](int i) { return f.value(i).toLongLong(); };
		const auto yes = [&f](int i) { return f.value(i) == QLatin1String("yes"); };
		if (f[0] == "memory") {
			s.total = n(1);
			s.available = n(2);
			s.cache = n(3);
			s.valid = s.total > 0;
		} else if (f[0] == "swapfile") {
			s.swapSize = n(1);
			s.swapUsed = n(2);
			s.swapOn = yes(3);
		} else if (f[0] == "swapfile-room") {
			s.swapRoom = n(1);
		} else if (f[0] == "other-swap") {
			s.otherSwap << f.value(1) + ", " + memorySizeText(n(2));
		} else if (f[0] == "zram") {
			s.zramInstalled = yes(1);
			s.zramOn = yes(2);
			s.zramPercent = int(n(3));
			s.zramSize = n(4);
			s.zramUsed = n(5);
			s.zramCompressed = n(6);
		} else if (f[0] == "guard") {
			s.guardInstalled = yes(1);
			s.guardOn = yes(2);
		} else if (f[0] == "ramdisk") {
			s.ramDiskOn = yes(1);
			s.ramDiskSave = yes(4);
			s.ramDiskSize = s.ramDiskOn ? n(2) : n(5);
			s.ramDiskUsed = n(3);
		}
	}
	return s;
}

QString memorySizeText(qint64 bytes) {
	const double gb = double(bytes) / (1024.0 * MB);
	if (gb >= 1) {
		const double rounded = std::round(gb * 10) / 10;
		return (rounded == std::floor(rounded) ? QString::number(qint64(rounded))
			: QString::number(rounded, 'f', 1)) + " GB";
	}
	return QString::number((bytes + MB / 2) / MB) + " MB";
}

static QList<int> steps(QList<int> candidates, qint64 most, qint64 currentBytes) {
	QList<int> out;
	for (int mb : candidates) {
		if (mb * MB <= most) {
			out << mb;
		}
	}
	const int current = int((currentBytes + MB / 2) / MB);
	if (current > 0 && !out.contains(current)) {
		out << current;
		std::sort(out.begin(), out.end());
	}
	return out;
}

QList<int> memorySwapSteps(qint64 roomBytes, qint64 currentBytes) {
	return steps({ 1024, 2048, 4096, 8192, 12288, 16384, 24576, 32768, 49152, 65536 },
		roomBytes + currentBytes - 2048 * MB, currentBytes);
}

QList<int> memoryRamDiskSteps(qint64 totalBytes, qint64 currentBytes) {
	return steps({ 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 }, totalBytes / 2, currentBytes);
}

/* The helper: installed beside us, or the source tree's. */
static QString helperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_MEMORY_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-memory-helper",
			sourceTreePath("memory/zacos9-memory-helper") }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/zacos9-memory-helper";
}

/* Runs the helper to the end, keeping the window drawn (pkexec may be
 * asking for a password; making a swap file takes a moment). No
 * waitForFinished(): see CLAUDE.md. */
static QString runHelper(const QStringList &args, bool privileged, bool *ok, QString *err) {
	QString program = helperPath();
	QStringList arguments = args;
	if (privileged && geteuid() != 0 && qEnvironmentVariableIsEmpty("ZACOS9_MEMORY_HELPER")) {
		arguments.prepend(program);
		program = "pkexec";
	}
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	p.start(program, arguments);
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	*ok = started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	*err = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
	if (!*ok && err->isEmpty()) {
		*err = started ? QString("The memory helper stopped with status %1.").arg(p.exitCode())
			: QString("The memory helper couldn't be run.");
	}
	return QString::fromUtf8(p.readAllStandardOutput());
}

struct MemoryPanel::Parts {
	PanelHost host;
	PanelRadios vm, zram, ramDisk;
	PanelPopup zramSize;
	PanelSlider vmSize, ramDiskSize;
	PanelCheckbox guard, save;
	PanelButton defaults;
	QList<int> vmSteps, ramDiskSteps;
	int vmPressed = -1, ramDiskPressed = -1; /* slider values when pressed */
	QRect vmArea, ramDiskArea; /* the sliders, for their hints */
	MemoryStatus status;
	QString problem;
	bool busy = false;
	explicit Parts(QWidget *w) : host(w) {}
};

MemoryPanel::MemoryPanel() : d(std::make_unique<Parts>(this)) {
	setWindowTitle("Memory");
	setFixedSize(W, H);
	d->vm.add("On", QPoint(INSET, VM_Y + 24));
	d->vm.add("Off", QPoint(INSET + 64, VM_Y + 24));
	d->vm.hint = "Virtual memory lets programs use more memory than the computer has, "
		"keeping what they use least on the startup disk.";
	d->vm.changed = [this](int i) { setVirtualMemory(i == 0); };
	d->vmSize.pos = QPoint(INSET, VM_Y + 86);
	d->vmSize.width = W - 2 * INSET;
	d->vmSize.hint = "How much of the startup disk virtual memory may use.";
	d->vmSize.changed = [this](int) { update(); };
	d->guard = PanelCheckbox("Quit the biggest program before memory runs out", QPoint(INSET, VM_Y + 162));
	d->guard.hint = "When memory and virtual memory are nearly full, quits the program using the "
		"most, instead of letting the computer stop responding (earlyoom).";
	d->guard.toggled = [this](bool on) { setGuard(on); };
	d->zram.add("On", QPoint(INSET, ZRAM_Y + 24));
	d->zram.add("Off", QPoint(INSET + 64, ZRAM_Y + 24));
	d->zram.hint = "Compressed Memory keeps memory that hasn't been used for a while compressed "
		"in RAM, so there is room for more before anything goes to the disk.";
	d->zram.changed = [this](int i) {
		setCompressedMemory(i == 0 ? ZRAM_PERCENTS[std::clamp(d->zramSize.selected, 0, 3)] : 0);
	};
	d->zramSize.label = "Use up to:";
	d->zramSize.rect = QRect(INSET + 70, ZRAM_Y + 48, 214, PL_POPUP_H);
	d->zramSize.items = { PopupItem("a quarter of memory"), PopupItem("half of memory"),
		PopupItem("three quarters of memory"), PopupItem("all of memory") };
	d->zramSize.hint = "The most of the computer's memory Compressed Memory may hold. It only takes what it needs.";
	d->zramSize.chosen = [this](int row) {
		if (row != d->zramSize.selected && d->status.zramOn) {
			setCompressedMemory(ZRAM_PERCENTS[std::clamp(row, 0, 3)]);
		} else {
			d->zramSize.selected = row;
		}
	};
	d->ramDisk.add("On", QPoint(INSET, RAM_Y + 24));
	d->ramDisk.add("Off", QPoint(INSET + 64, RAM_Y + 24));
	d->ramDisk.hint = "A RAM Disk is a disk made of memory: very fast, and emptied when the "
		"computer shuts down unless it is saved.";
	d->ramDisk.changed = [this](int i) { setRamDisk(i == 0); };
	d->ramDiskSize.pos = QPoint(INSET, RAM_Y + 48);
	d->ramDiskSize.width = W - 2 * INSET;
	d->ramDiskSize.hint = "How much memory the RAM Disk may hold.";
	d->ramDiskSize.changed = [this](int) { update(); };
	d->save = PanelCheckbox("Save on Shut Down", QPoint(INSET, RAM_Y + 106));
	d->save.hint = "Keeps what's on the RAM Disk when the computer shuts down, and puts it back at startup.";
	d->save.toggled = [this](bool on) { setRamDiskSave(on); };
	d->defaults = PanelButton("Use Defaults", QRect(M, H - M - PL_BUTTON_H, 110, PL_BUTTON_H));
	d->defaults.hint = "Virtual memory on, the out-of-memory guard on, Compressed Memory off, and no RAM Disk.";
	d->defaults.clicked = [this] { useDefaults(); };
	d->host.buttons = { &d->defaults };
	d->host.checks = { &d->guard, &d->save };
	d->host.radios = { &d->vm, &d->zram, &d->ramDisk };
	d->host.popups = { &d->zramSize };
	d->vmArea = d->vmSize.hitRect();
	d->ramDiskArea = d->ramDiskSize.hitRect();
	d->host.hintAreas.push_back({ &d->vmArea, d->vmSize.hint });
	d->host.hintAreas.push_back({ &d->ramDiskArea, d->ramDiskSize.hint });
	reload();
}

MemoryPanel::~MemoryPanel() = default;

const MemoryStatus &MemoryPanel::status() const {
	return d->status;
}

QString MemoryPanel::problem() const {
	return d->problem;
}

/* The controls, from what the helper says now. */
void MemoryPanel::reload() {
	bool ok = false;
	QString err;
	const QString out = runHelper({ "status" }, false, &ok, &err);
	d->status = ok ? parseMemoryStatus(out) : MemoryStatus();
	if (!ok || !d->status.valid) {
		d->problem = ok ? QString("The memory settings couldn't be read.") : err;
	}
	const MemoryStatus &s = d->status;
	d->vm.selected = s.swapOn ? 0 : 1;
	d->vmSteps = memorySwapSteps(s.swapRoom, s.swapSize);
	const int vmCurrent = s.swapOn ? int((s.swapSize + MB / 2) / MB) : 16384;
	d->vmSize.steps = std::max<int>(2, d->vmSteps.size());
	int vmValue = 0;
	for (int i = 0; i < d->vmSteps.size(); i++) {
		if (d->vmSteps[i] <= vmCurrent) {
			vmValue = i;
		}
	}
	d->vmSize.value = vmValue;
	d->vmSize.enabled = !d->busy && s.swapOn && d->vmSteps.size() > 1;
	if (!d->vmSteps.isEmpty()) {
		d->vmSize.setLabels(memorySizeText(d->vmSteps.front() * MB), memorySizeText(d->vmSteps.back() * MB));
	}
	d->zram.selected = s.zramOn ? 0 : 1;
	for (int i = 0; i < 4; i++) {
		if (ZRAM_PERCENTS[i] == s.zramPercent) {
			d->zramSize.selected = i;
		}
	}
	d->zram.enabled = d->zramSize.enabled = !d->busy && s.valid && (s.zramInstalled || s.zramOn);
	d->guard.on = s.guardOn;
	d->guard.enabled = !d->busy && s.guardInstalled;
	d->ramDisk.selected = s.ramDiskOn ? 0 : 1;
	d->ramDiskSteps = memoryRamDiskSteps(s.total, s.ramDiskSize);
	const int ramCurrent = s.ramDiskSize > 0 ? int((s.ramDiskSize + MB / 2) / MB) : 512;
	d->ramDiskSize.steps = std::max<int>(2, d->ramDiskSteps.size());
	int ramValue = 0;
	for (int i = 0; i < d->ramDiskSteps.size(); i++) {
		if (d->ramDiskSteps[i] <= ramCurrent) {
			ramValue = i;
		}
	}
	d->ramDiskSize.value = ramValue;
	d->ramDiskSize.enabled = !d->busy && d->ramDiskSteps.size() > 1;
	if (!d->ramDiskSteps.isEmpty()) {
		d->ramDiskSize.setLabels(memorySizeText(d->ramDiskSteps.front() * MB),
			memorySizeText(d->ramDiskSteps.back() * MB));
	}
	d->save.on = s.ramDiskSave;
	d->vm.enabled = d->ramDisk.enabled = d->save.enabled = d->defaults.enabled = !d->busy && s.valid;
	update();
}

bool MemoryPanel::apply(const QStringList &args) {
	d->busy = true;
	d->problem.clear();
	reload();
	repaint();
	bool ok = false;
	QString err;
	runHelper(args, true, &ok, &err);
	d->busy = false;
	if (!ok) {
		d->problem = err;
	}
	reload();
	return ok;
}

static int chosen(const QList<int> &steps, const PanelSlider &slider, int fallback) {
	return steps.isEmpty() ? fallback : steps.value(std::clamp(slider.value, 0, int(steps.size()) - 1));
}

void MemoryPanel::setVirtualMemory(bool on) {
	apply(on ? QStringList{ "swap", QString::number(chosen(d->vmSteps, d->vmSize, 4096)) }
		: QStringList{ "swap", "off" });
}

void MemoryPanel::setVirtualMemorySize(int megabytes) {
	apply({ "swap", QString::number(megabytes) });
}

void MemoryPanel::setGuard(bool on) {
	apply({ "guard", on ? "on" : "off" });
}

void MemoryPanel::setCompressedMemory(int percent) {
	apply(percent > 0 ? QStringList{ "zram", QString::number(percent) } : QStringList{ "zram", "off" });
}

void MemoryPanel::setRamDisk(bool on) {
	apply(on ? QStringList{ "ramdisk", QString::number(chosen(d->ramDiskSteps, d->ramDiskSize, 512)),
			d->save.on ? "save" : "nosave" }
		: QStringList{ "ramdisk", "off" });
}

void MemoryPanel::setRamDiskSize(int megabytes) {
	if (d->status.ramDiskOn) {
		apply({ "ramdisk", QString::number(megabytes), d->save.on ? "save" : "nosave" });
	}
}

void MemoryPanel::setRamDiskSave(bool on) {
	if (d->status.ramDiskOn) {
		apply({ "ramdisk", QString::number(chosen(d->ramDiskSteps, d->ramDiskSize, 512)),
			on ? "save" : "nosave" });
	}
}

/* Mac OS 9's defaults, as far as they go here: virtual memory on (16 GB,
 * or as much as the disk allows), the guard on, no RAM Disk; and Compressed
 * Memory off, as the package leaves it. */
void MemoryPanel::useDefaults() {
	const QList<int> sizes = memorySwapSteps(d->status.swapRoom, d->status.swapSize);
	int size = 0;
	for (int mb : sizes) {
		if (mb <= 16384) {
			size = mb;
		}
	}
	if (size > 0 && !(d->status.swapOn && (d->status.swapSize + MB / 2) / MB == size) &&
			!apply({ "swap", QString::number(size) })) {
		return;
	}
	if (d->status.guardInstalled && !d->status.guardOn && !apply({ "guard", "on" })) {
		return;
	}
	if (d->status.zramOn && !apply({ "zram", "off" })) {
		return;
	}
	if (d->status.ramDiskOn) {
		apply({ "ramdisk", "off" });
	}
}

void MemoryPanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	const MemoryStatus &s = d->status;
	const int text = W - 2 * INSET;

	panelGroup(c, M, CACHE_Y, W - M - 1, VM_Y - 10, "Disk Cache", FACE);
	panelText(c, "Recently used files kept in memory: " + memorySizeText(s.cache), INSET, CACHE_Y + 26);
	panelText(c, "The system sizes the disk cache by itself.", INSET, CACHE_Y + 42, PL_FONT_VIEWS, GRAY(0x4));

	panelGroup(c, M, VM_Y, W - M - 1, ZRAM_Y - 10, "Virtual Memory", FACE);
	panelText(c, "On the startup disk. Available on disk: " + memorySizeText(s.swapRoom), INSET, VM_Y + 58,
		PL_FONT_SYSTEM, C_BLACK, text);
	panelText(c, "Built-in memory: " + memorySizeText(s.total) + "   Available now: " +
		memorySizeText(s.available), INSET, VM_Y + 74, PL_FONT_SYSTEM, C_BLACK, text);
	const int vmMb = chosen(d->vmSteps, d->vmSize, 0);
	const qint64 vmBytes = s.swapOn ? vmMb * MB : 0;
	panelText(c, s.swapOn ? "Virtual memory: " + memorySizeText(vmBytes) + "   In use: " +
			memorySizeText(s.swapUsed) : QString("Virtual memory is off."),
		INSET, VM_Y + 136, PL_FONT_SYSTEM, C_BLACK, text);
	panelText(c, "Total memory: " + memorySizeText(s.total + vmBytes), INSET, VM_Y + 152,
		PL_FONT_SYSTEM, C_BLACK, text);
	if (!s.guardInstalled && s.valid) {
		panelText(c, "(earlyoom isn't installed)", INSET + PL_CHECKBOX_LABEL_X, VM_Y + 192,
			PL_FONT_VIEWS, GRAY(0x6));
	} else if (!s.otherSwap.isEmpty()) {
		panelText(c, "Also: " + s.otherSwap.join("; "), INSET, VM_Y + 192, PL_FONT_VIEWS, GRAY(0x6), text);
	}

	panelGroup(c, M, ZRAM_Y, W - M - 1, RAM_Y - 10, "Compressed Memory", FACE);
	QString zramLine;
	if (!s.zramInstalled && !s.zramOn) {
		zramLine = "Needs systemd-zram-generator, which isn't installed.";
	} else if (s.zramSize > 0) {
		zramLine = "Compressed memory: " + memorySizeText(s.zramSize) + "   In use: " +
			memorySizeText(s.zramUsed);
		if (s.zramUsed > 0 && s.zramCompressed > 0) {
			zramLine += ", kept in " + memorySizeText(s.zramCompressed);
		}
	} else {
		zramLine = s.zramOn ? QString("Set up, but not running.") : QString("Compressed Memory is off.");
	}
	panelText(c, zramLine, INSET, ZRAM_Y + 88, PL_FONT_SYSTEM, C_BLACK, text);

	panelGroup(c, M, RAM_Y, W - M - 1, RAM_BOTTOM, "RAM Disk", FACE);
	const qint64 ramBytes = chosen(d->ramDiskSteps, d->ramDiskSize, 0) * MB;
	panelText(c, "RAM Disk size: " + memorySizeText(ramBytes) +
			(s.ramDiskOn ? "   In use: " + memorySizeText(s.ramDiskUsed) : QString()),
		INSET, RAM_Y + 98, PL_FONT_SYSTEM, C_BLACK, text);

	QString line = d->busy ? QString("One moment…") : d->problem;
	int y = RAM_BOTTOM + 18;
	for (const QString &part : panelWrap(line, W - 2 * M, PL_FONT_VIEWS)) {
		panelText(c, part, M, y, PL_FONT_VIEWS, d->problem.isEmpty() ? C_BLACK : RGB(0xAA, 0x22, 0x22));
		y += 12;
	}
	d->vmSize.paint(c);
	d->ramDiskSize.paint(c);
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void MemoryPanel::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (!d->busy && d->vmSize.press(pos)) {
		d->vmPressed = d->vmSize.value;
	} else if (!d->busy && d->ramDiskSize.press(pos)) {
		d->ramDiskPressed = d->ramDiskSize.value;
	} else {
		d->host.hostPress(e);
	}
	update();
}

void MemoryPanel::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (!d->vmSize.move(pos) && !d->ramDiskSize.move(pos)) {
		d->host.hostMove(e);
	}
	update();
}

/* A slider's size applies when it's let go. */
void MemoryPanel::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	d->vmSize.release(pos);
	d->ramDiskSize.release(pos);
	if (d->vmPressed >= 0) {
		const int was = d->vmPressed;
		d->vmPressed = -1;
		if (d->vmSize.value != was) {
			setVirtualMemorySize(chosen(d->vmSteps, d->vmSize, 4096));
		}
	} else if (d->ramDiskPressed >= 0) {
		const int was = d->ramDiskPressed;
		d->ramDiskPressed = -1;
		if (d->ramDiskSize.value != was) {
			setRamDiskSize(chosen(d->ramDiskSteps, d->ramDiskSize, 512));
		}
	} else {
		d->host.hostRelease(e);
	}
	update();
}

void MemoryPanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	d->host.hostKey(e);
	update();
}
