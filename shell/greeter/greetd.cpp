#include "greetd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <cstdint>
#include <cstring>

Greetd::Greetd(const QString &socketPath)
	: m_path(socketPath.isEmpty() ? qEnvironmentVariable("GREETD_SOCK") : socketPath) {}

QByteArray Greetd::frame(const QJsonObject &message) {
	const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
	const uint32_t length = static_cast<uint32_t>(json.size());
	QByteArray bytes(reinterpret_cast<const char *>(&length), sizeof(length));
	return bytes + json;
}

void Greetd::login(const QString &user, const QString &password, const QStringList &command) {
	if (busy()) {
		return;
	}
	if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
		m_socket = std::make_unique<QLocalSocket>();
		QObject::connect(m_socket.get(), &QLocalSocket::readyRead, [this] { readMessages(); });
		QObject::connect(m_socket.get(), &QLocalSocket::disconnected, [this] {
			if (busy()) {
				m_state = Idle;
				if (failed) {
					failed("The login service stopped answering. Try again.");
				}
			}
		});
		m_buffer.clear();
		m_socket->connectToServer(m_path);
		if (m_path.isEmpty() || !m_socket->waitForConnected(2000)) {
			if (failed) {
				failed("The login service isn't running.");
			}
			return;
		}
	}
	m_state = Authenticating;
	m_password = password;
	m_command = command;
	m_answered = false;
	m_note.clear();
	m_why.clear();
	send({ { "type", "create_session" }, { "username", user } });
}

void Greetd::send(const QJsonObject &message) {
	m_socket->write(frame(message));
	m_socket->flush();
}

void Greetd::readMessages() {
	m_buffer += m_socket->readAll();
	while (m_buffer.size() >= 4) {
		uint32_t length;
		memcpy(&length, m_buffer.constData(), sizeof(length));
		if (static_cast<qsizetype>(length) > m_buffer.size() - 4) {
			return;
		}
		const QJsonObject message = QJsonDocument::fromJson(m_buffer.mid(4, length)).object();
		m_buffer.remove(0, 4 + length);
		handle(message);
	}
}

void Greetd::fail(const QString &why) {
	/* greetd keeps the half-made session until it's cancelled, and won't
	 * start another before then. */
	m_why = why;
	m_state = Cancelling;
	send({ { "type", "cancel_session" } });
}

void Greetd::handle(const QJsonObject &message) {
	const QString type = message["type"].toString();
	if (m_state == Cancelling) {
		m_state = Idle;
		if (failed) {
			failed(m_why);
		}
		return;
	}
	if (type == "success") {
		if (m_state == Authenticating) {
			m_password.fill(QChar(0));
			m_password.clear();
			m_state = Starting;
			send({ { "type", "start_session" }, { "cmd", QJsonArray::fromStringList(m_command) },
				{ "env", QJsonArray() } });
		} else if (m_state == Starting) {
			m_state = Idle;
			if (started) {
				started();
			}
		}
	} else if (type == "auth_message") {
		const QString kind = message["auth_message_type"].toString();
		const QString text = message["auth_message"].toString().trimmed();
		if (kind == "info" || kind == "error") {
			m_note = text;
			send({ { "type", "post_auth_message_response" }, { "response", QJsonValue::Null } });
		} else if (!m_answered) {
			/* The password prompt ("secret"), or a "visible" one such as a
			 * name: we have one answer, the password. */
			m_answered = true;
			send({ { "type", "post_auth_message_response" }, { "response", m_password } });
		} else {
			/* A second question: an expired password wanting a new one, a
			 * one-time code. The window can't ask those. */
			fail(text.isEmpty() ? QString("This account needs more than a password to log in.")
				: text);
		}
	} else if (type == "error") {
		const QString kind = message["error_type"].toString();
		QString why = kind == "auth_error" ? QString("Sorry, that password isn't right.")
			: message["description"].toString();
		if (kind != "auth_error" && !m_note.isEmpty()) {
			why = m_note;
		}
		if (why.isEmpty()) {
			why = "The login didn't work. Try again.";
		}
		fail(why);
	}
}
