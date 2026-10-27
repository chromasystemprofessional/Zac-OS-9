/*
 * The login window's greetd client against a stand-in for greetd's
 * socket: what it sends, and how it takes greetd's answers.
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>

#include "greetd.h"

static bool waitFor(const std::function<bool()> &ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 10000) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return ready();
}

/* A greetd that knows one account: alice, password "sesame". `extra`
 * prompts come before the password one. Everything received is logged. */
struct FakeGreetd {
	QLocalServer server;
	QLocalSocket *client = nullptr;
	QByteArray buffer;
	std::vector<QJsonObject> received;
	std::vector<QJsonObject> extra;
	bool secondSecret = false;
	size_t step = 0;

	explicit FakeGreetd(const QString &path) {
		assert(server.listen(path));
		QObject::connect(&server, &QLocalServer::newConnection, [this] {
			client = server.nextPendingConnection();
			QObject::connect(client, &QLocalSocket::readyRead, [this] { read(); });
		});
	}
	void reply(const QJsonObject &m) { client->write(Greetd::frame(m)); }
	void secret() {
		reply({ { "type", "auth_message" }, { "auth_message_type", "secret" },
			{ "auth_message", "Password: " } });
	}
	void read() {
		buffer += client->readAll();
		while (buffer.size() >= 4) {
			uint32_t n;
			memcpy(&n, buffer.constData(), 4);
			if (buffer.size() < 4 + static_cast<qsizetype>(n)) {
				return;
			}
			const QJsonObject m = QJsonDocument::fromJson(buffer.mid(4, n)).object();
			buffer.remove(0, 4 + n);
			received.push_back(m);
			answer(m);
		}
	}
	void answer(const QJsonObject &m) {
		const QString type = m["type"].toString();
		if (type == "create_session") {
			step = 0;
			if (m["username"].toString() != "alice") {
				reply({ { "type", "error" }, { "error_type", "error" },
					{ "description", "pam_start: unknown user" } });
			} else if (!extra.empty()) {
				reply(extra[step++]);
			} else {
				step = 1; /* past the password question, asked now */
				secret();
			}
		} else if (type == "post_auth_message_response") {
			if (step < extra.size()) {
				reply(extra[step++]);
			} else if (step == extra.size()) {
				step++;
				secret();
			} else if (secondSecret) {
				secondSecret = false;
				reply({ { "type", "auth_message" }, { "auth_message_type", "secret" },
					{ "auth_message", "New password:" } });
			} else if (m["response"].toString() == "sesame") {
				reply({ { "type", "success" } });
			} else {
				reply({ { "type", "error" }, { "error_type", "auth_error" },
					{ "description", "pam_authenticate: AUTH_ERR" } });
			}
		} else {
			reply({ { "type", "success" } }); /* start_session, cancel_session */
		}
	}
	QStringList types() const {
		QStringList t;
		for (const QJsonObject &m : received) {
			t << m["type"].toString();
		}
		return t;
	}
};

struct Outcome {
	bool started = false;
	QString failed;
	bool done() const { return started || !failed.isEmpty(); }
};

static void watch(Greetd &g, Outcome &o) {
	g.started = [&o] { o.started = true; };
	g.failed = [&o](const QString &why) { o.failed = why; };
}

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	const QString path = dir.filePath("greetd.sock");

	/* The frame: native-order length, then compact JSON. */
	{
		const QByteArray f = Greetd::frame({ { "type", "cancel_session" } });
		uint32_t n;
		memcpy(&n, f.constData(), 4);
		assert(n == static_cast<uint32_t>(f.size() - 4));
		assert(f.mid(4) == "{\"type\":\"cancel_session\"}");
	}

	/* No greetd at all. */
	{
		Greetd g(dir.filePath("absent.sock"));
		Outcome o;
		watch(g, o);
		g.login("alice", "sesame", { "zacos9-session" });
		assert(!o.failed.isEmpty() && !g.busy());
	}

	FakeGreetd fake(path);

	/* The right password: the session is started, with our command. */
	{
		Greetd g(path);
		Outcome o;
		watch(g, o);
		g.login("alice", "sesame", { "zacos9-session" });
		assert(g.busy());
		assert(waitFor([&] { return o.done(); }));
		assert(o.started && !g.busy());
		assert((fake.types() == QStringList{ "create_session", "post_auth_message_response",
			"start_session" }));
		assert(fake.received[0]["username"] == "alice");
		assert(fake.received[1]["response"] == "sesame");
		assert(fake.received[2]["cmd"].toArray() == QJsonArray{ "zacos9-session" });
		assert(fake.received[2]["env"].toArray().isEmpty());
		fake.received.clear();
		o = Outcome();
		g.login("alice", "sesame", { "zacos9-session" }, { "ZACOS9_SAFE_MODE=1" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.started && fake.received[2]["env"].toArray() ==
			QJsonArray{ "ZACOS9_SAFE_MODE=1" });
	}

	/* A wrong password: cancelled, then said; and the next try works. */
	{
		fake.received.clear();
		Greetd g(path);
		Outcome o;
		watch(g, o);
		g.login("alice", "open up", { "zacos9-session" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.failed == "Sorry, that password isn't right.");
		assert(fake.types().last() == "cancel_session");
		o = Outcome();
		g.login("alice", "sesame", { "zacos9-session" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.started);
	}

	/* PAM's own messages are acknowledged with no answer. */
	{
		fake.received.clear();
		fake.extra = { { { "type", "auth_message" }, { "auth_message_type", "info" },
			{ "auth_message", "Last login: yesterday" } } };
		Greetd g(path);
		Outcome o;
		watch(g, o);
		g.login("alice", "sesame", { "zacos9-session" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.started);
		assert(fake.received[1]["response"].isNull());
		assert(fake.received[2]["response"] == "sesame");
		fake.extra.clear();
	}

	/* A second password question (an expired password) isn't guessed at. */
	{
		fake.received.clear();
		fake.secondSecret = true;
		Greetd g(path);
		Outcome o;
		watch(g, o);
		g.login("alice", "sesame", { "zacos9-session" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.failed == "New password:");
		assert(fake.types().count("post_auth_message_response") == 1);
		assert(fake.types().last() == "cancel_session");
	}

	/* greetd's own error text for anything but a wrong password. */
	{
		Greetd g(path);
		Outcome o;
		watch(g, o);
		g.login("bob", "x", { "zacos9-session" });
		assert(waitFor([&] { return o.done(); }));
		assert(o.failed == "pam_start: unknown user");
	}

	puts("greetd client: ok");
	return 0;
}
