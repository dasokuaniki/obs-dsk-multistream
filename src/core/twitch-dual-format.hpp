#pragma once

#include "core/output-target.hpp"

#include <QString>
#include <QStringView>
#include <QUrl>

namespace dsk {

enum class TwitchDualFormatState {
	NotTwitch,
	VerticalCanvasUnavailable,
	EnhancedBroadcastingDisabled,
	VerticalCanvasNotSelected,
	Ready,
};

inline TwitchDualFormatState twitchDualFormatState(QStringView nativePlatformId,
						    bool enhancedBroadcastingEnabled,
						    QStringView selectedCanvasUuid,
						    QStringView verticalCanvasUuid)
{
	if (nativePlatformId.toString().trimmed().compare(QStringLiteral("twitch"), Qt::CaseInsensitive) != 0)
		return TwitchDualFormatState::NotTwitch;
	if (verticalCanvasUuid.trimmed().isEmpty())
		return TwitchDualFormatState::VerticalCanvasUnavailable;
	if (!enhancedBroadcastingEnabled)
		return TwitchDualFormatState::EnhancedBroadcastingDisabled;
	if (selectedCanvasUuid.trimmed().isEmpty() ||
	    selectedCanvasUuid.toString().trimmed().compare(verticalCanvasUuid.toString().trimmed(),
							       Qt::CaseInsensitive) != 0)
		return TwitchDualFormatState::VerticalCanvasNotSelected;
	return TwitchDualFormatState::Ready;
}

inline bool twitchDualFormatActive(TwitchDualFormatState state)
{
	return state == TwitchDualFormatState::Ready;
}

inline bool shouldDeferVerticalCanvasRelease(bool obsNativeUsingVerticalCanvas, bool shuttingDown)
{
	return obsNativeUsingVerticalCanvas && !shuttingDown;
}

inline bool isObsNativeTwitchService(QStringView serviceName, QStringView serviceType, QStringView serviceId)
{
	const auto isCustomRtmp = [](QStringView value) {
		return value.trimmed().compare(QStringLiteral("rtmp_custom"), Qt::CaseInsensitive) == 0;
	};
	return !isCustomRtmp(serviceType) && !isCustomRtmp(serviceId) &&
	       serviceName.trimmed().compare(QStringLiteral("Twitch"), Qt::CaseInsensitive) == 0;
}

inline bool isTwitchOutputTarget(const OutputTarget &target)
{
	const QString platformId = target.platformId.trimmed();
	if (platformId.compare(QStringLiteral("twitch"), Qt::CaseInsensitive) == 0 ||
	    target.authMode == TargetAuthMode::TwitchOAuth)
		return true;
	// Kick and Twitch can both use Amazon IVS hosts under live-video.net. Respect an
	// explicitly configured non-Twitch platform/auth mode before considering the
	// endpoint as a fallback signal for unclassified manual RTMP targets.
	if ((!platformId.isEmpty() &&
	     platformId.compare(QStringLiteral("custom"), Qt::CaseInsensitive) != 0) ||
	    target.authMode != TargetAuthMode::ManualRtmp)
		return false;

	const QString host = QUrl(target.serverUrl.trimmed()).host().trimmed().toLower();
	return host == QStringLiteral("twitch.tv") || host.endsWith(QStringLiteral(".twitch.tv")) ||
	       host == QStringLiteral("live-video.net") || host.endsWith(QStringLiteral(".live-video.net"));
}

inline bool shouldSuppressIndependentTwitchTarget(const OutputTarget &target, bool dualFormatActive)
{
	return dualFormatActive && isTwitchOutputTarget(target);
}

inline bool shouldBlockIndependentTwitchStart(const OutputTarget &target, bool dualFormatActive,
					       bool alreadyRunning)
{
	return !alreadyRunning && shouldSuppressIndependentTwitchTarget(target, dualFormatActive);
}

} // namespace dsk
