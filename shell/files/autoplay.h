#pragma once

#include <gio/gio.h>

#include <QString>
#include <functional>

#include "mediakind.h"

/*
 * Mac OS 9's AutoPlay: an audio CD or a video DVD just inserted plays in
 * the program that plays that kind of disc. Only physical optical media
 * are considered; a mounted image, a USB stick or a data disc is not.
 * Programs on a disc are never started.
 */

/* The "media-autoplay" setting (on unless "false"). */
bool mediaAutoplayEnabled();

/* The first content type AutoPlay handles from a NULL-terminated list,
 * or empty. */
QString mediaAutoplayType(char **types);

/* Start the asynchronous content-type guess and launch the handler for
 * `mount`, whose medium is `kind`. `missing` runs with the disc's content
 * type when no installed program can play it. Returns false immediately
 * for anything that isn't physical optical media, or when this mount is
 * already being handled. */
bool mediaAutoplayMount(GMount *mount, MediaKind kind,
	const std::function<void(const QString &)> &missing = {});

/* Play an audio CD when the person opens its desktop icon: the default
 * audio-CD handler with the device, independent of the AutoPlay setting. */
bool mediaPlayAudioCd(const QString &device);

/* A mount was removed: allow the same device/disc to autoplay again. */
void mediaAutoplayForget(GMount *mount);
