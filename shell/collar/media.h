#pragma once

/*
 * What's playing, for The Collar's media module (Mac OS 9's CD Strip, for
 * any player): the programs that speak MPRIS on the session bus (music and
 * video players, browsers), what each plays, and play/pause, next,
 * previous and bringing the player forward.
 */

#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <functional>

class QTimer;

struct MediaPlayer {
	QString service, identity, status; /* status: Playing, Paused or Stopped */
	QString title, artist;
	bool canPlay = false, canPause = false, canNext = false, canPrevious = false, canRaise = false;
	bool playing() const { return status == "Playing"; }
	bool operator==(const MediaPlayer &o) const {
		return service == o.service && identity == o.identity && status == o.status && title == o.title &&
			artist == o.artist && canPlay == o.canPlay && canPause == o.canPause && canNext == o.canNext &&
			canPrevious == o.canPrevious && canRaise == o.canRaise;
	}
};

class MediaPlayers : public QObject {
public:
	explicit MediaPlayers(QObject *parent = nullptr, const QString &prefix = defaultPrefix());
	/* $ZACOS9_MPRIS_PREFIX (for tests), else org.mpris.MediaPlayer2. */
	static QString defaultPrefix();
	QList<MediaPlayer> players; /* by service name */
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	/* The one the module controls: the one chosen from its menu while it
	 * runs, else one playing, else one paused, else the first. */
	const MediaPlayer *current() const;
	void choose(const QString &service);
	void playPause(const QString &service);
	void next(const QString &service);
	void previous(const QString &service);
	void raise(const QString &service);

private:
	QString m_prefix, m_chosen;
	QTimer *m_debounce;
	bool m_refreshing = false, m_again = false;
	void call(const QString &service, const QString &interface, const QString &method, const QVariantList &args,
		std::function<void(const QDBusMessage &)> done);
	void command(const QString &service, const QString &interface, const QString &method, const QString &what);
	void finish(const QList<MediaPlayer> &found);
};
