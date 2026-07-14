#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace dsk {

enum class YouTubeBroadcastSelectionState {
	NoActiveBroadcast,
	Selected,
	MultipleActiveBroadcasts,
	NoStreamKeyMatch,
	MultipleStreamKeyMatches,
};

struct YouTubeBroadcastSelection {
	YouTubeBroadcastSelectionState state = YouTubeBroadcastSelectionState::NoActiveBroadcast;
	QJsonObject broadcast;
};

YouTubeBroadcastSelection selectYouTubeBroadcast(const QJsonArray &broadcasts,
						  const QHash<QString, QJsonObject> &streamsById,
						  const QString &targetStreamKey);

} // namespace dsk
