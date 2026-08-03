#pragma once

#include <QString>

namespace dsk {

inline constexpr int YouTubePreferredBroadcastPropagationMaxRetries = 5;

inline bool shouldIgnoreOutputSignalDuringPendingRelease(const QString &signalName)
{
	return signalName != QStringLiteral("stopping") && signalName != QStringLiteral("deactivate") &&
	       signalName != QStringLiteral("stop");
}

inline bool shouldRetryYouTubePreferredBroadcastAfterRtmp(bool preflight, bool rtmpSignalActive,
							   int attempt)
{
	return !preflight && rtmpSignalActive && attempt >= 0 &&
	       attempt < YouTubePreferredBroadcastPropagationMaxRetries;
}

} // namespace dsk
