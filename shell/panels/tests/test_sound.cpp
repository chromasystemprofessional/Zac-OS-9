#include "soundclient.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static bool until(const std::function<bool()> &done, int timeout = 5000) {
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return done();
}

static const QByteArray sinks = R"JSON([
 {"name":"built-in","description":"Built-in Audio","mute":false,
  "volume":{"left":{"value":26214},"right":{"value":26214}},
  "active_port":"speakers","ports":[
   {"name":"speakers","description":"Speakers","availability":"availability unknown"},
   {"name":"headphones","description":"Headphones","availability":"not available"}]},
 {"name":"monitor","description":"Digital Stereo (HDMI)","mute":true,
  "volume":{"left":{"value":32768},"right":{"value":32768}},
  "properties":{"device.profile.name":"hdmi-stereo","alsa.name":"SONY TV"},
  "active_port":"hdmi-0","ports":[
   {"name":"hdmi-0","description":"HDMI / DisplayPort","availability":"available"}]},
 {"name":"bluetooth","description":"Wireless Speakers","mute":false,
  "volume":{"mono":{"value":65536}},"ports":[]}
])JSON";

static bool save(const QString &path, const QJsonObject &state) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(QJsonDocument(state).toJson()) > 0;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	if (app.arguments().contains("--live")) {
		SoundClient client;
		bool done = false;
		QString error;
		client.changed = [&] { done = true; };
		client.failed = [&](const QString &message) { error = message; };
		client.refresh();
		check(until([&] { return done; }) && error.isEmpty(), "read live sound outputs: " + error);
		for (const SoundOutput &o : client.outputs) {
			QTextStream(stdout) << (o.current ? "* " : "  ") << o.label << " (" << o.volume << "%)\n";
		}
		return fails ? 1 : 0;
	}
	QVector<SoundOutput> outputs;
	QString error;
	check(parseSoundOutputs(sinks, "built-in", outputs, error), "parse built-in, monitor and Bluetooth outputs");
	check(outputs.size() == 3, "exclude disconnected headphones, keep unknown availability and portless outputs");
	check(outputs.size() == 3 && outputs[0].current && outputs[0].volume == 40 && !outputs[0].muted,
		"read default built-in output, average volume and mute");
	check(outputs.size() == 3 && outputs[1].label.contains("SONY TV") && outputs[1].muted,
		"identify monitor by its name");
	check(parseSoundOutputs(sinks, "monitor", outputs, error) && outputs[1].current && !outputs[0].current,
		"select the monitor's active port when it is default");
	check(!parseSoundOutputs("not json", "built-in", outputs, error) && outputs.isEmpty() && !error.isEmpty(),
		"report malformed server data");
	check(!parseSoundOutputs(R"([{"name":"broken"}])", "broken", outputs, error) && !error.isEmpty(),
		"report incomplete output data");
	check(parseSoundOutputs("[]", "", outputs, error) && outputs.isEmpty(), "handle a server with no outputs");

	QTemporaryDir dir;
	check(dir.isValid(), "create isolated fake sound server");
	if (!dir.isValid() || app.arguments().size() < 2) {
		return 1;
	}
	const QString executable = dir.path() + "/pactl";
	check(QFile::copy(app.arguments()[1], executable)
		&& QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
		"install test pactl fixture");
	const QByteArray oldPath = qgetenv("PATH");
	qputenv("PATH", dir.path().toUtf8());
	const QString path = dir.path() + "/state.json";
	qputenv("SOUND_TEST_STATE", path.toUtf8());
	QJsonObject state{
		{ "default", "built-in" },
		{ "sinks", QJsonDocument::fromJson(sinks).array() },
		{ "inputs", QJsonArray{ QJsonObject{ { "index", 12 } }, QJsonObject{ { "index", 34 } } } }
	};
	check(save(path, state), "write sound server fixture state");
	SoundClient client;
	int updates = 0;
	int volumeFeedback = 0;
	client.changed = [&] { updates++; };
	client.volumeApplied = [&] { volumeFeedback++; };
	client.failed = [&](const QString &message) { error = message; };
	error.clear();
	client.refresh();
	check(until([&] { return updates > 0; }) && error.isEmpty() && client.outputs.size() == 3,
		"discover outputs asynchronously");
	if (client.outputs.size() != 3) {
		return 1;
	}
	updates = 0;
	client.selectOutput(client.outputs[1]);
	check(until([&] { return !client.switching && updates >= 3; }) && error.isEmpty()
		&& client.outputs[1].current && client.outputs[1].muted,
		"choose HDMI output, refresh its volume and mute");
	QFile file(path);
	check(file.open(QIODevice::ReadOnly), "read updated server state");
	QJsonObject updated = QJsonDocument::fromJson(file.readAll()).object();
	file.close();
	const QJsonArray streams = updated.value("inputs").toArray();
	check(updated.value("default").toString() == "monitor" && streams.size() == 2
		&& streams[0].toObject().value("target").toString() == "monitor"
		&& streams[1].toObject().value("target").toString() == "monitor",
		"switch default output and move all existing playback streams");

	updates = 0;
	client.setVolume("monitor", 71);
	check(until([&] { return updates > 0; }) && client.outputs[1].volume == 71,
		"set volume on selected monitor");
	check(volumeFeedback == 1, "offer sound feedback after successful volume adjustment");
	error.clear();
	qputenv("SOUND_TEST_FAIL", "set-sink-volume");
	client.setVolume("monitor", 50);
	check(until([&] { return !error.isEmpty(); }) && volumeFeedback == 1,
		"failed volume adjustments do not offer success feedback");
	updates = 0;
	check(until([&] { return updates > 0; }), "refresh actual volume after failed adjustment");
	qunsetenv("SOUND_TEST_FAIL");
	error.clear();
	updates = 0;
	client.setMuted("monitor", false);
	check(until([&] { return updates > 0; }) && !client.outputs[1].muted,
		"unmute selected monitor");

	state["default"] = "built-in";
	QJsonArray remaining = state.value("sinks").toArray();
	remaining.removeAt(1);
	state["sinks"] = remaining;
	check(save(path, state), "disconnect monitor in fixture");
	updates = 0;
	client.refresh();
	check(until([&] { return updates > 0; }) && client.outputs.size() == 2 && client.outputs[0].current,
		"hot-unplug removes monitor and follows server fallback to built-in speakers");
	remaining = QJsonDocument::fromJson(sinks).array();
	QJsonObject builtin = remaining[0].toObject();
	QJsonArray ports = builtin.value("ports").toArray();
	QJsonObject headphones = ports[1].toObject();
	headphones["availability"] = "available";
	ports[1] = headphones;
	builtin["ports"] = ports;
	remaining[0] = builtin;
	state["sinks"] = remaining;
	check(save(path, state), "reconnect monitor and plug in headphones");
	updates = 0;
	client.refresh();
	check(until([&] { return updates > 0; }) && client.outputs.size() == 4,
		"hot-plug updates available output ports");
	updates = 0;
	client.selectOutput(client.outputs[1]);
	check(until([&] { return !client.switching && updates >= 3; }) && client.outputs[1].current,
		"switch between speaker and headphone ports on the same sink");

	error.clear();
	qputenv("SOUND_TEST_FAIL", "set-default-sink");
	client.selectOutput(client.outputs[2]);
	check(until([&] { return !error.isEmpty(); }) && !client.switching
		&& error.contains("simulated sound server failure"), "report rejected output changes");
	// Finish the automatic refresh before changing the fixture's failure mode.
	updates = 0;
	check(until([&] { return updates > 0; }), "refresh actual output after rejected change");
	qunsetenv("SOUND_TEST_FAIL");
	error.clear();
	qputenv("SOUND_TEST_FAIL", "move-sink-input");
	client.selectOutput(client.outputs[2]);
	check(until([&] { return !error.isEmpty(); }) && error.contains("default output changed"),
		"report partial failure instead of claiming current playback moved");
	updates = 0;
	check(until([&] { return updates > 0; }), "refresh actual default after partial failure");
	qunsetenv("SOUND_TEST_FAIL");
	error.clear();
	qputenv("SOUND_TEST_TIMEOUT", "1");
	client.refresh();
	check(until([&] { return !error.isEmpty(); }) && error.contains("in time"),
		"timeout an unresponsive audio server without blocking the event loop");
	qunsetenv("SOUND_TEST_TIMEOUT");
	error.clear();
	qputenv("PATH", (dir.path() + "/missing").toUtf8());
	client.refresh();
	check(until([&] { return !error.isEmpty(); }) && error.contains("start pactl"),
		"report missing pactl explicitly");
	qputenv("PATH", oldPath);
	qunsetenv("SOUND_TEST_STATE");
	return fails ? 1 : 0;
}
