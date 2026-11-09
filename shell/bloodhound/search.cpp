#include "search.h"

#include "findindex.h"

#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

static constexpr int MAX_RESULTS = 5000;

/* Not worth searching: kernel and device trees, other volumes. */
static bool skipDir(const QString &path) {
	static const QStringList skip = {
		"/proc", "/sys", "/dev", "/run", "/mnt", "/media", "/tmp", "/lost+found",
	};
	return skip.contains(path);
}

BloodhoundSearch::BloodhoundSearch(QObject *parent) : QObject(parent) {}

BloodhoundSearch::~BloodhoundSearch() {
	stop();
}

void BloodhoundSearch::start(const FindQuery &query) {
	stop();
	const quint64 generation = ++m_generation;
	if (query.hasContents()) {
		/* The index answers on the GUI thread; the walk then runs in the
		 * worker, sifting its candidates with the other rows. */
		m_pending = true;
		const QStringList words = query.contentWords();
		indexSearch(words, query.root(),
			[this, query, generation](const QStringList &paths, const QString &error) {
			if (generation != m_generation) {
				return;
			}
			m_pending = false;
			if (!error.isEmpty()) {
				emit finished(error);
				return;
			}
			startWalk(query, paths, true, generation);
		});
		return;
	}
	startWalk(query, {}, false, generation);
}

void BloodhoundSearch::startWalk(const FindQuery &query, const QStringList &candidates,
		bool useCandidates, quint64 generation) {
	m_thread = QThread::create([this, query, candidates, useCandidates, generation] {
		QStringList pending;
		int found = 0;
		QElapsedTimer sent;
		sent.start();
		const QDate today = QDate::currentDate();
		auto flush = [&] {
			if (pending.isEmpty()) {
				return;
			}
			const QStringList paths = pending;
			pending.clear();
			QMetaObject::invokeMethod(this, [this, paths, generation] {
				if (generation == m_generation) {
					emit this->batch(paths);
				}
			}, Qt::QueuedConnection);
			sent.restart();
		};
		auto consider = [&](const QFileInfo &info) {
			if (found < MAX_RESULTS && findMatches(query, info, today)) {
				pending << info.absoluteFilePath();
				found++;
			}
			if (pending.size() >= 200 || sent.elapsed() > 250) {
				flush();
			}
		};
		if (useCandidates) {
			for (const QString &path : candidates) {
				if (QThread::currentThread()->isInterruptionRequested()) {
					break;
				}
				const QFileInfo info(path);
				if (info.exists()) {
					consider(info);
				}
			}
		} else {
			QStringList stack = { query.root() };
			while (!stack.isEmpty() && found < MAX_RESULTS &&
					!QThread::currentThread()->isInterruptionRequested()) {
				const QString dir = stack.takeLast();
				QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
				if (query.hasVisibilityCriterion()) {
					filters |= QDir::Hidden;
				}
				const auto entries = QDir(dir).entryInfoList(
					filters, QDir::Name | QDir::IgnoreCase | QDir::Reversed);
				for (const QFileInfo &info : entries) {
					if (QThread::currentThread()->isInterruptionRequested()) {
						break;
					}
					const QString path = info.absoluteFilePath();
					if (info.isDir() && !info.isSymLink() && !skipDir(path)) {
						stack.push_back(path);
					}
					consider(info);
				}
			}
		}
		flush();
		QMetaObject::invokeMethod(this, [this, generation] {
			if (generation != m_generation) {
				return;
			}
			if (m_thread) {
				m_thread->wait();
				m_thread->deleteLater();
				m_thread = nullptr;
			}
			emit finished(QString());
		}, Qt::QueuedConnection);
	});
	m_thread->start();
}

void BloodhoundSearch::stop() {
	++m_generation;
	m_pending = false;
	if (m_thread) {
		m_thread->requestInterruption();
		m_thread->wait();
		m_thread->deleteLater();
		m_thread = nullptr;
	}
}
