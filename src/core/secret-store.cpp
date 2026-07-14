#include "core/secret-store.hpp"

#include <QByteArray>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>

#include <string>
#endif

namespace dsk {

namespace {

#ifdef _WIN32
std::wstring toWide(const QString &value)
{
	return value.toStdWString();
}

int maxCredentialBlobSize()
{
#ifdef CRED_MAX_CREDENTIAL_BLOB_SIZE
	return CRED_MAX_CREDENTIAL_BLOB_SIZE;
#else
	return 5 * 512;
#endif
}

QString windowsErrorMessage(DWORD error)
{
	LPWSTR buffer = nullptr;
	const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					    nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	QString message = length > 0 && buffer ? QString::fromWCharArray(buffer, int(length)).trimmed() : QString("Windows error %1").arg(error);
	if (buffer)
		LocalFree(buffer);
	return message;
}
#endif

} // namespace

bool SecretStore::writeSecret(const QString &credentialRef, const QString &secret, QString *errorMessage) const
{
	if (credentialRef.trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = "Credential reference is empty.";
		return false;
	}

#ifdef _WIN32
	const QByteArray blob = secret.toUtf8();
	const int maxBlobSize = maxCredentialBlobSize();
	if (blob.size() > maxBlobSize) {
		if (errorMessage)
			*errorMessage = QString("Credential value is too large for Windows Credential Manager (%1 bytes, max %2).")
						.arg(blob.size())
						.arg(maxBlobSize);
		return false;
	}

	const std::wstring targetName = toWide(credentialRef);
	const std::wstring userName = toWide(QStringLiteral("DSK Multistream"));

	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<LPWSTR>(targetName.c_str());
	credential.UserName = const_cast<LPWSTR>(userName.c_str());
	// LOCAL_MACHINE keeps the generic credential in the current user's local vault without Enterprise roaming.
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
	credential.CredentialBlobSize = DWORD(blob.size());

	if (!CredWriteW(&credential, 0)) {
		if (errorMessage)
			*errorMessage = windowsErrorMessage(GetLastError());
		return false;
	}

	if (errorMessage)
		errorMessage->clear();
	return true;
#else
	if (errorMessage)
		*errorMessage = "OS credential storage is not available on this platform.";
	return false;
#endif
}

bool SecretStore::readSecret(const QString &credentialRef, QString *secret, QString *errorMessage) const
{
	const SecretReadResult result = readSecretResult(credentialRef, secret, errorMessage);
	if (result == SecretReadResult::NotFound && errorMessage)
		*errorMessage = QStringLiteral("Credential was not found.");
	return result == SecretReadResult::Found;
}

SecretReadResult SecretStore::readSecretResult(const QString &credentialRef, QString *secret,
						QString *errorMessage) const
{
	if (secret)
		secret->clear();
	if (credentialRef.trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = "Credential reference is empty.";
		return SecretReadResult::Error;
	}

#ifdef _WIN32
	PCREDENTIALW credential = nullptr;
	const std::wstring targetName = toWide(credentialRef);
	if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
		const DWORD error = GetLastError();
		if (error == ERROR_NOT_FOUND) {
			if (errorMessage)
				errorMessage->clear();
			return SecretReadResult::NotFound;
		}
		if (errorMessage)
			*errorMessage = windowsErrorMessage(error);
		return SecretReadResult::Error;
	}

	if (secret) {
		const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob), int(credential->CredentialBlobSize));
		*secret = QString::fromUtf8(blob);
	}
	CredFree(credential);
	if (errorMessage)
		errorMessage->clear();
	return SecretReadResult::Found;
#else
	if (errorMessage)
		*errorMessage = "OS credential storage is not available on this platform.";
	return SecretReadResult::Error;
#endif
}

bool SecretStore::deleteSecret(const QString &credentialRef, QString *errorMessage) const
{
	if (credentialRef.trimmed().isEmpty()) {
		if (errorMessage)
			errorMessage->clear();
		return true;
	}

#ifdef _WIN32
	const std::wstring targetName = toWide(credentialRef);
	if (!CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0)) {
		const DWORD error = GetLastError();
		if (error == ERROR_NOT_FOUND) {
			if (errorMessage)
				errorMessage->clear();
			return true;
		}
		if (errorMessage)
			*errorMessage = windowsErrorMessage(error);
		return false;
	}
	if (errorMessage)
		errorMessage->clear();
	return true;
#else
	if (errorMessage)
		*errorMessage = "OS credential storage is not available on this platform.";
	return false;
#endif
}

QString SecretStore::streamKeyCredentialRef(const QString &targetId)
{
	return QStringLiteral("DSK Multistream/stream-key/%1").arg(targetId);
}

QString SecretStore::oauthClientSecretCredentialRef(const QString &targetId)
{
	return QStringLiteral("DSK Multistream/oauth-client-secret/%1").arg(targetId);
}

QString SecretStore::oauthRefreshTokenCredentialRef(const QString &targetId)
{
	return QStringLiteral("DSK Multistream/oauth-refresh-token/%1").arg(targetId);
}

bool SecretStore::isOwnedCredentialRef(const QString &credentialRef)
{
	const QString ref = credentialRef.trimmed();
	for (const QString &prefix : {
		     QStringLiteral("DSK Multistream/stream-key/"),
		     QStringLiteral("DSK Multistream/oauth-client-secret/"),
		     QStringLiteral("DSK Multistream/oauth-refresh-token/"),
	     }) {
		if (ref.startsWith(prefix) && ref.size() > prefix.size())
			return true;
	}
	return false;
}

bool SecretStore::isAvailable()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

} // namespace dsk
