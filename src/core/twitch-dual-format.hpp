#pragma once

#include "core/output-target.hpp"

#include <QString>
#include <QStringView>

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

inline bool shouldSuppressIndependentTwitchTarget(const OutputTarget &target, bool dualFormatActive)
{
	return dualFormatActive && target.platformId.trimmed().compare(QStringLiteral("twitch"), Qt::CaseInsensitive) == 0;
}

inline bool shouldBlockIndependentTwitchStart(const OutputTarget &target, bool dualFormatActive,
					       bool alreadyRunning)
{
	return !alreadyRunning && shouldSuppressIndependentTwitchTarget(target, dualFormatActive);
}

} // namespace dsk
