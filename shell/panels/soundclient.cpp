#include "soundclient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include <algorithm>

static bool jsonArray(const QByteArray &json, QJsonArray &array, QString &error) {
	QJsonParseError parse;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
	if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
		error = "The sound server returned invalid output data.";
		return false;
	}
	array = doc.array();
	return true;
}

bool parseSoundOutputs(const QByteArray &json, const QString &defaultSink,
		QVector<SoundOutput> &outputs, QString &error) {
	outputs.clear();
	error.clear();
	QJsonArray sinks;
	if (!jsonArray(json, sinks, error)) {
		return false;
	}
	for (const QJsonValue &value : sinks) {
		const QJsonObject sink = value.toObject();
		const QString name = sink.value("name").toString();
		if (name.isEmpty() || !sink.value("volume").isObject() || !sink.value("mute").isBool()) {
			error = "The sound server returned an incomplete output.";
			outputs.clear();
			return false;
		}
		QString description = sink.value("description").toString();
		if (description.isEmpty()) {
			description = name;
		}
		const QJsonObject properties = sink.value("properties").toObject();
		if (properties.value("device.profile.name").toString().startsWith("hdmi")) {
			const QString monitor = properties.value("alsa.name").toString();
			if (!monitor.isEmpty() && !monitor.startsWith("HDMI")) {
				description = monitor + " - " + description;
			}
		}
		const QJsonObject volumes = sink.value("volume").toObject();
		double total = 0;
		double left = 0, right = 0;
		bool hasLeft = false, hasRight = false;
		for (auto channelIt = volumes.constBegin(); channelIt != volumes.constEnd(); ++channelIt) {
			const QJsonValue &channel = channelIt.value();
			const QJsonValue v = channel.toObject().value("value");
			if (!v.isDouble() || v.toDouble() < 0) {
				error = "The sound server returned an invalid volume.";
				outputs.clear();
				return false;
			}
			const double level = v.toDouble();
			total += level;
			const QString name = channelIt.key();
			if (name == "left" || name == "front-left") {
				left = level;
				hasLeft = true;
			} else if (name == "right" || name == "front-right") {
				right = level;
				hasRight = true;
			}
		}
		if (volumes.isEmpty()) {
			error = "The sound server returned no volume channels.";
			outputs.clear();
			return false;
		}
		const int percent = static_cast<int>(std::clamp(total / volumes.size() * 100 / 65536, 0.0, 100.0) + 0.5);
		const bool stereo = volumes.size() == 2 && hasLeft && hasRight;
		const int balance = stereo && left + right > 0
			? static_cast<int>(right * 100 / (left + right) + 0.5) : 50;
		const QString active = sink.value("active_port").toString();
		const QJsonArray ports = sink.value("ports").toArray();
		if (ports.isEmpty()) {
			outputs.push_back({ name, {}, description, percent, balance,
				sink.value("mute").toBool(), name == defaultSink, stereo });
		} else {
			for (const QJsonValue &p : ports) {
				const QJsonObject port = p.toObject();
				if (port.value("availability").toString() == "not available") {
					continue;
				}
				const QString portName = port.value("name").toString();
				if (portName.isEmpty()) {
					error = "The sound server returned an unnamed output port.";
					outputs.clear();
					return false;
				}
				const QString label = port.value("description").toString(portName);
				outputs.push_back({ name, portName, description + " - " + label,
					percent, balance, sink.value("mute").toBool(),
					name == defaultSink && portName == active, stereo });
			}
		}
	}
	return true;
}

bool parseSoundInputs(const QByteArray &json, const QString &defaultSource,
		QVector<SoundInput> &inputs, QString &error) {
	inputs.clear();
	error.clear();
	QJsonArray sources;
	if (!jsonArray(json, sources, error)) {
		return false;
	}
	for (const QJsonValue &value : sources) {
		const QJsonObject source = value.toObject();
		const QString name = source.value("name").toString();
		if (name.isEmpty()) {
			error = "The sound server returned an unnamed input.";
			inputs.clear();
			return false;
		}
		if (name.endsWith(".monitor") ||
				source.value("properties").toObject().value("device.class").toString() == "monitor") {
			continue;
		}
		QString description = source.value("description").toString();
		if (description.isEmpty()) {
			description = name;
		}
		inputs.push_back({ name, description, name == defaultSource });
	}
	return true;
}

