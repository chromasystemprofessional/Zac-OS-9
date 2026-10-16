#pragma once

#include <QString>
#include <functional>

using CryptoFileCompletion = std::function<void(bool, const QString &)>;

void encryptFile(const QString &source, const QString &destination, const QString &password,
	CryptoFileCompletion completion);
void decryptFile(const QString &source, const QString &destination, const QString &password,
	CryptoFileCompletion completion);
