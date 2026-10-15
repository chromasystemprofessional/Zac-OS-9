#pragma once

/*
 * The Collar's printer module (Mac OS 9's Printer Selector Control Strip
 * module): the printers CUPS knows, yours ticked; choosing another makes
 * it yours. Asked of lpstat in the background (one look at a time); the
 * choice is lpoptions -d, which needs no password. ZACOS9_CUPS_BIN puts
 * stand-ins for both first (tests).
 */

#include <QObject>
#include <QStringList>
#include <functional>

class QProcess;

class PrinterChoice : public QObject {
public:
	explicit PrinterChoice(QObject *parent = nullptr);
	QStringList printers; /* queue names */
	QString current;      /* your printer, or "" */
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	void choose(const QString &printer);
	/* What to call a queue: "Office_Laser" -> "Office Laser". */
	static QString shownName(const QString &queue);

private:
	QProcess *m_look = nullptr;
	QProcess *start(const QString &script, const QString &arg = {});
};
