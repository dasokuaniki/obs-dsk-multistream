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

QString streamKeyForBroadcast(const QJsonObject &broadcast,
			      const QHash<QString, QJsonObject> &streamsById)
{
	const QString streamId = broadcast.value(QStringLiteral("contentDetails"))
					 .toObject()
					 .value(QStringLiteral("boundStreamId"))
					 .toString();
	return streamsById.value(streamId)
		.value(QStringLiteral("cdn"))
		.toObject()
		.value(QStringLiteral("ingestionInfo"))
		.toObject()
		.value(QStringLiteral("streamName"))
		.toString()
		.trimmed();
}

YouTubeBroadcastSelection selectedBroadcastResult(
	const QJsonObject &selected,
	const QVector<QJsonObject> &eligibleBroadcasts,
	const QHash<QString, QJsonObject> &streamsById)
{
	const QString selectedKey = streamKeyForBroadcast(selected, streamsById);
	QVector<QJsonObject> sameStreamCandidates;
	for (const QJsonObject &candidate : eligibleBroadcasts) {
		if (candidate == selected ||
		    (!selectedKey.isEmpty() && streamKeyForBroadcast(candidate, streamsById) == selectedKey))
			sameStreamCandidates.push_back(candidate);
	}
	return {YouTubeBroadcastSelectionState::Selected, selected, sameStreamCandidates, selectedKey};
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

	const QString cleanPreferredId = preferredBroadcastId.trimmed();
	if (mode == YouTubeBroadcastSelectionMode::Preflight) {
		if (!cleanPreferredId.isEmpty()) {
			for (const QJsonObject &broadcast : activeBroadcasts) {
				if (broadcast.value(QStringLiteral("id")).toString() == cleanPreferredId)
					return selectedBroadcastResult(broadcast, activeBroadcasts, streamsById);
			}
			return {YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable, {}, activeBroadcasts, {}};
		}
		if (activeBroadcasts.size() == 1)
			return selectedBroadcastResult(activeBroadcasts.first(), activeBroadcasts, streamsById);
		if (activeBroadcasts.size() > 1)
			return {YouTubeBroadcastSelectionState::MultipleActiveBroadcasts, {}, activeBroadcasts, {}};
		return {};
	}

	const QVector<QJsonObject> eligibleBroadcasts = cleanKey.isEmpty() ? activeBroadcasts : matchingBroadcasts;
	if (!cleanPreferredId.isEmpty()) {
		for (const QJsonObject &broadcast : eligibleBroadcasts) {
			if (broadcast.value(QStringLiteral("id")).toString() == cleanPreferredId)
				return selectedBroadcastResult(broadcast, eligibleBroadcasts, streamsById);
		}
		return {YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable, {}, eligibleBroadcasts, {}};
	}

	if (cleanKey.isEmpty()) {
		if (activeBroadcasts.size() == 1)
			return selectedBroadcastResult(activeBroadcasts.first(), activeBroadcasts, streamsById);
		if (activeBroadcasts.size() > 1)
			return {YouTubeBroadcastSelectionState::MultipleActiveBroadcasts, {}, activeBroadcasts, {}};
		return {};
	}

	if (matchingBroadcasts.size() == 1)
		return selectedBroadcastResult(matchingBroadcasts.first(), matchingBroadcasts, streamsById);
	if (matchingBroadcasts.size() > 1)
		return {YouTubeBroadcastSelectionState::MultipleStreamKeyMatches, {}, matchingBroadcasts, {}};
	if (!activeBroadcasts.isEmpty())
		return {YouTubeBroadcastSelectionState::NoStreamKeyMatch, {}, activeBroadcasts, {}};
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