void SoundClient::run(const QStringList &args, Done done) {
	auto *process = new QProcess(this);
	auto *timeout = new QTimer(process);
	timeout->setSingleShot(true);
	connect(timeout, &QTimer::timeout, process, [process] {
		process->setProperty("timedOut", true);
		process->kill();
	});
	connect(process, &QProcess::finished, this,
		[process, timeout, args, done](int code, QProcess::ExitStatus status) {
			timeout->stop();
			const QByteArray out = process->readAllStandardOutput();
			QString error;
			if (process->property("timedOut").toBool()) {
				error = "The sound server did not respond in time.";
			} else if (status != QProcess::NormalExit || code != 0) {
				const QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
				error = "Couldn't " + args.first() + ": " + (detail.isEmpty() ? "sound server command failed." : detail);
			}
			process->deleteLater();
			done(out, error);
		});
	connect(process, &QProcess::errorOccurred, this, [process, timeout, done](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			timeout->stop();
			const QString detail = "Couldn't start pactl: " + process->errorString();
			process->deleteLater();
			done({}, detail);
		}
	});
	process->start("pactl", args);
	timeout->start(3000);
}

void SoundClient::report(const QString &error, bool command) {
	qWarning().noquote() << error;
	if (failed) {
		failed(error);
	}
	if (command && commandFailed) {
		commandFailed(error);
	}
}

void SoundClient::refresh() {
	if (m_refreshing || switching) {
		return;
	}
	m_refreshing = true;
	const unsigned generation = m_generation;
	run({ "get-default-sink" }, [this, generation](const QByteArray &out, const QString &error) {
		if (generation != m_generation) {
			m_refreshing = false;
			refresh();
			return;
		}
		if (!error.isEmpty()) {
			m_refreshing = false;
			if (!switching) {
				outputs.clear();
				report(error);
				if (changed) {
					changed();
				}
				refreshInputs();
			}
			return;
		}
		const QString sink = QString::fromUtf8(out).trimmed();
		run({ "--format=json", "list", "sinks" }, [this, sink, generation](const QByteArray &json, const QString &error) {
			m_refreshing = false;
			if (switching || generation != m_generation) {
				refresh();
				return;
			}
			QString problem = error;
			QVector<SoundOutput> fresh;
			if (problem.isEmpty()) {
				parseSoundOutputs(json, sink, fresh, problem);
			}
			outputs = fresh;
			if (!problem.isEmpty()) {
				report(problem);
			}
			if (changed) {
				changed();
			}
			refreshInputs();
		});
	});
}

void SoundClient::refreshInputs() {
	if (m_inputRefreshing || inputSwitching) {
		return;
	}
	m_inputRefreshing = true;
	const unsigned generation = ++m_inputGeneration;
	run({ "get-default-source" }, [this, generation](const QByteArray &out, const QString &error) {
		if (generation != m_inputGeneration) {
			m_inputRefreshing = false;
			refreshInputs();
			return;
		}
		if (!error.isEmpty()) {
			m_inputRefreshing = false;
			inputs.clear();
			report(error);
			if (changed) {
				changed();
			}
			return;
		}
		const QString source = QString::fromUtf8(out).trimmed();
		run({ "--format=json", "list", "sources" },
			[this, source, generation](const QByteArray &json, const QString &error) {
				m_inputRefreshing = false;
				if (inputSwitching || generation != m_inputGeneration) {
					refreshInputs();
					return;
				}
				QString problem = error;
				QVector<SoundInput> fresh;
				if (problem.isEmpty()) {
					parseSoundInputs(json, source, fresh, problem);
				}
				inputs = fresh;
				if (!problem.isEmpty()) {
					report(problem);
				}
				if (changed) {
					changed();
				}
			});
	});
}

void SoundClient::finishSwitch(const QString &error) {
	switching = false;
	if (!error.isEmpty()) {
		report(error, true);
	}
	if (changed) {
		changed();
	}
	refresh();
}

void SoundClient::movePlayback(const QString &sink, QStringList inputs) {
	if (inputs.isEmpty()) {
		finishSwitch({});
		return;
	}
	const QString input = inputs.takeFirst();
	run({ "move-sink-input", input, sink }, [this, sink, inputs](const QByteArray &, const QString &error) {
		if (!error.isEmpty()) {
			finishSwitch("The default output changed, but current playback couldn't be moved. " + error);
			return;
		}
		movePlayback(sink, inputs);
	});
}

