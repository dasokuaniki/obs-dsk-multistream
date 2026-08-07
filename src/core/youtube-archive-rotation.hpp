#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QString>

namespace dsk {

constexpr qint64 YouTubeArchiveRotationIntervalMs = (11 * 60 + 30) * 60 * 1000LL;

enum class YouTubeArchiveFailureAction {
	RetryLaterCurrentLive,
	ConfirmCurrentState,
	NeedsAttention,
};

qint64 youtubeArchiveRotationIntervalMs();
qint64 youtubeArchiveRotationDeadlineMs(qint64 actualStartMs);
qint64 youtubeArchiveRotationDelayMs(qint64 actualStartMs, qint64 nowMs);
qint64 youtubeArchiveRotationFallbackStartMs(bool previousArchiveConfirmedComplete,
					     qint64 sessionStartedAtMs, qint64 nowMs);
YouTubeArchiveFailureAction youtubeArchiveFailureAction(bool currentBroadcastMayBeComplete,
							 bool currentBroadcastConfirmedComplete,
							 int pollCount, int maxPolls);
qint64 youtubeBroadcastActualStartMs(const QJsonObject &broadcast, qint64 fallbackMs);
QString youtubeNextArchiveTitle(const QString &currentTitle, int nextPart);
int youtubeNextArchivePart(const QString &currentTitle);
QJsonObject youtubeArchiveBroadcastInsertBody(const QJsonObject &currentBroadcast, int nextPart,
					      const QString &scheduledStartTimeUtc);
QJsonObject youtubeMostRecentReusableCompletedBroadcast(
	const QJsonArray &broadcasts, const QHash<QString, QJsonObject> &streamsById,
	const QString &targetStreamKey = {});
QJsonObject youtubeReusedBroadcastInsertBody(const QJsonObject &completedBroadcast,
					     const QString &scheduledStartTimeUtc);

} // namespace dsk
