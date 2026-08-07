#include "core/youtube-archive-rotation.hpp"

#include <QDateTime>
#include <QRegularExpression>

namespace dsk {

namespace {

QJsonObject youtubeBroadcastInsertBody(const QJsonObject &sourceBroadcast,
				       const QString &title,
				       const QString &scheduledStartTimeUtc)
{
	const QJsonObject sourceSnippet = sourceBroadcast.value(QStringLiteral("snippet")).toObject();
	QJsonObject snippet;
	snippet.insert(QStringLiteral("title"), title);
	snippet.insert(QStringLiteral("description"), sourceSnippet.value(QStringLiteral("description")).toString());
	snippet.insert(QStringLiteral("scheduledStartTime"), scheduledStartTimeUtc);

	const QJsonObject sourceStatus = sourceBroadcast.value(QStringLiteral("status")).toObject();
	QJsonObject status;
	status.insert(QStringLiteral("privacyStatus"),
		      sourceStatus.value(QStringLiteral("privacyStatus")).toString(QStringLiteral("private")));
	if (sourceStatus.value(QStringLiteral("selfDeclaredMadeForKids")).isBool())
		status.insert(QStringLiteral("selfDeclaredMadeForKids"),
			      sourceStatus.value(QStringLiteral("selfDeclaredMadeForKids")));

	const QJsonObject sourceDetails = sourceBroadcast.value(QStringLiteral("contentDetails")).toObject();
	QJsonObject details;
	details.insert(QStringLiteral("enableAutoStart"), false);
	details.insert(QStringLiteral("enableAutoStop"), true);
	details.insert(QStringLiteral("monitorStream"),
		       QJsonObject{{QStringLiteral("enableMonitorStream"), false}});
	for (const QString &field : {QStringLiteral("enableDvr"), QStringLiteral("recordFromStart")}) {
		if (sourceDetails.value(field).isBool())
			details.insert(field, sourceDetails.value(field));
	}
	const QString latency = sourceDetails.value(QStringLiteral("latencyPreference")).toString();
	if (latency == QStringLiteral("normal") || latency == QStringLiteral("low") ||
	    latency == QStringLiteral("ultraLow"))
		details.insert(QStringLiteral("latencyPreference"), latency);

	return QJsonObject{
		{QStringLiteral("snippet"), snippet},
		{QStringLiteral("status"), status},
		{QStringLiteral("contentDetails"), details},
	};
}

qint64 youtubeCompletedBroadcastTimeMs(const QJsonObject &broadcast)
{
	const QJsonObject snippet = broadcast.value(QStringLiteral("snippet")).toObject();
	for (const QString &field : {QStringLiteral("actualEndTime"),
				     QStringLiteral("actualStartTime"),
				     QStringLiteral("scheduledStartTime")}) {
		const QDateTime parsed = QDateTime::fromString(snippet.value(field).toString(), Qt::ISODate);
		if (parsed.isValid())
			return parsed.toMSecsSinceEpoch();
	}
	return 0;
}

} // namespace

qint64 youtubeArchiveRotationIntervalMs()
{
	bool parsed = false;
	const int minutes = qEnvironmentVariableIntValue("DSK_YOUTUBE_ARCHIVE_ROTATION_MINUTES", &parsed);
	constexpr int DefaultMinutes = 11 * 60 + 30;
	if (!parsed || minutes < 1 || minutes > DefaultMinutes)
		return YouTubeArchiveRotationIntervalMs;
	return qint64(minutes) * 60 * 1000;
}

qint64 youtubeArchiveRotationDeadlineMs(qint64 actualStartMs)
{
	return actualStartMs > 0 ? actualStartMs + youtubeArchiveRotationIntervalMs() : 0;
}

qint64 youtubeArchiveRotationDelayMs(qint64 actualStartMs, qint64 nowMs)
{
	return qMax<qint64>(0, youtubeArchiveRotationDeadlineMs(actualStartMs) - nowMs);
}

qint64 youtubeArchiveRotationFallbackStartMs(bool previousArchiveConfirmedComplete,
					     qint64 sessionStartedAtMs, qint64 nowMs)
{
	if (previousArchiveConfirmedComplete || sessionStartedAtMs <= 0)
		return nowMs;
	return sessionStartedAtMs;
}

YouTubeArchiveFailureAction youtubeArchiveFailureAction(bool currentBroadcastMayBeComplete,
							 bool currentBroadcastConfirmedComplete,
							 int pollCount, int maxPolls)
{
	if (!currentBroadcastMayBeComplete)
		return YouTubeArchiveFailureAction::RetryLaterCurrentLive;
	if (currentBroadcastConfirmedComplete || maxPolls <= 0 || pollCount >= maxPolls)
		return YouTubeArchiveFailureAction::NeedsAttention;
	return YouTubeArchiveFailureAction::ConfirmCurrentState;
}

qint64 youtubeBroadcastActualStartMs(const QJsonObject &broadcast, qint64 fallbackMs)
{
	const QString value = broadcast.value(QStringLiteral("snippet"))
				      .toObject()
				      .value(QStringLiteral("actualStartTime"))
				      .toString()
				      .trimmed();
	const QDateTime parsed = QDateTime::fromString(value, Qt::ISODate);
	return parsed.isValid() ? parsed.toMSecsSinceEpoch() : fallbackMs;
}

QString youtubeNextArchiveTitle(const QString &currentTitle, int nextPart)
{
	QString base = currentTitle.trimmed();
	static const QRegularExpression suffix(
		QStringLiteral(R"(\s*[\(（]Part\s+\d+[\)）]\s*$)"),
		QRegularExpression::CaseInsensitiveOption);
	base.remove(suffix);
	if (base.isEmpty())
		base = QStringLiteral("Live stream");
	const QString partSuffix = QStringLiteral(" (Part %1)").arg(qMax(2, nextPart));
	const int maxBaseLength = qMax(1, 100 - partSuffix.size());
	if (base.size() > maxBaseLength)
		base = base.left(maxBaseLength).trimmed();
	return base + partSuffix;
}

int youtubeNextArchivePart(const QString &currentTitle)
{
	static const QRegularExpression suffix(
		QStringLiteral(R"([\(（]Part\s+(\d+)[\)）]\s*$)"),
		QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = suffix.match(currentTitle.trimmed());
	bool ok = false;
	const int currentPart = match.hasMatch() ? match.captured(1).toInt(&ok) : 0;
	return ok && currentPart >= 1 && currentPart < 9999 ? currentPart + 1 : 2;
}

QJsonObject youtubeArchiveBroadcastInsertBody(const QJsonObject &currentBroadcast, int nextPart,
					      const QString &scheduledStartTimeUtc)
{
	const QJsonObject currentSnippet = currentBroadcast.value(QStringLiteral("snippet")).toObject();
	return youtubeBroadcastInsertBody(
		currentBroadcast,
		youtubeNextArchiveTitle(currentSnippet.value(QStringLiteral("title")).toString(), nextPart),
		scheduledStartTimeUtc);
}

QJsonObject youtubeMostRecentReusableCompletedBroadcast(
	const QJsonArray &broadcasts, const QHash<QString, QJsonObject> &streamsById,
	const QString &targetStreamKey)
{
	const QString cleanTargetKey = targetStreamKey.trimmed();
	QJsonObject selected;
	qint64 selectedTimeMs = -1;
	QString selectedId;
	for (const QJsonValue &value : broadcasts) {
		const QJsonObject broadcast = value.toObject();
		if (broadcast.value(QStringLiteral("status"))
			    .toObject()
			    .value(QStringLiteral("lifeCycleStatus"))
			    .toString() != QStringLiteral("complete"))
			continue;
		const QString streamId = broadcast.value(QStringLiteral("contentDetails"))
					 .toObject()
					 .value(QStringLiteral("boundStreamId"))
					 .toString()
					 .trimmed();
		const QJsonObject stream = streamsById.value(streamId);
		if (streamId.isEmpty() || stream.isEmpty() ||
		    !stream.value(QStringLiteral("contentDetails"))
			    .toObject()
			    .value(QStringLiteral("isReusable"))
			    .toBool(false))
			continue;
		const QString streamKey = stream.value(QStringLiteral("cdn"))
					  .toObject()
					  .value(QStringLiteral("ingestionInfo"))
					  .toObject()
					  .value(QStringLiteral("streamName"))
					  .toString()
					  .trimmed();
		if (streamKey.isEmpty() || (!cleanTargetKey.isEmpty() && streamKey != cleanTargetKey))
			continue;

		const qint64 candidateTimeMs = youtubeCompletedBroadcastTimeMs(broadcast);
		const QString candidateId = broadcast.value(QStringLiteral("id")).toString().trimmed();
		if (candidateId.isEmpty())
			continue;
		if (selected.isEmpty() || candidateTimeMs > selectedTimeMs ||
		    (candidateTimeMs == selectedTimeMs && candidateId > selectedId)) {
			selected = broadcast;
			selectedTimeMs = candidateTimeMs;
			selectedId = candidateId;
		}
	}
	return selected;
}

QJsonObject youtubeReusedBroadcastInsertBody(const QJsonObject &completedBroadcast,
					     const QString &scheduledStartTimeUtc)
{
	QString title = completedBroadcast.value(QStringLiteral("snippet"))
				.toObject()
				.value(QStringLiteral("title"))
				.toString()
				.trimmed();
	if (title.isEmpty())
		title = QStringLiteral("Live stream");
	if (title.size() > 100)
		title = title.left(100).trimmed();
	return youtubeBroadcastInsertBody(completedBroadcast, title, scheduledStartTimeUtc);
}

} // namespace dsk
