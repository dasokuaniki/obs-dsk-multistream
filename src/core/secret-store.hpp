#pragma once

#include <QString>

namespace dsk {

enum class SecretReadResult {
	Found,
	NotFound,
	Error,
};

class SecretStore {
public:
	bool writeSecret(const QString &credentialRef, const QString &secret, QString *errorMessage = nullptr) const;
	bool readSecret(const QString &credentialRef, QString *secret, QString *errorMessage = nullptr) const;
	SecretReadResult readSecretResult(const QString &credentialRef, QString *secret,
					QString *errorMessage = nullptr) const;
	bool deleteSecret(const QString &credentialRef, QString *errorMessage = nullptr) const;

	static QString streamKeyCredentialRef(const QString &targetId);
	static QString oauthClientSecretCredentialRef(const QString &targetId);
	static QString oauthRefreshTokenCredentialRef(const QString &targetId);
	static bool isOwnedCredentialRef(const QString &credentialRef);
	static bool isAvailable();
};

} // namespace dsk
