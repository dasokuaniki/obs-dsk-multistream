#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <optional>

namespace dsk {

inline QUrl commentViewerObsIntegrationUrlForBase(const QUrl &baseUrl)
{
	return baseUrl.resolved(QUrl(QStringLiteral("/api/integrations/obs/v1")));
}

struct CommentViewerObsIntegration {
	QString appVersion;
	QUrl viewerUrl;
	bool canSendComments = false;
};

struct CommentViewerInstallation {
	QString directory;
	QUrl baseUrl;
	bool twitchFocused = false;
};

struct CommentViewerYouTubeBroadcastSelection {
	QString broadcastId;
};

inline bool isCommentViewerYouTubeBroadcastId(const QString &value)
{
	const QString broadcastId = value.trimmed();
	if (broadcastId.size() != 11)
		return false;
	for (const QChar character : broadcastId) {
		const ushort code = character.unicode();
		const bool asciiAlphaNumeric = (code >= 'A' && code <= 'Z') ||
					       (code >= 'a' && code <= 'z') ||
					       (code >= '0' && code <= '9');
		if (!asciiAlphaNumeric && character != QLatin1Char('_') && character != QLatin1Char('-'))
			return false;
	}
	return true;
}

inline QUrl commentViewerYouTubeRecheckUrl()
{
	return QUrl(QStringLiteral("http://127.0.0.1:17321/api/youtube/recheck"));
}

inline QUrl commentViewerYouTubeLiveStartUrl()
{
	return QUrl(QStringLiteral("http://127.0.0.1:17321/api/integrations/obs/v2/youtube-live-start"));
}

inline QUrl commentViewerYouTubeBroadcastSelectionUrl()
{
	return QUrl(QStringLiteral("http://127.0.0.1:17321/api/integrations/obs/v2/youtube-broadcast-selection"));
}

inline QByteArray commentViewerYouTubeLiveStartPayload(const QString &sessionId,
						       const QString &broadcastId)
{
	const QString normalizedSessionId = sessionId.trimmed();
	const QString normalizedBroadcastId = broadcastId.trimmed();
	if (normalizedSessionId.isEmpty() || normalizedSessionId.size() > 120 ||
	    !isCommentViewerYouTubeBroadcastId(normalizedBroadcastId))
		return {};
	for (const QChar character : normalizedSessionId) {
		if (!character.isLetterOrNumber() && character != QLatin1Char(':') &&
		    character != QLatin1Char('_') && character != QLatin1Char('-')) {
			return {};
		}
	}
	return QJsonDocument(QJsonObject{{QStringLiteral("sessionId"), normalizedSessionId},
					 {QStringLiteral("broadcastId"), normalizedBroadcastId}})
		.toJson(QJsonDocument::Compact);
}

inline std::optional<CommentViewerYouTubeBroadcastSelection>
parseCommentViewerYouTubeBroadcastSelection(const QByteArray &payload)
{
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return std::nullopt;
	const QJsonObject root = document.object();
	const QString broadcastId = root.value(QStringLiteral("broadcastId")).toString().trimmed();
	if (!root.value(QStringLiteral("ok")).toBool() || !isCommentViewerYouTubeBroadcastId(broadcastId))
		return std::nullopt;
	return CommentViewerYouTubeBroadcastSelection{broadcastId};
}

inline bool isCommentViewerInstallDirectory(const QString &directory)
{
	const QDir root(directory);
	if (directory.trimmed().isEmpty() || !root.exists())
		return false;

	for (const QString &required : {QStringLiteral("start-hidden.vbs"),
					QStringLiteral("start-server-hidden.vbs")}) {
		const QFileInfo file(root.filePath(required));
		if (!file.isFile() || !file.isReadable())
			return false;
	}

	QFile metadata(root.filePath(QStringLiteral("package.json")));
	if (!metadata.open(QIODevice::ReadOnly) || metadata.size() <= 0 || metadata.size() > 64 * 1024)
		return false;
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return false;

	const QJsonObject package = document.object();
	const QString version = package.value(QStringLiteral("version")).toString().trimmed();
	return package.value(QStringLiteral("name")).toString() == QStringLiteral("dsk-comment-viewer") &&
	       !version.isEmpty() && version.size() <= 128;
}

inline std::optional<bool> isTwitchFocusedCommentViewerDirectory(const QString &directory)
{
	QFile profile(QDir(directory).filePath(QStringLiteral("runtime-profile.json")));
	if (!profile.exists())
		return false;
	if (!profile.open(QIODevice::ReadOnly) || profile.size() <= 0 || profile.size() > 16 * 1024)
		return std::nullopt;

	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(profile.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return std::nullopt;

	const QJsonObject root = document.object();
	if (root.value(QStringLiteral("schemaVersion")).toInt() != 1)
		return std::nullopt;
	const QString edition = root.value(QStringLiteral("edition")).toString();
	const QString instance = root.value(QStringLiteral("instance")).toString();
	if (instance == QStringLiteral("twitch-focused")) {
		if (edition != QStringLiteral("public") ||
		    root.value(QStringLiteral("port")).toInt() != 17325) {
			return std::nullopt;
		}
		return true;
	}
	const bool standardProfile =
		(edition == QStringLiteral("public") && instance == QStringLiteral("public")) ||
		(edition == QStringLiteral("personal") && instance == QStringLiteral("personal"));
	if (!standardProfile || root.value(QStringLiteral("port")).toInt() != 17321)
		return std::nullopt;
	return false;
}

inline std::optional<CommentViewerInstallation>
commentViewerInstallationAt(const QString &directory, bool allowLegacyStandard)
{
	if (!isCommentViewerInstallDirectory(directory))
		return std::nullopt;
	const auto twitchFocused = isTwitchFocusedCommentViewerDirectory(directory);
	if (!twitchFocused.has_value())
		return std::nullopt;
	if (!allowLegacyStandard && !*twitchFocused)
		return std::nullopt;
	return CommentViewerInstallation{
		directory,
		QUrl(*twitchFocused ? QStringLiteral("http://127.0.0.1:17325")
				      : QStringLiteral("http://127.0.0.1:17321")),
		*twitchFocused,
	};
}

inline std::optional<CommentViewerInstallation>
detectCommentViewerInstallation(const QString &configuredDirectory, const QString &localAppData)
{
	if (!configuredDirectory.trimmed().isEmpty()) {
		const auto configured = commentViewerInstallationAt(configuredDirectory.trimmed(), true);
		if (configured)
			return configured;
	}
	if (localAppData.trimmed().isEmpty())
		return std::nullopt;

	const QDir root(localAppData.trimmed());
	const auto standard =
		commentViewerInstallationAt(root.filePath(QStringLiteral("DSKCommentViewer")), true);
	if (standard)
		return standard;
	return commentViewerInstallationAt(root.filePath(QStringLiteral("DSKTwitchCommentViewer")), false);
}

inline bool isSupportedCommentViewerBaseUrl(const QUrl &url)
{
	return url.isValid() && url.scheme() == QStringLiteral("http") &&
	       url.host() == QStringLiteral("127.0.0.1") &&
	       (url.port() == 17321 || url.port() == 17325) &&
	       (url.path().isEmpty() || url.path() == QStringLiteral("/")) && !url.hasQuery() &&
	       url.fragment().isEmpty() && url.userInfo().isEmpty();
}

inline QUrl commentViewerBrowserDockUrl(const QUrl &viewerUrl)
{
	QUrl baseUrl = viewerUrl;
	baseUrl.setPath({});
	baseUrl.setQuery({});
	baseUrl.setFragment({});
	if (!isSupportedCommentViewerBaseUrl(baseUrl) ||
	    viewerUrl != baseUrl.resolved(QUrl(QStringLiteral("/viewer?dock=chat&send=1")))) {
		return {};
	}
	QUrl browserUrl = viewerUrl;
	browserUrl.setHost(QStringLiteral("localhost"));
	return browserUrl;
}

inline std::optional<CommentViewerObsIntegration>
parseCommentViewerObsIntegration(
	const QByteArray &payload,
	const QUrl &expectedBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:17321")))
{
	if (!isSupportedCommentViewerBaseUrl(expectedBaseUrl))
		return std::nullopt;
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return std::nullopt;

	const QJsonObject root = document.object();
	const QString appVersion = root.value(QStringLiteral("appVersion")).toString().trimmed();
	if (!root.value(QStringLiteral("ok")).toBool() ||
	    root.value(QStringLiteral("service")).toString() != QStringLiteral("dsk-comment-viewer") ||
	    root.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
	    root.value(QStringLiteral("integration")).toString() != QStringLiteral("obs-browser-dock") ||
	    appVersion.isEmpty() || appVersion.size() > 128) {
		return std::nullopt;
	}

	const QJsonObject dock = root.value(QStringLiteral("obsDock")).toObject();
	const QString viewerPath = dock.value(QStringLiteral("viewerPath")).toString();
	if (viewerPath != QStringLiteral("/viewer?dock=chat&send=1"))
		return std::nullopt;

	QSet<QString> capabilities;
	for (const QJsonValue &value : dock.value(QStringLiteral("capabilities")).toArray()) {
		if (!value.isString())
			return std::nullopt;
		capabilities.insert(value.toString());
	}
	if (!capabilities.contains(QStringLiteral("comments.read")))
		return std::nullopt;

	CommentViewerObsIntegration integration;
	integration.appVersion = appVersion;
	integration.viewerUrl = expectedBaseUrl.resolved(QUrl(viewerPath));
	integration.canSendComments = capabilities.contains(QStringLiteral("comments.send"));
	return integration;
}

} // namespace dsk
