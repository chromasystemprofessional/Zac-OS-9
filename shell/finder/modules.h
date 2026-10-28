#pragma once

/*
 * Turning the Extensions Manager's curated extensions on or off for
 * start-up: "on" lists their kernel modules in modules-load.d and removes
 * any modprobe.d blacklist; "off" blacklists them and removes them from
 * modules-load.d. Changes take effect after restart. The privileged write
 * goes to zacos9-modules-helper (pkexec); reading the two config files is
 * safe without root.
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

/* Runs zacos9-modules-helper as root (pkexec unless already root) and waits
 * for it, keeping windows drawn. *ok and *err on its behalf. */
QString runModulesHelper(const QStringList &args, bool *ok, QString *err = nullptr);

/* Asynchronous version: done(success, error) is delivered on *receiver. */
void setExtensionEnabledAsync(QObject *receiver, const QString &key, bool on,
	const QStringList &modules,
	std::function<void(bool, const QString &)> done);
