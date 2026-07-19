#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsk {

enum class YouTubeBroadcastSelectionState {
	NoActiveBroadcast,
	Selected,
	MultipleActiveBroadcasts,
	NoStreamKeyMatch,
	MultipleStreamKeyMatches,
	PreferredBroadcastUnavailable,
};

struct YouTubeBroadcastSelection {
	YouTubeBroadcastSelectionState state = YouTubeBroadcastSelectionState::NoActiveBroadcast;
	QJsonObject broadcast;
	QVector<QJsonObject> candidates;
};

YouTubeBroadcastSelection selectYouTubeBroadcast(const QJsonArray &broadcasts,
						  const QHash<QString, QJsonObject> &streamsById,
						  const QString &targetStreamKey,
						  const QString &preferredBroadcastId = {});

} // namespace dsk
