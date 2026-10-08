#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

/* The user's own systemd services (systemctl --user), for the Extensions
 * Manager's Background Services view. Only the user manager is ever
 * contacted: no system services, no privilege escalation. Essential
 * session services (D-Bus, audio, ZacOS itself, ...) are listed read-only. */
struct UserService {
	QString unit;        /* "foo.service" */
	QString description;
	QString fileState;   /* "enabled", "disabled", "static", ... */
	QString activeState; /* "active", "inactive", ... */
	bool manageable = false;
	QString reason;      /* why it is read-only */
};

enum class UserServiceAction { EnableAtLogin, DisableAtLogin, Start, Stop };

struct UserServiceQuery {
	QList<UserService> services;
	QString error;
	bool success = false;
};

/* Pure helpers (tested): parse `list-unit-files` and `show` output, and
 * decide what may be changed. */
QList<UserService> parseUserServices(const QString &unitFiles, const QString &show);
bool userServiceUnitSafe(const QString &unit, QString *reason = nullptr);

/* Query and mutate the user manager without blocking the UI thread. */
UserServiceQuery userServices();
bool userServiceAct(const QString &unit, UserServiceAction action, QString *error = nullptr);
void userServicesAsync(QObject *receiver, std::function<void(UserServiceQuery)> done);
void userServiceActAsync(QObject *receiver, const QString &unit, UserServiceAction action,
	std::function<void(bool, const QString &)> done);
