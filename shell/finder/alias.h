#pragma once

#include <QString>

/* Persistent Macintosh-style aliases. An alias stays an ordinary symbolic
 * link (so existing links keep working); next to it, hidden in
 * "<folder>/.alias/<name>", is a small record of the target's identity
 * (filesystem ID, device, inode, name, parent folder). When the link's path stops
 * resolving, the identity lets Finder find a renamed or moved target by
 * looking only in a few nearby folders on the same disk. Nothing is ever
 * created to stand in for a missing target. */
enum class AliasState {
	NotAlias,
	Ok,
	Reconnected, /* found again by identity; link and record updated */
	Changed,     /* path exists but is a different item; needs confirmation */
	Missing,
	Loop,
};

struct AliasResolution {
	AliasState state = AliasState::NotAlias;
	QString target; /* Ok/Reconnected/Changed: the item to use */
	QString error;  /* a repair or identity-record write that could not complete */
};

/* Records the identity of `target` for the link at `aliasPath`. */
bool aliasRecord(const QString &aliasPath, const QString &target);
/* Resolves; with `reconnect`, repairs the link when the identity is found. */
AliasResolution aliasResolve(const QString &aliasPath, bool reconnect = true);
/* User-assisted: points the alias at an item the user chose (or confirmed).
 * Refuses a missing item or one that would make a loop. */
bool aliasReconnect(const QString &aliasPath, const QString &newTarget, QString *error = nullptr);
/* Keeps records with their links across move, copy, rename and removal. */
bool aliasMoveRecord(const QString &from, const QString &to, bool copy);
bool aliasRemoveRecord(const QString &aliasPath);
/* A sentence for the user describing a failed state. */
QString aliasProblemText(AliasState state);
