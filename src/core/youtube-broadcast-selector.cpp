#include "core/youtube-broadcast-selector.hpp"

#include <QVector>

namespace dsk {

namespace {

bool isTransitionableLifecycle(const QString &lifecycle)
{
	return lifecycle == QStringLiteral("ready") || lifecycle == QStringLiteral("testing") ||
	       lifecycle == QStringLiteral("testStarting") || lifecycle == QStringLiteral("liveStarting") ||
	       lifecycle == QStringLiteral("live");
}

} // namespace

YouTubeBroadcastSelection selectYouTubeBroadcast(const QJsonArray &broadcasts,
						  const QHash<QString, QJsonObject> &streamsById,
						  const QString &targetStreamKey,
						  const QString &preferredBroadcastId,
						  YouTubeBroadcastSelectionMode mode)
{
	QVector<QJsonObject> activeBroadcasts;
	QVector<QJsonObject> matchingBroadcasts;
	const QString cleanKey = targetStreamKey.trimmed();

	for (const QJsonValue &value : broadcasts) {
		const QJsonObject broadcast = value.toObject();
		const QString lifecycle = broadcast.value(QStringLiteral("status"))
						  .toObject()
						  .value(QStringLiteral("lifeCycleStatus"))
						  .toString();
		if (!isTransitionableLifecycle(lifecycle))
			continue;

		const QString streamId = broadcast.value(QStringLiteral("contentDetails"))
						 .toObject()
						 .value(QStringLiteral("boundStreamId"))
						 .toString();
		const QJsonObject stream = streamsById.value(streamId);
		const QString streamStatus = stream.value(QStringLiteral("status"))
						   .toObject()
						   .value(QStringLiteral("streamStatus"))
						   .toString();
		if (mode == YouTubeBroadcastSelectionMode::ActiveSignal &&
		    streamStatus != QStringLiteral("active"))
			continue;

		activeBroadcasts.push_back(broadcast);
		const QString streamName = stream.value(QStringLiteral("cdn"))
						 .toObject()
						 .value(QStringLiteral("ingestionInfo"))
						 .toObject()
						 .value(QStringLiteral("streamName"))
						 .toString();
		if (!cleanKey.isEmpty() && streamName == cleanKey)
			matchingBroadcasts.push_back(broadcast);
	}

	const QVector<QJsonObject> eligibleBroadcasts = cleanKey.isEmpty() ? activeBroadcasts : matchingBroadcasts;
	const QString cleanPreferredId = preferredBroadcastId.trimmed();
	if (!cleanPreferredId.isEmpty()) {
		for (const QJsonObject &broadcast : eligibleBroadcasts) {
			if (broadcast.value(QStringLiteral("id")).toString() == cleanPreferredId)
				return {YouTubeBroadcastSelectionState::Selected, broadcast, eligibleBroadcasts};
		}
		return {YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable, {}, eligibleBroadcasts};
	}

	if (cleanKey.isEmpty()) {
		if (activeBroadcasts.size() == 1)
			return {YouTubeBroadcastSelectionState::Selected, activeBroadcasts.first(), activeBroadcasts};
		if (activeBroadcasts.size() > 1)
			return {YouTubeBroadcastSelectionState::MultipleActiveBroadcasts, {}, activeBroadcasts};
		return {};
	}

	if (matchingBroadcasts.size() == 1)
		return {YouTubeBroadcastSelectionState::Selected, matchingBroadcasts.first(), matchingBroadcasts};
	if (matchingBroadcasts.size() > 1)
		return {YouTubeBroadcastSelectionState::MultipleStreamKeyMatches, {}, matchingBroadcasts};
	if (!activeBroadcasts.isEmpty())
		return {YouTubeBroadcastSelectionState::NoStreamKeyMatch, {}, activeBroadcasts};
	return {};
}

bool youtubeHasConflictingAutoStart(const QVector<QJsonObject> &candidates,
				    const QString &selectedBroadcastId)
{
	const QString cleanSelectedId = selectedBroadcastId.trimmed();
	if (cleanSelectedId.isEmpty())
		return false;

	for (const QJsonObject &candidate : candidates) {
		const QString candidateId = candidate.value(QStringLiteral("id")).toString().trimmed();
		if (candidateId.isEmpty() || candidateId == cleanSelectedId)
			continue;
		if (candidate.value(QStringLiteral("contentDetails"))
			    .toObject()
			    .value(QStringLiteral("enableAutoStart"))
			    .toBool(false))
			return true;
	}
	return false;
}

} // namespace dsk
