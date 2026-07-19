#pragma once

#include "core/output-target.hpp"
#include "core/youtube-api-warning.hpp"

#include <QString>
#include <QtGlobal>

#include <stdint.h>

namespace dsk {

enum class TransportState {
	Idle,
	Starting,
	Connected,
	Active,
	Reconnecting,
	Stopping,
	Failed,
};

enum class PlatformLiveState {
	NotApplicable,
	Unknown,
	RtmpSignalOnly,
	WaitingForSignal,
	Preparing,
	Testing,
	LiveStarting,
	Live,
	NeedsManualStart,
	AuthExpired,
	QuotaBlocked,
	BroadcastMismatch,
	MultipleBroadcasts,
	Failed,
};

struct TargetRuntimeStatus {
	QString targetId;
	quint64 sessionSerial = 0;
	TransportState transport = TransportState::Idle;
	PlatformLiveState platform = PlatformLiveState::Unknown;
	qint64 startedAtMs = 0;
	qint64 lastChangedAtMs = 0;
	uint64_t totalBytes = 0;
	int totalFrames = 0;
	int droppedFrames = 0;
	float congestion = 0.0f;
	int reconnectDelaySeconds = 0;
	QString streamStatus;
	QString lifeCycleStatus;
	QString broadcastId;
	QString transportMessage;
	QString platformMessage;
	QString lastUserMessage;
	QString lastTechnicalError;
};

inline QString transportStateToString(TransportState state)
{
	switch (state) {
	case TransportState::Idle:
		return QStringLiteral("Idle");
	case TransportState::Starting:
		return QStringLiteral("Starting");
	case TransportState::Connected:
		return QStringLiteral("RTMP sending");
	case TransportState::Active:
		return QStringLiteral("RTMP sending");
	case TransportState::Reconnecting:
		return QStringLiteral("Reconnecting");
	case TransportState::Stopping:
		return QStringLiteral("Stopping");
	case TransportState::Failed:
		return QStringLiteral("Failed");
	}
	return QStringLiteral("Unknown");
}

inline QString platformLiveStateToString(PlatformLiveState state)
{
	switch (state) {
	case PlatformLiveState::NotApplicable:
		return QStringLiteral("Not applicable");
	case PlatformLiveState::Unknown:
		return QStringLiteral("Platform unknown");
	case PlatformLiveState::RtmpSignalOnly:
		return QStringLiteral("RTMP signal only");
	case PlatformLiveState::WaitingForSignal:
		return QStringLiteral("Waiting for YouTube signal");
	case PlatformLiveState::Preparing:
		return QStringLiteral("YouTube preparing");
	case PlatformLiveState::Testing:
		return QStringLiteral("YouTube testing");
	case PlatformLiveState::LiveStarting:
		return QStringLiteral("YouTube going live");
	case PlatformLiveState::Live:
		return QStringLiteral("YouTube Live");
	case PlatformLiveState::NeedsManualStart:
		return QStringLiteral("Manual start needed");
	case PlatformLiveState::AuthExpired:
		return QStringLiteral("Login expired");
	case PlatformLiveState::QuotaBlocked:
		return QStringLiteral("API quota blocked");
	case PlatformLiveState::BroadcastMismatch:
		return QStringLiteral("Broadcast mismatch");
	case PlatformLiveState::MultipleBroadcasts:
		return QStringLiteral("Multiple broadcasts");
	case PlatformLiveState::Failed:
		return QStringLiteral("Platform start failed");
	}
	return QStringLiteral("Platform unknown");
}

inline bool runtimeTransportIsRunning(const TargetRuntimeStatus &status)
{
	return status.transport == TransportState::Starting || status.transport == TransportState::Connected ||
	       status.transport == TransportState::Active || status.transport == TransportState::Reconnecting;
}

inline bool runtimeTransportIsBusy(const TargetRuntimeStatus &status)
{
	return status.transport == TransportState::Starting || status.transport == TransportState::Stopping;
}

inline bool runtimeHasSession(const TargetRuntimeStatus &status)
{
	return status.sessionSerial != 0 && status.transport != TransportState::Idle && status.transport != TransportState::Failed;
}

inline bool runtimePlatformIsWarning(PlatformLiveState state)
{
	return state == PlatformLiveState::RtmpSignalOnly || state == PlatformLiveState::WaitingForSignal ||
	       state == PlatformLiveState::Preparing || state == PlatformLiveState::NeedsManualStart ||
	       state == PlatformLiveState::AuthExpired || state == PlatformLiveState::QuotaBlocked ||
	       state == PlatformLiveState::BroadcastMismatch || state == PlatformLiveState::MultipleBroadcasts ||
	       state == PlatformLiveState::Failed;
}

inline PlatformLiveState platformStateForYouTubeApiWarningText(const QString &message)
{
	const QString clean = message.trimmed();
	if (clean.startsWith(QStringLiteral("YouTube token refresh failed")) ||
	    clean.contains(QStringLiteral("invalid_grant"), Qt::CaseInsensitive))
		return PlatformLiveState::AuthExpired;
	if (clean.contains(QStringLiteral("quota"), Qt::CaseInsensitive))
		return PlatformLiveState::QuotaBlocked;
	if (clean.startsWith(QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts")))
		return PlatformLiveState::MultipleBroadcasts;
	if (clean.startsWith(QStringLiteral("YouTube broadcast start blocked: no active broadcast matched")))
		return PlatformLiveState::BroadcastMismatch;
	if (clean.startsWith(QStringLiteral("YouTube broadcast lookup found no broadcasts")) ||
	    clean.startsWith(QStringLiteral("YouTube broadcast lookup found no bound stream")) ||
	    clean.startsWith(QStringLiteral("YouTube API start unavailable")))
		return PlatformLiveState::NeedsManualStart;
	if (clean.startsWith(QStringLiteral("YouTube API start failed")))
		return PlatformLiveState::NeedsManualStart;
	return PlatformLiveState::Failed;
}

inline QString runtimeStatusLabel(const OutputTarget &target, const TargetRuntimeStatus &status)
{
	if (runtimeTransportIsRunning(status)) {
		if (status.transport == TransportState::Starting)
			return QStringLiteral("Starting");
		if (status.transport == TransportState::Reconnecting)
			return QStringLiteral("Reconnecting");
		if (isYouTubeTarget(target)) {
			if (status.platform == PlatformLiveState::Live)
				return QStringLiteral("YouTube Live");
			if (status.platform == PlatformLiveState::Testing)
				return QStringLiteral("YouTube testing");
			if (status.platform == PlatformLiveState::LiveStarting)
				return QStringLiteral("YouTube starting");
			if (runtimePlatformIsWarning(status.platform))
				return QStringLiteral("RTMP only");
		}
		return transportStateToString(status.transport);
	}
	if (!target.enabled)
		return QStringLiteral("Auto start off");
	if (targetHasExpiredYouTubeLoginWarning(target))
		return QStringLiteral("Needs login");
	if (targetHasYouTubeApiWarning(target))
		return QStringLiteral("API warning");
	if (!target.lastError.trimmed().isEmpty() || target.state == TargetState::Error)
		return QStringLiteral("Failed");
	return targetStateToString(target.state);
}

inline QString runtimeStatusDetail(const OutputTarget &target, const TargetRuntimeStatus &status)
{
	if (status.transport == TransportState::Starting)
		return status.transportMessage.trimmed().isEmpty() ? QStringLiteral("Connecting") : status.transportMessage.trimmed();
	if (status.transport == TransportState::Stopping)
		return status.transportMessage.trimmed().isEmpty() ? QStringLiteral("Stopping") : status.transportMessage.trimmed();
	if (status.transport == TransportState::Reconnecting) {
		if (status.reconnectDelaySeconds > 0)
			return QStringLiteral("Reconnecting in %1 sec").arg(status.reconnectDelaySeconds);
		return status.transportMessage.trimmed().isEmpty() ? QStringLiteral("Reconnecting") : status.transportMessage.trimmed();
	}

	if (runtimeTransportIsRunning(status) && isYouTubeTarget(target) && !status.platformMessage.trimmed().isEmpty())
		return status.platformMessage.trimmed();

	const QString error = target.lastError.trimmed();
	if (!error.isEmpty()) {
		if (targetHasLiveYouTubeApiWarning(target))
			return liveYouTubeApiWarningRowText(error);
		if (targetHasExpiredYouTubeLoginWarning(target))
			return QStringLiteral("RTMP connected - YouTube login expired");
		if (targetHasYouTubeApiWarning(target))
			return userFacingYouTubeApiWarningText(error);
		return error;
	}

	if (runtimeTransportIsRunning(status)) {
		if (isYouTubeTarget(target)) {
			if (!status.platformMessage.trimmed().isEmpty())
				return status.platformMessage.trimmed();
			switch (status.platform) {
			case PlatformLiveState::Live:
				return QStringLiteral("YouTube broadcast is live");
			case PlatformLiveState::Testing:
				return QStringLiteral("YouTube monitor is testing");
			case PlatformLiveState::LiveStarting:
				return QStringLiteral("YouTube is switching to live");
			case PlatformLiveState::WaitingForSignal:
				return QStringLiteral("RTMP connected - waiting for YouTube signal");
			case PlatformLiveState::NeedsManualStart:
				return QStringLiteral("YouTube preparing - start in YouTube Studio");
			case PlatformLiveState::RtmpSignalOnly:
			case PlatformLiveState::Unknown:
				return QStringLiteral("RTMP connected - checking YouTube Live");
			case PlatformLiveState::AuthExpired:
				return QStringLiteral("YouTube preparing - reconnect login");
			case PlatformLiveState::QuotaBlocked:
				return QStringLiteral("YouTube preparing - API quota blocked");
			case PlatformLiveState::BroadcastMismatch:
				return QStringLiteral("YouTube preparing - stream key mismatch");
			case PlatformLiveState::MultipleBroadcasts:
				return QStringLiteral("YouTube preparing - multiple broadcasts");
			case PlatformLiveState::Preparing:
				return QStringLiteral("RTMP connected - YouTube preparing");
			case PlatformLiveState::Failed:
				return QStringLiteral("RTMP connected - YouTube API needs attention");
			case PlatformLiveState::NotApplicable:
				break;
			}
		}
		if (!status.transportMessage.trimmed().isEmpty())
			return status.transportMessage.trimmed();
		if (!status.lastUserMessage.trimmed().isEmpty())
			return status.lastUserMessage.trimmed();
		return transportStateToString(status.transport);
	}

	return runtimeStatusLabel(target, status);
}

} // namespace dsk
