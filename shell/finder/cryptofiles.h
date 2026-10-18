#pragma once

#include <QString>
#include <functional>

using CryptoFileCompletion = std::function<void(bool, const QString &)>;

void encryptFile(const QString &source, const QString &destination, const QString &password,
	CryptoFileCompletion completion);
void decryptFile(const QString &source, const QString &destination, const QString &password,
	CryptoFileCompletion completion);

/* An encrypted file's passphrase in the keychain (the Secret Service,
 * through secret-tool; $ZACOS9_SECRET_TOOL for tests), found by the
 * file's path: Mac OS 9's "Add to keychain". */
void keychainStorePassphrase(const QString &file, const QString &password, std::function<void(bool)> done);
/* The passphrase, or empty if the keychain has none for it (or is locked
 * and stays so). */
void keychainFindPassphrase(const QString &file, std::function<void(const QString &)> done);
