#pragma once

#include <QtGlobal>

namespace dsk {

inline constexpr int CommentViewerMaxProbeAttempts = 30;

enum class CommentViewerProbeAction {
	Ignore,
	Connect,
	Launch,
	Retry,
	GiveUp,
};

inline bool commentViewerIntegrationEnabledAtStartup(bool viewerInstalled)
{
	return viewerInstalled;
}

inline bool shouldReconnectCommentViewerAfterOpen(bool enabled, bool shuttingDown, bool viewerInstalled)
{
	return enabled && !shuttingDown && viewerInstalled;
}

inline CommentViewerProbeAction commentViewerProbeAction(bool enabled, quint64 currentGeneration,
						  quint64 callbackGeneration, bool responseValid,
						  bool launchAttempted, int attempt, int maxAttempts)
{
	if (!enabled || callbackGeneration != currentGeneration)
		return CommentViewerProbeAction::Ignore;
	if (responseValid)
		return CommentViewerProbeAction::Connect;
	if (!launchAttempted)
		return CommentViewerProbeAction::Launch;
	if (attempt >= 0 && attempt + 1 < maxAttempts)
		return CommentViewerProbeAction::Retry;
	return CommentViewerProbeAction::GiveUp;
}

} // namespace dsk
