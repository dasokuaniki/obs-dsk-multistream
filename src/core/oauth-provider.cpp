#include "core/oauth-provider.hpp"

#include "oauth-publisher-config.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

namespace dsk {

namespace {

constexpr auto PublisherRelayUrl = "https://45-77-181-113.sslip.io";

QUrl relayEndpoint(const OAuthProvider &provider, const QString &action)
{
	QUrl url = oauthPublisherRelayBaseUrl();
	url.setPath(QStringLiteral("/v1/%1/%2").arg(provider.id, action));
	url.setQuery(QString());
	url.setFragment(QString());
	return url;
}

} // namespace

bool OAuthClientCredentials::isComplete() const
{
	return !clientId.trimmed().isEmpty() && !clientSecret.trimmed().isEmpty();
}

bool OAuthClientCredentials::hasAny() const
{
	return !clientId.trimmed().isEmpty() || !clientSecret.trimmed().isEmpty();
}

bool KickChannelConnection::isComplete() const
{
	return errorMessage.isEmpty() && !serverUrl.trimmed().isEmpty() && !streamKey.trimmed().isEmpty();
}

OAuthProvider oauthProviderForAuthMode(TargetAuthMode mode)
{
	if (mode == TargetAuthMode::TwitchOAuth) {
		return {
			TargetAuthMode::TwitchOAuth,
			"twitch",
			"Twitch",
			QUrl("https://id.twitch.tv/oauth2/authorize"),
			QUrl("https://id.twitch.tv/oauth2/token"),
			{"channel:read:stream_key"},
			true,
			"multistream",
			{"channel:read:stream_key"},
		};
	}

	if (mode == TargetAuthMode::YouTubeOAuth) {
		return {
			TargetAuthMode::YouTubeOAuth,
			"youtube",
			"YouTube",
			QUrl("https://accounts.google.com/o/oauth2/v2/auth"),
			QUrl("https://oauth2.googleapis.com/token"),
			{"https://www.googleapis.com/auth/youtube.force-ssl"},
			true,
			{},
			{},
		};
	}

	if (mode == TargetAuthMode::KickOAuth) {
		return {
			TargetAuthMode::KickOAuth,
			"kick",
			"Kick",
			QUrl("https://id.kick.com/oauth/authorize"),
			QUrl("https://id.kick.com/oauth/token"),
			{"user:read", "channel:read", "streamkey:read"},
			true,
			"multistream",
			{"user:read", "channel:read", "streamkey:read"},
		};
	}

	return {};
}

OAuthClientCredentials oauthBundledClientCredentials(TargetAuthMode mode)
{
	if (mode != TargetAuthMode::YouTubeOAuth)
		return {};

	return {
		QString::fromLatin1(generated::BundledYouTubeClientId).trimmed(),
		QString::fromLatin1(generated::BundledYouTubeClientSecret).trimmed(),
	};
}

OAuthClientCredentials oauthEffectiveClientCredentials(TargetAuthMode mode, const QString &customClientId,
							 const QString &customClientSecret)
{
	OAuthClientCredentials custom{customClientId.trimmed(), customClientSecret.trimmed()};
	if (custom.hasAny())
		return custom;
	return oauthBundledClientCredentials(mode);
}

bool oauthHasBundledClientCredentials(TargetAuthMode mode)
{
	return oauthBundledClientCredentials(mode).isComplete();
}

bool oauthHasUsableClientCredentials(TargetAuthMode mode, const QString &customClientId,
				     const QString &customClientSecret, const QString &customClientSecretRef)
{
	const OAuthClientCredentials custom{customClientId.trimmed(), customClientSecret.trimmed()};
	const bool hasCustomSecretRef = !customClientSecretRef.trimmed().isEmpty();
	if (custom.hasAny() || hasCustomSecretRef)
		return !custom.clientId.isEmpty() && (!custom.clientSecret.isEmpty() || hasCustomSecretRef);
	return oauthHasBundledClientCredentials(mode);
}

QUrl oauthAuthorizeUrl(const OAuthProvider &provider, const QString &clientId, const QUrl &redirectUri, const QString &state, const QString &codeChallenge)
{
	QUrl url = provider.authorizeUrl;
	QUrlQuery query;
	query.addQueryItem("response_type", "code");
	query.addQueryItem("client_id", clientId);
	query.addQueryItem("redirect_uri", redirectUri.toString());
	query.addQueryItem("scope", provider.scopes.join(' '));
	query.addQueryItem("state", state);
	if (provider.requiresPkce) {
		query.addQueryItem("code_challenge", codeChallenge);
		query.addQueryItem("code_challenge_method", "S256");
	}
	if (provider.authMode == TargetAuthMode::YouTubeOAuth) {
		query.addQueryItem("access_type", "offline");
		query.addQueryItem("prompt", "consent");
	}
	url.setQuery(query);
	return url;
}

bool oauthUsesPublisherRelay(TargetAuthMode mode)
{
	return !oauthProviderForAuthMode(mode).publisherRelayProfile.isEmpty();
}

QString oauthPublisherRelayProfile(TargetAuthMode mode)
{
	return oauthProviderForAuthMode(mode).publisherRelayProfile;
}

QUrl oauthPublisherRelayBaseUrl()
{
	return QUrl(QString::fromLatin1(PublisherRelayUrl));
}

QUrl oauthPublisherRelayAuthorizeUrl(const OAuthProvider &provider, const QUrl &redirectUri,
				     const QString &state, const QString &codeChallenge)
{
	QUrl url = relayEndpoint(provider, QStringLiteral("authorize"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("profile"), oauthPublisherRelayProfile(provider.authMode));
	query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri.toString());
	query.addQueryItem(QStringLiteral("state"), state);
	query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
	query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
	url.setQuery(query);
	return url;
}

QUrl oauthPublisherRelayTokenUrl(const OAuthProvider &provider)
{
	return relayEndpoint(provider, QStringLiteral("token"));
}

QString oauthValidatePublisherRelayTokenMetadata(const OAuthProvider &provider,
						 const QJsonObject &response,
						 QString *publicClientId)
{
	if (publicClientId)
		publicClientId->clear();
	if (provider.publisherRelayProfile.isEmpty())
		return QStringLiteral("This login provider is not configured for the DSK publisher relay.");
	if (response.value(QStringLiteral("profile")).toString() != provider.publisherRelayProfile)
		return QStringLiteral("DSK OAuth relay returned the wrong application profile.");

	const QString clientId = response.value(QStringLiteral("client_id")).toString().trimmed();
	if (clientId.isEmpty())
		return QStringLiteral("DSK OAuth relay did not return its public OAuth Client ID.");

	QSet<QString> returnedScopes;
	const QJsonValue scopeValue = response.value(QStringLiteral("scope"));
	if (scopeValue.isArray()) {
		for (const QJsonValue &scope : scopeValue.toArray()) {
			const QString value = scope.toString().trimmed();
			if (!value.isEmpty())
				returnedScopes.insert(value);
		}
	} else if (scopeValue.isString()) {
		const QStringList values = scopeValue.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (const QString &value : values)
			returnedScopes.insert(value.trimmed());
	}
	QSet<QString> requiredScopes;
	for (const QString &scope : provider.requiredRelayScopes)
		requiredScopes.insert(scope);
	QSet<QString> missingScopes = requiredScopes;
	missingScopes.subtract(returnedScopes);
	if (!missingScopes.isEmpty())
		return QStringLiteral("OAuth login did not grant the required permission. Reconnect and approve the requested permission.");

	if (publicClientId)
		*publicClientId = clientId;
	return {};
}

KickChannelConnection oauthParseKickChannelResponse(const QJsonObject &response)
{
	KickChannelConnection connection;
	const QJsonArray channels = response.value(QStringLiteral("data")).toArray();
	if (channels.isEmpty() || !channels.first().isObject()) {
		connection.errorMessage = QStringLiteral("Kick account lookup returned no channel.");
		return connection;
	}

	const QJsonObject channel = channels.first().toObject();
	connection.accountName = channel.value(QStringLiteral("slug")).toString().trimmed();
	const QJsonObject stream = channel.value(QStringLiteral("stream")).toObject();
	if (stream.isEmpty()) {
		connection.errorMessage = QStringLiteral("Kick did not return this channel's stream configuration.");
		return connection;
	}

	connection.serverUrl = stream.value(QStringLiteral("url")).toString().trimmed();
	connection.streamKey = stream.value(QStringLiteral("key")).toString().trimmed();
	const QUrl server(connection.serverUrl);
	const QString scheme = server.scheme().toLower();
	if (!server.isValid() || (scheme != QStringLiteral("rtmp") && scheme != QStringLiteral("rtmps")) ||
	    server.host().trimmed().isEmpty()) {
		connection.errorMessage = QStringLiteral("Kick returned an invalid streaming server URL.");
		connection.serverUrl.clear();
		connection.streamKey.clear();
		return connection;
	}
	if (connection.streamKey.isEmpty()) {
		connection.errorMessage = QStringLiteral(
			"Kick did not return a stream key. Reconnect and approve the stream-key permission.");
		connection.serverUrl.clear();
		return connection;
	}

	return connection;
}

QString oauthSafeErrorDetail(const QByteArray &payload)
{
	const QByteArray boundedPayload = payload.left(16 * 1024);
	const QJsonDocument document = QJsonDocument::fromJson(boundedPayload);
	QString detail;
	if (document.isObject()) {
		const QJsonObject object = document.object();
		const QJsonValue errorValue = object.value(QStringLiteral("error"));
		if (errorValue.isString())
			detail = errorValue.toString();
		else if (errorValue.isObject()) {
			const QJsonObject errorObject = errorValue.toObject();
			detail = errorObject.value(QStringLiteral("message")).toString();
			const QJsonArray errors = errorObject.value(QStringLiteral("errors")).toArray();
			if (!errors.isEmpty() && errors.first().isObject()) {
				const QString reason = errors.first().toObject().value(QStringLiteral("reason")).toString();
				if (!reason.isEmpty())
					detail += detail.isEmpty() ? reason : QStringLiteral(" (%1)").arg(reason);
			}
		}
		const QString description = object.value(QStringLiteral("error_description")).toString();
		if (!description.isEmpty() && description != detail)
			detail += detail.isEmpty() ? description : QStringLiteral(": %1").arg(description);
		if (detail.isEmpty())
			detail = object.value(QStringLiteral("message")).toString();
	}
	if (detail.isEmpty())
		detail = QString::fromUtf8(boundedPayload);

	detail.replace(QRegularExpression(QStringLiteral("(?i)(access_token|refresh_token|client_secret|stream_key|code)(\\s*[=:]\\s*|\\\"\\s*:\\s*\\\")[^\\s,}&\\\"]+")),
		       QStringLiteral("\\1=[redacted]"));
	detail.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]+")), QStringLiteral(" "));
	detail = detail.trimmed().left(500);
	return detail.isEmpty() ? QStringLiteral("Provider rejected the request.") : detail;
}

} // namespace dsk
