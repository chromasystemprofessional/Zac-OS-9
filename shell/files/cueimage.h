#pragma once

#include <QString>

/*
 * A CUE sheet's image. The sheet names its track file and the tracks in it;
 * ZacOS can mount a single data track by converting it to an ISO with
 * bchunk. Audio and multi-track sheets are recognized but not mountable.
 */
struct CueImage {
	bool hasTracks = false;
	bool data = false;
	bool audio = false;
	int tracks = 0;
	QString binPath;
	QString error;
};

CueImage cueImageInfo(const QString &path);
