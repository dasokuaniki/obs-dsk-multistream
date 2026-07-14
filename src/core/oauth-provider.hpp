#pragma once

#include "core/output-target.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace dsk {

struct OAuthProvider {
	TargetAuthMode authMode = TargetAuthMode::ManualRtmp;
	QString id;
	QString displayName;
	QUrl authorizeUrl;
	QUrl tokenUrl;
	QStringList scopes;
	bool requiresPkce = true;
	QString publisherRelayProfile;
	QStringList requiredRelayScopes;
};

struct OAuthClientCredentials {
	QString clientId;
	QString clientSecret;

	bool isComplete() const;
	bool hasAny() const;
};

struct KickChannelConnection {
	QString accountName;
	QString serverUrl;
	QString streamKey;
	QString errorMessage;

	bool isComplete() const;
};

OAuthProvider oauthProviderForAuthMode(TargetAuthMode mode);
OAuthClientCredentials oauthBundledClientCredentials(TargetAuthMode mode);
OAuthClientCredentials oauthEffectiveClientCredentials(TargetAuthMode mode, const QString &customClientId,
						 const QString &customClientSecret);
bool oauthHasBundledClientCredentials(TargetAuthMode mode);
bool oauthHasUsableClientCredentials(TargetAuthMode mode, const QString &customClientId,
				     const QString &customClientSecret, const QString &customClientSecretRef);
QUrl oauthAuthorizeUrl(const OAuthProvider &provider, const QString &clientId, const QUrl &redirectUri, const QString &state, const QString &codeChallenge);
bool oauthUsesPublisherRelay(TargetAuthMode mode);
QString oauthPublisherRelayProfile(TargetAuthMode mode);
QUrl oauthPublisherRelayBaseUrl();
QUrl oauthPublisherRelayAuthorizeUrl(const OAuthProvider &provider, const QUrl &redirectUri,
				     const QString &state, const QString &codeChallenge);
QUrl oauthPublisherRelayTokenUrl(const OAuthProvider &provider);
QString oauthValidatePublisherRelayTokenMetadata(const OAuthProvider &provider,
						 const QJsonObject &response,
						 QString *publicClientId = nullptr);
KickChannelConnection oauthParseKickChannelResponse(const QJsonObject &response);
QString oauthSafeErrorDetail(const QByteArray &payload);

} // namespace dsk