void SoundClient::makeDefault(const QString &sink) {
	run({ "set-default-sink", sink }, [this, sink](const QByteArray &, const QString &error) {
		if (!error.isEmpty()) {
			finishSwitch(error);
			return;
		}
		run({ "--format=json", "list", "sink-inputs" }, [this, sink](const QByteArray &json, const QString &error) {
			QString problem = error;
			QJsonArray array;
			if (problem.isEmpty()) {
				jsonArray(json, array, problem);
			}
			QStringList inputs;
			for (const QJsonValue &value : array) {
				const QJsonObject input = value.toObject();
				const QJsonValue index = input.value("index");
				if (!index.isDouble() || index.toDouble() < 0 || index.toDouble() != index.toInteger(-1)) {
					problem = "The sound server returned an invalid playback stream.";
					break;
				}
				inputs << QString::number(index.toInteger());
			}
			if (!problem.isEmpty()) {
				finishSwitch("The default output changed, but current playback couldn't be read. " + problem);
				return;
			}
			movePlayback(sink, inputs);
		});
	});
}

void SoundClient::selectOutput(const SoundOutput &output) {
	if (switching) {
		report("An output change is already in progress.", true);
		return;
	}
	if (output.sink.isEmpty()) {
		report("No sound output was selected.", true);
		return;
	}
	switching = true;
	++m_generation;
	if (changed) {
		changed();
	}
	if (output.port.isEmpty()) {
		makeDefault(output.sink);
	} else {
		run({ "set-sink-port", output.sink, output.port },
			[this, output](const QByteArray &, const QString &error) {
				if (!error.isEmpty()) {
					finishSwitch(error);
				} else {
					makeDefault(output.sink);
				}
			});
	}
}

void SoundClient::selectInput(const SoundInput &input) {
	if (inputSwitching) {
		report("An input change is already in progress.", true);
		return;
	}
	if (input.source.isEmpty()) {
		report("No sound input was selected.", true);
		return;
	}
	inputSwitching = true;
	++m_inputGeneration;
	if (changed) {
		changed();
	}
	run({ "set-default-source", input.source }, [this](const QByteArray &, const QString &error) {
		inputSwitching = false;
		if (!error.isEmpty()) {
			report(error, true);
		}
		refreshInputs();
	});
}

void SoundClient::setVolume(const SoundOutput &output, int percent) {
	applyVolume(output, percent, true);
}

void SoundClient::applyVolume(const SoundOutput &output, int percent, bool feedback) {
	if (output.sink.isEmpty() || percent < 0 || percent > 100) {
		report("No valid sound output or volume was selected.", true);
		return;
	}
	QStringList args{ "set-sink-volume", output.sink };
	if (output.stereo) {
		const int balance = std::clamp(output.balance, 0, 100);
		const int left = balance <= 50 ? percent : percent * (100 - balance) / balance;
		const int right = balance >= 50 ? percent : percent * balance / (100 - balance);
		args << QString::number(left) + "%" << QString::number(right) + "%";
	} else {
		args << QString::number(percent) + "%";
	}
	const unsigned generation = ++m_volumeGeneration;
	run(args,
		[this, generation, feedback](const QByteArray &, const QString &error) {
			if (!error.isEmpty()) {
				report(error, true);
			} else if (feedback && generation == m_volumeGeneration && volumeApplied) {
				volumeApplied();
			}
			refresh();
		});
}

void SoundClient::setVolume(const QString &sink, int percent) {
	const auto output = std::find_if(outputs.cbegin(), outputs.cend(),
		[&sink](const SoundOutput &candidate) { return candidate.sink == sink; });
	if (output != outputs.cend()) {
		setVolume(*output, percent);
		return;
	}
	SoundOutput fallback;
	fallback.sink = sink;
	setVolume(fallback, percent);
}

void SoundClient::setBalance(const SoundOutput &output, int percent, int balance) {
		if (!output.stereo || balance < 0 || balance > 100) {
			report("Speaker balance is available only for stereo outputs.", true);
			return;
		}
		SoundOutput adjusted = output;
		adjusted.balance = balance;
		applyVolume(adjusted, percent, false);
}

void SoundClient::setMuted(const QString &sink, bool muted) {
	if (sink.isEmpty()) {
		report("No sound output was selected.", true);
		return;
	}
	run({ "set-sink-mute", sink, muted ? "1" : "0" },
		[this](const QByteArray &, const QString &error) {
			if (!error.isEmpty()) {
				report(error, true);
			}
			refresh();
		});
}
