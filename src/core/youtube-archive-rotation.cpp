#include "core/youtube-archive-rotation.hpp"

#include <QDateTime>
#include <QRegularExpression>

namespace dsk {

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
	QJsonObject snippet;
	snippet.insert(QStringLiteral("title"),
		       youtubeNextArchiveTitle(currentSnippet.value(QStringLiteral("title")).toString(), nextPart));
	snippet.insert(QStringLiteral("description"), currentSnippet.value(QStringLiteral("description")).toString());
	snippet.insert(QStringLiteral("scheduledStartTime"), scheduledStartTimeUtc);

	const QJsonObject currentStatus = currentBroadcast.value(QStringLiteral("status")).toObject();
	QJsonObject status;
	status.insert(QStringLiteral("privacyStatus"),
		      currentStatus.value(QStringLiteral("privacyStatus")).toString(QStringLiteral("private")));
	if (currentStatus.value(QStringLiteral("selfDeclaredMadeForKids")).isBool())
		status.insert(QStringLiteral("selfDeclaredMadeForKids"),
			      currentStatus.value(QStringLiteral("selfDeclaredMadeForKids")));

	const QJsonObject currentDetails = currentBroadcast.value(QStringLiteral("contentDetails")).toObject();
	QJsonObject details;
	details.insert(QStringLiteral("enableAutoStart"), false);
	details.insert(QStringLiteral("enableAutoStop"), true);
	details.insert(QStringLiteral("monitorStream"),
		       QJsonObject{{QStringLiteral("enableMonitorStream"), false}});
	for (const QString &field : {QStringLiteral("enableDvr"), QStringLiteral("recordFromStart")}) {
		if (currentDetails.value(field).isBool())
			details.insert(field, currentDetails.value(field));
	}
	const QString latency = currentDetails.value(QStringLiteral("latencyPreference")).toString();
	if (latency == QStringLiteral("normal") || latency == QStringLiteral("low") ||
	    latency == QStringLiteral("ultraLow"))
		details.insert(QStringLiteral("latencyPreference"), latency);

	return QJsonObject{
		{QStringLiteral("snippet"), snippet},
		{QStringLiteral("status"), status},
		{QStringLiteral("contentDetails"), details},
	};
}

} // namespace dsk
