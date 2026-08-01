#pragma once

#include "core/output-target.hpp"

namespace dsk {

inline bool isYouTubeTarget(const OutputTarget &target)
{
	return target.platformId.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) == 0;
}

inline bool isYouTubeApiWarningText(const QString &message)
{
	return message.startsWith(QStringLiteral("YouTube API start "))
	       || message.startsWith(QStringLiteral("YouTube token refresh "))
	       || message.startsWith(QStringLiteral("YouTube broadcast lookup "))
	       || message.startsWith(QStringLiteral("YouTube stream status lookup "))
	       || message.startsWith(QStringLiteral("YouTube broadcast start "))
	       || message.startsWith(QStringLiteral("YouTube broadcast testing "))
	       || message.startsWith(QStringLiteral("YouTube broadcast live "))
	       || message.startsWith(QStringLiteral("YouTube archive rotation "));
}

inline bool targetHasYouTubeApiWarning(const OutputTarget &target)
{
	return isYouTubeTarget(target) && isYouTubeApiWarningText(target.lastError.trimmed());
}

inline bool targetHasLiveYouTubeApiWarning(const OutputTarget &target)
{
	return target.state == TargetState::Live && targetHasYouTubeApiWarning(target);
}

inline bool targetHasExpiredYouTubeLoginWarning(const OutputTarget &target)
{
	const QString error = target.lastError.trimmed();
	return targetHasYouTubeApiWarning(target) && error.startsWith(QStringLiteral("YouTube token refresh failed")) &&
	       error.contains(QStringLiteral("invalid_grant"), Qt::CaseInsensitive);
}

inline QString userFacingYouTubeApiWarningText(const QString &message)
{
	if (message.startsWith(QStringLiteral("YouTube archive rotation failed")))
		return QStringLiteral("The current YouTube broadcast remains live. DSK could not prepare the next archive and will retry automatically.");
	if (message.startsWith(QStringLiteral("YouTube token refresh failed"))) {
		if (message.contains(QStringLiteral("invalid_grant"), Qt::CaseInsensitive))
			return QStringLiteral("YouTube is receiving RTMP but remains in preparation. Reconnect YouTube login or press Go Live in YouTube Studio.");
		return QStringLiteral("YouTube RTMP is connected. YouTube login could not be refreshed.");
	}
	if (message.contains(QStringLiteral("quota"), Qt::CaseInsensitive))
		return QStringLiteral("YouTube RTMP is connected. YouTube API quota is exhausted, so use YouTube Studio auto-start/manual start.");
	if (message.startsWith(QStringLiteral("YouTube API start failed")))
		return QStringLiteral("YouTube RTMP is connected. DSK could not confirm the YouTube broadcast became live.");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup blocked")))
		return QStringLiteral("YouTube RTMP is connected. Too many scheduled broadcasts were returned; remove old scheduled broadcasts and retry.");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no broadcasts")))
		return QStringLiteral("YouTube RTMP is connected. No active YouTube broadcast was found for API start.");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no bound stream")))
		return QStringLiteral("YouTube RTMP is connected. No bound YouTube stream was found for API start.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: no active broadcast matched")))
		return QStringLiteral("YouTube RTMP is connected. The active YouTube broadcast did not match this stream key.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: selected broadcast")))
		return QStringLiteral("YouTube RTMP is connected. The selected broadcast is no longer available; choose another in DSK Streaming.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts")))
		return QStringLiteral("YouTube RTMP is connected. Multiple YouTube broadcasts are active; choose one in DSK Streaming.");
	return QStringLiteral("YouTube RTMP is connected. YouTube API start needs attention.");
}

inline QString userFacingYouTubePreflightWarningText(const QString &message)
{
	if (message.startsWith(QStringLiteral("YouTube token refresh failed")))
		return QStringLiteral("YouTube login could not be refreshed. Video was not sent.");
	if (message.contains(QStringLiteral("quota"), Qt::CaseInsensitive))
		return QStringLiteral("YouTube API quota is exhausted. Video was not sent, so choose or start the broadcast in YouTube Studio.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts")))
		return QStringLiteral("Choose the YouTube broadcast in DSK Streaming before video is sent.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: another broadcast")))
		return QStringLiteral("Another broadcast using this stream key has Auto-start enabled. Video was not sent; disable Auto-start on the other broadcast and retry.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: selected broadcast")))
		return QStringLiteral("The selected YouTube broadcast is no longer available. Video was not sent; choose another broadcast.");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: no active broadcast matched")))
		return QStringLiteral("No YouTube broadcast matched this stream key. Video was not sent.");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no broadcasts")))
		return QStringLiteral("No YouTube broadcast was available. Video was not sent.");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no bound stream")))
		return QStringLiteral("No bound YouTube stream was available. Video was not sent.");
	return QStringLiteral("The YouTube broadcast could not be confirmed. Video was not sent.");
}

inline QString liveYouTubeApiWarningRowText(const QString &message)
{
	if (message.startsWith(QStringLiteral("YouTube archive rotation failed")))
		return QStringLiteral("YouTube Live - archive split retrying");
	if (message.startsWith(QStringLiteral("YouTube token refresh failed"))) {
		if (message.contains(QStringLiteral("invalid_grant"), Qt::CaseInsensitive))
			return QStringLiteral("RTMP only - login expired");
		return QStringLiteral("Live - YouTube login warning");
	}
	if (message.contains(QStringLiteral("quota"), Qt::CaseInsensitive))
		return QStringLiteral("Live - YouTube API quota warning");
	if (message.startsWith(QStringLiteral("YouTube API start failed")))
		return QStringLiteral("Live signal - API did not confirm live");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup blocked")))
		return QStringLiteral("Live signal - broadcast list too large");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no broadcasts")))
		return QStringLiteral("Live signal - no broadcast found");
	if (message.startsWith(QStringLiteral("YouTube broadcast lookup found no bound stream")))
		return QStringLiteral("Live signal - no bound stream");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: no active broadcast matched")))
		return QStringLiteral("Live signal - stream key mismatch");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: selected broadcast")))
		return QStringLiteral("Live signal - selected broadcast unavailable");
	if (message.startsWith(QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts")))
		return QStringLiteral("Live signal - multiple broadcasts");
	return QStringLiteral("Live - YouTube API warning");
}

} // namespace dsk
