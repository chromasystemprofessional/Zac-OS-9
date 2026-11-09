#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

#include "findcriteria.h"

/*
 * A Files search on a worker thread: the walk and the criteria are the
 * Sniffer's Find (findcriteria.h, findindex.h), so the standalone window
 * and the Find dialog share one engine. A contents search first asks the
 * index for candidates, then walks those; results arrive in batches.
 * stop() asks the worker to end as soon as it can.
 */
class BloodhoundSearch : public QObject {
	Q_OBJECT
public:
	explicit BloodhoundSearch(QObject *parent = nullptr);
	~BloodhoundSearch() override;

	void start(const FindQuery &query);
	void stop();
	bool running() const { return m_thread != nullptr || m_pending; }

signals:
	void batch(const QStringList &paths);
	void finished(const QString &problem);

private:
	void startWalk(const FindQuery &query, const QStringList &candidates, bool useCandidates,
		quint64 generation);

	QThread *m_thread = nullptr;
	quint64 m_generation = 0;
	bool m_pending = false;
};
