#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

/*
 * A greetd client (https://man.sr.ht/~kennylevinsen/greetd/): the login
 * window asks greetd, over $GREETD_SOCK, to log a user in. greetd runs
 * PAM itself; we only answer its prompts with the password. Messages are
 * JSON, each after a 32-bit length in the machine's own byte order.
 *
 *   create_session {username}    -> auth_message | success | error
 *   post_auth_message_response   -> auth_message | success | error
 *   start_session {cmd, env}     -> success: greetd starts the session
 *                                   as soon as the greeter exits
 *   cancel_session               -> success (after any error)
 */
class Greetd {
public:
	/* `socketPath` empty: $GREETD_SOCK. */
	explicit Greetd(const QString &socketPath = QString());

	/* Log `user` in and, once PAM accepts, start `command` as their
	 * session. One login at a time; busy() until it ends. */
	void login(const QString &user, const QString &password, const QStringList &command);
	bool busy() const { return m_state != Idle; }

	/* The session will start once we exit. */
	std::function<void()> started;
	/* The login didn't go through; why, in a sentence for the window. */
	std::function<void(const QString &)> failed;

	static QByteArray frame(const QJsonObject &message);

private:
	enum State { Idle, Authenticating, Starting, Cancelling };

	void send(const QJsonObject &message);
	void readMessages();
	void handle(const QJsonObject &message);
	void fail(const QString &why);

	QString m_path;
	std::unique_ptr<QLocalSocket> m_socket;
	QByteArray m_buffer;
	State m_state = Idle;
	QString m_password;
	QStringList m_command;
	bool m_answered = false; /* the password went to one secret prompt */
	QString m_note;          /* PAM's last info or error text */
	QString m_why;           /* reported once the cancel is acknowledged */
};
