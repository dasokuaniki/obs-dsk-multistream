#include "core/youtube-archive-rotation.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>

#include <iostream>

namespace {

int failures = 0;
int scenarios = 0;

void verify(bool condition, const char *message)
{
	++scenarios;
	if (condition)
		return;
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

QJsonObject virtualBroadcast(const QString &id, const QString &title,
			     const QString &actualStartTime = {})
{
	QJsonObject snippet{
		{QStringLiteral("title"), title},
		{QStringLiteral("description"), QStringLiteral("Virtual archive simulation")},
	};
	if (!actualStartTime.isEmpty())
		snippet.insert(QStringLiteral("actualStartTime"), actualStartTime);
	return QJsonObject{
		{QStringLiteral("id"), id},
		{QStringLiteral("snippet"), snippet},
		{QStringLiteral("status"),
		 QJsonObject{{QStringLiteral("lifeCycleStatus"), QStringLiteral("live")},
			     {QStringLiteral("privacyStatus"), QStringLiteral("unlisted")},
			     {QStringLiteral("selfDeclaredMadeForKids"), false}}},
		{QStringLiteral("contentDetails"),
		 QJsonObject{{QStringLiteral("boundStreamId"), QStringLiteral("reusable-stream")},
			     {QStringLiteral("enableDvr"), true},
			     {QStringLiteral("recordFromStart"), true},
			     {QStringLiteral("latencyPreference"), QStringLiteral("low")}}},
	};
}

void simulateConsecutiveArchiveRotation()
{
	using namespace dsk;

	const qint64 sessionStartMs =
		QDateTime::fromString(QStringLiteral("2026-07-27T00:00:00Z"), Qt::ISODate)
			.toMSecsSinceEpoch();
	qint64 archiveStartMs = sessionStartMs;
	QJsonObject current = virtualBroadcast(QStringLiteral("archive00001"),
					       QStringLiteral("Virtual stream"),
					       QStringLiteral("2026-07-27T00:00:00Z"));

	for (int nextPart = 2; nextPart <= 6; ++nextPart) {
		const qint64 deadlineMs = youtubeArchiveRotationDeadlineMs(archiveStartMs);
		verify(youtubeArchiveRotationDelayMs(archiveStartMs, deadlineMs - 1) == 1,
		       "rotation remains armed immediately before the deadline");
		verify(youtubeArchiveRotationDelayMs(archiveStartMs, deadlineMs) == 0,
		       "rotation fires exactly at the deadline");

		const QString scheduledStart =
			QDateTime::fromMSecsSinceEpoch(deadlineMs, Qt::UTC).addSecs(5).toString(Qt::ISODate);
		const QJsonObject insertBody =
			youtubeArchiveBroadcastInsertBody(current, nextPart, scheduledStart);
		const QJsonObject nextSnippet = insertBody.value(QStringLiteral("snippet")).toObject();
		const QJsonObject nextDetails =
			insertBody.value(QStringLiteral("contentDetails")).toObject();
		verify(nextSnippet.value(QStringLiteral("title")).toString().endsWith(
			       QStringLiteral("(Part %1)").arg(nextPart)),
		       "each virtual archive receives the next part number");
		verify(!nextDetails.value(QStringLiteral("enableAutoStart")).toBool(true) &&
			       nextDetails.value(QStringLiteral("enableAutoStop")).toBool(false),
		       "DSK controls each transition while YouTube ends the final archive after RTMP stops");
		verify(nextDetails.value(QStringLiteral("enableDvr")).toBool(false) &&
			       nextDetails.value(QStringLiteral("recordFromStart")).toBool(false),
		       "archive recording settings survive each virtual transition");

		current = virtualBroadcast(
			QStringLiteral("archive%1").arg(nextPart, 5, 10, QLatin1Char('0')),
			nextSnippet.value(QStringLiteral("title")).toString());
		archiveStartMs = youtubeBroadcastActualStartMs(
			current,
			youtubeArchiveRotationFallbackStartMs(true, sessionStartMs, deadlineMs));
		verify(archiveStartMs == deadlineMs,
		       "a missing actualStartTime after rotation cannot trigger an immediate second split");
		verify(youtubeArchiveRotationDelayMs(archiveStartMs, archiveStartMs) ==
			       YouTubeArchiveRotationIntervalMs,
		       "every new virtual archive is armed for another full 11 hours 30 minutes");
	}
}

void simulateRestartRecovery()
{
	using namespace dsk;

	const qint64 actualStartMs =
		QDateTime::fromString(QStringLiteral("2026-07-27T00:00:00Z"), Qt::ISODate)
			.toMSecsSinceEpoch();
	const QJsonObject restored = virtualBroadcast(
		QStringLiteral("archive00001"), QStringLiteral("Virtual stream"),
		QStringLiteral("2026-07-27T00:00:00Z"));
	const qint64 restartAtMs = actualStartMs + 7 * 60 * 60 * 1000LL;
	const qint64 restoredStartMs = youtubeBroadcastActualStartMs(restored, restartAtMs);
	verify(youtubeArchiveRotationDelayMs(restoredStartMs, restartAtMs) ==
		       (4 * 60 + 30) * 60 * 1000LL,
	       "restart recovery preserves the remaining four hours thirty minutes");
}

void simulateFailureSafety()
{
	using namespace dsk;

	verify(youtubeArchiveFailureAction(false, false, 0, 30) ==
		       YouTubeArchiveFailureAction::RetryLaterCurrentLive,
	       "create or bind failure keeps the current archive live");
	verify(youtubeArchiveFailureAction(true, false, 0, 30) ==
		       YouTubeArchiveFailureAction::ConfirmCurrentState,
	       "an ambiguous completion response is confirmed before proceeding");
	verify(youtubeArchiveFailureAction(true, false, 29, 30) ==
		       YouTubeArchiveFailureAction::ConfirmCurrentState,
	       "confirmation remains bounded but allows the final poll");
	verify(youtubeArchiveFailureAction(true, false, 30, 30) ==
		       YouTubeArchiveFailureAction::NeedsAttention,
	       "confirmation stops at the poll limit");
	verify(youtubeArchiveFailureAction(true, true, 0, 30) ==
		       YouTubeArchiveFailureAction::NeedsAttention,
	       "a confirmed-complete current archive never enters an infinite retry loop");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	qunsetenv("DSK_YOUTUBE_ARCHIVE_ROTATION_MINUTES");

	verify(dsk::youtubeArchiveRotationIntervalMs() ==
		       dsk::YouTubeArchiveRotationIntervalMs,
	       "production interval is 11 hours 30 minutes");
	simulateConsecutiveArchiveRotation();
	simulateRestartRecovery();
	simulateFailureSafety();

	if (failures > 0) {
		std::cerr << failures << " of " << scenarios
			  << " virtual archive checks failed\n";
		return 1;
	}
	std::cout << "Passed " << scenarios
		  << " virtual YouTube archive checks across five consecutive splits, "
		     "restart recovery, and bounded failure handling.\n";
	return 0;
}
