#pragma once

/*
 * Desktop printers (Mac OS 9's, Missing Manual p. 375): a small file on the
 * desktop, "NAME.printer", naming a CUPS queue:
 *
 *   [Desktop Printer]
 *   Queue=Brother_MFC_9130CW
 *
 * The Sniffer shows it with a printer's icon; opening it opens the queue's
 * window, and documents dropped on it are printed there. Printer Picker
 * makes them.
 */

#include <QString>

inline constexpr const char *DESKTOP_PRINTER_SUFFIX = "printer";

/* The queue a desktop printer file names, or "" if it isn't one. */
QString desktopPrinterQueue(const QString &path);
/* Writes NAME.printer for a queue into a folder; its path, or "" (error set). */
QString createDesktopPrinter(const QString &folder, const QString &name, const QString &queue, QString *error);
