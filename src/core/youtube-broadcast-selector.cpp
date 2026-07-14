#include "core/youtube-broadcast-selector.hpp"

#include <QVector>

namespace dsk {

YouTubeBroadcastSelection selectYouTubeBroadcast(const QJsonArray &broadcasts,
						  const QHash<QString, QJsonObject> &streamsById,
						  const QString &targetStreamKey)
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
		if (lifecycle == QStringLiteral("complete") || lifecycle == QStringLiteral("revoked"))
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
		if (streamStatus != QStringLiteral("active"))
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

	if (cleanKey.isEmpty()) {
		if (activeBroadcasts.size() == 1)
			return {YouTubeBroadcastSelectionState::Selected, activeBroadcasts.first()};
		if (activeBroadcasts.size() > 1)
			return {YouTubeBroadcastSelectionState::MultipleActiveBroadcasts, {}};
		return {};
	}

	if (matchingBroadcasts.size() == 1)
		return {YouTubeBroadcastSelectionState::Selected, matchingBroadcasts.first()};
	if (matchingBroadcasts.size() > 1)
		return {YouTubeBroadcastSelectionState::MultipleStreamKeyMatches, {}};
	if (!activeBroadcasts.isEmpty())
		return {YouTubeBroadcastSelectionState::NoStreamKeyMatch, {}};
	return {};
}

} // namespace dsk
