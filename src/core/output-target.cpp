#include "core/output-target.hpp"

#include <QDateTime>
#include <QUrl>
#include <QUuid>

namespace dsk {

QString encoderGroupToString(EncoderGroup group)
{
	switch (group) {
	case EncoderGroup::DskHorizontal:
		return "dsk-horizontal";
	case EncoderGroup::DskVertical:
		return "dsk-vertical";
	}
	return "dsk-horizontal";
}

EncoderGroup encoderGroupFromString(const QString &value)
{
	if (value == "dsk-vertical")
		return EncoderGroup::DskVertical;
	return EncoderGroup::DskHorizontal;
}

QString targetStateToString(TargetState state)
{
	switch (state) {
	case TargetState::Stopped:
		return "Stopped";
	case TargetState::Starting:
		return "Starting";
	case TargetState::Live:
		return "Live";
	case TargetState::Stopping:
		return "Stopping";
	case TargetState::Error:
		return "Error";
	}
	return "Stopped";
}

QString targetAuthModeToString(TargetAuthMode mode)
{
	switch (mode) {
	case TargetAuthMode::ManualRtmp:
		return "manual-rtmp";
	case TargetAuthMode::TwitchOAuth:
		return "twitch-oauth";
	case TargetAuthMode::YouTubeOAuth:
		return "youtube-oauth";
	case TargetAuthMode::KickOAuth:
		return "kick-oauth";
	}
	return "manual-rtmp";
}

TargetAuthMode targetAuthModeFromString(const QString &value)
{
	if (value == "twitch-oauth")
		return TargetAuthMode::TwitchOAuth;
	if (value == "youtube-oauth")
		return TargetAuthMode::YouTubeOAuth;
	if (value == "kick-oauth")
		return TargetAuthMode::KickOAuth;
	return TargetAuthMode::ManualRtmp;
}

QString targetAuthModeDisplayName(TargetAuthMode mode)
{
	switch (mode) {
	case TargetAuthMode::ManualRtmp:
		return "Manual RTMP key";
	case TargetAuthMode::TwitchOAuth:
		return "Login with Twitch";
	case TargetAuthMode::YouTubeOAuth:
		return "Login with YouTube";
	case TargetAuthMode::KickOAuth:
		return "Login with Kick";
	}
	return "Manual RTMP key";
}

QString youtubeBroadcastModeToString(YouTubeBroadcastMode mode)
{
	switch (mode) {
	case YouTubeBroadcastMode::Normal:
		return QStringLiteral("normal");
	case YouTubeBroadcastMode::ArchiveRotation:
		return QStringLiteral("archive-rotation");
	}
	return QStringLiteral("normal");
}

YouTubeBroadcastMode youtubeBroadcastModeFromString(const QString &value)
{
	if (value == QStringLiteral("archive-rotation"))
		return YouTubeBroadcastMode::ArchiveRotation;
	return YouTubeBroadcastMode::Normal;
}

QString youtubeBroadcastModeDisplayName(YouTubeBroadcastMode mode)
{
	switch (mode) {
	case YouTubeBroadcastMode::Normal:
		return QStringLiteral("Normal - keep one YouTube broadcast");
	case YouTubeBroadcastMode::ArchiveRotation:
		return QStringLiteral("Split archives every 11 h 30 min");
	}
	return QStringLiteral("Normal - keep one YouTube broadcast");
}

QString targetSceneModeToString(TargetSceneMode mode)
{
	switch (mode) {
	case TargetSceneMode::FollowObs:
		return "follow-obs";
	case TargetSceneMode::FixedScene:
		return "fixed-scene";
	case TargetSceneMode::LinkedScene:
		return "linked-scene";
	}
	return "follow-obs";
}

TargetSceneMode targetSceneModeFromString(const QString &value)
{
	if (value == "fixed-scene")
		return TargetSceneMode::FixedScene;
	if (value == "linked-scene")
		return TargetSceneMode::LinkedScene;
	return TargetSceneMode::FollowObs;
}

QString targetSceneModeDisplayName(TargetSceneMode mode)
{
	switch (mode) {
	case TargetSceneMode::FollowObs:
		return "Follow OBS Program";
	case TargetSceneMode::FixedScene:
		return "Fixed OBS Scene";
	case TargetSceneMode::LinkedScene:
		return "Linked OBS Scene";
	}
	return "Follow OBS Program";
}

bool platformSupportsAuthMode(const QString &platformId, TargetAuthMode mode)
{
	if (mode == TargetAuthMode::ManualRtmp)
		return true;
	if (mode == TargetAuthMode::TwitchOAuth)
		return platformId == "twitch";
	if (mode == TargetAuthMode::YouTubeOAuth)
		return platformId == "youtube";
	if (mode == TargetAuthMode::KickOAuth)
		return platformId == "kick";
	return false;
}

bool migratePublisherManagedOAuthCredentials(OutputTarget &target)
{
	if (target.authMode != TargetAuthMode::TwitchOAuth && target.authMode != TargetAuthMode::KickOAuth)
		return false;

	const bool changed = !target.oauthClientId.isEmpty() || !target.oauthClientSecret.isEmpty() ||
			     !target.oauthClientSecretRef.isEmpty() || !target.oauthRefreshToken.isEmpty() ||
			     !target.oauthRefreshTokenRef.isEmpty();
	target.oauthClientId.clear();
	target.oauthClientSecret.clear();
	target.oauthClientSecretRef.clear();
	target.oauthRefreshToken.clear();
	target.oauthRefreshTokenRef.clear();
	return changed;
}

QString maskedKey(const QString &streamKey)
{
	if (streamKey.isEmpty())
		return {};
	return QStringLiteral("********");
}

QString newTargetId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool validateOutputTargetConfig(const OutputTarget &target, QString *errorMessage, bool requireEnabled)
{
	if (requireEnabled && !target.enabled) {
		if (errorMessage)
			*errorMessage = "Target is disabled.";
		return false;
	}

	if (!platformSupportsAuthMode(target.platformId, target.authMode)) {
		if (errorMessage)
			*errorMessage = "Login mode does not match the selected platform.";
		return false;
	}

	if (target.youtubeBroadcastMode == YouTubeBroadcastMode::ArchiveRotation &&
	    (target.platformId != QStringLiteral("youtube") || target.authMode != TargetAuthMode::YouTubeOAuth)) {
		if (errorMessage)
			*errorMessage = "YouTube archive splitting requires Login with YouTube.";
		return false;
	}

	const QString serverUrl = target.serverUrl.trimmed();
	if (serverUrl.isEmpty()) {
		if (errorMessage)
			*errorMessage = "Server URL is empty.";
		return false;
	}

	const QUrl url(serverUrl);
	const QString scheme = url.scheme().toLower();
	if (!url.isValid() || (scheme != "rtmp" && scheme != "rtmps")) {
		if (errorMessage)
			*errorMessage = "Server URL must start with rtmp:// or rtmps://.";
		return false;
	}
	if (url.host().trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = "Server URL must include a host name.";
		return false;
	}
	if (target.platformId == QStringLiteral("tiktok") &&
	    url.host().compare(QStringLiteral("push.tiktokcdn.com"), Qt::CaseInsensitive) == 0 &&
	    url.path().compare(QStringLiteral("/live"), Qt::CaseInsensitive) == 0) {
		if (errorMessage)
			*errorMessage = "The legacy generic RTMP URL cannot be used. Paste the server URL issued for this stream.";
		return false;
	}

	const bool resolvesStreamKeyAtStart =
		target.platformId == QStringLiteral("youtube") && target.authMode == TargetAuthMode::YouTubeOAuth;
	if (!resolvesStreamKeyAtStart && target.streamKey.trimmed().isEmpty() &&
	    target.authCredentialRef.trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = "Stream key is empty.";
		return false;
	}

	if (target.reconnectEnabled &&
	    (target.reconnectMaxRetries < 0 || target.reconnectMaxRetries > 1000 ||
	     target.reconnectDelaySeconds <= 0 || target.reconnectDelaySeconds > 3600)) {
		if (errorMessage)
			*errorMessage = "Reconnect settings are invalid.";
		return false;
	}

	if (target.videoBitrateKbps < 0 || target.videoBitrateKbps > 200000 ||
	    target.audioBitrateKbps < 0 || target.audioBitrateKbps > 2048 ||
	    target.keyframeSeconds < 0 || target.keyframeSeconds > 20) {
		if (errorMessage)
			*errorMessage = "Encoder settings are outside the supported range.";
		return false;
	}

	if (target.encoderGroup == EncoderGroup::DskHorizontal && target.sceneMode == TargetSceneMode::FixedScene &&
	    target.sceneName.trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = "Fixed scene mode needs an OBS scene.";
		return false;
	}

	if (errorMessage)
		errorMessage->clear();
	return true;
}

QVector<QString> startAllTargetIds(const QVector<OutputTarget> &targets)
{
	QVector<QString> ids;
	for (const auto &target : targets) {
		if (target.enabled && target.startWithAll)
			ids.push_back(target.id);
	}
	return ids;
}

} // namespace dsk
